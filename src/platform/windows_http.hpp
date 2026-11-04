#pragma once
#include "bianlingo/core.hpp"
#include <windows.h>
#include <winhttp.h>

// Shared asynchronous request state for API calls and model downloads. Buffers
// stay alive through the last callback, including cancellation during headers.
namespace bl::windows_http {
struct HttpAsyncState {
    std::mutex mutex;
    std::condition_variable changed;
    DWORD completed = 0, error = 0, received = 0;
    std::string payload;
    std::array<char, 65536> buffer{};
};
// The context owns the buffers until HANDLE_CLOSING, the final notification.
// Cancellation closes an asynchronous request, never a synchronous call in use.
inline void CALLBACK http_callback(HINTERNET, DWORD_PTR context, DWORD status, void *info, DWORD length) {
    if (!context) return;
    auto owner = reinterpret_cast<std::shared_ptr<HttpAsyncState> *>(context);
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { delete owner; return; }
    auto state = *owner;
    std::lock_guard lock(state->mutex);
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
        state->error = static_cast<WINHTTP_ASYNC_RESULT *>(info)->dwError;
    if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) state->received = length;
    state->completed |= status;
    state->changed.notify_all();
}

inline std::shared_ptr<HttpAsyncState> attach(HINTERNET request) {
    auto state = std::make_shared<HttpAsyncState>();
    auto owner = std::make_unique<std::shared_ptr<HttpAsyncState>>(state);
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(owner.get());
    if (!WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
        throw std::runtime_error("Could not initialize the network request");
    if (WinHttpSetStatusCallback(request, http_callback,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES |
            WINHTTP_CALLBACK_FLAG_REQUEST_ERROR, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
        throw std::runtime_error("Could not initialize the network request");
    owner.release();
    return state;
}
inline void check(BOOL result) {
    if (!result && GetLastError() != ERROR_IO_PENDING) throw std::runtime_error("The network request could not be executed");
}
inline void wait(const std::shared_ptr<HttpAsyncState> &state, DWORD completion,
                 Clock::time_point deadline, const std::function<void()> &check_cancel,
                 const char *failure) {
    std::unique_lock lock(state->mutex);
    for (;;) {
        check_cancel();
        if (Clock::now() >= deadline) throw std::runtime_error("Network request exceeded its total time limit.");
        if (state->error) throw std::runtime_error(failure);
        if (state->completed & completion) break;
        state->changed.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::milliseconds(50)));
    }
    state->completed &= ~completion;
}
} // namespace bl::windows_http
