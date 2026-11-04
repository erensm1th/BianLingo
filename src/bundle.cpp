#include "bianlingo/bundle.hpp"
#include "bianlingo/localization.hpp"

namespace bl {
Json bundled_json(std::string_view name) {
    return Json::parse(bundled_text(name));
}
std::string bundled_notices() {
    const auto usage = bundled_json("usage-notice.json");
    std::string result = usage.at(ui_language() == "zh" ? "zh" : "en").get<std::string>();
    for (auto name : {"project-notice.txt", "project-license.txt", "source-access.txt", "privacy.txt", "third-party.txt", "licenses/OpenSSL.txt", "licenses/nlohmann-json.txt", "licenses/Terminology.txt",
                      "licenses/Tesseract.txt", "licenses/Leptonica.txt", "licenses/OCRModels.txt",
                      "licenses/OCRDependencies.txt", "licenses/CTranslate2.txt", "licenses/SentencePiece.txt",
                      "licenses/CC-BY-4.0.txt", "licenses/CT2Runtime.txt", "licenses/CT2Native.txt", "licenses/Ollama.txt", "licenses/OllamaRuntime.txt"}) {
        result += "\n================ " + std::string(name) + " ================\n\n";
        result += bundled_text(name);
        result += '\n';
    }
    return result;
}
} // namespace bl
