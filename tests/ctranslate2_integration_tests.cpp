#include "bianlingo/ctranslate2.hpp"
#include "bianlingo/core.hpp"
#include "bianlingo/localization.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    try {
        const auto french = bl::ctranslate2_translate("Hello, my friend.", true, "", "fr");
        if (bl::lower_ascii(french).find("bonjour") == std::string::npos)
            throw std::runtime_error("Explicit French target did not use the French model");
        const auto pivot = bl::ctranslate2_translate("你好，朋友。", false, "French", "zh-Hans");
        if (bl::lower_ascii(pivot).find("bonjour") == std::string::npos)
            throw std::runtime_error("Known Chinese-to-French pivot failed");
        const auto incoming = bl::ctranslate2_translate("Hello, friend.", true, "", "zh-Hans");
        if (incoming.empty() || incoming == "Hello, friend.") throw std::runtime_error("Chinese model was not selected");
        const auto outgoing = bl::ctranslate2_translate("你好，朋友。", false, "English", "zh-Hans");
        if (bl::lower_ascii(outgoing).find("friend") == std::string::npos) throw std::runtime_error("Reverse Chinese model failed");
        const auto linked = bl::ctranslate2_translate("Meet at [[BLG_LINK_1]].", true, "", "fr");
        if (linked.find(" [[BLG_LINK_1]]") == std::string::npos) throw std::runtime_error("Protected link or spacing was lost");
        bool unsupported = false;
        try { bl::ctranslate2_translate("Hello, friend.", true, "", "zh-Hant"); }
        catch (const std::runtime_error &) { unsupported = true; }
        if (!unsupported) throw std::runtime_error("Unsupported direction silently reused another model");
        std::cout << "CTranslate2 app-to-helper translation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
