#pragma once
#include "core.hpp"
namespace bl {
struct OcrLine {
    std::string text;
    int x = 0, y = 0, w = 0, h = 0;
    float confidence = 0;
};
struct OcrModel {
    std::string id, name, url, hash;
};
const std::vector<OcrModel> &ocr_models();
void validate_ocr(const Json &);
bool ocr_pure_mode(const Json &);
void install_ocr_languages(const std::filesystem::path &, const Json &,
                           const std::function<bool()> &cancelled);
std::vector<std::string> ocr_messages(const std::vector<OcrLine> &);
std::string ocr_key(std::string_view);
std::string ocr_body(std::string_view);
// Sequence overlap preserves repeated utterances occurring later in the stream.
class OcrTracker {
    std::vector<std::string> previous_, candidate_;
    bool initial_ = true;

  public:
    std::vector<std::string> observe(const std::vector<std::string> &);
    void reset();
};
namespace platform {
class OcrCapture {
  public:
    virtual ~OcrCapture() = default;
    virtual std::optional<Image> grab(uintptr_t window, Rect client_region, int width, int height,
                                      int dpi) = 0;
};
std::unique_ptr<OcrCapture> make_ocr_capture();
class OcrRecognizer {
  public:
    virtual ~OcrRecognizer() = default;
    virtual std::vector<OcrLine> recognize(const Image &) = 0;
};
std::unique_ptr<OcrRecognizer> make_ocr_recognizer(const std::filesystem::path &, const std::string &);
} // namespace platform
} // namespace bl
