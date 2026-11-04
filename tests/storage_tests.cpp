#include "bianlingo/storage.hpp"
#include <iostream>
using namespace bl;
void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
int main() {
    const auto parent = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    auto root = parent / ("bianlingo-storage-" + sha256(random_bytes(12)).substr(0, 16));
    struct Cleanup { std::filesystem::path root, parent; ~Cleanup() {
        std::error_code e; auto target = std::filesystem::weakly_canonical(root, e);
        if (!e && target.parent_path() == parent && target.filename().string().starts_with("bianlingo-storage-")) std::filesystem::remove_all(target, e);
    } } cleanup{root, parent};
    try {
        auto app = root / "Application";
        prepare_portable_data(app);
        auto data = portable_data(app);
        check(data.parent_path() == std::filesystem::absolute(app).lexically_normal(), "Storage escaped the executable directory");
        write_json(data / "settings.json", {{"font_size", 27}});
        Bytes model(20 * 1024 * 1024, 42); atomic_write(data / "bergamot" / "models" / "en-zh-Hans" / "model.bin", model);
        prepare_portable_data(app);
        check(read_json(data / "settings.json")["font_size"] == 27, "Initialization overwrote existing settings");
        check(std::filesystem::file_size(data / "bergamot" / "models" / "en-zh-Hans" / "model.bin") == model.size(), "Initialization altered model files");
        const auto blocked_root = root / "NotADirectory";
        atomic_write(blocked_root, Bytes{'x'});
        bool refused = false;
        try { prepare_portable_data(blocked_root); } catch (const std::exception &) { refused = true; }
        check(refused, "Invalid storage location silently fell back elsewhere");
        auto installed = root / "runtime", staged = root / "staged", backup = root / "runtime-backup";
        write_json(installed / "identity.json", {{"version", "old"}});
        write_json(staged / "identity.json", {{"version", "new"}});
        int renames = 0; bool failed = false;
        try {
            commit_directory(staged, installed, backup, [&](const auto &from, const auto &to) {
                if (++renames == 2) throw std::runtime_error("injected commit failure");
                std::filesystem::rename(from,to);
            });
        } catch (const std::exception &) { failed = true; }
        check(failed && renames == 3 && read_json(installed / "identity.json")["version"] == "old" &&
              std::filesystem::exists(staged) && !std::filesystem::exists(backup), "Failed commit did not restore the previous runtime");
        commit_directory(staged, installed, backup);
        check(read_json(installed / "identity.json")["version"] == "new" && read_json(backup / "identity.json")["version"] == "old",
              "Successful commit lost the backup or installed the wrong runtime");
        std::cout << "PASS portable root, data preservation, storage failure and directory rollback\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
