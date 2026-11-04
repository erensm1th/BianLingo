#pragma once
#include "windows_widgets.hpp"
#include <algorithm>
#include <chrono>
#include <commctrl.h>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace bl::ui_checks {
inline std::string read_number(HWND h) {
    wchar_t value[32]{};
    int n = GetWindowTextW(h, value, 32);
    std::string result;
    for (int i = 0; i < n; ++i) {
        if (value[i] > 127)
            throw std::runtime_error("Non-ASCII numeric fixture");
        result += static_cast<char>(value[i]);
    }
    return result;
}
inline void write_number(HWND h, const std::string &value) {
    std::wstring wide(value.begin(), value.end());
    SetWindowTextW(h, wide.c_str());
}

inline void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
struct Pixels {
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ original{};
    uint32_t *data{};
    int width{}, height{};
    explicit Pixels(HWND h) {
        RECT r{};
        GetClientRect(h, &r);
        width = r.right;
        height = r.bottom;
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *bits{};
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        data = static_cast<uint32_t *>(bits);
        if (!dc || !bitmap)
            throw std::runtime_error("Cannot allocate interaction screenshot");
        original = SelectObject(dc, bitmap);
    }
    ~Pixels() {
        if (dc) {
            SelectObject(dc, original);
            if (bitmap)
                DeleteObject(bitmap);
            DeleteDC(dc);
        }
    }
    Pixels(const Pixels &) = delete;
};
struct Comparison {
    Pixels actual, expected;
    explicit Comparison(HWND h) : actual(h), expected(h) {}
    void check(HWND h) {
        require(IsWindowVisible(h), "Interaction screenshot target is hidden");
        UpdateWindow(h);
        auto dc = GetDC(h);
        BOOL copied = BitBlt(actual.dc, 0, 0, actual.width, actual.height, dc, 0, 0, SRCCOPY);
        ReleaseDC(h, dc);
        require(copied != FALSE, "Cannot capture live control frame");
        SendMessageW(h, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(expected.dc), PRF_CLIENT);
        GdiFlush();
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        bool shaped = GetWindowRgn(h, region) != ERROR;
        auto difference = [&] {
            for (int i = 0; i < actual.width * actual.height; ++i) {
                if (shaped && !PtInRegion(region, i % actual.width, i / actual.width)) continue;
                if ((actual.data[i] & 0xffffff) != (expected.data[i] & 0xffffff)) return i;
            }
            return -1;
        };
        int mismatch = difference();
        if (mismatch >= 0) {
            // A window DC may still expose the preceding compositor frame.
            // Synchronize once and sample again without invalidation/repainting;
            // persistent stale pixels must still fail this check.
            DwmFlush();
            auto current_dc = GetDC(h);
            copied = BitBlt(actual.dc, 0, 0, actual.width, actual.height, current_dc, 0, 0, SRCCOPY);
            ReleaseDC(h, current_dc);
            GdiFlush();
            require(copied != FALSE, "Cannot recapture synchronized control frame");
            mismatch = difference();
        }
        DeleteObject(region);
        if (mismatch >= 0) {
            auto kind = winui::kind(h);
            throw std::runtime_error("Live frame mismatch: kind=" + std::to_string(kind ? int(*kind) : -1) +
                                     " x=" + std::to_string(mismatch % actual.width) +
                                     " y=" + std::to_string(mismatch / actual.width) +
                                     " actual=" + std::to_string(actual.data[mismatch] & 0xffffff) +
                                     " expected=" + std::to_string(expected.data[mismatch] & 0xffffff));
        }
    }
};
// Tests real child windows at every input stage, not just a final PrintWindow screenshot.
template <class App> void verify_widgets(App &app) {
    app.restore_appearance();
    app.switch_page(0);
    ShowWindow(app.settings, SW_SHOW);
    SetForegroundWindow(app.settings);
    RedrawWindow(app.settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    auto automatic =
        std::find_if(app.checks.begin(), app.checks.end(), [](const auto &c) { return c.key == "auto"; });
    require(automatic != app.checks.end(), "Missing toggle fixture");
    HWND toggle = automatic->h;
    require(GetWindowTextLengthW(toggle) > 0 && GetWindowTextLengthW(app.brand) > 0,
            "Initial widget captions were not retained");
    for (auto label : app.pages[0])
        if (winui::kind(label) == winui::Kind::Label)
            require(GetWindowTextLengthW(label) > 0, "Appearance label lost its initial caption");
    require(winui::kind(toggle) == winui::Kind::Toggle, "Toggle still paints over a native button");
    for (const auto &n : app.number_rows)
        require(winui::kind(n.slider) == winui::Kind::Slider, "Slider still paints over a native trackbar");
    require(winui::kind(app.design_select) == winui::Kind::Choice &&
                winui::kind(app.hotkey) == winui::Kind::Hotkey,
            "Choice/hotkey still use a native painted-over model");
    for (auto choice : {app.design_select, app.skin_select, app.prompt_style}) {
        RECT outer{}, client{};
        GetWindowRect(choice, &outer);
        GetClientRect(choice, &client);
        require(!(GetWindowLongPtrW(choice, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL | WS_BORDER)) &&
                    outer.right - outer.left == client.right && outer.bottom - outer.top == client.bottom,
                "Owned choice retained native nonclient chrome");
    }
    auto route = [&](HWND h, UINT message, WPARAM key, LPARAM flags = 0) {
        MSG event{};
        event.hwnd = h;
        event.message = message;
        event.wParam = key;
        event.lParam = flags;
        if (!IsDialogMessageW(app.settings, &event))
            DispatchMessageW(&event);
    };
    std::map<HWND, std::unique_ptr<Comparison>> frames;
    size_t frame_count = 0;
    auto check = [&](HWND h) {
        auto previous_scroll = app.body_scroll;
        app.ensure_setting_visible(h);
        if (previous_scroll != app.body_scroll)
            RedrawWindow(app.settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        auto &frame = frames[h];
        RECT bounds{};
        GetClientRect(h, &bounds);
        if (!frame || frame->actual.width != bounds.right || frame->actual.height != bounds.bottom)
            frame = std::make_unique<Comparison>(h);
        frame->check(h);
        ++frame_count;
    };
    auto start = std::chrono::steady_clock::now();
    auto previous_registry = app.theme_registry;
    auto previous_preview = app.appearance_preview;
    auto fixture_dir =
        platform::executable_dir().parent_path().parent_path() / ".local" / "qa" / "theme-fixtures";
    std::filesystem::create_directories(fixture_dir);
    app.theme_registry = ThemeRegistry(fixture_dir / "Skins");
    for (int variant = 0; variant < 2; ++variant) {
        auto definition = app.theme_registry.export_theme(
            default_settings(), variant ? "test.checkbox" : "test.mixed", variant ? "Checkbox" : "Mixed");
        definition["metrics"]["sliderPadding"] = variant ? 30 : 23;
        definition["metrics"]["thumbSize"] = variant ? 16 : 20;
        definition["metrics"]["popupRowHeight"] = 38;
        definition["widgets"]["toggle"] = variant ? "checkbox" : "switch";
        definition["widgets"]["slider"] = variant ? "round" : "square";
        definition["colors"]["focus"] = "#FF3355";
        auto file = fixture_dir / (variant ? "checkbox.json" : "mixed.json");
        write_json(file, definition);
        app.theme_registry.import_file(file);
    }
    app.fill_design_choices(app.appearance());
    auto theme_variants_tested = app.design_choices.size();
    auto saved_font = app.chat_font;
    auto saved_render_count = app.chat_render_calls;
    std::vector<std::string> numbers;
    for (const auto &n : app.number_rows)
        numbers.push_back(read_number(n.edit));
    for (int style = 0; style < int(app.design_choices.size()); ++style) {
        SendMessageW(app.design_select, CB_SETCURSEL, style, 0);
        app.select_design();
        {
            Pixels icon(app.gear);
            RECT bounds{}; GetClientRect(app.gear, &bounds);
            for (UINT state : {0u, UINT(ODS_SELECTED), UINT(ODS_FOCUS)}) {
                DRAWITEMSTRUCT item{};
                item.CtlType = ODT_BUTTON; item.CtlID = GetDlgCtrlID(app.gear);
                item.hwndItem = app.gear; item.hDC = icon.dc; item.rcItem = bounds; item.itemState = state;
                SendMessageW(app.main, WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
                require(GetPixel(icon.dc, 0, 0) == RGB(1, 2, 3) &&
                        GetPixel(icon.dc, bounds.right - 1, bounds.bottom - 1) == RGB(1, 2, 3),
                        "Settings icon has an opaque tile or border in this theme/state");
                require(GetPixel(icon.dc, bounds.right / 2, bounds.bottom / 2) != RGB(1, 2, 3),
                        "Transparent settings button lost the cat artwork");
            }
        }
        RedrawWindow(app.settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        int before = int(SendMessageW(toggle, BM_GETCHECK, 0, 0));
        SendMessageW(toggle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 12));
        check(toggle);
        SendMessageW(toggle, WM_LBUTTONUP, 0, MAKELPARAM(12, 12));
        check(toggle);
        require(SendMessageW(toggle, BM_GETCHECK, 0, 0) != before,
                "Mouse toggle did not change exactly once");
        SetFocus(toggle);
        route(toggle, WM_KEYDOWN, VK_SPACE);
        check(toggle);
        route(toggle, WM_KEYUP, VK_SPACE);
        check(toggle);
        require(SendMessageW(toggle, BM_GETCHECK, 0, 0) == before,
                "Space toggle did not change exactly once");
        SendMessageW(toggle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 12));
        SendMessageW(toggle, WM_LBUTTONUP, 0, MAKELPARAM(-8, -8));
        check(toggle);
        require(SendMessageW(toggle, BM_GETCHECK, 0, 0) == before, "Outside release toggled switch");
        EnableWindow(toggle, FALSE);
        SendMessageW(toggle, BM_CLICK, 0, 0);
        check(toggle);
        require(SendMessageW(toggle, BM_GETCHECK, 0, 0) == before, "Disabled toggle accepted input");
        EnableWindow(toggle, TRUE);
        const auto renders_before_sliders = app.chat_render_calls;
        for (const auto &n : app.number_rows) {
            RECT r{};
            GetClientRect(n.slider, &r);
            auto geometry = app.slider_layout(n.slider);
            int pad = geometry.start, width = geometry.end - geometry.start;
            SendMessageW(n.slider, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pad, r.bottom / 2));
            check(n.slider);
            auto allocations = winui::diagnostics().allocations;
            for (int step = 0; step <= 120; ++step) {
                int x = pad + MulDiv(step, width, 120);
                SendMessageW(n.slider, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, r.bottom / 2));
                check(n.slider);
                require(SendMessageW(n.slider, TBM_GETPOS, 0, 0) == geometry.value(x, n.minimum, n.maximum),
                        "Theme slider hit testing differs from painted geometry");
            }
            for (int step = 120; step >= 0; --step) {
                int x = pad + MulDiv(step, width, 120);
                SendMessageW(n.slider, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, r.bottom / 2));
                check(n.slider);
            }
            SendMessageW(n.slider, WM_LBUTTONUP, 0, MAKELPARAM(pad, r.bottom / 2));
            check(n.slider);
            require(winui::diagnostics().allocations == allocations,
                    "Dragging reallocates the retained paint buffer");
            require(read_number(n.edit) == std::to_string(n.minimum),
                    "Slider drag and number field diverged");
            auto paints = winui::diagnostics().paints;
            for (int i = 0; i < 50; ++i) {
                SendMessageW(n.slider, TBM_SETPOS, TRUE, n.minimum);
                UpdateWindow(n.slider);
            }
            require(winui::diagnostics().paints == paints, "Unchanged slider position triggers repaint");
            SetFocus(n.slider);
            route(n.slider, WM_KEYDOWN, VK_END);
            check(n.slider);
            require(SendMessageW(n.slider, TBM_GETPOS, 0, 0) == n.maximum,
                    "End key does not reach slider maximum");
            route(n.slider, WM_KEYDOWN, VK_LEFT);
            check(n.slider);
            require(SendMessageW(n.slider, TBM_GETPOS, 0, 0) == n.maximum - 1,
                    "Slider arrow key range handling failed");
            SendMessageW(n.slider, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(width / 2, r.bottom / 2));
            ReleaseCapture();
            auto value = SendMessageW(n.slider, TBM_GETPOS, 0, 0);
            SendMessageW(n.slider, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(width, r.bottom / 2));
            require(SendMessageW(n.slider, TBM_GETPOS, 0, 0) == value,
                    "Slider continues dragging after capture loss");
        }
        require(app.chat_render_calls == renders_before_sliders,
                "Slider previews unnecessarily rebuild chat history");
        SetFocus(app.skin_select);
        route(app.skin_select, WM_KEYDOWN, VK_F4);
        require(app.picker != nullptr, "Dialog route did not open picker");
        route(app.skin_select, WM_KEYDOWN, VK_END);
        route(app.skin_select, WM_KEYDOWN, VK_ESCAPE);
        require(!app.picker, "Dialog route swallowed picker Escape");
        route(app.skin_select, WM_KEYDOWN, VK_F4);
        route(app.skin_select, WM_KEYDOWN, VK_HOME);
        route(app.skin_select, WM_KEYDOWN, VK_RETURN);
        require(!app.picker, "Dialog route swallowed picker Enter");
        check(app.skin_select);
        route(app.skin_select, WM_KEYDOWN, VK_F4);
        route(app.skin_select, WM_KEYDOWN, VK_TAB);
        require(!app.picker && GetFocus() != app.skin_select, "Tab did not dismiss picker and move focus");
    }
    require(app.chat_font == saved_font && app.chat_render_calls > saved_render_count,
            "Theme changes must recolor chat without recreating its font");
    for (size_t i = 0; i < numbers.size(); ++i)
        write_number(app.number_rows[i].edit, numbers[i]);
    // Warm both styles, then check repeated switches and popup lifetimes for retained resources.
    auto cycle = [&](int style) {
        SendMessageW(app.design_select, CB_SETCURSEL, style, 0);
        app.select_design();
        {
            Pixels icon(app.gear);
            RECT bounds{}; GetClientRect(app.gear, &bounds);
            for (UINT state : {0u, UINT(ODS_SELECTED), UINT(ODS_FOCUS)}) {
                DRAWITEMSTRUCT item{};
                item.CtlType = ODT_BUTTON; item.CtlID = GetDlgCtrlID(app.gear);
                item.hwndItem = app.gear; item.hDC = icon.dc; item.rcItem = bounds; item.itemState = state;
                SendMessageW(app.main, WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
                require(GetPixel(icon.dc, 0, 0) == RGB(1, 2, 3) &&
                        GetPixel(icon.dc, bounds.right - 1, bounds.bottom - 1) == RGB(1, 2, 3),
                        "Settings icon has an opaque tile or border in this theme/state");
                require(GetPixel(icon.dc, bounds.right / 2, bounds.bottom / 2) != RGB(1, 2, 3),
                        "Transparent settings button lost the cat artwork");
            }
        }
        RedrawWindow(app.settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        SendMessageW(app.skin_select, CB_SHOWDROPDOWN, TRUE, 0);
        if (app.picker) {
            UpdateWindow(app.picker);
            Comparison popup_frame(app.picker);
            popup_frame.check(app.picker);
            bool nonempty = false;
            for (int i = 0; i < popup_frame.expected.width * popup_frame.expected.height; ++i)
                if ((popup_frame.expected.data[i] & 0xffffff) != (popup_frame.expected.data[0] & 0xffffff)) {
                    nonempty = true;
                    break;
                }
            require(nonempty, "Picker paint produced a blank surface");
        }
        app.close_picker(false);
        UpdateWindow(app.skin_select);
    };
    for (int i = 0; i < int(app.design_choices.size()); ++i)
        cycle(i);
    cycle(0);
    auto handles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    auto live = winui::diagnostics().live_widgets;
    for (int i = 0; i < 30; ++i)
        cycle(i % int(app.design_choices.size()));
    require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= handles,
            "Style/popup stress leaks GDI objects");
    require(winui::diagnostics().live_widgets == live, "Style/popup stress leaks widgets");
    app.poll();
    RedrawWindow(app.settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    auto idle_before = winui::diagnostics();
    for (int i = 0; i < 200; ++i) {
        app.poll();
        RedrawWindow(app.settings, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    auto idle_after = winui::diagnostics();
    require(idle_after.paints == idle_before.paints, "Stable idle polls keep repainting the interface");
    require(idle_after.allocations == idle_before.allocations, "Stable idle polls allocate new UI buffers");
    // Destroying a focused/open choice must dismiss its popup and free its buffer.
    auto lifecycle_before = winui::diagnostics();
    auto gdi_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 40; ++i) {
        auto temporary =
            app.control(app.body, L"COMBOBOX", "", 20, 10, 180, 180, CBS_DROPDOWNLIST | WS_TABSTOP, 0);
        SendMessageW(temporary, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"temporary"));
        SendMessageW(temporary, CB_SETCURSEL, 0, 0);
        UpdateWindow(temporary);
        SendMessageW(temporary, CB_SHOWDROPDOWN, TRUE, 0);
        require(app.picker != nullptr, "Temporary choice failed to open");
        DestroyWindow(temporary);
        require(!app.picker, "Destroyed choice left an orphan popup");
    }
    require(winui::diagnostics().live_widgets == lifecycle_before.live_widgets &&
                winui::diagnostics().buffer_bytes == lifecycle_before.buffer_bytes,
            "Destroyed widgets retain model or paint-buffer memory");
    require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi_before,
            "Destroying widgets leaks GDI objects");
    auto before_failure = app.active_theme;
    bool rejected = false;
    try {
        app.appearance_transaction([&] {
            auto invalid = app.appearance();
            invalid["ui_style"] = "missing.theme";
            app.appearance_preview = invalid;
        });
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected && app.active_theme == before_failure, "Failed preview does not restore working theme");
    app.theme_registry = std::move(previous_registry);
    app.appearance_preview = previous_preview;
    app.fill_design_choices(app.appearance());
    app.restore_appearance();
    auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    auto qa = platform::executable_dir().parent_path().parent_path() / ".local" / "qa";
    std::filesystem::create_directories(qa);
    std::ofstream out(qa / "ui-interaction-checks.json");
    out << Json{{"live_frames_compared", frame_count},
                {"elapsed_ms", elapsed},
                {"style_switches", 30},
                {"theme_variants_tested", theme_variants_tested},
                {"shared_geometry_assertions", true},
                {"failed_preview_rollback", true},
                {"idle_polls", 200},
                {"idle_paints", idle_after.paints - idle_before.paints},
                {"idle_allocations", idle_after.allocations - idle_before.allocations},
                {"gdi_objects_after_warmup", handles},
                {"gdi_objects_after_stress", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)},
                {"retained_buffer_bytes", winui::diagnostics().buffer_bytes},
                {"chat_font_recreated", false},
                {"chat_reflows_from_style_preview", 0}}
               .dump(2);
}
} // namespace bl::ui_checks
