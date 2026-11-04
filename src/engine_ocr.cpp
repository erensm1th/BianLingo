#include "bianlingo/ocr.hpp"
#include <algorithm>
#include <ctime>
namespace bl {
void Engine::test_ocr() {
    {
        std::lock_guard lock(ocr_mutex_);
        ocr_status_ = "Waiting for the game window to preview local OCR…";
        ocr_preview_.clear();
    }
    ocr_test_ = true;
}
void Engine::install_ocr_models() {
    ocr_install_ = true;
}
std::string Engine::ocr_status() const {
    std::lock_guard lock(ocr_mutex_);
    return ocr_status_;
}
std::string Engine::ocr_preview() const {
    std::lock_guard lock(ocr_mutex_);
    return ocr_preview_;
}
void Engine::ocr_loop(StopToken stop) {
    auto status = [&](std::string value) {
        std::lock_guard lock(ocr_mutex_);
        ocr_status_ = std::move(value);
    };
    std::unique_ptr<platform::OcrCapture> capture;
    std::unique_ptr<platform::OcrRecognizer> recognizer;
    OcrTracker tracker;
    Json loaded, previous_config;
    Clock::time_point next{}, last_recognize{}, test_deadline{};
    std::vector<uint8_t> last_pixels;
    uintptr_t owner = 0;
    bool active = false, confirm = false, testing = false;
    uint32_t session = 0x80000000u | (uint32_t(std::time(nullptr)) & 0x7fffffff), seq = 0;
    while (!stop.stop_requested()) {
        auto cfg = settings();
        auto o = cfg.at("ocr");
        bool pure = ocr_pure_mode(cfg);
        auto state = chat_state_.load();
        auto window = game_window_.load();
        if (ocr_test_.exchange(false)) {
            testing = true;
            test_deadline = Clock::now() + std::chrono::seconds(8);
        }
        if (testing && Clock::now() > test_deadline) {
            testing = false;
            status("OCR preview timed out; check the game window and selected region");
        }
        bool wanted = o.value("enabled", false) && cfg.value("auto", true) && (pure || state == 2);
        bool changed = o != previous_config || window != owner;
        if (changed || wanted != active) {
            capture.reset();
            tracker.reset();
            last_pixels.clear();
            confirm = false;
            owner = window;
            previous_config = o;
            active = wanted;
            if (!wanted && !testing)
                status(o.value("enabled", false)
                           ? (pure ? "OCR-only mode is idle; enable automatic translation"
                                   : state == 1 ? "OCR fallback is idle; chat is readable" : "OCR fallback is idle; waiting for the addon restriction status")
                           : "OCR fallback is disabled");
        }
        if (ocr_install_.exchange(false)) {
            try {
                status("Installing selected OCR language data…");
                install_ocr_languages(data_directory_ / "OCR", o["languages"],
                                      [&] { return stop.stop_requested(); });
                recognizer.reset();
                loaded = Json();
                status("OCR language data installed; recognition can be tested");
            } catch (const std::exception &) {
                status("OCR language installation failed; check the network and retry. Unverified models will not load.");
            }
        }
        if ((wanted || testing) && window && Clock::now() >= next) {
            next = Clock::now() + std::chrono::milliseconds(500);
            try {
                auto epoch = ocr_epoch_.load();
                auto region = o.at("region");
                if (region.empty())
                    throw std::runtime_error("Select the native chat text region first");
                if (!capture)
                    capture = platform::make_ocr_capture();
                auto image = capture->grab(window, {region[0], region[1], region[2], region[3]}, region[4],
                                           region[5], region[6]);
                if (image && Clock::now() - last_recognize >= std::chrono::seconds(1)) {
                    // Sample quantized brightness, not a cryptographic hash of
                    // every pixel; tiny antialiasing changes should be ignored.
                    std::vector<uint8_t> pixels;
                    for (int y = 0; y < image->height; y += 4)
                        for (int x = 0; x < image->width; x += 4) {
                            auto p = image->pixels.data() + size_t(y) * image->stride + x * 4;
                            pixels.push_back(std::max({p[0], p[1], p[2]}) / 32);
                        }
                    size_t changed_pixels = 0;
                    if (pixels.size() == last_pixels.size())
                        for (size_t i = 0; i < pixels.size(); ++i)
                            changed_pixels += pixels[i] != last_pixels[i];
                    bool dirty = pixels.size() != last_pixels.size() ||
                                 changed_pixels > std::max(size_t(3), pixels.size() / 2000);
                    if (dirty || confirm || testing) {
                        if (!recognizer || loaded != o["languages"]) {
                            std::string languages;
                            for (auto &l : o["languages"]) {
                                auto id = l.get<std::string>();
                                auto file = data_directory_ / "OCR" / (id + ".traineddata");
                                auto model = std::find_if(ocr_models().begin(), ocr_models().end(),
                                                          [&](auto &m) { return m.id == id; });
                                if (!std::filesystem::exists(file) || model == ocr_models().end() ||
                                    sha256(read_bytes(file)) != model->hash)
                                    throw std::runtime_error("Install the selected OCR language data; a model is missing or failed verification");
                                if (!languages.empty())
                                    languages += '+';
                                languages += id;
                            }
                            recognizer = platform::make_ocr_recognizer(data_directory_ / "OCR", languages);
                            loaded = o["languages"];
                        }
                        auto before = Clock::now();
                        auto lines = recognizer->recognize(*image);
                        auto duration =
                            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - before)
                                .count();
                        last_recognize = Clock::now();
                        last_pixels = std::move(pixels);
                        if (testing) {
                            std::string preview;
                            for (auto &line : lines)
                                preview += line.text + "\n";
                            {
                                std::lock_guard lock(ocr_mutex_);
                                ocr_preview_ = preview.empty() ? "No text recognized" : preview;
                            }
                            testing = false;
                            status("Local OCR preview completed without an API request in " + std::to_string(duration) + " ms");
                        } else if ((pure || chat_state_ == 2) && ocr_epoch_ == epoch &&
                                   settings().at("ocr") == o && game_window_ == window) {
                            auto messages = ocr_messages(lines);
                            auto added = tracker.observe(messages);
                            confirm = dirty; // One confirmation pass even when the next image is unchanged.
                            for (auto &text : added) {
                                Message m;
                                m.event = "OCR_CHAT";
                                m.text = std::move(text);
                                m.session = session;
                                m.seq = ++seq;
                                m.sent = uint32_t(std::time(nullptr));
                                m.received = m.sent;
                                m.session_order = uint64_t(session);
                                m.receive_order = seq;
                                submit(std::move(m));
                            }
                            status(std::string(pure ? "OCR-only recognition · " : "Chat is restricted; OCR fallback recognition · ") +
                                   std::to_string(duration) +
                                   " ms · new messages: " + std::to_string(added.size()) + " messages");
                        }
                    }
                }
            } catch (const std::exception &e) {
                status(std::string("OCR paused: ") + e.what());
                capture.reset();
                testing = false;
                next = Clock::now() + std::chrono::seconds(5);
            } catch (...) {
                status("OCR paused: system window capture is unavailable or the HDR image could not be read");
                capture.reset();
                testing = false;
                next = Clock::now() + std::chrono::seconds(5);
            }
        }
        if (!wanted && !testing) {
            capture.reset();
            recognizer.reset();
            loaded = Json();
        }
        stop.wait_for(std::chrono::milliseconds(wanted || testing ? 100 : 1000));
    }
}
} // namespace bl
