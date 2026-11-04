#include "bianlingo/prompts.hpp"
#include "bianlingo/bundle.hpp"
#include "bianlingo/localization.hpp"
#include "bianlingo/provider.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace bl;
namespace {
void require(bool value, const char *description) {
    if (!value) throw std::runtime_error(description);
}
void locale(const char *value) {
#ifdef _WIN32
    _putenv_s("BIANLINGO_UI_LANGUAGE", value);
#else
    setenv("BIANLINGO_UI_LANGUAGE", value, 1);
#endif
}
size_t occurrences(std::string_view text, std::string_view token) {
    size_t result = 0, pos = 0;
    while ((pos = text.find(token, pos)) != text.npos) { ++result; pos += token.size(); }
    return result;
}
void verify_request(std::string_view format, const std::string &prompt, const std::string &text) {
    auto p = format == "ollama_local" ? new_local_ollama_connection("test") : new_connection("test");
    p["format"] = format;
    p["base_url"] = format == "ollama_local" ? "http://127.0.0.1:11435/api/chat" : "https://api.example.invalid/v1";
    p["key"] = format == "ollama_local" ? "" : "synthetic-key";
    const auto request = provider_request(p, prompt, text);
    if (format == "openai_chat" || format == "ollama_local") {
        require(request.body.at("messages").size() == 2 &&
                    request.body["messages"][0]["content"] == prompt &&
                    request.body["messages"][1]["content"] == text,
                "Chat/Ollama did not receive the complete multilingual prompt and separate user data");
    } else if (format == "openai_responses") {
        require(request.body.at("instructions") == prompt && request.body.at("input") == text,
                "Responses prompt/input changed");
    } else if (format == "anthropic") {
        require(request.body.at("system") == prompt && request.body["messages"][0]["content"] == text,
                "Anthropic prompt/input changed");
    } else {
        require(request.body["systemInstruction"]["parts"][0]["text"] == prompt &&
                    request.body["contents"][0]["parts"][0]["text"] == text, "Gemini prompt/input changed");
    }
}
} // namespace
int main() {
    try {
        const auto terms = bundled_json("outbound_terms.json");
        const auto presets = read_json(TEST_PROMPTS_RESOURCE);
        Glossary glossary(bundled_json("glossary_zhCN.json"));
        const auto input = protect_chat_links("LFM |cffa335ee|Hitem:123|h[épée 你好]|h|r, Alice? dont pull");
        const auto matches = glossary.matches("LFM tank");
        const char *targets[] = {"en-US", "en-GB", "es", "pt-BR", "pt-PT", "fr", "de", "it", "ru", "uk", "pl", "nl", "sv", "da", "nb", "fi", "cs", "tr", "el", "hu", "ro", "zh-Hans", "zh-Hant", "ja", "ko", "ar"};
        for (auto lang : {"zh", "en", "fr", "de", "es", "ja", "ko", "pt-BR", "ru"}) {
            locale(lang);
            auto settings = default_settings();
            require(settings["outbound_template"] != settings["outbound_default_template"],
                    "Short and standard style collapsed in a UI locale");
            require(settings["incoming_prompt"].get<std::string>().find("{language}") != std::string::npos,
                    "Incoming built-in prompt hardcodes a target language");
            for (auto target : targets) {
                settings["target_language"] = target;
                auto prompt = translation_prompt(settings, true, "short", "", input, matches, terms);
                const auto selected = "Selected output language: " + target_language_name(target) + ".";
                require(prompt.find(selected) != prompt.npos && prompt.find("{language}") == prompt.npos,
                        "UI locale leaked into the selected chat target");
                require(prompt.find("[[BLG_LINK_1]]") != prompt.npos && prompt.find("Alice") == prompt.npos,
                        "Link guidance missing or whole user input leaked into the system prompt");
                for (auto format : {"openai_chat", "openai_responses", "anthropic", "gemini", "ollama_local"})
                    verify_request(format, prompt, input.text);
                if (std::string_view(target) != "zh-Hans")
                    require(!local_incoming_translation(settings, "tank", glossary.matches("tank"), glossary),
                            "Simplified Chinese dictionary bypassed a different selected target");
            }
            settings["target_language"] = "zh-Hans";
            require(local_incoming_translation(settings, "tank", glossary.matches("tank"), glossary) == "坦克",
                    "Chinese dictionary fast path was lost");
            require(!local_incoming_translation(settings, "|Hitem:1|h[tank]|h", matches, glossary),
                    "Dictionary fast path accepted a protected game link");
            for (auto style : {"short", "default"}) {
                auto prompt = translation_prompt(settings, false, style, "German", input, matches, terms);
                require(prompt.find("Selected output language: German.") != prompt.npos &&
                            prompt.find("{language}") == prompt.npos && prompt.find("[[BLG_LINK_1]]") != prompt.npos,
                        "Outgoing style/target/link contract missing");
                require(input.restore(input.text).find("|Hitem:123|h[épée 你好]|h") != std::string::npos,
                        "Outgoing link restoration changed the original game link");
            }
            auto output = protect_chat_links("缺治疗，十分钟后出发");
            auto french = translation_prompt(settings, false, "short", "French", output, {}, terms);
            require(french.find("healer") != french.npos && french.find("Selected output language: French.") != french.npos,
                    "Non-English outgoing translation lost game terminology references");
            for (auto &[field, values] : presets.at("RECOGNIZED_DEFAULTS").items()) {
                for (const auto &value : values) {
                    auto stored = settings;
                    stored[field] = value;
                    normalize_locale_prompts(stored);
                    require(stored[field] == default_prompt_settings(lang).at(field),
                            "Recognized built-in prompt was not updated for this UI locale");
                    require(!normalize_locale_prompts(stored), "Prompt normalization is not idempotent");
                }
            }
            auto custom = settings;
            for (auto key : {"incoming_prompt", "outbound_template", "outbound_default_template", "glossary_prefix", "outbound_terms_prefix"})
                custom[key] = "my exact custom {language} instructions: «éß中文»";
            auto before = custom;
            require(!normalize_locale_prompts(custom) && custom == before, "Custom prompt content was overwritten");
            custom["incoming_prompt"] = "custom prompt without a language placeholder";
            auto rendered = translation_prompt(custom, true, "short", "", input, {}, terms);
            require(rendered.starts_with("custom prompt without a language placeholder"),
                    "Existing custom incoming prompt no longer works without placeholder");
            settings["terms_enabled"] = false;
            require(!local_incoming_translation(settings, "tank", matches, glossary), "Disabled terminology still returned dictionary text");
            auto disabled = translation_prompt(settings, true, "short", "", input, matches, terms);
            require(disabled.find("References describe meanings") == disabled.npos && disabled.find("LFG seeks") == disabled.npos,
                    "Disabled terminology still appended dictionary instructions");
            require(local_outbound_translation(before, "缺治疗", "English", terms) == "lf healer",
                    "Exact output phrase no longer returns locally");
        }
        require(provider_uses_prompts("ollama_local") && provider_uses_prompts("openai_chat") &&
                    !provider_uses_prompts("bergamot_local") && !provider_uses_prompts("ctranslate2_local"),
                "Local dedicated translation engines are advertised as using prompts");
        require(should_translate_for_target("你好，朋友", "fr") && should_translate_for_target("你好", "de") &&
                    !should_translate_for_target("123456", "en-US") && !should_translate_for_target("😀", "en-US") &&
                    !should_translate_for_target("x-123", "fr") && !should_translate_for_target("来个 DPS", "zh-Hans") &&
                    should_translate_for_target("bonjour mes amis", "en-US", true) &&
                    should_translate_for_target("hallo freunde", "en-GB", true) &&
                    !should_translate_for_target("x-123", "en-US", true),
                "Target-language filter uses Chinese rules for another target or submits noise");
        std::vector<Term> many;
        for (int i = 0; i < 30; ++i) many.push_back({0, 0, "term" + std::to_string(i), std::string(300, 'x')});
        auto settings = default_settings(); settings["target_language"] = "zh-Hans";
        const auto bounded = translation_prompt(settings, true, "short", "", {}, many, Json::object());
        require(occurrences(bounded, " = ") <= 12 && bounded.size() < 6500,
                "Dictionary references cause unbounded prompt growth");
        std::cout << "PASS prompts: 9 UI locales, 26 targets, 5 LLM formats, styles, upgrades, links and dictionary routing\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
