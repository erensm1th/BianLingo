#include "bianlingo/blocklist.hpp"
#include <algorithm>

namespace bl {
namespace {
bool origin_valid(std::string_view s) {
    return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
    });
}

}
std::string block_name_key(std::string_view raw) {
    auto name = trim(std::string(raw));
    if (name.empty() || name.size() > 192 || std::any_of(name.begin(), name.end(), [](unsigned char c) {
            return c < 32 || c == 127 || std::string_view("/\\|;\"{}[]=,:").find(c) != std::string_view::npos;
        })) throw std::runtime_error("Invalid player name");
    utf32(name);
    auto dash = name.find('-');
    auto player = trim(name.substr(0, dash));
    if (player.empty() || player.find(' ') != player.npos) throw std::runtime_error("Invalid player name");
    std::string realm;
    if (dash != name.npos) {
        realm = name.substr(dash + 1);
        std::erase_if(realm, [](char c) { return c == ' ' || c == '-'; });
        if (realm.empty()) throw std::runtime_error("Realm name cannot be empty");
    }
    return lower_ascii(player + (realm.empty() ? "" : "-" + realm));
}
std::string block_player_name(const Message &m) {
    if (!m.event.starts_with("CHAT_MSG_") || m.event.starts_with("CHAT_MSG_BN_") ||
        m.event == "CHAT_MSG_COMMUNITIES_CHANNEL") return {};
    auto name = m.author.empty() ? m.player_name : m.author;
    if (!name.empty() && name.find('-') == name.npos && !m.realm.empty()) name += "-" + m.realm;
    try { block_name_key(name); return name; } catch (...) { return {}; }
}
PlayerBlocklist::PlayerBlocklist(const Json &state) : origin_("pc-" + sha256(random_bytes(16)).substr(0, 16)) {
    if (state.contains("origin") && state["origin"].is_string() && origin_valid(state["origin"].get<std::string>()))
        origin_ = state["origin"].get<std::string>();
    if (!state.empty()) merge(state);
}
Json PlayerBlocklist::snapshot(bool storage) const {
    Json out{{"schemaVersion", 1}, {"entries", Json::array()}};
    if (storage) out["origin"] = origin_;
    for (const auto &[key, r] : records_) out["entries"].push_back(Json::array({r.name, r.clock, r.origin, r.blocked}));
    return out;
}
bool PlayerBlocklist::merge(const Json &snapshot) {
    if (!snapshot.is_object() || snapshot.value("schemaVersion", 0) != 1 || !snapshot.contains("entries") ||
        !snapshot["entries"].is_array() || snapshot["entries"].size() > max_block_records)
        throw std::runtime_error("Invalid blocklist format");
    auto candidate = records_;
    auto clock = clock_;
    bool changed = false;
    for (const auto &entry : snapshot["entries"]) {
        if (!entry.is_array() || entry.size() != 4 || !entry[0].is_string() || !entry[1].is_number_integer() ||
            !entry[2].is_string() || !entry[3].is_boolean() ||
            entry[1].get<double>() < 1 || entry[1].get<double>() > 9007199254740991.0 ||
            !origin_valid(entry[2].get<std::string>())) throw std::runtime_error("Invalid blocklist record");
        auto name = trim(entry[0].get<std::string>()), key = block_name_key(name);
        Record record{name, entry[2].get<std::string>(), entry[1].get<uint64_t>(), entry[3].get<bool>()};
        clock = std::max(clock, record.clock);
        auto old = candidate.find(key);
        if (old == candidate.end() || record.clock > old->second.clock ||
            (record.clock == old->second.clock && (record.origin > old->second.origin ||
             (record.origin == old->second.origin && !record.blocked && old->second.blocked)))) {
            candidate[key] = std::move(record); changed = true;
        }
    }
    if (candidate.size() > max_block_records) throw std::runtime_error("The blocklist is full, including removal records");
    records_ = std::move(candidate); clock_ = clock;
    has_active_ = std::any_of(records_.begin(), records_.end(), [](const auto &entry) { return entry.second.blocked; });
    return changed;
}
bool PlayerBlocklist::set(std::string name, bool blocked) {
    name = trim(std::move(name));
    auto key = block_name_key(name);
    auto found = records_.find(key);
    if (found != records_.end() && found->second.blocked == blocked) return false;
    if (found == records_.end() && records_.size() >= max_block_records)
        throw std::runtime_error("The blocklist is full, including removal records");
    const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    if (clock_ >= 9007199254740991ULL) throw std::runtime_error("Blocklist version is out of range");
    clock_ = std::max(clock_, now) + 1;
    records_[key] = {std::move(name), origin_, clock_, blocked};
    has_active_ = blocked || std::any_of(records_.begin(), records_.end(), [](const auto &entry) { return entry.second.blocked; });
    return true;
}
bool PlayerBlocklist::blocks(const Message &message) const {
    if (!has_active_) return false;
    auto name = block_player_name(message);
    if (name.empty()) return false;
    auto key = block_name_key(name);
    for (const auto &match : {key, key.substr(0, key.find('-'))}) {
        auto found = records_.find(match);
        if (found != records_.end() && found->second.blocked) return true;
    }
    return false;
}
std::vector<std::string> PlayerBlocklist::names() const {
    std::vector<std::string> names;
    for (const auto &[key, r] : records_) if (r.blocked) names.push_back(r.name);
    return names;
}
std::string blocklist_import_code(const PlayerBlocklist &list) {
    const char *hex = "0123456789abcdef";
    const auto state = list.snapshot();
    std::string body;
    for (const auto &record : state.at("entries")) {
        for (unsigned char c : record[0].get_ref<const std::string &>()) {
            body += hex[c >> 4]; body += hex[c & 15];
        }
        body += "\t" + std::to_string(record[1].get<uint64_t>()) + "\t" + record[2].get<std::string>() +
                "\t" + (record[3].get<bool>() ? "1\n" : "0\n");
    }
    const auto checksum = adler(std::span(reinterpret_cast<const uint8_t *>(body.data()), body.size()));
    std::string sum;
    for (int shift = 28; shift >= 0; shift -= 4) sum += hex[(checksum >> shift) & 15];
    auto code = "BLGB1|" + sum + "|" + std::to_string(state.at("entries").size()) + "\n" + body;
    if (code.size() > max_block_import_bytes) throw std::runtime_error("Blocklist sync code is too large");
    return code;
}
} // namespace bl
