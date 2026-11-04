#include "bianlingo/localization.hpp"
#include "bianlingo/theme.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace bl {
uint32_t parse_skin_color(std::string_view value) {
    if (value.size() != 7 || value[0] != '#' ||
        !std::all_of(value.begin() + 1, value.end(), [](unsigned char c) { return std::isxdigit(c); }))
        throw std::runtime_error("Enter skin colors as #RRGGBB, for example #282828");
    return uint32_t(std::stoul(std::string(value.substr(1)), nullptr, 16));
}
std::string skin_color_hex(uint32_t c) {
    char out[8];
    std::snprintf(out, sizeof(out), "#%06X", unsigned(c & 0xffffff));
    return out;
}
SkinPalette skin_preset(std::string_view name) {
    return builtin_themes().preset(name);
}
std::vector<std::pair<std::string, std::string>> skin_options(std::string_view style) {
    return builtin_themes().palettes(style);
}
void change_ui_style(Json &s, std::string_view style) {
    builtin_themes().change_style(s, style);
}
SkinPalette skin_palette(const Json &s) {
    auto copy = s;
    if (!copy.contains("ui_style"))
        copy["ui_style"] = copy.value("skin", std::string()).rfind("terminal_", 0) == 0 ? "terminal" : "ios";
    return builtin_themes().resolve(copy).palette;
}
Json skin_colors(const SkinPalette &p) {
    return {{"background", skin_color_hex(p.background)},
            {"surface", skin_color_hex(p.surface)},
            {"text", skin_color_hex(p.text)},
            {"accent", skin_color_hex(p.accent)}};
}
namespace {
bool style_id(std::string_view id) {
    if (builtin_themes().is_builtin(id))
        return true;
    return id.size() <= 64 && !id.empty() && id.front() >= 'a' && id.front() <= 'z' && id.back() != '.' &&
           id.find('.') != id.npos && id.find("..") == id.npos &&
           std::all_of(id.begin(), id.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
           });
}
void validate_colors(const Json &c) {
    if (!c.is_object())
        throw std::runtime_error("Invalid custom color format");
    for (auto k : {"background", "surface", "text", "accent"})
        parse_skin_color(c.at(k).get<std::string>());
}
void validate_opacity_memory(const Json &value) {
    if (!value.contains("background_opacity"))
        return;
    const auto &v = value.at("background_opacity");
    if (!v.is_number())
        throw std::runtime_error("Invalid saved background opacity format");
    auto n = v.get<double>();
    if (!std::isfinite(n) || n < 0 || n > 1)
        throw std::runtime_error("Saved background opacity is out of range");
}
void validate_choice(std::string_view id, const std::string &name) {
    if (!style_id(id))
        throw std::runtime_error("Unknown design style");
    if (builtin_themes().is_builtin(id)) {
        auto options = skin_options(id);
        if (std::none_of(options.begin(), options.end(), [&](const auto &p) { return p.first == name; }))
            throw std::runtime_error("The palette does not match the design");
    } else if (name != "base" && name != "custom")
        throw std::runtime_error("Invalid community skin palette");
}
} // namespace
void validate_skin(const Json &s) {
    validate_choice(s.value("ui_style", std::string("ios")), s.value("skin", std::string("graphite")));
    validate_colors(s.at("skin_colors"));
    if (s.contains("style_memory")) {
        const auto &m = s.at("style_memory");
        if (!m.is_object() || m.size() > 128)
            throw std::runtime_error("Invalid saved appearance format");
        for (auto i = m.begin(); i != m.end(); ++i) {
            validate_choice(i.key(), i.value().at("skin").get<std::string>());
            validate_colors(i.value().at("colors"));
            validate_opacity_memory(i.value());
        }
    }
}
bool normalize_skin_settings(Json &s) {
    auto original = s;
    try {
        validate_choice(s.value("ui_style", std::string("ios")), s.value("skin", std::string("graphite")));
    } catch (...) {
        s["ui_style"] = "ios";
        s["skin"] = "graphite";
    }
    try {
        validate_colors(s.at("skin_colors"));
    } catch (...) {
        s["skin_colors"] = skin_colors(skin_preset("graphite"));
        if (s.value("skin", std::string()) == "custom") {
            s["ui_style"] = "ios";
            s["skin"] = "graphite";
        }
    }
    if (!s.contains("style_memory") || !s["style_memory"].is_object())
        s["style_memory"] = Json::object();
    auto &memory = s["style_memory"];
    for (auto i = memory.begin(); i != memory.end();) {
        try {
            validate_choice(i.key(), i.value().at("skin").get<std::string>());
            validate_colors(i.value().at("colors"));
            validate_opacity_memory(i.value());
            if (memory.size() > 128)
                throw std::runtime_error("limit");
            ++i;
        } catch (...) {
            i = memory.erase(i);
        }
    }
    return s != original;
}
void normalize_player_display(Json &s) {
    for (auto old : {"show_localized_class", "show_english_class"})
        if (s.contains(old) && s[old] == true)
            s["show_class"] = true;
    if (s.contains("show_english_race") && s["show_english_race"] == true)
        s["show_race"] = true;
    for (auto old : {"show_localized_class", "show_english_class", "show_english_race", "show_realm",
                     "show_player_name"})
        s.erase(old);
}
std::string player_display_name(const Message &m) {
    return m.author.substr(0, m.author.find('-'));
}
std::string player_badges(const Message &m, const Json &s) {
    static const std::map<std::string, std::string> class_names = {
        {"WARRIOR", "Warrior"}, {"PALADIN", "Paladin"}, {"HUNTER", "Hunter"},
        {"ROGUE", "Rogue"}, {"PRIEST", "Priest"},    {"DEATHKNIGHT", "Death Knight"},
        {"SHAMAN", "Shaman"},  {"MAGE", "Mage"},      {"WARLOCK", "Warlock"},
        {"MONK", "Monk"},    {"DRUID", "Druid"},   {"DEMONHUNTER", "Demon Hunter"},
        {"EVOKER", "Evoker"}};
    static const std::map<std::string, std::string> races = {
        {"Human", "Human"},   {"Orc", "Orc"},        {"Dwarf", "Dwarf"},     {"NightElf", "Night Elf"},
        {"Scourge", "Undead"}, {"Undead", "Undead"},     {"Tauren", "Tauren"},  {"Gnome", "Gnome"},
        {"Troll", "Troll"},   {"BloodElf", "Blood Elf"}, {"Draenei", "Draenei"}, {"Worgen", "Worgen"},
        {"Goblin", "Goblin"},  {"Pandaren", "Pandaren"}};
    std::string result;
    auto add = [&](bool on, const std::string &v) {
        if (on && !v.empty())
            result += "[" + ui_text(v) + "]";
    };
    auto cls = class_names.find(m.player_class), race = races.find(m.english_race);
    add(s.value("show_class", false), cls == class_names.end() ? m.localized_class : cls->second);
    add(s.value("show_race", false), race == races.end() ? m.race : race->second);
    add(s.value("show_sex", false), m.sex == 2 ? "Male" : m.sex == 3 ? "Female" : "");
    return result;
}
} // namespace bl
