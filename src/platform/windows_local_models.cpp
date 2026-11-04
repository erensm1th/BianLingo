#include "bianlingo/local_models.hpp"
#include <algorithm>
#include <fstream>
#include <zstd.h>

namespace bl {
namespace {
std::mutex preparation_mutex;
bool language_valid(std::string_view s) {
    return s.size() >= 2 && s.size() <= 20 && s.front() != '-' && s.back() != '-' &&
        std::all_of(s.begin(), s.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        });
}
std::string file_role(const Json &r) { return r.value("role", r.at("name").get<std::string>().substr(0, r.at("name").get<std::string>().find('.'))); }
bool digest_valid(const Json &v) {
    return v.is_string() && v.get_ref<const std::string &>().size() == 64 &&
        std::all_of(v.get_ref<const std::string &>().begin(), v.get_ref<const std::string &>().end(),
                    [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
Bytes read_model(const std::filesystem::path &path, uint64_t maximum, StopToken cancel = {}) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || uint64_t(input.tellg()) > maximum) throw std::runtime_error("A model file is missing or too large");
    Bytes bytes(size_t(input.tellg()), 0);
    input.seekg(0);
    for (size_t offset = 0; offset < bytes.size();) {
        if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
        auto count = std::min(size_t(65536), bytes.size() - offset);
        if (!input.read(reinterpret_cast<char *>(bytes.data() + offset), std::streamsize(count)))
            throw std::runtime_error("Could not read the model");
        offset += count;
    }
    return bytes;
}
bool present(const std::filesystem::path &file, uint64_t size) {
    std::error_code error;
    return std::filesystem::is_regular_file(file, error) && std::filesystem::file_size(file, error) == size && !error;
}
void check_stop(std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("Model preparation cancelled");
}
}
std::string local_pack_for_target(std::string_view target) {
    if (target == "zh" || target == "zh-CN" || target == "zh-SG" || target == "zh-Hans") return "zh-Hans";
    if (target == "zh-Hant" || target == "zh-TW" || target == "zh-HK") return "zh-Hant";
    auto primary = std::string(target.substr(0, target.find('-')));
    return language_valid(primary) ? primary : std::string();
}
BergamotModels::BergamotModels(std::filesystem::path root, std::filesystem::path cache)
    : root_(std::move(root)), cache_(std::move(cache)) {
    if (!std::filesystem::is_regular_file(root_ / "models.json"))
        throw std::runtime_error("Bergamot components are missing. Install the full package with models.");
    try { files_ = Json::parse(read_bytes(root_ / "models.json")).at("models"); }
    catch (const Json::exception &) { throw std::runtime_error("Invalid Bergamot model manifest"); }
    if (!files_.is_array() || files_.empty() || files_.size() > 4096) throw std::runtime_error("Invalid Bergamot model manifest");
    std::map<std::string, std::set<std::string>> roles;
    std::set<std::string> names;
    for (const auto &r : files_) {
        const auto pair = r.at("pair").get<std::string>(), name = r.at("name").get<std::string>();
        auto source = r.at("source").get<std::string>(), destination = r.at("target").get<std::string>();
        bool known = language_valid(source) && language_valid(destination) && source != destination && pair == source + "-" + destination;
        const auto role = file_role(r);
        if (role != "model" && role != "lex" && role != "vocab" && role != "srcvocab" && role != "trgvocab") known = false;
        if (!known || name.empty() || name.size() > 100 || name == "." || name == ".." ||
            !std::all_of(name.begin(), name.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
            }) || !names.insert(pair + "/" + name).second || !digest_valid(r.at("sha256")) ||
            !r.at("size").is_number_unsigned() || r.at("size").get<uint64_t>() == 0 || r.at("size").get<uint64_t>() > 64 * 1024 * 1024 ||
            !r.at("url").get<std::string>().starts_with("https://firefox-settings-attachments.cdn.mozilla.net/") ||
            (r.contains("compressed_sha256") && !digest_valid(r["compressed_sha256"])))
            throw std::runtime_error("The Bergamot manifest contains an invalid file");
        if (!roles[pair].insert(role).second) throw std::runtime_error("The Bergamot manifest contains duplicate file roles");
        pairs_[pair] = {source, destination};
    }
    for (const auto &[pair, present_roles] : roles)
        if (!present_roles.contains("model") || !present_roles.contains("lex") ||
            (!present_roles.contains("vocab") && (!present_roles.contains("srcvocab") || !present_roles.contains("trgvocab"))))
            throw std::runtime_error("The Bergamot model manifest is incomplete");
}
std::vector<LocalLanguagePack> BergamotModels::inventory() const {
    std::vector<LocalLanguagePack> result;
    for (const auto &[id, direction] : pairs_) {
        LocalLanguagePack pack;
        pack.id = id; pack.source = direction.first; pack.target = direction.second;
        pack.label = pack.source + " → " + pack.target; pack.installed = pack.bundled = true;
        for (const auto &r : files_) {
            const auto pair = r.at("pair").get<std::string>(), name = r.at("name").get<std::string>();
            if (pair != id) continue;
            auto size = r.at("size").get<uint64_t>(); pack.bytes += size;
            auto installed = present(cache_ / "models" / pair / name, size);
            if (installed) pack.installed_bytes += size;
            pack.installed = pack.installed && installed;
            pack.bundled = pack.bundled && std::filesystem::is_regular_file(root_ / "compressed" / pair / (name + ".zst"));
        }
        result.push_back(std::move(pack));
    }
    return result;
}
void BergamotModels::prepare(const std::vector<std::string> &packs, std::stop_token stop, const Progress &progress,
                             bool network, const Fetch &fetch) {
    if (packs.empty()) throw std::runtime_error("Select at least one language pack");
    std::set<std::string> pairs;
    for (const auto &id : packs) {
        if (!pairs_.contains(id)) throw std::runtime_error("This local engine has no pack for the selected language");
        pairs.insert(id);
    }
    prepare_files(pairs, stop, progress, network, fetch);
}
void BergamotModels::prepare_files(const std::set<std::string> &pairs, std::stop_token stop, const Progress &progress,
                                  bool network, const Fetch &fetch) {
    std::unique_lock lock(preparation_mutex, std::defer_lock);
    if (!network) {
        if (!lock.try_lock()) throw std::runtime_error("A language pack is being prepared. Try again shortly.");
    } else {
        while (!lock.try_lock()) { check_stop(stop); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    }
    uint64_t done = 0, total = 0;
    for (const auto &r : files_) if (pairs.contains(r.at("pair").get<std::string>())) total += r.at("size").get<uint64_t>();
    for (const auto &r : files_) {
        check_stop(stop);
        auto pair = r.at("pair").get<std::string>(), name = r.at("name").get<std::string>();
        if (!pairs.contains(pair)) continue;
        const auto size = r.at("size").get<uint64_t>();
        auto emit = [&](uint64_t bytes) { check_stop(stop); if (progress) progress({pair, name, done + std::min(bytes, size), total}); };
        emit(0);
        auto target = cache_ / "models" / pair / name;
        const auto expected = r.at("sha256").get<std::string>();
        if (!present(target, size) || sha256(read_model(target, size)) != expected) {
            auto bundled = root_ / "compressed" / pair / (name + ".zst");
            Bytes content;
            if (std::filesystem::is_regular_file(bundled)) {
                auto packed = read_model(bundled, 64 * 1024 * 1024);
                if (!r.contains("compressed_sha256") || sha256(packed) != r.at("compressed_sha256").get<std::string>())
                    throw std::runtime_error("Compressed Bergamot model verification failed");
                content.resize(size);
                auto actual = ZSTD_decompress(content.data(), content.size(), packed.data(), packed.size());
                if (ZSTD_isError(actual) || actual != size) throw std::runtime_error("Bergamot model unpacking failed");
            } else {
                if (!network) throw std::runtime_error("Language pack missing. Prepare it in Settings before playing.");
                if (r.contains("compressed_sha256")) throw std::runtime_error("Bundled model files are missing. Reinstall the full package.");
                std::filesystem::create_directories(target.parent_path());
                auto temporary = target; temporary += L".download";
                try {
                    auto callback = [&](uint64_t bytes) { emit(bytes); };
                    if (fetch) fetch(r.at("url").get<std::string>(), temporary, size, callback);
                    else download::download(r.at("url").get<std::string>(), temporary, size, callback);
                    content = read_model(temporary, size);
                    std::filesystem::remove(temporary);
                } catch (...) { std::error_code ignored; std::filesystem::remove(temporary, ignored); throw; }
            }
            check_stop(stop);
            if (content.size() != size || sha256(content) != expected) throw std::runtime_error("Bergamot model verification failed");
            atomic_write(target, content);
        }
        emit(size); done += size;
    }
    for (const auto &pair : pairs) {
        std::map<std::string, std::string> names;
        for (const auto &r : files_) if (r.at("pair") == pair) names[file_role(r)] = r.at("name").get<std::string>();
        auto source_vocab = names.contains("vocab") ? names.at("vocab") : names.at("srcvocab");
        auto target_vocab = names.contains("vocab") ? names.at("vocab") : names.at("trgvocab");
        auto list = names.at("model") + "\n" + source_vocab + "\n" + target_vocab + "\n" + names.at("lex") + "\n";
        atomic_write(cache_ / "models" / pair / "files.list", std::span(reinterpret_cast<const uint8_t *>(list.data()), list.size()));
    }

}
void BergamotModels::prepare_pair(std::string_view pair, StopToken cancel) {
    if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
    if (!pairs_.contains(std::string(pair))) throw std::runtime_error("This local engine has no pack for the selected language");
    bool ready = true;
    for (const auto &r : files_) {
        if (r.at("pair").get<std::string>() != pair) continue;
        const auto size = r.at("size").get<uint64_t>();
        auto target = cache_ / "models" / pair / r.at("name").get<std::string>();
        ready = ready && present(target, size) && sha256(read_model(target, size, cancel)) == r.at("sha256").get<std::string>();
    }
    if (ready && std::filesystem::is_regular_file(cache_ / "models" / pair / "files.list")) return; // An unrelated background download cannot stall an installed language.
    if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
    prepare_files({std::string(pair)}, {}, {}, false, {});
    if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
}
std::vector<std::string> BergamotModels::route(std::string_view source, std::string_view target) const {
    auto from = local_pack_for_target(source), to = local_pack_for_target(target);
    if (from.empty() || to.empty() || from == to) throw std::runtime_error("This local engine has no pack for the selected language");
    if (pairs_.contains(from + "-" + to)) return {from + "-" + to};
    if (from != "en" && to != "en" && pairs_.contains(from + "-en") && pairs_.contains("en-" + to))
        return {from + "-en", "en-" + to};
    throw std::runtime_error("This local engine has no pack for the selected language");
}
std::vector<std::string> BergamotModels::route(std::string_view requested) const {
    for (const auto &[pair, direction] : pairs_) {
        auto prefix = direction.first + "-";
        if (requested.starts_with(prefix)) {
            auto target = requested.substr(prefix.size());
            if (language_valid(target)) {
                try { return route(direction.first, target); } catch (const std::exception &) {}
            }
        }
    }
    throw std::runtime_error("This local engine has no pack for the selected language");
}
} // namespace bl
