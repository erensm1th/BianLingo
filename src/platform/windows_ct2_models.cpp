#include "bianlingo/local_models.hpp"
#include "bianlingo/bundle.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <fstream>
#include <windows.h>

namespace bl {
namespace {
namespace fs = std::filesystem;
std::mutex ct2_preparation;
void cancelled(std::stop_token stop) { if (stop.stop_requested()) throw std::runtime_error("Model preparation cancelled"); }
bool safe_name(std::string_view value) {
    return !value.empty() && value.size() < 150 && value != "." && value != ".." &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        });
}
bool hex(std::string_view value, size_t length) {
    return value.size() == length && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool present(const fs::path &file, uint64_t size) {
    std::error_code error;
    return fs::is_regular_file(file, error) && fs::file_size(file, error) == size && !error;
}
std::string file_hash(const fs::path &file, std::stop_token stop = {}, StopToken cancel = {}) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return {};
    auto context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || !EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr)) throw std::runtime_error("Could not verify the model");
    std::array<char, 65536> buffer{};
    while (input) {
        cancelled(stop);
        if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
        input.read(buffer.data(), buffer.size());
        if (input.gcount() && !EVP_DigestUpdate(context.get(), buffer.data(), size_t(input.gcount()))) throw std::runtime_error("Model verification failed");
    }
    if (!input.eof()) throw std::runtime_error("Could not read the model");
    std::array<unsigned char, 32> digest{}; unsigned size = 0;
    if (!EVP_DigestFinal_ex(context.get(), digest.data(), &size) || size != digest.size()) throw std::runtime_error("Model verification failed");
    std::string result; const char *digits = "0123456789abcdef";
    for (auto b : digest) { result += digits[b >> 4]; result += digits[b & 15]; }
    return result;
}
bool complete(const fs::path &directory, const Json &files) {
    for (auto i = files.begin(); i != files.end(); ++i)
        if (i.key() != "README.md" && !present(directory / i.key(), i.value()[0].get<uint64_t>())) return false;
    return true;
}
std::string id(const Json &model) { return model.at("source").get<std::string>() + "-" + model.at("target").get<std::string>(); }
const Json &find_model(const Json &models, std::string_view pair) {
    for (const auto &m : models) if (id(m) == pair) return m;
    throw std::runtime_error("This local engine has no pack for the selected language");
}
std::map<std::string, std::string> model_notices(const Json &model) {
    auto metadata = model; metadata.erase("files");
    return {{"ATTRIBUTION.json", metadata.dump(2) + "\n"},
            {"LICENSE-Conversion.txt", std::string(bundled_text("licenses/SentencePiece.txt"))},
            {"LICENSE-Upstream.txt", std::string(bundled_text(model.at("license") == "CC-BY-4.0" ? "licenses/CC-BY-4.0.txt" : "licenses/SentencePiece.txt"))},
            {"PROVENANCE.txt", "Original: https://huggingface.co/" + model.at("source_model").get<std::string>() +
                "/tree/" + model.at("license_evidence_revision").get<std::string>() +
                "\nThis is a license-evidence snapshot, NOT a proven original weight revision used by the converter.\n" +
                model.at("attribution").get<std::string>() + "\n" + model.at("changes").get<std::string>() +
                "\nConversion publisher declares Apache-2.0. Original model terms also apply; the conversion label does not replace them. Evidence review uses pinned publisher cards, not reproduction of the unrecorded original checkpoint.\n"
                "https://creativecommons.org/licenses/by/4.0/\nhttps://www.apache.org/licenses/LICENSE-2.0\n"}};
}
void install_model_notices(const fs::path &directory, const Json &model) {
    for (const auto &[name, text] : model_notices(model)) {
        auto path = directory / name;
        if (!present(path, text.size()) || sha256(read_bytes(path)) != sha256(text))
            atomic_write(path, std::span(reinterpret_cast<const uint8_t *>(text.data()), text.size()));
    }
}
void verify_native_runtime(const fs::path &directory, const Json &runtime, std::stop_token stop = {}, StopToken cancel = {}) {
    const auto binary = directory / "BianLingoCT2.exe";
    const auto manifest = read_json(directory / "native-build.json");
    const auto digest_bytes = read_bytes(directory / "engine.sha256");
    const auto expected = trim(std::string(digest_bytes.begin(), digest_bytes.end()));
    if (!hex(expected,64) || file_hash(binary,stop,cancel) != expected ||
        manifest.value("binary_sha256",std::string()) != expected ||
        manifest.value("source_lock_sha256",std::string()) != runtime.at("source_lock_sha256").get<std::string>() ||
        manifest.value("protocol",std::string()) != "BLCT3" || !fs::is_regular_file(directory / "engine-LICENSES.txt"))
        throw std::runtime_error("CTranslate2 native component verification failed; reinstall the complete package");
}
void fetch_checked(std::string_view url, const fs::path &temporary, uint64_t size, std::string_view digest,
                   std::stop_token stop, const BergamotModels::Fetch &fetch, const download::Progress &progress) {
    try {
        fs::create_directories(temporary.parent_path());
        auto callback = [&](uint64_t bytes) { cancelled(stop); if (progress) progress(bytes); };
        if (fetch) fetch(url, temporary, size, callback); else download::download(url, temporary, size, callback);
        cancelled(stop);
        if (!present(temporary, size) || file_hash(temporary, stop) != digest) throw std::runtime_error("CTranslate2 file size or SHA-256 verification failed");
    } catch (...) { std::error_code ignored; fs::remove(temporary, ignored); throw; }
}
}
CTranslate2Models::CTranslate2Models(fs::path root, fs::path cache, Json models, Json runtime,
        std::function<void(const fs::path &, std::stop_token)> probe)
    : root_(std::move(root)), cache_(std::move(cache)), probe_(std::move(probe)) {
    if (!probe_) probe_ = [](const auto &path, std::stop_token stop) {
        probe_ct2_runtime(path, [stop] { return stop.stop_requested(); });
    };
    models_ = models.is_null() ? Json::parse(bundled_text("ctranslate2-models.json")) : std::move(models);
    runtime_ = runtime.is_null() ? Json::parse(bundled_text("ctranslate2-runtime.json")) : std::move(runtime);
    models_ = models_.at("models");
    if (!models_.is_array() || models_.empty() || models_.size() > 1000) throw std::runtime_error("Invalid CTranslate2 model directory");
    std::set<std::string> ids;
    for (const auto &m : models_) {
        auto from = m.at("source").get<std::string>(), to = m.at("target").get<std::string>();
        auto pair = m.at("pair").get<std::string>(), repo = m.at("repository").get<std::string>();
        auto slash = repo.find('/');
        if (!safe_name(from) || !safe_name(to) || from == to || from.find('.') != from.npos || to.find('.') != to.npos ||
            !safe_name(pair) || !ids.insert(id(m)).second || slash == repo.npos || !safe_name(repo.substr(0, slash)) ||
            !safe_name(repo.substr(slash + 1)) || !hex(m.at("revision").get<std::string>(), 40) ||
            (m.at("license") != "Apache-2.0" && m.at("license") != "CC-BY-4.0") ||
            m.at("original_license") != m.at("license") || !hex(m.at("license_evidence_revision").get<std::string>(), 40) ||
            !m.at("source_model").get<std::string>().starts_with("Helsinki-NLP/opus-mt-") ||
            m.at("attribution").get<std::string>().empty() || m.at("changes").get<std::string>().empty())
            throw std::runtime_error("Invalid CTranslate2 model source");
        const auto &files = m.at("files");
        if (!files.is_object() || files.size() != 6) throw std::runtime_error("The CTranslate2 model files are incomplete");
        for (const auto *name : {"model.bin", "config.json", "shared_vocabulary.json", "source.spm", "target.spm", "README.md"}) {
            const auto &f = files.at(name);
            if (!f.is_array() || f.size() != 2 || !f[0].is_number_unsigned() || !f[0].get<uint64_t>() ||
                f[0].get<uint64_t>() > download::max_download || !hex(f[1].get<std::string>(), 64)) throw std::runtime_error("Invalid CTranslate2 file manifest");
        }
    }
    if (runtime_.at("kind") != "native-cpu" || runtime_.at("protocol") != "BLCT3" ||
        !hex(runtime_.at("source_lock_sha256").get<std::string>(),64))
        throw std::runtime_error("Invalid CTranslate2 native runtime manifest");
}
fs::path CTranslate2Models::model_directory(std::string_view pair) const {
    const auto &m = find_model(models_, pair);
    auto destination = cache_ / "models" / std::string(pair);
    if (complete(destination, m["files"])) return destination;
    auto fallback = root_ / "models" / m["pair"].get<std::string>();
    return complete(fallback, m["files"]) ? fallback : destination;
}
fs::path CTranslate2Models::runtime_directory() const {
    for (auto file : {"BianLingoCT2.exe", "engine.sha256", "native-build.json", "engine-LICENSES.txt"})
        if (!fs::is_regular_file(root_/file)) return {};
    return root_;
}

