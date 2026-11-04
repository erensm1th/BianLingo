#include "bianlingo/bundle.hpp"
#include "bianlingo/localization.hpp"
#include <iostream>
#include <stdexcept>

using namespace bl;
namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
}
int main() {
    try {
        const auto messages = bundled_json("message-locales.json");
        for (auto entry = messages.begin(); entry != messages.end(); ++entry) {
            require(!contains_han(entry.key()), "The default notification contains Han characters");
            const auto chinese = entry.value().at("zh").get<std::string>();
            require(!chinese.empty(), "A notification has no Chinese translation");
            require(notification_text_for_language(entry.key(), "zh") == chinese,
                    "Chinese notification translation is missing");
            for (const auto &language : ui_languages)
                if (language.code != "zh")
                    require(notification_text_for_language(entry.key(), language.code) == entry.key(),
                            "A non-Chinese UI received a non-English notification");
        }
        const std::string mac = "Could not identify this Mac";
        require(notification_text_for_language(mac, "ja") == mac &&
                    notification_text_for_language(mac, "zh") != mac,
                "Platform exceptions do not follow the two-language policy");
        const std::string http = "The local Ollama request failed (HTTP 503); check the model name and Ollama service";
        const auto localized_http = notification_text_for_language(http, "zh");
        require(localized_http != http && localized_http.find("503") != std::string::npos &&
                    notification_text_for_language(localized_http, "ko") == http,
                "A structured error lost its status code or cannot switch back to English");
        const std::string replay = "Replay batch 7: new 2, already received 4, incomplete 1";
        const auto chinese_replay = notification_text_for_language(replay, "zh");
        require(chinese_replay != replay && notification_text_for_language(chinese_replay, "fr") == replay,
                "Replay notification counts changed across languages");
        const std::string path = "E:/models/日本語/模型.bin";
        const std::string prefix = "Runtime replacement and rollback failed; the previous runtime is retained at ";
        const auto localized_path = notification_text_for_language(prefix + path, "zh");
        require(localized_path.ends_with(path) && notification_text_for_language(localized_path, "de") == prefix + path,
                "Notification localization changed an opaque Unicode path");
        require(ui_text_for_language("Save", "ja") != "Save" &&
                    ui_text_for_language(mac, "ja") == mac,
                "Interface localization was replaced by notification localization");
        require(notification_text_for_language("HTTP_ERROR_403", "zh") == "HTTP_ERROR_403",
                "An unknown machine error code was altered");
        std::cout << "PASS English-default notifications, Chinese display, all non-Chinese locales, codes, counts and opaque paths\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
