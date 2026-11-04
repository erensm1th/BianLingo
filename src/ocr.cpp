#include "bianlingo/ocr.hpp"
#include "bianlingo/bundle.hpp"
#include "bianlingo/download.hpp"
#include <algorithm>
#include <regex>
namespace bl {
const std::vector<OcrModel> &ocr_models() {
    static const auto models = [] {
        std::vector<OcrModel> out;
        auto j = Json::parse(bundled_text("ocr-models.json"));
        for (auto &m : j)
            out.push_back({m.at("id"), m.at("name"), m.at("url"), m.at("sha256")});
        return out;
    }();
    return models;
}
void install_ocr_languages(const std::filesystem::path &directory, const Json &languages,
                           const std::function<bool()> &cancelled) {
    if (cancelled())
        throw std::runtime_error("OCR download cancelled");

    std::filesystem::create_directories(directory);
    for (auto &language : languages) {
        auto model = std::find_if(ocr_models().begin(), ocr_models().end(),
                                  [&](auto &m) { return m.id == language.get<std::string>(); });
        if (model == ocr_models().end())
            throw std::runtime_error("Unknown OCR language");
        auto file = directory / (model->id + ".traineddata");
        if (std::filesystem::exists(file) && sha256(read_bytes(file)) == model->hash)
            continue;
        auto temporary = directory / (model->id + ".download");
        bool verified = false;
        for (auto url : {std::string("https://ghfast.top/") + model->url, model->url}) {
            try {
                download::download(url, temporary, 32 * 1024 * 1024, [&](uint64_t) {
                    if (cancelled())
                        throw std::runtime_error("OCR download cancelled");
                });
                verified = sha256(read_bytes(temporary)) == model->hash;
                if (verified)
                    break;
            } catch (...) {
                if (cancelled())
                    throw;
            }
        }
        if (!verified)
            throw std::runtime_error("OCR language data failed verification");
        atomic_write(file, read_bytes(temporary));
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
}
void validate_ocr(const Json &s) {
    auto o = s.at("ocr");
    if (!o.is_object() || !o.at("enabled").is_boolean() || !o.at("languages").is_array() ||
        o["languages"].empty() || o["languages"].size() > 3)
        throw std::runtime_error("Invalid OCR settings; select at most three languages");
    if (o.contains("always") && !o["always"].is_boolean())
        throw std::runtime_error("Invalid OCR-only setting");
    for (auto &lang : o["languages"])
        if (!lang.is_string() || std::none_of(ocr_models().begin(), ocr_models().end(),
                                              [&](auto &m) { return m.id == lang.get<std::string>(); }))
            throw std::runtime_error("Unknown OCR language");
    auto region = o.at("region");
    if (!region.is_array() || (region.size() != 0 && region.size() != 7))
        throw std::runtime_error("Invalid OCR region");
    for (auto &n : region)
        if (!n.is_number_integer() || n.get<int64_t>() < 0 || n.get<int64_t>() > 32768)
            throw std::runtime_error("The OCR region is out of bounds");
    if (!region.empty() &&
        (region[2].get<int>() < 64 || region[3].get<int>() < 32 || region[4].get<int>() < 64 ||
         region[5].get<int>() < 32 || region[6].get<int>() < 48 ||
         region[0].get<int>() + region[2].get<int>() > region[4].get<int>() ||
         region[1].get<int>() + region[3].get<int>() > region[5].get<int>() ||
         int64_t(region[2].get<int>()) * region[3].get<int>() > 2'000'000))
        throw std::runtime_error("The OCR region must fit within the client area and contain at most two million pixels");
}
bool ocr_pure_mode(const Json &settings) {
    const auto &ocr = settings.at("ocr");
    return ocr.value("enabled", false) && ocr.value("always", false);
}
std::string ocr_key(std::string_view text) {
    // Never strip punctuation or non-Latin characters: similar names/messages
    // must not become the same identity merely because OCR is approximate.
    auto s = clean_chat(text);
    std::string out;
    bool space = false;
    for (unsigned char c : s) {
        if (c <= 32) {
            space = !out.empty();
            continue;
        }
        if (space)
            out += ' ';
        space = false;
        out += c;
    }
    return out;
}
std::string ocr_body(std::string_view text) {
    auto close = text.find(']');
    if (text.starts_with("[") && close != text.npos) {
        auto stamp = text.substr(1, close - 1);
        if (stamp.find(':') != stamp.npos && std::all_of(stamp.begin(), stamp.end(), [](char c) {
                return (c >= '0' && c <= '9') || c == ':' || c == ' ';
            }))
            text.remove_prefix(close + 1);
    }
    auto at = text.find(':');
    auto chinese = text.find("：");
    if (chinese != text.npos && (at == text.npos || chinese < at))
        return trim(std::string(text.substr(chinese + 3)));
    return at == text.npos ? trim(std::string(text)) : trim(std::string(text.substr(at + 1)));
}
std::vector<std::string> ocr_messages(const std::vector<OcrLine> &input) {
    auto lines = input;
    std::stable_sort(lines.begin(), lines.end(), [](auto &a, auto &b) { return a.y < b.y; });
    // Native English/Chinese chat prefixes, timestamps and [Name]: variants.
    // Require a header to avoid translating random panel/input text.
    static const std::regex header(
        R"(^\s*(\[[^\]]{1,100}\]\s*){0,3}([^:]{1,100}(says|yells|whispers|说|喊|悄悄地说)\s*[:：]|\[[^\]]{1,100}\]\s*[:：]))");
    // Classic channel names can be garbled by OCR while the bare speaker and
    // colon remain legible. Require a preceding bracketed channel prefix.
    static const std::regex channel_speaker(
        R"(^\s*(\[[^\]]{1,100}\]\s*){1,3}[A-Za-z][A-Za-z0-9'._@-]{1,63}\s*[:：])");
    static const std::regex noisy_channel_speaker(
        R"(^.{0,40}\[[A-Za-z][A-Za-z0-9'._@-]{1,63}\]\s*[:：])");
    std::vector<std::string> out;
    int bottom = 0, height = 0;
    for (auto &line : lines) {
        auto text = trim(line.text);
        if (line.confidence < 65 || text.empty() || text.size() > 2048) {
            bottom = -10000;
            continue;
        }
        if (std::regex_search(text, header) || std::regex_search(text, channel_speaker) ||
            std::regex_search(text, noisy_channel_speaker)) {
            out.push_back(text);
        } else if (!out.empty() && line.y - bottom >= -2 && line.y - bottom <= std::max(8, height)) {
            out.back() += " " + text;
        } else {
            bottom = -10000;
            continue;
        }
        bottom = line.y + line.h;
        height = line.h;
        if (out.back().size() > 4096) {
            out.pop_back();
            bottom = -10000;
        }
    }
    return out;
}
void OcrTracker::reset() {
    previous_.clear();
    candidate_.clear();
    initial_ = true;
}
std::vector<std::string> OcrTracker::observe(const std::vector<std::string> &raw) {
    std::vector<std::string> now;
    for (auto &x : raw)
        now.push_back(ocr_key(x));
    if (now != candidate_) {
        candidate_ = std::move(now);
        return {};
    }
    if (now.empty())
        return {}; // Do not forget the previous visible sequence on fade/occlusion.
    size_t overlap = 0;
    for (size_t n = std::min(previous_.size(), now.size()); n > 0; --n)
        if (std::equal(previous_.end() - n, previous_.end(), now.begin())) {
            overlap = n;
            break;
        }
    std::vector<std::string> added;
    if (initial_)
        overlap = now.size() > 3 ? now.size() - 3 : 0;
    // With no overlap after the first frame, require a fresh baseline: a tab
    // switch/scroll must not be mistaken for a flood of new messages.
    if (initial_ || overlap)
        for (size_t i = overlap; i < raw.size(); ++i)
            added.push_back(raw[i]);
    previous_ = std::move(now);
    initial_ = false;
    return added;
}
#ifndef _WIN32
namespace platform {
std::unique_ptr<OcrCapture> make_ocr_capture() {
    throw std::runtime_error("OCR fallback capture is not available on this platform");
}
std::unique_ptr<OcrRecognizer> make_ocr_recognizer(const std::filesystem::path &, const std::string &) {
    throw std::runtime_error("OCR recognition is not available on this platform");
}
} // namespace platform
#endif
} // namespace bl
