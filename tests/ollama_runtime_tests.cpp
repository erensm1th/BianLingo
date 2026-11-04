#include <winsock2.h>
#include <windows.h>
#include "bianlingo/core.hpp"
#include "bianlingo/ollama_runtime.hpp"
#include <iostream>
using namespace bl;
std::string env_path(const wchar_t *key) {
    wchar_t value[32768]{}; GetEnvironmentVariableW(key, value, 32768);
    auto bytes = std::filesystem::path(value).generic_u8string(); return {bytes.begin(), bytes.end()};
}
int stub() {
    WSADATA wsa{}; if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 2;
    auto listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(11435);
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR) return 3;
    Json values{{"pid", GetCurrentProcessId()}, {"models", env_path(L"OLLAMA_MODELS")}, {"profile", env_path(L"USERPROFILE")},
                {"temp", env_path(L"TEMP")}, {"host", env_path(L"OLLAMA_HOST")}};
    auto storage = std::filesystem::u8path(env_path(L"OLLAMA_MODELS")).parent_path().parent_path();
    write_json(storage / "stub-environment.json", values);
    if (listen(listener, 5)) return 4;
    for (;;) Sleep(1000); // The owning job must terminate this stub and all descendants.
}
void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
int main(int argc, char **argv) {
    if (argc > 1 && std::string_view(argv[1]) == "serve") return stub();
    try {
        auto directory = platform::executable_dir(), data = platform::app_data();
        auto runtime = directory / "data" / "ollama";
        std::filesystem::create_directories(runtime);
        wchar_t current[32768]{}; GetModuleFileNameW(nullptr, current, 32768);
        auto binary = runtime / "ollama.exe";
        std::filesystem::copy_file(current, binary, std::filesystem::copy_options::overwrite_existing);
        ensure_portable_ollama();
        auto values = read_json(data / "stub-environment.json");
        auto path = [](const std::filesystem::path &p) { auto s = p.generic_u8string(); return std::string(s.begin(), s.end()); };
        check(values["models"] == path(data / "ollama" / "models") && values["profile"] == path(data / "ollama-home") &&
              values["temp"] == path(data / "Temp") && values["host"] == "127.0.0.1:11435", "Managed Ollama escaped its portable environment");
        auto child = OpenProcess(SYNCHRONIZE, FALSE, values["pid"].get<DWORD>());
        check(child != nullptr, "Managed child disappeared");
        pause_portable_ollama(true);
        check(WaitForSingleObject(child, 1000) == WAIT_OBJECT_0, "Pausing setup did not terminate the managed daemon"); CloseHandle(child);
        bool rejected = false; try { ensure_portable_ollama(); } catch (...) { rejected = true; }
        check(rejected, "Translation restarted a daemon during setup");
        pause_portable_ollama(false); ensure_portable_ollama(); stop_portable_ollama();
        // Only the fixture binary in this target's dedicated directory is removed.
        check(std::filesystem::weakly_canonical(binary).parent_path() == std::filesystem::weakly_canonical(runtime), "Unsafe fixture cleanup path");
        std::filesystem::remove(binary);
        std::cout << "PASS private runtime, host/profile/model/temp environment, pause and job cleanup\n";
        return 0;
    } catch (const std::exception &e) { stop_portable_ollama(); std::cerr << e.what() << '\n'; return 1; }
}
