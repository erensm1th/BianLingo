#include "bianlingo/chat_tabs.hpp"
#include "bianlingo/localization.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace bl {
namespace {
std::string name_key(std::string value) {
    auto zone = value.find(" - ");
    if (zone != value.npos)
        value.resize(zone);
    value = trim(value);
    for (auto &c : value)
        if (static_cast<unsigned char>(c) < 128)
            c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
void label(const std::string &s, size_t max) {
    if (trim(s).empty() || utf32(s).size() > max ||
        std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error("Invalid tab or channel name");
}
void object_keys(const Json &j, std::initializer_list<const char *> allowed) {
    if (!j.is_object())
        throw std::runtime_error("Invalid chat tab format");
    for (auto it = j.begin(); it != j.end(); ++it)
        if (std::none_of(allowed.begin(), allowed.end(), [&](auto key) { return it.key() == key; }))
            throw std::runtime_error("Unknown tab field: " + it.key());
}
void channel_values(const Json &j, std::set<int> &numbers, std::set<std::string> &names) {
    if (!j.is_array() || j.size() > 64)
        throw std::runtime_error("At most 64 channel conditions are allowed");
    for (auto &v : j) {
        if (v.is_number_integer()) {
            auto n = v.get<int64_t>();
            if (n < 1 || n > 999)
                throw std::runtime_error("Channel numbers must be between 1 and 999");
            numbers.insert(int(n));
        } else if (v.is_string()) {
            auto s = v.get<std::string>();
            label(s, 128);
            names.insert(name_key(s));
        } else
            throw std::runtime_error("A channel condition must be a number or name");
    }
}
bool channel_hit(const Message &m, const std::set<int> &numbers, const std::set<std::string> &names) {
    return numbers.contains(m.channel_number) || (!names.empty() && names.contains(name_key(m.channel_name)));
}
} // namespace
const std::vector<std::pair<std::string, std::string>> &chat_event_options() {
    static const std::vector<std::pair<std::string, std::string>> options = {
        {"CHAT_MSG_SAY", "Say"},
        {"CHAT_MSG_YELL", "Yell"},
        {"CHAT_MSG_PARTY", "Party"},
        {"CHAT_MSG_PARTY_LEADER", "Party leader"},
        {"CHAT_MSG_RAID", "Raid"},
        {"CHAT_MSG_RAID_LEADER", "Raid leader"},
        {"CHAT_MSG_RAID_WARNING", "Raid warning"},
        {"CHAT_MSG_INSTANCE_CHAT", "Instance Chat"},
        {"CHAT_MSG_INSTANCE_CHAT_LEADER", "Instance leader"},
        {"CHAT_MSG_GUILD", "Guild"},
        {"CHAT_MSG_OFFICER", "Officer"},
        {"CHAT_MSG_WHISPER", "Incoming whisper"},
        {"CHAT_MSG_WHISPER_INFORM", "Outgoing whisper"},
        {"CHAT_MSG_BN_WHISPER", "Incoming Battle.net whisper"},
        {"CHAT_MSG_BN_WHISPER_INFORM", "Outgoing Battle.net whisper"},
        {"CHAT_MSG_CHANNEL", "Public Channels"},
        {"CHAT_MSG_COMMUNITIES_CHANNEL", "Community"},
        {"OCR_CHAT", "OCR recognition (read-only)"}};
    return options;
}
Json new_chat_tab(std::string id, std::string name) {
    Json kinds = Json::array();
    for (auto &[event, _] : chat_event_options())
        kinds.push_back(event);
    return {{"id", std::move(id)},
            {"name", std::move(name)},
            {"events", kinds},
            {"channels", Json::array()},
            {"exclude_channels", Json::array()}};
}
std::vector<ChatTab> parse_chat_tabs(const Json &j) {
    if (!j.is_array() || j.empty() || j.size() > max_chat_tabs)
        throw std::runtime_error("Between 1 and 16 chat tabs are required");
    std::set<std::string> ids;
    std::vector<ChatTab> result;
    for (auto &v : j) {
        object_keys(v, {"id", "name", "events", "channels", "exclude_channels"});
        ChatTab t;
        t.id = v.at("id").get<std::string>();
        t.name = v.at("name").get<std::string>();
        label(t.name, 24);
        if (t.id.empty() || t.id.size() > 64 ||
            !std::all_of(
                t.id.begin(), t.id.end(),
                [](unsigned char c) { return (std::isalnum(c) && c < 128) || c == '-' || c == '_'; }) ||
            !ids.insert(t.id).second)
            throw std::runtime_error("Invalid or duplicate tab ID");
        auto &kinds = v.at("events");
        if (!kinds.is_array() || kinds.size() > chat_event_options().size())
            throw std::runtime_error("Invalid tab message types");
        for (auto &kind : kinds) {
            auto event = kind.get<std::string>();
            auto &known = chat_event_options();
            if (std::none_of(known.begin(), known.end(), [&](auto &p) { return p.first == event; }))
                throw std::runtime_error("Unsupported tab message type");
            t.events.insert(event);
        }
        channel_values(v.value("channels", Json::array()), t.channels, t.channel_names);
        channel_values(v.value("exclude_channels", Json::array()), t.excluded_channels,
                       t.excluded_channel_names);
        result.push_back(std::move(t));
    }
    return result;
}
bool ChatTab::matches(const Message &m) const {
    if (m.event == "BRIDGE_TEST")
        return true;
    if (!events.contains(m.event))
        return false;
    if (m.event != "CHAT_MSG_CHANNEL")
        return true;
    if (channel_hit(m, excluded_channels, excluded_channel_names))
        return false;
    return channels.empty() && channel_names.empty() || channel_hit(m, channels, channel_names);
}
bool normalize_chat_tabs(Json &s) {
    auto old = s;
    try {
        parse_chat_tabs(s.at("chat_tabs"));
    } catch (...) {
        s["chat_tabs"] = Json::array({new_chat_tab("main", ui_text("General"))});
    }
    if (!s.contains("active_chat_tab") || !s["active_chat_tab"].is_string() ||
        std::none_of(s["chat_tabs"].begin(), s["chat_tabs"].end(),
                     [&](auto &t) { return t["id"] == s["active_chat_tab"]; }))
        s["active_chat_tab"] = s["chat_tabs"][0]["id"];
    return old != s;
}
Json parse_channel_filter(std::string_view input) {
    std::string s(input);
    for (auto sep : {std::string("，"), std::string("；")})
        for (size_t p = 0; (p = s.find(sep, p)) != s.npos;)
            s.replace(p, sep.size(), ",");
    std::replace(s.begin(), s.end(), ';', ',');
    Json values = Json::array();
    std::istringstream stream(s);
    std::string part;
    while (std::getline(stream, part, ',')) {
        part = trim(part);
        if (part.empty())
            continue;
        bool numeric = std::all_of(part.begin(), part.end(),
                                   [](unsigned char c) { return std::isdigit(c) || std::isspace(c); });
        if (numeric) {
            std::istringstream tokens(part);
            std::string token;
            while (tokens >> token) {
                if (token.size() > 3)
                    throw std::runtime_error("Channel numbers must be between 1 and 999");
                values.push_back(std::stoi(token));
            }
        } else
            values.push_back(part);
    }
    std::set<int> n;
    std::set<std::string> names;
    channel_values(values, n, names);
    return values;
}
std::string format_channel_filter(const Json &j) {
    std::string out;
    for (auto &v : j) {
        if (!out.empty())
            out += ", ";
        out += v.is_string() ? v.get<std::string>() : v.dump();
    }
    return out;
}
Json parse_chat_layout(std::string_view text) {
    if (text.size() > 12288)
        throw std::runtime_error("Game chat layout is too large");
    std::vector<std::set<std::string>> objects;
    auto j = Json::parse(text, [&](int depth, Json::parse_event_t e, Json &v) {
        if (depth > 8)
            throw std::runtime_error("Game chat layout is nested too deeply");
        if (e == Json::parse_event_t::object_start)
            objects.emplace_back();
        if (e == Json::parse_event_t::key && !objects.back().insert(v.get<std::string>()).second)
            throw std::runtime_error("Game chat layout contains duplicate fields");
        if (e == Json::parse_event_t::object_end)
            objects.pop_back();
        return true;
    });
    object_keys(j, {"schemaVersion", "windows"});
    if (!j.at("schemaVersion").is_number_integer() || j.at("schemaVersion") != 1)
        throw std::runtime_error("Unsupported game chat layout version");
    auto tabs = parse_chat_tabs(j.at("windows"));
    for (auto &tab : tabs) {
        if (!tab.id.starts_with("wow-"))
            throw std::runtime_error("Invalid game window ID");
        auto suffix = tab.id.substr(4);
        if (suffix.empty() || suffix.size() > 2 ||
            !std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) { return std::isdigit(c); }) ||
            std::stoi(suffix) < 1 || std::stoi(suffix) > 16)
            throw std::runtime_error("Invalid game window ID");
    }
    return j;
}
Json import_chat_layout(const Json &layout) {
    return parse_chat_layout(layout.dump()).at("windows");
}

Json synchronize_chat_layout(const Json &settings, const Json &layout,
                             const std::function<void(const Json &)> &persist) {
    auto next = settings;
    next["chat_tabs"] = import_chat_layout(layout);
    normalize_chat_tabs(next);
    if (next != settings)
        persist(next);
    return next;
}

} // namespace bl
