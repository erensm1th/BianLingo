#include "bianlingo/provider.hpp"
#include <windows.h>

#include "bianlingo/core.hpp"
#include "bianlingo/storage.hpp"
#include "windows_http.hpp"
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <shlobj.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <winhttp.h>

namespace bl::platform {
std::string user_locale() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (!GetEnvironmentVariableW(L"BIANLINGO_UI_LANGUAGE", name, LOCALE_NAME_MAX_LENGTH)) {
        wchar_t preferred[LOCALE_NAME_MAX_LENGTH * 4]{};
        ULONG count = 0, size = LOCALE_NAME_MAX_LENGTH * 4;
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, preferred, &size) && count > 0)
            wcsncpy_s(name, LOCALE_NAME_MAX_LENGTH, preferred, _TRUNCATE);
        else if (!GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH))
            return "en";
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
    std::string value(size_t(n > 0 ? n : 0), 0);
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, name, -1, value.data(), n, nullptr, nullptr);
    if (!value.empty()) value.pop_back();
    return value;
}
bool is_letter(char32_t c) {
    wchar_t chars[2];
    int n = 1;
    if (c > 0xffff) {
        c -= 0x10000;
        chars[0] = wchar_t(0xd800 + (c >> 10));
        chars[1] = wchar_t(0xdc00 + (c & 1023));
        n = 2;
    } else
        chars[0] = wchar_t(c);
    WORD types[2]{};
    return GetStringTypeW(CT_CTYPE1, chars, n, types) && (types[0] & C1_ALPHA);
}
namespace {
std::wstring wide(std::string_view s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string narrow(std::wstring_view w) {
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), int(w.size()), nullptr, 0, nullptr,
                                nullptr);
    if (!n)
        throw std::runtime_error("Invalid Unicode");
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), int(w.size()), s.data(), n, nullptr,
                        nullptr);
    return s;
}
struct Http {
    HINTERNET h = nullptr;
    ~Http() {
        if (h)
            WinHttpCloseHandle(h);
    }
};
struct Key {
    HKEY h = nullptr;
    ~Key() {
        if (h)
            RegCloseKey(h);
    }
};
} // namespace
std::filesystem::path app_data() {
    return portable_data(executable_dir());
}
void initialize_storage() {
    prepare_portable_data(executable_dir());
    auto temporary = app_data() / "Temp";
    for (auto key : {L"TEMP", L"TMP"})
        if (!SetEnvironmentVariableW(key, temporary.c_str())) throw std::runtime_error("Could not set the portable temporary directory");
    auto python_cache = app_data() / "PythonCache";
    SetEnvironmentVariableW(L"PYTHONPYCACHEPREFIX", python_cache.c_str());
    for (const auto &[key, folder] : std::vector<std::pair<std::wstring, std::filesystem::path>>{
             {L"HF_HOME", "Cache/huggingface"}, {L"HF_HUB_CACHE", "Cache/huggingface/hub"},
             {L"TORCH_HOME", "Cache/torch"}, {L"XDG_CACHE_HOME", "Cache"},
             {L"PSModuleAnalysisCachePath", "Temp/ps-module-cache"}})
        if (!SetEnvironmentVariableW(key.c_str(), (app_data() / folder).c_str()))
            throw std::runtime_error("Could not set the portable cache directory");
    package_root();
}
std::filesystem::path executable_dir() {
    std::wstring buf(32768, 0);
    auto n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    if (!n || n == buf.size())
        throw std::runtime_error("Could not locate the application directory");
    buf.resize(n);
    return std::filesystem::path(buf).parent_path();
}
Bytes protect(std::span<const uint8_t> raw, bool decrypt) {
    if (raw.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Credential too large");
    DATA_BLOB input{DWORD(raw.size()), const_cast<BYTE *>(raw.data())}, output{};
    BOOL ok = decrypt ? CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                                           CRYPTPROTECT_UI_FORBIDDEN, &output)
                      : CryptProtectData(&input, L"BianLingo", nullptr, nullptr, nullptr,
                                         CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!ok)
        throw std::runtime_error("Windows could not read or save local credentials");
    Bytes result(output.pbData, output.pbData + output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}
std::string hardware_id() {
    constexpr DWORD provider = 0x52534d42; // Python int.from_bytes(b'RSMB', 'big').
    auto size = GetSystemFirmwareTable(provider, 0, nullptr, 0);
    Json identity = Json::object();
    if (size >= 8 && size <= 4 * 1024 * 1024) {
        Bytes raw(size);
        auto n = GetSystemFirmwareTable(provider, 0, raw.data(), size);
        if (n >= 8 && n <= size) {
            size_t declared = raw[4] | size_t(raw[5]) << 8 | size_t(raw[6]) << 16 | size_t(raw[7]) << 24,
                   end = std::min(size_t(n), 8 + declared), offset = 8;
            while (offset + 4 <= end) {
                auto kind = raw[offset], len = raw[offset + 1];
                if (len < 4 || offset + len > end)
                    break;
                size_t strings_end = offset + len;
                while (strings_end + 1 < end && (raw[strings_end] != 0 || raw[strings_end + 1] != 0))
                    strings_end++;
                if (strings_end + 1 >= end)
                    break;
                if (kind == 1 && len >= 24) {
                    auto begin = raw.begin() + offset + 8, finish = begin + 16;
                    if (!std::all_of(begin, finish, [](auto c) { return c == 0; }) &&
                        !std::all_of(begin, finish, [](auto c) { return c == 255; })) {
                        std::string hex;
                        for (auto i = begin; i != finish; ++i) {
                            hex += "0123456789abcdef"[*i >> 4];
                            hex += "0123456789abcdef"[*i & 15];
                        }
                        identity["system_uuid"] = hex;
                    }
                }
                if (kind == 2 && len >= 8) {
                    int index = raw[offset + 7];
                    size_t begin = offset + len;
                    for (int j = 1; j < index && begin < strings_end; j++) {
                        while (begin < strings_end && raw[begin])
                            begin++;
                        begin++;
                    }
                    if (index > 0 && begin <= strings_end) {
                        size_t finish = begin;
                        while (finish < strings_end && raw[finish])
                            finish++;
                        std::string serial;
                        for (auto j = begin; j < finish; j++)
                            if (raw[j] < 128)
                                serial += char(raw[j]);
                        serial = lower_ascii(trim(serial));
                        bool varied =
                            serial.size() > 1 && std::any_of(serial.begin() + 1, serial.end(),
                                                             [&](char c) { return c != serial.front(); });
                        if (varied && serial != "default string" && serial != "to be filled by o.e.m." &&
                            serial != "none" && serial != "unknown" && serial != "not specified")
                            identity["board_serial"] = serial;
                    }
                }
                offset = strings_end + 2;
                if (kind == 127)
                    break;
            }
        }
    }
    if (identity.empty()) {
        Key key;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0,
                          KEY_READ | KEY_WOW64_64KEY, &key.h) != ERROR_SUCCESS)
            throw std::runtime_error("Could not identify this device");
        std::array<wchar_t, 256> buf{};
        DWORD type = 0, n = DWORD(buf.size() * sizeof(wchar_t));
        if (RegQueryValueExW(key.h, L"MachineGuid", nullptr, &type, reinterpret_cast<BYTE *>(buf.data()),
                             &n) != ERROR_SUCCESS ||
            type != REG_SZ)
            throw std::runtime_error("Could not read the device identifier");
        buf.back() = 0;
        auto guid = lower_ascii(trim(narrow(buf.data())));
        if (guid.empty())
            throw std::runtime_error("The device identifier is empty");
        identity["windows_installation"] = guid;
    }
    std::string domain = "bianlingo/device/v1";
    domain += '\0';
    return sha256(domain + canonical(identity));
}
Json http_json(std::string_view address, const Json &body, std::string_view bearer, bool get,
               const std::map<std::string, std::string> &extra_headers, StopToken stop) {
    if (stop.stop_requested()) throw std::runtime_error("Request cancelled.");
    const auto started = Clock::now();
    validate_http_headers(extra_headers);
    constexpr std::string_view local_ollama = "http://127.0.0.1:11435/api/chat";
    bool local = address == local_ollama;
    if (address.starts_with("http://") && !local)
        throw std::runtime_error("Plain HTTP is allowed only for the fixed local Ollama address");
    if (local && (get || !bearer.empty() || !extra_headers.empty()))
        throw std::runtime_error("Local Ollama requests cannot include keys or custom headers");
    auto url = wide(address);
    URL_COMPONENTS parsed{};
    parsed.dwStructSize = sizeof(parsed);
    parsed.dwHostNameLength = parsed.dwUrlPathLength = parsed.dwExtraInfoLength = parsed.dwUserNameLength =
        parsed.dwPasswordLength = DWORD(-1);
    if (!WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &parsed) ||
        parsed.nScheme != (local ? INTERNET_SCHEME_HTTP : INTERNET_SCHEME_HTTPS) ||
        !parsed.dwHostNameLength || parsed.dwUserNameLength ||
        parsed.dwPasswordLength)
        throw std::runtime_error("Invalid API URL; online connections must use HTTPS");
    auto host = std::wstring(parsed.lpszHostName, parsed.dwHostNameLength),
         path = std::wstring(parsed.lpszUrlPath, parsed.dwUrlPathLength);
    if (path.empty())
        path = L"/";
    if (parsed.dwExtraInfoLength)
        path.append(parsed.lpszExtraInfo, parsed.dwExtraInfoLength);
    Http session{WinHttpOpen(L"BianLingo", local ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)};
    if (!session.h || !WinHttpSetTimeouts(session.h, local ? 2000 : 8000, 8000, local ? 90000 : 20000,
                                           local ? 90000 : 20000))
        throw std::runtime_error("Could not initialize networking");
    Http connection{WinHttpConnect(session.h, host.c_str(), parsed.nPort, 0)};
    if (!connection.h)
        throw std::runtime_error("Could not connect to the API");
    Http request{WinHttpOpenRequest(connection.h, get ? L"GET" : L"POST", path.c_str(), nullptr,
                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                    local ? 0 : WINHTTP_FLAG_SECURE)};
    if (!request.h)
        throw std::runtime_error("Could not initialize the API request");
    auto state = windows_http::attach(request.h);
    const auto deadline = started + std::chrono::seconds(local ? 100 : 45);
    auto wait = [&](DWORD completion) {
        windows_http::wait(state, completion, deadline, [&] {
            if (stop.stop_requested()) throw std::runtime_error("Request cancelled.");
        }, local ? "Local Ollama is not running or the model response timed out" : "The API connection failed, the certificate is invalid or the request timed out");
    };
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    if (bearer.find_first_of("\r\n") != std::string_view::npos)
        throw std::runtime_error("Invalid API key format");
    std::wstring headers;
    bool content_type = false, authorization = false;
    for (const auto &[name, value] : extra_headers) {
        content_type |= lower_ascii(name) == "content-type";
        authorization |= lower_ascii(name) == "authorization";
        headers += wide(name) + L": " + wide(value) + L"\r\n";
    }
    if (!content_type)
        headers += L"Content-Type: application/json\r\n";
    if (!bearer.empty() && !authorization)
        headers += L"Authorization: Bearer " + wide(bearer) + L"\r\n";
    auto &data = state->payload;
    data = get ? std::string() : body.dump();
    if (data.size() > 128 * 1024)
        throw std::runtime_error("The API request is too large");
    DWORD_PTR context = 0; DWORD context_size = sizeof(context);
    if (!WinHttpQueryOption(request.h, WINHTTP_OPTION_CONTEXT_VALUE, &context, &context_size))
        throw std::runtime_error("Could not initialize the network request");
    windows_http::check(WinHttpSendRequest(request.h, headers.c_str(), DWORD(headers.size()),
                            get ? WINHTTP_NO_REQUEST_DATA : data.data(), DWORD(data.size()),
                            DWORD(data.size()), context));
    wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
    windows_http::check(WinHttpReceiveResponse(request.h, nullptr));
    wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    DWORD status = 0, n = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &n, WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("Invalid API response");
    if (status < 200 || status >= 300) {
        if (local)
            throw std::runtime_error("The local Ollama request failed (HTTP " + std::to_string(status) +
                                     "); check the model name and Ollama service");
        std::string reason = status == 401   ? "Invalid key"
                             : status == 402 ? "Insufficient balance"
                             : status == 429 ? "Rate limit reached"
                                             : "Service error";
        throw std::runtime_error("API：" + reason + "（HTTP " + std::to_string(status) + "）");
    }
    std::string output;
    for (;;) {
        if (stop.stop_requested()) throw std::runtime_error("Request cancelled.");
        if (Clock::now() >= deadline) throw std::runtime_error("The API request timed out");
        windows_http::check(WinHttpReadData(request.h, state->buffer.data(), DWORD(state->buffer.size()), nullptr));
        wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
        const auto got = state->received;
        if (!got) break;
        if (output.size() + got > 2 * 1024 * 1024)
            throw std::runtime_error("The API response is too large");
        output.append(state->buffer.data(), got);
    }
    try {
        return Json::parse(output);
    } catch (const std::exception &) {
        throw std::runtime_error("The API returned invalid JSON");
    }
}
namespace {
class WowWindow {
    HWND current_ = nullptr, considered_foreground_ = nullptr;
    DWORD process_ = 0;
    Clock::time_point next_find_{};

