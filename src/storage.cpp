#include "bianlingo/storage.hpp"
namespace bl {
void commit_directory(const std::filesystem::path &staged, const std::filesystem::path &destination,
                      const std::filesystem::path &backup,
                      const std::function<void(const std::filesystem::path &, const std::filesystem::path &)> &rename) {
    auto move = [&](const auto &from, const auto &to) {
        if (rename) rename(from, to); else std::filesystem::rename(from, to);
    };
    bool moved = false;
    if (std::filesystem::exists(destination)) { move(destination, backup); moved = true; }
    try { move(staged, destination); }
    catch (...) {
        const auto failure = std::current_exception();
        if (moved) {
            try { move(backup, destination); }
            catch (...) { throw std::runtime_error("Runtime replacement and rollback failed; the previous runtime is retained at " + backup.string()); }
        }
        std::rethrow_exception(failure);
    }
}
std::filesystem::path portable_data(const std::filesystem::path &root) {
    return std::filesystem::absolute(root).lexically_normal() / "UserData";
}
void prepare_portable_data(const std::filesystem::path &root) {
    namespace fs = std::filesystem;
    const auto destination = portable_data(root);
    auto resolved_root = fs::weakly_canonical(root), resolved_data = fs::weakly_canonical(destination);
    if (resolved_data.parent_path() != resolved_root) throw std::runtime_error("UserData must stay inside the application folder");
    fs::create_directories(destination / "Temp");
    // Fail clearly when the selected directory is not writable.
    auto probe = destination / (".write-test-" + sha256(random_bytes(12)).substr(0, 16));
    atomic_write(probe, {}); fs::remove(probe);
}
}
