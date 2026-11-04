#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include "bianlingo/core.hpp"
#include "bianlingo/ollama_runtime.hpp"
namespace bl {
namespace {
struct EnvironmentLess { bool operator()(const std::wstring &a, const std::wstring &b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; } };
std::vector<wchar_t> environment(const std::filesystem::path &data) {
    std::map<std::wstring, std::wstring, EnvironmentLess> values;
    auto original = GetEnvironmentStringsW();
    if (!original) throw std::runtime_error("Could not read the process environment");
    for (auto p = original; *p; p += wcslen(p) + 1) {
        std::wstring item(p); auto equal = item.find(L'=', item.starts_with(L'=') ? 1 : 0);
        if (equal != item.npos) values[item.substr(0, equal)] = item.substr(equal + 1);
    }
    FreeEnvironmentStringsW(original);
    auto home = data / "ollama-home";
    values[L"USERPROFILE"] = home.wstring(); values[L"APPDATA"] = (home / "AppData" / "Roaming").wstring();
    values[L"LOCALAPPDATA"] = (home / "AppData" / "Local").wstring();
    values[L"TEMP"] = values[L"TMP"] = (data / "Temp").wstring();
    values[L"OLLAMA_MODELS"] = (data / "ollama" / "models").wstring();
    values[L"OLLAMA_HOST"] = L"127.0.0.1:11435"; values[L"OLLAMA_NO_CLOUD"] = L"1";
    values[L"OLLAMA_KEEP_ALIVE"] = L"10m";
    std::vector<wchar_t> block;
    for (const auto &[key, value] : values) { auto line = key + L"=" + value; block.insert(block.end(), line.begin(), line.end()); block.push_back(0); }
    block.push_back(0); return block;
}
DWORD listener() {
    ULONG size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    std::vector<uint8_t> bytes(size);
    if (GetExtendedTcpTable(bytes.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR)
        throw std::runtime_error("Could not inspect the local Ollama port");
    auto table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(bytes.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i)
        if (ntohs(USHORT(table->table[i].dwLocalPort)) == 11435) return table->table[i].dwOwningPid;
    return 0;
}
class Runtime {
    std::timed_mutex mutex_;
    HANDLE process_{}, job_{};
    bool paused_ = false;
    void stop_locked() {
        if (job_) { CloseHandle(job_); job_ = nullptr; }
        if (process_) { WaitForSingleObject(process_, 3000); CloseHandle(process_); process_ = nullptr; }
    }
  public:
    ~Runtime() { stop_locked(); }
    void pause(bool value) { std::lock_guard lock(mutex_); paused_ = value; if (value) stop_locked(); }
    void stop() { std::lock_guard lock(mutex_); stop_locked(); }
    void ensure(StopToken cancel) {
        std::unique_lock lock(mutex_, std::defer_lock);
        while (!lock.try_lock_for(std::chrono::milliseconds(50)))
            if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
        if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
        if (paused_) throw std::runtime_error("Ollama is preparing models. Retry shortly.");
        if (process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) return;
        stop_locked();
        auto root = platform::executable_dir() / "data" / "ollama", data = platform::app_data();
        auto executable = root / "ollama.exe";
        if (!std::filesystem::is_regular_file(executable)) throw std::runtime_error("Download portable Ollama and the selected model first");
        if (listener()) throw std::runtime_error("The portable Ollama port is occupied by another process");
        for (auto path : {data / "Temp", data / "Logs", data / "ollama" / "models", data / "ollama-home",
                          data / "ollama-home" / "AppData" / "Local", data / "ollama-home" / "AppData" / "Roaming"})
            std::filesystem::create_directories(path);
        auto block = environment(data);
        job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            stop_locked(); throw std::runtime_error("Could not create the Ollama process container");
        }
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        auto log = CreateFileW((data / "Logs" / "ollama-server.log").c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        auto null_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE; startup.hStdOutput = startup.hStdError = log; startup.hStdInput = null_input;
        PROCESS_INFORMATION info{}; auto command = L"\"" + executable.wstring() + L"\" serve";
        bool launched = log != INVALID_HANDLE_VALUE && null_input != INVALID_HANDLE_VALUE && CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | BELOW_NORMAL_PRIORITY_CLASS, block.data(), root.c_str(), &startup, &info);
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log); if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);
        if (!launched) { stop_locked(); throw std::runtime_error("Could not start portable Ollama"); }
        process_ = info.hProcess;
        if (!AssignProcessToJobObject(job_, process_)) {
            TerminateProcess(process_, 1); CloseHandle(info.hThread); stop_locked(); throw std::runtime_error("Could not manage portable Ollama");
        }
        ResumeThread(info.hThread); CloseHandle(info.hThread);
        for (int i = 0; i < 150; ++i) {
            if (cancel.stop_requested()) { stop_locked(); throw std::runtime_error("Request cancelled."); }
            if (WaitForSingleObject(process_, 0) != WAIT_TIMEOUT) break;
            auto pid = listener();
            if (pid == info.dwProcessId) return;
            if (pid) break;
            Sleep(100);
        }
        stop_locked(); throw std::runtime_error("Portable Ollama failed to start. Check the local log.");
    }
};
Runtime &runtime() { static Runtime value; return value; }
}
void ensure_portable_ollama(StopToken cancel) { runtime().ensure(cancel); }
void pause_portable_ollama(bool paused) { runtime().pause(paused); }
void stop_portable_ollama() { runtime().stop(); }
}