std::vector<LocalLanguagePack> CTranslate2Models::inventory() const {
    std::vector<LocalLanguagePack> out;
    for (const auto &m : models_) {
        LocalLanguagePack p; p.id = id(m); p.source = m["source"]; p.target = m["target"]; p.label = p.source + " → " + p.target;
        auto directory = model_directory(p.id); p.installed = true;
        for (auto i = m["files"].begin(); i != m["files"].end(); ++i) {
            auto size = i.value()[0].get<uint64_t>(); p.bytes += size;
            if (present(directory / i.key(), size)) p.installed_bytes += size; else p.installed = false;
        }
        out.push_back(std::move(p));
    }
    return out;
}
std::vector<std::string> CTranslate2Models::route(std::string_view source, std::string_view target) const {
    auto from = local_pack_for_target(source), to = local_pack_for_target(target);
    auto has = [&](std::string_view a, std::string_view b) { return std::any_of(models_.begin(), models_.end(), [&](const Json &m) { return m["source"].get<std::string>() == a && m["target"].get<std::string>() == b; }); };
    if (from != to && has(from, to)) return {from + "-" + to};
    if (from != "en" && to != "en" && from != to && has(from, "en") && has("en", to)) return {from + "-en", "en-" + to};
    throw std::runtime_error("This local engine has no pack for the selected language");
}
std::vector<std::string> CTranslate2Models::route(std::string_view requested) const {
    for (const auto &m : models_) {
        auto prefix = m["source"].get<std::string>() + "-";
        if (requested.starts_with(prefix)) { try { return route(m["source"].get<std::string>(), requested.substr(prefix.size())); } catch (const std::exception &) {} }
    }
    throw std::runtime_error("This local engine has no pack for the selected language");
}
void CTranslate2Models::prepare_runtime(std::stop_token stop) {
    cancelled(stop);
    const auto installed = runtime_directory();
    if (installed.empty()) throw std::runtime_error("CTranslate2 native component is missing; install the complete package");
    verify_native_runtime(installed,runtime_,stop);
    probe_(installed,stop);
}