    static std::optional<Capture::GameWindow> bounds(HWND window, DWORD expected_process) {
        DWORD process = 0;
        if (!window || !IsWindow(window) || !IsWindowVisible(window) || IsIconic(window) ||
            GetWindow(window, GW_OWNER) || !GetWindowThreadProcessId(window, &process) ||
            process != expected_process)
            return std::nullopt;
        RECT client{};
        POINT origin{};
        if (!GetClientRect(window, &client) || !ClientToScreen(window, &origin) || client.right <= 0 ||
            client.bottom <= 0)
            return std::nullopt;
        return Capture::GameWindow{reinterpret_cast<uintptr_t>(window),
                                   {origin.x, origin.y, client.right, client.bottom}};
    }
    void find(HWND foreground) {
        // Toolhelp enumerates executable names without opening the game process.
        struct Snapshot {
            HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            ~Snapshot() {
                if (handle != INVALID_HANDLE_VALUE)
                    CloseHandle(handle);
            }
        } snapshot;
        if (snapshot.handle == INVALID_HANDLE_VALUE)
            return;
        std::vector<DWORD> processes;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot.handle, &entry)) {
            do {
                for (auto name : {L"Wow.exe", L"WowClassic.exe", L"WowB.exe", L"WowT.exe", L"WowClassicB.exe",
                                  L"WowClassicT.exe"}) {
                    if (_wcsicmp(entry.szExeFile, name) == 0) {
                        processes.push_back(entry.th32ProcessID);
                        break;
                    }
                }
            } while (Process32NextW(snapshot.handle, &entry));
        }
        if (processes.empty()) {
            current_ = nullptr;
            process_ = 0;
            return;
        }
        struct Search {
            const std::vector<DWORD> &processes;
            HWND foreground, current, found = nullptr;
            DWORD process = 0;
            int priority = 0;
            int64_t area = 0;
        } search{processes, foreground, current_};
        EnumWindows(
            [](HWND window, LPARAM parameter) -> BOOL {
                auto &search = *reinterpret_cast<Search *>(parameter);
                DWORD process = 0;
                GetWindowThreadProcessId(window, &process);
                if (std::find(search.processes.begin(), search.processes.end(), process) ==
                    search.processes.end())
                    return TRUE;
                auto target = bounds(window, process);
                if (!target)
                    return TRUE;
                int priority = window == search.foreground ? 3 : window == search.current ? 2 : 1;
                int64_t area = int64_t(target->client.w) * target->client.h;
                if (priority > search.priority || (priority == search.priority && area > search.area)) {
                    search.found = window;
                    search.process = process;
                    search.priority = priority;
                    search.area = area;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        current_ = search.found;
        process_ = search.process;
    }

  public:
    std::optional<Capture::GameWindow> get() {
        auto foreground = GetForegroundWindow();
        auto target = bounds(current_, process_);
        auto now = Clock::now();
        if ((!target || foreground != considered_foreground_) && now >= next_find_) {
            next_find_ = now + std::chrono::seconds(2);
            find(foreground);
            considered_foreground_ = foreground;
            target = bounds(current_, process_);
        }
        return target;
    }
};
} // namespace
class GdiCapture final : public Capture {
    HDC desktop_ = nullptr, memory_ = nullptr;
    HBITMAP original_ = nullptr;
    struct Surface {
        HBITMAP bitmap = nullptr;
        void *bits = nullptr;
        Image image;
        uint64_t used = 0;
    };
    // Keep marker, full strip and discovery buffers resident. Alternating a
    // 4x4 marker with a full strip must not allocate a DIB twice every poll.
    std::array<Surface, 3> surfaces_;
    uint64_t use_ = 0;
    WowWindow game_;
    void release() {
        if (memory_ && original_)
            SelectObject(memory_, original_);
        for (auto &surface : surfaces_) {
            if (surface.bitmap)
                DeleteObject(surface.bitmap);
            surface.bitmap = nullptr;
        }
    }

