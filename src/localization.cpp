#include "bianlingo/localization.hpp"
#include "bianlingo/core.hpp"
#include "bianlingo/bundle.hpp"
#include <stdexcept>
#include <array>
#include <atomic>
#include <algorithm>
#include <unordered_map>

namespace bl {
namespace {
std::atomic<int> selected_ui_language{0};
const Json &ui_dictionary() {
    static const Json value = bundled_json("ui-locales.json");
    return value;
}
}
std::string normalized_ui_language(std::string_view tag) {
    auto value = lower_ascii(std::string(tag));
    if (value.starts_with("zh")) return "zh";
    if (value == "pt" || value.starts_with("pt-") || value.starts_with("pt_")) return "pt-BR";
    for (auto code : {"en", "fr", "de", "es", "ja", "ko", "ru"})
        if (value == code || value.starts_with(std::string(code) + "-") ||
            value.starts_with(std::string(code) + "_")) return code;
    return "en";
}
bool supported_ui_language(std::string_view choice) {
    if (choice == "system") return true;
    for (const auto &language : ui_languages) if (language.code == choice) return true;
    return false;
}
std::string resolve_ui_language(std::string_view choice) {
    if (!supported_ui_language(choice)) throw std::runtime_error("Unsupported interface language");
    return choice == "system" ? normalized_ui_language(platform::user_locale()) : std::string(choice);
}
void set_ui_language_preference(std::string_view choice) {
    if (!supported_ui_language(choice)) throw std::runtime_error("Unsupported interface language");
    int index = 0;
    if (choice != "system")
        for (size_t i = 0; i < ui_languages.size(); ++i)
            if (ui_languages[i].code == choice) index = int(i + 1);
    selected_ui_language.store(index, std::memory_order_relaxed);
}
std::string ui_language() {
    const auto index = selected_ui_language.load(std::memory_order_relaxed);
    return index ? std::string(ui_languages[size_t(index - 1)].code)
                 : normalized_ui_language(platform::user_locale());
}
namespace {
const Json &ui_aliases() {
    static const Json value = bundled_json("ui-source-aliases.json");
    return value;
}
std::string source_key(std::string_view source) {
    const auto found = ui_aliases().find(std::string(source));
    return found == ui_aliases().end() ? std::string(source) : found->get<std::string>();
}
struct NotificationPattern {
    std::vector<std::string> literals;
    std::vector<unsigned> slots;
    explicit NotificationPattern(const std::string &value) {
        size_t offset = 0;
        while (offset < value.size()) {
            auto next = value.find('{', offset);
            if (next == value.npos || next + 2 >= value.size() || value[next + 2] != '}' ||
                value[next + 1] < '0' || value[next + 1] > '9') break;
            literals.push_back(value.substr(offset, next - offset));
            slots.push_back(unsigned(value[next + 1] - '0'));
            offset = next + 3;
        }
        literals.push_back(value.substr(offset));
    }
    bool match(std::string_view value, std::array<std::string_view, 10> &arguments) const {
        if (!value.starts_with(literals.front())) return false;
        size_t offset = literals.front().size();
        for (size_t i = 0; i < slots.size(); ++i) {
            const auto &next = literals[i + 1];
            const auto end = next.empty() ? value.size() : value.find(next, offset);
            if (end == value.npos || end == offset) return false;
            const auto argument = value.substr(offset, end - offset);
            if (!std::all_of(argument.begin(), argument.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
            arguments[slots[i]] = argument; offset = end + next.size();
        }
        return offset == value.size();
    }
    std::string render(const std::array<std::string_view, 10> &arguments) const {
        std::string result = literals.front();
        for (size_t i = 0; i < slots.size(); ++i) { result += arguments[slots[i]]; result += literals[i + 1]; }
        return result;
    }
};
struct NotificationEntry { std::string english, chinese; };
struct NotificationIndex {
    std::unordered_map<std::string, NotificationEntry> exact;
    std::array<std::vector<std::pair<std::string, NotificationEntry>>, 256> prefixes;
    std::vector<std::pair<NotificationPattern, NotificationPattern>> formats;
    NotificationIndex() {
        const auto catalog = bundled_json("message-locales.json");
        for (auto entry = catalog.begin(); entry != catalog.end(); ++entry) {
            NotificationEntry value{entry.key(), entry.value().at("zh")};
            if (entry.value().value("format", false)) formats.emplace_back(NotificationPattern(value.english), NotificationPattern(value.chinese));
            auto add = [&](const std::string &key) {
                exact.emplace(key, value);
                if (key.size() >= 8 && (key.ends_with(" ") || key.ends_with(":") || key.ends_with("：") || key.ends_with("=")))
                    prefixes[static_cast<unsigned char>(key.front())].emplace_back(key, value);
            };
            add(entry.key()); add(value.chinese);
            for (const auto &alias : entry.value().value("aliases", Json::array())) add(alias.get<std::string>());
        }
        for (auto &bucket : prefixes)
            std::sort(bucket.begin(), bucket.end(), [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
    }
};
const NotificationIndex &notification_index() { static const NotificationIndex value; return value; }
std::optional<std::string> translated_notification(std::string_view source, std::string_view language) {
    const auto &index = notification_index();
    const bool chinese = normalized_ui_language(language) == "zh";
    if (auto found = index.exact.find(std::string(source)); found != index.exact.end())
        return chinese ? found->second.chinese : found->second.english;
    if (source.size() <= 4096)
        for (const auto &[english, localized] : index.formats) {
            std::array<std::string_view, 10> arguments{};
            if (english.match(source, arguments) || localized.match(source, arguments))
                return (chinese ? localized : english).render(arguments);
        }
    if (!source.empty())
        for (const auto &[prefix, value] : index.prefixes[static_cast<unsigned char>(source.front())])
            if (source.starts_with(prefix))
                return (chinese ? value.chinese : value.english) + std::string(source.substr(prefix.size()));
    return std::nullopt;
}
}
bool has_ui_translation(std::string_view source) {
    return ui_dictionary().contains(source_key(source)) || notification_index().exact.contains(std::string(source));
}
std::string notification_text_for_language(std::string_view source, std::string_view language) {
    std::string result;
    size_t offset = 0;
    do {
        const auto end = source.find('\n', offset);
        const auto line = source.substr(offset, end == source.npos ? source.size() - offset : end - offset);
        if (auto translated = translated_notification(line, language)) result += *translated;
        else {
            const auto key = source_key(line);
            const auto entry = ui_dictionary().find(key);
            if (entry == ui_dictionary().end()) result += line;
            else result += entry->value(normalized_ui_language(language) == "zh" ? "zh" : "en", key);
        }
        if (end == source.npos) break;
        result += '\n'; offset = end + 1;
    } while (offset <= source.size());
    return result;
}
std::string notification_text(std::string_view source) { return notification_text_for_language(source, ui_language()); }
std::string ui_text_for_language(std::string_view source, std::string_view language) {
    if (auto found = notification_index().exact.find(std::string(source)); found != notification_index().exact.end())
        return normalized_ui_language(language) == "zh" ? found->second.chinese : found->second.english;
    const auto key = source_key(source);
    const auto &dictionary = ui_dictionary();
    auto entry = dictionary.find(key);
    if (entry == dictionary.end() || !entry->is_object()) {
        if (auto message = translated_notification(source, language)) return *message;
        return key;
    }
    auto translated = entry->find(language);
    if (translated != entry->end() && translated->is_string() && !translated->get_ref<const std::string &>().empty())
        return translated->get<std::string>();
    auto english = entry->find("en");
    return english != entry->end() && english->is_string() ? english->get<std::string>() : key;
}
std::string ui_text(std::string_view source) { return ui_text_for_language(source, ui_language()); }
bool contains_han(std::string_view text) {
    for (auto c : utf32(prose_text(text)))
        if (c >= 0x3400 && c <= 0x9fff) return true;
    return false;
}
bool should_translate_for_ui(std::string_view text) {
    if (ui_language() == "en") return contains_han(text);
    return should_translate(text);
}
namespace {
constexpr std::array<std::pair<std::string_view, std::string_view>, 26> targets{{
    {"en-US", "English (US)"}, {"en-GB", "English (UK)"},
    {"es", "Spanish"}, {"pt-BR", "Portuguese (Brazil)"}, {"pt-PT", "Portuguese (Portugal)"},
    {"fr", "French"}, {"de", "German"}, {"it", "Italian"}, {"ru", "Russian"},
    {"uk", "Ukrainian"}, {"pl", "Polish"}, {"nl", "Dutch"}, {"sv", "Swedish"},
    {"da", "Danish"}, {"nb", "Norwegian"}, {"fi", "Finnish"}, {"cs", "Czech"},
    {"tr", "Turkish"}, {"el", "Greek"}, {"hu", "Hungarian"}, {"ro", "Romanian"},
    {"zh-Hans", "Chinese (Simplified)"}, {"zh-Hant", "Chinese (Traditional)"},
    {"ja", "Japanese"}, {"ko", "Korean"}, {"ar", "Arabic"}}};
}
bool supported_target_language(std::string_view code) {
    for (auto [key, name] : targets) if (key == code) return true;
    return false;
}
std::string target_language_name(std::string_view code) {
    for (auto [key, name] : targets) if (key == code) return std::string(name);
    throw std::runtime_error("Unsupported chat translation target language");
}
std::string default_target_language() {
    auto locale = lower_ascii(platform::user_locale());
    if (locale.starts_with("zh")) return locale.find("tw") != std::string::npos ||
        locale.find("hk") != std::string::npos || locale.find("mo") != std::string::npos
            ? "zh-Hant" : "zh-Hans";
    if (locale.starts_with("pt")) return locale.starts_with("pt-pt") ? "pt-PT" : "pt-BR";
    if (locale.starts_with("en")) return locale.starts_with("en-gb") ? "en-GB" : "en-US";
    auto base = locale.substr(0, locale.find_first_of("-_"));
    if (base == "no" || base == "nn") base = "nb";
    return supported_target_language(base) ? base : "en-US";
}
bool should_translate_for_target(std::string_view text, std::string_view code, bool accept_unknown_latin) {
    if (code == "zh-Hans" || code == "zh-Hant") return should_translate(text);
    size_t ascii_letters = 0, other_letters = 0;
    for (auto c : utf32(prose_text(text))) {
        if (!platform::is_letter(c)) continue;
        if (c < 128) ++ascii_letters;
        else ++other_letters;
    }
    if ((code == "en-US" || code == "en-GB") && !accept_unknown_latin) return other_letters > 0;
    return other_letters > 0 || ascii_letters > 1;
}
std::string local_translation_pair_for_language(std::string_view language, bool incoming,
                                                std::string_view text, std::string_view target) {
    if (incoming) {
        if (language == "en-US" || language == "en-GB" || language == "en") {
            if (contains_han(text)) return "zh-Hans-en";
            throw std::runtime_error("No local model for this source language into English");
        }
        return "en-" + (language == "zh" ? std::string("zh-Hans") : std::string(language));
    }
    std::string destination;
    if (target == "English") destination = "en";
    else if (target == "Chinese") destination = "zh-Hans";
    else if (target == "French") destination = "fr";
    else if (target == "German") destination = "de";
    else if (target == "Spanish") destination = "es";
    else if (target == "Japanese") destination = "ja";
    else if (target == "Korean") destination = "ko";
    else if (target == "Russian") destination = "ru";
    else if (target == "Brazilian Portuguese") destination = "pt";
    else if (target == "Italian") destination = "it";
    if (destination.empty()) throw std::runtime_error("No local model for the selected output language");
    const auto source = language == "zh" ? std::string("zh-Hans") :
                        language == "zh-Hans" || language == "zh-Hant" ? std::string(language) :
                        language == "en-US" || language == "en-GB" ? std::string("en") :
                        std::string(language.substr(0, language.find('-')));
    if (source == destination) throw std::runtime_error("Source and target languages are the same");
    return source + "-" + destination;
}
std::string local_translation_pair(bool incoming, std::string_view text, std::string_view target,
                                   std::string_view chat_language) {
    const auto source = chat_language.empty() ? ui_language() : std::string(chat_language);
    return local_translation_pair_for_language(source, incoming, text, target);
}
} // namespace bl