void CTranslate2Models::prepare(const std::vector<std::string> &pairs, std::stop_token stop,
                              const BergamotModels::Progress &progress, bool network, const BergamotModels::Fetch &fetch) {
    if (pairs.empty()) throw std::runtime_error("Select at least one language pack");
    for (const auto &pair : pairs) find_model(models_, pair);
    std::unique_lock lock(ct2_preparation, std::defer_lock);
    if (!network) { if (!lock.try_lock()) throw std::runtime_error("A language pack is being prepared. Try again shortly."); }
    else while (!lock.try_lock()) { cancelled(stop); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    if (network) prepare_runtime(stop);
    else if (runtime_directory().empty()) throw std::runtime_error("Prepare the CTranslate2 runtime and language packs in settings");
    std::set<std::string> selected(pairs.begin(), pairs.end()); uint64_t done = 0, total = 0;
    for (const auto &pair : selected) for (const auto &f : find_model(models_, pair)["files"]) total += f[0].get<uint64_t>();
    for (const auto &pair : selected) {
        const auto &m = find_model(models_, pair);
        for (auto i = m["files"].begin(); i != m["files"].end(); ++i) {
            cancelled(stop); auto size = i.value()[0].get<uint64_t>(); auto expected = i.value()[1].get<std::string>();
            auto emit = [&](uint64_t bytes) { cancelled(stop); if (progress) progress({pair, i.key(), done + std::min(size, bytes), total}); };
            emit(0);
            auto destination = cache_ / "models" / pair / i.key();
            if (!present(destination, size) || file_hash(destination, stop) != expected) {
                if (!network && i.key() == "README.md") { emit(size); done += size; continue; }
                auto temporary = destination; temporary += ".download";
                auto fallback = root_ / "models" / m["pair"].get<std::string>() / i.key();
                try {
                    if (present(fallback, size) && file_hash(fallback, stop) == expected) {
                        fs::create_directories(destination.parent_path()); fs::copy_file(fallback, temporary, fs::copy_options::overwrite_existing);
                        cancelled(stop);
                        if (file_hash(temporary, stop) != expected) throw std::runtime_error("Model copy verification failed");
                    } else {
                        if (!network) throw std::runtime_error("Language pack missing. Prepare it in Settings before playing.");
                        auto url = "https://huggingface.co/" + m["repository"].get<std::string>() + "/resolve/" + m["revision"].get<std::string>() + "/" + i.key();
                        fetch_checked(url, temporary, size, expected, stop, fetch, emit);
                    }
                    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Could not commit the language pack file");
                } catch (...) { std::error_code ignored; fs::remove(temporary, ignored); throw; }
            }
            emit(size); done += size;
        }
        install_model_notices(cache_ / "models" / pair, m);
    }
}
void CTranslate2Models::prepare_pair(std::string_view pair, StopToken cancel) {
    if (cancel.stop_requested()) throw std::runtime_error("Request cancelled.");
    const auto &m = find_model(models_, pair); auto directory = model_directory(pair);
    bool ready = !runtime_directory().empty();
    for (auto i = m["files"].begin(); ready && i != m["files"].end(); ++i)
        if (i.key() != "README.md") ready = present(directory / i.key(), i.value()[0].get<uint64_t>()) && file_hash(directory / i.key(), {}, cancel) == i.value()[1].get<std::string>();
    if (!ready) prepare({std::string(pair)}, {}, {}, false, {});
    verify_native_runtime(runtime_directory(), runtime_, {}, cancel);
    // Fallback weights remain reusable. Add corrected notices without rewriting weights.
    install_model_notices(model_directory(pair), m);
}
} // namespace bl