  public:
    bool window_scoped() const override {
        return true;
    }
    std::optional<GameWindow> game_window() override {
        return game_.get();
    }
    GdiCapture() {
        desktop_ = GetDC(nullptr);
        if (desktop_)
            memory_ = CreateCompatibleDC(desktop_);
        if (!desktop_ || !memory_) {
            if (memory_)
                DeleteDC(memory_);
            if (desktop_)
                ReleaseDC(nullptr, desktop_);
            throw std::runtime_error("Could not initialize region capture");
        }
        original_ = reinterpret_cast<HBITMAP>(GetCurrentObject(memory_, OBJ_BITMAP));
    }
    ~GdiCapture() override {
        release();
        if (memory_)
            DeleteDC(memory_);
        if (desktop_)
            ReleaseDC(nullptr, desktop_);
    }
    std::vector<Rect> monitors() override {
        std::vector<Rect> out;
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR, HDC, LPRECT r, LPARAM param) -> BOOL {
                reinterpret_cast<std::vector<Rect> *>(param)->push_back(
                    {r->left, r->top, r->right - r->left, r->bottom - r->top});
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&out));
        return out;
    }
    const Image &grab(Rect r) override {
        if (r.w < 1 || r.h < 1 || r.w > 32768 || r.h > 32768 || uint64_t(r.w) * r.h > 100'000'000)
            throw std::runtime_error("Invalid capture region");
        auto slot = std::find_if(surfaces_.begin(), surfaces_.end(), [&](const Surface &s) {
            return s.bitmap && s.image.width == r.w && s.image.height == r.h;
        });
        if (slot == surfaces_.end()) {
            slot = std::min_element(surfaces_.begin(), surfaces_.end(),
                                    [](const Surface &a, const Surface &b) { return a.used < b.used; });
            if (slot->bitmap) {
                SelectObject(memory_, original_);
                DeleteObject(slot->bitmap);
                slot->bitmap = nullptr;
            }
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = r.w;
            info.bmiHeader.biHeight = -r.h;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            slot->bitmap = CreateDIBSection(desktop_, &info, DIB_RGB_COLORS, &slot->bits, nullptr, 0);
            if (!slot->bitmap)
                throw std::runtime_error("Could not create the capture buffer");
            slot->image.width = r.w;
            slot->image.height = r.h;
            slot->image.stride = r.w * 4;
            slot->image.pixels.resize(size_t(slot->image.stride) * r.h);
        }
        slot->used = ++use_;
        SelectObject(memory_, slot->bitmap);
        if (!BitBlt(memory_, 0, 0, r.w, r.h, desktop_, r.x, r.y, SRCCOPY | CAPTUREBLT))
            throw std::runtime_error("Could not read the screen region");
        GdiFlush();
        std::memcpy(slot->image.pixels.data(), slot->bits, slot->image.pixels.size());
        return slot->image;
    }
};
std::unique_ptr<Capture> make_capture() {
    return std::make_unique<GdiCapture>();
}
} // namespace bl::platform
