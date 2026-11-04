#include "bianlingo/local_models.hpp"
#include <windows.h>

namespace bl {
namespace {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};
}
void probe_ct2_runtime(const std::filesystem::path &root, const std::function<bool()> &should_stop) {
    const auto cancelled = [&] {
        if (should_stop && should_stop()) throw std::runtime_error("Model preparation cancelled");
    };
    cancelled();
    const auto executable = root / "BianLingoCT2.exe";
    if (!std::filesystem::is_regular_file(executable)) throw std::runtime_error("The CTranslate2 runtime is missing");
    Handle job{CreateJobObjectW(nullptr, nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw std::runtime_error("Unable to check CTranslate2 dependencies.");
    auto command = L"\"" + executable.wstring() + L"\" --self-test";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &info))
        throw std::runtime_error("CTranslate2 native component could not start; reinstall the complete package");
    Handle process{info.hProcess}, thread{info.hThread};
    if (!AssignProcessToJobObject(job.value, process.value)) {
        TerminateProcess(process.value, 1);
        throw std::runtime_error("Unable to check CTranslate2 dependencies.");
    }
    if (ResumeThread(thread.value) == DWORD(-1)) throw std::runtime_error("Unable to check CTranslate2 dependencies.");
    const auto deadline = Clock::now() + std::chrono::seconds(15);
    for (;;) {
        cancelled();
        const auto result = WaitForSingleObject(process.value, 50);
        if (result == WAIT_OBJECT_0) break;
        if (result != WAIT_TIMEOUT || Clock::now() >= deadline)
            throw std::runtime_error("CTranslate2 dependency check timed out. Check the runtime and retry.");
    }
    DWORD code = 1;
    if (!GetExitCodeProcess(process.value, &code) || code)
        throw std::runtime_error("CTranslate2 native component could not start; reinstall the complete package");
}
} // namespace bl
