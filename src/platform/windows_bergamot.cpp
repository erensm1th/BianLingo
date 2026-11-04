#include "bianlingo/bergamot.hpp"
#include "bianlingo/core.hpp"
#include "bianlingo/local_models.hpp"
#include "bianlingo/localization.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace bl {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    void reset(HANDLE h = nullptr) {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = h;
    }
};

std::string utf8_path(const fs::path &path) {
    auto bytes = path.u8string();
    return {bytes.begin(), bytes.end()};
}
Bytes read_large(const fs::path &path, uint64_t maximum) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || uint64_t(file.tellg()) > maximum)
        throw std::runtime_error("A Bergamot model file is missing or too large");
    Bytes bytes(size_t(file.tellg()), 0);
    file.seekg(0);
    if (!bytes.empty() && !file.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size())))
        throw std::runtime_error("Could not read the Bergamot model");
    return bytes;
}
std::string html_input(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        constexpr std::string_view marker = "[[BLG_LINK_";
        if (text.substr(i).starts_with(marker)) {
            auto digits = i + marker.size();
            while (digits < text.size() && text[digits] == '_')
                ++digits;
            auto end = text.find("]]", digits);
            auto number = text.substr(digits, end == text.npos ? 0 : end - digits);
            if (end != text.npos && !number.empty() && number.size() <= 3 &&
                std::all_of(number.begin(), number.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                out += "<code>";
                out.append(text.substr(i, end + 2 - i));
                out += "</code>";
                i = end + 2;
                continue;
            }
        }
        switch (text[i++]) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default: out += text[i - 1]; break;
        }
    }
    return out;
}
void replace_all(std::string &text, std::string_view from, std::string_view to) {
    size_t at = 0;
    while ((at = text.find(from, at)) != text.npos) {
        text.replace(at, from.size(), to);
        at += to.size();
    }
}

class Client {
  public:
    Client() : sweeper_([this](std::stop_token token) {
        while (!token.stop_requested()) {
            for (int i = 0; i < 30 && !token.stop_requested(); ++i)
                Sleep(1000);
            std::lock_guard lock(mutex_);
            if (process_.value && Clock::now() - used_ > std::chrono::minutes(10))
                stop();
        }
    }) {}
    ~Client() {
        sweeper_.request_stop();
        sweeper_.join();
        stop();
    }
    std::string translate(std::string_view text, bool incoming, std::string_view target,
                          std::string_view chat_language, StopToken cancel) {
        std::unique_lock lock(mutex_, std::defer_lock);
        while (!lock.try_lock_for(std::chrono::milliseconds(50)))
            if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
        if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
        if (text.empty() || text.size() > 12000) throw std::runtime_error("Bergamot input is empty or too long");
        const auto root = platform::package_root() / "data" / "bergamot";
        const auto cache = platform::app_data() / "bergamot";
        if (!models_) models_ = std::make_unique<BergamotModels>(root, cache);
        auto route = models_->route(local_translation_pair(incoming, text, target, chat_language));
        // Verify all dependencies before translating the first leg. No automatic downloads.
        for (const auto &pair : route) if (!prepared_.contains(pair)) {
            models_->prepare_pair(pair, cancel); prepared_.insert(pair);
        }
        std::string result(text);
        for (const auto &pair : route) result = translate_one(result, pair, root, cache, cancel);
        return result;
    }
    std::string translate_one(std::string_view text, const std::string &pair, const fs::path &root, const fs::path &cache, StopToken cancel) {
        const auto model = cache / "models" / pair;
        if (!fs::is_regular_file(root / "BianLingoBergamot.exe"))
            throw std::runtime_error("Bergamot components are missing. Install the full package with models.");
        if (process_.value && Clock::now() - used_ > std::chrono::minutes(10))
            stop();
        try {
            if (!process_.value)
                start(root, cancel);
            auto directory = utf8_path(model);
            if (directory.find_first_of("\t\r\n") != std::string::npos)
                throw std::runtime_error("The Bergamot model path contains unsupported characters");
            bool html = text.find("[[BLG_LINK_") != text.npos;
            auto input = html ? html_input(text) : std::string(text);
            if (directory.size() > 4096)
                throw std::runtime_error("The Bergamot model path is too long");
            std::string header;
            header += char(pair.starts_with("en-") ? 0 : 1);
            header += char(html ? 1 : 0);
            auto append_u32 = [&header](uint32_t value) {
                for (int shift = 0; shift < 32; shift += 8)
                    header += char(value >> shift);
            };
            append_u32(uint32_t(directory.size()));
            append_u32(uint32_t(input.size()));
            write(header, cancel);
            write(directory, cancel);
            write(input, cancel);
            const auto response = read(5, cancel);
            if (response[0] != 0 && response[0] != 1)
                throw std::runtime_error("Invalid Bergamot response status");
            const size_t length = uint8_t(response[1]) | (size_t(uint8_t(response[2])) << 8) |
                                  (size_t(uint8_t(response[3])) << 16) |
                                  (size_t(uint8_t(response[4])) << 24);
            if (length > 120000)
                throw std::runtime_error("The Bergamot response is too long");
            auto result = read(length, cancel);
            if (response[0] != 0)
                throw std::runtime_error("Bergamot translation failed: " + result);
            if (result.empty())
                throw std::runtime_error("Bergamot returned no translation");
            if (html) {
                replace_all(result, "<code>", "");
                replace_all(result, "</code>", "");
                replace_all(result, "&lt;", "<");
                replace_all(result, "&gt;", ">");
                replace_all(result, "&amp;", "&");
            }
            used_ = Clock::now();
            return result;
        } catch (...) {
            stop();
            throw;
        }
    }

