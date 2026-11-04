#include "bianlingo/bubble.hpp"
#include "bianlingo/blocklist.hpp"
#include "bianlingo/bergamot.hpp"
#include "bianlingo/ctranslate2.hpp"
#include "bianlingo/bundle.hpp"
#include "bianlingo/capture.hpp"
#include "bianlingo/chat_style.hpp"
#include "bianlingo/chat_tabs.hpp"
#include "bianlingo/core.hpp"
#include "bianlingo/journal.hpp"
#include "bianlingo/localization.hpp"
#include "bianlingo/ocr.hpp"
#include "bianlingo/provider.hpp"
#include "bianlingo/prompts.hpp"
#include "bianlingo/ollama_runtime.hpp"
#include "bianlingo/theme.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <set>

namespace bl {
namespace {
void sleep(StopToken stop, std::chrono::milliseconds duration) {
    stop.wait_for(duration);
}
void validate_settings(const Json &s) {
    if (!s.is_object())
        throw std::runtime_error("Invalid settings format");
    const auto expected = default_settings();
    if (!s.contains("ui_language") || !s.at("ui_language").is_string() ||
        !supported_ui_language(s.at("ui_language").get<std::string>()))
        throw std::runtime_error("Unsupported interface language");
    for (auto i = expected.begin(); i != expected.end(); ++i) {
        const auto &value = s.at(i.key());
        const bool valid = i.value().is_number_integer() ?
            value.is_number_integer() && value.get<double>() >= -2147483648.0 && value.get<double>() <= 2147483647.0 :
            i.value().is_number() ? value.is_number() : i.value().is_null() || value.type() == i.value().type();
        if (!valid) throw std::runtime_error("Invalid setting type: " + i.key());
    }
    validate_ocr(s);
    if (s["width"].get<int>() < 220 || s["width"].get<int>() > 32768 ||
        s["height"].get<int>() < 90 || s["height"].get<int>() > 32768 ||
        std::abs(s["x"].get<int64_t>()) > 131072 || std::abs(s["y"].get<int64_t>()) > 131072)
        throw std::runtime_error("Invalid window position or size");
    if (!s.at("chat_logging").is_boolean() || !s.at("log_retention_days").is_number_integer() ||
        s["log_retention_days"].get<int>() < 0 || s["log_retention_days"].get<int>() > 3650)
        throw std::runtime_error("Log retention must be 0–3650 days");
    for (auto k : {"incoming_prompt", "outbound_template", "outbound_default_template", "glossary_prefix",
                   "outbound_terms_prefix"})
        if (!s.at(k).is_string() || trim(s[k].get<std::string>()).empty() ||
            utf32(s[k].get<std::string>()).size() > 6000)
            throw std::runtime_error("Prompts must contain 1–6000 characters");
    if (s.at("outbound_template").get<std::string>().find("{language}") == std::string::npos ||
        s.at("outbound_default_template").get<std::string>().find("{language}") == std::string::npos)
        throw std::runtime_error("The output prompt must contain {language}");
    if (!s.at("outbound_style").is_string() ||
        (s.at("outbound_style") != "short" && s.at("outbound_style") != "default"))
        throw std::runtime_error("Unknown output style");
    if (!s.at("target_language").is_string() ||
        !supported_target_language(s.at("target_language").get<std::string>()))
        throw std::runtime_error("Unsupported chat target language");
    if (!s.at("base_url").is_string() ||
        (s["base_url"].get<std::string>().rfind("https://", 0) != 0 &&
         s["base_url"] != "http://127.0.0.1:11435/api/chat" &&
         s["base_url"] != "local://bergamot" &&
         s["base_url"] != "local://ctranslate2"))
        throw std::runtime_error("Online API URLs must use HTTPS");
    if (!s.at("model").is_string() || s["model"].get<std::string>().empty())
        throw std::runtime_error("The model name cannot be empty");
    if (!s.at("groups").is_array() || !s.at("channels").is_array())
        throw std::runtime_error("Invalid channel filter format");
    for (auto &g : s["groups"])
        if (!g.is_string())
            throw std::runtime_error("Invalid channel group format");
    for (auto &n : s["channels"])
        if (!n.is_number_integer() || n.get<int>() < 1 || n.get<int>() > 999)
            throw std::runtime_error("Channel numbers must be between 1 and 999");
    for (auto k :
         {"auto", "skip_target_language_nicknames", "terms_enabled", "show_original", "show_time",
          "class_colors", "show_class", "show_race", "show_sex", "click_through", "clear_background"})
        if (!s.at(k).is_boolean())
            throw std::runtime_error("Invalid toggle setting");
    bubble_appearance(s);
    double opacity = s.at("opacity").get<double>();
    validate_skin(s);
    auto tabs = parse_chat_tabs(s.at("chat_tabs"));
    if (!s.at("active_chat_tab").is_string() || std::none_of(tabs.begin(), tabs.end(), [&](auto &t) {
            return t.id == s["active_chat_tab"].get<std::string>();
        }))
        throw std::runtime_error("The active tab does not exist");
    double background = s.at("background_opacity").get<double>();
    int font = s.at("font_size").get<int>(), limit = s.at("max_messages").get<int>();
    if (!std::isfinite(background) || background < 0 || background > 1 || !std::isfinite(opacity) ||
        opacity < 0.1 || opacity > 1 || font < 9 || font > 36 || limit < 10 || limit > 200)
        throw std::runtime_error("Appearance settings are out of range");
    auto hotkey = s.at("hotkey");
    if (!hotkey.at("key").is_number_integer() || hotkey["key"].get<int>() < 1 ||
        hotkey["key"].get<int>() > 254 || !hotkey.at("modifiers").is_number_integer() ||
        hotkey["modifiers"].get<int>() < 0 || hotkey["modifiers"].get<int>() > 15)
        throw std::runtime_error("Invalid shortcut format");
}
std::string cache_key(const Json &settings, std::string_view text) {
    return sha256(settings.at("base_url").get<std::string>() + "\n" +
                  settings.at("model").get<std::string>() + "\n" +
                  settings.at("incoming_prompt").get<std::string>() + "\n" +
                  settings.at("target_language").get<std::string>() + "\n" +
                  settings.at("glossary_prefix").get<std::string>() + "\n" +
                  (settings.value("terms_enabled", true) ? "1" : "0") + "\n" + std::string(text));
}
// Only the rejected fields fall back. Credentials and custom prompts must not
// be discarded because an unrelated display/channel option is damaged.
Json recover_settings(const Json &saved, const Json &defaults) {
    auto recovered = defaults;
    for (auto i = saved.begin(); i != saved.end(); ++i) {
        if (!defaults.contains(i.key()) || i.key() == "chat_tabs" || i.key() == "active_chat_tab") continue;
        const auto &expected = defaults.at(i.key());
        if (expected.type() != i.value().type() && !(expected.is_number() && i.value().is_number())) continue;
        auto candidate = recovered;
        candidate[i.key()] = i.value();
        try { validate_settings(candidate); recovered = std::move(candidate); } catch (const std::exception &) {}
    }
    auto candidate = recovered;
    for (auto key : {"chat_tabs", "active_chat_tab"}) if (saved.contains(key)) candidate[key] = saved[key];
    try { normalize_chat_tabs(candidate); validate_settings(candidate); recovered = std::move(candidate); }
    catch (const std::exception &) {}
    return recovered;
}
} // namespace
Engine::Engine(std::filesystem::path directory)
    : data_directory_(std::move(directory)), settings_(default_settings()),
      usage_(read_json(data_directory_ / "usage-stats.json", {{"prompt_tokens", 0},
                                                              {"completion_tokens", 0},
                                                              {"total_tokens", 0},
                                                              {"cache_hit_tokens", 0},
                                                              {"requests", 0}})),
      glossary_(bundled_json("glossary_zhCN.json")), outbound_terms_(bundled_json("outbound_terms.json")) {
    try { blocklist_ = std::make_unique<PlayerBlocklist>(read_json(data_directory_ / "blocklist.json")); }
    catch (const std::exception &) {
        blocklist_ = std::make_unique<PlayerBlocklist>();
        notice({Notice::Status, {}, "Could not read the blocklist; check blocklist.json"});
    }
    auto saved = Json::object();
    const auto settings_path = data_directory_ / "settings.json";
    try {
        if (std::filesystem::exists(settings_path)) {
            saved = Json::parse(read_bytes(settings_path));
            if (!saved.is_object()) throw std::runtime_error("Invalid settings format");
        }
    } catch (const std::exception &) {
        saved = Json::object();
        settings_backup_required_ = true;
    }
    if (saved.contains("base_url") && saved["base_url"].is_string() && saved["base_url"] == "http://127.0.0.1:11434/api/chat")
        saved["base_url"] = "http://127.0.0.1:11435/api/chat";
    if (auto language = saved.find("ui_language"); language != saved.end() && language->is_string() &&
        supported_ui_language(language->get<std::string>())) settings_["ui_language"] = *language;
    settings_.update(default_prompt_settings(resolve_ui_language(settings_.at("ui_language").get<std::string>())));
    auto defaults = settings_;
    try {
        normalize_outbound_prompt(saved);
        normalize_locale_prompts(saved);
        normalize_player_display(saved);
        normalize_nickname_filter(saved);
        if (!saved.contains("background_opacity") &&
            (saved.contains("clear_background") || saved.contains("opacity")))
            saved["background_opacity"] = background_opacity(saved);
        for (auto i = saved.begin(); i != saved.end(); ++i)
            if (settings_.contains(i.key()))
                settings_[i.key()] = i.value();
        normalize_chat_tabs(settings_);
        normalize_bubble_settings(settings_);
        if (normalize_skin_settings(settings_))
            notice({Notice::Status, {}, "Invalid appearance settings were repaired"});
        validate_settings(settings_);
    } catch (const std::exception &) {
        settings_ = recover_settings(saved, defaults);
        settings_backup_required_ = true;
    }
    ocr_pure_ = ocr_pure_mode(settings_);
    display_tabs_ = parse_chat_tabs(settings_.at("chat_tabs"));
    auto path = data_directory_ / "provider.dpapi";
    try {
        if (std::filesystem::exists(path)) {
            auto raw = platform::protect(read_bytes(path), true);
            api_key_ = std::string(raw.begin(), raw.end());
        }
    } catch (const std::exception &) {
        notice({Notice::Status, {}, "Could not read the API key; enter it again"});
    }
    try {
        if (!settings_.value("connection_store", std::string()).empty())
            connections_ = open_connections(settings_["connection_store"].get<std::string>());
        else
            connections_ = Json::array({make_connection(settings_, api_key_)});
        auto active = settings_["active_connection"].get<std::string>();
        connections_ = fixed_local_connections(std::move(connections_), active);
        if (std::none_of(connections_.begin(), connections_.end(), [&](const auto &p) { return p.at("id") == active; })) {
            auto fallback = std::find_if(connections_.begin(), connections_.end(), [](const auto &p) {
                return !p.at("format").template get<std::string>().ends_with("_local");
            });
            if (fallback != connections_.end()) active = fallback->at("id").get<std::string>();
            else if (!connections_.empty()) active = connections_.front().at("id").get<std::string>();
            settings_backup_required_ = true;
            notice({Notice::Status, {}, "The selected connection is missing; saved connections were retained and an available one was selected"});
        }
        settings_["active_connection"] = active;
        validate_connections(connections_, settings_["active_connection"].get<std::string>());
    } catch (const std::exception &) {
        // Keep the unreadable encrypted store until an explicit credential edit;
        // preserve a recoverable copy before any later replacement.
        settings_backup_required_ = true;
        connections_ = Json::array({make_connection(settings_, "")});
        settings_["active_connection"] = "default";
        auto active = std::string("default");
        connections_ = fixed_local_connections(std::move(connections_), active);
        notice({Notice::Status, {}, "Could not read saved connections; enter and save the active key again"});
    }
    // Seed once without changing the selected connection or writing during load.
    // Saving the version also preserves an explicit deletion across restarts.
    if (settings_.at("api_presets_version") == 0) {
        auto seeded = add_default_api_connections(connections_);
        if (seeded != connections_) {
            connections_ = std::move(seeded);
            // The stored ciphertext describes the pre-seed list. An explicit
            // save must seal the new list rather than reuse that serialization.
            settings_["connection_store"] = "";
        }
        settings_["api_presets_version"] = 1;
    }
#ifdef _WIN32
    if (!settings_backup_required_ && !std::filesystem::exists(settings_path) &&
        !std::filesystem::exists(data_directory_ / "provider.dpapi"))
        settings_["active_connection"] = "local-bergamot";
#endif
    const auto &current = active_connection(connections_, settings_["active_connection"].get<std::string>());
    settings_["base_url"] = current["base_url"];
    settings_["model"] = current["model"];
    api_key_ = current["key"].get<std::string>();
    if (!usage_.is_object())
        usage_ = Json::object();
    if (!usage_.contains("by_connection") || !usage_["by_connection"].is_object()) {
        usage_.erase("by_connection");
        auto fallback = usage_;
        usage_["by_connection"] = {{"default", fallback}};
    }
    if (settings_backup_required_) {
        try { backup_recovered_settings(); }
        catch (const std::exception &) {
            notice({Notice::Status, {}, ui_text("Configuration backup failed. The original will not be overwritten. Check UserData permissions.")});
        }
    }
    output_session_ = uint32_t(std::time(nullptr));
}
Engine::~Engine() {
    stop();
}
void Engine::backup_recovered_settings() {
    if (!settings_backup_required_) return;
    const auto original = data_directory_ / "settings.json";
    if (std::filesystem::exists(original)) {
        const auto backup = data_directory_ / ("settings.recovery-" +
            std::to_string(std::time(nullptr)) + "-" + sha256(random_bytes(8)).substr(0, 12) + ".json");
        std::filesystem::copy_file(original, backup); // No overwrite, also preserves malformed bytes.
        notice({Notice::Status, {}, ui_text("Some settings were recovered. Original configuration backed up as ") + backup.filename().string()});
    }
    settings_backup_required_ = false;
}
void Engine::start() {
    if (!workers_.empty())
        return;
    journal_ = std::make_unique<ChatJournal>(data_directory_ / "Logs", settings_.value("chat_logging", false),
                                         settings_.value("log_retention_days", 0));
    workers_.emplace_back([this](auto stop) { capture_loop(stop); });
    workers_.emplace_back([this](auto stop) { ocr_loop(stop); });
    for (int i = 0; i < 2; i++)
        workers_.emplace_back([this](auto stop) { worker(stop); });
}
void Engine::clear_chat_logs() {
    std::lock_guard lock(mutex_);
    if (!journal_) journal_ = std::make_unique<ChatJournal>(data_directory_ / "Logs", false);
    journal_->clear();
}
void Engine::stop() {
    for (auto &w : workers_)
        w.request_stop();
    {
        std::lock_guard lock(mutex_);
        jobs_changed_.notify_all();
    }
    workers_.clear();
    {
        std::lock_guard lock(mutex_);
        if (journal_)
            for (const auto &j : jobs_)
                if (j.target.empty() || j.target[0] != '_')
                    journal_->result(j.message, "Skipped", "Application exit", !j.target.empty());
        jobs_.clear();
    }
    if (journal_)
        journal_->close();
}
Json Engine::settings() const {
    std::lock_guard lock(mutex_);
    return settings_;
}
bool Engine::has_key() const {
    std::lock_guard lock(mutex_);
    return provider_key_ready(
        active_connection(connections_, settings_["active_connection"].get<std::string>()));
}
Json Engine::connections() const {
    std::lock_guard lock(mutex_);
    return public_connections(connections_);
}
bool Engine::has_balance() const {
    std::lock_guard lock(mutex_);
    return active_connection(connections_, settings_["active_connection"].get<std::string>())
               .at("balance")
               .at("type") != "none";
}
void Engine::configure(Json s, std::optional<std::string> key, std::optional<Json> profiles) {
    std::lock_guard configuration_lock(configure_mutex_);
    normalize_nickname_filter(s);
    Json old_connections, old_settings;
    {
        std::lock_guard lock(mutex_);
        old_connections = connections_;
        old_settings = settings_;
    }
    auto defaults = default_settings();
    for (auto it = s.begin(); it != s.end(); ++it)
        if (defaults.contains(it.key()))
            defaults[it.key()] = it.value();
    s = std::move(defaults);
    if (s.at("ui_language") != old_settings.at("ui_language")) normalize_locale_prompts(s);
    auto next_connections =
        profiles ? merge_connections(std::move(*profiles), old_connections) : old_connections;
    auto active_id = s.at("active_connection").get<std::string>();
    next_connections = fixed_local_connections(std::move(next_connections), active_id);
    s["active_connection"] = active_id;
    for (auto &p : next_connections)
        if (p["id"] == active_id) {
            // Flattened fields describe the previously active profile. When
            // selecting another saved profile they must not overwrite it.
            if (!profiles && active_id == old_settings["active_connection"].get<std::string>()) {
                p["base_url"] = s["base_url"];
                p["model"] = s["model"];
            }
            if (key)
                p["key"] = trim(*key);
            s["base_url"] = p["base_url"];
            s["model"] = p["model"];
        }
    validate_connections(next_connections, active_id);
    validate_settings(s);
    auto next_tabs = parse_chat_tabs(s.at("chat_tabs"));
    const auto &next_active = active_connection(next_connections, active_id);
    auto next_key = next_active.at("key").get<std::string>();
    s["connection_store"] =
        next_connections == old_connections && !old_settings.value("connection_store", std::string()).empty()
            ? old_settings["connection_store"]
            : Json(seal_connections(next_connections));
    // One atomic settings replacement commits the active id and the encrypted
    // profile store together. No plaintext key/header/options file is created.
    auto stored_settings = s;
    stored_settings.erase("base_url");
    stored_settings.erase("model");
    backup_recovered_settings(); // A failed backup must leave the original untouched.
    write_json(data_directory_ / "settings.json", stored_settings);
    std::lock_guard lock(mutex_);
    if (journal_ && (settings_["chat_logging"] != s["chat_logging"] ||
                     settings_["log_retention_days"] != s["log_retention_days"]))
        journal_->set_policy(s["chat_logging"], s["log_retention_days"]);
    bool change = old_settings["active_connection"] != s["active_connection"] ||
                  active_connection(old_connections, old_settings["active_connection"].get<std::string>()) !=
                      next_active;
    for (auto k : {"auto", "groups", "channels", "skip_target_language_nicknames", "terms_enabled", "base_url",
                   "model", "ocr", "target_language", "incoming_prompt", "outbound_template", "outbound_default_template",
                   "glossary_prefix", "outbound_terms_prefix"})
        if (settings_[k] != s[k])
            change = true;
    if (key && *key != api_key_)
        change = true;
    if (change) {
        generation_++;
        if (journal_)
            for (const auto &j : jobs_)
                if (j.target.empty() || j.target[0] != '_')
                    journal_->result(j.message, "Skipped", "Settings changed", !j.target.empty());
        jobs_.clear();
        std::erase_if(notices_, [](const Notice &n) {
            return n.kind == Notice::Balance || n.kind == Notice::BalanceError ||
                   n.kind == Notice::Outbound || n.kind == Notice::OutboundError ||
                   n.kind == Notice::ProviderTest || n.kind == Notice::ProviderTestError;
        });
        if (outbound_pending_)
            notices_.push_back({Notice::OutboundError, {}, "Settings changed; submit the output translation again"});
        cache_.clear();
        cache_order_.clear();
    }
    if(settings_.at("ocr")!=s.at("ocr")) ++ocr_epoch_;
    settings_ = std::move(s);
    ocr_pure_ = ocr_pure_mode(settings_);
    display_tabs_ = std::move(next_tabs);
    connections_ = std::move(next_connections);
    api_key_ = std::move(next_key);
}
void Engine::notice(Notice n, std::optional<uint64_t> generation) {
    std::lock_guard lock(mutex_);
    if (generation && *generation != generation_)
        return;
    if (notices_.size() >= 1024)
        notices_.pop_front();
    notices_.push_back(std::move(n));
}
std::vector<Notice> Engine::drain() {
    std::lock_guard lock(mutex_);
    std::vector<Notice> out;
    out.reserve(notices_.size());
    while (!notices_.empty()) {
        if (notices_.front().kind == Notice::Outbound || notices_.front().kind == Notice::OutboundError)
            outbound_pending_ = false;
        if (notices_.front().kind != Notice::Chat || !blocklist_->blocks(notices_.front().chat.message))
            out.push_back(std::move(notices_.front()));
        notices_.pop_front();
    }
    return out;
}
Json Engine::blocklist() const {
    std::lock_guard lock(mutex_);
    return blocklist_->snapshot(true);
}
bool Engine::player_blocked(const Message &message) const {
    std::lock_guard lock(mutex_);
    return blocklist_->blocks(message);
}
void Engine::update_blocklist(const std::function<bool(PlayerBlocklist &)> &change) {
    std::lock_guard configuration_lock(configure_mutex_);
    auto candidate = [&] { std::lock_guard lock(mutex_); return *blocklist_; }();
    if (!change(candidate)) return;
    write_json(data_directory_ / "blocklist.json", candidate.snapshot(true));
    auto state = std::make_shared<const Json>(candidate.snapshot());
    {
        std::lock_guard lock(mutex_);
        *blocklist_ = std::move(candidate);
        std::erase_if(jobs_, [&](const Job &job) {
            const bool blocked = job.target.empty() && blocklist_->blocks(job.message);
            if (blocked && journal_) journal_->result(job.message, "Skipped", "Player blocked");
            return blocked;
        });
        std::erase_if(notices_, [&](const Notice &n) { return n.kind == Notice::Chat && blocklist_->blocks(n.chat.message); });
    }
    Notice n;
    n.kind = Notice::Blocklist; n.layout = std::move(state);
    notice(std::move(n));
}
void Engine::merge_blocklist(const Json &snapshot) {
    update_blocklist([&](PlayerBlocklist &list) { return list.merge(snapshot); });
}
void Engine::set_player_blocked(std::string name, bool blocked) {
    update_blocklist([&](PlayerBlocklist &list) { return list.set(std::move(name), blocked); });
}
void Engine::submit(Message m) {
    bool pure = ocr_pure_.load();
    if (pure && m.event != "OCR_CHAT")
        return;
    {
        std::lock_guard guard(ocr_mutex_);
        auto now = int64_t(std::time(nullptr));
        while (!recent_text_.empty() && (recent_text_.front().first < now - 60 || recent_text_.size() >= 128))
            recent_text_.pop_front();
        auto key = ocr_key(m.text);
        if (m.event == "OCR_CHAT") {
            if (!pure)
                for (auto &[time, text] : recent_text_)
                    if (text == key || (text.size() > 5 && key.ends_with(text)))
                        return;
        } else
            recent_text_.emplace_back(now, std::move(key));
    }
    if (journal_)
        journal_->received(m);
    bool eligible, blocked, accepts_latin;
    std::string chat_language;
    {
        std::lock_guard lock(mutex_);
        blocked = blocklist_->blocks(m);
        eligible = !blocked && allows(settings_, m) && std::any_of(display_tabs_.begin(), display_tabs_.end(),
                                                       [&](auto &tab) { return tab.matches(m); });
        chat_language = settings_.at("target_language").get<std::string>();
        const auto &profile = active_connection(connections_, settings_.at("active_connection").get<std::string>());
        accepts_latin = provider_uses_prompts(profile.at("format").get<std::string>());
    }
    if (!eligible || !should_translate_for_target(m.event == "OCR_CHAT" ? ocr_body(m.text) : m.text,
                                                   chat_language, accepts_latin)) {
        if (journal_)
            journal_->result(m, "Skipped", blocked ? "Player blocked" : "Channel or translation filter");
        return;
    }
    std::lock_guard lock(mutex_);
    if (jobs_.size() >= 100) {
        if (journal_)
            journal_->result(m, "Skipped", "The translation queue is full");
        notices_.push_back({Notice::Status, {}, "The translation queue is full; the new message was skipped"});
        return;
    }
    jobs_.push_back({std::move(m), "", generation_});
    jobs_changed_.notify_one();
}
void Engine::outbound(std::string text, std::string target) {
    if (trim(text).empty())
        return;
    std::lock_guard lock(mutex_);
    if (outbound_pending_)
        throw std::runtime_error("Previous message still translating, please wait; current input will be kept.");
    if (jobs_.size() >= 100)
        throw std::runtime_error("The translation queue is full");
    Message m;
    m.text = std::move(text);
    m.session = output_session_;
    m.seq = ++output_seq_;
    m.sent = uint32_t(std::time(nullptr));
    m.received = m.sent;
    m.event = "Send translation";
    if (journal_)
        journal_->received(m, true);
    jobs_.push_front(
        {std::move(m), std::move(target), generation_, settings_.value("outbound_style", "short")});
    outbound_pending_ = true;
    jobs_changed_.notify_one();
}
void Engine::test_connection() {
    std::lock_guard lock(mutex_);
    if (jobs_.size() >= 100)
        throw std::runtime_error("The task queue is full");
    jobs_.push_front({{}, "__provider_test", generation_});
    jobs_changed_.notify_one();
}
void Engine::query_balance() {
    std::lock_guard lock(mutex_);
    if (jobs_.size() >= 100)
        throw std::runtime_error("The task queue is full; refresh the balance later");
    jobs_.push_front({{}, "__balance", generation_});
    jobs_changed_.notify_one();
}
Json Engine::usage() const {
    std::lock_guard lock(mutex_);
    auto id = settings_["active_connection"].get<std::string>();
    return usage_.at("by_connection").value(id, Json::object());
}
std::string Engine::status() const {
    std::string capture = ocr_pure_.load()
                              ? (game_window_.load() ? "OCR-only mode: game window found" : "OCR-only mode: waiting for the game window")
                              : (connected_ ? (idle_ ? "Idle marker found; waiting for messages" : "Pixel bridge connected")
                                            : "Waiting for the game pixel bridge");
    std::string value = capture;
    if (journal_) {
        auto error = journal_->error();
        if (!error.empty())
            value += "\n" + error;
    }
    return value;
}
void Engine::capture_loop(StopToken stop) {
    BridgeCapture reader;
    Assembler assembler;
    ReplayTracker replays;
    std::map<uint32_t, uint64_t> sessions;
    uint64_t receive_order = 0, session_order = 0;
    uintptr_t window_id = 0;
    bool color_warning = false, was_pure = false;
    std::string last_error;
    try {
        auto screen = platform::make_capture();
        while (!stop.stop_requested()) {
            auto started = Clock::now();
            auto interval = std::chrono::milliseconds(1000);
            try {
                if (ocr_pure_.load()) {
                    if (!was_pure) {
                        reader.reset();
                        assembler.clear_partial();
                        window_id = 0;
                        color_warning = false;
                        was_pure = true;
                        connected_ = false;
                        idle_ = false;
                    }
                    auto game = screen->game_window();
                    uintptr_t id = game ? game->id : 0;
                    if (game_window_.exchange(id) != id) ++ocr_epoch_;
                    if (chat_state_.exchange(0) != 0) ++ocr_epoch_;
                    last_error.clear();
                    sleep(stop, std::chrono::milliseconds(500));
                    continue;
                }
                if (was_pure) {
                    reader.reset();
                    assembler.clear_partial();
                    was_pure = false;
                }
                auto step = reader.poll(*screen, started);
                if(game_window_.exchange(step.window_id)!=step.window_id) ++ocr_epoch_;
                int state=step.connected?step.chat_state:0;
                if(chat_state_.exchange(state)!=state) ++ocr_epoch_;
                interval = step.interval;
                if (step.color_warning && !color_warning) {
                    notice({Notice::Status,
                            {},
                            "Compact matrix verification keeps failing; switch to Native mode. HDR or color conversion may affect luminance levels."});
                    color_warning = true;
                } else if (step.packet && color_warning) {
                    notice({Notice::Status, {}, "Pixel capture recovered"});
                    color_warning = false;
                }
                if (step.window_id && step.window_id != window_id) {
                    assembler
                        .clear_partial(); // Preserve completed identities when switching back to a window.
                    window_id = step.window_id;
                }
                if (step.packet) {
                    auto m = assembler.accept(*step.packet);
                    if (auto summary = replays.observe(*step.packet, m, assembler))
                        notice({Notice::Status, {}, *summary});
                    if (m) {
                        if (m->event == "BRIDGE_CHAT_LAYOUT") {
                            try {
                                auto layout = parse_chat_layout(m->text);
                                Notice n;
                                n.kind = Notice::ChatLayout;
                                n.layout = std::make_shared<const Json>(std::move(layout));
                                notice(std::move(n));
                            } catch (const std::exception &e) {
                                notice({Notice::Status, {}, std::string("Invalid game chat layout: ") + e.what()});
                            }
                        } else if (m->event == "BRIDGE_BLOCKLIST") {
                            try { merge_blocklist(Json::parse(m->text)); }
                            catch (const std::exception &e) { notice({Notice::Status, {}, e.what()}); }
                        } else if (m->event == "BRIDGE_READY")
                            notice({Notice::Status, {}, "Game pixel bridge connected"});
                        else if (m->event.rfind("BRIDGE_REPLAY_", 0) != 0) {
                            if (!sessions.contains(m->session))
                                sessions[m->session] = ++session_order;
                            m->session_order = sessions.at(m->session);
                            m->receive_order = ++receive_order;
                            m->received = std::time(nullptr);
                            submit(std::move(*m));
                        }
                    }
                }
                idle_ = step.idle;
                if (step.connected != connected_.exchange(step.connected))
                    notice({Notice::Status, {}, step.connected ? "Game pixel bridge connected" : "Waiting for the game pixel bridge"});
                last_error.clear();
            } catch (const std::exception &e) {
                reader.reset();
                game_window_ = 0;
                chat_state_ = 0; ++ocr_epoch_;
                connected_ = false;
                if (last_error != e.what()) {
                    last_error = e.what();
                    notice({Notice::Status, {}, "Capture: " + last_error});
                }
            }
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
            sleep(stop, std::max(std::chrono::milliseconds(15), interval - elapsed));
        }
    } catch (const std::exception &e) {
        notice({Notice::Status, {}, "Could not start capture: " + std::string(e.what())});
    }
    connected_ = false;
}
void Engine::worker(StopToken stop) {
    while (!stop.stop_requested()) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            jobs_changed_.wait(lock, [&] { return stop.stop_requested() || !jobs_.empty(); });
            if (stop.stop_requested())
                break;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            if (job.generation != generation_)
                continue;
        }
        try {
            if (job.target == "__balance" || job.target == "__provider_test") {
                Json profile;
                std::string chat_language;
                {
                    std::lock_guard lock(mutex_);
                    if (job.generation != generation_)
                        continue;
                    profile =
                        active_connection(connections_, settings_["active_connection"].get<std::string>());
                    chat_language = settings_["target_language"].get<std::string>();
                }
                if (job.target == "__provider_test") {
                    const bool english_target = chat_language == "en-US" || chat_language == "en-GB";
                    provider_call(profile, "You are a connection checker. Reply OK only.",
                                  (profile.at("format") == "bergamot_local" ||
                                   profile.at("format") == "ctranslate2_local")
                                      ? bundled_json("connection-test.json").at(english_target ? "zh" : "en").get<std::string>()
                                      : "Reply OK only.", true, "English", chat_language, stop);
                    notice({Notice::ProviderTest, {}, "Connection test passed; the selected model responded"}, job.generation);
                } else {
                    auto request = provider_balance_request(profile);
                    auto data =
                        platform::http_json(request.url, request.body, {}, request.get, request.headers, stop);
                    auto view = std::make_shared<BalanceView>(
                        provider_balance(profile, data, int64_t(std::time(nullptr))));
                    notice({Notice::Balance, {}, "", std::move(view)}, job.generation);
                }
                continue;
            }
            std::string source;
            auto result = translate(job, source, stop);
            if (journal_)
                journal_->result(job.message, source.empty() ? "Translation completed" : source, result,
                                 !job.target.empty());
            {
                std::lock_guard lock(mutex_);
                if (job.generation != generation_)
                    continue;
            }
            if (job.target.empty())
                notice({Notice::Chat, {job.message, result, source, ""}, ""}, job.generation);
            else
                notice({Notice::Outbound, {}, result}, job.generation);
        } catch (const std::exception &e) {
            if (journal_ && (job.target.empty() || job.target[0] != '_'))
                journal_->result(job.message, "Failed", e.what(), !job.target.empty());
            if (job.target == "__provider_test") {
                notice({Notice::ProviderTestError, {}, e.what()}, job.generation);
            } else if (job.target == "__balance") {
                notice({Notice::BalanceError, {}, e.what()}, job.generation);
            } else if (job.target.empty())
                notice({Notice::Chat, {job.message, "", "", e.what()}, ""}, job.generation);
            else
                notice({Notice::OutboundError, {}, e.what()}, job.generation);
        }
    }
}
std::string Engine::provider_call(const Json &profile, std::string_view system, std::string_view text,
                                  bool incoming, std::string_view target,
                                  std::string_view chat_language, StopToken stop) {
    if (stop.stop_requested()) throw std::runtime_error("Request cancelled.");
    if (profile.at("format") == "bergamot_local") {
#ifdef _WIN32
        return bergamot_translate(text, incoming, target, chat_language, stop);
#else
        throw std::runtime_error("Bergamot is currently available on Windows only");
#endif
    }
    if (profile.at("format") == "ctranslate2_local") {
#ifdef _WIN32
        return ctranslate2_translate(text, incoming, target, chat_language, stop);
#else
        throw std::runtime_error("CTranslate2 is currently available on Windows only");
#endif
    }
#ifdef _WIN32
    if (profile.at("format") == "ollama_local") ensure_portable_ollama(stop);
#endif
    auto request = provider_request(profile, system, text);
    auto response = platform::http_json(request.url, request.body, {}, false, request.headers, stop);
    auto result = provider_result(profile, response);
    bool usage_save_failed = false;
    {
        std::lock_guard lock(mutex_);
        add_provider_usage(usage_, result.usage);
        auto id = profile.at("id").get<std::string>();
        add_provider_usage(usage_["by_connection"][id], result.usage);
        try {
            write_json(data_directory_ / "usage-stats.json", usage_);
        } catch (const std::exception &) {
            usage_save_failed = true;
        }
    }
    if (usage_save_failed)
        notice({Notice::Status, {}, "Token usage could not be saved; this request result is still available"});
    if (!result.error.empty())
        throw std::runtime_error(result.error);
    return result.text;
}
std::string Engine::translate(const Job &job, std::string &source, StopToken stop) {
    if (stop.stop_requested()) throw std::runtime_error("Request cancelled.");
    auto text = clean_chat(job.message.text);
    if (utf32(text).size() > 3000)
        throw std::runtime_error("The message is too long");
    Json settings, profile;
    {
        std::lock_guard lock(mutex_);
        if (job.generation != generation_)
            throw std::runtime_error("The connection changed; the previous task was cancelled");
        settings = settings_;
        if (job.target.empty() && blocklist_->blocks(job.message))
            throw std::runtime_error("The player is blocked; translation skipped");
        profile = active_connection(connections_, settings_["active_connection"].get<std::string>());
    }
    bool incoming = job.target.empty(), terms = settings.value("terms_enabled", true);
    auto incoming_text = job.message.event == "OCR_CHAT" ? ocr_body(job.message.text) : job.message.text;
    auto links = protect_chat_links(incoming ? incoming_text : job.message.text);
    text = links.text;
    // A dropdown change only affects the next submission, not a queued request.
    if (!incoming)
        settings["outbound_style"] = job.style;
    if (incoming && skip_nickname_translation(settings, job.message)) {
        source = ui_text("Target-script name; translation skipped");
        return job.message.text;
    }
    const bool uses_prompts = provider_uses_prompts(profile.at("format").get<std::string>());
    const bool needs_glossary = terms && (uses_prompts || (incoming && settings.at("target_language") == "zh-Hans"));
    auto matches = needs_glossary ? glossary_.matches(prose_text(incoming_text)) : std::vector<Term>{};
    if (incoming) {
        if (!should_translate_for_target(incoming_text, settings.at("target_language").get<std::string>(),
                                         uses_prompts)) {
            source = "Original";
            return job.message.text;
        }
        if (auto local = local_incoming_translation(settings, incoming_text, matches, glossary_)) {
            source = "Glossary";
            return *local;
        }
    } else {
        if (auto local = local_outbound_translation(settings, text, job.target, outbound_terms_)) {
            source = "Glossary";
            return *local;
        }
    }
    std::string cache_id = incoming
                               ? std::to_string(job.generation) + ":" + sha256(canonical(profile)) + ":" +
                                     cache_key(settings, links.links.empty() ? text : job.message.text)
                               : "";
    std::shared_future<std::string> future;
    std::shared_ptr<std::promise<std::string>> promise;
    if (incoming) {
        std::lock_guard lock(mutex_);
        if (auto i = cache_.find(cache_id); i != cache_.end()) {
            source = "Cache";
            return links.restore(i->second);
        }
        if (auto i = inflight_.find(cache_id); i != inflight_.end())
            future = i->second;
        else {
            promise = std::make_shared<std::promise<std::string>>();
            inflight_[cache_id] = promise->get_future().share();
        }
    }
    if (future.valid()) {
        source = "Cache";
        while (future.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready)
            if (stop.stop_requested()) throw std::runtime_error("Request cancelled.");
        return links.restore(future.get());
    }
    try {
        const auto system = uses_prompts
                                ? translation_prompt(settings, incoming, job.style, job.target, links, matches, outbound_terms_)
                                : std::string();
        auto result = provider_call(profile, system, text, incoming, job.target,
                                    settings.at("target_language").get<std::string>(), stop);
        auto displayed = links.restore(result);
        {
            std::lock_guard lock(mutex_);
            if (incoming && job.generation == generation_) {
                cache_[cache_id] = result;
                cache_order_.push_back(cache_id);
                while (cache_order_.size() > 1000) {
                    cache_.erase(cache_order_.front());
                    cache_order_.pop_front();
                }
            }
        }
        if (promise) {
            promise->set_value(result);
            std::lock_guard lock(mutex_);
            inflight_.erase(cache_id);
        }
        source = profile.at("format") == "ollama_local" ? "Local Ollama"
                 : profile.at("format") == "bergamot_local" ? "Local Bergamot"
                 : profile.at("format") == "ctranslate2_local" ? "Local CTranslate2" : "AI";
        return displayed;
    } catch (...) {
        if (promise) {
            promise->set_exception(std::current_exception());
            std::lock_guard lock(mutex_);
            inflight_.erase(cache_id);
        }
        throw;
    }
}
} // namespace bl
