#pragma once
#include "bianlingo/core.hpp"
#include "bianlingo/download.hpp"
#include <stop_token>
#include <variant>

namespace bl {
struct LocalLanguagePack {
    std::string id, label, source, target;
    uint64_t bytes = 0;
    bool installed = false, bundled = false;
    uint64_t installed_bytes = 0;
};
struct ModelProgress {
    std::string pair, file;
    uint64_t done = 0, total = 0;
};
// Inventory only stats files. Preparation verifies hashes off the UI thread.
class BergamotModels {
  public:
    using Progress = std::function<void(const ModelProgress &)>;
    using Fetch = std::function<void(std::string_view, const std::filesystem::path &, uint64_t, const download::Progress &)>;
  private:
    std::filesystem::path root_, cache_;
    Json files_;
    std::map<std::string, std::pair<std::string, std::string>> pairs_;
    void prepare_files(const std::set<std::string> &, std::stop_token, const Progress &, bool, const Fetch &);
  public:
    BergamotModels(std::filesystem::path root, std::filesystem::path cache);
    std::vector<LocalLanguagePack> inventory() const;
    std::vector<std::string> route(std::string_view source, std::string_view target) const;
    std::vector<std::string> route(std::string_view requested_pair) const;
    void prepare(const std::vector<std::string> &packs, std::stop_token stop = {}, const Progress &progress = {},
                 bool network = true, const Fetch &fetch = {});
    void prepare_pair(std::string_view pair, StopToken cancel = {}); // Local files only, never downloads.
};
class CTranslate2Models {
    std::filesystem::path root_, cache_;
    Json models_, runtime_;
    std::function<void(const std::filesystem::path &, std::stop_token)> probe_;
    void prepare_runtime(std::stop_token);
  public:
    CTranslate2Models(std::filesystem::path root, std::filesystem::path cache,
                     Json models = {}, Json runtime = {},
                     std::function<void(const std::filesystem::path &, std::stop_token)> probe = {});
    std::vector<LocalLanguagePack> inventory() const;
    std::vector<std::string> route(std::string_view source, std::string_view target) const;
    std::vector<std::string> route(std::string_view requested_pair) const;
    void prepare(const std::vector<std::string> &, std::stop_token = {}, const BergamotModels::Progress & = {},
                 bool network = true, const BergamotModels::Fetch & = {});
    void prepare_pair(std::string_view, StopToken cancel = {}); // Offline validation only.
    std::filesystem::path model_directory(std::string_view) const;
    std::filesystem::path runtime_directory() const;
};
void probe_ct2_runtime(const std::filesystem::path &, const std::function<bool()> &cancelled = {});
// A common manager uses one immutable backend per task, not another copy of its UI.
class LocalModelStore {
    std::variant<BergamotModels, CTranslate2Models> store_;
  public:
    LocalModelStore(BergamotModels store) : store_(std::move(store)) {}
    LocalModelStore(CTranslate2Models store) : store_(std::move(store)) {}
    auto inventory() const { return std::visit([](const auto &s) { return s.inventory(); }, store_); }
    auto route(std::string_view source, std::string_view target) const {
        return std::visit([&](const auto &s) { return s.route(source, target); }, store_);
    }
    void prepare(const std::vector<std::string> &pairs, std::stop_token stop, const BergamotModels::Progress &progress,
                 bool network = true, const BergamotModels::Fetch &fetch = {}) {
        std::visit([&](auto &s) { s.prepare(pairs, stop, progress, network, fetch); }, store_);
    }
};
std::string local_pack_for_target(std::string_view target);
} // namespace bl