  private:
    std::timed_mutex mutex_;
    Handle process_, input_, output_;
    Clock::time_point used_{};
    std::set<std::string> prepared_;
    std::unique_ptr<BergamotModels> models_;
    bool helper_verified_ = false;
    std::jthread sweeper_;
    void stop() {
        input_.reset();
        output_.reset();
        if (process_.value) {
            if (WaitForSingleObject(process_.value, 0) == WAIT_TIMEOUT)
                TerminateProcess(process_.value, 1);
            process_.reset();
        }
    }
    void start(const fs::path &root, StopToken cancel) {
        if (!helper_verified_) {
            const auto digest_bytes = read_bytes(root / "engine.sha256");
            const auto digest = trim(std::string(digest_bytes.begin(), digest_bytes.end()));
            if (digest.size() != 64 ||
                sha256(read_large(root / "BianLingoBergamot.exe", 32 * 1024 * 1024)) != digest)
                throw std::runtime_error("Bergamot engine verification failed");
            helper_verified_ = true;
        }
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE child_input = nullptr, parent_input = nullptr, parent_output = nullptr, child_output = nullptr;
        if (!CreatePipe(&child_input, &parent_output, &security, 65536))
            throw std::runtime_error("Could not create the Bergamot output pipe");
        Handle read_child(child_input), write_parent(parent_output);
        if (!CreatePipe(&parent_input, &child_output, &security, 65536))
            throw std::runtime_error("Could not create the Bergamot input pipe");
        Handle read_parent(parent_input), write_child(child_output);
        SetHandleInformation(write_parent.value, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(read_parent.value, HANDLE_FLAG_INHERIT, 0);
        Handle child_error(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (child_error.value == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Could not prepare the Bergamot log channel");
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        startup.hStdInput = read_child.value;
        startup.hStdOutput = write_child.value;
        startup.hStdError = child_error.value;
        PROCESS_INFORMATION info{};
        auto exe = root / "BianLingoBergamot.exe";
        auto command = L"\"" + exe.wstring() + L"\"";
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, root.c_str(), &startup, &info))
            throw std::runtime_error("Could not start the local Bergamot engine");
        Handle thread(info.hThread);
        process_.reset(info.hProcess);
        input_.reset(write_parent.value);
        write_parent.value = nullptr;
        output_.reset(read_parent.value);
        read_parent.value = nullptr;
        read_child.reset();
        write_child.reset();
        if (line(cancel) != "BLMT1") {
            stop();
            throw std::runtime_error("Incompatible Bergamot engine version");
        }
        used_ = Clock::now();
    }
    void write(std::string_view value, StopToken cancel) {
        while (!value.empty()) {
            if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
            DWORD sent = 0;
            if (!WriteFile(input_.value, value.data(), DWORD(value.size()), &sent, nullptr) || !sent)
                throw std::runtime_error("The Bergamot input pipe is closed");
            value.remove_prefix(sent);
        }
    }
    std::string read(size_t length, StopToken cancel) {
        std::string value(length, '\0');
        size_t offset = 0;
        auto deadline = Clock::now() + std::chrono::seconds(20);
        while (offset < length) {
            if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
            DWORD available = 0;
            if (!PeekNamedPipe(output_.value, nullptr, 0, nullptr, &available, nullptr))
                throw std::runtime_error("The Bergamot engine exited");
            if (!available) {
                if (Clock::now() >= deadline)
                    throw std::runtime_error("Bergamot translation timed out");
                Sleep(5);
                continue;
            }
            DWORD got = 0;
            if (!ReadFile(output_.value, value.data() + offset,
                          DWORD(std::min<size_t>(available, length - offset)), &got, nullptr) || !got)
                throw std::runtime_error("Could not read the Bergamot translation");
            offset += got;
        }
        return value;
    }
    std::string line(StopToken cancel) {
        std::string result;
        while (result.size() < 512) {
            auto ch = read(1, cancel)[0];
            if (ch == '\n') return result;
            result += ch;
        }
        throw std::runtime_error("The Bergamot response header is too long");
    }
};
} // namespace

std::string bergamot_translate(std::string_view text, bool incoming, std::string_view target,
                               std::string_view chat_language, StopToken cancel) {
    static Client client;
    return client.translate(text, incoming, target, chat_language, cancel);
}
} // namespace bl
