#include "bianlingo/download.hpp"
#include "platform/windows_http.hpp"
#include <fstream>
#include <windows.h>
#include <winhttp.h>

namespace bl::download {
namespace {
std::wstring wide(std::string_view s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n && !s.empty()) throw std::runtime_error("The download URL is not valid UTF-8");
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}
struct Http {
    HINTERNET h = nullptr;
    ~Http() { if (h) WinHttpCloseHandle(h); }
};
} // namespace
void download(std::string_view address, const std::filesystem::path &destination, uint64_t limit,
              const Progress &progress) {
    if (!limit || limit > max_download)
        throw std::runtime_error("Invalid model download limit");
    std::wstring url = wide(address);
    Http session{WinHttpOpen(L"BianLingo-ModelDownload", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)};
    if (!session.h || !WinHttpSetTimeouts(session.h, 8000, 8000, 8000, 8000))
        throw std::runtime_error("Could not initialize the model network connection");
    auto deadline = Clock::now() + std::chrono::seconds(150);
    for (int redirect = 0; redirect < 6; ++redirect) {
        if (progress)
            progress(0);
        if (Clock::now() > deadline)
            throw std::runtime_error("Model download exceeded its time limit");
        URL_COMPONENTS p{};
        p.dwStructSize = sizeof(p);
        p.dwHostNameLength = p.dwUrlPathLength = p.dwExtraInfoLength = p.dwUserNameLength =
            p.dwPasswordLength = DWORD(-1);
        if (!WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &p) || p.nScheme != INTERNET_SCHEME_HTTPS ||
            !p.dwHostNameLength || p.dwUserNameLength || p.dwPasswordLength || url.find(L'#') != url.npos)
            throw std::runtime_error("Model URLs must use HTTPS without embedded credentials");
        auto host = std::wstring(p.lpszHostName, p.dwHostNameLength),
             path = std::wstring(p.lpszUrlPath, p.dwUrlPathLength);
        if (path.empty())
            path = L"/";
        if (p.dwExtraInfoLength)
            path.append(p.lpszExtraInfo, p.dwExtraInfoLength);
        Http connection{WinHttpConnect(session.h, host.c_str(), p.nPort, 0)};
        Http request{connection.h
                         ? WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                         : nullptr};
        if (!request.h)
            throw std::runtime_error("Could not connect to the model server");
        auto state = windows_http::attach(request.h);
        uint64_t total = 0;
        auto wait = [&](DWORD completion) {
            windows_http::wait(state, completion, deadline, [&] { if (progress) progress(total); },
                               "The model connection timed out or certificate verification failed");
        };
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER, logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
        WinHttpSetOption(request.h, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon));
        const wchar_t *headers = L"Accept: application/octet-stream\r\nCache-Control: no-cache\r\n";
        DWORD_PTR context = 0; DWORD context_size = sizeof(context);
        if (!WinHttpQueryOption(request.h, WINHTTP_OPTION_CONTEXT_VALUE, &context, &context_size))
            throw std::runtime_error("Could not initialize the network request");
        windows_http::check(WinHttpSendRequest(request.h, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, context));
        wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
        windows_http::check(WinHttpReceiveResponse(request.h, nullptr));
        wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
        DWORD status = 0, n = sizeof(status);
        if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &n, WINHTTP_NO_HEADER_INDEX))
            throw std::runtime_error("Invalid response from the model server");
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            DWORD bytes = 0;
            WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                                &bytes, WINHTTP_NO_HEADER_INDEX);
            if (!bytes || bytes > 32768)
                throw std::runtime_error("Invalid model redirect URL");
            std::wstring location(bytes / sizeof(wchar_t), 0);
            if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                     location.data(), &bytes, WINHTTP_NO_HEADER_INDEX))
                throw std::runtime_error("Model redirect failed");
            location.resize(wcslen(location.c_str()));
            if (location.starts_with(L"/") && !location.starts_with(L"//")) {
                // Model hosts also return same-origin absolute-path redirects.
                // Keep HTTPS and the existing authority; validate again above.
                url = L"https://" + host;
                if (p.nPort != INTERNET_DEFAULT_HTTPS_PORT) url += L":" + std::to_wstring(p.nPort);
                url += location;
            } else if (location.starts_with(L"https://")) {
                url = std::move(location);
            } else {
                throw std::runtime_error("Model downloads cannot downgrade to HTTP");
            }
            continue;
        }
        if (status != 200)
            throw std::runtime_error("Model HTTP " + std::to_string(status));
        std::ofstream out(destination, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("Could not write the model package");
        for (;;) {
            if (Clock::now() > deadline)
                throw std::runtime_error("Model download exceeded its time limit");
            windows_http::check(WinHttpReadData(request.h, state->buffer.data(), DWORD(state->buffer.size()), nullptr));
            wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
            const auto got = state->received;
            if (!got)
                break;
            total += got;
            if (total > limit)
                throw std::runtime_error("Model download exceeds the size limit");
            out.write(state->buffer.data(), got);
            if (!out)
                throw std::runtime_error("Model write failed");
            if (progress)
                progress(total);
        }
        out.close();
        if (!out || !total)
            throw std::runtime_error("The model package is empty or could not be saved");
        return;
    }
    throw std::runtime_error("Too many model redirects");
}
} // namespace bl::download
