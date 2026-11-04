#pragma once
#include <string>
#include <string_view>
#include <array>

namespace bl {
struct UiLanguage { std::string_view code, native_name; };
inline constexpr std::array<UiLanguage, 9> ui_languages{{
    {"en", "English"}, {"zh", "简体中文"}, {"ja", "日本語"}, {"ko", "한국어"},
    {"fr", "Français"}, {"de", "Deutsch"}, {"es", "Español"},
    {"pt-BR", "Português (Brasil)"}, {"ru", "Русский"}}};
// UI locales are independent of translation model availability.
std::string normalized_ui_language(std::string_view tag);
bool supported_ui_language(std::string_view choice);
std::string resolve_ui_language(std::string_view choice);
void set_ui_language_preference(std::string_view choice);
std::string ui_language();
bool has_ui_translation(std::string_view source);
std::string ui_text(std::string_view source);
std::string notification_text(std::string_view source);
std::string notification_text_for_language(std::string_view source, std::string_view language);
std::string ui_text_for_language(std::string_view source, std::string_view language);
bool contains_han(std::string_view text);
bool should_translate_for_ui(std::string_view text);
std::string default_target_language();
std::string target_language_name(std::string_view code);
bool supported_target_language(std::string_view code);
// LLMs can identify an unknown Latin source; dedicated workers require a known model direction.
bool should_translate_for_target(std::string_view text, std::string_view code, bool accept_unknown_latin = false);
std::string local_translation_pair_for_language(std::string_view language, bool incoming,
                                                std::string_view text, std::string_view target);
// Returns a pinned direct model pair or throws when the direction is unavailable.
std::string local_translation_pair(bool incoming, std::string_view text, std::string_view target,
                                   std::string_view chat_language = {});
} // namespace bl
