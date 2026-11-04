#include "bianlingo/chat_style.hpp"
#include "bianlingo/chat_tabs.hpp"
#include "bianlingo/ocr.hpp"
#include "bianlingo/reply_target.hpp"
#include <iostream>
#ifdef _WIN32
#include <cstring>
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif
using namespace bl;
void check(bool ok, const char *reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
int main(int argc, char **argv) {
    try {
        auto s = default_settings();
        validate_ocr(s);
        check(!ocr_pure_mode(s), "Pure OCR enabled by default");
        auto fallback = s;
        fallback["ocr"].erase("always");
        validate_ocr(fallback);
        check(!ocr_pure_mode(fallback), "Fallback OCR settings changed mode");
        auto invalid_mode = s;
        invalid_mode["ocr"]["always"] = "true";
        bool invalid_rejected = false;
        try {
            validate_ocr(invalid_mode);
        } catch (...) {
            invalid_rejected = true;
        }
        check(invalid_rejected, "Nonboolean pure OCR switch accepted");
        auto bad = s;
        bad["ocr"]["region"] = Json::array({0, 0, 1, 1, 1920, 1080, 96});
        bool rejected = false;
        try {
            validate_ocr(bad);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Invalid crop accepted");
        bad = s;
        bad["ocr"]["languages"] = Json::array({"../../other"});
        rejected = false;
        try {
            validate_ocr(bad);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Untrusted language filename accepted");
        check(ocr_body("[12:34:56] 玩家说：need healer") == "need healer",
              "Timestamp/Chinese name leaks into translation body");
        check(ocr_models().size() == 4, "Missing pinned model catalog");
        auto lines = std::vector<OcrLine>{{"[Party] [Runga]: need healer", 0, 0, 300, 20, 95},
                                          {"for Deadmines", 0, 23, 200, 20, 90},
                                          {"Other panel", 0, 100, 100, 20, 30},
                                          {"[Other]: hello there", 0, 130, 200, 20, 96}};
        auto messages = ocr_messages(lines);
        check(messages.size() == 2 && messages[0].ends_with("for Deadmines"),
              "Message headers/wrapped lines not reconstructed");
        check(ocr_messages({{"[4. Trade] Player-Realm: need healer", 0, 0, 380, 20, 80}}).size() == 1,
              "Classic bare speaker after channel was missed");
        check(ocr_messages({{"ie aN [Muffinz-Galakras]: LFM HEAL SOO 10man", 0, 0, 380, 20, 68},
                            {"starting at juggernaut pst", 0, 23, 300, 20, 95}}).size() == 1,
              "Noisy Classic channel prefix lost readable speaker");
        OcrTracker tracker;
        check(tracker.observe({"a", "b", "c", "d"}).empty(), "Unstable first frame emitted");
        check(tracker.observe({"a", "b", "c", "d"}) == std::vector<std::string>({"b", "c", "d"}),
              "Initial takeover floods history");
        check(tracker.observe({"b", "c", "d", "d"}).empty(), "Changed frame emitted without stability");
        check(tracker.observe({"b", "c", "d", "d"}) == std::vector<std::string>({"d"}),
              "Repeated new occurrence was swallowed");
        check(tracker.observe({"b", "c", "d", "d"}).empty(), "Identical frame duplicates API work");
        tracker.observe({"unrelated history"});
        check(tracker.observe({"unrelated history"}).empty(),
              "Scrolling to unrelated history emits messages");
        tracker.observe({});
        tracker.observe({});
        check(tracker.observe({"unrelated history"}).empty(), "Fade loses dedup baseline");
        Message m;
        m.event = "OCR_CHAT";
        m.text = "[Name]: hello";
        check(!reply_target(m) && !whisper_target(m), "OCR identity treated as trusted reply target");
        check(chat_channel_label(m) == "【OCR】 ", "Missing OCR source label");
        auto tab = parse_chat_tabs(Json::array({new_chat_tab("main", "General")}))[0];
        check(tab.matches(m), "OCR excluded from new comprehensive tab");
        check(!allows(s, m), "Disabled OCR passes translation filter");
        s["ocr"]["enabled"] = true;
        check(allows(s, m), "Enabled OCR rejected");
        Message bridge;
        bridge.event = "CHAT_MSG_PARTY";
        bridge.text = "need a healer";
        check(allows(s, bridge), "Fallback mode blocked pixel chat");
        s["ocr"]["always"] = true;
        check(ocr_pure_mode(s) && allows(s, m) && !allows(s, bridge),
              "Pure OCR did not exclusively route OCR chat");
        s["ocr"]["enabled"] = false;
        check(!ocr_pure_mode(s) && allows(s, bridge), "Disabling OCR did not restore pixel chat");
#ifdef _WIN32
        if (argc == 3 && std::string_view(argv[1]) == "--download") {
            install_ocr_languages(std::filesystem::u8path(argv[2]), Json::array({"eng"}),
                                  [] { return false; });
            std::cout << "Native model download, pinned SHA256 and atomic installation passed\n";
        }
        if (argc == 2) {
            auto dc = CreateCompatibleDC(nullptr);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = 900;
            info.bmiHeader.biHeight = -180;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            void *pixels = nullptr;
            auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            auto old = SelectObject(dc, bitmap);
            auto font = CreateFontW(-28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                    ANTIALIASED_QUALITY, 0, L"Arial");
            auto old_font = SelectObject(dc, font);
            RECT box{0, 0, 900, 180};
            FillRect(dc, &box, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(170, 170, 255));
            const wchar_t *message = L"[Party] [Player]: need a healer for Deadmines";
            TextOutW(dc, 10, 15, message, int(wcslen(message)));
            SetTextColor(dc, RGB(255, 128, 255));
            message = L"[Friend]: thanks for the group";
            TextOutW(dc, 10, 65, message, int(wcslen(message)));
            GdiFlush();
            Image image;
            image.width = 900;
            image.height = 180;
            image.stride = 3600;
            image.pixels.resize(648000);
            std::memcpy(image.pixels.data(), pixels, image.pixels.size());
            SelectObject(dc, old_font);
            DeleteObject(font);
            SelectObject(dc, old);
            DeleteObject(bitmap);
            DeleteDC(dc);
            auto start = Clock::now();
            auto recognizer = platform::make_ocr_recognizer(std::filesystem::u8path(argv[1]), "eng");
            auto found = recognizer->recognize(image);
            auto reconstructed = ocr_messages(found);
            for (auto &line : found)
                std::cout << line.confidence << " " << line.text << '\n';
            PROCESS_MEMORY_COUNTERS_EX memory_before{}, memory_after{};
            GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory_before),
                                 sizeof(memory_before));
            auto repeated = Clock::now();
            for (int i = 0; i < 20; ++i)
                check(ocr_messages(recognizer->recognize(image)).size() == 2,
                      "Repeated recognition lost messages");
            GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory_after),
                                 sizeof(memory_after));
            std::cout
                << "20 warm recognitions ms="
                << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - repeated).count()
                << " private bytes " << memory_before.PrivateUsage << " -> " << memory_after.PrivateUsage
                << '\n';
            check(reconstructed.size() == 2 && reconstructed[0].find("healer") != std::string::npos,
                  "Real local OCR failed native-coloured chat fixture");
            std::cout << "Real Tesseract load+recognition ms="
                      << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count()
                      << '\n';
        }
#endif
        std::cout << "OCR parsing, sequence dedup, settings, source and read-only checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
