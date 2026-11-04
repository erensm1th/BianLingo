#include "bianlingo/bergamot.hpp"
#include "bianlingo/localization.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    try {
        const auto language = bl::ui_language();
        std::string incoming_text = "Hello, friend.";
        std::string outbound_text = "你好，朋友。";
        std::string outbound_target = "English";
        if (language == "en") {
            incoming_text = "你好，朋友。";
            outbound_text = "Hello, friend.";
            outbound_target = "Chinese";
        } else if (language == "fr") outbound_text = "Bonjour, mon ami.";
        else if (language == "de") outbound_text = "Hallo, mein Freund.";
        else if (language == "es") outbound_text = "Hola, amigo.";
        const auto incoming_result = bl::bergamot_translate(incoming_text, true, "");
        if (incoming_result.empty() || incoming_result == incoming_text)
            throw std::runtime_error("Incoming local translation returned no result");
        const auto outbound_result = bl::bergamot_translate(outbound_text, false, outbound_target);
        if (outbound_result.empty() || outbound_result == outbound_text)
            throw std::runtime_error("Outbound local translation returned no result");
        if (language != "en") {
            const auto protected_link = bl::bergamot_translate("Meet at [[BLG_LINK_1]].", true, "");
            if (protected_link.find("[[BLG_LINK_1]]") == std::string::npos)
                throw std::runtime_error("Chat link placeholder was lost");
        }
        std::cout << "Bergamot app-to-helper translation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
