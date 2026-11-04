#include "bianlingo/bundle.hpp"
#include "bianlingo/chat_tabs.hpp"
#include "bianlingo/core.hpp"
#include "bianlingo/localization.hpp"
#include "bianlingo/ocr.hpp"
#include "bianlingo/prompts.hpp"
#include "resources.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

namespace bl {
namespace {
const std::string &glossary_context(const char *key) {
    static const auto meanings = bundled_json("glossary-context.json");
    return meanings.at(key).get_ref<const std::string &>();
}
}

std::u32string utf32(std::string_view in) {
    std::u32string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        uint32_t c = uint8_t(in[i++]);
        int n = 0;
        uint32_t min = 0;
        if (c < 128) {
        } else if ((c & 224) == 192) {
            c &= 31;
            n = 1;
            min = 128;
        } else if ((c & 240) == 224) {
            c &= 15;
            n = 2;
            min = 2048;
        } else if ((c & 248) == 240) {
            c &= 7;
            n = 3;
            min = 65536;
        } else
            throw std::runtime_error("Invalid UTF-8");
        for (int j = 0; j < n; j++) {
            if (i >= in.size() || (uint8_t(in[i]) & 192) != 128)
                throw std::runtime_error("Invalid UTF-8");
            c = (c << 6) | (uint8_t(in[i++]) & 63);
        }
        if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            throw std::runtime_error("Invalid Unicode");
        out.push_back(char32_t(c));
    }
    return out;
}
std::string utf8(std::u32string_view in) {
    std::string out;
    for (uint32_t c : in) {
        if (c < 128)
            out += char(c);
        else if (c < 2048) {
            out += char(192 | (c >> 6));
            out += char(128 | (c & 63));
        } else if (c < 65536) {
            out += char(224 | (c >> 12));
            out += char(128 | ((c >> 6) & 63));
            out += char(128 | (c & 63));
        } else {
            out += char(240 | (c >> 18));
            out += char(128 | ((c >> 12) & 63));
            out += char(128 | ((c >> 6) & 63));
            out += char(128 | (c & 63));
        }
    }
    return out;
}
std::string trim(std::string s) {
    auto space = [](unsigned char c) { return c <= 32; };
    auto a = std::find_if_not(s.begin(), s.end(), space);
    auto b = std::find_if_not(s.rbegin(), s.rend(), space).base();
    return a < b ? std::string(a, b) : std::string();
}
std::string lower_ascii(std::string s) {
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return s;
}
static std::string strip(std::string_view in, bool links) {
    std::string s(in);
    static const std::regex link(R"(\|H[^|]*\|h([\s\S]*?)\|h)"),
        color(R"(\|c[0-9a-fA-F]{8}|\|cn[A-Za-z0-9_]+:|\|r)"), texture(R"(\|[TA][\s\S]*?\|[ta])");
    s = std::regex_replace(s, link, links ? " " : "$1");
    s = std::regex_replace(s, color, "");
    s = std::regex_replace(s, texture, "");
    std::erase_if(s, [](unsigned char c) { return c < 9 || (c >= 11 && c < 32) || c == 127; });
    return trim(s);
}
std::string clean_chat(std::string_view s) {
    return strip(s, false);
}
std::string prose_text(std::string_view s) {
    return strip(s, true);
}
static bool han(char32_t c) {
    return (c >= 0x3400 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0x20000 && c <= 0x2fa1f);
}
bool nickname_uses_target_script(const Message &message, std::string_view language) {
    bool chinese = false, kana = false, korean = false, cyrillic = false, greek = false,
         arabic = false, latin = false;
    for (std::string_view name : {std::string_view(message.author), std::string_view(message.player_name)}) {
        for (auto c : utf32(name.substr(0, name.find('-')))) {
            chinese |= han(c);
            kana |= c >= 0x3041 && c <= 0x30ff;
            korean |= (c >= 0xac00 && c <= 0xd7af) || (c >= 0x1100 && c <= 0x11ff);
            cyrillic |= c >= 0x400 && c <= 0x52f;
            greek |= c >= 0x370 && c <= 0x3ff;
            arabic |= (c >= 0x620 && c <= 0x63f) || (c >= 0x641 && c <= 0x64a) ||
                      (c >= 0x66e && c <= 0x6d3) || (c >= 0x750 && c <= 0x77f);
            latin |= (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= 0xc0 && c <= 0x24f && c != 0xd7 && c != 0xf7) ||
                     (c >= 0x1e00 && c <= 0x1eff);
        }
    }
    const auto tag = lower_ascii(std::string(language));
    const auto base = tag.substr(0, tag.find_first_of("-_"));
    // This is a script heuristic, not language identification: Latin/Cyrillic/Han
    // are shared by multiple languages. Never infer a player's spoken language.
    if (base == "zh") return chinese && !kana && !korean;
    if (base == "ja") return (kana || chinese) && !korean;
    if (base == "ko") return korean;
    if (base == "ru" || base == "uk") return cyrillic;
    if (base == "el") return greek;
    if (base == "ar") return arabic;
    static const std::set<std::string> latin_languages{
        "en", "fr", "de", "es", "pt", "it", "pl", "nl", "sv", "da", "nb", "fi", "cs", "tr", "hu", "ro"};
    return latin_languages.contains(base) && latin && !chinese && !kana && !korean && !cyrillic && !greek && !arabic;
}
bool skip_nickname_translation(const Json &settings, const Message &message) {
    if (!settings.value("skip_target_language_nicknames", false) || message.event == "OCR_CHAT")
        return false;
    auto language = settings.value("target_language", std::string());
    if (language.empty()) language = default_target_language();
    return nickname_uses_target_script(message, language);
}
bool normalize_nickname_filter(Json &settings) {
    if (!settings.contains("skip_chinese_nicknames")) return false;
    if (!settings.contains("skip_target_language_nicknames"))
        settings["skip_target_language_nicknames"] = settings["skip_chinese_nicknames"];
    settings.erase("skip_chinese_nicknames");
    return true;
}
static bool foreign(char32_t c) {
    return (c >= 0x3040 && c <= 0x30ff) || (c >= 0xac00 && c <= 0xd7af) || (c >= 0x400 && c <= 0x52f) ||
           (c >= 0x370 && c <= 0x3ff) || (c >= 0x590 && c <= 0x8ff) || (c >= 0x900 && c <= 0x1fff);
}
static std::vector<std::pair<size_t, size_t>> words(std::string_view s) {
    std::vector<std::pair<size_t, size_t>> out;
    for (size_t i = 0; i < s.size();) {
        if (std::isalnum(uint8_t(s[i])) && uint8_t(s[i]) < 128) {
            auto a = i++;
            while (i < s.size() && uint8_t(s[i]) < 128 && (std::isalnum(uint8_t(s[i])) || s[i] == '\''))
                i++;
            out.push_back({a, i});
        } else
            i++;
    }
    return out;
}
bool should_translate(std::string_view raw) {
    auto s = prose_text(raw);
    if (s.empty())
        return false;
    auto unicode = utf32(s);
    size_t h = 0, latin = 0, other = 0;
    for (auto c : unicode) {
        if (foreign(c) && platform::is_letter(c))
            return true;
        if (han(c))
            h++;
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            latin++;
        else if (platform::is_letter(c))
            other++;
    }
    static const std::set<char32_t> traditional = [] {
        auto u = utf32(embedded::traditional);
        return std::set<char32_t>(u.begin(), u.end());
    }();
    if (h && std::any_of(unicode.begin(), unicode.end(), [](auto c) { return traditional.contains(c); }))
        return false;
    if (h) {
        static const std::set<std::string> terms = {
            "dps",  "t",   "n",   "mt",  "ot",  "tank", "healer", "heal", "buff", "debuff",
            "aoe",  "dot", "hot", "cd",  "gcd", "oom",  "afk",    "brb",  "gg",   "ok",
            "boss", "lfg", "lfm", "pve", "pvp", "dk",   "dh",     "ss",   "zs",   "fs",
            "ms",   "qs",  "lr",  "sm",  "dz",  "xd",   "dkp",    "roll"};
        static const std::regex ascii_word("[A-Za-z]+");
        size_t letters = 0, long_words = 0, unknown = 0;
        for (std::sregex_iterator it(s.begin(), s.end(), ascii_word), end; it != end; ++it) {
            auto w = lower_ascii(it->str());
            if (!terms.contains(w)) {
                unknown++;
                letters += w.size();
                if (w.size() >= 3)
                    long_words++;
            }
        }
        if (unknown)
            return letters >= 8 && (long_words >= 2 || (long_words >= 1 && unknown >= 4));
        return other > 0;
    }
    if (other)
        return true;
    // One ASCII letter, including letter+digits/punctuation, is not prose.
    return latin > 1;
}
std::string script_language(std::string_view s) {
    for (auto c : utf32(prose_text(s))) {
        if (c >= 0xac00 && c <= 0xd7af)
            return "Korean";
        if (c >= 0x3040 && c <= 0x30ff)
            return "Japanese";
        if (c >= 0x400 && c <= 0x52f)
            return "Cyrillic";
    }
    return "";
}
bool allows(const Json &settings, const Message &m) {
    if (!settings.value("auto", true))
        return false;
    if (ocr_pure_mode(settings) && m.event != "OCR_CHAT")
        return false;
    if (m.event == "OCR_CHAT")
        return settings.at("ocr").value("enabled", false);
    if (m.event == "BRIDGE_TEST")
        return true;
    static const std::map<std::string, std::vector<std::string>> groups = {
        {"party", {"CHAT_MSG_PARTY", "CHAT_MSG_PARTY_LEADER"}},
        {"raid", {"CHAT_MSG_RAID", "CHAT_MSG_RAID_LEADER", "CHAT_MSG_RAID_WARNING"}},
        {"instance", {"CHAT_MSG_INSTANCE_CHAT", "CHAT_MSG_INSTANCE_CHAT_LEADER"}},
        {"whisper", {"CHAT_MSG_WHISPER"}},
        {"bn", {"CHAT_MSG_BN_WHISPER"}},
        {"outgoing", {"CHAT_MSG_WHISPER_INFORM", "CHAT_MSG_BN_WHISPER_INFORM"}},
        {"guild", {"CHAT_MSG_GUILD", "CHAT_MSG_OFFICER"}},
        {"say", {"CHAT_MSG_SAY", "CHAT_MSG_YELL"}},
        {"community", {"CHAT_MSG_COMMUNITIES_CHANNEL"}},
        {"public", {"CHAT_MSG_CHANNEL"}}};
    for (auto &g : settings.at("groups")) {
        auto i = groups.find(g.get<std::string>());
        if (i != groups.end() && std::find(i->second.begin(), i->second.end(), m.event) != i->second.end()) {
            if (m.event != "CHAT_MSG_CHANNEL")
                return true;
            auto nums = settings.value("channels", Json::array());
            return nums.empty() || std::find(nums.begin(), nums.end(), m.channel_number) != nums.end();
        }
    }
    return false;
}
Glossary::Glossary(const Json &data) {
    for (auto &a : data.value("ambiguous", Json::array()))
        ambiguous_[a.get<std::string>()] = true;
    for (auto &a : data.value("uppercase", Json::array()))
        uppercase_[a.get<std::string>()] = true;
    auto terms = data.value("terms", Json::object());
    for (auto it = terms.begin(); it != terms.end(); ++it) {
        auto term = lower_ascii(it.key());
        auto w = words(term);
        if (w.empty() || !it.value().is_string())
            continue;
        index_[term.substr(w[0].first, w[0].second - w[0].first)].push_back(
            {term, it.value().get<std::string>()});
    }
    for (auto &[_, v] : index_)
        std::sort(v.begin(), v.end(), [](auto &a, auto &b) { return a.first.size() > b.first.size(); });
}
std::vector<Term> Glossary::matches(std::string_view s) const {
    auto lower = lower_ascii(std::string(s));
    std::vector<Term> out;
    size_t end = 0;
    for (auto [a, b] : words(lower)) {
        if (a < end)
            continue;
        auto it = index_.find(lower.substr(a, b - a));
        if (it == index_.end())
            continue;
        for (auto &[term, tr] : it->second) {
            size_t e = a + term.size();
            if (lower.compare(a, term.size(), term) != 0 ||
                (e < s.size() && uint8_t(s[e]) < 128 && std::isalnum(uint8_t(s[e]))))
                continue;
            auto original = std::string(s.substr(a, term.size()));
            if (uppercase_.contains(term)) {
                auto upper = term;
                for (char &c : upper)
                    if (c >= 'a' && c <= 'z')
                        c -= 32;
                if (original != upper)
                    continue;
            }
            auto translated = tr;
            if (term == "hot") {
                static const std::regex recruitment(
                    R"(\b(lf[0-9]*m|lfm|lfg|looking|need|recruit|recruiting|pst|dps)\b)", std::regex::icase),
                    healing(
                        R"(\b(rejuvenation|renew|lifebloom|regrowth|healing|spell|apply|refresh|tick|ticks|hots|on (the )?tank)\b)",
                        std::regex::icase);
                if (std::regex_search(lower, healing))
                    translated = glossary_context("heal_over_time");
                else if (std::regex_search(lower, recruitment))
                    translated = glossary_context("hall_of_thanes");
                else if (original == "hot")
                    continue;
                else
                    translated = glossary_context("ambiguous_hot");
            }
            out.push_back({a, e, original, translated});
            end = e;
            break;
        }
        if (out.size() == 12)
            break;
    }
    return out;
}
std::optional<std::string> Glossary::local(std::string_view s, const std::vector<Term> &found) const {
    if (found.empty() || s.size() > 540)
        return {};
    static const std::set<std::string> codes = {"lfm",  "lfg",  "lf1m",  "lf2m",   "lfm1", "lfm2", "wts",
                                                "wtb",  "dps",  "hps",   "tps",    "tank", "heal", "icc",
                                                "mc",   "bwl",  "zg",    "aq",     "naxx", "kara", "brd",
                                                "ubrs", "lbrs", "strat", "scholo", "sfk",  "wc",   "rfc"};
    auto alpha = [](std::string_view v) {
        for (auto c : utf32(v))
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128)
                return true;
        return false;
    };
    std::string out;
    size_t cursor = 0;
    for (auto &t : found) {
        auto term = lower_ascii(t.original);
        if (ambiguous_.contains(term) || (found.size() > 1 && !codes.contains(term)))
            return {};
        auto between = s.substr(cursor, t.start - cursor);
        if (alpha(between))
            return {};
        out += between;
        out += t.translation;
        cursor = t.end;
    }
    auto tail = s.substr(cursor);
    if (alpha(tail))
        return {};
    return out + std::string(tail);
}
Json default_settings() {
    const auto p = default_prompt_settings(ui_language());
    return {{"auto", true},
            {"ui_language", "system"},
            {"chat_logging", false},
            {"log_retention_days", 0},
            {"groups", {"party", "raid", "instance", "whisper", "bn", "outgoing", "guild", "say", "community", "public"}},
            {"channels", Json::array()},
            {"chat_tabs", Json::array({new_chat_tab("main", ui_text("General"))})},
            {"active_chat_tab", "main"},
            {"skip_target_language_nicknames", false},
            {"ocr", {{"enabled", false}, {"always", false}, {"languages", Json::array({"eng"})},
                     {"region", Json::array()}}},
            {"terms_enabled", true},
            {"show_original", false},
            {"show_time", false},
            {"class_colors", true},
            {"show_class", false},
            {"show_race", false},
            {"show_sex", false},
            {"clear_background", true},
            {"opacity", 0.80},
            {"background_opacity", 0.30}, // 70% background transparency; text stays opaque.
            {"bubble_transparency", 50},
            {"bubble_scale", 100},
            {"skin", "graphite"},
            {"ui_style", "ios"},
            {"style_memory", Json::object()},
            {"skin_colors",
             {{"background", "#1C1C1E"}, {"surface", "#2C2C2E"}, {"text", "#F5F5F7"}, {"accent", "#0A84FF"}}},
            {"font_size", 14},
            {"max_messages", 20},
            {"width", 460},
            {"height", 240},
            {"x", 60},
            {"y", 100},
            {"click_through", false},
            {"hotkey", {{"key", 192}, {"modifiers", 0}}},
            {"mac_keycode", 50},
            {"active_connection", "default"},
            {"api_presets_version", 0},
            {"connection_store", ""},
            {"model", "deepseek-chat"},
            {"base_url", "https://api.deepseek.com"},
            {"target", default_target_language().starts_with("en") ? "Chinese" : "English"},
            {"target_language", default_target_language()},
            {"incoming_prompt", p.at("incoming_prompt")},
            {"outbound_template", p.at("outbound_template")},
            {"outbound_default_template", p.at("outbound_default_template")},
            {"outbound_style", "short"},
            {"glossary_prefix", p.at("glossary_prefix")},
            {"outbound_terms_prefix", p.at("outbound_terms_prefix")}};
}
std::optional<std::string> local_outbound_translation(const Json &settings, std::string_view text,
                                                      std::string_view target, const Json &terms) {
    if (target != "English" || !settings.value("terms_enabled", true))
        return std::nullopt;
    auto value = utf32(trim(std::string(text)));
    while (!value.empty() && (value.back() == U'。' || value.back() == U'！' || value.back() == U'!'))
        value.pop_back();
    auto key = trim(utf8(value));
    if (terms.contains("exact") && terms["exact"].contains(key)) {
        auto result = terms["exact"][key].get<std::string>();
        return settings.value("outbound_style", "short") == "short" ? lower_ascii(result) : result;
    }
    return std::nullopt;
}
} // namespace bl
