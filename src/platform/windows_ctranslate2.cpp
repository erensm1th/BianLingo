#include "bianlingo/ctranslate2.hpp"
#include "bianlingo/core.hpp"
#include "bianlingo/localization.hpp"
#include "bianlingo/local_models.hpp"
#include "bianlingo/bundle.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace bl {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { reset(); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    void reset(HANDLE h = nullptr) {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = h;
    }
};

class Client {
  public:
    Client() : sweeper_([this](std::stop_token token) {
        while (!token.stop_requested()) {
            for (int i = 0; i < 30 && !token.stop_requested(); ++i) Sleep(1000);
            std::lock_guard lock(mutex_);
            if (process_.value && Clock::now() - used_ > std::chrono::minutes(10)) stop();
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
        if (text.empty() || text.size() > 12000)
            throw std::runtime_error("CTranslate2 input is empty or too long");
        if (!models_) models_.emplace(platform::package_root() / "data" / "ctranslate2", platform::app_data() / "ctranslate2");
        const auto routes = models_->route(local_translation_pair(incoming, text, target, chat_language));
        for (const auto &pair : routes) if (!prepared_.contains(pair)) { models_->prepare_pair(pair, cancel); prepared_.insert(pair); }
        try {
            if (!process_.value) {
                auto root = models_->runtime_directory();
                if (root.empty()) throw std::runtime_error("Prepare the CTranslate2 runtime and language packs in settings");
                start(root, cancel);
            }
            std::string result(text);
            for (const auto &pair : routes) {
            std::string request(1, char(2));
            request += char(pair.size());
            const auto length = uint32_t(result.size());
            for (int shift = 0; shift < 32; shift += 8) request += char(length >> shift);
            request += pair;
            write(request, cancel);
            write(result, cancel);
            const auto response = read(5, cancel);
            if (response[0] != 0 && response[0] != 1)
                throw std::runtime_error("Invalid CTranslate2 response status");
            const auto size = uint32_t(uint8_t(response[1])) |
                              (uint32_t(uint8_t(response[2])) << 8) |
                              (uint32_t(uint8_t(response[3])) << 16) |
                              (uint32_t(uint8_t(response[4])) << 24);
            if (size > 120000) throw std::runtime_error("The CTranslate2 response is too long");
            result = read(size, cancel);
            if (response[0] != 0) throw std::runtime_error("CTranslate2 translation failed: " + result);
            if (result.empty()) throw std::runtime_error("CTranslate2 returned no translation");
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
    std::jthread sweeper_;
    std::optional<CTranslate2Models> models_;
    std::set<std::string> prepared_;
    void stop() {
        prepared_.clear();
        input_.reset();
        output_.reset();
        if (process_.value) {
            if (WaitForSingleObject(process_.value, 0) == WAIT_TIMEOUT)
                TerminateProcess(process_.value, 1);
            process_.reset();
        }
    }
    void start(const fs::path &root, StopToken cancel) {
        probe_ct2_runtime(root, [cancel] { return cancel.stop_requested(); });
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE child_input = nullptr, parent_input = nullptr, parent_output = nullptr, child_output = nullptr;
        if (!CreatePipe(&child_input, &parent_output, &security, 65536))
            throw std::runtime_error("Could not create the CTranslate2 input pipe");
        Handle read_child(child_input), write_parent(parent_output);
        if (!CreatePipe(&parent_input, &child_output, &security, 65536))
            throw std::runtime_error("Could not create the CTranslate2 output pipe");
        Handle read_parent(parent_input), write_child(child_output);
        SetHandleInformation(write_parent.value, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(read_parent.value, HANDLE_FLAG_INHERIT, 0);
        Handle child_error(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (child_error.value == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Could not prepare the CTranslate2 log channel");
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        startup.hStdInput = read_child.value;
        startup.hStdOutput = write_child.value;
        startup.hStdError = child_error.value;
        auto executable = root / "BianLingoCT2.exe";
        const auto data = platform::app_data() / "ctranslate2";
        auto catalog = bundled_text("ctranslate2-models.json");
        atomic_write(data / "models.json", std::span(reinterpret_cast<const uint8_t *>(catalog.data()), catalog.size()));
        auto fallback = platform::package_root() / "data" / "ctranslate2";
        auto command = L"\"" + executable.wstring() + L"\" --models-root \"" +
                       data.wstring() + L"\" --bundled-root \"" + fallback.wstring() + L"\" --catalog \"" + (data / "models.json").wstring() + L"\"";
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, root.c_str(), &startup, &info))
            throw std::runtime_error("Could not start the local CTranslate2 engine");
        Handle thread(info.hThread);
        process_.reset(info.hProcess);
        input_.reset(write_parent.value);
        write_parent.value = nullptr;
        output_.reset(read_parent.value);
        read_parent.value = nullptr;
        read_child.reset();
        write_child.reset();
        if (line(cancel) != "BLCT3") {
            stop();
            throw std::runtime_error("The CTranslate2 engine version is incompatible or startup failed");
        }
        used_ = Clock::now();
    }
    void write(std::string_view value, StopToken cancel) {
        while (!value.empty()) {
            if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
            DWORD sent = 0;
            if (!WriteFile(input_.value, value.data(), DWORD(value.size()), &sent, nullptr) || !sent)
                throw std::runtime_error("The CTranslate2 input pipe is closed");
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
                throw std::runtime_error("The CTranslate2 engine exited");
            if (!available) {
                if (Clock::now() >= deadline) throw std::runtime_error("CTranslate2 translation timed out");
                Sleep(5);
                continue;
            }
            DWORD got = 0;
            if (!ReadFile(output_.value, value.data() + offset,
                          DWORD(std::min<size_t>(available, length - offset)), &got, nullptr) || !got)
                throw std::runtime_error("Could not read the CTranslate2 translation");
            offset += got;
        }
        return value;
    }
    std::string line(StopToken cancel) {
        std::string result;
        for (;;) {
            const auto byte = read(1, cancel)[0];
            if (byte == '\n') return result;
            if (result.size() >= 80) throw std::runtime_error("The CTranslate2 engine handshake is too long");
            if (byte != '\r') result += byte;
        }
    }
};

Client &client() {
    static Client instance;
    return instance;
}
} // namespace

std::string ctranslate2_translate(std::string_view text, bool incoming, std::string_view target,
                                  std::string_view chat_language, StopToken cancel) {
    return client().translate(text, incoming, target, chat_language, cancel);
}
} // namespace bl
