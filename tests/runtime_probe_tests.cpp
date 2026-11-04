#include "bianlingo/local_models.hpp"
#include <windows.h>
#include <iostream>
using namespace bl;
namespace fs = std::filesystem;
void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
int main(int argc, char **argv) {
    // An owned synthetic child tests the real CreateProcess/job/cancel path.
    // It does not stand in for clean-Windows testing of actual native imports.
    if (argc > 1 && std::string_view(argv[1]) == "--self-test") {
        if (fs::exists("fail-import")) return 37;
        if (fs::exists("stall-import")) Sleep(60000);
        return 0;
    }
    auto root = fs::temp_directory_path() / ("runtime-probe-" + sha256(random_bytes(12)).substr(0,16));
    struct Cleanup { fs::path p; ~Cleanup() { std::error_code ec; fs::remove_all(p,ec); } } cleanup{root};
    try {
        fs::create_directories(root);
        wchar_t module[32768]{}; GetModuleFileNameW(nullptr,module,32768);
        fs::copy_file(fs::path(module),root/"BianLingoCT2.exe");
        probe_ct2_runtime(root);
        atomic_write(root/"fail-import",{});
        bool failed = false;
        try { probe_ct2_runtime(root); } catch (const std::exception &e) {
            failed = std::string(e.what()).find("complete package") != std::string::npos;
        }
        check(failed, "Native import failure did not produce an actionable dependency error");
        fs::remove(root/"fail-import"); atomic_write(root/"stall-import",{});
        std::atomic<bool> cancel = false;
        auto pending = std::async(std::launch::async,[&] {
            try { probe_ct2_runtime(root,[&] { return cancel.load(); }); return false; }
            catch (const std::exception &e) { return lower_ascii(e.what()).find("cancel") != std::string::npos; }
        });
        Sleep(200); cancel = true;
        check(pending.wait_for(std::chrono::seconds(2)) == std::future_status::ready && pending.get(),
              "Stalled native probe did not cancel within the shutdown budget");
        std::cout << "PASS real child probe: success, import failure and cancellable job cleanup\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
