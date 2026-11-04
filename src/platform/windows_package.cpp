#include "bianlingo/package.hpp"
#include "bianlingo/storage.hpp"
#include <windows.h>
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <memory>
#include <set>

namespace bl {
namespace {
namespace fs = std::filesystem;
constexpr uint64_t maximum = 256ULL * 1024 * 1024;
void require(bool condition) {
    if (!condition) throw std::runtime_error("Invalid embedded package");
}
bool digest_valid(const std::string &value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
void safe_path(const std::string &name) {
    require(!name.empty() && name.size() <= 240 && name.front() != '/' &&
            name.find_first_of("\\:<>\"|?*") == name.npos && name.find("//") == name.npos &&
            std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= 32 && c < 127; }));
    for (const auto &part : fs::path(name)) {
        require(part != "." && part != ".." && !part.empty() &&
                part.native().back() != L'.' && part.native().back() != L' ');
        const auto stem = lower_ascii(part.string().substr(0, part.string().find('.')));
        require(stem != "con" && stem != "prn" && stem != "aux" && stem != "nul" &&
                !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
                  stem.back() >= '1' && stem.back() <= '9'));
    }
    static const std::set<std::string> notices{
        "LICENSE", "THIRD_PARTY_NOTICES.md", "PROJECT_NOTICE.md", "PRIVACY.md",
        "SOURCE_ACCESS.md", "RELEASE_REVIEW.json", "artifact-inventory.json"};
    require(name.starts_with("data/bergamot/") || name.starts_with("data/ctranslate2/") || notices.contains(name));
}
void no_links(fs::path path) {
    for (path = fs::absolute(path); path != path.root_path(); path = path.parent_path()) {
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("The embedded package directory cannot use links");
    }
}
std::span<const uint8_t> resource(int id) {
    auto module = GetModuleHandleW(nullptr);
    auto entry = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!entry) return {};
    const auto size = SizeofResource(module, entry);
    const auto data = LockResource(LoadResource(module, entry));
    require(size && size <= maximum && data);
    return {static_cast<const uint8_t *>(data), size};
}
}
void unpack_package(std::span<const uint8_t> payload, const Json &metadata, const fs::path &destination) {
    require(destination.filename() == "Package");
    require(metadata.at("schema") == 1 && digest_valid(metadata.at("sha256").get<std::string>()));
    const auto &files = metadata.at("files");
    require(files.is_object() && !files.empty() && files.size() <= 64);
    std::set<std::string> names;
    uint64_t total = 0;
    for (auto item = files.begin(); item != files.end(); ++item) {
        safe_path(item.key());
        require(names.insert(lower_ascii(item.key())).second);
        const auto size = item.value().at("size").get<uint64_t>();
        require(size && size <= 80ULL * 1024 * 1024 && total <= maximum - size &&
                digest_valid(item.value().at("sha256").get<std::string>()));
        total += size;
    }
    no_links(destination);
    no_links(destination / ".package-ready.json");
    const auto ready = read_json(destination / ".package-ready.json");
    const auto metadata_digest = sha256(canonical(metadata));
    bool cached = ready.is_object() && ready.contains("sha256") && ready.at("sha256").is_string() &&
                  ready.at("sha256").get<std::string>() == metadata.at("sha256").get<std::string>() &&
                  ready.contains("metadata_sha256") && ready.at("metadata_sha256").is_string() &&
                  ready.at("metadata_sha256").get<std::string>() == metadata_digest;
    for (auto item = files.begin(); cached && item != files.end(); ++item) {
        const auto file = destination / item.key();
        no_links(file);
        std::error_code error;
        cached = fs::is_regular_file(file, error) && fs::file_size(file, error) == item.value().at("size").get<uint64_t>() && !error;
    }
    if (cached) return;
    require(!payload.empty() && payload.size() <= maximum && sha256(payload) == metadata.at("sha256").get<std::string>());
    fs::create_directories(destination.parent_path());
    const auto suffix = sha256(random_bytes(12)).substr(0, 16);
    const auto staged = destination.parent_path() / (".package-staged-" + suffix);
    const auto backup = destination.parent_path() / (".package-backup-" + suffix);
    require(fs::create_directory(staged));
    try {
        std::unique_ptr<archive, decltype(&archive_read_free)> input(archive_read_new(), archive_read_free);
        require(bool(input));
        require(archive_read_support_format_zip(input.get()) == ARCHIVE_OK &&
                archive_read_open_memory(input.get(), payload.data(), payload.size()) == ARCHIVE_OK);
        std::set<std::string> received;
        archive_entry *entry = nullptr;
        int status = ARCHIVE_OK;
        while ((status = archive_read_next_header(input.get(), &entry)) == ARCHIVE_OK) {
            const char *raw = archive_entry_pathname(entry);
            require(raw && archive_entry_filetype(entry) == AE_IFREG &&
                    !archive_entry_symlink(entry) && !archive_entry_hardlink(entry));
            const std::string name(raw);
            safe_path(name);
            require(files.contains(name) && received.insert(name).second);
            const auto size = files.at(name).at("size").get<size_t>();
            require(archive_entry_size(entry) >= 0 && uint64_t(archive_entry_size(entry)) == size);
            Bytes bytes(size);
            size_t offset = 0;
            while (offset < size) {
                const auto count = archive_read_data(input.get(), bytes.data() + offset, size - offset);
                require(count > 0 && uint64_t(count) <= size - offset);
                offset += size_t(count);
            }
            char extra;
            require(archive_read_data(input.get(), &extra, 1) == 0 && sha256(bytes) == files.at(name).at("sha256").get<std::string>());
            atomic_write(staged / name, bytes);
        }
        require(status == ARCHIVE_EOF && received.size() == files.size());
        write_json(staged / ".package-ready.json", {{"sha256", metadata.at("sha256")}, {"metadata_sha256", metadata_digest}});
        commit_directory(staged, destination, backup);
        // Both names are generated siblings of the checked destination.
        std::error_code ignored;
        fs::remove_all(backup, ignored);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(staged, ignored);
        throw;
    }
}
namespace platform {
fs::path package_root() {
    static const auto root = [] {
        const auto payload = resource(301), manifest = resource(302);
        if (payload.empty() && manifest.empty()) return executable_dir();
        require(!payload.empty() && !manifest.empty() && manifest.size() <= 1024 * 1024);
        const auto metadata = Json::parse(manifest.begin(), manifest.end());
        const auto destination = app_data() / "Package";
        unpack_package(payload, metadata, destination);
        return destination;
    }();
    return root;
}
}
}
