#include "bianlingo/ocr.hpp"
#include "bianlingo/provider.hpp"
#include <windows.h>
// GDI+ imaging declarations require COM interfaces before their headers.
// clang-format off
#include <objidl.h>
#include <gdiplus.h>
// clang-format on

#include "bianlingo/bubble.hpp"
#include "bianlingo/blocklist.hpp"
#include "bianlingo/local_models.hpp"
#include "bianlingo/ollama_runtime.hpp"
#include "bianlingo/bundle.hpp"
#include "bianlingo/chat_preview.hpp"
#include "bianlingo/chat_style.hpp"
#include "bianlingo/chat_tabs.hpp"
#include "bianlingo/core.hpp"
#include "bianlingo/localization.hpp"
#include "bianlingo/quick_reply.hpp"
#include "bianlingo/reply_target.hpp"
#include "bianlingo/theme.hpp"
#include "version.hpp"
#include "windows_edit_scroll.hpp"
#include "windows_theme_paint.hpp"
#include "windows_widgets.hpp"
#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <dwmapi.h>
#include <imm.h>
#include <richedit.h>
#include <set>
#include <shellapi.h>
#include <shobjidl.h>
#include <sstream>
#ifdef BL_UI_TESTS
#include "windows_ui_checks.hpp"
#include <fstream>
#endif
#include <windowsx.h>

namespace {
using namespace bl;
namespace widgets = bl::winui;
std::filesystem::path settings_data_directory() {
#ifdef BL_UI_TESTS
    static const auto directory = platform::executable_dir() / "ui-test-state" /
                                  ("run-" + sha256(random_bytes(16)).substr(0, 16));
    std::filesystem::create_directories(directory);
    return directory;
#else
    return platform::app_data();
#endif
}
// Align the initial overlay's right/bottom edges with the centered settings window.
// Smaller work areas keep the overlay visible even when side-by-side space is limited.
RECT initial_overlay_rect(const RECT &work, SIZE overlay, SIZE settings) {
    const LONG width = std::clamp(overlay.cx, 1L, std::max(1L, work.right - work.left));
    const LONG height = std::clamp(overlay.cy, 1L, std::max(1L, work.bottom - work.top));
    const LONG settings_left = work.left + (work.right - work.left - settings.cx) / 2;
    const LONG settings_bottom = work.top + (work.bottom - work.top + settings.cy) / 2;
    const LONG x = std::clamp(settings_left - width, work.left, work.right - width);
    const LONG y = std::clamp(settings_bottom - height, work.top, work.bottom - height);
    return {x, y, x + width, y + height};
}
constexpr COLORREF transparency_key = RGB(1, 2, 3);
COLORREF bg = RGB(25, 25, 25), panel = RGB(40, 40, 40), fg = RGB(230, 230, 230), accent = RGB(169, 181, 197);
COLORREF mix(COLORREF a, COLORREF b) {
    return RGB((GetRValue(a) + GetRValue(b)) / 2, (GetGValue(a) + GetGValue(b)) / 2,
               (GetBValue(a) + GetBValue(b)) / 2);
}
constexpr UINT tray_message = WM_APP + 1;
constexpr int id_gear = 10, id_save = 11, id_exit = 12, id_balance = 15, id_translate = 16,
              id_reset = 18, id_output_close = 21,
              id_output_prompt_reset = 22, id_prompt_style = 23, id_output_style = 24, id_notices = 25,
              id_logs = 26, id_incoming_prompt_reset = 27, id_skin = 28, id_skin_reset = 29, id_skin_color0 = 30, id_design = 34,
              id_theme_import = 35, id_theme_export = 36, id_theme_reload = 37, id_theme_folder = 38,
              id_chat_preview = 39, id_tab_editor = 40, id_tab_add = 41, id_tab_delete = 42,
              id_tab_import = 43, id_tab_manage = 44, id_bubble_preview = 45, id_ui_language = 46, id_nav0 = 60,
              id_provider_select = 80, id_provider_add = 81, id_provider_delete = 82,
              id_provider_advanced = 83, id_provider_test = 84, id_provider_apply = 85,
              id_provider_cancel = 86, id_ocr_open = 90, id_ocr_region = 91, id_ocr_install = 92,
              id_ocr_test = 93, id_ocr_close = 94, id_provider_local = 95, id_provider_bergamot = 96,
              id_ollama_setup = 97, id_chat_target = 98, id_ollama_model = 99, id_ollama_apply = 100,
              id_ollama_custom = 101, id_blocks_open = 110, id_blocks_add = 111, id_blocks_remove = 112,
              id_blocks_sync = 114, id_models_open = 120, id_models_now = 121,
              id_models_current = 122, id_models_all = 123, id_models_prepare = 124, id_models_cancel = 125,
              id_models_folder = 126, id_models_search = 128, id_models_pair = 129,
              id_models_add = 130, id_models_remove = 131, id_logs_clear = 132, id_privacy = 133;
constexpr std::array<std::string_view, 26> chat_targets{
    "en-US", "en-GB", "es", "pt-BR", "pt-PT", "fr", "de", "it", "ru", "uk", "pl", "nl",
    "sv", "da", "nb", "fi", "cs", "tr", "el", "hu", "ro", "zh-Hans", "zh-Hant", "ja", "ko", "ar"};
std::wstring wide(std::string_view s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n && !s.empty())
        throw std::runtime_error("Invalid UTF-8");
    std::wstring w(n, 0);
    if (n)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string narrow(std::wstring_view w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    if (n)
        WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::string text(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring w(n + 1, 0);
    GetWindowTextW(h, w.data(), n + 1);
    w.resize(n);
    return narrow(w);
}
// Remember only catalogued UI captions, never editable values or chat content.
std::map<HWND, std::string> ui_caption_sources;
void remember_caption(HWND h, std::string_view source) {
    if (!h) return;
    if (!has_ui_translation(source)) { ui_caption_sources.erase(h); return; }
    ui_caption_sources[h] = source;
    SetWindowSubclass(h, [](HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) -> LRESULT {
        if (message == WM_NCDESTROY) ui_caption_sources.erase(window);
        return DefSubclassProc(window, message, w, l);
    }, 901, 0);
}
void set(HWND h, std::string_view s) {
    wchar_t type[64]{};
    GetClassNameW(h, type, 64);
    const bool editable = _wcsicmp(type, L"Edit") == 0 || _wcsicmp(type, L"RICHEDIT50W") == 0 ||
                          _wcsicmp(type, L"ComboBox") == 0;
    if (!editable) remember_caption(h, s);
    auto w = wide(editable ? std::string(s) : ui_text(s));
    int n = GetWindowTextLengthW(h);
    if (n == int(w.size())) {
        std::wstring current(size_t(n) + 1, L'\0');
        GetWindowTextW(h, current.data(), n + 1);
        current.resize(n);
        if (current == w)
            return;
    }
    SetWindowTextW(h, w.c_str());
}
struct GraphicsRuntime {
    ULONG_PTR token{};
    GraphicsRuntime() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
            throw std::runtime_error("Failed to initialize UI drawing");
    }
    ~GraphicsRuntime() {
        Gdiplus::GdiplusShutdown(token);
    }
};
Gdiplus::Color gcolor(COLORREF c) {
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}
void rounded(HDC dc, RECT rect, COLORREF fill, float radius, COLORREF border = CLR_INVALID,
             int border_width = 1) {
    if (rect.right <= rect.left || rect.bottom <= rect.top)
        return;
    if (radius <= 0) {
        auto brush = reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH));
        SetDCBrushColor(dc, fill);
        FillRect(dc, &rect, brush);
        if (border != CLR_INVALID && border_width > 0) {
            SetDCBrushColor(dc, border);
            for (int i = 0; i < border_width; ++i) {
                FrameRect(dc, &rect, brush);
                InflateRect(&rect, -1, -1);
            }
        }
        return;
    }
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    auto path = [](Gdiplus::GraphicsPath &p, float x, float y, float w, float h, float r) {
        r = std::min(r, std::min(w, h) / 2);
        float d = std::max(0.1f, r * 2);
        p.AddArc(x, y, d, d, 180, 90);
        p.AddArc(x + w - d, y, d, d, 270, 90);
        p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
        p.AddArc(x, y + h - d, d, d, 90, 90);
        p.CloseFigure();
    };
    float x = float(rect.left), y = float(rect.top), w = float(rect.right - rect.left),
          h = float(rect.bottom - rect.top);
    Gdiplus::GraphicsPath shape;
    path(shape, x, y, w, h, radius);
    Gdiplus::SolidBrush brush(gcolor(fill));
    g.FillPath(&brush, &shape);
    if (border != CLR_INVALID && border_width > 0 && w > border_width && h > border_width) {
        Gdiplus::GraphicsPath edge;
        float inset = border_width / 2.f;
        path(edge, x + inset, y + inset, w - border_width, h - border_width, std::max(.1f, radius - inset));
        Gdiplus::Pen pen(gcolor(border), float(border_width));
        g.DrawPath(&pen, &edge);
    }
}
struct Control {
    HWND h;
    std::string key;
};
class App;
App *app = nullptr;
LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
class App {
  public:
    GraphicsRuntime graphics_runtime;
    HINSTANCE instance;
    HWND backdrop = nullptr, main = nullptr, chat = nullptr, reply_surface = nullptr, gear = nullptr,
         scrollbar = nullptr, settings = nullptr, body = nullptr, body_scrollbar = nullptr, output = nullptr,
         input = nullptr, target = nullptr, output_status = nullptr, bubble = nullptr,
         previous_foreground = nullptr, settings_status = nullptr, run_status = nullptr, api_key = nullptr,
         hotkey = nullptr, balance_amount = nullptr, balance_detail = nullptr,
         balance_state = nullptr, balance_updated = nullptr, usage_total = nullptr, usage_detail = nullptr;
    HFONT font = nullptr, chat_font = nullptr;
    std::map<HWND, std::unique_ptr<widgets::Buffer>> paint_buffers;
    int applied_chat_font_size = 0, chat_line_height = 1, chat_wheel_remainder = 0;
    UINT applied_chat_dpi = 0;
    RECT last_chat_thumb{};
    bool chat_thumb_valid = false;
    RECT reply_region_bounds{};
    bool reply_region_valid = false, tab_flash_running = false;
    uint64_t tab_flash_epoch = 0;
    void buffered(HWND h, HDC dc, const widgets::Painter &paint) {
        auto &b = paint_buffers[h];
        if (!b)
            b = std::make_unique<widgets::Buffer>();
        b->draw(h, dc, paint);
    }

    HWND chat_target = nullptr, interface_language = nullptr;
    HWND output_style = nullptr, output_heading = nullptr, prompt_style = nullptr, short_prompt = nullptr,
         default_prompt = nullptr;
    HWND incoming_prompt_help = nullptr, outbound_prompt_help = nullptr;
    HWND notices = nullptr, notices_text = nullptr;
    std::map<HWND, UINT> window_dpi;
    HBRUSH brush = CreateSolidBrush(bg), field = CreateSolidBrush(panel),
           sidebar_brush = CreateSolidBrush(mix(bg, panel)), keyed_brush = CreateSolidBrush(transparency_key);
    ThemeRegistry theme_registry{settings_data_directory() / "Skins"};
    ResolvedTheme active_theme;
    SkinPalette active_skin{};
    bool skin_ready = false, skin_sync = false, chat_style_dirty = false;
    HWND skin_select = nullptr, design_select = nullptr;
    HWND picker = nullptr, picker_combo = nullptr;
    int picker_first = 0, picker_hover = -1, picker_rows = 0, picker_row_height = 0;
    bool picker_armed = false;
    std::optional<Json> appearance_preview;
    std::vector<std::pair<std::string, std::string>> skin_choices, design_choices;
    std::array<HWND, 4> skin_buttons{};
    static constexpr std::array<const char *, 4> color_keys{"background", "surface", "text", "accent"};
    struct NumberRow {
        std::string key;
        HWND slider, edit;
        int minimum, maximum;
    };
    std::vector<NumberRow> number_rows, bubble_rows;
    bool bubble_preview = false;
    BubbleAppearance bubble_style;
    std::wstring bubble_caption;
    bool gear_down = false, gear_dragged = false;
    POINT gear_press{};
    HMODULE rich = nullptr;
    std::unique_ptr<Engine> engine;
    PlayerBlocklist ui_blocks;
    Json config;
    std::array<HWND, 6> navigation{};
    HWND page_title = nullptr, page_description = nullptr, brand = nullptr, brand_detail = nullptr,
         version_label = nullptr;
    std::map<std::pair<UINT, int>, HFONT> ui_fonts;
    std::map<int, HICON> brand_icons;
    std::set<HWND> muted_controls;
    std::map<HWND, Rect> body_positions;
    std::map<HWND, Rect> body_seeds;
    std::array<std::vector<std::vector<HWND>>, 6> settings_sections;
    std::array<std::vector<Rect>, 6> settings_cards;
    std::array<std::string, 6> settings_layout_keys;
    std::array<int, 3> settings_category_y{70, 195, 355};
    int body_scroll = 0, body_max_scroll = 0, body_drag_offset = 0;
    bool body_dragging = false;
    Clock::time_point body_scroll_until{};
    static constexpr std::array<const char *, 6> page_titles{"Chat appearance", "Translation scope", "API & Balance", "Send translation",
                                                             "Status", "Chat tabs"};
    static constexpr std::array<const char *, 6> page_descriptions{
        "Blend chat into the game and adjust the display to your habits.",
        "Select chats to translate to reduce unnecessary requests.",
        "Connect a translation service to view balance and usage.",
        "Express thoughts naturally, keep your own way of speaking.",
        "View connection status and chat logs.",
        "Name each tab and choose which messages to show, or import a game chat window."};
    std::array<std::vector<HWND>, 6> pages;
    std::vector<Control> checks, edits;
    std::deque<DisplayMessage> messages, preview_messages;
    std::vector<std::unique_ptr<widgets::EditScrollbar>> edit_scrollbars;
    HWND hovered_button = nullptr;
    FrameVisual settings_layout_frame = FrameVisual::Flat;
    bool message_preview = false;
    std::vector<ChatTab> display_tabs;
    Json tabs_snapshot, tab_draft, game_layout;
    HWND tab_input_surface = nullptr, gear_input_surface = nullptr, resize_input_surface = nullptr;
    int resize_input_edge = 0;
    HWND chat_tab_bar = nullptr, tab_editor = nullptr, tab_name = nullptr, tab_channels = nullptr,
         tab_excluded = nullptr;
    std::vector<std::pair<HWND, std::string>> tab_checks;
    size_t active_tab = 0, editing_tab = 0;
    bool tab_sync = false;
    int tab_first = 0, tab_hover = -3, tab_pressed = -3;
    struct TabHit {
        RECT bounds;
        int index;
    };
    std::vector<TabHit> tab_hits;
    std::map<std::string, unsigned> tab_unread;
    struct ChatRow {
        long start, end;
        size_t index;
        uint32_t session, seq;
    };
    std::vector<ChatRow> chat_rows;
    struct ReplyHit {
        RECT bounds;
        uint32_t session, seq;
        long start;
    };
    std::vector<ReplyHit> reply_hits;
    std::optional<std::pair<uint32_t, uint32_t>> pressed_message;
    std::map<UINT, HCURSOR> reply_cursors;
    POINT chat_press{};
    bool chat_pressed = false, handles_visible = false, scroll_dragging = false;
    UINT chat_button = 0;
    int scroll_drag_offset = 0;
    std::optional<ReplyTarget> chosen_reply;
    std::optional<ReplyTarget> submitted_reply;
    bool retry_reply = false;
    std::string status_note, last_status_note, balance_error;
    BalanceView account;
    bool balance_busy = false, balance_requested = false;
    int active_page = 0;
    bool smoke = false, restarting = false, composing = false;
#ifdef BL_UI_TESTS
    size_t chat_render_calls = 0;
#endif
    DWORD previous_pid = 0;
    QuickReply reply;
    int clipboard_attempts = 0;
    std::string pending_input;
    RECT bubble_area{};
    NOTIFYICONDATAW tray{};
    bool first_launch = false;
    explicit App(HINSTANCE i) : instance(i) {
        first_launch = !std::filesystem::exists(settings_data_directory() / "settings.json") &&
                       !std::filesystem::exists(settings_data_directory() / "provider.dpapi");
        engine = std::make_unique<Engine>(settings_data_directory());
        config = engine->settings();
        set_ui_language_preference(config.value("ui_language", "system"));
        ui_blocks = PlayerBlocklist(engine->blocklist());
        sync_chat_tabs();
        try {
            game_layout = parse_chat_layout(read_json(platform::app_data() / "game-chat-layout.json").dump());
        } catch (...) {
        }
        std::string theme_warning;
        auto selected_theme = theme_registry.fallback(config, &theme_warning);
        if (!theme_warning.empty()) {
            status_note = theme_warning;
            config["ui_style"] = selected_theme.id;
            config["skin"] = "graphite";
            config["skin_colors"] = skin_colors(selected_theme.palette);
        }
        if (!theme_registry.warnings().empty())
            status_note = theme_registry.warnings().front();
        update_skin();
        font = CreateFontW(-MulDiv(14, int(GetDpiForSystem()), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        rich = LoadLibraryW(L"Msftedit.dll");
        if (!rich)
            throw std::runtime_error("Failed to load system text control");
    }
    ~App() {
        model_worker.request_stop();
        if (model_worker.joinable()) model_worker.join();
        close_message_menu();
        close_picker(false);
        if (ollama_setup_process)
            CloseHandle(ollama_setup_process);
        if (ollama_setup_job) CloseHandle(ollama_setup_job);
        engine->stop();
        stop_portable_ollama();
        for (auto window : {model_window, block_window, ocr_selector, ocr_window, provider_advanced, tab_input_surface,
                            gear_input_surface, resize_input_surface, bubble, output, settings, notices})
            if (IsWindow(window)) {
                paint_buffers.erase(window);
                DestroyWindow(window);
            }
        Shell_NotifyIconW(NIM_DELETE, &tray);
        if (IsWindow(backdrop))
            DestroyWindow(backdrop);
        if (chat_font)
            DeleteObject(chat_font);
        if (font)
            DeleteObject(font);
        for (auto [_, f] : ui_fonts)
            DeleteObject(f);
        for (auto [_, icon] : brand_icons)
            DestroyIcon(icon);
        for (auto [_, cursor] : reply_cursors)
            DestroyCursor(cursor);
        DeleteObject(brush);
        DeleteObject(field);
        DeleteObject(keyed_brush);
        DeleteObject(sidebar_brush);
        if (rich)
            FreeLibrary(rich);
    }
    int scale(int v, HWND h = nullptr) const {
        return MulDiv(v, int(GetDpiForWindow(h ? h : (main ? main : GetDesktopWindow()))), 96);
    }
    static bool has_cjk(std::string_view s) {
        auto chars = utf32(s);
        return std::any_of(chars.begin(), chars.end(), [](auto c) { return c >= 0x2e80; });
    }
    HFONT font_for(HWND parent) {
        return ui_font(14, false, parent);
    }
    void change_dpi(HWND window, UINT dpi, const RECT &bounds) {
        close_picker(false);
        UINT previous = window_dpi.contains(window) ? window_dpi[window] : GetDpiForSystem();
        window_dpi[window] = dpi;
        SetWindowPos(window, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                     bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (window == bubble) {
            update_bubble();
            return;
        }
        if (window == main) {
            apply();
            resize_main();
            SendMessageW(gear, WM_SETFONT, reinterpret_cast<WPARAM>(font_for(main)), TRUE);
            return;
        }
        if (window == provider_advanced || window == model_window || window == block_window) {
            for (HWND h = GetWindow(window, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font(13, false, window, true)), TRUE);
            if (window == model_window) layout_model_window();
            else if (window == block_window) layout_block_window();
            else layout_provider_advanced();
            return;
        }
        if (window == settings) {
            for (HWND child = GetWindow(settings, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font(13, false, settings)), TRUE);
            for (const auto &[h, p] : body_positions)
                SendMessageW(
                    h, WM_SETFONT,
                    reinterpret_cast<WPARAM>(ui_font(muted_controls.contains(h) ? 12 : 13, false, body)),
                    TRUE);
            SendMessageW(brand, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font(18, true, settings)), TRUE);
            SendMessageW(page_title, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font(22, true, settings)), TRUE);
            resize_settings();
            return;
        }
        for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            RECT r{};
            GetWindowRect(child, &r);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&r), 2);
            MoveWindow(child, MulDiv(r.left, int(dpi), int(previous)), MulDiv(r.top, int(dpi), int(previous)),
                       MulDiv(r.right - r.left, int(dpi), int(previous)),
                       MulDiv(r.bottom - r.top, int(dpi), int(previous)), TRUE);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_for(window)), TRUE);
        }
        if (window == settings)
            update_account_view();
    }
    HWND control(HWND parent, const wchar_t *type, std::string_view label, int x, int y, int w, int h,
                 DWORD style, int id = 0) {
        if (wcscmp(type, L"BUTTON") == 0 && (style & BS_TYPEMASK) == BS_PUSHBUTTON)
            style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
        if (wcscmp(type, L"BUTTON") == 0 && (style & BS_TYPEMASK) == BS_AUTOCHECKBOX)
            style |= WS_TABSTOP;
        if (wcscmp(type, L"EDIT") == 0 && (style & ES_MULTILINE))
            style |= ES_WANTRETURN;
        auto value = wide((wcscmp(type, L"STATIC") == 0 || wcscmp(type, L"BUTTON") == 0)
                              ? ui_text(label) : std::string(label));
        HWND handle{};
        std::optional<widgets::Kind> kind;
        if (wcscmp(type, L"BUTTON") == 0 && (style & BS_TYPEMASK) == BS_AUTOCHECKBOX)
            kind = widgets::Kind::Toggle;
        else if (_wcsicmp(type, TRACKBAR_CLASSW) == 0)
            kind = widgets::Kind::Slider;
        else if (_wcsicmp(type, L"COMBOBOX") == 0 && (style & 3) == CBS_DROPDOWNLIST)
            kind = widgets::Kind::Choice;
        else if (_wcsicmp(type, HOTKEY_CLASSW) == 0)
            kind = widgets::Kind::Hotkey;
        else if (_wcsicmp(type, L"STATIC") == 0)
            kind = widgets::Kind::Label;
        if (kind) {
            auto chosen = *kind;
            int height =
                chosen == widgets::Kind::Choice ? (parent == body || parent == settings ? 28 : 24) : h;
            handle = widgets::create(
                instance, parent, value.c_str(), scale(x, parent), scale(y, parent), scale(w, parent),
                scale(height, parent), style, id, chosen,
                [this, chosen](HWND window, HDC dc) {
                    switch (chosen) {
                    case widgets::Kind::Toggle:
                        paint_toggle(window, dc);
                        break;
                    case widgets::Kind::Slider:
                        paint_slider(window, dc);
                        break;
                    case widgets::Kind::Choice:
                        paint_selector(window, dc);
                        break;
                    case widgets::Kind::Hotkey:
                        paint_hotkey(window, dc);
                        break;
                    case widgets::Kind::Label:
                        paint_label(window, dc);
                        break;
                    }
                },
                [this, chosen](HWND window, UINT msg, WPARAM a, LPARAM b) -> std::optional<LRESULT> {
                    if (msg == WM_SETFOCUS)
                        ensure_setting_visible(window);
                    if (chosen == widgets::Kind::Choice)
                        return choice_input(window, msg, a, b);
                    if (chosen == widgets::Kind::Hotkey) {
                        if (msg == WM_SETFOCUS)
                            UnregisterHotKey(main, 1);
                        else if (msg == WM_KILLFOCUS && !register_hotkey(config.at("hotkey")))
                            status_note = "Failed to restore output shortcut. Please re-save it in settings.";
                    }
                    return std::nullopt;
                },
                [this](const char *error) { status_note = std::string("UI rendering failed:") + error; },
                [this](HWND window) { return slider_layout(window); });
        } else
            handle = CreateWindowExW(0, type, value.c_str(), WS_CHILD | WS_VISIBLE | style, scale(x, parent),
                                     scale(y, parent), scale(w, parent), scale(h, parent), parent,
                                     reinterpret_cast<HMENU>(INT_PTR(id)), instance, nullptr);
        if (!handle)
            throw std::runtime_error("Failed to create UI control");
        if (wcscmp(type, L"STATIC") == 0 || wcscmp(type, L"BUTTON") == 0)
            remember_caption(handle, label);
        SendMessageW(handle, WM_SETFONT,
                     reinterpret_cast<WPARAM>(parent == settings || parent == body
                                                  ? ui_font(13, false, parent, has_cjk(label))
                                                  : ui_font(14, false, parent, has_cjk(label))),
                     TRUE);
        if (!kind)
            SetWindowSubclass(
                handle,
                [](HWND window, UINT msg, WPARAM a, LPARAM b, UINT_PTR, DWORD_PTR context) -> LRESULT {
                    if (msg == WM_SETFOCUS)
                        reinterpret_cast<App *>(context)->ensure_setting_visible(window);
                    if (msg == WM_NCDESTROY)
                        reinterpret_cast<App *>(context)->paint_buffers.erase(window);
                    return DefSubclassProc(window, msg, a, b);
                },
                11, reinterpret_cast<DWORD_PTR>(this));
        if (!kind && wcscmp(type, L"BUTTON") == 0)
            SetWindowSubclass(
                handle,
                [](HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) -> LRESULT {
                    auto self = reinterpret_cast<App *>(data);
                    if (msg == WM_MOUSEMOVE && self->hovered_button != h) {
                        auto old = self->hovered_button;
                        self->hovered_button = h;
                        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0};
                        TrackMouseEvent(&track);
                        if (IsWindow(old))
                            InvalidateRect(old, nullptr, FALSE);
                        InvalidateRect(h, nullptr, FALSE);
                    } else if ((msg == WM_MOUSELEAVE || msg == WM_NCDESTROY) && self->hovered_button == h) {
                        self->hovered_button = nullptr;
                        if (msg != WM_NCDESTROY)
                            InvalidateRect(h, nullptr, FALSE);
                    }
                    return DefSubclassProc(h, msg, w, l);
                },
                12, reinterpret_cast<DWORD_PTR>(this));
        if (wcscmp(type, L"EDIT") == 0 && (style & ES_MULTILINE)) {
            auto decoration = std::make_unique<widgets::EditScrollbar>(instance, handle);
            decoration->configure(beveled() || parent == provider_advanced || parent == ocr_window,
                                  rgb(active_theme.colors.track), rgb(active_theme.colors.thumb),
                                  rgb(active_theme.colors.border));
            edit_scrollbars.push_back(std::move(decoration));
        }
        return handle;
    }
    HWND page_control(int page, const wchar_t *type, std::string_view label, int x, int y, int w, int h,
                      DWORD style, int id = 0) {
        auto c = control(body, type, label, x, y - 40, w, h, style, id);
        body_positions[c] = body_seeds[c] = {x, y - 40, w, widgets::kind(c) == widgets::Kind::Choice ? 28 : h};
        pages[page].push_back(c);
        if (settings_sections[page].empty())
            settings_sections[page].emplace_back();
        settings_sections[page].back().push_back(c);
        return c;
    }
    void settings_section(int page) { settings_sections[page].emplace_back(); }
    HWND label(int p, std::string_view value, int x, int y, int w = 600) {
        auto h = page_control(p, L"STATIC", value, x, y, w, 22, SS_LEFT);
        if (utf32(value).size() > 28) {
            muted_controls.insert(h);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font(12, false, body)), TRUE);
        }
        return h;
    }
    void checkbox(int p, const std::string &key, std::string_view label_text, int x, int y, int width = 285) {
        auto h = page_control(p, L"BUTTON", label_text, x, y, width, 24, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(h, BM_SETCHECK, config.value(key, false) ? BST_CHECKED : BST_UNCHECKED, 0);
        checks.push_back({h, key});
    }
    HWND edit(int p, const std::string &key, int x, int y, int w, int h = 27, bool multi = false) {
        auto c = page_control(p, L"EDIT", config.value(key, std::string()), x, y, w, h,
                              WS_BORDER | WS_TABSTOP |
                                  (multi ? ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL : ES_AUTOHSCROLL));
        SendMessageW(c, EM_SETLIMITTEXT, multi ? 24000 : 4096, 0);
        edits.push_back({c, key});
        return c;
    }
    HFONT ui_font(int size, bool semibold, HWND parent, bool chinese = false) {
        UINT dpi = GetDpiForWindow(parent);
        if (!dpi)
            dpi = GetDpiForSystem();
        window_dpi.try_emplace(parent, dpi);
        size += active_theme.fonts.size - 13;
        const auto language = ui_language();
        const bool mono = active_theme.fonts.monospace && !chinese;
        const bool serif = active_theme.fonts.serif;
        const int script = !chinese && (serif || mono) ? 0 :
                           language == "ja" ? 1 : language == "ko" ? 2 : language == "zh" || chinese ? 3 : 0;
        auto &f = ui_fonts[{dpi, size * 128 + script * 16 + int(serif) * 4 + int(semibold) * 2 + int(mono)}];
        if (!f) {
            const wchar_t *face = mono ? L"Consolas" : serif ?
                (script == 1 ? L"Yu Mincho" : script == 2 ? L"Batang" : script == 3 ? L"SimSun" : L"Georgia") :
                (script == 1 ? L"Yu Gothic UI" : script == 2 ? L"Malgun Gothic" : script == 3 ? L"Microsoft YaHei UI" : L"Segoe UI");
            f = CreateFontW(-MulDiv(size, int(dpi), 96), 0, 0, 0, semibold ? FW_SEMIBOLD : FW_NORMAL, FALSE,
                            FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
        }
        return f;
    }
    void draw_brand_icon(HDC dc, RECT bounds, bool pressed) {
        const int size = std::max(1, int(std::min(bounds.right - bounds.left, bounds.bottom - bounds.top)));
        auto &icon = brand_icons[size];
        if (!icon)
            icon = reinterpret_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(102), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
        if (!icon) throw std::runtime_error("Could not load the BianLingo icon");
        int x = (bounds.left + bounds.right - size) / 2, y = (bounds.top + bounds.bottom - size) / 2;
        if (pressed) { x += scale(1, gear); y += scale(1, gear); }
        DrawIconEx(dc, x, y, icon, size, size, 0, nullptr, DI_NORMAL);
    }
    std::vector<Rect> group_cards() const {
        return settings_cards[active_page];
    }
    bool in_card(HWND control) const {
        auto it = body_positions.find(control);
        if (it == body_positions.end())
            return false;
        const auto &r = it->second;
        int x = r.x + 5, y = r.y + 10;
        for (auto card : group_cards())
            if (x >= card.x && x < card.x + card.w && y >= card.y && y < card.y + card.h)
                return true;
        return false;
    }
    COLORREF sidebar_color() const {
        return mix(bg, panel);
    }
    bool beveled() const {
        return active_theme.widgets.frame == FrameVisual::Beveled;
    }
    void paint_beveled_settings(HDC dc) {
        RECT r{};
        GetClientRect(settings, &r);
        const int u = scale(1, settings);
        const auto rim = rgb(active_theme.colors.border);
        widgets::paint::frame(dc, {u, u, r.right - u, r.bottom - u}, bg, rim, u, false, false, false);
        RECT title{scale(5, settings), scale(5, settings), r.right - scale(5, settings), scale(31, settings)};
        widgets::paint::gradient(dc, title, mix(panel, rim), panel);
        widgets::paint::edge(dc, title, mix(rim, fg), RGB(4, 5, 6), u);
        auto old = SelectObject(dc, ui_font(16, false, settings, true));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, fg);
        DrawTextW(dc, wide(ui_text("BianLingo · Options")).c_str(), -1, &title, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        widgets::paint::frame(
            dc,
            {scale(12, settings), scale(50, settings), scale(198, settings), r.bottom - scale(58, settings)},
            panel, rim, u, true, false, false);
        widgets::paint::frame(dc,
                              {scale(208, settings), scale(50, settings), r.right - scale(12, settings),
                               r.bottom - scale(58, settings)},
                              bg, rim, u, true, false, false);
        SelectObject(dc, ui_font(13, false, settings, true));
        for (auto [y, name] : {std::pair{settings_category_y[0], "Chat & display"},
                               {settings_category_y[1], "Translate"}, {settings_category_y[2], "Manage"}}) {
            RECT bar{scale(18, settings), scale(y, settings), scale(192, settings), scale(y + 27, settings)};
            widgets::paint::frame(dc, bar, RGB(12, 13, 14), rim, u, false);
            bar.left += scale(12, settings);
            SetTextColor(dc, RGB(239, 234, 220));
            DrawTextW(dc, wide(ui_text(name)).c_str(), -1, &bar, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        RECT separator{scale(228, settings), scale(108, settings), r.right - scale(28, settings),
                       scale(108, settings) + 1};
        widgets::paint::solid(dc, separator, mix(panel, rim));
        SelectObject(dc, old);
    }
    void paint_beveled_background(HWND h, HDC dc) {
        RECT bounds{};
        GetClientRect(h, &bounds);
        FillRect(dc, &bounds, brush);
        HWND parent = GetParent(h);
        POINT offset{};
        if (h == settings || parent == settings) {
            if (h != settings)
                MapWindowPoints(h, settings, &offset, 1);
            auto state = SaveDC(dc);
            SetViewportOrgEx(dc, -offset.x, -offset.y, nullptr);
            paint_beveled_settings(dc);
            RestoreDC(dc, state);
        } else if (h == body || parent == body) {
            if (h != body)
                MapWindowPoints(h, body, &offset, 1);
            for (auto card : group_cards()) {
                int y = scale(card.y + card.h + 5, body) - body_scroll - offset.y;
                RECT line{scale(20, body) - offset.x, y, scale(626, body) - offset.x, y + 1};
                widgets::paint::solid(dc, line, mix(bg, rgb(active_theme.colors.border)));
            }
        }
    }
    void paint_control_background(HWND h, HDC dc) {
        if (h == gear) {
            RECT bounds{};
            GetClientRect(h, &bounds);
            FillRect(dc, &bounds, keyed_brush);
            return;
        }
        if (beveled()) {
            paint_beveled_background(h, dc);
            return;
        }
        RECT bounds{};
        GetClientRect(h, &bounds);
        FillRect(dc, &bounds, brush);
        HWND parent = GetParent(h);
        POINT offset{};
        if (h == settings || parent == settings) {
            if (h != settings)
                MapWindowPoints(h, settings, &offset, 1);
            RECT side{-offset.x, -offset.y, scale(198, settings) - offset.x, bounds.bottom};
            RECT clipped{};
            if (IntersectRect(&clipped, &bounds, &side))
                FillRect(dc, &clipped, sidebar_brush);
        }
        if (h == body || parent == body) {
            offset = {};
            if (h != body)
                MapWindowPoints(h, body, &offset, 1);
            for (auto card : group_cards()) {
                RECT shape{scale(card.x, body) - offset.x, scale(card.y, body) - body_scroll - offset.y,
                           scale(card.x + card.w, body) - offset.x,
                           scale(card.y + card.h, body) - body_scroll - offset.y};
                RECT clipped{};
                if (IntersectRect(&clipped, &shape, &bounds))
                    rounded(dc, shape, panel, float(scale(active_theme.metrics.card_radius, body)),
                            active_theme.widgets.outline_cards ? rgb(active_theme.colors.border)
                                                               : CLR_INVALID,
                            active_theme.metrics.border_width);
            }
        }
    }
    void paint_beveled_button(const DRAWITEMSTRUCT &item, HDC dc) {
        const auto &c = active_theme.colors;
        int id = GetDlgCtrlID(item.hwndItem);
        bool nav = id >= id_nav0 && id < id_nav0 + int(navigation.size());
        bool selected = nav && id - id_nav0 == active_page;
        bool down = item.itemState & ODS_SELECTED, enabled = IsWindowEnabled(item.hwndItem) != FALSE;
        bool hover = hovered_button == item.hwndItem;
        paint_control_background(item.hwndItem, dc);
        RECT box = item.rcItem;
        InflateRect(&box, -1, -1);
        if (nav) {
            if (selected || hover) {
                widgets::paint::gradient(dc, box, rgb(c.selection), panel);
                auto edge = box;
                edge.right = edge.left + scale(2, item.hwndItem);
                widgets::paint::solid(dc, edge, selected ? fg : rgb(c.border));
            }
        } else {
            auto fill = !enabled ? panel
                        : down   ? rgb(c.pressed)
                        : hover  ? mix(rgb(c.button), rgb(c.pressed))
                                 : rgb(c.button);
            widgets::paint::frame(dc, box, fill,
                                  rgb(hover || GetFocus() == item.hwndItem ? c.focus : c.border),
                                  scale(1, item.hwndItem), down, hover);
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, !enabled ? rgb(c.disabled) : selected ? rgb(c.selected_text) : fg);
        auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)));
        auto caption = wide(text(item.hwndItem));
        if (nav)
            box.left += scale(12, item.hwndItem);
        if (down)
            OffsetRect(&box, scale(1, item.hwndItem), scale(1, item.hwndItem));
        draw_wrapped_caption(dc, caption, box, nav ? DT_LEFT : DT_CENTER,
                             scale(nav ? 4 : 10, item.hwndItem));
        SelectObject(dc, old);
        int color_index = id - id_skin_color0;
        if (color_index >= 0 && color_index < 4) {
            RECT swatch = item.rcItem;
            swatch.left = swatch.right - scale(18, item.hwndItem);
            InflateRect(&swatch, -3, -5);
            widgets::paint::solid(dc, swatch, draft_color(color_index));
        }
        if (item.itemState & ODS_FOCUS) {
            auto focus = item.rcItem;
            InflateRect(&focus, -scale(4, item.hwndItem), -scale(4, item.hwndItem));
            DrawFocusRect(dc, &focus);
        }
    }
    void paint_settings(HDC dc) {
        paint_control_background(settings, dc);
    }
    void paint_body(HDC dc) {
        paint_control_background(body, dc);
    }
    void paint_window(HWND h, HDC dc) {
        if (h == provider_advanced || h == ocr_window) {
            RECT r{};
            GetClientRect(h, &r);
            FillRect(dc, &r, brush);
            return;
        }
        if (h == settings) {
            paint_settings(dc);
            return;
        }
        if (h == body) {
            paint_body(dc);
            return;
        }
        if (h == body_scrollbar) {
            RECT r{};
            GetClientRect(h, &r);
            FillRect(dc, &r, brush);
            auto thumb = body_thumb();
            auto b = reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH));
            SetDCBrushColor(dc, mix(fg, panel));
            FillRect(dc, &thumb, b);
            return;
        }
        if (h == scrollbar) {
            RECT bounds{};
            GetClientRect(h, &bounds);
            FillRect(dc, &bounds, field);
            auto thumb = scroll_thumb(scroll_state());
            auto thumb_brush = reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH));
            SetDCBrushColor(dc, accent);
            FillRect(dc, &thumb, thumb_brush);
            return;
        }
        if (h == bubble) {
            RECT bounds{};
            GetClientRect(h, &bounds);
            FillRect(dc, &bounds, field);
            InflateRect(&bounds, -bubble_unit(18), -bubble_unit(11));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, reply.phase == QuickReply::Error ? RGB(246, 174, 112) : accent);
            auto old = SelectObject(dc, bubble_font());
            DrawTextW(dc, bubble_caption.c_str(), -1, &bounds, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, old);
            return;
        }
        RECT r{};
        GetClientRect(h, &r);
        FillRect(dc, &r, brush);
    }
    void paint_hotkey(HWND h, HDC dc) {
        paint_control_background(h, dc);
        RECT r{};
        GetClientRect(h, &r);
        RECT field_rect = r;
        InflateRect(&field_rect, -1, -1);
        if (beveled())
            widgets::paint::frame(
                dc, field_rect, panel,
                rgb(GetFocus() == h ? active_theme.colors.focus : active_theme.colors.border), scale(1, h),
                true);
        else
            rounded(dc, field_rect, panel, float(scale(active_theme.metrics.control_radius, h)),
                    rgb(GetFocus() == h ? active_theme.colors.focus : active_theme.colors.border),
                    active_theme.metrics.border_width);
        WORD key = WORD(SendMessageW(h, HKM_GETHOTKEY, 0, 0));
        int mods = HIBYTE(key);
        std::wstring name;
        if (mods & HOTKEYF_CONTROL)
            name += L"Ctrl + ";
        if (mods & HOTKEYF_ALT)
            name += L"Alt + ";
        if (mods & HOTKEYF_SHIFT)
            name += L"Shift + ";
        if (LOBYTE(key) == VK_OEM_3)
            name += L"· / `";
        else if (LOBYTE(key)) {
            wchar_t key_name[128]{};
            GetKeyNameTextW(LONG(MapVirtualKeyW(LOBYTE(key), MAPVK_VK_TO_VSC) << 16) |
                                ((mods & HOTKEYF_EXT) ? 1 << 24 : 0),
                            key_name, 128);
            name += key_name;
        } else
            name = wide(ui_text("Press the shortcut"));
        auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, fg);
        r.left += scale(10, h);
        r.right -= scale(6, h);
        DrawTextW(dc, name.c_str(), -1, &r, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, old);
    }
    void paint_label(HWND h, HDC dc) {
        paint_control_background(h, dc);
        SendMessageW(GetParent(h), WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc),
                     reinterpret_cast<LPARAM>(h));
        SetBkMode(dc, TRANSPARENT);
        auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)));
        RECT r{};
        GetClientRect(h, &r);
        auto value = wide(text(h));
        auto type = GetWindowLongPtrW(h, GWL_STYLE) & SS_TYPEMASK;
        UINT flags = DT_NOPREFIX | DT_WORDBREAK |
                     (type == SS_RIGHT    ? DT_RIGHT
                      : type == SS_CENTER ? DT_CENTER
                                          : DT_LEFT);
        DrawTextW(dc, value.c_str(), -1, &r, flags);
        SelectObject(dc, old);
    }
    void fill_interface_languages() {
        if (!interface_language) return;
        SendMessageW(interface_language, CB_RESETCONTENT, 0, 0);
        SendMessageW(interface_language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(ui_text("Follow system")).c_str()));
        int selected = 0;
        const auto preference = config.value("ui_language", "system");
        for (size_t i = 0; i < ui_languages.size(); ++i) {
            SendMessageW(interface_language, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(wide(ui_languages[i].native_name).c_str()));
            if (preference == ui_languages[i].code) selected = int(i + 1);
        }
        SendMessageW(interface_language, CB_SETCURSEL, selected, 0);
    }
    void refresh_ui_language(const std::string &previous) {
        close_picker(false);
        // Snapshot: set() may remove dynamic captions from the registry.
        const auto captions = ui_caption_sources;
        for (const auto &[window, source] : captions) if (IsWindow(window)) set(window, source);
        const auto dictionary = bundled_json("ui-locales.json");
        std::vector<HWND> choices;
        for (auto parent : {settings, output, provider_advanced, model_window, ocr_window}) {
            if (!parent) continue;
            EnumChildWindows(parent, [](HWND h, LPARAM context) -> BOOL {
                if (widgets::kind(h) == widgets::Kind::Choice)
                    reinterpret_cast<std::vector<HWND> *>(context)->push_back(h);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&choices));
        }
        for (auto combo : choices) {
            // User-chosen connection and tab names must remain verbatim.
            if (combo == interface_language || combo == provider_select || combo == tab_editor) continue;
            const auto selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
            if (combo == chat_target) {
                SendMessageW(combo, CB_RESETCONTENT, 0, 0);
                for (auto code : chat_targets) {
                    const auto caption = wide(ui_text(target_language_name(code)));
                    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(caption.c_str()));
                }
                SendMessageW(combo, CB_SETCURSEL, selected, 0);
                continue;
            }
            const auto count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
            std::vector<std::wstring> items;
            for (int i = 0; i < count; ++i) {
                auto caption = narrow(picker_text(combo, i));
                for (auto entry = dictionary.begin(); entry != dictionary.end(); ++entry) {
                    if (caption == ui_text_for_language(entry.key(), previous)) {
                        caption = ui_text(entry.key());
                        break;
                    }
                }
                items.push_back(wide(caption));
            }
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            for (const auto &item : items) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
            SendMessageW(combo, CB_SETCURSEL, selected, 0);
        }
#ifdef BL_UI_TESTS
        if (chat_target)
            for (size_t i = 0; i < chat_targets.size(); ++i)
                if (narrow(picker_text(chat_target, int(i))) != ui_text(target_language_name(chat_targets[i])))
                    throw std::runtime_error("Target-language captions did not refresh with the UI language");
#endif
        fill_interface_languages();
        fill_design_choices(config);
        fill_skin_choices(config);
        if (settings) set(settings, "BianLingo · Settings");
        if (output) set(output, "BianLingo · Output translation");
        if (model_window) set(model_window, model_ct2 ? "CTranslate2 language packs" : "Bergamot language packs");
        if (block_window) set(block_window, "Blocked players");
        if (ocr_window) set(ocr_window, "Local OCR settings");
        if (provider_advanced) set(provider_advanced, "Connection · Advanced settings");
        if (notices) set(notices, "BianLingo · About & component notices");
        // Only exact built-in presets were changed by Engine::configure.
        for (const auto &edit : edits)
            if (edit.key == "incoming_prompt" || edit.key == "outbound_template" || edit.key == "outbound_default_template")
                set(edit.h, config.at(edit.key).get<std::string>());
        refresh_interface_fonts();
        if (settings) {
            set(page_description, page_descriptions[active_page]);
            resize_settings();
            RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        }
    }
    void refresh_interface_fonts() {
        if (page_title)
            set(page_title, (active_theme.widgets.plain_navigation && !beveled() ? "~/ " : "") +
                                std::string(page_titles[active_page]));
        if (brand_detail)
            set(brand_detail, active_theme.widgets.plain_navigation && !beveled() ? "translate / play"
                                                                                  : "Bring communication back into the game");
        for (auto parent : {settings, body, output, notices, provider_advanced, block_window, model_window, ocr_window}) {
            if (!parent)
                continue;
            for (HWND h = GetWindow(parent, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
                if (h == body || h == body_scrollbar)
                    continue;
                int size = h == balance_amount          ? 22
                           : h == brand                 ? 18
                           : h == page_title            ? 22
                           : muted_controls.contains(h) ? 12
                           : parent == output           ? 14
                                                        : 13;
                auto next = ui_font(size, h == brand || h == page_title || h == balance_amount, parent,
                                    has_cjk(text(h)));
                if (reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)) != next)
                    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(next), FALSE);
            }
        }
        if (block_window)
            layout_block_window();
    }
    std::wstring picker_text(HWND combo, int index) {
        int n = int(SendMessageW(combo, CB_GETLBTEXTLEN, index, 0));
        if (n < 0 || n > 4096)
            return {};
        std::wstring value(size_t(n) + 1, L'\0');
        SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(value.data()));
        value.resize(n);
        return value;
    }
    void close_picker(bool accept) {
        auto window = picker, combo = picker_combo;
        int index = picker_hover;
        picker = nullptr;
        picker_combo = nullptr;
        if (window) {
            if (GetCapture() == window)
                ReleaseCapture();
            paint_buffers.erase(window);
            DestroyWindow(window);
        }
        if (combo)
            InvalidateRect(combo, nullptr, FALSE);
        if (accept && combo && index >= 0) {
            SendMessageW(combo, CB_SETCURSEL, index, 0);
            auto parent = GetParent(combo);
            auto id = GetDlgCtrlID(combo);
            SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
            SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, CBN_SELENDOK), reinterpret_cast<LPARAM>(combo));
        }
    }
    void open_picker(HWND combo) {
        close_picker(false);
        int count = int(SendMessageW(combo, CB_GETCOUNT, 0, 0));
        if (count <= 0)
            return;
        SetFocus(combo);
        RECT anchor{};
        GetWindowRect(combo, &anchor);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(combo, MONITOR_DEFAULTTONEAREST), &monitor);
        picker_combo = combo;
        picker_hover = int(SendMessageW(combo, CB_GETCURSEL, 0, 0));
        if (picker_hover < 0)
            picker_hover = 0;
        picker_row_height = scale(active_theme.metrics.popup_row_height, combo);
        int pad = scale(6, combo),
            available = std::max(anchor.top - monitor.rcWork.top, monitor.rcWork.bottom - anchor.bottom) -
                        scale(8, combo);
        picker_rows = std::clamp((available - pad * 2) / picker_row_height, 1, std::min(count, 9));
        picker_first = std::clamp(picker_hover - picker_rows / 2, 0, count - picker_rows);
        picker_armed = false;
        int width = std::max(int(anchor.right - anchor.left), scale(180, combo)),
            height = picker_rows * picker_row_height + pad * 2;
        int x = std::clamp(int(anchor.left), int(monitor.rcWork.left), int(monitor.rcWork.right) - width);
        int y = anchor.bottom + scale(4, combo);
        if (y + height > monitor.rcWork.bottom)
            y = anchor.top - height - scale(4, combo);
        picker = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                 L"BianLingo.Window", L"", WS_POPUP, x, y, width, height,
                                 GetAncestor(combo, GA_ROOT), nullptr, instance, nullptr);
        if (!picker) {
            picker_combo = nullptr;
            return;
        }
        if (active_theme.metrics.control_radius > 0)
            SetWindowRgn(picker,
                         CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                            scale(2 * active_theme.metrics.control_radius, combo),
                                            scale(2 * active_theme.metrics.control_radius, combo)),
                         FALSE);
        ShowWindow(picker, SW_SHOWNOACTIVATE);
        SetCapture(picker);
        InvalidateRect(combo, nullptr, FALSE);
    }
    void move_picker(int index) {
        if (!picker)
            return;
        int count = int(SendMessageW(picker_combo, CB_GETCOUNT, 0, 0));
        auto next = std::clamp(index, 0, count - 1);
        if (next == picker_hover && next >= picker_first && next < picker_first + picker_rows)
            return;
        picker_hover = next;
        if (picker_hover < picker_first)
            picker_first = picker_hover;
        if (picker_hover >= picker_first + picker_rows)
            picker_first = picker_hover - picker_rows + 1;
        InvalidateRect(picker, nullptr, FALSE);
    }
    void paint_picker(HDC dc) {
        RECT r{};
        GetClientRect(picker, &r);
        FillRect(dc, &r, field);
        if (beveled())
            widgets::paint::frame(dc, r, panel, rgb(active_theme.colors.border), scale(1, picker), true,
                                  false, false);
        else
            rounded(dc, r, panel, float(scale(active_theme.metrics.control_radius, picker)),
                    rgb(active_theme.colors.border), active_theme.metrics.border_width);
        int pad = scale(6, picker), selected = int(SendMessageW(picker_combo, CB_GETCURSEL, 0, 0));
        auto old = SelectObject(dc, ui_font(13, false, picker_combo, true));
        SetBkMode(dc, TRANSPARENT);
        for (int i = 0; i < picker_rows; ++i) {
            int index = picker_first + i;
            RECT row{pad, pad + i * picker_row_height, r.right - pad, pad + (i + 1) * picker_row_height};
            bool hover = index == picker_hover;
            if (hover)
                rounded(dc, row, rgb(active_theme.colors.selection),
                        float(scale(active_theme.metrics.control_radius, picker)));
            SetTextColor(dc, hover ? rgb(active_theme.colors.selected_text) : fg);
            auto label = picker_text(picker_combo, index);
            row.left += scale(28, picker);
            row.right -= scale(12, picker);
            DrawTextW(dc, label.c_str(), -1, &row,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (index == selected) {
                row.right = row.left - scale(5, picker);
                row.left = pad + scale(5, picker);
                DrawTextW(dc, active_theme.widgets.chevron_mark ? L">" : L"✓", -1, &row,
                          DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            }
        }
        int count = int(SendMessageW(picker_combo, CB_GETCOUNT, 0, 0));
        if (count > picker_rows) {
            int height = r.bottom - 2 * pad;
            RECT bar{r.right - scale(4, picker), pad + height * picker_first / count,
                     r.right - scale(2, picker), pad + height * (picker_first + picker_rows) / count};
            rounded(dc, bar, mix(fg, panel), 1);
        }
        SelectObject(dc, old);
    }
    LRESULT picker_input(UINT msg, WPARAM w, LPARAM l) {
        if (msg == WM_PAINT) {
            PAINTSTRUCT p{};
            auto dc = BeginPaint(picker, &p);
            buffered(picker, dc, [this](HWND, HDC memory) { paint_picker(memory); });
            EndPaint(picker, &p);
            return 0;
        }
        if (msg == WM_PRINTCLIENT || msg == WM_PRINT) {
            buffered(picker, reinterpret_cast<HDC>(w), [this](HWND, HDC memory) { paint_picker(memory); });
            return 0;
        }
        if (msg == WM_ERASEBKGND)
            return 1;
        if (msg == WM_MOUSEWHEEL) {
            int count = int(SendMessageW(picker_combo, CB_GETCOUNT, 0, 0));
            picker_first = std::clamp(picker_first - (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1) * 3, 0,
                                      count - picker_rows);
            picker_hover = std::clamp(picker_hover, picker_first, picker_first + picker_rows - 1);
            InvalidateRect(picker, nullptr, FALSE);
            return 0;
        }
        if (msg == WM_CAPTURECHANGED) {
            close_picker(false);
            return 0;
        }
        if (msg == WM_LBUTTONDOWN || msg == WM_MOUSEMOVE || msg == WM_LBUTTONUP) {
            RECT r{};
            GetClientRect(picker, &r);
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            bool inside = PtInRect(&r, p);
            int row = (p.y - scale(6, picker)) / picker_row_height;
            bool item = inside && p.y >= scale(6, picker) && row >= 0 && row < picker_rows;
            if (msg == WM_LBUTTONDOWN) {
                if (!inside) {
                    close_picker(false);
                    return 0;
                }
                picker_armed = true;
            }
            if (item) {
                auto next = picker_first + row;
                if (picker_hover != next) {
                    picker_hover = next;
                    InvalidateRect(picker, nullptr, FALSE);
                }
                if (msg == WM_MOUSEMOVE && (w & MK_LBUTTON))
                    picker_armed = true;
            }
            if (msg == WM_LBUTTONUP && picker_armed)
                close_picker(item);
            return 0;
        }
        return DefWindowProcW(picker, msg, w, l);
    }
    void paint_selector(HWND h, HDC dc) {
        RECT r{};
        GetClientRect(h, &r);
        paint_control_background(h, dc);
        bool focus = GetFocus() == h || picker_combo == h;
        RECT shape = r;
        InflateRect(&shape, -1, -1);
        if (beveled())
            widgets::paint::frame(dc, shape, panel,
                                  rgb(focus ? active_theme.colors.focus : active_theme.colors.border),
                                  scale(1, h), true, focus);
        else
            rounded(dc, shape,
                    rgb(widgets::hovered(h) ? active_theme.colors.hover : active_theme.colors.button),
                    float(scale(active_theme.metrics.control_radius, h)),
                    rgb(focus ? active_theme.colors.focus : active_theme.colors.border),
                    active_theme.metrics.border_width);
        auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, fg);
        auto label = picker_text(h, int(SendMessageW(h, CB_GETCURSEL, 0, 0)));
        SelectObject(dc, ui_font(GetParent(h) == output ? 14 : 13, false, h, has_cjk(narrow(label))));
        r.left += scale(10, h);
        r.right -= scale(24, h);
        DrawTextW(dc, label.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        r.left = r.right;
        r.right += scale(18, h);
        SetTextColor(dc, accent);
        if (beveled()) {
            int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, u = scale(4, h);
            auto pen = SelectObject(dc, GetStockObject(NULL_PEN));
            auto old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
            SetDCBrushColor(dc, fg);
            POINT arrow[] = {{cx - u, cy - u / 2}, {cx + u, cy - u / 2}, {cx, cy + u}};
            Polygon(dc, arrow, 3);
            SelectObject(dc, old_brush);
            SelectObject(dc, pen);
        } else
            DrawTextW(dc, active_theme.widgets.chevron_mark ? L"v" : L"⌄", -1, &r,
                      DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        SelectObject(dc, old);
    }
    std::optional<LRESULT> choice_input(HWND h, UINT msg, WPARAM w, LPARAM l) {
        if (msg == WM_GETDLGCODE && l) {
            auto event = reinterpret_cast<MSG *>(l);
            if (event->message == WM_KEYDOWN && event->wParam == VK_TAB && picker_combo == h)
                close_picker(false);
        }

        if (msg == CB_GETDROPPEDSTATE)
            return picker_combo == h;
        if (msg == CB_SHOWDROPDOWN) {
            if (w)
                open_picker(h);
            else
                close_picker(false);
            return 1;
        }
        if (msg == WM_LBUTTONDOWN) {
            if (picker_combo == h)
                close_picker(false);
            else
                open_picker(h);
            return 0;
        }
        if (msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK)
            return 0;
        if (msg == WM_SYSKEYDOWN && (w == VK_DOWN || w == VK_UP)) {
            if (picker_combo == h)
                close_picker(false);
            else
                open_picker(h);
            return 0;
        }
        if (msg == WM_KEYDOWN) {
            bool open = picker_combo == h;
            if (open) {
                switch (w) {
                case VK_F4:
                case VK_ESCAPE:
                    close_picker(false);
                    return 0;
                case VK_RETURN:
                case VK_SPACE:
                    close_picker(true);
                    return 0;
                case VK_UP:
                    move_picker(picker_hover - 1);
                    return 0;
                case VK_DOWN:
                    move_picker(picker_hover + 1);
                    return 0;
                case VK_HOME:
                    move_picker(0);
                    return 0;
                case VK_END:
                    move_picker(int(SendMessageW(h, CB_GETCOUNT, 0, 0)) - 1);
                    return 0;
                case VK_PRIOR:
                    move_picker(picker_hover - picker_rows);
                    return 0;
                case VK_NEXT:
                    move_picker(picker_hover + picker_rows);
                    return 0;
                case VK_TAB:
                    close_picker(false);
                    break;
                }
            } else if (w == VK_F4 || w == VK_SPACE || w == VK_RETURN || w == VK_DOWN || w == VK_UP) {
                open_picker(h);
                return 0;
            }
        }
        if (msg == WM_CHAR && (w == VK_RETURN || w == VK_SPACE || w == VK_ESCAPE))
            return 0;
        if (msg == WM_MOUSEWHEEL) {
            if (picker_combo != h)
                open_picker(h);
            if (picker)
                picker_input(msg, w, l);
            return 0;
        }
        if ((msg == WM_KILLFOCUS || msg == CB_RESETCONTENT || msg == WM_NCDESTROY) && picker_combo == h)
            close_picker(false);
        return std::nullopt;
    }
    void paint_toggle(HWND h, HDC dc) {
        RECT r{};
        GetClientRect(h, &r);
        paint_control_background(h, dc);
        bool on = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
        bool pressed = (SendMessageW(h, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0,
             enabled = IsWindowEnabled(h) != FALSE;
        const auto &m = active_theme.metrics;
        const auto &c = active_theme.colors;
        int x = scale(1, h), width = scale(m.toggle_width, h), height = scale(m.toggle_height, h),
            y = (r.bottom - height) / 2;
        COLORREF ink = rgb(!enabled              ? c.disabled
                           : pressed             ? c.pressed
                           : widgets::hovered(h) ? c.hover
                           : on                  ? active_skin.accent
                                                 : c.toggle_off);
        if (beveled()) {
            x = r.right - height - scale(4, h);
            RECT box{x, y, x + height, y + height};
            auto rim = rgb(!enabled                                 ? c.disabled
                           : widgets::hovered(h) || GetFocus() == h ? c.focus
                                                                    : c.border);
            widgets::paint::frame(dc, box, rgb(c.toggle_off), rim, scale(1, h), true, on);
            if (on)
                widgets::paint::tick(dc, box, enabled ? fg : rgb(c.disabled), scale(1, h));
            r.right = x - scale(12, h);
        } else if (active_theme.widgets.toggle == ToggleVisual::Ascii) {
            RECT box{x, y, x + width, y + height};
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, ink);
            auto old = SelectObject(dc, ui_font(14, false, h));
            DrawTextW(dc, on ? L"[x]" : L"[ ]", -1, &box, DT_SINGLELINE | DT_VCENTER);
            SelectObject(dc, old);
        } else if (active_theme.widgets.toggle == ToggleVisual::Checkbox) {
            RECT box{x, y, x + height, y + height};
            rounded(dc, box, on ? ink : panel, float(scale(m.control_radius, h)), ink, m.border_width);
            if (on) {
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, rgb(c.on_accent));
                auto old = SelectObject(dc, ui_font(13, true, h));
                DrawTextW(dc, L"✓", -1, &box, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                SelectObject(dc, old);
            }
        } else {
            RECT pill{x, y, x + width, y + height};
            rounded(dc, pill, ink, float(height) / 2);
            int inset = scale(2, h), diameter = height - 2 * inset,
                knob = x + (on ? width - inset - diameter : inset);
            RECT circle{knob, y + inset, knob + diameter, y + inset + diameter};
            rounded(dc, circle, rgb(c.thumb), float(diameter) / 2);
        }
        if (!beveled())
            r.left += width + scale(10, h);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, enabled ? fg : rgb(active_theme.colors.disabled));
        auto f = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)));
        auto caption = wide(text(h));
        draw_wrapped_caption(dc, caption, r, DT_LEFT);
        SelectObject(dc, f);
        if (GetFocus() == h)
            DrawFocusRect(dc, &r);
    }
    #include "windows_settings_layout.inc"
    void ensure_setting_visible(HWND h) {
        if (!body || GetParent(h) != body || !body_positions.contains(h) || !IsWindowVisible(h)) return;
        RECT viewport{};
        GetClientRect(body, &viewport);
        const auto &p = body_positions.at(h);
        const int top = scale(p.y, body), bottom = scale(p.y + p.h, body), margin = scale(8, body);
        int next = body_scroll;
        if (top < next + margin) next = top - margin;
        else if (bottom > next + viewport.bottom - margin) next = bottom - viewport.bottom + margin;
        next = std::clamp(next, 0, body_max_scroll);
        if (next != body_scroll) scroll_body(next - body_scroll);
    }
    void layout_body() {
        if (!body)
            return;
        RECT r{};
        GetClientRect(body, &r);
        reflow_settings_page(r.right);
        int bottom = 0;
        for (auto h : pages[active_page]) {
            const auto &p = body_positions.at(h);
            wchar_t type[64]{};
            GetClassNameW(h, type, 64);
            if (GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE)
                bottom = std::max(bottom, scale(p.y + p.h + 12, body));
        }
        for (auto card : group_cards())
            bottom = std::max(bottom, scale(card.y + card.h + 2, body));
        body_max_scroll = std::max(0, bottom - int(r.bottom));
        body_scroll = std::clamp(body_scroll, 0, body_max_scroll);
        for (auto h : pages[active_page]) {
            const auto &p = body_positions.at(h);
            MoveWindow(h, scale(p.x, body), scale(p.y, body) - body_scroll, scale(p.w, body),
                       scale(p.h, body), FALSE);
        }
        update_settings_scrollbar();
        RedrawWindow(body, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        InvalidateRect(body_scrollbar, nullptr, TRUE);
    }
    void scroll_body(int delta) {
        body_scroll_until = Clock::now() + std::chrono::milliseconds(1000);
        body_scroll = std::clamp(body_scroll + delta, 0, body_max_scroll);
        layout_body();
    }
    RECT body_thumb() const {
        RECT r{};
        GetClientRect(body_scrollbar, &r);
        int h = std::max(1, int(r.bottom));
        int thumb = std::max(scale(28, settings), int(int64_t(h) * h / (h + body_max_scroll)));
        thumb = std::min(thumb, h);
        int top = body_max_scroll ? int(int64_t(body_scroll) * (h - thumb) / body_max_scroll) : 0;
        return {0, top, r.right, top + thumb};
    }
    void drag_body(int y) {
        RECT r{};
        GetClientRect(body_scrollbar, &r);
        auto thumb = body_thumb();
        int room = r.bottom - (thumb.bottom - thumb.top);
        if (room > 0) {
            body_scroll = int(int64_t(std::clamp(y - body_drag_offset, 0, room)) * body_max_scroll / room);
            layout_body();
        }
    }
    void update_settings_scrollbar() {
        if (!body_scrollbar)
            return;
        bool show = false;
        if (settings && IsWindowVisible(settings) && body_max_scroll > 0) {
            POINT p{};
            RECT r{};
            GetWindowRect(body, &r);
            show = body_dragging || Clock::now() < body_scroll_until ||
                   (GetCursorPos(&p) && p.x >= r.right - scale(12, settings) &&
                    p.x <= r.right + scale(10, settings) && p.y >= r.top && p.y < r.bottom);
        }
        if (bool(IsWindowVisible(body_scrollbar)) != show)
            ShowWindow(body_scrollbar, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    void resize_settings() {
        if (!settings || !body)
            return;
        RECT r{};
        GetClientRect(settings, &r);
        settings_layout_frame = active_theme.widgets.frame;
        const bool metal = beveled();
        HDC measure_dc = GetDC(settings);
        const int body_x = 208;
        int x = scale(body_x, settings);
        int width = std::max(1, std::min(scale(650, settings), int(r.right) - x - scale(24, settings)));
        int description_width = std::max(1, MulDiv(width, 96, int(GetDpiForWindow(settings))) - 22);
        int description_height = std::max(22, settings_caption_height(page_description, description_width, measure_dc));
        int description_top = metal ? 92 : 59;
        int y = scale(std::max(metal ? 118 : 90, description_top + description_height + 14), settings);
        int height = std::max(1, int(r.bottom) - y - scale(64, settings));
        MoveWindow(body, x, y, width, height, TRUE);
        MoveWindow(body_scrollbar, x + width + scale(3, settings), y, scale(4, settings), height, TRUE);
        MoveWindow(page_title, x + scale(20, settings), scale(20, settings), width - scale(20, settings),
                   scale(32, settings), TRUE);
        MoveWindow(page_description, x + scale(22, settings), scale(58, settings),
                   width - scale(22, settings), scale(description_height, settings), TRUE);
        MoveWindow(GetDlgItem(settings, id_save), r.right - scale(166, settings),
                   r.bottom - scale(44, settings), scale(142, settings), scale(32, settings), TRUE);
        MoveWindow(GetDlgItem(settings, id_exit), scale(22, settings), r.bottom - scale(44, settings),
                   scale(156, settings), scale(32, settings), TRUE);
        if (settings_status)
            MoveWindow(settings_status, x + scale(20, settings), r.bottom - scale(50, settings),
                       std::max(1, int(r.right) - x - scale(208, settings)), scale(38, settings), TRUE);
        if (version_label)
            MoveWindow(version_label, scale(22, settings), r.bottom - scale(80, settings),
                       scale(165, settings), scale(20, settings), TRUE);
        MoveWindow(brand, scale(22, settings), scale(26, settings), scale(165, settings), scale(28, settings),
                   TRUE);
        int detail_height = std::max(22, settings_caption_height(brand_detail, 165, measure_dc));
        MoveWindow(brand_detail, scale(22, settings), scale(58, settings), scale(165, settings),
                   scale(detail_height, settings), TRUE);
        auto brand_caption = wide(text(brand));
        for (int size = 18; size >= 12; --size) {
            auto font = ui_font(size, true, settings, has_cjk(text(brand)));
            auto previous = SelectObject(measure_dc, font);
            SIZE extent{};
            GetTextExtentPoint32W(measure_dc, brand_caption.c_str(), int(brand_caption.size()), &extent);
            SelectObject(measure_dc, previous);
            if (extent.cx <= scale(165, settings) || size == 12) {
                SendMessageW(brand, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                break;
            }
        }
        int nav_y = std::max(112, 58 + detail_height + 16);
        for (int i = 0; i < int(pages.size()); ++i) {
            int nav_height = std::max(38, settings_caption_height(navigation[i], 142, measure_dc));
            MoveWindow(navigation[i], scale(12, settings), scale(nav_y, settings),
                       scale(174, settings), scale(nav_height, settings), TRUE);
            nav_y += nav_height + 6;
        }
        ShowWindow(brand, metal ? SW_HIDE : SW_SHOWNOACTIVATE);
        ShowWindow(brand_detail, metal ? SW_HIDE : SW_SHOWNOACTIVATE);
        if (metal) {
            MoveWindow(page_title, scale(230, settings), scale(61, settings), r.right - scale(260, settings),
                       scale(30, settings), TRUE);
            MoveWindow(page_description, scale(230, settings), scale(92, settings),
                       r.right - scale(260, settings), scale(description_height, settings), TRUE);
            int category_y = 70;
            auto category = [&](size_t index, std::initializer_list<int> items) {
                settings_category_y[index] = category_y;
                int cursor = category_y + 32;
                for (int i : items) {
                    int height = std::max(32, settings_caption_height(navigation[i], 162, measure_dc));
                    MoveWindow(navigation[i], scale(24, settings), scale(cursor, settings),
                               scale(162, settings), scale(height, settings), TRUE);
                    cursor += height + 6;
                }
                category_y = cursor + 14;
            };
            category(0, {0, 5});
            category(1, {1, 2, 3});
            category(2, {4});
        } else {
            MoveWindow(page_title, scale(228, settings), scale(22, settings), r.right - scale(260, settings),
                       scale(32, settings), TRUE);
            MoveWindow(page_description, scale(230, settings), scale(59, settings),
                       r.right - scale(260, settings), scale(description_height, settings), TRUE);
        }
        ReleaseDC(settings, measure_dc);
        layout_body();
    }
    const Json &appearance() const {
        return appearance_preview ? *appearance_preview : config;
    }
    bool terminal() const {
        return active_theme.widgets.toggle == ToggleVisual::Ascii;
    }
    void theme_caption() {
        if (!settings)
            return;
        BOOL dark = GetRValue(bg) + GetGValue(bg) + GetBValue(bg) < 450;
        DwmSetWindowAttribute(settings, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        DwmSetWindowAttribute(settings, DWMWA_CAPTION_COLOR, &bg, sizeof(bg));
        DwmSetWindowAttribute(settings, DWMWA_TEXT_COLOR, &fg, sizeof(fg));
        DwmSetWindowAttribute(settings, DWMWA_BORDER_COLOR, &bg, sizeof(bg));
    }
    void update_skin() {
        std::string warning;
        auto resolved = theme_registry.fallback(appearance(), &warning);
        if (!warning.empty())
            status_note = warning;
        auto next = resolved.palette;
        if (skin_ready && resolved == active_theme)
            return;
        auto new_brush = CreateSolidBrush(rgb(next.background)),
             new_field = CreateSolidBrush(rgb(next.surface)),
             new_side = CreateSolidBrush(mix(rgb(next.background), rgb(next.surface)));
        if (!new_brush || !new_field || !new_side) {
            if (new_brush)
                DeleteObject(new_brush);
            if (new_field)
                DeleteObject(new_field);
            if (new_side)
                DeleteObject(new_side);
            throw std::runtime_error("Failed to create UI color scheme");
        }
        bg = rgb(next.background);
        panel = rgb(next.surface);
        fg = rgb(next.text);
        accent = rgb(next.accent);
        if (main)
            SetClassLongPtrW(main, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(new_brush));
        DeleteObject(brush);
        DeleteObject(field);
        brush = new_brush;
        field = new_field;
        DeleteObject(sidebar_brush);
        sidebar_brush = new_side;
        theme_caption();
        active_theme = std::move(resolved);
        active_skin = next;
        skin_ready = true;
        chat_style_dirty = true;
    }
    void repaint_appearance() {
        refresh_interface_fonts();
        if (settings && body)
            resize_settings();
        if (backdrop) {
            SetLayeredWindowAttributes(backdrop, 0, BYTE(std::lround(background_opacity(appearance()) * 255)),
                                       LWA_ALPHA);
            sync_background();
        }
        for (auto &decoration : edit_scrollbars)
            decoration->configure(beveled() || GetParent(decoration->edit()) == provider_advanced ||
                                      GetParent(decoration->edit()) == ocr_window,
                                  rgb(active_theme.colors.track), rgb(active_theme.colors.thumb),
                                  rgb(active_theme.colors.border));
        sync_tab_input_surface();
        sync_gear_input_surface();
        sync_resize_input_surface();
        sync_tab_flash();
        if (notices_text)
            style_notices();
        for (auto h : {main, settings, output, notices, bubble, backdrop, provider_advanced, ocr_window, block_window, model_window})
            if (h)
                RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        if (chat_tab_bar)
            InvalidateRect(chat_tab_bar, nullptr, FALSE);
        // RichEdit retains per-character colors; invalidate alone cannot update them.
        if (chat && (message_preview || chat_style_dirty)) {
            render();
            chat_style_dirty = false;
        }
        if (gear)
            InvalidateRect(gear, nullptr, FALSE);
        if (scrollbar && IsWindowVisible(scrollbar))
            InvalidateRect(scrollbar, nullptr, FALSE);
    }
    template <class F> void appearance_transaction(F change) {
        auto previous_preview = appearance_preview;
        struct RedrawGuard {
            HWND window;
            bool visible;
            explicit RedrawGuard(HWND h) : window(h), visible(h && IsWindowVisible(h)) {
                if (visible)
                    SendMessageW(window, WM_SETREDRAW, FALSE, 0);
            }
            ~RedrawGuard() {
                if (visible) {
                    SendMessageW(window, WM_SETREDRAW, TRUE, 0);
                    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
                }
            }
        } redraw(settings);
        try {
            change();
            theme_registry.resolve(appearance());
            update_skin();
            repaint_appearance();
        } catch (...) {
            auto error = std::current_exception();
            appearance_preview = std::move(previous_preview);
            try {
                update_skin();
                if (design_select) {
                    fill_design_choices(appearance());
                    fill_skin_choices(appearance());
                    fill_skin_fields(appearance());
                }
                repaint_appearance();
            } catch (...) {
                status_note = "Failed to restore skin preview. Please reopen settings.";
            }
            std::rethrow_exception(error);
        }
    }
    bool gear_input(HWND h, UINT msg, WPARAM, LPARAM l) {
        if (msg == WM_LBUTTONDOWN && !config.value("click_through", false)) {
            gear_down = true;
            gear_dragged = false;
            gear_press = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            SetCapture(h);
            SendMessageW(gear, BM_SETSTATE, TRUE, 0);
            return true;
        }
        if (msg == WM_MOUSEMOVE && gear_down &&
            (std::abs(GET_X_LPARAM(l) - gear_press.x) > scale(4) ||
             std::abs(GET_Y_LPARAM(l) - gear_press.y) > scale(4))) {
            POINT start = gear_press;
            ClientToScreen(h, &start);
            gear_dragged = true;
            gear_down = false;
            SendMessageW(gear, BM_SETSTATE, FALSE, 0);
            ReleaseCapture();
            SendMessageW(main, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(start.x, start.y));
            return true;
        }
        if (msg == WM_LBUTTONUP && gear_down) {
            RECT bounds{};
            GetClientRect(h, &bounds);
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            bool click = !gear_dragged && PtInRect(&bounds, p);
            gear_down = false;
            SendMessageW(gear, BM_SETSTATE, FALSE, 0);
            ReleaseCapture();
            if (click)
                SendMessageW(main, WM_COMMAND, MAKEWPARAM(id_gear, BN_CLICKED), reinterpret_cast<LPARAM>(gear));
            return true;
        }
        if (msg == WM_CAPTURECHANGED) {
            gear_down = false;
            SendMessageW(gear, BM_SETSTATE, FALSE, 0);
        }
        return false;
    }
    int number_value(const NumberRow &n) const {
        auto value = text(n.edit);
        if (value.empty() || value.size() > 4 ||
            !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
            throw std::runtime_error("Enter an integer within the range");
        int v = std::stoi(value);
        if (v < n.minimum || v > n.maximum)
            throw std::runtime_error("Value out of allowed range");
        return v;
    }
    void number_row(const char *key, const char *title, int y, int minimum, int maximum, int value,
                    int page = 0) {
        label(page, title, 20, y + 4, 170);
        auto slider =
            page_control(page, TRACKBAR_CLASSW, "", 195, y, 315, 28, TBS_HORZ | TBS_NOTICKS | WS_TABSTOP);
        SendMessageW(slider, TBM_SETRANGEMIN, FALSE, minimum);
        SendMessageW(slider, TBM_SETRANGEMAX, FALSE, maximum);
        SendMessageW(slider, TBM_SETPOS, TRUE, value);
        SendMessageW(slider, TBM_SETPAGESIZE, 0, maximum == 1000 ? 20 : maximum == 100 ? 5 : 1);
        auto number_edit = edit(page, key, 530, y, 85);
        SendMessageW(number_edit, EM_SETLIMITTEXT, 4, 0);
        set(number_edit, std::to_string(value));
        (page == 3 ? bubble_rows : number_rows).push_back({key, slider, number_edit, minimum, maximum});
    }
    SliderGeometry slider_layout(HWND h) const {
        RECT area{};
        GetClientRect(h, &area);
        return slider_geometry(active_theme.metrics, area.right, area.bottom, int(GetDpiForWindow(h)));
    }
    void paint_slider(HWND h, HDC dc) {
        RECT area{};
        GetClientRect(h, &area);
        paint_control_background(h, dc);
        auto g = slider_layout(h);
        int minimum = int(SendMessageW(h, TBM_GETRANGEMIN, 0, 0)),
            maximum = int(SendMessageW(h, TBM_GETRANGEMAX, 0, 0));
        int center = g.pixel(int(SendMessageW(h, TBM_GETPOS, 0, 0)), minimum, maximum);
        bool square = active_theme.widgets.slider == SliderVisual::Square,
             enabled = IsWindowEnabled(h) != FALSE;
        const auto &c = active_theme.colors;
        RECT rail{g.start, g.center_y - g.track_half, g.end, g.center_y + g.track_half};
        if (active_theme.widgets.slider == SliderVisual::Diamond) {
            RECT groove = rail;
            InflateRect(&groove, scale(2, h), scale(2, h));
            widgets::paint::frame(dc, groove, rgb(c.track), rgb(c.border), scale(1, h), true);
            RECT thumb{center - g.thumb_half, g.center_y - g.thumb_half, center + g.thumb_half,
                       g.center_y + g.thumb_half};
            widgets::paint::diamond(dc, thumb,
                                    rgb(!enabled            ? c.disabled
                                        : GetCapture() == h ? c.focus
                                                            : c.thumb),
                                    rgb(c.border));
            if (GetFocus() == h) {
                InflateRect(&area, -1, -1);
                DrawFocusRect(dc, &area);
            }
            return;
        }
        rounded(dc, rail, rgb(c.track), square ? 0 : float(g.track_half));
        rail.right = std::max(g.start + 1, center);
        rounded(dc, rail, enabled ? accent : rgb(c.disabled), square ? 0 : float(g.track_half));
        RECT thumb{center - g.thumb_half, g.center_y - g.thumb_half, center + g.thumb_half,
                   g.center_y + g.thumb_half};
        rounded(dc, thumb,
                rgb(!enabled              ? c.disabled
                    : GetCapture() == h   ? c.pressed
                    : widgets::hovered(h) ? c.hover
                    : square              ? active_skin.accent
                                          : c.thumb),
                square ? 0 : float(g.thumb_half), rgb(c.border), active_theme.metrics.border_width);
        if (GetFocus() == h) {
            InflateRect(&area, -1, -1);
            DrawFocusRect(dc, &area);
        }
    }
    void number_slide(HWND control) {
        for (const auto *controls : {&number_rows, &bubble_rows})
            for (const auto &n : *controls)
                if (n.slider == control) {
                    set(n.edit, std::to_string(SendMessageW(n.slider, TBM_GETPOS, 0, 0)));
                    return;
                }
    }
    void appearance_edit(HWND control) {
        for (const auto *controls : {&number_rows, &bubble_rows})
            for (const auto &n : *controls)
                if (n.edit == control) {
                    try {
                        SendMessageW(n.slider, TBM_SETPOS, TRUE, number_value(n));
                        if (n.key == "__background_transparency") preview_skin();
                        if (controls == &bubble_rows && bubble_preview)
                            update_bubble();
                    } catch (const std::exception &) {
                    }
                    return;
                }
        if (skin_sync || !skin_select)
            return;
        for (const auto &e : edits)
            if (e.h == control && e.key.rfind("__skin_", 0) == 0) {
                SendMessageW(skin_select, CB_SETCURSEL, skin_choices.size() - 1, 0);
                for (auto button : skin_buttons)
                    if (button)
                        InvalidateRect(button, nullptr, TRUE);
                preview_skin();
                return;
            }
    }
    HWND color_edit(int index) const {
        for (const auto &e : edits)
            if (e.key == std::string("__skin_") + color_keys.at(index))
                return e.h;
        return nullptr;
    }
    COLORREF draft_color(int index) const {
        try {
            return rgb(parse_skin_color(text(color_edit(index))));
        } catch (const std::exception &) {
            return panel;
        }
    }
    void fill_skin_fields(const Json &s) {
        skin_sync = true;
        auto colors = skin_colors(theme_registry.resolve(s).palette);
        for (int i = 0; i < 4; ++i) {
            set(color_edit(i), colors[color_keys[i]].get<std::string>());
            InvalidateRect(skin_buttons[i], nullptr, TRUE);
        }
        for (const auto &n : number_rows)
            if (n.key == "__background_transparency") {
                int value = int(std::lround((1 - background_opacity(s)) * 100));
                set(n.edit, std::to_string(value));
                SendMessageW(n.slider, TBM_SETPOS, TRUE, value);
            }
        skin_sync = false;
    }
    void fill_design_choices(const Json &s) {
        design_choices = theme_registry.styles();
        SendMessageW(design_select, CB_RESETCONTENT, 0, 0);
        int selected = 0;
        for (size_t i = 0; i < design_choices.size(); ++i) {
            auto caption = wide(design_choices[i].first == "ios" ? "iOS" : design_choices[i].first == "terminal" ? "Terminal" : design_choices[i].first == "classic" ? "Classic" : design_choices[i].second);
            SendMessageW(design_select, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(caption.c_str()));
            if (design_choices[i].first == s.value("ui_style", std::string("ios")))
                selected = int(i);
        }
        SendMessageW(design_select, CB_SETCURSEL, selected, 0);
    }
    void preview_theme_id(std::string_view id) {
        auto next = draft_skin();
        theme_registry.change_style(next, id);
        theme_registry.resolve(next);
        appearance_transaction([&] {
            appearance_preview = std::move(next);
            fill_design_choices(appearance());
            fill_skin_choices(appearance());
            fill_skin_fields(appearance());
        });
    }
    void import_theme() {
        wchar_t file[32768]{};
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = settings;
        dialog.lpstrFile = file;
        dialog.nMaxFile = 32768;
        dialog.lpstrFilter = L"JSON skins (*.json)\0*.json\0";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog))
            return;
        auto previous = theme_registry;
        try {
            auto id = theme_registry.import_file(file);
            preview_theme_id(id);
            set(settings_status, "Skin imported and previewed; click Save to apply");
        } catch (...) {
            theme_registry = std::move(previous);
            throw;
        }
    }
    void export_theme() {
        auto draft = draft_skin();
        auto definition = theme_registry.export_theme(draft, "community.export", active_theme.name);
        definition["id"] = "community.export-" + sha256(definition.dump()).substr(0, 12);
        wchar_t file[32768] = L"theme.json";
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = settings;
        dialog.lpstrFile = file;
        dialog.nMaxFile = 32768;
        dialog.lpstrFilter = L"JSON skins (*.json)\0*.json\0";
        dialog.lpstrDefExt = L"json";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetSaveFileNameW(&dialog)) {
            write_json(file, definition);
            set(settings_status, "Current full skin configuration exported");
        }
    }
    void reload_themes() {
        auto previous = theme_registry;
        try {
            auto draft = draft_skin();
            theme_registry.reload();
            std::string warning;
            auto candidate = theme_registry.fallback(draft, &warning);
            if (!warning.empty()) {
                draft["ui_style"] = candidate.id;
                draft["skin"] = "graphite";
                draft["skin_colors"] = skin_colors(candidate.palette);
            }
            appearance_transaction([&] {
                appearance_preview = draft;
                fill_design_choices(draft);
                fill_skin_choices(draft);
                fill_skin_fields(draft);
            });
            if (warning.empty() && !theme_registry.warnings().empty())
                warning = theme_registry.warnings().front();
            set(settings_status, warning.empty() ? "Skin reloaded" : warning);
        } catch (...) {
            theme_registry = std::move(previous);
            throw;
        }
    }
    void open_theme_directory() {
        std::filesystem::create_directories(theme_registry.directory());
        ShellExecuteW(settings, L"open", theme_registry.directory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    void fill_skin_choices(const Json &s) {
        skin_choices = theme_registry.palettes(s.value("ui_style", std::string("ios")));
        SendMessageW(skin_select, CB_RESETCONTENT, 0, 0);
        int index = 0;
        for (size_t i = 0; i < skin_choices.size(); ++i) {
            auto name = wide(ui_text(skin_choices[i].second));
            SendMessageW(skin_select, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
            if (skin_choices[i].first == s.value("skin", std::string("graphite")))
                index = int(i);
        }
        SendMessageW(skin_select, CB_SETCURSEL, index, 0);
    }
    Json draft_skin() {
        Json s = appearance();
        int index = int(SendMessageW(skin_select, CB_GETCURSEL, 0, 0));
        if (index < 0 || index >= int(skin_choices.size()))
            throw std::runtime_error("Select a UI color scheme");
        s["skin"] = skin_choices[index].first;
        for (int i = 0; i < 4; ++i)
            s["skin_colors"][color_keys[i]] = skin_color_hex(parse_skin_color(text(color_edit(i))));
        for (const auto &n : number_rows)
            if (n.key == "__background_transparency")
                s["background_opacity"] = 1 - number_value(n) / 100.0;
        return s;
    }
    void preview_skin() {
        Json next;
        try {
            next = draft_skin();
        } catch (const std::exception &) {
            return;
        } // Partial hex input is not a render failure.
        theme_registry.resolve(next);
        appearance_transaction([&] { appearance_preview = std::move(next); });
    }
    void select_skin() {
        int index = int(SendMessageW(skin_select, CB_GETCURSEL, 0, 0));
        if (index < 0 || index >= int(skin_choices.size()))
            return;
        auto next = appearance();
        for (const auto &n : number_rows)
            if (n.key == "__background_transparency")
                next["background_opacity"] = 1 - number_value(n) / 100.0;
        next["skin"] = skin_choices[index].first;
        if (skin_choices[index].first == "custom")
            next = draft_skin();
        theme_registry.resolve(next);
        appearance_transaction([&] {
            fill_skin_fields(next);
            appearance_preview = std::move(next);
        });
    }
    void select_design() {
        try {
            auto next = draft_skin();
            int index = int(SendMessageW(design_select, CB_GETCURSEL, 0, 0));
            if (index < 0 || index >= int(design_choices.size()))
                throw std::runtime_error("Select a design style");
            theme_registry.change_style(next, design_choices[index].first);
            theme_registry.resolve(next);
            appearance_transaction([&] {
                fill_skin_choices(next);
                fill_skin_fields(next);
                appearance_preview = std::move(next);
            });
        } catch (...) {
            fill_design_choices(appearance());
            throw;
        }
    }
    void restore_appearance() {
        stop_bubble_preview();
        if (message_preview)
            toggle_message_preview();
        close_picker(false);
        auto resolved = theme_registry.fallback(config);
        if (resolved.id != config.value("ui_style", std::string("ios"))) {
            config["ui_style"] = resolved.id;
            config["skin"] = "graphite";
            config["skin_colors"] = skin_colors(resolved.palette);
        }
        appearance_transaction([&] {
            appearance_preview.reset();
            if (design_select) {
                fill_design_choices(appearance());
                fill_skin_choices(config);
                fill_skin_fields(config);
            }
        });
    }
    void choose_skin_color(int index) {
        static COLORREF choices[16]{};
        CHOOSECOLORW color{sizeof(color)};
        color.hwndOwner = settings;
        color.lpCustColors = choices;
        color.rgbResult = draft_color(index);
        color.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&color)) {
            set(color_edit(index),
                skin_color_hex((uint32_t(GetRValue(color.rgbResult)) << 16) |
                               (uint32_t(GetGValue(color.rgbResult)) << 8) | GetBValue(color.rgbResult)));
        }
    }
    HCURSOR reply_cursor() {
        auto dpi = GetDpiForWindow(main);
        auto &cursor = reply_cursors[dpi];
        if (cursor)
            return cursor;
        int size = std::max(32, scale(32));
        float unit = float(size) / 32;
        Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
        Gdiplus::Graphics g(&bitmap);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        path.AddLine(24 * unit, 6 * unit, 24 * unit, 18 * unit);
        path.AddLine(24 * unit, 18 * unit, 9 * unit, 18 * unit);
        path.StartFigure();
        path.AddLine(15 * unit, 12 * unit, 9 * unit, 18 * unit);
        path.AddLine(9 * unit, 18 * unit, 15 * unit, 24 * unit);
        Gdiplus::Pen outline(Gdiplus::Color(255, 0, 0, 0), 5 * unit),
            white(Gdiplus::Color(255, 255, 255, 255), 2.5f * unit);
        outline.SetLineJoin(Gdiplus::LineJoinRound);
        white.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawPath(&outline, &path);
        g.DrawPath(&white, &path);
        HBITMAP color = nullptr;
        bitmap.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &color);
        std::vector<unsigned char> mask_bits(size_t((size + 15) / 16) * 2 * size, 0);
        HBITMAP mask = CreateBitmap(size, size, 1, 1, mask_bits.data());
        ICONINFO icon{FALSE, DWORD(9 * unit), DWORD(18 * unit), mask, color};
        cursor = CreateIconIndirect(&icon);
        DeleteObject(color);
        DeleteObject(mask);
        return cursor ? cursor : LoadCursorW(nullptr, IDC_HAND);
    }
    std::optional<ReplyHit> reply_at(POINT p) const {
        if (message_preview || config.value("click_through", false))
            return {};
        for (const auto &hit : reply_hits)
            if (PtInRect(&hit.bounds, p))
                return hit;
        return {};
    }
    bool reply_pointer(HWND source) {
        POINT p{};
        GetCursorPos(&p);
        ScreenToClient(chat, &p);
        if (reply_at(p)) {
            SetCursor(reply_cursor());
            return true;
        }
        if (source == reply_surface) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return true;
        }
        return false;
    }
    void sync_tab_input_surface() {
        if (!tab_input_surface || !chat_tab_bar)
            return;
        if (active_theme.chat.tabs != ChatTabVisual::Classic || config.value("click_through", false) ||
            !IsWindowVisible(main) || IsIconic(main) || !IsWindowVisible(chat_tab_bar)) {
            ShowWindow(tab_input_surface, SW_HIDE);
            return;
        }
        RECT bounds{}, old{};
        GetWindowRect(chat_tab_bar, &bounds);
        GetWindowRect(tab_input_surface, &old);
        if (!EqualRect(&bounds, &old) || !IsWindowVisible(tab_input_surface))
            SetWindowPos(tab_input_surface, main, bounds.left, bounds.top, bounds.right - bounds.left,
                         bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    void sync_gear_input_surface() {
        if (!gear_input_surface || !gear)
            return;
        if (config.value("click_through", false) || !IsWindowVisible(main) || IsIconic(main)) {
            ShowWindow(gear_input_surface, SW_HIDE);
            return;
        }
        RECT bounds{}, old{};
        GetWindowRect(gear, &bounds);
        GetWindowRect(gear_input_surface, &old);
        // Style switches can reorder the tab/resize planes without changing bounds.
        // Keep gear input immediately below the overlay and above those planes.
        if (!EqualRect(&bounds, &old) || !IsWindowVisible(gear_input_surface) ||
            GetWindow(main, GW_HWNDNEXT) != gear_input_surface)
            SetWindowPos(gear_input_surface, main, bounds.left, bounds.top, bounds.right - bounds.left,
                         bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    void sync_resize_input_surface() {
        if (!resize_input_surface)
            return;
        if (active_theme.chat.tabs != ChatTabVisual::Classic || config.value("click_through", false) ||
            !IsWindowVisible(main) || IsIconic(main)) {
            ShowWindow(resize_input_surface, SW_HIDE);
            return;
        }
        RECT bounds{}, old{};
        GetWindowRect(main, &bounds);
        GetWindowRect(resize_input_surface, &old);
        int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top, edge = scale(6);
        if (width != old.right - old.left || height != old.bottom - old.top || edge != resize_input_edge) {
            auto ring = CreateRectRgn(0, 0, width, height);
            auto interior = CreateRectRgn(edge, edge, width - edge, height - edge);
            bool ready = ring && interior && CombineRgn(ring, ring, interior, RGN_DIFF) != ERROR;
            if (interior)
                DeleteObject(interior);
            if (!ready || !SetWindowRgn(resize_input_surface, ring, FALSE)) {
                if (ring)
                    DeleteObject(ring);
                ShowWindow(resize_input_surface, SW_HIDE);
                return;
            }
            resize_input_edge = edge;
        }
        if (!EqualRect(&bounds, &old) || !IsWindowVisible(resize_input_surface))
            // Tab and gear input planes take priority where they overlap the ring.
            SetWindowPos(resize_input_surface, tab_input_surface, bounds.left, bounds.top, width, height,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    LRESULT resize_input_surface_message(UINT msg, WPARAM w, LPARAM l) {
        if (msg == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        if (msg == WM_NCHITTEST || msg == WM_NCLBUTTONDOWN)
            return SendMessageW(main, msg, w, l);
        if (msg == WM_ERASEBKGND || msg == WM_PAINT) {
            PAINTSTRUCT paint{};
            auto dc = msg == WM_PAINT ? BeginPaint(resize_input_surface, &paint) : reinterpret_cast<HDC>(w);
            RECT bounds{};
            GetClientRect(resize_input_surface, &bounds);
            FillRect(dc, &bounds, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            if (msg == WM_PAINT)
                EndPaint(resize_input_surface, &paint);
            return msg == WM_ERASEBKGND ? 1 : 0;
        }
        return DefWindowProcW(resize_input_surface, msg, w, l);
    }
    LRESULT gear_input_surface_message(UINT msg, WPARAM w, LPARAM l) {
        if (msg == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        if (msg == WM_ERASEBKGND || msg == WM_PAINT) {
            PAINTSTRUCT paint{};
            auto dc = msg == WM_PAINT ? BeginPaint(gear_input_surface, &paint) : reinterpret_cast<HDC>(w);
            RECT r{};
            GetClientRect(gear_input_surface, &r);
            FillRect(dc, &r, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            if (msg == WM_PAINT)
                EndPaint(gear_input_surface, &paint);
            return msg == WM_ERASEBKGND ? 1 : 0;
        }
        if (gear_input(gear_input_surface, msg, w, l))
            return 0;
        return DefWindowProcW(gear_input_surface, msg, w, l);
    }
    LRESULT tab_input_surface_message(UINT msg, WPARAM w, LPARAM l) {
        if (msg == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        if (msg == WM_SETCURSOR) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        if (msg == WM_ERASEBKGND || msg == WM_PAINT) {
            PAINTSTRUCT paint{};
            auto dc = msg == WM_PAINT ? BeginPaint(tab_input_surface, &paint) : reinterpret_cast<HDC>(w);
            RECT r{};
            GetClientRect(tab_input_surface, &r);
            FillRect(dc, &r, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            if (msg == WM_PAINT)
                EndPaint(tab_input_surface, &paint);
            return msg == WM_ERASEBKGND ? 1 : 0;
        }
        if (auto result = chat_tab_input(tab_input_surface, msg, w, l))
            return *result;
        return DefWindowProcW(tab_input_surface, msg, w, l);
    }
    void sync_reply_surface() {
        sync_tab_input_surface();
        sync_gear_input_surface();
        sync_resize_input_surface();
        if (!reply_surface || !chat)
            return;
        if (config.value("click_through", false) || !IsWindowVisible(main) || IsIconic(main)) {
            ShowWindow(reply_surface, SW_HIDE);
            return;
        }
        RECT r{};
        GetClientRect(chat, &r);
        POINT p{};
        ClientToScreen(chat, &p);
        r.right += scale(8); // Include the scrollbar gutter, even while its visual is hidden.
        RECT old{};
        GetWindowRect(reply_surface, &old);
        if (old.left != p.x || old.top != p.y || old.right - old.left != r.right ||
            old.bottom - old.top != r.bottom || !IsWindowVisible(reply_surface) ||
            GetWindow(reply_surface, GW_HWNDPREV) != main)
            SetWindowPos(reply_surface, main, p.x, p.y, r.right, r.bottom, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    void refresh_reply_surface() {
        if (!reply_surface || !chat)
            return;
        reply_hits.clear();
        const auto &source = message_preview ? preview_messages : messages;
        RECT area{};
        SendMessageW(chat, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&area));
        POINT top{area.left, area.top};
        auto first = long(SendMessageW(chat, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&top)));
        auto row = std::lower_bound(chat_rows.begin(), chat_rows.end(), first,
                                    [](const ChatRow &r, long pos) { return r.end <= pos; });
        // Keep wheel routing stable as rows move, including inter-line gaps and the list ends.
        // Reply hit rectangles remain independent; blank viewport clicks never select a player.
        RECT viewport{};
        GetClientRect(chat, &viewport);
        viewport.right += scale(8);
        if (!reply_region_valid || !EqualRect(&viewport, &reply_region_bounds)) {
            auto region = CreateRectRgnIndirect(&viewport);
            if (!SetWindowRgn(reply_surface, region, FALSE))
                DeleteObject(region);
            else {
                reply_region_bounds = viewport;
                reply_region_valid = true;
            }
        }
        int height = scroll_state().line_height;
        for (; row != chat_rows.end(); ++row) {
            POINT start{}, last{};
            SendMessageW(chat, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&start), row->start);
            if (start.y >= area.bottom)
                break;
            SendMessageW(chat, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&last),
                         std::max(row->start, row->end - 1));
            RECT bounds{area.left, std::max(area.top, start.y), area.right,
                        std::min(area.bottom, last.y + height)};
            if (bounds.bottom <= bounds.top || row->index >= source.size())
                continue;
            const auto &message = source[row->index].message;
            if (!message_preview && !reply_target(message) && !whisper_target(message))
                continue;
            reply_hits.push_back({bounds, row->session, row->seq, row->start});
        }
        sync_reply_surface();
    }
    bool chat_mouse(HWND source, UINT msg, LPARAM l) {
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN) {
            auto hit = reply_at(p);
            chat_button = msg;
            chat_press = p;
            chat_pressed = hit.has_value();
            pressed_message = hit ? std::optional(std::pair{hit->session, hit->seq}) : std::nullopt;
            if (chat_pressed)
                SetCapture(source);
            return source == reply_surface || chat_pressed;
        }
        auto hit = reply_at(p);
        bool click = chat_pressed && pressed_message && hit &&
                     *pressed_message == std::pair{hit->session, hit->seq} &&
                     chat_button == UINT(msg == WM_RBUTTONUP ? WM_RBUTTONDOWN : WM_LBUTTONDOWN) &&
                     std::abs(p.x - chat_press.x) <= scale(4) && std::abs(p.y - chat_press.y) <= scale(4);
        chat_pressed = false;
        pressed_message.reset();
        if (GetCapture() == reply_surface || GetCapture() == chat)
            ReleaseCapture();
        if (click) {
            if (msg == WM_RBUTTONUP) {
                POINT screen = p;
                ClientToScreen(source, &screen);
                show_message_menu(hit->start, screen);
            } else pick_reply(hit->start);
            return true;
        }
        return source == reply_surface;
    }
    LRESULT reply_surface_message(UINT msg, WPARAM w, LPARAM l) {
        if (msg == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        if (msg == WM_SETCURSOR || msg == WM_MOUSEMOVE) {
            reply_pointer(reply_surface);
            return TRUE;
        }
        if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONUP) {
            chat_mouse(reply_surface, msg, l);
            return 0;
        }
        if (msg == WM_CAPTURECHANGED) {
            chat_pressed = false;
            pressed_message.reset();
            return 0;
        }
        if (msg == WM_MOUSEWHEEL) {
            scroll_chat_wheel(GET_WHEEL_DELTA_WPARAM(w));
            return 0;
        }
        if (msg == WM_ERASEBKGND || msg == WM_PAINT) {
            PAINTSTRUCT paint{};
            auto dc = msg == WM_PAINT ? BeginPaint(reply_surface, &paint) : reinterpret_cast<HDC>(w);
            RECT r{};
            GetClientRect(reply_surface, &r);
            FillRect(dc, &r, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            if (msg == WM_PAINT)
                EndPaint(reply_surface, &paint);
            return msg == WM_ERASEBKGND ? 1 : 0;
        }
        return DefWindowProcW(reply_surface, msg, w, l);
    }
    void create() {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = procedure;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = brush;
        wc.lpszClassName = L"BianLingo.Window";
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
        if (!wc.hIcon)
            wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wc);
        backdrop = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
        if (!backdrop)
            throw std::runtime_error("Failed to create background layer");
        if (first_launch) {
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
                const auto &work = monitor.rcWork;
                const auto bounds = initial_overlay_rect(work,
                    SIZE{scale(config.value("width", 460)), scale(config.value("height", 240))},
                    SIZE{std::min(scale(900), int(work.right - work.left) - 24),
                         std::min(scale(700), int(work.bottom - work.top) - 24)});
                config["x"] = bounds.left;
                config["y"] = bounds.top;
            }
        }
        main =
            CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_APPWINDOW, wc.lpszClassName,
                            wide(ui_text("BianLingo")).c_str(), WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN,
                            config.value("x", 60), config.value("y", 100), scale(config.value("width", 460)),
                            scale(config.value("height", 240)), backdrop, nullptr, instance, nullptr);
        if (!main)
            throw std::runtime_error("Failed to create chat overlay");
        // Keep native resize behavior without the system-painted light frame.
        // Windows 11 can also draw a one-pixel DWM outline outside the client.
        COLORREF no_border = DWMWA_COLOR_NONE;
        DwmSetWindowAttribute(main, DWMWA_BORDER_COLOR, &no_border, sizeof(no_border));
        SetWindowPos(main, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        chat =
            control(main, MSFTEDIT_CLASS, "", 5, 24, 440, 200, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL);
        SendMessageW(chat, EM_SETBKGNDCOLOR, 0, transparency_key);
        SendMessageW(chat, EM_SETLIMITTEXT, 2'000'000, 0);
        SendMessageW(chat, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
        SendMessageW(chat, EM_SETEVENTMASK, 0, ENM_LINK | ENM_MOUSEEVENTS);
        SetWindowSubclass(
            chat,
            [](HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) -> LRESULT {
                if (app && (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_LBUTTONUP ||
                            msg == WM_RBUTTONUP))
                    if (app->chat_mouse(h, msg, l))
                        return 0;
                if (app && msg == WM_MOUSEWHEEL) {
                    app->scroll_chat_wheel(GET_WHEEL_DELTA_WPARAM(w));
                    return 0;
                }
                if (app && msg == WM_CAPTURECHANGED) {
                    app->chat_pressed = false;
                    app->pressed_message.reset();
                }
                if (app && msg == WM_SETCURSOR && app->reply_pointer(h))
                    return TRUE;
                if (msg == WM_CONTEXTMENU)
                    return 0;
                auto result = DefSubclassProc(h, msg, w, l);
                if (app && msg == WM_MOUSEMOVE)
                    app->reply_pointer(h); // RichEdit may restore its I-beam during mouse movement.
                if (app && (msg == WM_VSCROLL || msg == WM_KEYUP)) {
                    app->update_scrollbar();
                    app->refresh_reply_surface();
                }
                return result;
            },
            2, 0);
        reply_surface = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                        wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, backdrop, nullptr,
                                        instance, nullptr);
        if (!reply_surface)
            throw std::runtime_error("Cannot create message click area");
        // Alpha zero/color-keyed pixels cannot receive Windows mouse input.
        // This black 1/255 plane sits UNDER the text and covers the stable chat viewport.
        // Wheel input must remain ours through blank pixels and at either scroll boundary.
        SetLayeredWindowAttributes(reply_surface, 0, 1, LWA_ALPHA);
        scrollbar = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_CHILD, 0, 0, 0, 0, main,
                                    nullptr, instance, nullptr);
        gear = control(main, L"BUTTON", "BianLingo · Settings", 420, 0, 27, 24, BS_PUSHBUTTON, id_gear);
        SetWindowSubclass(
            gear,
            [](HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) -> LRESULT {
                if (app && app->gear_input(h, msg, w, l))
                    return 0;
                return DefSubclassProc(h, msg, w, l);
            },
            1, 0);
        chat_tab_bar = widgets::create(
            instance, main, L"Chat tabs", 0, 0, 1, 1, 0, 0, widgets::Kind::Label,
            [this](HWND h, HDC dc) { paint_chat_tab_bar(h, dc); },
            [this](HWND h, UINT m, WPARAM w, LPARAM l) { return chat_tab_input(h, m, w, l); });
        // The color-keyed title interior is truly transparent. A separate 1/255
        // input plane retains full-tab clicks without an opaque title background.
        tab_input_surface = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"",
            WS_POPUP, 0, 0, 1, 1, backdrop, nullptr, instance, nullptr);
        if (!tab_input_surface)
            throw std::runtime_error("Failed to create tab hit area");
        SetLayeredWindowAttributes(tab_input_surface, 0, 1, LWA_ALPHA);
        gear_input_surface = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"",
            WS_POPUP, 0, 0, 1, 1, backdrop, nullptr, instance, nullptr);
        if (!gear_input_surface)
            throw std::runtime_error("Failed to create settings button hit area");
        SetLayeredWindowAttributes(gear_input_surface, 0, 1, LWA_ALPHA);
        // Keep the native resize hit zones without painting a visible frame.
        // Its region is only a thin ring; message and tab interiors stay free.
        resize_input_surface = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"",
            WS_POPUP, 0, 0, 1, 1, backdrop, nullptr, instance, nullptr);
        if (!resize_input_surface)
            throw std::runtime_error("Could not create the window resize region");
        SetLayeredWindowAttributes(resize_input_surface, 0, 1, LWA_ALPHA);
        apply();
        resize_main();
        ShowWindow(main, SW_SHOWNOACTIVATE);
        if (!RegisterHotKey(main, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F10))
            status_note = "Ctrl+Alt+F10 unlock hotkey registration failed; use the tray menu";
        if (!register_hotkey(config.at("hotkey")))
            status_note = "Output shortcut is in use. Change it in settings or close the old translator and restart the beta.";
        tray.cbSize = sizeof(tray);
        tray.hWnd = main;
        tray.uID = 1;
        tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        tray.uCallbackMessage = tray_message;
        tray.hIcon = wc.hIcon;
        wcscpy_s(tray.szTip, L"BianLingo · Right-click settings");
        Shell_NotifyIconW(NIM_ADD, &tray);
        SetTimer(main, 1, 100, nullptr);
        if (smoke)
            SetTimer(main, 99, 1200, nullptr);
        else {
            engine->start();
            if (first_launch || !engine->has_key()) {
                open_settings();
                switch_page(2);
            }
        }
    }
    bool register_hotkey(const Json &key) {
        UnregisterHotKey(main, 1);
        return RegisterHotKey(main, 1, UINT(key.value("modifiers", 0)) | MOD_NOREPEAT,
                              UINT(key.value("key", 192))) != 0;
    }
    void apply() {
        sync_chat_tabs();
        update_skin();
        refresh_interface_fonts();
        // Text stays fully opaque. Only the separate lower window changes alpha.
        SetLayeredWindowAttributes(main, transparency_key, 255, LWA_COLORKEY);
        SetLayeredWindowAttributes(backdrop, 0, BYTE(std::lround(background_opacity(config) * 255)),
                                   LWA_ALPHA);
        sync_background();
        auto ex = GetWindowLongPtrW(main, GWL_EXSTYLE);
        if (config.value("click_through", false))
            ex |= WS_EX_TRANSPARENT;
        else
            ex &= ~WS_EX_TRANSPARENT;
        SetWindowLongPtrW(main, GWL_EXSTYLE, ex);
        auto dpi = GetDpiForWindow(main);
        int size = config.value("font_size", 14);
        if (!chat_font || applied_chat_dpi != dpi || applied_chat_font_size != size) {
            auto next = CreateFontW(-scale(size), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                                    0, ANTIALIASED_QUALITY, 0, L"Microsoft YaHei UI");
            if (!next)
                throw std::runtime_error("Failed to create chat font");
            SendMessageW(chat, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
            if (chat_font)
                DeleteObject(chat_font);
            chat_font = next;
            applied_chat_dpi = dpi;
            applied_chat_font_size = size;
            auto dc = GetDC(chat);
            auto old = SelectObject(dc, chat_font);
            TEXTMETRICW metrics{};
            GetTextMetricsW(dc, &metrics);
            chat_line_height = std::max(1, int(metrics.tmHeight));
            SelectObject(dc, old);
            ReleaseDC(chat, dc);
        }
        repaint_appearance();
        update_overlay_handles();
        refresh_reply_surface();
        InvalidateRect(main, nullptr, TRUE);
    }
    void sync_background() {
        if (!main || !backdrop)
            return;
        if (!IsWindowVisible(main) || IsIconic(main) || background_opacity(appearance()) <= 0) {
            ShowWindow(backdrop, SW_HIDE);
            return;
        }
        RECT r{};
        GetWindowRect(main, &r);
        if (active_theme.chat.tabs == ChatTabVisual::Classic)
            r.top = std::min(r.bottom - 1, r.top + scale(24));
        RECT old{};
        GetWindowRect(backdrop, &old);
        if (!EqualRect(&r, &old))
            SetWindowPos(backdrop, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        if (!IsWindowVisible(backdrop))
            ShowWindow(backdrop, SW_SHOWNOACTIVATE);
    }
    void update_overlay_handles() {
        POINT cursor{};
        RECT bounds{};
        GetWindowRect(main, &bounds);
        InflateRect(&bounds, scale(6), scale(6));
        bool visible =
            !config.value("click_through", false) && GetCursorPos(&cursor) && PtInRect(&bounds, cursor);
        if (visible != handles_visible) {
            handles_visible = visible;
            if (active_theme.chat.tabs != ChatTabVisual::Classic)
                InvalidateRect(main, nullptr, TRUE);
        }
    }
    void resize_main() {
        if (!chat)
            return;
        RECT r{};
        GetClientRect(main, &r);
        MoveWindow(chat, scale(5), scale(24), std::max(1, int(r.right) - scale(18)),
                   std::max(1, int(r.bottom) - scale(28)), TRUE);
        MoveWindow(gear, std::max(0, int(r.right) - scale(32)), 0, scale(27), scale(24), TRUE);
        if (chat_tab_bar)
            MoveWindow(chat_tab_bar, scale(5), 0, std::max(1, int(r.right) - scale(42)), scale(24), TRUE);
        if (scrollbar)
            MoveWindow(scrollbar, std::max(0, int(r.right) - scale(12)), scale(26), scale(7),
                       std::max(1, int(r.bottom) - scale(30)), TRUE);
        update_scrollbar();
        refresh_reply_surface();
    }
    COLORREF player_color(std::string_view c) {
        static const std::map<std::string, COLORREF> colors = {
            {"WARRIOR", RGB(199, 156, 110)}, {"PALADIN", RGB(245, 140, 186)},
            {"HUNTER", RGB(171, 212, 115)},  {"ROGUE", RGB(255, 245, 105)},
            {"PRIEST", RGB(255, 255, 255)},  {"DEATHKNIGHT", RGB(196, 31, 59)},
            {"SHAMAN", RGB(0, 112, 222)},    {"MAGE", RGB(105, 204, 240)},
            {"WARLOCK", RGB(148, 130, 201)}, {"MONK", RGB(0, 255, 152)},
            {"DRUID", RGB(255, 125, 10)},    {"DEMONHUNTER", RGB(163, 48, 201)},
            {"EVOKER", RGB(51, 147, 127)}};
        auto i = colors.find(std::string(c));
        return i == colors.end() ? fg : i->second;
    }
    void append(std::string_view s, COLORREF color) {
        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR;
        format.crTextColor = color;
        SendMessageW(chat, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        auto w = wide(s);
        SendMessageW(chat, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(w.c_str()));
    }
    static COLORREF rgb(uint32_t c) {
        return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255);
    }
    void append_chat(std::string_view value, uint32_t base) {
        for (const auto &span : chat_spans(value, base))
            append(span.text, rgb(span.color));
    }
    long chat_length() const {
        GETTEXTLENGTHEX options{GTL_NUMCHARS | GTL_PRECISE, 1200};
        return long(SendMessageW(chat, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&options), 0));
    }
    struct ScrollState {
        POINT position{};
        int maximum = 0, viewport = 0, line_height = 1;
        bool bottom = true;
    };
    ScrollState scroll_state() const {
        ScrollState state;
        SendMessageW(chat, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&state.position));
        RECT area{};
        SendMessageW(chat, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&area));
        state.viewport = std::max(1, int(area.bottom - area.top));
        const long length = chat_length();
        POINT end{};
        SendMessageW(chat, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&end), std::max(0L, length - 1));
        state.line_height = chat_line_height;
        state.maximum = std::max(0, int(end.y + state.position.y + chat_line_height - area.bottom));
        state.bottom = state.position.y >= state.maximum - 2;
        return state;
    }
    RECT scroll_thumb(const ScrollState &state) const {
        RECT bounds{};
        GetClientRect(scrollbar, &bounds);
        int height = std::max(1, int(bounds.bottom));
        int length =
            std::clamp(int(int64_t(height) * state.viewport / std::max(1, state.maximum + state.viewport)),
                       std::min(height, scale(18)), height);
        int top = state.maximum ? int(int64_t(height - length) * state.position.y / state.maximum) : 0;
        return {0, std::clamp(top, 0, height - length), bounds.right,
                std::clamp(top, 0, height - length) + length};
    }
    void set_scrollbar_hover(bool hover) {
        if (!scrollbar || !chat)
            return;
        bool visible = (hover || scroll_dragging) && scroll_state().maximum > 0;
        if (bool(IsWindowVisible(scrollbar)) != visible)
            ShowWindow(scrollbar, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (visible) {
            auto next = scroll_thumb(scroll_state());
            if (!chat_thumb_valid || !EqualRect(&next, &last_chat_thumb)) {
                last_chat_thumb = next;
                chat_thumb_valid = true;
                InvalidateRect(scrollbar, nullptr, FALSE);
            }
        } else
            chat_thumb_valid = false;
    }
    void update_scrollbar() {
        if (!scrollbar || !chat)
            return;
        POINT cursor{};
        RECT bounds{};
        GetWindowRect(main, &bounds);
        bool hover =
            GetCursorPos(&cursor) && PtInRect(&bounds, cursor) && !config.value("click_through", false);
        set_scrollbar_hover(hover);
    }
    void scroll_chat_wheel(int delta) {
        if (!chat || config.value("click_through", false))
            return;
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        chat_wheel_remainder += delta;
        int notches = chat_wheel_remainder / WHEEL_DELTA;
        chat_wheel_remainder %= WHEEL_DELTA;
        if (!notches || !lines)
            return;
        auto state = scroll_state();
        int64_t distance = lines == WHEEL_PAGESCROLL ? state.viewport : int64_t(lines) * state.line_height;
        auto position = state.position;
        position.y = LONG(std::clamp(int64_t(position.y) - int64_t(notches) * distance, int64_t(0),
                                     int64_t(state.maximum)));
        SendMessageW(chat, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&position));
        update_scrollbar();
        refresh_reply_surface();
        // Hover is stationary during wheel scrolling; update the target cursor immediately.
        POINT pointer{};
        if (GetCursorPos(&pointer)) {
            RECT bounds{};
            GetWindowRect(chat, &bounds);
            if (PtInRect(&bounds, pointer))
                reply_pointer(reply_surface);
        }
    }
    void drag_scrollbar(int y) {
        auto state = scroll_state();
        auto thumb = scroll_thumb(state);
        RECT bounds{};
        GetClientRect(scrollbar, &bounds);
        int travel = std::max(1, int(bounds.bottom - (thumb.bottom - thumb.top)));
        POINT position = state.position;
        position.y = int(int64_t(std::clamp(y - scroll_drag_offset, 0, travel)) * state.maximum / travel);
        SendMessageW(chat, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&position));
        InvalidateRect(chat, nullptr, FALSE);
        update_scrollbar();
        refresh_reply_surface();
    }
    struct ChatBookmark {
        POINT scroll{}, anchor_pos{};
        bool bottom = true;
        std::optional<std::pair<uint32_t, uint32_t>> anchor;
    };
    std::optional<ChatBookmark> preview_live_view;
    std::map<std::string, ChatBookmark> tab_views, preview_tab_views;
    ChatBookmark bookmark() const {
        ChatBookmark view;
        auto &[scroll, anchor_pos, bottom, anchor] = view;
        SendMessageW(chat, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        bottom = scroll_state().bottom;
        if (!bottom) {
            POINT top{4, 0};
            auto character = long(SendMessageW(chat, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&top)));
            for (const auto &row : chat_rows)
                if (row.end > character) {
                    anchor = {row.session, row.seq};
                    SendMessageW(chat, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&anchor_pos), row.start);
                    break;
                }
        }
        return view;
    }
    void sync_chat_tabs() {
        if (tabs_snapshot != config.at("chat_tabs")) {
            auto next = parse_chat_tabs(config.at("chat_tabs"));
            display_tabs = std::move(next);
            tabs_snapshot = config.at("chat_tabs");
            auto exists = [&](const auto &entry) {
                return std::any_of(display_tabs.begin(), display_tabs.end(),
                                   [&](auto &t) { return t.id == entry.first; });
            };
            std::erase_if(tab_views, [&](auto &entry) { return !exists(entry); });
            std::erase_if(preview_tab_views, [&](auto &entry) { return !exists(entry); });
            std::erase_if(tab_unread, [&](auto &entry) { return !exists(entry); });
        }
        active_tab = 0;
        for (size_t i = 0; i < display_tabs.size(); ++i)
            if (display_tabs[i].id == config.value("active_chat_tab", std::string("main")))
                active_tab = i;
        tab_first = std::clamp(tab_first, 0, int(display_tabs.size()) - 1);
        if (chat_tab_bar) {
            bool visible = display_tabs.size() > 1 || display_tabs.front().id != "main" ||
                           display_tabs.front().name != ui_text("General") &&
                           display_tabs.front().name != ui_text_for_language("General", "zh") &&
                           display_tabs.front().name != "General";
            ShowWindow(chat_tab_bar, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
            InvalidateRect(chat_tab_bar, nullptr, FALSE);
            sync_tab_input_surface();
        }
    }
    std::string tab_caption(size_t index) const {
        const auto &tab = display_tabs.at(index);
        auto caption = tab.name;
        auto unread = tab_unread.find(tab.id);
        if (active_theme.chat.unread_count && unread != tab_unread.end() && unread->second)
            caption += " · " + std::to_string(unread->second);
        return caption;
    }
    void layout_chat_tab_hits(HWND h) {
        tab_hits.clear();
        RECT area{};
        GetClientRect(h, &area);
        auto dc = GetDC(h);
        auto old = SelectObject(dc, ui_font(12, false, h, true));
        std::vector<int> widths;
        int total = 0;
        for (size_t i = 0; i < display_tabs.size(); ++i) {
            auto label = wide(tab_caption(i));
            SIZE size{};
            GetTextExtentPoint32W(dc, label.c_str(), int(label.size()), &size);
            int width = std::clamp(int(size.cx) + scale(18, h), scale(48, h), scale(146, h));
            widths.push_back(width);
            total += width + scale(3, h);
        }
        SelectObject(dc, old);
        ReleaseDC(h, dc);
        bool overflow = total > area.right;
        int arrow = overflow ? scale(18, h) : 0, x = arrow;
        tab_first = overflow ? std::clamp(tab_first, 0, int(display_tabs.size()) - 1) : 0;
        if (overflow)
            tab_hits.push_back({{0, 0, arrow, area.bottom}, tab_first > 0 ? -1 : -4});
        int index = tab_first;
        for (; index < int(display_tabs.size()); ++index) {
            int end = std::min(x + widths[index], int(area.right) - arrow);
            if (end - x < scale(20, h))
                break;
            tab_hits.push_back({{x, 1, end, area.bottom - 1}, index});
            x = end + scale(3, h);
            if (x >= area.right - arrow) {
                ++index;
                break;
            }
        }
        if (overflow)
            tab_hits.push_back({{area.right - arrow, 0, area.right, area.bottom},
                                index < int(display_tabs.size()) ? -2 : -5});
    }
    bool tab_alert(size_t index) const {
        if (index == active_tab || index >= display_tabs.size())
            return false;
        if (message_preview && active_theme.chat.flash_unread)
            return true; // Local visual preview only.
        auto found = tab_unread.find(display_tabs[index].id);
        return found != tab_unread.end() && found->second > 0;
    }
    void sync_tab_flash() {
        bool wanted = active_theme.chat.flash_unread && chat_tab_bar && IsWindowVisible(chat_tab_bar) &&
                      !IsIconic(main);
        if (wanted) {
            wanted = false;
            for (size_t i = 0; i < display_tabs.size(); ++i)
                wanted = wanted || tab_alert(i);
        }
        if (wanted == tab_flash_running)
            return;
        if (wanted) {
            tab_flash_epoch = GetTickCount64();
            tab_flash_running = SetTimer(main, 3, 33, nullptr) != 0;
        } else {
            KillTimer(main, 3);
            tab_flash_running = false;
            if (chat_tab_bar)
                InvalidateRect(chat_tab_bar, nullptr, FALSE);
        }
    }
    void animate_tabs() {
        sync_tab_flash();
        if (!tab_flash_running)
            return;
        for (const auto &hit : tab_hits)
            if (hit.index >= 0 && tab_alert(size_t(hit.index)))
                InvalidateRect(chat_tab_bar, &hit.bounds, FALSE);
    }
    void paint_classic_tab(HDC dc, RECT box, bool selected, bool hover, bool pressed, double flash) {
        // Restore the open, square Classic chat tab: a tiny clipped corner,
        // fading vertical edges and no bottom line or filled title strip.
        const auto &style = active_theme.chat;
        const int stroke = scale(1), bevel = scale(2);
        const int left = box.left + stroke, right = box.right - stroke - 1;
        const int top = box.top + stroke, bottom = box.bottom - 1;
        if (right - left <= 2 * bevel || bottom - top <= bevel)
            return;
        auto blend = [](uint32_t a, uint32_t b, double amount) {
            auto channel = [&](int shift) {
                return BYTE(std::lround(((a >> shift) & 255) * (1 - amount) + ((b >> shift) & 255) * amount));
            };
            return RGB(channel(16), channel(8), channel(0));
        };
        auto tint = selected || hover ? style.glow : style.tab_border;
        auto dc_brush = reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH));
        for (int row = top; row < bottom; ++row) {
            double depth = double(row - top) / std::max(1, bottom - top - 1);
            double glow = std::max(selected && !pressed ? 0.6 : hover ? 0.3 : 0.0, flash);
            auto base = blend(tint, 0, depth * 0.65 + (pressed ? 0.15 : 0));
            auto light = blend(style.glow, 0xffeed0, 0.4);
            uint32_t base_rgb = (GetRValue(base) << 16) | (GetGValue(base) << 8) | GetBValue(base);
            uint32_t light_rgb = (GetRValue(light) << 16) | (GetGValue(light) << 8) | GetBValue(light);
            SetDCBrushColor(dc, blend(base_rgb, light_rgb, glow * (1 - depth * 0.85)));
            int corner = std::max(0, bevel - (row - top));
            RECT start{left + corner, row, left + corner + stroke, row + 1};
            RECT end{right - corner - stroke + 1, row, right - corner + 1, row + 1};
            FillRect(dc, &start, dc_brush);
            FillRect(dc, &end, dc_brush);
            if (row < top + stroke) {
                RECT cap{left + bevel, row, right - bevel + 1, row + 1};
                FillRect(dc, &cap, dc_brush);
            }
        }
    }
    void paint_chat_tab_bar(HWND h, HDC dc) {
        RECT area{};
        GetClientRect(h, &area);
        FillRect(dc, &area, keyed_brush);
        layout_chat_tab_hits(h);
        auto old = SelectObject(dc, ui_font(12, false, h, true));
        SetBkMode(dc, TRANSPARENT);
        for (auto hit : tab_hits) {
            bool selected = hit.index >= 0 && size_t(hit.index) == active_tab;
            auto box = hit.bounds;
            if (active_theme.chat.tabs == ChatTabVisual::Classic && hit.index >= 0) {
                auto phase = tab_alert(size_t(hit.index))
                                 ? chat_flash_alpha(active_theme.chat, GetTickCount64() - tab_flash_epoch)
                                 : 0;
                paint_classic_tab(dc, box, selected, hit.index == tab_hover, hit.index == tab_pressed, phase);
                SetTextColor(dc, rgb(active_theme.chat.tab_text));
            } else {
                rounded(dc, box,
                        rgb(selected                   ? active_theme.colors.selection
                            : hit.index == tab_pressed ? active_theme.colors.pressed
                            : hit.index == tab_hover   ? active_theme.colors.hover
                                                       : active_theme.colors.button),
                        float(scale(std::min(6, active_theme.metrics.control_radius), h)));
                SetTextColor(dc, rgb(hit.index < -2 ? active_theme.colors.disabled
                                     : selected     ? active_theme.colors.selected_text
                                                    : active_skin.text));
            }
            auto caption = hit.index >= 0 ? wide(tab_caption(size_t(hit.index)))
                                          : (hit.index == -1 || hit.index == -4 ? L"‹" : L"›");
            InflateRect(&box, -scale(5, h), 0);
            DrawTextW(dc, caption.c_str(), -1, &box,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        SelectObject(dc, old);
    }
    void switch_chat_tab(size_t index) {
        if (index >= display_tabs.size() || index == active_tab)
            return;
        auto next = config;
        next["active_chat_tab"] = display_tabs[index].id;
        if (!smoke)
            engine->configure(next); // Smoke/UI tests must not persist fixture settings.
        auto &views = message_preview ? preview_tab_views : tab_views;
        views[display_tabs[active_tab].id] = bookmark();
        config = std::move(next);
        active_tab = index;
        if (!message_preview)
            tab_unread[display_tabs[index].id] = 0;
        auto found = views.find(display_tabs[index].id);
        render(found == views.end() ? ChatBookmark{} : found->second);
        layout_chat_tab_hits(chat_tab_bar);
        if (std::none_of(tab_hits.begin(), tab_hits.end(),
                         [&](auto &hit) { return hit.index == int(active_tab); }))
            tab_first = int(active_tab);
        InvalidateRect(chat_tab_bar, nullptr, FALSE);
        chat_wheel_remainder = 0;
        sync_tab_flash();
    }
    std::optional<LRESULT> chat_tab_input(HWND h, UINT msg, WPARAM w, LPARAM l) {
        if (msg == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        if (msg == WM_SETCURSOR && active_theme.chat.tabs == ChatTabVisual::Classic) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        auto hit = [&] {
            layout_chat_tab_hits(chat_tab_bar);
            for (auto &item : tab_hits)
                if (PtInRect(&item.bounds, point))
                    return item.index;
            return -3;
        };
        if (msg == WM_MOUSEMOVE) {
            int next = hit();
            if (next != tab_hover) {
                tab_hover = next;
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0};
                TrackMouseEvent(&track);
                InvalidateRect(chat_tab_bar, nullptr, FALSE);
            }
            return 0;
        }
        if (msg == WM_MOUSELEAVE) {
            tab_hover = -3;
            InvalidateRect(chat_tab_bar, nullptr, FALSE);
            return 0;
        }
        if (msg == WM_LBUTTONDOWN) {
            tab_pressed = hit();
            SetCapture(h);
            InvalidateRect(chat_tab_bar, nullptr, FALSE);
            return 0;
        }
        if (msg == WM_LBUTTONUP) {
            auto pressed = tab_pressed;
            tab_pressed = -3;
            if (GetCapture() == h)
                ReleaseCapture();
            auto index = hit();
            if (index == pressed) {
                if (index >= 0)
                    switch_chat_tab(size_t(index));
                else if (index == -1 || index == -2)
                    tab_first =
                        std::clamp(tab_first + (index == -1 ? -1 : 1), 0, int(display_tabs.size()) - 1);
            }
            InvalidateRect(chat_tab_bar, nullptr, FALSE);
            return 0;
        }
        if (msg == WM_CAPTURECHANGED) {
            tab_pressed = -3;
            InvalidateRect(chat_tab_bar, nullptr, FALSE);
            return 0;
        }
        if (msg == WM_MOUSEWHEEL) {
            tab_first = std::clamp(tab_first - (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1), 0,
                                   int(display_tabs.size()) - 1);
            InvalidateRect(chat_tab_bar, nullptr, FALSE);
            return 0;
        }
        if (msg == WM_RBUTTONUP) {
            open_settings();
            switch_page(5);
            return 0;
        }
        return {};
    }
    void capture_tab_editor() {
        if (tab_sync || !tab_editor || editing_tab >= tab_draft.size())
            return;
        auto candidate = tab_draft;
        auto &tab = candidate[editing_tab];
        tab["name"] = trim(text(tab_name));
        tab["events"] = Json::array();
        for (auto &[h, event] : tab_checks)
            if (SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED)
                tab["events"].push_back(event);
        tab["channels"] = parse_channel_filter(text(tab_channels));
        tab["exclude_channels"] = parse_channel_filter(text(tab_excluded));
        parse_chat_tabs(candidate);
        tab_draft = std::move(candidate);
    }
    void fill_tab_editor(size_t index) {
        if (index >= tab_draft.size())
            index = 0;
        editing_tab = index;
        tab_sync = true;
        SendMessageW(tab_editor, CB_RESETCONTENT, 0, 0);
        for (auto &tab : tab_draft) {
            auto label = wide(tab.at("name").get<std::string>());
            SendMessageW(tab_editor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(tab_editor, CB_SETCURSEL, index, 0);
        auto &tab = tab_draft[index];
        set(tab_name, tab.at("name").get<std::string>());
        for (auto &[h, event] : tab_checks)
            SendMessageW(h, BM_SETCHECK,
                         std::find(tab["events"].begin(), tab["events"].end(), event) != tab["events"].end()
                             ? BST_CHECKED
                             : BST_UNCHECKED,
                         0);
        set(tab_channels, format_channel_filter(tab.value("channels", Json::array())));
        set(tab_excluded, format_channel_filter(tab.value("exclude_channels", Json::array())));
        EnableWindow(GetDlgItem(body, id_tab_delete), tab_draft.size() > 1);
        tab_sync = false;
    }
    void select_tab_editor() {
        if (tab_sync)
            return;
        int index = int(SendMessageW(tab_editor, CB_GETCURSEL, 0, 0));
        try {
            capture_tab_editor();
            fill_tab_editor(size_t(std::max(0, index)));
        } catch (...) {
            SendMessageW(tab_editor, CB_SETCURSEL, editing_tab, 0);
            throw;
        }
    }
    void add_tab() {
        capture_tab_editor();
        if (tab_draft.size() >= max_chat_tabs)
            throw std::runtime_error("Up to 16 chat tabs");
        tab_draft.push_back(new_chat_tab("tab-" + sha256(random_bytes(16)).substr(0, 16),
                                         "New tab" + std::to_string(tab_draft.size() + 1)));
        fill_tab_editor(tab_draft.size() - 1);
        set(settings_status, "Edit name and message type, then click Save settings");
    }
    void delete_tab() {
        if (tab_draft.size() <= 1)
            throw std::runtime_error("Keep at least one chat tab");
        tab_draft.erase(tab_draft.begin() + editing_tab);
        fill_tab_editor(std::min(editing_tab, tab_draft.size() - 1));
    }
    bool apply_game_tabs(const Json &layout) {
        // Use committed settings, never pending provider/hotkey/appearance editor contents.
        auto next = synchronize_chat_layout(config, layout, [this](const Json &candidate) {
            if (!smoke)
                engine->configure(candidate);
        });
        bool changed = next != config;
        auto old_id = display_tabs.at(active_tab).id;
        auto view = bookmark();
        bool cache_changed = game_layout != layout;
        config = std::move(next); // Persistence has succeeded before the visible model changes.
        game_layout = layout;
        std::string note = changed ? "Game chat windows synced and saved:" : "Game chat windows already match:";
        note += std::to_string(config["chat_tabs"].size()) + "tabs";
        try {
            sync_chat_tabs();
            tab_draft = config["chat_tabs"];
            if (settings && tab_editor)
                fill_tab_editor(active_tab);
            if (changed)
                render(display_tabs.at(active_tab).id == old_id ? view : ChatBookmark{});
            sync_tab_flash();
        } catch (const std::exception &e) {
            note += std::string("; settings saved, UI refresh failed:") + e.what();
        }
        if (cache_changed && !smoke) {
            try {
                write_json(platform::app_data() / "game-chat-layout.json", game_layout);
            } catch (const std::exception &e) {
                note += std::string("; recent export cache failed:") + e.what();
            }
        }
        status_note = std::move(note);
        if (settings_status)
            set(settings_status, status_note);
        return changed;
    }
    void import_game_tabs() {
        if (game_layout.is_null())
            throw std::runtime_error("Run /bl windows in-game first; the receiver will auto-sync and save once complete");
        apply_game_tabs(game_layout);
    }
    bool receive_chat(DisplayMessage value) {
        std::array<bool, max_chat_tabs> matches{};
        bool any = false;
        for (size_t i = 0; i < display_tabs.size(); ++i) {
            matches[i] = display_tabs[i].matches(value.message);
            any = any || matches[i];
        }
        if (!any)
            return false;
        size_t limit = size_t(config.value("max_messages", 200));
        size_t removed = messages.size() + 1 > limit ? messages.size() + 1 - limit : 0;
        bool lost_visible = false;
        for (size_t i = 0; i < std::min(removed, messages.size()); ++i)
            lost_visible = lost_visible || display_tabs[active_tab].matches(messages[i].message);
        auto change = update_history(messages, std::move(value), limit);
        if (!change.retained)
            return false;
        bool redraw = matches[active_tab] || lost_visible;
        if (!message_preview && !redraw) {
            for (auto &row : chat_rows) {
                size_t shifted = row.index + (row.index >= change.position ? 1 : 0);
                if (shifted < change.removed) {
                    redraw = true;
                    break;
                }
                row.index = shifted - change.removed;
            }
        }
        for (size_t i = 0; i < display_tabs.size(); ++i)
            if ((i != active_tab || message_preview) && matches[i])
                tab_unread[display_tabs[i].id] = std::min(9999u, tab_unread[display_tabs[i].id] + 1);
        if (chat_tab_bar)
            InvalidateRect(chat_tab_bar, nullptr, FALSE);
        sync_tab_flash();
        return redraw;
    }
    void toggle_message_preview() {
        if (!message_preview) {
            auto samples = chat_preview_messages(uint32_t(std::time(nullptr)));
            preview_live_view = bookmark();
            tab_views[display_tabs.at(active_tab).id] = *preview_live_view;
            preview_tab_views.clear();
            preview_messages = std::move(samples);
            message_preview = true;
            render(ChatBookmark{});
            ShowWindow(main, SW_SHOWNOACTIVATE);
            sync_background();
        } else {
            message_preview = false;
            preview_messages.clear();
            tab_unread[display_tabs.at(active_tab).id] = 0;
            if (chat_tab_bar)
                InvalidateRect(chat_tab_bar, nullptr, FALSE);
            auto current = tab_views.find(display_tabs.at(active_tab).id);
            render(current == tab_views.end() ? ChatBookmark{} : current->second);
            preview_tab_views.clear();
            preview_live_view.reset();
        }
        if (settings) {
            set(GetDlgItem(body, id_chat_preview), message_preview ? "Exit test preview" : "Show test message");
            set(settings_status, message_preview
                                     ? "Test preview: style only, scrollable, not used for replies; no API requests, no logs"
                                     : "Returned to real chat");
        }
    }
    void render(std::optional<ChatBookmark> restore = {}) {
#ifdef BL_UI_TESTS
        ++chat_render_calls;
#endif
        auto view = restore ? *restore : bookmark();
        auto &[scroll, anchor_pos, bottom, anchor] = view;
        const auto &visible_messages = message_preview ? preview_messages : messages;
        SendMessageW(chat, WM_SETREDRAW, FALSE, 0);
        SetWindowTextW(chat, L"");
        chat_rows.clear();
        for (size_t message_index = 0; message_index < visible_messages.size(); ++message_index) {
            const auto &item = visible_messages[message_index];
            const auto &m = item.message;
            if (!display_tabs.at(active_tab).matches(m) || !item.error.empty() || ui_blocks.blocks(m)) {
                continue;
            }
            if (!chat_rows.empty())
                append("\n", fg);
            long row_start = chat_length();
            if (message_preview)
                append(ui_text("[Test]"), accent);
            if (m.replay_batch)
                append(ui_text("[Resend]"), accent);
            if (config.value("show_time", false) && m.sent) {
                std::time_t t = m.sent;
                std::tm tm{};
                localtime_s(&tm, &t);
                char stamp[16];
                std::strftime(stamp, sizeof(stamp), "[%H:%M] ", &tm);
                append(stamp, mix(fg, panel));
            }
            auto channel = chat_color(m);
            append(chat_channel_label(m), rgb(channel));
            auto name = player_display_name(m);
            auto meta = player_badges(m, config);
            if (!name.empty())
                append(meta + "[" + name + "]: ",
                       config.value("class_colors", true) && !m.player_class.empty()
                           ? player_color(m.player_class)
                           : rgb(channel));
            append_chat(item.translation, channel);
            if (config.value("show_original", false))
                append_chat("  " + m.text, channel);
            chat_rows.push_back({row_start, chat_length(), message_index, m.session, m.seq});
        }
        // RichEdit only settles its viewport after redraw/layout is enabled.
        // Scrolling before this leaves the recreated document at its first line.
        SendMessageW(chat, WM_SETREDRAW, TRUE, 0);
        if (bottom) {
            SendMessageW(chat, EM_SETSEL, WPARAM(-1), LPARAM(-1));
            SendMessageW(chat, WM_VSCROLL, SB_BOTTOM, 0);
            SendMessageW(chat, EM_SCROLLCARET, 0, 0);
        } else {
            // Keep the same message at the same pixel offset when older replay
            // messages are inserted above the reader's current viewport.
            if (anchor)
                for (const auto &row : chat_rows)
                    if (anchor == std::pair{row.session, row.seq}) {
                        POINT current{};
                        SendMessageW(chat, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&current), row.start);
                        POINT position{};
                        SendMessageW(chat, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&position));
                        scroll.y = position.y + current.y - anchor_pos.y;
                        break;
                    }
            SendMessageW(chat, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        }
        InvalidateRect(chat, nullptr, FALSE);
        update_scrollbar();
        refresh_reply_surface();
    }
#ifdef BL_UI_TESTS
    void verify_bridge_ui() {
        auto require = [](bool ok, const std::string &why) {
            if (!ok)
                throw std::runtime_error(why);
        };
        config["ui_style"] = "ios";
        COLORREF key = 0;
        BYTE alpha = 0;
        DWORD flags = 0;
        require(std::abs(background_opacity(default_settings()) - 0.30) < 0.001, "Default background transparency is not 70 percent");
        auto initial = initial_overlay_rect(RECT{0, 0, 1920, 1080}, SIZE{460, 240}, SIZE{900, 700});
        require(initial.right == 510 && initial.bottom == 890,
                "Initial overlay does not align with centered settings' lower-left corner");
        for (const auto &work : {RECT{0, 0, 1280, 720}, RECT{-1920, -200, 0, 880}, RECT{0, 0, 800, 600}}) {
            auto placed = initial_overlay_rect(work, SIZE{460, 240}, SIZE{900, 700});
            require(placed.left >= work.left && placed.top >= work.top && placed.right <= work.right &&
                        placed.bottom <= work.bottom,
                    "Initial overlay fell outside the monitor work area");
        }

        auto font_size = config.value("font_size", 14);
        config["click_through"] = false;
        for (double opacity : {0.0, 0.25, 0.75, 1.0}) {
            config["background_opacity"] = opacity;
            apply();
            require(GetLayeredWindowAttributes(main, &key, &alpha, &flags) && key == transparency_key &&
                        (flags & LWA_COLORKEY) && !(flags & LWA_ALPHA),
                    "Background slider faded chat text");
            require(GetLayeredWindowAttributes(backdrop, &key, &alpha, &flags) && flags == LWA_ALPHA &&
                        alpha == BYTE(std::lround(opacity * 255)),
                    "Background alpha did not follow setting");
            require(IsWindowVisible(main), "Background setting hid the chat window");
            require(bool(IsWindowVisible(backdrop)) == (opacity > 0),
                    "Background layer visibility incorrect");
        }
        require(GetWindow(main, GW_OWNER) == backdrop && IsWindowVisible(gear) && !GetDlgItem(main, 27),
                "Unexpected extra drag grip or missing gear/background");
        config["background_opacity"] = 0.0;
        apply();
        RedrawWindow(main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        RECT gr{};
        GetWindowRect(gear, &gr);
        HDC gripDC = GetDC(gear);
        COLORREF gripPixel = GetPixel(gripDC, scale(2), scale(2));
        ReleaseDC(gear, gripDC);
        require(gripPixel == transparency_key && IsWindowVisible(gear_input_surface),
                "Transparent settings button lost its independent mouse input surface");
        bool drag_forwarded = false;
        auto intercept_drag = [](HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) -> LRESULT {
            if (msg == WM_NCLBUTTONDOWN) {
                *reinterpret_cast<bool *>(data) = w == HTCAPTION;
                return 0; // Verify dispatch without entering the OS move loop.
            }
            return DefSubclassProc(h, msg, w, l);
        };
        SetWindowSubclass(main, intercept_drag, 998, reinterpret_cast<DWORD_PTR>(&drag_forwarded));
        SendMessageW(gear, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(scale(10), scale(8)));
        SendMessageW(gear, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(scale(20), scale(16)));
        SendMessageW(gear, WM_LBUTTONUP, 0, MAKELPARAM(scale(20), scale(16)));
        RemoveWindowSubclass(main, intercept_drag, 998);
        require(drag_forwarded && !IsWindowVisible(settings),
                "Dragging gear failed or accidentally opened settings");
        SendMessageW(gear, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(scale(10), scale(8)));
        SendMessageW(gear, WM_LBUTTONUP, 0, MAKELPARAM(scale(10), scale(8)));
        require(IsWindowVisible(settings), "Clicking gear no longer opens settings");
        set(settings_status, "Could not identify this Mac");
        require(text(settings_status) == notification_text("Could not identify this Mac"),
                "The UI error display ignored the notification language policy");
        const auto original_choice = SendMessageW(interface_language, CB_GETCURSEL, 0, 0);
        const auto original_language = ui_language();
        const auto original_incoming = config.at("incoming_prompt").get<std::string>();
        const auto original_target = config.at("target_language");
        const auto original_outgoing = config.at("target");
        const auto original_connections = engine->connections();
        auto incoming_edit = provider_edit("incoming_prompt");
        require(incoming_edit && SendMessageW(interface_language, CB_GETCOUNT, 0, 0) == 10,
                "UI language selector or prompt editor missing");
        const auto alternate_choice = original_language == "ja" ? 4 : 3; // Korean / Japanese
        set(incoming_edit, "custom {language} instructions retained during UI changes");
        SendMessageW(interface_language, CB_SETCURSEL, alternate_choice, 0);
        SendMessageW(settings, WM_COMMAND, MAKEWPARAM(id_save, BN_CLICKED), 0);
        const auto expected_language = std::string(ui_languages[size_t(alternate_choice - 1)].code);
        require(ui_language() == expected_language && config.at("ui_language") == expected_language &&
                    text(GetDlgItem(settings, id_save)) == ui_text("Save"),
                "Saving the UI language did not refresh existing controls");
        require(config.at("target_language") == original_target && config.at("target") == original_outgoing &&
                    engine->connections() == original_connections &&
                    text(incoming_edit) == "custom {language} instructions retained during UI changes",
                "Changing UI language altered translation targets, credentials or custom prompt text");
        require(Engine(settings_data_directory()).settings().at("ui_language") == expected_language,
                "UI language was not persisted for restart");
        set(incoming_edit, original_incoming);
        SendMessageW(interface_language, CB_SETCURSEL, original_choice, 0);
        SendMessageW(settings, WM_COMMAND, MAKEWPARAM(id_save, BN_CLICKED), 0);
        require(ui_language() == original_language, "Following system / original language was not restored");

        ShowWindow(settings, SW_HIDE);
        config["background_opacity"] = 0.5;
        apply();
        RECT previous{};
        GetWindowRect(main, &previous);
        SetWindowPos(main, nullptr, previous.left + 12, previous.top + 8, previous.right - previous.left + 18,
                     previous.bottom - previous.top + 12, SWP_NOACTIVATE | SWP_NOZORDER);
        RECT front{}, back{};
        GetWindowRect(main, &front);
        GetWindowRect(backdrop, &back);
        require(EqualRect(&front, &back), "Background did not follow window movement and resizing");
        ShowWindow(main, SW_HIDE);
        require(!IsWindowVisible(backdrop), "Hidden chat left a detached background");
        ShowWindow(main, SW_SHOWNOACTIVATE);
        require(IsWindowVisible(backdrop), "Restored chat did not restore background");
        config["background_opacity"] = 0.0;
        apply();
        LOGFONTW face{};
        GetObjectW(chat_font, sizeof(face), &face);
        require(std::wstring(face.lfFaceName) == L"Microsoft YaHei UI" &&
                    config.value("font_size", 14) == font_size,
                "Background toggle changed the user's font");
        config["show_original"] = false;
        SetWindowPos(main, nullptr, 0, 0, scale(480), scale(240), SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
        RECT outer{}, client{};
        GetWindowRect(main, &outer);
        GetClientRect(main, &client);
        POINT origin{};
        ClientToScreen(main, &origin);
        require(origin.x == outer.left && origin.y == outer.top && client.right == outer.right - outer.left &&
                    client.bottom == outer.bottom - outer.top,
                "Chat popup still has a system-painted nonclient frame");
        auto hit = [&](int x, int y) {
            POINT point{x, y};
            ClientToScreen(main, &point);
            return SendMessageW(main, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y));
        };
        const int w = client.right, h = client.bottom;
        require(hit(1, 1) == HTTOPLEFT && hit(w - 2, 1) == HTTOPRIGHT && hit(1, h - 2) == HTBOTTOMLEFT &&
                    hit(w - 2, h - 2) == HTBOTTOMRIGHT && hit(w / 2, 1) == HTTOP &&
                    hit(w / 2, h - 2) == HTBOTTOM && hit(1, h / 2) == HTLEFT && hit(w - 2, h / 2) == HTRIGHT,
                "Borderless chat popup lost edge/corner resizing");
        require(hit(scale(40), scale(12)) == HTCAPTION && hit(w - scale(20), scale(12)) == HTCLIENT,
                "Borderless chat popup lost dragging or gear interaction");
        messages.clear();
        DisplayMessage sample;
        sample.message.event = "CHAT_MSG_PARTY";
        sample.message.chat_rgb = 0x123456;
        sample.translation = "|cff0070dd|Hitem:1|h[Blue Item]|h|r free";
        messages.push_back(sample);
        render();
        auto rendered = wide(text(chat));
        auto color_at = [&](std::wstring_view token) {
            auto pos = rendered.find(token);
            require(pos != rendered.npos, "Chat color test label missing");
            SendMessageW(chat, EM_SETSEL, pos, pos + 1);
            CHARFORMAT2W format{};
            format.cbSize = sizeof(format);
            SendMessageW(chat, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
            return format.crTextColor;
        };
        require(color_at(L"[Blue Item]") == rgb(0x0070dd) && color_at(L"free") == rgb(0x123456) &&
                    color_at(wide("[" + ui_text("Party") + "]")) == rgb(0x123456),
                "RichEdit lost exact item color or native channel color");
        open_settings();
        int numeric_left = -1, previous_top = -1, numeric_rows = 0;
        for (auto setting_key : {"__background_transparency", "__font", "__limit"}) {
            auto found = std::find_if(edits.begin(), edits.end(),
                                      [&](const Control &c) { return c.key == setting_key; });
            require(found != edits.end(), "Appearance numeric row missing");
            RECT r{};
            GetWindowRect(found->h, &r);
            if (numeric_left < 0)
                numeric_left = r.left;
            require(r.left == numeric_left && r.top > previous_top,
                    "Appearance rows are not aligned vertically");
            previous_top = r.top;
            ++numeric_rows;
        }
        require(numeric_rows == 3 && std::none_of(edits.begin(), edits.end(),
                                                  [](const Control &c) { return c.key == "__opacity"; }),
                "Whole-window opacity control still exists");
        for (int page = 0; page < int(pages.size()); ++page) {
            SendMessageW(settings, WM_COMMAND, MAKEWPARAM(id_nav0 + page, BN_CLICKED),
                         reinterpret_cast<LPARAM>(navigation[page]));
            require(active_page == page && text(page_title).ends_with(ui_text(page_titles[page])),
                    "Sidebar navigation selected the wrong page: " + text(page_title) + " vs " + ui_text(page_titles[page]));
            auto cards = group_cards();
            verify_settings_page_layout();
            for (size_t i = 0; i < cards.size(); ++i)
                for (size_t j = i + 1; j < cards.size(); ++j) {
                    auto a = cards[i], b = cards[j];
                    require(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y,
                            "Settings groups overlap and create an uneven seam");
                }
            for (int other = 0; other < int(pages.size()); ++other)
                if (other != page)
                    for (HWND page_control : pages[other])
                        require(!IsWindowVisible(page_control), "Sidebar exposed another page's controls");
            if (body_max_scroll > 0) {
                scroll_body(body_max_scroll);
                require(body_scroll == body_max_scroll, "Settings viewport cannot reach the end");
                scroll_body(-body_max_scroll);
                require(body_scroll == 0, "Settings viewport lost its original position");
            }
        }
        switch_page(0);
        const auto first_card = group_cards().at(0), second_card = group_cards().at(1);
        const int seam_y = first_card.y + first_card.h;
        RECT body_area{};
        GetClientRect(body, &body_area);
        auto body_dc = GetDC(body), body_memory = CreateCompatibleDC(body_dc);
        auto body_bitmap = CreateCompatibleBitmap(body_dc, body_area.right, body_area.bottom);
        auto old_body = SelectObject(body_memory, body_bitmap);
        SendMessageW(body, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(body_memory), PRF_CLIENT);
        require(GetPixel(body_memory, scale(120, body), scale(seam_y - 4, body)) == panel &&
                    GetPixel(body_memory, scale(120, body), scale(seam_y + 6, body)) == bg &&
                    GetPixel(body_memory, scale(120, body), scale(second_card.y + 4, body)) == panel,
                "Parent card geometry does not align with child backgrounds");
        SelectObject(body_memory, old_body);
        DeleteObject(body_bitmap);
        DeleteDC(body_memory);
        ReleaseDC(body, body_dc);
        auto bg_probe = control(body, L"STATIC", "", 100, seam_y - 8, 120, 32, SS_LEFT);
        HDC probe_dc = GetDC(bg_probe), probe_memory = CreateCompatibleDC(probe_dc);
        HBITMAP probe_bitmap = CreateCompatibleBitmap(probe_dc, scale(120, body), scale(32, body));
        auto old_probe = SelectObject(probe_memory, probe_bitmap);
        SendMessageW(bg_probe, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(probe_memory),
                     PRF_CLIENT | PRF_ERASEBKGND);
        require(GetPixel(probe_memory, scale(20, body), scale(4, body)) == panel &&
                    GetPixel(probe_memory, scale(20, body), scale(14, body)) == bg &&
                    GetPixel(probe_memory, scale(20, body), scale(27, body)) == panel,
                "Child control creates a stray color block across a card edge");
        SelectObject(probe_memory, old_probe);
        DeleteObject(probe_bitmap);
        DeleteDC(probe_memory);
        ReleaseDC(bg_probe, probe_dc);
        DestroyWindow(bg_probe);
        verify_settings_layout_variants();
        auto automatic =
            std::find_if(checks.begin(), checks.end(), [](const Control &c) { return c.key == "auto"; });
        require(automatic != checks.end(), "Automatic translation switch missing");
        auto initialCheck = SendMessageW(automatic->h, BM_GETCHECK, 0, 0);
        SendMessageW(automatic->h, BM_CLICK, 0, 0);
        require(SendMessageW(automatic->h, BM_GETCHECK, 0, 0) != initialCheck,
                "Custom switch no longer toggles");
        SendMessageW(automatic->h, BM_SETCHECK, initialCheck, 0);
        RECT hostBounds{}, saveBounds{}, headingBounds{};
        GetWindowRect(body, &hostBounds);
        GetWindowRect(GetDlgItem(settings, id_save), &saveBounds);
        GetWindowRect(page_description, &headingBounds);
        require(hostBounds.top >= headingBounds.bottom && hostBounds.bottom < saveBounds.top,
                "Settings content overlaps header/footer");
        require(number_rows.size() == 3, "Slider rows missing");
        for (const auto &n : number_rows) {
            set(n.edit, std::to_string(n.minimum));
            require(SendMessageW(n.slider, TBM_GETPOS, 0, 0) == n.minimum,
                    "Number edit did not update slider");
            SendMessageW(n.slider, TBM_SETPOS, TRUE, n.maximum);
            SendMessageW(settings, WM_HSCROLL, SB_THUMBTRACK, reinterpret_cast<LPARAM>(n.slider));
            require(text(n.edit) == std::to_string(n.maximum), "Slider did not update numeric input");
            set(n.edit, "abc");
            require(SendMessageW(n.slider, TBM_GETPOS, 0, 0) == n.maximum, "Invalid input changed slider");
            bool rejected = false;
            try {
                number_value(n);
            } catch (const std::exception &) {
                rejected = true;
            }
            require(rejected, "Invalid numeric input accepted");
            RECT sliderRect{};
            GetClientRect(n.slider, &sliderRect);
            SendMessageW(n.slider, WM_LBUTTONDOWN, MK_LBUTTON,
                         MAKELPARAM(sliderRect.right - 1, sliderRect.bottom / 2));
            SendMessageW(n.slider, WM_LBUTTONUP, 0, MAKELPARAM(sliderRect.right - 1, sliderRect.bottom / 2));
            require(text(n.edit) == std::to_string(n.maximum), "Slider click does not seek directly");
            set(n.edit, std::to_string(n.minimum));
        }
        require(std::none_of(checks.begin(), checks.end(),
                             [](const Control &c) {
                                 return c.key == "show_realm" || c.key == "show_player_name" ||
                                        c.key == "show_localized_class" || c.key == "show_english_class" ||
                                        c.key == "show_english_race";
                             }),
                "Removed metadata display controls still present");
        for (size_t i = 0; i < chat_targets.size(); ++i)
            require(narrow(picker_text(chat_target, int(i))) == ui_text(target_language_name(chat_targets[i])),
                    "Target-language caption does not follow the application UI language");
        require(chat_target && SendMessageW(chat_target, CB_GETCOUNT, 0, 0) == int(chat_targets.size()) &&
                    SendMessageW(chat_target, CB_GETCURSEL, 0, 0) >= 0,
                "Chat target selector is missing a saved language");
        for (auto selector : {design_select, skin_select, prompt_style, output_style, target, chat_target}) {
            if (!selector)
                continue;
            SendMessageW(selector, CB_SHOWDROPDOWN, TRUE, 0);
            require(picker && picker_combo == selector && SendMessageW(selector, CB_GETDROPPEDSTATE, 0, 0),
                    "Custom popup did not open");
            auto original = SendMessageW(selector, CB_GETCURSEL, 0, 0);
            SendMessageW(selector, WM_KEYDOWN, VK_END, 0);
            SendMessageW(selector, WM_KEYDOWN, VK_ESCAPE, 0);
            require(!picker && SendMessageW(selector, CB_GETCURSEL, 0, 0) == original,
                    "Cancel changed selector value or left popup");
        }
        SendMessageW(design_select, CB_SHOWDROPDOWN, TRUE, 0);
        SendMessageW(design_select, WM_KEYDOWN, VK_HOME, 0);
        SendMessageW(design_select, WM_KEYDOWN, VK_DOWN, 0);
        SendMessageW(design_select, WM_KEYDOWN, VK_RETURN, 0);
        require(terminal() && bg == RGB(0, 0, 0) && panel == RGB(0, 0, 0) && skin_choices.size() == 4,
                "Terminal design did not apply");
        LOGFONTW terminalFace{};
        GetObjectW(reinterpret_cast<HFONT>(SendMessageW(number_rows[0].edit, WM_GETFONT, 0, 0)),
                   sizeof(terminalFace), &terminalFace);
        require(std::wstring(terminalFace.lfFaceName) == L"Consolas", "Terminal font not applied");
        SendMessageW(skin_select, CB_SETCURSEL, 1, 0);
        select_skin();
        require(accent == rgb(0xffb454), "Terminal amber palette ignored");
        require(!message_preview, "Live palette regression must use normal chat");
        auto live_palette_renders = chat_render_calls;
        SendMessageW(skin_select, CB_SETCURSEL, 0, 0);
        select_skin();
        require(chat_render_calls == live_palette_renders + 1,
                "Palette change did not recolor live chat without restarting");
        SendMessageW(skin_select, CB_SETCURSEL, 1, 0);
        select_skin();
        SendMessageW(design_select, CB_SETCURSEL, 0, 0);
        select_design();
        require(!terminal() && skin_choices.size() == 5, "iOS design not restored");
        SendMessageW(design_select, CB_SETCURSEL, 1, 0);
        select_design();
        require(accent == rgb(0xffb454), "Family palette memory lost");
        restore_appearance();
        SendMessageW(skin_select, CB_SHOWDROPDOWN, TRUE, 0);
        SendMessageW(picker, WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
        require(picker != nullptr, "Initial selector release dismissed popup");
        int choiceY = scale(6, picker) + picker_row_height + picker_row_height / 2;
        SendMessageW(picker, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(scale(20, picker), choiceY));
        SendMessageW(picker, WM_LBUTTONUP, 0, MAKELPARAM(scale(20, picker), choiceY));
        require(!picker && SendMessageW(skin_select, CB_GETCURSEL, 0, 0) == 1 && bg == rgb(0xf2f2f7),
                "Mouse popup selection did not apply paper palette");
        SendMessageW(skin_select, CB_SHOWDROPDOWN, TRUE, 0);
        SendMessageW(picker, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(-10, -10));
        require(!picker, "Outside click did not dismiss picker");
        restore_appearance();
        SendMessageW(skin_select, CB_SETCURSEL, 3, 0);
        SendMessageW(settings, WM_COMMAND, MAKEWPARAM(id_skin, CBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(skin_select));
        require(text(color_edit(0)) == "#26211C" && SendMessageW(skin_select, CB_GETCURSEL, 0, 0) == 3,
                "Skin preset did not populate color controls");
        set(color_edit(0), "#FFFFFF");
        require(SendMessageW(skin_select, CB_GETCURSEL, 0, 0) == LRESULT(skin_choices.size() - 1) &&
                    draft_color(0) == RGB(255, 255, 255),
                "Manual color input did not choose custom skin");
        appearance_preview.reset();
        auto original_skin = config["skin"], original_colors = config["skin_colors"];
        config["skin"] = "custom";
        config["skin_colors"] = {
            {"background", "#FFFFFF"}, {"surface", "#202020"}, {"text", "#EEEEEE"}, {"accent", "#C09060"}};
        apply();
        render();
        require(bg == RGB(255, 255, 255) && panel == RGB(32, 32, 32) && accent == RGB(192, 144, 96),
                "Custom palette was ignored");
        require(GetLayeredWindowAttributes(main, &key, &alpha, &flags) && key == transparency_key &&
                    !(flags & LWA_ALPHA),
                "Light custom skin changed the text transparency key or alpha");
        config["skin"] = original_skin;
        config["skin_colors"] = original_colors;
        apply();
        ShowWindow(settings, SW_HIDE);
        messages.clear();
        for (int i = 1; i <= 80; ++i) {
            DisplayMessage d;
            d.message.session = 1;
            d.message.seq = i;
            d.message.sent = 1000 + i;
            d.message.event = "CHAT_MSG_WHISPER";
            d.message.author = "Player" + std::to_string(i) + "-Realm";
            d.message.text = "hello";
            d.translation = "test line " + std::to_string(i);
            insert_history(messages, std::move(d), 200);
        }
        render();
        require(scroll_state().bottom, "Initial history did not follow the latest message");
        const auto rendered_history = text(chat);
        require(!rendered_history.empty() && rendered_history.back() != '\n' &&
                    rendered_history.back() != '\r',
                "Chat history ended with an empty line");
        require(!(GetWindowLongPtrW(chat, GWL_STYLE) & WS_VSCROLL),
                "Native white scrollbar is still present");
        require(text(chat).find("↪") == std::string::npos, "Reply arrow is still visible");
        for (int i = 81; i <= 100; ++i) {
            DisplayMessage d;
            d.message.session = 1;
            d.message.seq = i;
            d.message.sent = 1000 + i;
            d.message.event = "CHAT_MSG_PARTY";
            d.message.author = "NewPlayer-Realm";
            d.message.text = "hello";
            d.translation = std::string(150, 'x') + " latest message " + std::to_string(i);
            insert_history(messages, std::move(d), 200);
            render();
            require(scroll_state().bottom, "New wrapped message jumped away from the bottom");
            require(SendMessageW(chat, EM_GETFIRSTVISIBLELINE, 0, 0) > 0,
                    "New message reset scroll to the first line");
        }
        POINT desired{0, 260};
        SendMessageW(chat, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&desired));
        auto firstLine = SendMessageW(chat, EM_GETFIRSTVISIBLELINE, 0, 0);
        auto character = long(SendMessageW(chat, EM_LINEINDEX, firstLine, 0));
        auto old = std::find_if(chat_rows.begin(), chat_rows.end(),
                                [&](const ChatRow &row) { return row.end > character; });
        require(old != chat_rows.end() && old->index > 0, "UI test did not scroll above the bottom");
        auto identity = old->seq;
        POINT before{};
        SendMessageW(chat, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&before), old->start);
        DisplayMessage replay;
        replay.message.session = 1;
        replay.message.seq = 0;
        replay.message.sent = 999;
        replay.message.replay_batch = 1;
        replay.message.event = "CHAT_MSG_SAY";
        replay.translation = "older manual replay";
        insert_history(messages, std::move(replay), 200);
        render();
        auto current = std::find_if(chat_rows.begin(), chat_rows.end(),
                                    [&](const ChatRow &row) { return row.seq == identity; });
        require(current != chat_rows.end(), "Scroll anchor message lost");
        POINT after{};
        SendMessageW(chat, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&after), current->start);
        require(std::abs(after.y - before.y) <= 2, "Manual replay changed the reader scroll anchor");
        require(text(chat).find(ui_text("[Resend]")) != std::string::npos, "Replay badge missing in native RichEdit");
        pick_reply(current->start);
        require(chosen_reply && chosen_reply->prefix == "/w Player" + std::to_string(identity) + "-Realm ",
                "Sorted history left a stale click-to-reply mapping");
        auto party = std::find_if(chat_rows.begin(), chat_rows.end(), [&](const ChatRow &row) {
            return messages[row.index].message.event == "CHAT_MSG_PARTY";
        });
        require(party != chat_rows.end(), "Party reply test row missing");
        pick_reply(party->start);
        require(chosen_reply && chosen_reply->prefix == "/p ",
                "Left click did not retain source-channel reply");
        pick_reply(party->start, true);
        require(chosen_reply && chosen_reply->prefix == "/w NewPlayer-Realm ",
                "Explicit whisper action did not select the player");
        SendMessageW(chat, WM_VSCROLL, SB_BOTTOM, 0);
        auto last = chat_rows.back();
        POINT at{};
        SendMessageW(chat, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&at), last.start);
        LPARAM point = MAKELPARAM(at.x + scale(3), at.y + scroll_state().line_height / 2);
        SendMessageW(chat, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(chat, WM_LBUTTONUP, 0, point);
        require(chosen_reply && chosen_reply->prefix == "/p ", "Actual left-button path lost channel reply");
        hide_output();
        auto usage_before_menu = engine->usage();
        SendMessageW(chat, WM_RBUTTONDOWN, MK_RBUTTON, point);
        SendMessageW(chat, WM_RBUTTONUP, 0, point);
        require(message_menu && context_message && GetMenuItemCount(message_menu) == 3 && !IsWindowVisible(output),
                "Right click should open the three-action menu without starting a whisper");
        auto context_saved = messages;
        DisplayMessage inserted;
        inserted.message = messages.back().message;
        inserted.message.author = "Different-Realm";
        inserted.message.seq += 10000;
        inserted.message.sent -= 100;
        insert_history(messages, std::move(inserted), 1000);
        render();
        message_action(1);
        require(chosen_reply && chosen_reply->prefix == "/w NewPlayer-Realm ",
                "An asynchronous history insert retargeted the context menu player");
        require(GetFocus() == input, "Context menu whisper action did not focus the input");
        messages = std::move(context_saved); render();
        hide_output();
        show_message_menu(last.start, {}); message_action(2);
        require(prepared_who == "/who NewPlayer" && engine->usage() == usage_before_menu,
                "Level query should prepare /who without a translation request");
        show_message_menu(last.start, {}); message_action(3);
        require(engine->player_blocked(messages.back().message) && text(chat).find("NewPlayer") == std::string::npos,
                "Context menu block did not hide existing messages immediately");
        open_block_window();
        auto verify_block_layout = [&] {
          RECT block_client{}; GetClientRect(block_window, &block_client);
          auto block_dc = GetDC(block_window);
          for (HWND child = GetWindow(block_window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            RECT area{}; GetWindowRect(child, &area);
            MapWindowPoints(nullptr, block_window, reinterpret_cast<POINT *>(&area), 2);
            require(area.left >= scale(16, block_window) && area.right <= block_client.right - scale(16, block_window) &&
                        area.top >= scale(12, block_window) && area.bottom <= block_client.bottom - scale(16, block_window),
                    "Blocklist manager control extends beyond its padding");
            auto measured = settings_caption_height(child, MulDiv(area.right - area.left, 96, int(GetDpiForWindow(block_window))), block_dc);
            require(scale(measured, block_window) <= area.bottom - area.top,
                    "Localized blocklist manager caption is clipped: " + text(child));
          }
          ReleaseDC(block_window, block_dc);
        };
        verify_block_layout();
        // Exercise taller fallback metrics and wrapping, even on a developer
        // desktop whose installed CJK font happens to fit the old fixed rows.
        RECT old_block_bounds{}; GetWindowRect(block_window, &old_block_bounds);
        const auto old_interface_size = active_theme.fonts.size;
        active_theme.fonts.size += 7;
        refresh_interface_fonts();
        verify_block_layout();
        SetWindowPos(block_window, nullptr, 0, 0, scale(620, block_window), old_block_bounds.bottom - old_block_bounds.top,
                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
        layout_block_window(); verify_block_layout();
        active_theme.fonts.size = old_interface_size;
        refresh_interface_fonts();
        SetWindowPos(block_window, nullptr, 0, 0, old_block_bounds.right - old_block_bounds.left,
                     old_block_bounds.bottom - old_block_bounds.top, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
        layout_block_window();
        require(SendMessageW(block_list, LB_GETCOUNT, 0, 0) == 1, "Shared blocklist manager did not show the new block");
        copy_block_code();
        require(prepared_block_code == blocklist_import_code(PlayerBlocklist(engine->blocklist())) &&
                    prepared_block_code.starts_with("BLGB1|"), "Manager did not prepare a data-only synchronization code");
        auto saved_block_status = text(block_status);
        status_note = "unrelated provider status";
        open_block_window();
        require(text(block_status) == saved_block_status,
                "Opening the blocklist overwrote its explicit copy status");
        SendMessageW(block_list, LB_SETCURSEL, 0, 0);
        SendMessageW(block_window, WM_COMMAND, MAKEWPARAM(0, LBN_SELCHANGE), reinterpret_cast<LPARAM>(block_list));
        require(text(block_name) == "NewPlayer-Realm", "Selecting a blocked player did not fill its full identity");
        SendMessageW(block_window, WM_COMMAND, MAKEWPARAM(id_blocks_remove, BN_CLICKED),
                     reinterpret_cast<LPARAM>(GetDlgItem(block_window, id_blocks_remove)));
        require(!engine->player_blocked(messages.back().message) && ui_blocks.names().empty(),
                "Unblocking did not update the receiver and shared model");
        for (int i = 0; i < 40; ++i) {
            auto line = L"Player" + std::to_wstring(i);
            SendMessageW(block_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        }
        SendMessageW(block_list, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        require(SendMessageW(block_list, LB_GETTOPINDEX, 0, 0) > 0, "Blocklist mouse wheel did not scroll");
        ShowWindow(block_window, SW_HIDE);
        require(engine->usage() == usage_before_menu, "Blocking or synchronizing consumed translation API tokens");
        SendMessageW(chat, WM_VSCROLL, SB_BOTTOM, 0);
        messages.back().translation = "short tail";
        render();
        require(!reply_hits.empty() && IsWindowVisible(reply_surface),
                "Visible messages have no input surface");
        auto bottom_hit = reply_hits.back();
        POINT blank{bottom_hit.bounds.right - scale(12),
                    (bottom_hit.bounds.top + bottom_hit.bounds.bottom) / 2};
        SetWindowPos(main, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RedrawWindow(main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        UpdateWindow(reply_surface);
        DwmFlush();
        POINT blank_screen = blank;
        ClientToScreen(chat, &blank_screen);
        require(WindowFromPoint(blank_screen) == reply_surface,
                "Transparent message whitespace still passes through to underlying window");
        auto fullrow = reply_at(blank);
        require(fullrow && fullrow->seq == bottom_hit.seq,
                "End-of-row blank area does not resolve to message");
        HRGN click_region = CreateRectRgn(0, 0, 0, 0);
        GetWindowRgn(reply_surface, click_region);
        require(PtInRegion(click_region, blank.x, blank.y),
                "Transparent blank row has no physical hit region");
        if (bottom_hit.bounds.bottom + 1 < client.bottom - scale(28))
            require(PtInRegion(click_region, blank.x, bottom_hit.bounds.bottom + 1),
                    "Viewport blank pixels still pass wheel input through to the game");
        DeleteObject(click_region);
        LPARAM blank_point = MAKELPARAM(blank.x, blank.y);
        SendMessageW(reply_surface, WM_LBUTTONDOWN, MK_LBUTTON, blank_point);
        render(); // A translation redraw of the same message must not cancel a click.
        SendMessageW(reply_surface, WM_LBUTTONUP, 0, blank_point);
        require(chosen_reply && chosen_reply->prefix == "/p " && GetFocus() == input,
                "Blank-row left click failed to focus channel reply");
        SendMessageW(reply_surface, WM_RBUTTONDOWN, MK_RBUTTON, blank_point);
        SendMessageW(reply_surface, WM_RBUTTONUP, 0, blank_point);
        require(message_menu && context_message, "Blank-row right click did not open the message menu");
        message_action(1);
        require(chosen_reply && chosen_reply->prefix == "/w NewPlayer-Realm " && GetFocus() == input,
                "Blank-row right click failed to focus whisper");
        messages.back().message.same_realm = true;
        SendMessageW(reply_surface, WM_RBUTTONDOWN, MK_RBUTTON, blank_point);
        SendMessageW(reply_surface, WM_RBUTTONUP, 0, blank_point);
        message_action(1);
        require(chosen_reply && chosen_reply->prefix == "/w NewPlayer " && GetFocus() == input,
                "Confirmed same-realm right click retained the realm or lost focus");
        messages.back().message.same_realm = false;
        // Reply tests leave the composer visible. On small desktops it legitimately
        // covers the gutter. Verify that occlusion, then isolate wheel hit testing.
        RECT viewport{}, composer_bounds{};
        GetClientRect(reply_surface, &viewport);
        GetWindowRect(output, &composer_bounds);
        POINT composer_probe{int(viewport.right) - scale(3), int(viewport.bottom / 2)};
        ClientToScreen(reply_surface, &composer_probe);
        SetWindowPos(output, HWND_TOPMOST,
                     composer_probe.x - (composer_bounds.right - composer_bounds.left) / 2,
                     composer_probe.y - (composer_bounds.bottom - composer_bounds.top) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        auto composer_owner = WindowFromPoint(composer_probe);
        require(composer_owner == output || IsChild(output, composer_owner),
                "Foreground reply composer did not own its covered viewport point");
        hide_output();
        SetWindowPos(output, nullptr, composer_bounds.left, composer_bounds.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        require(!IsWindowVisible(output), "Composer remained visible during wheel routing test");
        SetWindowPos(main, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        refresh_reply_surface();
        // A stationary pointer must retain our wheel target through gaps, long scrolls and both ends.
        for (int step = 0; step < 120; ++step) {
            SendMessageW(reply_surface, WM_MOUSEWHEEL, MAKEWPARAM(0, step < 60 ? WHEEL_DELTA : -WHEEL_DELTA),
                         0);
            for (int y : {1, int(viewport.bottom / 2), int(viewport.bottom - 1)}) {
                POINT probe{int(viewport.right) - scale(3), y};
                require(!reply_at(probe), "Gutter blank space incorrectly selected a reply target");
                auto screen = probe;
                ClientToScreen(chat, &screen);
                auto owner = WindowFromPoint(screen);
                if (owner != reply_surface && owner != scrollbar) {
                    wchar_t owner_class[128]{};
                    GetClassNameW(owner, owner_class, 128);
                    RECT plane{}; GetWindowRect(reply_surface, &plane);
                    throw std::runtime_error("Wheel viewport hit mismatch: owner=" + narrow(owner_class) +
                        ", step=" + std::to_string(step) + ", point=" + std::to_string(screen.x) + "," +
                        std::to_string(screen.y) + ", plane=" + std::to_string(plane.left) + "," +
                        std::to_string(plane.top) + "," + std::to_string(plane.right) + "," +
                        std::to_string(plane.bottom));
                }
            }
        }
        auto retained_reply = chosen_reply;
        LPARAM gutter = MAKELPARAM(viewport.right - scale(3), viewport.bottom - 1);
        SendMessageW(reply_surface, WM_LBUTTONDOWN, MK_LBUTTON, gutter);
        SendMessageW(reply_surface, WM_LBUTTONUP, 0, gutter);
        require(bool(chosen_reply) == bool(retained_reply) &&
                    (!chosen_reply || chosen_reply->prefix == retained_reply->prefix),
                "Empty viewport click changed the reply player");
        DWORD plane_flags{};
        BYTE plane_alpha{};
        COLORREF plane_key{};
        require(GetLayeredWindowAttributes(reply_surface, &plane_key, &plane_alpha, &plane_flags) &&
                    plane_alpha == 1 && plane_flags == LWA_ALPHA,
                "Input surface opacity changed");
        ICONINFO cursor_info{};
        require(GetIconInfo(reply_cursor(), &cursor_info) && !cursor_info.fIcon, "Reply cursor is missing");
        DeleteObject(cursor_info.hbmColor);
        DeleteObject(cursor_info.hbmMask);
        POINT saved_pointer{};
        GetCursorPos(&saved_pointer);
        // Some desktop sessions deny SetCursorPos even with an unlocked input
        // desktop. Keep real-cursor checks when permitted, without confusing an
        // OS capability denial with a failure of the message hit/cursor logic.
        if (SetCursorPos(blank_screen.x, blank_screen.y)) {
            SendMessageW(reply_surface, WM_SETCURSOR, reinterpret_cast<WPARAM>(reply_surface),
                         MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
            require(GetCursor() == reply_cursor(), "Blank row did not select return cursor");
            SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
            SendMessageW(chat, WM_MOUSEMOVE, 0, blank_point);
            require(GetCursor() == reply_cursor(), "RichEdit mouse movement restored its I-beam");
            SetCursorPos(saved_pointer.x, saved_pointer.y);
        } else {
            std::ofstream(platform::executable_dir() / "ui-tests-cursor-skipped.log")
                << "SetCursorPos denied by the desktop; physical-pointer cursor checks not executed.\n";
        }
        auto saved_clickthrough = config["click_through"];
        config["click_through"] = true;
        refresh_reply_surface();
        require(!IsWindowVisible(reply_surface) && !reply_at(blank),
                "Click-through mode still intercepts message rows");
        config["click_through"] = saved_clickthrough;
        refresh_reply_surface();
        ShowWindow(main, SW_HIDE);
        require(!IsWindowVisible(reply_surface), "Hiding chat left input surface behind");
        ShowWindow(main, SW_SHOWNOACTIVATE);
        sync_reply_surface();

        set_scrollbar_hover(false);
        require(!IsWindowVisible(scrollbar), "Scrollbar did not hide without hover");
        set_scrollbar_hover(true);
        require(IsWindowVisible(scrollbar), "Scrollbar did not appear on hover");
        scroll_dragging = true;
        set_scrollbar_hover(false);
        require(IsWindowVisible(scrollbar), "Scrollbar disappeared during drag");
        scroll_dragging = false;
        set_scrollbar_hover(false);
        // The test button must use the real renderer without altering real history or making requests.
        auto real_history = messages;
        auto old_view = bookmark();
        POINT reading{0, 260};
        SendMessageW(chat, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&reading));
        auto real_bookmark = bookmark();
        auto usage_before_preview = engine->usage();
        auto test_button = GetDlgItem(body, id_chat_preview);
        require(test_button && text(test_button) == ui_text("Show test message"), "Preview button missing: " + (test_button ? text(test_button) : "<null>") + " vs " + ui_text("Show test message"));
        SendMessageW(test_button, BM_CLICK, 0, 0);
        require(message_preview && chat_rows.size() == 16 && preview_messages.size() == 16,
                "Preview did not render all samples");
        require(text(chat).find(ui_text("[Test]")) != std::string::npos &&
                    text(chat).find(ui_text("[Resend]")) != std::string::npos,
                "Preview labels missing");
        require(messages.size() == real_history.size() && !reply_hits.empty() &&
                    IsWindowVisible(reply_surface),
                "Preview lost its wheel input surface or mutated history");
        if (UINT lines = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0) && lines) {
            SendMessageW(chat, WM_VSCROLL, SB_BOTTOM, 0);
            auto bottom = scroll_state().position.y;
            require(bottom > 0, "Preview did not overflow for wheel test");
            SendMessageW(reply_surface, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
            require(scroll_state().position.y < bottom, "Preview wheel on blank row did not scroll");
        }
        auto preview_color = [&](std::wstring_view token) {
            std::wstring word(token);
            FINDTEXTEXW find{{0, -1}, word.data(), {}};
            auto pos = SendMessageW(chat, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&find));
            require(pos >= 0, "Preview color label missing");
            SendMessageW(chat, EM_SETSEL, pos, pos + 1);
            CHARFORMAT2W f{};
            f.cbSize = sizeof(f);
            SendMessageW(chat, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&f));
            return f.crTextColor;
        };
        auto preview_item = [&](uint32_t color) {
            for (const auto &span : chat_spans(preview_messages.at(13).translation, 0xffffff))
                if (span.color == color && !trim(span.text).empty()) return wide(trim(span.text));
            throw std::runtime_error("Preview item fixture is missing");
        };
        require(preview_color(preview_item(0x0070dd)) == rgb(0x0070dd) &&
                    preview_color(preview_item(0xff8000)) == rgb(0xff8000), "Preview lost item colors");
        require(preview_color(wide("[" + player_display_name(preview_messages.front().message) + "]")) ==
                    player_color("WARRIOR"), "Preview lost class color");
        bool output_was_visible = IsWindowVisible(output) != FALSE;
        auto previous_reply = chosen_reply ? chosen_reply->prefix : std::string();
        pick_reply(chat_rows.front().start, true);
        require(bool(IsWindowVisible(output)) == output_was_visible &&
                    (chosen_reply ? chosen_reply->prefix : std::string()) == previous_reply,
                "Preview fixture changed reply target");
        auto renders = chat_render_calls;
        repaint_appearance();
        require(chat_render_calls == renders + 1, "Theme change does not refresh preview");
        DisplayMessage incoming;
        incoming.message.session = 7;
        incoming.message.seq = 1;
        incoming.message.sent = 999999;
        incoming.message.event = "CHAT_MSG_PARTY";
        incoming.translation = "received during preview";
        insert_history(messages, incoming, 200);
        SendMessageW(test_button, BM_CLICK, 0, 0);
        require(!message_preview && preview_messages.empty() &&
                    text(chat).find(ui_text("[Test]")) == std::string::npos,
                "Preview exit failed");
        require(text(chat).find("received during preview") != std::string::npos &&
                    messages.size() == real_history.size() + 1,
                "Preview lost a newly received message");
        auto restored_view = bookmark();
        require(real_bookmark.anchor == restored_view.anchor &&
                    std::abs(real_bookmark.anchor_pos.y - restored_view.anchor_pos.y) <= 2,
                "Preview did not restore reading anchor");
        require(engine->usage() == usage_before_preview, "Preview consumed API tokens");
        messages = real_history;
        render(old_view);
        for (int i = 0; i < 8; ++i) {
            toggle_message_preview();
            toggle_message_preview();
        }
        toggle_message_preview();
        restore_appearance();
        require(!message_preview && !preview_live_view, "Closing settings leaves test preview active");
        // Display tabs share one history; hidden inserts must repair reply indices without reflow.
        auto original_config = config;
        config["max_messages"] = 200; // Retain the full sixty-message tab fixture.
        auto original_history = messages;
        auto original_view = bookmark();
        auto original_draft = tab_draft;
        auto original_layout = game_layout;
        auto original_editing = editing_tab;
        auto original_reply = chosen_reply;
        auto usage_before_tabs = engine->usage();
        auto party_tab = new_chat_tab("party", "Party");
        party_tab["events"] = {"CHAT_MSG_PARTY", "CHAT_MSG_PARTY_LEADER"};
        auto whisper_tab = new_chat_tab("whisper", "Whisper");
        whisper_tab["events"] = {"CHAT_MSG_WHISPER", "CHAT_MSG_WHISPER_INFORM"};
        config["chat_tabs"] = Json::array({party_tab, whisper_tab});
        config["active_chat_tab"] = "party";
        sync_chat_tabs();
        messages.clear();
        for (int i = 1; i <= 60; ++i) {
            DisplayMessage m;
            m.message.session = 101;
            m.message.seq = i;
            m.message.sent = 10000 + i;
            m.message.event = i % 2 ? "CHAT_MSG_PARTY" : "CHAT_MSG_WHISPER";
            m.message.author = "Sender" + std::to_string(i) + "-Westfall";
            m.message.text = "hello";
            m.translation = "tab sample " + std::to_string(i);
            receive_chat(std::move(m));
        }
        render(ChatBookmark{});
        require(chat_rows.size() == 30 && IsWindowVisible(chat_tab_bar), "Named tabs did not filter rows");
        for (auto &row : chat_rows)
            require(messages.at(row.index).message.event == "CHAT_MSG_PARTY", "Party tab leaked whispers");
        require(tab_unread["whisper"] == 30, "Inactive tab unread counter differs");
        auto rendered_before_hidden = text(chat);
        DisplayMessage hidden;
        hidden.message.session = 101;
        hidden.message.seq = 61;
        hidden.message.sent = 10030;
        hidden.message.event = "CHAT_MSG_WHISPER";
        hidden.message.author = "Hidden-Westfall";
        hidden.translation = "hidden middle insertion";
        require(!receive_chat(hidden) && text(chat) == rendered_before_hidden,
                "Hidden tab message needlessly reflowed active chat");
        for (auto &row : chat_rows)
            require(messages.at(row.index).message.seq == row.seq,
                    "Hidden insertion left a stale click-to-reply index");
        pick_reply(chat_rows.back().start, true);
        require(chosen_reply && chosen_reply->prefix == "/w Sender59-Westfall ",
                "Hidden insertion changed the actual whisper target");
        // Explicit wheel routing works without the native scrollbar, on both input windows.
        UINT system_lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &system_lines, 0);
        if (system_lines) {
            SendMessageW(chat, WM_VSCROLL, SB_BOTTOM, 0);
            auto bottom = scroll_state().position.y;
            chat_wheel_remainder = 0;
            SendMessageW(main, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), 0);
            require(scroll_state().position.y == bottom, "Partial wheel delta was not accumulated");
            SendMessageW(reply_surface, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), 0);
            require(scroll_state().position.y < bottom, "Wheel on transparent message area did not scroll");
            SendMessageW(chat, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
            require(scroll_state().bottom, "Wheel on text did not scroll back down");
            SendMessageW(scrollbar, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
            require(scroll_state().position.y < bottom, "Wheel on custom scrollbar did not scroll");
        }
        POINT read_party{0, 100};
        SendMessageW(chat, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&read_party));
        auto party_view = bookmark();
        switch_chat_tab(1);
        require(chat_rows.size() == 31 && tab_unread["whisper"] == 0,
                "Switching tab lost history or did not clear unread");
        for (auto &row : chat_rows)
            require(messages.at(row.index).message.event == "CHAT_MSG_WHISPER", "Private tab leaked party");
        switch_chat_tab(0);
        auto party_restored = bookmark();
        require(party_restored.anchor == party_view.anchor &&
                    std::abs(party_restored.anchor_pos.y - party_view.anchor_pos.y) <= 2,
                "Tab switch lost independent reading bookmark");
        // Preview uses the current tab's filter but never changes the reply target.
        auto reply_before_preview = chosen_reply;
        toggle_message_preview();
        require(!chat_rows.empty() && chat_rows.size() < preview_messages.size(),
                "Preview ignored tab filter");
        require(!reply_hits.empty() &&
                    !reply_at({reply_hits.back().bounds.left, reply_hits.back().bounds.top}),
                "Preview exposed a fake player reply");
        if (system_lines && scroll_state().maximum > 0) {
            auto bottom = scroll_state().position.y;
            SendMessageW(reply_surface, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
            require(scroll_state().position.y < bottom, "Preview input plane lost wheel handling");
        }
        toggle_message_preview();
        require(bool(chosen_reply) == bool(reply_before_preview) &&
                    (!chosen_reply || chosen_reply->prefix == reply_before_preview->prefix),
                "Preview altered reply destination");
        tab_draft = config["chat_tabs"];
        fill_tab_editor(0);
        set(tab_name, "Party Chat");
        capture_tab_editor();
        require(tab_draft[0]["name"] == "Party Chat", "Tab rename draft not captured");
        add_tab();
        require(tab_draft.size() == 3 && parse_chat_tabs(tab_draft).size() == 3,
                "New tab has invalid ID or was not created");
        delete_tab();
        require(tab_draft.size() == 2, "Delete tab did not update draft");
        auto appearance_before_flash = appearance_preview;
        auto classic_preview = config;
        theme_registry.change_style(classic_preview, "classic");
        appearance_transaction([&] { appearance_preview = classic_preview; });
        require(beveled() && active_theme.widgets.slider == SliderVisual::Diamond,
                "Classic settings did not activate the native beveled/diamond painters");
        require(ui_font(13, false, body, true) != ui_font(13, false, body, false),
                "Serif Chinese and Latin fonts collide in the cache");
        require(!(GetWindowLongPtrW(brand, GWL_STYLE) & WS_VISIBLE) &&
                    !(GetWindowLongPtrW(brand_detail, GWL_STYLE) & WS_VISIBLE),
                "Classic grouped settings still show the flat style's brand block");
        auto navigation_rect = [&](int index) {
            RECT r{};
            GetWindowRect(navigation[index], &r);
            return r;
        };
        require(navigation_rect(5).top > navigation_rect(0).top &&
                    navigation_rect(5).top < navigation_rect(1).top &&
                    navigation_rect(3).top < navigation_rect(4).top,
                "Classic sidebar categories are not grouped by display, translation and management");
        require(!edit_scrollbars.empty(), "No native prompt scrollbars were registered");
        const auto page_before_prompt_scroll = active_page;
        switch_page(3);
        auto prompt_control = std::find_if(edits.begin(), edits.end(), [](const Control &item) {
            return item.key == "outbound_template";
        });
        require(prompt_control != edits.end(), "Output prompt editor missing");
        const auto saved_prompt_text = text(prompt_control->h);
        set(prompt_control->h, std::string(4000, 'A'));
        auto scroll_fixture = std::find_if(edit_scrollbars.begin(), edit_scrollbars.end(), [](auto &bar) {
            return (GetWindowLongPtrW(bar->window(), GWL_STYLE) & WS_VISIBLE) != 0;
        });
        require(scroll_fixture != edit_scrollbars.end(), "Long prompt has no themed scrollbar");
        auto prompt_edit = (*scroll_fixture)->edit(), prompt_bar = (*scroll_fixture)->window();
        auto prompt_before_scroll = text(prompt_edit);
        require(!(GetWindowLongPtrW(prompt_edit, GWL_STYLE) & WS_VSCROLL),
                "Classic prompt still exposes the native white scrollbar");
        SendMessageW(prompt_edit, EM_LINESCROLL, 0, -9999);
        RECT prompt_track{};
        GetClientRect(prompt_bar, &prompt_track);
        SendMessageW(prompt_bar, WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(prompt_track.right / 2, prompt_track.bottom - 2));
        SendMessageW(prompt_bar, WM_LBUTTONUP, 0,
                     MAKELPARAM(prompt_track.right / 2, prompt_track.bottom - 2));
        require(SendMessageW(prompt_edit, EM_GETFIRSTVISIBLELINE, 0, 0) > 0 &&
                    text(prompt_edit) == prompt_before_scroll,
                "Themed prompt scrollbar does not scroll or changes the prompt draft");
        SendMessageW(prompt_edit, EM_LINESCROLL, 0, -9999);
        SendMessageW(prompt_bar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(prompt_track.right / 2, 2));
        SendMessageW(prompt_bar, WM_MOUSEMOVE, MK_LBUTTON,
                     MAKELPARAM(prompt_track.right / 2, prompt_track.bottom - 2));
        SendMessageW(prompt_bar, WM_LBUTTONUP, 0,
                     MAKELPARAM(prompt_track.right / 2, prompt_track.bottom - 2));
        require(SendMessageW(prompt_edit, EM_GETFIRSTVISIBLELINE, 0, 0) > 0,
                "Dragging the themed prompt thumb did not scroll the native editor");
        SendMessageW(prompt_edit, EM_LINESCROLL, 0, -9999);
        SendMessageW(prompt_bar, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        require(SendMessageW(prompt_edit, EM_GETFIRSTVISIBLELINE, 0, 0) > 0 &&
                    text(prompt_edit) == prompt_before_scroll,
                "Themed prompt wheel routing failed or changed the text");
        set(prompt_control->h, saved_prompt_text);
        switch_page(page_before_prompt_scroll);
        SendMessageW(prompt_edit, EM_LINESCROLL, 0, -9999);
        RECT main_box{}, background_box{};
        GetWindowRect(main, &main_box);
        GetWindowRect(backdrop, &background_box);
        require(background_box.top == main_box.top + scale(24) && background_box.bottom == main_box.bottom,
                "Classic background still covers the transparent title row");
        {
            // Hovering a resizable Classic overlay must not add the old thick
            // frame or leave black fragments between its transparent children.
            bl::ui_checks::Pixels canvas(main);
            auto previous_handles = handles_visible;
            handles_visible = true;
            SendMessageW(main, WM_ERASEBKGND, reinterpret_cast<WPARAM>(canvas.dc), 0);
            for (auto point : {POINT{scale(1), scale(5)}, POINT{canvas.width / 2, scale(1)},
                               POINT{canvas.width - scale(2), canvas.height / 2},
                               POINT{canvas.width / 2, canvas.height - scale(2)}})
                require(GetPixel(canvas.dc, point.x, point.y) == transparency_key,
                        "Classic hover exposed an opaque resize frame");
            handles_visible = previous_handles;
        }
        RedrawWindow(gear, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        {
            HDC dc = GetDC(gear);
            COLORREF corner = GetPixel(dc, scale(2), scale(2));
            ReleaseDC(gear, dc);
            require(corner == transparency_key, "Classic gear kept an opaque button background");
        }
        {
            bl::ui_checks::Pixels pixels(chat_tab_bar);
            RECT frame{0, 0, scale(100), scale(23)};
            COLORREF idle_cap{};
            for (auto [selected, hover, pressed, flash] :
                 {std::tuple{false, false, false, 0.0}, std::tuple{true, false, false, 0.0},
                  std::tuple{false, true, false, 0.0}, std::tuple{true, false, true, 0.0},
                  std::tuple{false, false, false, 1.0}}) {
                FillRect(pixels.dc, &frame, keyed_brush);
                paint_classic_tab(pixels.dc, frame, selected, hover, pressed, flash);
                auto cap = GetPixel(pixels.dc, frame.right / 2, frame.top + scale(1));
                require(cap != transparency_key &&
                            GetPixel(pixels.dc, frame.left + scale(1), frame.top + scale(5)) !=
                                transparency_key &&
                            GetPixel(pixels.dc, frame.right - scale(2), frame.bottom - scale(3)) !=
                                transparency_key,
                        "Classic tab lost its square cap or straight fading sides");
                require(GetPixel(pixels.dc, frame.left + scale(1), frame.top + scale(1)) ==
                            transparency_key,
                        "Classic tab lost its small clipped corner");
                for (int y = scale(4); y < frame.bottom - scale(4); ++y)
                    for (int x = scale(4); x < frame.right - scale(4); ++x)
                        require(GetPixel(pixels.dc, x, y) == transparency_key,
                                "Classic title has a filled area during selection, hover or unread glow");
                require(GetPixel(pixels.dc, frame.right / 2, frame.bottom - scale(3)) ==
                            transparency_key,
                        "Classic tab regained a bottom underline");
                if (!selected && !hover && flash == 0)
                    idle_cap = cap;
                if (flash > 0)
                    require(cap != idle_cap, "Unread Classic tab did not brighten its frame");
            }
        }
        sync_tab_input_surface();
        require(IsWindowVisible(tab_input_surface), "Transparent classic tabs have no click surface");
        sync_gear_input_surface();
        require(IsWindowVisible(gear_input_surface), "Transparent gear has no click surface");
        sync_resize_input_surface();
        require(IsWindowVisible(resize_input_surface), "Classic transparent edges have no resize surface");
        RECT resize_box{};
        GetWindowRect(resize_input_surface, &resize_box);
        POINT resize_point{resize_box.left + 1, (resize_box.top + resize_box.bottom) / 2};
        require(WindowFromPoint(resize_point) == resize_input_surface &&
                    SendMessageW(resize_input_surface, WM_NCHITTEST, 0,
                                 MAKELPARAM(resize_point.x, resize_point.y)) == HTLEFT,
                "Classic transparent edge lost native resize input");
        require(WindowFromPoint({(resize_box.left + resize_box.right) / 2,
                                 (resize_box.top + resize_box.bottom) / 2}) != resize_input_surface,
                "Classic resize surface covers the message viewport");
        RECT gear_bounds{};
        GetWindowRect(gear_input_surface, &gear_bounds);
        POINT gear_corner{gear_bounds.left + scale(2), gear_bounds.top + scale(2)};
        require(WindowFromPoint(gear_corner) == gear_input_surface,
                "Transparent gear corner clicks fall through to the game");
        SendMessageW(gear_input_surface, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(scale(2), scale(2)));
        SendMessageW(gear_input_surface, WM_LBUTTONUP, 0, MAKELPARAM(scale(2), scale(2)));
        require(IsWindowVisible(settings), "Transparent gear did not open settings");
        ShowWindow(settings, SW_HIDE);
        layout_chat_tab_hits(chat_tab_bar);
        sync_tab_input_surface();
        DwmFlush();
        auto target_tab =
            std::find_if(tab_hits.begin(), tab_hits.end(), [](auto &hit) { return hit.index == 1; });
        require(target_tab != tab_hits.end(), "Classic tab fixture missing");
        POINT blank_tab{(target_tab->bounds.left + target_tab->bounds.right) / 2,
                        target_tab->bounds.bottom - scale(3)};
        POINT blank_tab_screen = blank_tab;
        ClientToScreen(chat_tab_bar, &blank_tab_screen);
        auto tab_window_at_point = WindowFromPoint(blank_tab_screen);
        require(tab_window_at_point == tab_input_surface || tab_window_at_point == chat_tab_bar,
                "Transparent tab interior clicks fall through to the game; expected=" +
                    std::to_string(reinterpret_cast<uintptr_t>(tab_input_surface)) + " actual=" +
                    std::to_string(reinterpret_cast<uintptr_t>(tab_window_at_point)) + " tab=" +
                    std::to_string(reinterpret_cast<uintptr_t>(chat_tab_bar)) + " main=" +
                    std::to_string(reinterpret_cast<uintptr_t>(main)) + " resize=" +
                    std::to_string(reinterpret_cast<uintptr_t>(resize_input_surface)));
        SendMessageW(tab_input_surface, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(blank_tab.x, blank_tab.y));
        SendMessageW(tab_input_surface, WM_LBUTTONUP, 0, MAKELPARAM(blank_tab.x, blank_tab.y));
        require(active_tab == 1, "Whole transparent tab click did not select the target");
        switch_chat_tab(0);
        auto opaque_classic = classic_preview;
        opaque_classic["background_opacity"] = 1.0;
        appearance_transaction([&] { appearance_preview = opaque_classic; });
        GetWindowRect(backdrop, &background_box);
        require(background_box.top == main_box.top + scale(24), "Opaque chat setting filled the title row");
        appearance_transaction([&] { appearance_preview = classic_preview; });
        tab_unread["whisper"] = 1;
        sync_tab_flash();
        require(tab_flash_running && tab_alert(1) && !tab_alert(0),
                "Unread classic tab did not start its glow");
        auto render_before_flash = chat_render_calls;
        for (int frame = 0; frame < 60; ++frame)
            animate_tabs();
        require(chat_render_calls == render_before_flash, "Tab animation reflowed the chat history");
        tab_unread["whisper"] = 0;
        sync_tab_flash();
        require(!tab_flash_running, "Reading classic tab did not stop its animation timer");
        appearance_transaction([&] { appearance_preview = appearance_before_flash; });
        sync_tab_flash();
        require(!tab_flash_running, "Changing style left the glow animation running");
        GetWindowRect(backdrop, &background_box);
        require((!IsWindowVisible(backdrop) || background_box.top == main_box.top) &&
                    !IsWindowVisible(tab_input_surface) && IsWindowVisible(gear_input_surface) &&
                    !IsWindowVisible(resize_input_surface),
                "Leaving classic retained its tab/resize planes or lost the persistent gear input plane");
        require(!beveled() && (GetWindowLongPtrW(brand, GWL_STYLE) & WS_VISIBLE) &&
                    (GetWindowLongPtrW(brand_detail, GWL_STYLE) & WS_VISIBLE),
                "Leaving classic did not restore the existing flat settings layout");
        require((GetWindowLongPtrW(prompt_edit, GWL_STYLE) & WS_VSCROLL) &&
                    !(GetWindowLongPtrW(prompt_bar, GWL_STYLE) & WS_VISIBLE),
                "Switching to flat theme did not restore native prompt scrolling");
        auto game_party = party_tab;
        game_party["id"] = "wow-3";
        Json export_snapshot = {{"schemaVersion", 1},
                                {"windows", Json::array({new_chat_tab("wow-1", "General"), game_party})}};
        auto url_control = std::find_if(edits.begin(), edits.end(),
                                        [](const Control &control) { return control.key == "base_url"; });
        require(url_control != edits.end(), "Provider editor unavailable for sync isolation test");
        auto old_url_text = text(url_control->h);
        auto committed_url = config["base_url"];
        set(url_control->h, "https://unsaved.invalid");
        set(tab_name, ""); // Old incomplete tab draft must not block an explicit game sync.
        require(apply_game_tabs(export_snapshot), "Fresh game export did not automatically apply");
        require(config["chat_tabs"] == export_snapshot["windows"] && tab_draft == config["chat_tabs"] &&
                    display_tabs.size() == 2 && active_tab == 0 && editing_tab == 0,
                "One-click sync did not immediately replace the visible and editable tabs");
        require(config["base_url"] == committed_url && text(url_control->h) == "https://unsaved.invalid",
                "Auto sync saved or discarded another pending settings editor");
        auto renders_after_sync = chat_render_calls;
        require(!apply_game_tabs(export_snapshot) && chat_render_calls == renders_after_sync,
                "Identical game export reflowed the chat history");
        export_snapshot["windows"].erase(export_snapshot["windows"].begin() + 1);
        apply_game_tabs(export_snapshot);
        require(config["chat_tabs"] == export_snapshot["windows"] && display_tabs.size() == 1,
                "One-click resync retained a deleted game window");
        auto valid_config = config;
        auto valid_draft = tab_draft;
        auto last_valid_snapshot = game_layout;
        export_snapshot["windows"][0]["events"] = {"BAD_EVENT"};
        bool rejected = false;
        try {
            apply_game_tabs(export_snapshot);
        } catch (const std::exception &) {
            rejected = true;
        }
        require(rejected && tab_draft == valid_draft && config == valid_config &&
                    game_layout == last_valid_snapshot,
                "Invalid auto sync damaged the draft, active layout or cached snapshot");
        set(url_control->h, old_url_text);
        require(engine->usage() == usage_before_tabs, "Tab operations consumed API tokens");
        config = original_config;
        messages = std::move(original_history);
        tab_draft = original_draft;
        game_layout = original_layout;
        tab_views.clear();
        tab_unread.clear();
        sync_chat_tabs();
        fill_tab_editor(original_editing);
        chosen_reply = original_reply;
        render(original_view);
        // Independent bubble preview: live sizing/alpha, no translation state or focus changes.
        auto reply_before_bubble = reply;
        auto bubble_settings = config;
        auto area_before_bubble = bubble_area;
        auto usage_before_bubble = engine->usage();
        reply = QuickReply{};
        switch_page(3);
        require(bubble_rows.size() == 2, "Bubble appearance controls are missing");
        set(bubble_rows[0].edit, "50");
        set(bubble_rows[1].edit, "100");
        SetForegroundWindow(settings);
        auto foreground_before_bubble = GetForegroundWindow();
        auto button = GetDlgItem(body, id_bubble_preview);
        SendMessageW(button, BM_CLICK, 0, 0);
        require(bubble_preview && bubble && IsWindowVisible(bubble) && reply.phase == QuickReply::Hidden,
                "Preview entered the translation/clipboard flow or did not show the bubble");
        DWORD bubble_flags{};
        BYTE bubble_alpha{};
        COLORREF bubble_key{};
        require(GetLayeredWindowAttributes(bubble, &bubble_key, &bubble_alpha, &bubble_flags) &&
                    bubble_alpha == 128,
                "Bubble default is not 50% transparent");
        require(GetForegroundWindow() == foreground_before_bubble &&
                    (GetWindowLongPtrW(bubble, GWL_EXSTYLE) & (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) ==
                        (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE),
                "Bubble preview steals focus or blocks game mouse input");
        RECT bubble_default{};
        GetWindowRect(bubble, &bubble_default);
        require(std::abs((bubble_default.left + bubble_default.right) / 2.0 -
                         (bubble_area.left + bubble_area.right) / 2.0) <= 1 &&
                    std::abs((bubble_default.top + bubble_default.bottom) / 2.0 -
                             (bubble_area.top + (bubble_area.bottom - bubble_area.top) * 0.4)) <= 1,
                "Bubble is not centered slightly above the current monitor center");
        set(bubble_rows[1].edit, "150");
        RECT bubble_large{};
        GetWindowRect(bubble, &bubble_large);
        require(bubble_large.right - bubble_large.left > bubble_default.right - bubble_default.left &&
                    bubble_large.bottom - bubble_large.top > bubble_default.bottom - bubble_default.top &&
                    SendMessageW(bubble_rows[1].slider, TBM_GETPOS, 0, 0) == 150,
                "Bubble scale did not resize text and padding or synchronize the slider");
        set(bubble_rows[0].edit, "75");
        GetLayeredWindowAttributes(bubble, &bubble_key, &bubble_alpha, &bubble_flags);
        require(bubble_alpha == 64 && config == bubble_settings,
                "Bubble preview did not isolate its draft opacity");
        RECT slider_bounds{};
        GetClientRect(bubble_rows[0].slider, &slider_bounds);
        SendMessageW(bubble_rows[0].slider, WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(0, slider_bounds.bottom / 2));
        SendMessageW(bubble_rows[0].slider, WM_LBUTTONUP, 0, MAKELPARAM(0, slider_bounds.bottom / 2));
        GetLayeredWindowAttributes(bubble, &bubble_key, &bubble_alpha, &bubble_flags);
        require(text(bubble_rows[0].edit) == "0" && bubble_alpha == 255,
                "Bubble opacity slider does not update live");
        switch_page(0);
        require(!bubble_preview && !IsWindowVisible(bubble) && engine->usage() == usage_before_bubble,
                "Leaving bubble settings retained preview or consumed API tokens");
        reply = reply_before_bubble;
        bubble_area = area_before_bubble;
        set(bubble_rows[0].edit, std::to_string(config.value("bubble_transparency", 50)));
        set(bubble_rows[1].edit, std::to_string(config.value("bubble_scale", 100)));
        verify_provider_ui();
        verify_models_ui();
        verify_ocr_ui();
        bl::ui_checks::verify_widgets(*this);
        // Render only our own opaque settings window for visual QA.
        SendMessageW(skin_select, CB_SETCURSEL, 0, 0);
        select_skin();
        for (const auto &n : number_rows)
            set(n.edit, n.key == "__font" ? "14" : n.key == "__limit" ? "200" : "100");
        switch_page(0);
        ShowWindow(settings, SW_SHOWNOACTIVATE);
        RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        auto snapshot = [&](HWND window, const char *name, bool composited = false) {
            RECT preview{};
            GetWindowRect(window, &preview);
            int pw = preview.right - preview.left, ph = preview.bottom - preview.top;
            HDC window_dc = GetDC(composited ? nullptr : window), memory = CreateCompatibleDC(window_dc);
            HBITMAP bitmap = CreateCompatibleBitmap(window_dc, pw, ph);
            auto previous_bitmap = SelectObject(memory, bitmap);
            bool printed;
            if (composited) {
                printed = BitBlt(memory, 0, 0, pw, ph, window_dc, preview.left, preview.top,
                                 SRCCOPY | CAPTUREBLT) != FALSE;
                // Only our own solid test window is behind the overlay here.
                require(GetPixel(memory, pw / 2, scale(1)) == RGB(64, 88, 112) &&
                            GetPixel(memory, scale(1), scale(5)) == RGB(64, 88, 112),
                        "Composited Classic title or hover frame is not transparent");
            } else if (window == picker) {
                SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
                printed = true;
            } else
                printed = PrintWindow(window, memory, PW_RENDERFULLCONTENT) != FALSE;
            SelectObject(memory, previous_bitmap);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = pw;
            info.bmiHeader.biHeight = -ph;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            Bytes pixels(size_t(pw) * ph * 4);
            bool copied = GetDIBits(memory, bitmap, 0, ph, pixels.data(), &info, DIB_RGB_COLORS) == ph;
            DeleteObject(bitmap);
            DeleteDC(memory);
            ReleaseDC(composited ? nullptr : window, window_dc);
            require(printed && copied, "Could not render settings QA image");
            auto qa = platform::executable_dir().parent_path().parent_path() / ".local" / "qa";
            std::filesystem::create_directories(qa);
            BITMAPFILEHEADER header{};
            header.bfType = 0x4D42;
            header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
            header.bfSize = DWORD(header.bfOffBits + pixels.size());
            std::ofstream shot(qa / name, std::ios::binary);
            shot.write(reinterpret_cast<const char *>(&header), sizeof(header));
            shot.write(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(info.bmiHeader));
            shot.write(reinterpret_cast<const char *>(pixels.data()), std::streamsize(pixels.size()));
            shot.close();
            const std::string_view filename(name);
            if (filename == "ui-ios-settings.bmp" || filename == "ui-classic-settings.bmp" ||
                filename == "ui-ios-page1.bmp" || filename == "ui-ios-page2.bmp" || filename == "ui-ios-page4.bmp" ||
                filename == "preview-runtime-privacy.bmp" ||
                filename.starts_with("preview-scope-") || filename == "preview-api-language.bmp" ||
                filename.starts_with("preview-ollama-") || filename == "preview-blocklist.bmp" || filename == "preview-language-packs.bmp" || filename == "preview-ct2-language-packs.bmp") {
                auto localized_qa = qa / "settings" / ui_language();
                std::filesystem::create_directories(localized_qa);
                std::filesystem::copy_file(qa / name, localized_qa / name,
                                           std::filesystem::copy_options::overwrite_existing);
            }
        };
        restore_appearance();
        open_block_window();
        set(block_name, "Player-Westfall");
        RedrawWindow(block_window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        snapshot(block_window, "preview-blocklist.bmp");
        ShowWindow(block_window, SW_HIDE);
        open_model_window();
        RedrawWindow(model_window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        snapshot(model_window, "preview-language-packs.bmp");
        ShowWindow(model_window, SW_HIDE);
        auto snapshot_format = provider_draft[provider_index]["format"];
        provider_draft[provider_index]["format"] = "ctranslate2_local"; fill_provider(); open_model_window();
        RedrawWindow(model_window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        snapshot(model_window, "preview-ct2-language-packs.bmp"); ShowWindow(model_window, SW_HIDE);
        provider_draft[provider_index]["format"] = snapshot_format; fill_provider();
        set(number_rows[0].edit, "100");
        set(number_rows[1].edit, "14");
        set(number_rows[2].edit, "200");
        RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        snapshot(settings, "ui-ios-settings.bmp");
        for (int page = 1; page < int(pages.size()); ++page) {
            switch_page(page);
            RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            snapshot(settings, ("ui-ios-page" + std::to_string(page) + ".bmp").c_str());
            if (page == 1 || page == 2 || page == 4) {
                if (page == 1 || page == 4) body_scroll = body_max_scroll;
                else body_scroll = std::clamp(scale(body_positions.at(chat_target).y - 130, body), 0, body_max_scroll);
                layout_body();
                RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                snapshot(settings, page == 1 ? "preview-scope-bottom.bmp" : page == 4 ? "preview-runtime-privacy.bmp" : "preview-api-language.bmp");
                body_scroll = 0;
                layout_body();
            }
        }
        {
            auto saved_profiles = provider_draft;
            auto saved_index = provider_index;
            auto preview_profile = new_connection("preview");
            preview_profile["name"] = "Custom connection";
            preview_profile["base_url"] = "https://api.example.com/v1";
            preview_profile["model"] = "your-model-name";
            provider_draft = public_connections(Json::array({preview_profile}));
            provider_index = 0;
            switch_page(2);
            fill_provider();
            RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            snapshot(settings, "ui-custom-api-settings.bmp");
            open_provider_advanced();
            RedrawWindow(provider_advanced, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            snapshot(provider_advanced, "ui-custom-api-advanced.bmp");
            close_provider_advanced();
            provider_draft = std::move(saved_profiles);
            provider_index = saved_index;
            fill_provider();
        }
        {
            auto saved_profiles = provider_draft;
            auto saved_index = provider_index;
            provider_draft = public_connections(Json::array({new_local_ollama_connection("local-ollama")}));
            provider_index = 0;
            switch_page(2);
            RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            snapshot(settings, "preview-ollama-models.bmp");
            SendMessageW(ollama_model_choice, CB_SETCURSEL, 3, 0);
            select_ollama_model();
            set(ollama_custom_model, "library/custom-model:Q4_K_M");
            RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            snapshot(settings, "preview-ollama-custom.bmp");
            provider_draft = std::move(saved_profiles);
            provider_index = saved_index;
            fill_provider();
        }
        open_ocr_settings();
        RedrawWindow(ocr_window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        snapshot(ocr_window, "ui-ocr-settings.bmp");
        close_ocr_settings();
        switch_page(3);
        body_scroll = body_max_scroll;
        layout_body();
        RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        snapshot(settings, "ui-output-bubble-settings.bmp");
        switch_page(5);
        body_scroll = body_max_scroll;
        layout_body();
        RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        snapshot(settings, "ui-ios-tabs-import.bmp");
        switch_page(0);
        SendMessageW(skin_select, CB_SHOWDROPDOWN, TRUE, 0);
        snapshot(picker, "ui-ios-picker.bmp");
        close_picker(false);
        SendMessageW(design_select, CB_SETCURSEL, 1, 0);
        select_design();
        RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        snapshot(settings, "ui-terminal-settings.bmp");
        SendMessageW(skin_select, CB_SHOWDROPDOWN, TRUE, 0);
        snapshot(picker, "ui-terminal-picker.bmp");
        close_picker(false);
        auto saved_registry = theme_registry;
        theme_registry = ThemeRegistry(platform::executable_dir().parent_path().parent_path() / ".local" /
                                       "qa" / "theme-fixtures" / "Skins");
        preview_theme_id("test.checkbox");
        body_scroll = body_max_scroll;
        layout_body();
        snapshot(settings, "ui-community-settings.bmp");
        RECT chat_before{};
        GetWindowRect(main, &chat_before);
        SetWindowPos(main, nullptr, 0, 0, scale(620), scale(620), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        toggle_message_preview();
        RedrawWindow(main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        snapshot(main, "ui-test-chat.bmp");
        auto qa_config = config;
        config["chat_tabs"] = Json::array({new_chat_tab("main", "General"), party_tab, whisper_tab});
        config["active_chat_tab"] = "main";
        sync_chat_tabs();
        render(ChatBookmark{});
        RedrawWindow(main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        snapshot(main, "ui-chat-tabs.bmp");
        auto qa_appearance = appearance_preview;
        preview_theme_id("classic");
        sync_tab_flash();
        for (auto [offset, name] : {std::pair{0, "ui-classic-chat-dim.bmp"},
                                    {1000, "ui-classic-chat-glow.bmp"},
                                    {1500, "ui-classic-chat-half.bmp"}}) {
            tab_flash_epoch = GetTickCount64() - offset;
            RedrawWindow(main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            snapshot(main, name);
        }
        {
            // PrintWindow does not composite color-keyed windows against their
            // surroundings. Check the real desktop composition over a private
            // test window, capturing only the rectangle entirely covered by it.
            auto backing_brush = CreateSolidBrush(RGB(64, 88, 112));
            WNDCLASSW backing_class{};
            backing_class.lpfnWndProc = DefWindowProcW;
            backing_class.hInstance = instance;
            backing_class.hbrBackground = backing_brush;
            backing_class.lpszClassName = L"BianLingo.ClassicPreviewBacking";
            auto registered = RegisterClassW(&backing_class);
            require(registered != 0, "Cannot register Classic preview backing");
            RECT original{};
            GetWindowRect(main, &original);
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(main, MONITOR_DEFAULTTONEAREST), &monitor);
            bool settings_visible = IsWindowVisible(settings);
            ShowWindow(settings, SW_HIDE);
            SetWindowPos(main, HWND_TOPMOST, monitor.rcWork.left + scale(20),
                         monitor.rcWork.top + scale(20), 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            RECT bounds{};
            GetWindowRect(main, &bounds);
            HWND backing = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                           backing_class.lpszClassName, L"", WS_POPUP, bounds.left,
                                           bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                                           nullptr, nullptr, instance, nullptr);
            auto restore = [&] {
                if (backing)
                    DestroyWindow(backing);
                UnregisterClassW(backing_class.lpszClassName, instance);
                DeleteObject(backing_brush);
                SetWindowPos(main, nullptr, original.left, original.top, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                if (settings_visible)
                    ShowWindow(settings, SW_SHOWNOACTIVATE);
            };
            try {
                require(backing != nullptr, "Cannot create Classic preview backing");
                SetWindowPos(backing, backdrop, bounds.left, bounds.top, bounds.right - bounds.left,
                             bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
                RedrawWindow(backing, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
                RedrawWindow(main, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                DwmFlush();
                snapshot(main, "preview-classic-composited.bmp", true);
            } catch (...) {
                restore();
                throw;
            }
            restore();
        }
        switch_page(0);
        RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        snapshot(settings, "ui-classic-settings.bmp");
        for (int page = 1; page < int(pages.size()); ++page) {
            switch_page(page);
            RedrawWindow(settings, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            snapshot(settings, ("ui-classic-page" + std::to_string(page) + ".bmp").c_str());
        }
        switch_page(0);
        open_picker(skin_select);
        snapshot(picker, "ui-classic-picker.bmp");
        close_picker(false);
        appearance_transaction([&] {
            appearance_preview = qa_appearance;
            fill_design_choices(appearance());
            fill_skin_choices(appearance());
            fill_skin_fields(appearance());
        });
        switch_chat_tab(2);
        RedrawWindow(main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        snapshot(main, "ui-chat-private.bmp");
        config = std::move(qa_config);
        sync_chat_tabs();
        toggle_message_preview();
        SetWindowPos(main, nullptr, 0, 0, chat_before.right - chat_before.left,
                     chat_before.bottom - chat_before.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        theme_registry = std::move(saved_registry);
        restore_appearance();
        toggle_message_preview();
        SendMessageW(settings, WM_CLOSE, 0, 0);
        require(!message_preview && preview_messages.empty() && !appearance_preview && !picker &&
                    appearance() == config,
                "Closing settings did not discard preview");
        // Real GDI buffers alternate marker/strip/discovery without handle leaks.
        auto count = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        {
            auto capture = platform::make_capture();
            for (int i = 0; i < 100; ++i)
                for (auto size : {Rect{0, 0, 4, 4}, Rect{0, 0, 192, 24}, Rect{0, 0, 440, 120}}) {
                    const auto &image = capture->grab(size);
                    require(image.width == size.w && image.height == size.h, "GDI buffer dimensions changed");
                }
        }
        require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= count,
                "GDI buffer cache leaked handles");
    }
#endif
    void pick_reply(long character, bool whisper = false) {
        if (message_preview)
            return;
        auto row = std::find_if(chat_rows.begin(), chat_rows.end(),
                                [&](const ChatRow &r) { return character >= r.start && character < r.end; });
        if (row == chat_rows.end() || row->index >= messages.size())
            return;
        reply_to_message(messages[row->index].message, whisper);
    }
#include "windows_ocr.inc"
#include "windows_providers.inc"
#include "windows_blocklist.inc"
#include "windows_local_models.inc"
    void open_settings() {
        if (settings) {
            ShowWindow(settings, SW_SHOW);
            SetForegroundWindow(settings);
            return;
        }
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(main, MONITOR_DEFAULTTONEAREST), &monitor);
        int width = std::min(scale(900), int(monitor.rcWork.right - monitor.rcWork.left) - 24);
        int height = std::min(scale(700), int(monitor.rcWork.bottom - monitor.rcWork.top) - 24);
        settings =
            CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, L"BianLingo.Window", wide(ui_text("BianLingo · Settings")).c_str(),
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                            monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
                            monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2,
                            width, height, nullptr, nullptr, instance, nullptr);
        BOOL dark = GetRValue(bg) + GetGValue(bg) + GetBValue(bg) < 450;
        DwmSetWindowAttribute(settings, 20, &dark, sizeof(dark));
        brand = control(settings, L"STATIC", "BianLingo", 22, 26, 165, 28, SS_LEFT);
        SendMessageW(brand, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font(18, true, settings)), TRUE);
        brand_detail = control(settings, L"STATIC", "Bring communication back into the game", 22, 58, 165, 22, SS_LEFT);
        muted_controls.insert(brand_detail);
        for (int i = 0; i < int(pages.size()); ++i)
            navigation[i] = control(settings, L"BUTTON", page_titles[i], 12, 112 + i * 44, 174, 38,
                                    BS_PUSHBUTTON | WS_TABSTOP, id_nav0 + i);
        version_label =
            control(settings, L"STATIC", "BianLingo " BL_VERSION, 22, 560, 165, 20, SS_LEFT);
        muted_controls.insert(version_label);
        page_title = control(settings, L"STATIC", page_titles[0], 228, 22, 620, 32, SS_LEFT);
        SendMessageW(page_title, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font(22, true, settings)), TRUE);
        page_description = control(settings, L"STATIC", page_descriptions[0], 230, 59, 620, 22, SS_LEFT);
        muted_controls.insert(page_description);
        body = CreateWindowExW(WS_EX_CONTROLPARENT, L"BianLingo.Window", L"",
                               WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 1, 1, settings, nullptr,
                               instance, nullptr);
        body_scrollbar = CreateWindowExW(WS_EX_NOACTIVATE, L"BianLingo.Window", L"", WS_CHILD, 0, 0, 1,
                                         1, settings, nullptr, instance, nullptr);
        label(0, "Interface language", 20, 14, 180);
        interface_language = page_control(0, L"COMBOBOX", "", 240, 9, 375, 300,
                                           CBS_DROPDOWNLIST | WS_TABSTOP, id_ui_language);
        fill_interface_languages();
        label(0, "Applies when saved; chat translation targets stay unchanged.", 20, 45, 600);
        settings_section(0);
        int y = 92;
        std::array<std::pair<const char *, const char *>, 9> options = {
            {{"auto", "Auto translate"},
             {"terms_enabled", "Use WoW Glossary"},
             {"show_original", "Show original text too"},
             {"show_time", "Show time"},
             {"class_colors", "Color names by class"},
             {"show_class", "Show class"},
             {"show_race", "Show race"},
             {"show_sex", "Show gender"},
             {"click_through", "Click through (hotkey)"}}};
        for (size_t i = 0; i < options.size(); ++i)
            checkbox(0, options[i].first, options[i].second, i % 2 ? 340 : 20, y + int(i / 2) * 32);
        settings_section(0);
        number_row("__background_transparency", "Background opacity (%)", 244, 0, 100,
                   int(std::lround((1 - background_opacity(config)) * 100)));
        number_row("__font", "Font size", 286, 9, 36, config.value("font_size", 14));
        number_row("__limit", "History messages (count)", 328, 10, 200, config.value("max_messages", 20));
        label(0, "Use the slider or enter a number; save to apply. 100% = fully transparent.", 20, 371);
        settings_section(0);
        label(0, "Design style", 20, 411, 100);
        design_select =
            page_control(0, L"COMBOBOX", "", 125, 406, 170, 180, CBS_DROPDOWNLIST | WS_TABSTOP, id_design);
        fill_design_choices(appearance());
        label(0, "Color scheme", 340, 411, 60);
        skin_select =
            page_control(0, L"COMBOBOX", "", 405, 406, 210, 180, CBS_DROPDOWNLIST | WS_TABSTOP, id_skin);
        fill_skin_choices(config);
        page_control(0, L"BUTTON", "Reset", 470, 510, 145, 27, BS_PUSHBUTTON, id_skin_reset);
        auto colors = skin_colors(theme_registry.fallback(config).palette);
        skin_sync = true;
        for (size_t i = 0; i < 4; ++i) {
            int x = i % 2 ? 340 : 20, row = 442 + int(i / 2) * 38;
            const char *labels[] = {"Background", "Controls", "Text", "Accent"};
            skin_buttons[i] =
                page_control(0, L"BUTTON", labels[i], x, row, 85, 27, BS_PUSHBUTTON, id_skin_color0 + int(i));
            auto h = edit(0, std::string("__skin_") + color_keys[i], x + 98, row, 150);
            set(h, colors[color_keys[i]].get<std::string>());
        }
        skin_sync = false;
        label(0, "Live preview; save to apply.", 20, 514, 430);
        page_control(0, L"BUTTON", "Show test message", 20, 549, 110, 27, BS_PUSHBUTTON, id_chat_preview);
        page_control(0, L"BUTTON", "Import", 144, 549, 110, 27, BS_PUSHBUTTON, id_theme_import);
        page_control(0, L"BUTTON", "Export", 268, 549, 110, 27, BS_PUSHBUTTON, id_theme_export);
        page_control(0, L"BUTTON", "Reload", 392, 549, 110, 27, BS_PUSHBUTTON, id_theme_reload);
        page_control(0, L"BUTTON", "Theme folder", 516, 549, 110, 27, BS_PUSHBUTTON, id_theme_folder);
        std::array<std::pair<const char *, const char *>, 10> groups = {{{"party", "Party (incl. leader)"},
                                                                         {"raid", "Team (with notifications)"},
                                                                         {"instance", "Instance Chat"},
                                                                         {"whisper", "Received whispers"},
                                                                         {"bn", "Received Battle.net whispers"},
                                                                         {"outgoing", "Whispers you send"},
                                                                         {"guild", "Guild / Officer"},
                                                                         {"say", "Nearby / Yell"},
                                                                         {"community", "Community"},
                                                                         {"public", "Public Channels"}}};
        y = 65;
        for (auto &[key, title] : groups) {
            auto h = page_control(1, L"BUTTON", title, 20, y, 400, 26, BS_AUTOCHECKBOX);
            bool on =
                std::find(config["groups"].begin(), config["groups"].end(), key) != config["groups"].end();
            SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            checks.push_back({h, "group:" + std::string(key)});
            y += 34;
        }
        settings_section(1);
        label(1, "Public channel numbers, comma-separated; empty means all", 20, 420);
        auto channels = edit(1, "channel_text", 20, 450, 600);
        edits.back().key = "__channels";
        std::string numbers;
        for (auto &n : config["channels"]) {
            if (!numbers.empty())
                numbers += ",";
            numbers += std::to_string(n.get<int>());
        }
        set(channels, numbers);
        checkbox(1, "skip_target_language_nicknames", "Skip names using the target language's script", 20, 490, 600);
        label(1, "Uses the chat target language (initially your system language). Player name only, excluding realm.", 20, 525);
        label(1, "Shared scripts cannot identify a language: English and French names can both use Latin letters.", 20, 551);
        settings_section(1);
        page_control(1, L"BUTTON", "OCR & Fallback Settings…", 20, 565, 600, 30, BS_PUSHBUTTON,
                     id_ocr_open);
        page_control(1, L"BUTTON", "Blocked players & addon sync…", 20, 606, 600, 30, BS_PUSHBUTTON | WS_TABSTOP,
                     id_blocks_open);
        label(2, "Translation method", 20, 54, 120);
        provider_select = page_control(2, WC_COMBOBOXW, "", 130, 50, 325, 240, CBS_DROPDOWNLIST | WS_TABSTOP,
                                       id_provider_select);
        page_control(2, L"BUTTON", "+ API", 465, 50, 82, 28, BS_PUSHBUTTON | WS_TABSTOP, id_provider_add);
        page_control(2, L"BUTTON", "Delete", 555, 50, 65, 28, BS_PUSHBUTTON | WS_TABSTOP,
                     id_provider_delete);
        provider_name_label = label(2, "API Connection Name", 20, 96, 100);
        provider_name =
            page_control(2, L"EDIT", "", 130, 90, 490, 27, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
        provider_key_label = page_control(2, L"STATIC", "API Key", 20, 129, 600, 22, SS_LEFT);
        api_key = page_control(2, L"EDIT", "", 20, 156, 600, 27,
                               WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP);
        SendMessageW(api_key, EM_SETLIMITTEXT, 4096, 0);
        provider_url_label = label(2, "API URL (base URL includes version path, e.g. /v1)", 20, 196, 395);
        edit(2, "base_url", 20, 223, 390);
        provider_model_label = label(2, "Model name (manual entry allowed)", 430, 196, 190);
        edit(2, "model", 430, 223, 190);
        provider_local_note = page_control(2, L"STATIC", "", 20, 92, 600, 45, SS_LEFT);
        page_control(2, L"BUTTON", "Prepare current language pack", 20, 147, 285, 34, BS_PUSHBUTTON | WS_TABSTOP, id_models_now);
        page_control(2, L"BUTTON", "Manage language packs…", 320, 147, 300, 34, BS_PUSHBUTTON | WS_TABSTOP, id_models_open);
        ollama_model_label = label(2, "Ollama model", 20, 152, 100);
        ollama_model_choice = page_control(2, WC_COMBOBOXW, "", 130, 147, 490, 200,
                                          CBS_DROPDOWNLIST | WS_TABSTOP, id_ollama_model);
        for (const auto &model : ollama_model_options) {
            auto caption = wide(std::string(model.name) + " · " + std::string(model.download_size));
            SendMessageW(ollama_model_choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(caption.c_str()));
        }
        SendMessageW(ollama_model_choice, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(wide(ui_text("Custom model name")).c_str()));
        ollama_custom_label = label(2, "Model name", 20, 190, 100);
        ollama_custom_model = page_control(2, L"EDIT", "", 130, 185, 490, 27,
                                          WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, id_ollama_custom);
        SendMessageW(ollama_custom_model, EM_SETLIMITTEXT, 128, 0);
        ollama_apply = page_control(2, L"BUTTON", "Apply model", 20, 225, 140, 30,
                                   BS_PUSHBUTTON | WS_TABSTOP, id_ollama_apply);
        provider_setup = page_control(2, L"BUTTON", "Download model / prepare portable Ollama", 175, 225, 445, 30,
                                      BS_PUSHBUTTON | WS_TABSTOP, id_ollama_setup);
        page_control(2, L"BUTTON", "Advanced", 20, 265, 140, 29, BS_PUSHBUTTON | WS_TABSTOP,
                     id_provider_advanced);
        page_control(2, L"BUTTON", "Send test request", 175, 265, 165, 29, BS_PUSHBUTTON | WS_TABSTOP,
                     id_provider_test);
        label(2, "Save API changes below", 355, 269, 265);
        load_providers();
        settings_section(2);
        label(2, "Balance & Usage (Saved Connection)", 32, 319, 330);
        page_control(2, L"BUTTON", "Refresh Balance", 450, 317, 158, 29, BS_PUSHBUTTON, id_balance);
        balance_amount = page_control(2, L"STATIC", "—", 32, 350, 390, 35, SS_LEFT);
        balance_state = page_control(2, L"STATIC", "Not queried yet", 430, 361, 178, 22, SS_RIGHT);
        balance_detail = page_control(2, L"STATIC", "", 32, 391, 575, 20, SS_LEFT);
        balance_updated = page_control(2, L"STATIC", "", 32, 413, 575, 18, SS_LEFT);
        settings_section(2);
        label(2, "Translate chat into", 20, 442, 175);
        chat_target = page_control(2, WC_COMBOBOXW, "", 205, 437, 240, 400,
                                   CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, id_chat_target);
        auto selected_chat_target = config.value("target_language", default_target_language());
        for (size_t i = 0; i < chat_targets.size(); ++i) {
            const auto caption = wide(ui_text(target_language_name(chat_targets[i])));
            SendMessageW(chat_target, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(caption.c_str()));
            if (chat_targets[i] == selected_chat_target)
                SendMessageW(chat_target, CB_SETCURSEL, i, 0);
        }
        label(2, "Language support depends on the engine and prepared models. Prepare packs for your target language.", 20, 464, 590);
        settings_section(2);
        usage_total = page_control(2, L"STATIC", "", 20, 496, 600, 22, SS_LEFT);
        usage_detail = page_control(2, L"STATIC", "", 20, 520, 600, 22, SS_LEFT);
        settings_section(2);
        incoming_prompt_help = label(2, "", 20, 540);
        label(2, "Auto chat translation prompt (separate from manual output)", 20, 558, 400);
        page_control(2, L"BUTTON", "Reset prompt", 440, 553, 180, 30, BS_PUSHBUTTON, id_incoming_prompt_reset);
        edit(2, "incoming_prompt", 20, 583, 600, 100, true);
        label(2, "Term-matching prompt prefix", 20, 690);
        edit(2, "glossary_prefix", 20, 715, 600, 52, true);
        label(3, "Shortcut to open output box", 20, 65);
        hotkey = page_control(3, HOTKEY_CLASSW, "", 20, 95, 250, 30, WS_TABSTOP);
        auto hk = config["hotkey"];
        int mods = hk.value("modifiers", 0), flags = ((mods & MOD_SHIFT) ? HOTKEYF_SHIFT : 0) |
                                                     ((mods & MOD_CONTROL) ? HOTKEYF_CONTROL : 0) |
                                                     ((mods & MOD_ALT) ? HOTKEYF_ALT : 0);
        SendMessageW(hotkey, HKM_SETRULES, 0, 0);
        SendMessageW(hotkey, HKM_SETHOTKEY, MAKEWORD(hk.value("key", 192), flags), 0);
        label(3, "Default · / ` key. After changes, click \"Save settings\" at the bottom.", 20, 140);
        settings_section(3);
        label(3, "Edit output prompt (must include {language})", 20, 198, 370);
        outbound_prompt_help = label(3, "", 20, 214);
        prompt_style = page_control(3, WC_COMBOBOXW, "", 440, 193, 180, 200, CBS_DROPDOWNLIST | WS_VSCROLL,
                                    id_prompt_style);
        SendMessageW(prompt_style, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(ui_text("Short")).c_str()));
        SendMessageW(prompt_style, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(ui_text("Default")).c_str()));
        SendMessageW(prompt_style, CB_SETCURSEL, config.value("outbound_style", "short") == "default" ? 1 : 0,
                     0);
        short_prompt = edit(3, "outbound_template", 20, 226, 600, 170, true);
        default_prompt = edit(3, "outbound_default_template", 20, 226, 600, 170, true);
        label(3, "Output terminology prompt prefix", 20, 416);
        edit(3, "outbound_terms_prefix", 20, 446, 600, 65, true);
        page_control(3, L"BUTTON", "Open output translation", 20, 546, 180, 30, BS_PUSHBUTTON, id_translate);
        page_control(3, L"BUTTON", "Restore default prompt for current style", 220, 546, 240, 30, BS_PUSHBUTTON,
                     id_output_prompt_reset);
        settings_section(3);
        label(3, "Output tip bubble", 20, 610);
        number_row("__bubble_transparency", "Bubble opacity (%)", 648, 0, 100,
                   config.value("bubble_transparency", 50), 3);
        number_row("__bubble_scale", "Bubble size (%)", 690, 50, 200, config.value("bubble_scale", 100), 3);
        page_control(3, L"BUTTON", "Preview tooltip", 20, 736, 180, 28, BS_PUSHBUTTON, id_bubble_preview);
        label(3, "Default: slightly above screen center; size includes text and padding, applies after saving.", 20, 776, 600);

        run_status = page_control(4, L"EDIT", "", 20, 65, 600, 180,
                                  ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER);
        settings_section(4);
        page_control(4, L"BUTTON", "Notes and component notices", 20, 465, 210, 30, BS_PUSHBUTTON, id_notices);
        page_control(4, L"BUTTON", "Open log directory", 250, 465, 180, 30, BS_PUSHBUTTON, id_logs);
        page_control(4, L"BUTTON", "Privacy", 450, 465, 170, 30, BS_PUSHBUTTON, id_privacy);
        settings_section(4);
        checkbox(4, "chat_logging", "Save chat and output logs (includes whispers and names)", 20, 525, 600);
        label(4, "Retention days (0: keep forever, 1–3650)", 20, 567, 400);
        auto retention = page_control(4, L"EDIT", std::to_string(config.value("log_retention_days", 0)),
                                      445, 562, 175, 28, WS_BORDER | WS_TABSTOP | ES_NUMBER);
        edits.push_back({retention, "__log_retention_days"});
        label(4, "Disabling stops new entries. Retention runs at startup, date changes and settings changes.", 20, 610, 600);
        page_control(4, L"BUTTON", "Delete saved chat logs", 20, 651, 300, 30, BS_PUSHBUTTON, id_logs_clear);
        label(4, "Cloud connections send text to the selected service; local engines process it on this computer.", 20, 705, 600);
        tab_draft = config.at("chat_tabs");
        label(5, "Tabs only affect display; translation scope follows original settings; history limit is shared.", 20, 58, 600);
        label(5, "Chat tabs", 20, 92, 100);
        tab_editor =
            page_control(5, L"COMBOBOX", "", 125, 87, 285, 200, CBS_DROPDOWNLIST | WS_TABSTOP, id_tab_editor);
        page_control(5, L"BUTTON", "Add", 425, 87, 90, 28, BS_PUSHBUTTON, id_tab_add);
        page_control(5, L"BUTTON", "Delete", 530, 87, 85, 28, BS_PUSHBUTTON, id_tab_delete);
        label(5, "Tab name", 20, 132, 100);
        tab_name = page_control(5, L"EDIT", "", 125, 127, 490, 28, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
        SendMessageW(tab_name, EM_SETLIMITTEXT, 96, 0);
        label(5, "Names and filters apply after saving; right-click the overlay tab to open this page quickly.", 20, 165, 600);
        settings_section(5);
        label(5, "Message types shown (uncheck to hide)", 20, 212, 600);
        for (size_t i = 0; i < chat_event_options().size(); ++i) {
            auto &[event, title] = chat_event_options()[i];
            auto h = page_control(5, L"BUTTON", title, i % 2 ? 340 : 20, 245 + int(i / 2) * 28, 270, 26,
                                  BS_AUTOCHECKBOX);
            tab_checks.emplace_back(h, event);
        }
        settings_section(5);
        label(5, "Public Channels: Include Only (empty = all)", 20, 518, 600);
        tab_channels =
            page_control(5, L"EDIT", "", 20, 546, 595, 28, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
        label(5, "Public Channels: Exclude (overrides include)", 20, 590, 600);
        tab_excluded =
            page_control(5, L"EDIT", "", 20, 618, 595, 28, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
        SendMessageW(tab_channels, EM_SETLIMITTEXT, 4096, 0);
        SendMessageW(tab_excluded, EM_SETLIMITTEXT, 4096, 0);
        label(5, "Enter numbers or channel names, comma-separated, e.g. 1, Trade", 20, 658, 600);
        page_control(5, L"BUTTON", "Reapply last export", 20, 710, 220, 30, BS_PUSHBUTTON, id_tab_import);
        label(5, "Run /bl windows in-game to auto-replace and save; no need to apply again.", 20, 751, 600);
        fill_tab_editor(active_tab);
        control(settings, L"BUTTON", "Quit", 20, 610, 145, 32, BS_PUSHBUTTON, id_exit);
        control(settings, L"BUTTON", "Save", 510, 610, 140, 32, BS_PUSHBUTTON, id_save);
        settings_status = control(settings, L"STATIC", "", 230, 610, 360, 32, SS_LEFT);
        muted_controls.insert(settings_status);
        theme_caption();
        refresh_interface_fonts();
        resize_settings();
        switch_page(0);
        if (first_launch && !smoke) {
            RECT overlay{}, dialog{};
            MONITORINFO work{sizeof(work)};
            if (GetWindowRect(main, &overlay) && GetWindowRect(settings, &dialog) &&
                GetMonitorInfoW(MonitorFromWindow(settings, MONITOR_DEFAULTTONEAREST), &work)) {
                const auto bounds = initial_overlay_rect(work.rcWork,
                    SIZE{overlay.right - overlay.left, overlay.bottom - overlay.top},
                    SIZE{dialog.right - dialog.left, dialog.bottom - dialog.top});
                SetWindowPos(main, nullptr, bounds.left, bounds.top, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                config["x"] = bounds.left;
                config["y"] = bounds.top;
                first_launch = false;
            }
        }
        ShowWindow(settings, SW_SHOW);
        SetForegroundWindow(settings);
        update_status();
    }
    void switch_page(int p) {
        if (p != 3)
            stop_bubble_preview();
        close_picker(false);
        body_scroll = 0;
        active_page = p;
        for (int i = 0; i < int(pages.size()); i++)
            for (auto h : pages[i])
                ShowWindow(h, i == p ? SW_SHOW : SW_HIDE);
        if (p == 2)
            fill_provider();
        show_prompt_editor();
        set(page_title, active_theme.widgets.plain_navigation && !beveled()
                            ? std::string("~/ ") + ui_text(page_titles[p])
                            : ui_text(page_titles[p]));
        set(page_description, page_descriptions[p]);
        for (auto h : navigation)
            InvalidateRect(h, nullptr, TRUE);
        resize_settings();
        InvalidateRect(body, nullptr, TRUE);
        if (p == 2 || p == 3) update_account_view();
        if (p == 2) {
            if (!balance_requested && engine->has_key() && engine->has_balance() && !smoke)
                refresh_balance();
        }
    }
    void style_notices() {
        SendMessageW(notices_text, EM_SETBKGNDCOLOR, 0, bg);
        CHARFORMAT2W format{}; format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR | CFM_BACKCOLOR; format.crTextColor = fg; format.crBackColor = bg;
        SendMessageW(notices_text, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
        InvalidateRect(notices_text, nullptr, TRUE);
    }
    void show_notices() {
        if (!notices) {
            notices = CreateWindowExW(WS_EX_APPWINDOW, L"BianLingo.Window",
                                      wide(ui_text("BianLingo · About & component notices")).c_str(), WS_OVERLAPPEDWINDOW, 180, 160,
                                      scale(720), scale(520), nullptr, nullptr, instance, nullptr);
            notices_text = control(notices, MSFTEDIT_CLASS, bundled_notices(), 8, 8, 680, 450,
                                   ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL);
            SendMessageW(notices_text, EM_SETBKGNDCOLOR, 0, bg);
        }
        RECT r{};
        GetClientRect(notices, &r);
        set(notices_text, bundled_json("usage-notice.json").at(ui_language() == "zh" ? "zh" : "en").get<std::string>());
        style_notices();
        MoveWindow(notices_text, scale(8, notices), scale(8, notices), r.right - scale(16, notices),
                   r.bottom - scale(16, notices), TRUE);
        ShowWindow(notices, SW_SHOW);
        SetForegroundWindow(notices);
    }
    void show_prompt_editor() {
        bool standard = SendMessageW(prompt_style, CB_GETCURSEL, 0, 0) == 1;
        ShowWindow(short_prompt, active_page == 3 && !standard ? SW_SHOW : SW_HIDE);
        ShowWindow(default_prompt, active_page == 3 && standard ? SW_SHOW : SW_HIDE);
        layout_body();
    }
    void save_settings() {
        try {
            Json s = config;
            if (!ocr_draft.is_null()) {
                capture_ocr_draft();
                s["ocr"] = ocr_draft;
            }
            Json groups = Json::array();
            for (auto &c : checks) {
                bool on = SendMessageW(c.h, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (c.key.rfind("group:", 0) == 0) {
                    if (on)
                        groups.push_back(c.key.substr(6));
                } else
                    s[c.key] = on;
            }
            s["groups"] = groups;
            const auto language_index = SendMessageW(interface_language, CB_GETCURSEL, 0, 0);
            if (language_index < 0 || language_index > LRESULT(ui_languages.size()))
                throw std::runtime_error("Unsupported interface language");
            s["ui_language"] = language_index == 0 ? "system" : std::string(ui_languages[size_t(language_index - 1)].code);
            const int selected_target = int(SendMessageW(chat_target, CB_GETCURSEL, 0, 0));
            if (selected_target < 0 || selected_target >= int(chat_targets.size()))
                throw std::runtime_error("Select a chat translation target language");
            s["target_language"] = std::string(chat_targets[selected_target]);
            for (auto &e : edits) {
                auto value = text(e.h);
                auto bubble_number = std::find_if(bubble_rows.begin(), bubble_rows.end(),
                                                  [&](const NumberRow &n) { return n.edit == e.h; });
                if (bubble_number != bubble_rows.end()) {
                    s[e.key.substr(2)] = number_value(*bubble_number);
                    continue;
                }
                auto number = std::find_if(number_rows.begin(), number_rows.end(),
                                           [&](const NumberRow &n) { return n.edit == e.h; });
                if (number != number_rows.end()) {
                    int v = number_value(*number);
                    if (e.key == "__background_transparency")
                        s["background_opacity"] = 1 - v / 100.0;
                    else
                        s[e.key == "__font" ? "font_size" : "max_messages"] = v;
                } else if (e.key.rfind("__skin_", 0) == 0) {
                    s["skin_colors"][e.key.substr(7)] = skin_color_hex(parse_skin_color(value));
                } else if (e.key == "__log_retention_days") {
                    size_t used = 0; int days = std::stoi(value, &used);
                    if (used != value.size() || days < 0 || days > 3650)
                        throw std::runtime_error("Log retention must be 0–3650 days");
                    s["log_retention_days"] = days;
                } else if (e.key == "__channels") {
                    Json ns = Json::array();
                    std::replace(value.begin(), value.end(), ',', ' ');
                    std::istringstream stream(value);
                    std::string piece;
                    while (stream >> piece) {
                        if (piece.empty() || piece.size() > 3 ||
                            !std::all_of(piece.begin(), piece.end(),
                                         [](char c) { return c >= '0' && c <= '9'; }))
                            throw std::runtime_error("Invalid channel number format");
                        ns.push_back(std::stoi(piece));
                    }
                    s["channels"] = ns;
                } else
                    s[e.key] = value;
            }
            capture_tab_editor();
            s["chat_tabs"] = tab_draft;
            if (s.at("ocr").value("enabled", false) &&
                (!config.at("ocr").value("enabled", false) ||
                 (s.at("ocr").value("always", false) && !config.at("ocr").value("always", false)))) {
                auto &selected_events = s["chat_tabs"][0]["events"];
                if (std::find(selected_events.begin(), selected_events.end(), "OCR_CHAT") ==
                    selected_events.end())
                    selected_events.push_back("OCR_CHAT");
            }
            normalize_chat_tabs(s);
            auto draft = draft_skin();
            s["ui_style"] = draft.at("ui_style");
            s["skin"] = draft.at("skin");
            s["skin_colors"] = draft.at("skin_colors");
            s["style_memory"] = draft.value("style_memory", Json::object());
            theme_registry.change_style(s, s.at("ui_style").get<std::string>());
            WORD value = WORD(SendMessageW(hotkey, HKM_GETHOTKEY, 0, 0));
            int flags = HIBYTE(value), mods = ((flags & HOTKEYF_SHIFT) ? MOD_SHIFT : 0) |
                                              ((flags & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) |
                                              ((flags & HOTKEYF_ALT) ? MOD_ALT : 0);
            if (!LOBYTE(value))
                throw std::runtime_error("Select a shortcut");
            s["hotkey"] = {{"key", LOBYTE(value)}, {"modifiers", mods}};
            bool hotkey_changed = s["hotkey"] != config["hotkey"];
            if (hotkey_changed && !register_hotkey(s["hotkey"])) {
                register_hotkey(config["hotkey"]);
                throw std::runtime_error("Shortcut in use by another program; please choose another");
            }
            capture_provider();
            s["active_connection"] = provider_draft.at(provider_index).at("id");
            bool provider_changed = provider_dirty();
            try {
                engine->configure(s, std::nullopt, provider_draft);
                s = engine->settings();
            } catch (...) {
                if (hotkey_changed)
                    register_hotkey(config["hotkey"]);
                throw;
            }
            const auto previous_language = ui_language();
            config = std::move(s);
            set_ui_language_preference(config.at("ui_language").get<std::string>());
            if (previous_language != ui_language()) refresh_ui_language(previous_language);
            load_providers();
            provider_testing = false;
            EnableWindow(GetDlgItem(body, id_provider_test), TRUE);
            appearance_preview.reset();
            balance_busy = false;
            if (provider_changed) {
                account = BalanceView{};
                balance_error.clear();
                balance_requested = false;
            }
            SetWindowTextW(api_key, L"");
            apply();
            render();
            stop_bubble_preview();
            update_bubble();
            set(settings_status, "Saved");
            update_account_view();
            if (!smoke && active_page == 2 && !balance_requested && engine->has_key() &&
                engine->has_balance())
                refresh_balance();
        } catch (const std::exception &e) {
            set(settings_status, e.what());
        }
    }
    void open_output() {
        stop_bubble_preview();
        if (!retry_reply)
            chosen_reply.reset();
        auto foreground = GetForegroundWindow();
        if (foreground != output) {
            previous_foreground = foreground;
            GetWindowThreadProcessId(foreground, &previous_pid);
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromWindow(foreground, MONITOR_DEFAULTTOPRIMARY), &monitor))
                bubble_area = monitor.rcWork;
        }
        if (!output) {
            int width = scale(460), height = scale(104);
            output = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"BianLingo.Window", wide(ui_text("BianLingo · Output translation")).c_str(),
                WS_POPUP | WS_BORDER, bubble_area.left + (bubble_area.right - bubble_area.left - width) / 2,
                bubble_area.bottom - height - scale(90), width, height, nullptr, nullptr, instance, nullptr);
            output_heading = control(output, L"STATIC", "Send translation", 10, 9, 190, 22, SS_LEFT);
            output_style = control(output, WC_COMBOBOXW, "", 202, 5, 94, 200, CBS_DROPDOWNLIST | WS_VSCROLL,
                                   id_output_style);
            SendMessageW(output_style, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(ui_text("Short")).c_str()));
            SendMessageW(output_style, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(ui_text("Default")).c_str()));
            SendMessageW(output_style, CB_SETCURSEL,
                         config.value("outbound_style", "short") == "default" ? 1 : 0, 0);
            target = control(output, WC_COMBOBOXW, "", 306, 5, 104, 250, CBS_DROPDOWNLIST | WS_VSCROLL);
            int selected = 0;
            for (size_t i = 0; i < output_languages.size(); ++i) {
                auto w = wide(ui_text(output_languages[i].first));
                SendMessageW(target, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
                if (output_languages[i].second == config.value("target", "English"))
                    selected = int(i);
            }
            SendMessageW(target, CB_SETCURSEL, selected, 0);
            control(output, L"BUTTON", "×", 420, 5, 28, 25, BS_PUSHBUTTON, id_output_close);
            input = control(output, L"EDIT", "", 10, 36, 438, 29, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
            SendMessageW(input, EM_SETLIMITTEXT, 12000, 0);
            output_status = control(output, L"STATIC", "", 10, 74, 438, 21, SS_LEFT);
            SetWindowSubclass(
                input,
                [](HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) -> LRESULT {
                    if (msg == WM_IME_STARTCOMPOSITION)
                        app->composing = true;
                    if (msg == WM_IME_ENDCOMPOSITION)
                        app->composing = false;
                    if (msg == WM_KEYDOWN && (w == VK_RETURN || w == VK_ESCAPE)) {
                        auto context = ImmGetContext(h);
                        bool marked =
                            context && ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0) > 0;
                        if (context)
                            ImmReleaseContext(h, context);
                        if (app->composing || marked)
                            return DefSubclassProc(h, msg, w, l);
                        if (w == VK_ESCAPE) {
                            app->hide_output();
                            return 0;
                        }
                        app->send_output();
                        return 0;
                    }
                    if (msg == WM_CHAR && w == VK_RETURN && !app->composing)
                        return 0;
                    return DefSubclassProc(h, msg, w, l);
                },
                1, 0);
        }
        if (!retry_reply) {
            auto preferred = config.value("target", "English");
            for (size_t i = 0; i < output_languages.size(); ++i)
                if (output_languages[i].second == preferred)
                    SendMessageW(target, CB_SETCURSEL, i, 0);
        }
        set(output_heading, chosen_reply ? ui_text("Reply") + " " + localized_reply_label(chosen_reply->label)
                                         : ui_text("Send translation"));
        set(output_status, chosen_reply ? chosen_reply->prefix + ui_text("Translation · Confirm language, press Enter to translate and copy")
                                        : "Enter to translate and copy · Esc to return to game");
        ShowWindow(output, SW_SHOW);
        SetForegroundWindow(output);
        SetFocus(input);
        SendMessageW(input, EM_SETSEL, 0, -1);
    }
    void hide_output() {
        if (picker_combo && GetParent(picker_combo) == output)
            close_picker(false);
        bool focused = GetForegroundWindow() == output;
        ShowWindow(output, SW_HIDE);
        if (retry_reply) {
            retry_reply = false;
            chosen_reply.reset();
        }
        DWORD pid = 0;
        if (IsWindow(previous_foreground))
            GetWindowThreadProcessId(previous_foreground, &pid);
        if (focused && pid && pid == previous_pid)
            SetForegroundWindow(previous_foreground);
    }
    int bubble_unit(int value) const {
        return std::max(1, MulDiv(scale(value, bubble), bubble_style.scale, 100));
    }
    HFONT bubble_font() {
        int offset = active_theme.fonts.size - 13;
        return ui_font(std::max(1, MulDiv(14 + offset, bubble_style.scale, 100)) - offset, false, bubble,
                       true);
    }
    void stop_bubble_preview() {
        if (!bubble_preview)
            return;
        bubble_preview = false;
        if (settings)
            set(GetDlgItem(body, id_bubble_preview), "Preview tooltip");
        update_bubble();
    }
    void toggle_bubble_preview() {
        if (bubble_preview) {
            stop_bubble_preview();
            return;
        }
        if (reply.phase != QuickReply::Hidden) {
            set(settings_status, "Output tip is still showing. Please preview again later.");
            return;
        }
        auto draft = Json::object();
        for (const auto &n : bubble_rows)
            draft[n.key.substr(2)] = number_value(n);
        bubble_appearance(draft);
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromWindow(settings, MONITOR_DEFAULTTOPRIMARY), &monitor))
            bubble_area = monitor.rcWork;
        bubble_preview = true;
        set(GetDlgItem(body, id_bubble_preview), "Disable Bubble Preview");
        update_bubble();
    }
    void update_bubble() {
        if (!bubble_preview && reply.phase == QuickReply::Hidden) {
            if (bubble)
                ShowWindow(bubble, SW_HIDE);
            return;
        }
        auto options = Json::object();
        if (bubble_preview)
            for (const auto &n : bubble_rows)
                options[n.key.substr(2)] = number_value(n);
        bubble_style = bubble_appearance(bubble_preview ? options : config);
        if (!bubble) {
            bubble = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED |
                                         WS_EX_TRANSPARENT,
                                     L"BianLingo.Window", wide(ui_text("Output translation tip")).c_str(), WS_POPUP | WS_BORDER, 0, 0,
                                     1, 1, nullptr, nullptr, instance, nullptr);
            if (!bubble)
                throw std::runtime_error("Failed to create output tooltip bubble");
        }
        if (bubble_area.right <= bubble_area.left || bubble_area.bottom <= bubble_area.top) {
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromWindow(main, MONITOR_DEFAULTTOPRIMARY), &monitor))
                bubble_area = monitor.rcWork;
        }
        SetLayeredWindowAttributes(bubble, 0, BYTE(std::lround(bubble_style.opacity() * 255)), LWA_ALPHA);
        bubble_caption = wide(ui_text(bubble_preview ? "Translating… · Tooltip preview" : reply.caption()));
        int margin = scale(8, bubble), pad_x = bubble_unit(18), pad_y = bubble_unit(11);
        int work_width = std::max(1, int(bubble_area.right - bubble_area.left));
        auto dc = GetDC(bubble);
        auto old = SelectObject(dc, bubble_font());
        RECT measured{0, 0, std::max(1, std::min(bubble_unit(440), work_width - 2 * (margin + pad_x) - 2)),
                      0};
        DrawTextW(dc, bubble_caption.c_str(), -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        ReleaseDC(bubble, dc);
        auto bounds = bubble_bounds({int(bubble_area.left), int(bubble_area.top), work_width,
                                     int(bubble_area.bottom - bubble_area.top)},
                                    measured.right + 2 * pad_x + 2, measured.bottom + 2 * pad_y + 2, margin);
        SetWindowTextW(bubble, bubble_caption.c_str());
        SetWindowPos(bubble, HWND_TOPMOST, bounds.x, bounds.y, bounds.w, bounds.h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(bubble, nullptr, TRUE);
    }
    void send_output() {
        stop_bubble_preview();
        if (reply.busy()) {
            set(output_status, "Previous message still translating, please wait; current input will be kept.");
            return;
        }
        try {
            auto value = text(input);
            if (trim(value).empty())
                return;
            int selected = int(SendMessageW(target, CB_GETCURSEL, 0, 0));
            auto language = std::string(output_languages.at(size_t(std::max(selected, 0))).second);
            if (!chosen_reply && config.value("target", "English") != language) {
                auto next = config;
                next["target"] = language;
                engine->configure(next);
                config = std::move(next);
            }
            engine->outbound(value, language);
            reply.begin();
            retry_reply = false;
            submitted_reply = chosen_reply;
            pending_input = value;
            set(input, "");
            hide_output();
            update_bubble();
        } catch (const std::exception &e) {
            set(output_status, e.what());
        }
    }
    void select_output_style() {
        auto next = config;
        next["outbound_style"] = SendMessageW(output_style, CB_GETCURSEL, 0, 0) == 1 ? "default" : "short";
        try {
            engine->configure(next);
            config = std::move(next);
        } catch (const std::exception &e) {
            SendMessageW(output_style, CB_SETCURSEL,
                         config.value("outbound_style", "short") == "default" ? 1 : 0, 0);
            set(output_status, e.what());
        }
    }
    bool copy_clipboard(std::string_view value) {
        auto w = wide(value);
        if (w.empty() || !OpenClipboard(main))
            return false;
        bool copied = false;
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
        if (memory) {
            auto p = GlobalLock(memory);
            if (p) {
                std::memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(memory);
                copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
                if (!copied)
                    GlobalFree(memory);
            } else
                GlobalFree(memory);
        }
        CloseClipboard();
        return copied;
    }
    bool copy_output() { return copy_clipboard(reply.result); }
    std::string localized_reply_label(std::string_view label) const {
        if (label.starts_with("Whisper ")) return ui_text("Whisper") + " " + std::string(label.substr(7));
        if (label.starts_with("Channel ")) return ui_text("Channel") + " " + std::string(label.substr(7));
        return ui_text(label);
    }
    void output_error(std::string message) {
        reply.fail(std::move(message), Clock::now());
        chosen_reply = submitted_reply;
        retry_reply = chosen_reply.has_value();
        if (text(input).empty())
            set(input, pending_input);
        set(output_status, reply.error);
        update_bubble();
    }
    static std::string localized_balance_detail(std::string value) {
        for (const auto prefix : {"Paid ", "Bonus "}) {
            size_t position = 0;
            const auto translated = ui_text(prefix);
            while ((position = value.find(prefix, position)) != std::string::npos) {
                value.replace(position, std::string(prefix).size(), translated);
                position += translated.size();
            }
        }
        return value;
    }
    void update_account_view() {
        if (!balance_amount)
            return;
        auto active = engine->settings().at("active_connection").get<std::string>();
        auto format = active_connection(engine->connections(), active).at("format");
        bool bergamot = format == "bergamot_local";
        bool ct2 = format == "ctranslate2_local";
        bool local = format == "ollama_local" || bergamot || ct2;
        bool changed = false;
        auto assign = [&](HWND h, const std::string &value) {
            if (text(h) != ui_text(value)) {
                set(h, value);
                changed = true;
            }
        };
        const auto prompt_help = provider_uses_prompts(format.get<std::string>())
                                     ? "API / Ollama uses these prompts. {language} becomes the selected target language."
                                     : "Bergamot / CTranslate2 does not use prompts. Edits are kept for API / Ollama connections.";
        assign(incoming_prompt_help, prompt_help);
        assign(outbound_prompt_help, prompt_help);
        assign(balance_amount, local ? "—" : account.amount);
        assign(balance_detail, bergamot ? "Bergamot translates locally; this connection is not billed per token." :
                               ct2 ? "CTranslate2 translates locally; this connection is not billed per token." :
                               local ? "Chat text is translated by the local model; the Token count below only tracks usage." :
                               balance_error.empty() ? localized_balance_detail(account.detail) : ui_text("Refresh failed:") + balance_error);
        assign(balance_updated, local ? "" : (account.updated.starts_with("Updated at ") ?
               ui_text("Updated at ") + account.updated.substr(std::string("Updated at ").size()) : account.updated));
        auto state = balance_busy             ? "Querying…"
                     : !balance_error.empty() ? "Refresh Failed"
                     : !engine->has_key()     ? "API Key not saved"
                                              : account.state;
        assign(balance_state, state);
        // Keep the last successful amount with its timestamp when a refresh fails.
        auto usage = format_usage(engine->usage(), ui_language());
        assign(usage_total, usage.total);
        assign(usage_detail, usage.detail);
        auto large = ui_font(22, true, settings, has_cjk(text(balance_amount)));
        if (reinterpret_cast<HFONT>(SendMessageW(balance_amount, WM_GETFONT, 0, 0)) != large)
            SendMessageW(balance_amount, WM_SETFONT, reinterpret_cast<WPARAM>(large), TRUE);
        if (!engine->has_balance()) {
            assign(balance_state, local ? "No balance for local models" : "Balance API not configured");
            if (!local)
                assign(balance_detail, "Balance can be configured in advanced settings; usage below is counted per current connection.");
        }
        auto button = GetDlgItem(body, id_balance);
        bool enabled = !balance_busy && engine->has_key() && engine->has_balance();
        if (bool(IsWindowEnabled(button)) != enabled)
            EnableWindow(button, enabled);
        assign(button, balance_busy ? "Querying…" : "Refresh Balance");
        if (changed && (active_page == 2 || active_page == 3))
            layout_body();
    }
    void refresh_balance() {
        if (balance_busy)
            return;
        try {
            if (provider_dirty())
                throw std::runtime_error("Save the current connection before refreshing the balance");
            engine->query_balance();
            balance_busy = true;
            balance_requested = true;
            balance_error.clear();
        } catch (const std::exception &e) {
            balance_error = e.what();
        }
        update_account_view();
    }
    void update_status() {
        if (!settings || !IsWindowVisible(settings))
            return;
        if (ocr_window && IsWindowVisible(ocr_window))
            refresh_ocr_ui();
        std::string value = std::string("BianLingo ") + BL_VERSION + "\n" +
                            engine->ocr_status() + "\n\n" + engine->status() +
                            "\n\n" + status_note + "\n";
        // Native EDIT controls use CRLF. Compare the displayed representation so
        // the whole status control is not repainted on every 100 ms UI tick.
        std::string lines;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '\n' && (i == 0 || value[i - 1] != '\r'))
                lines += '\r';
            lines += value[i];
        }
        value = std::move(lines);
        if (text(run_status) != value)
            set(run_status, value);
        if (settings_status && last_status_note != status_note) {
            last_status_note = status_note;
            set(settings_status, status_note);
        }
        if (active_page == 2)
            update_account_view();
    }
    void poll() {
        poll_model_job();
        poll_ollama_setup();
        sync_tab_flash();
        update_overlay_handles();
        update_settings_scrollbar();
        update_scrollbar();
        bool dirty = false;
        for (auto &n : engine->drain()) {
            if (n.kind == Notice::ChatLayout && n.layout) {
                try {
                    if (apply_game_tabs(*n.layout))
                        dirty = false; // Includes earlier chat notices in this batch.
                } catch (const std::exception &e) {
                    status_note = std::string("Game layout sync failed; original layout kept:") + e.what();
                    if (settings_status)
                        set(settings_status, status_note);
                }
            } else if (n.kind == Notice::Blocklist) {
                if (engine->blocklist() == ui_blocks.snapshot(true)) continue;
                refresh_blocklist();
                dirty = true;
            } else if (n.kind == Notice::Chat) {
                if (!n.chat.error.empty()) {
                    status_note = n.chat.error;
                    continue;
                }
                dirty = receive_chat(std::move(n.chat)) || dirty;
            } else if (n.kind == Notice::Outbound) {
                if (reply.receive(!submitted_reply ? std::move(n.text)
                                                   : compose_reply(*submitted_reply, n.text)))
                    clipboard_attempts = 0;
            } else if (n.kind == Notice::OutboundError) {
                if (reply.busy())
                    output_error(n.text);
            } else if (n.kind == Notice::Balance) {
                if (n.balance)
                    account = *n.balance;
                balance_busy = false;
                balance_error.clear();
                update_account_view();
            } else if (n.kind == Notice::BalanceError) {
                balance_busy = false;
                balance_error = n.text;
                update_account_view();
            } else if (n.kind == Notice::ProviderTest || n.kind == Notice::ProviderTestError) {
                provider_testing = false;
                if (body)
                    EnableWindow(GetDlgItem(body, id_provider_test), TRUE);
                status_note = n.text;
                if (settings_status)
                    set(settings_status, n.text);
            } else {
                status_note = n.text;
            }
        }
        if (reply.phase == QuickReply::Clipboard) {
            if (copy_output()) {
                reply.copied(Clock::now());
                pending_input.clear();
                update_bubble();
            } else if (++clipboard_attempts >= 10)
                output_error("Copy failed: clipboard busy, please resubmit translation");
        }
        if (reply.tick(Clock::now()))
            update_bubble();
        if (dirty && !message_preview)
            render();
        update_status();
    }
    void toggle_lock() {
        config["click_through"] = false;
        engine->configure(config);
        apply();
        ShowWindow(main, SW_SHOWNOACTIVATE);
    }
    void save_geometry() {
        if (smoke)
            return;
        RECT r{};
        GetWindowRect(main, &r);
        config["x"] = r.left;
        config["y"] = r.top;
        config["width"] = MulDiv(r.right - r.left, 96, int(GetDpiForWindow(main)));
        config["height"] = MulDiv(r.bottom - r.top, 96, int(GetDpiForWindow(main)));
        try {
            engine->configure(config);
        } catch (const std::exception &e) {
            status_note = e.what();
        }
    }
};
LRESULT CALLBACK procedure(HWND h, UINT message, WPARAM w, LPARAM l) {
    if (!app)
        return DefWindowProcW(h, message, w, l);
    if (app && app->picker && (message == WM_ACTIVATEAPP && !w))
        app->close_picker(false);
    if (app && app->picker && (h == app->settings || h == app->output) &&
        (message == WM_MOVING || message == WM_SIZING))
        app->close_picker(false);
    try {
        if (h == app->reply_surface)
            return app->reply_surface_message(message, w, l);
        if (h == app->tab_input_surface)
            return app->tab_input_surface_message(message, w, l);
        if (h == app->gear_input_surface)
            return app->gear_input_surface_message(message, w, l);
        if (h == app->resize_input_surface)
            return app->resize_input_surface_message(message, w, l);
        if (h == app->picker)
            return app->picker_input(message, w, l);
        switch (message) {
        case WM_MOUSEACTIVATE:
            if (h == app->bubble)
                return MA_NOACTIVATE;
            break;
        case WM_MOUSEWHEEL:
            if (h == app->main || h == app->scrollbar) {
                app->scroll_chat_wheel(GET_WHEEL_DELTA_WPARAM(w));
                return 0;
            }
            if (h == app->body || h == app->settings || h == app->body_scrollbar) {
                app->scroll_body(-GET_WHEEL_DELTA_WPARAM(w) * app->scale(36, app->settings) / WHEEL_DELTA);
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (h == app->body_scrollbar) {
                RECT r = app->body_thumb();
                int y = GET_Y_LPARAM(l);
                app->body_drag_offset = y >= r.top && y < r.bottom ? y - r.top : (r.bottom - r.top) / 2;
                app->body_dragging = true;
                SetCapture(h);
                app->drag_body(y);
                return 0;
            }
            if (h == app->scrollbar) {
                auto thumb = app->scroll_thumb(app->scroll_state());
                int y = GET_Y_LPARAM(l);
                app->scroll_drag_offset =
                    (y >= thumb.top && y < thumb.bottom) ? y - thumb.top : (thumb.bottom - thumb.top) / 2;
                app->scroll_dragging = true;
                SetCapture(h);
                app->drag_scrollbar(y);
                return 0;
            }
            break;
        case WM_MOUSEMOVE:
            if (h == app->body_scrollbar && app->body_dragging) {
                app->drag_body(GET_Y_LPARAM(l));
                return 0;
            }
            if (h == app->scrollbar && app->scroll_dragging) {
                app->drag_scrollbar(GET_Y_LPARAM(l));
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (h == app->body_scrollbar && app->body_dragging) {
                app->body_dragging = false;
                ReleaseCapture();
                return 0;
            }
            if (h == app->scrollbar && app->scroll_dragging) {
                app->scroll_dragging = false;
                ReleaseCapture();
                app->update_scrollbar();
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            if (h == app->body_scrollbar) {
                app->body_dragging = false;
                return 0;
            }
            if (h == app->scrollbar) {
                app->scroll_dragging = false;
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            if (h == app->model_window || h == app->block_window || h == app->ocr_window || h == app->provider_advanced || h == app->settings || h == app->body ||
                h == app->output || h == app->bubble || h == app->notices)
                return 1;
            if (h == app->body_scrollbar)
                return 1;
            if (h == app->scrollbar)
                return 1;
            if (h == app->main) {
                RECT bounds{};
                GetClientRect(h, &bounds);
                auto dc = reinterpret_cast<HDC>(w);
                FillRect(dc, &bounds, app->keyed_brush);
                if (app->handles_visible && app->active_theme.chat.tabs != ChatTabVisual::Classic) {
                    // Only small interaction handles are visible; the pane stays keyed out.
                    RECT edge = bounds;
                    for (int i = 0; i < app->scale(3, h); ++i) {
                        FrameRect(dc, &edge, app->field);
                        InflateRect(&edge, -1, -1);
                    }
                }
                return 1;
            }
            break;
        case WM_PRINTCLIENT:
            if (h == app->model_window || h == app->block_window || h == app->ocr_window || h == app->provider_advanced || h == app->settings || h == app->body ||
                h == app->output || h == app->bubble || h == app->body_scrollbar || h == app->scrollbar ||
                h == app->notices) {
                app->buffered(h, reinterpret_cast<HDC>(w),
                              [&](HWND window, HDC dc) { app->paint_window(window, dc); });
                return 0;
            }
            break;
        case WM_PAINT:
            if (h == app->model_window || h == app->block_window || h == app->ocr_window || h == app->provider_advanced || h == app->settings || h == app->body ||
                h == app->output || h == app->bubble || h == app->body_scrollbar || h == app->scrollbar ||
                h == app->notices) {
                PAINTSTRUCT paint{};
                auto dc = BeginPaint(h, &paint);
                try {
                    app->buffered(h, dc, [&](HWND window, HDC memory) { app->paint_window(window, memory); });
                } catch (...) {
                    EndPaint(h, &paint);
                    throw;
                }
                EndPaint(h, &paint);
                return 0;
            }
            break;
        case WM_NCCALCSIZE:
            if (h == app->main)
                return 0; // The dark client fills the entire resizable popup.
            break;
        case WM_NCACTIVATE:
            if (h == app->main)
                return TRUE; // Do not repaint a light inactive system frame.
            break;
        case WM_ACTIVATE:
            if (h == app->output && LOWORD(w) != WA_INACTIVE && app->input) {
                SetFocus(app->input);
                return 0;
            }
            break;
        case WM_WINDOWPOSCHANGED:
            if (h == app->main) {
                app->sync_background();
                app->sync_reply_surface();
            }
            break;
        case WM_SHOWWINDOW:
            if (h == app->main && !w && app->backdrop) {
                ShowWindow(app->backdrop, SW_HIDE);
                if (app->reply_surface)
                    ShowWindow(app->reply_surface, SW_HIDE);
                if (app->tab_input_surface)
                    ShowWindow(app->tab_input_surface, SW_HIDE);
                if (app->gear_input_surface)
                    ShowWindow(app->gear_input_surface, SW_HIDE);
                if (app->resize_input_surface)
                    ShowWindow(app->resize_input_surface, SW_HIDE);
            }
            break;
        case WM_SIZE:
            if (h == app->model_window) { app->layout_model_window(); return 0; }
            if (h == app->provider_advanced) {
                app->layout_provider_advanced();
                return 0;
            }
            if (h == app->main) {
                app->resize_main();
                app->sync_background();
            } else if (h == app->block_window)
                app->layout_block_window();
            else if (h == app->settings && app->body)
                app->resize_settings();
            else if (h == app->notices && app->notices_text) {
                RECT r{};
                GetClientRect(h, &r);
                MoveWindow(app->notices_text, app->scale(8, h), app->scale(8, h),
                           std::max(1, int(r.right) - app->scale(16, h)),
                           std::max(1, int(r.bottom) - app->scale(16, h)), TRUE);
            }
            return 0;
        case WM_DPICHANGED:
            app->change_dpi(h, LOWORD(w), *reinterpret_cast<RECT *>(l));
            return 0;
        case WM_GETMINMAXINFO:
            if (h == app->main) {
                auto p = reinterpret_cast<MINMAXINFO *>(l);
                p->ptMinTrackSize = {app->scale(220), app->scale(90)};
            }
            return 0;
        case WM_EXITSIZEMOVE:
            if (h == app->main)
                app->save_geometry();
            return 0;
        case WM_NCHITTEST:
            if (h == app->bubble || h == app->backdrop)
                return HTTRANSPARENT;
            if (h == app->output) {
                POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                ScreenToClient(h, &p);
                if (p.y < app->scale(32, h) && p.x < app->scale(290, h))
                    return HTCAPTION;
            }
            if (h == app->main) {
                POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                ScreenToClient(h, &p);
                RECT r{};
                GetClientRect(h, &r);
                const int edge = app->scale(6, h);
                const bool left = p.x < edge, right = p.x >= r.right - edge;
                const bool top = p.y < edge, bottom = p.y >= r.bottom - edge;
                if (top)
                    return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
                if (bottom)
                    return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
                if (left)
                    return HTLEFT;
                if (right)
                    return HTRIGHT;
                if (p.y < app->scale(23) && p.x < r.right - app->scale(35))
                    return HTCAPTION;
                return HTCLIENT;
            }
            break;
        case WM_HOTKEY:
            if (w == 1) {
                if (app->output && IsWindowVisible(app->output))
                    app->hide_output();
                else
                    app->open_output();
            } else if (w == 2)
                app->toggle_lock();
            return 0;
        case WM_HSCROLL:
            if (h == app->body)
                return SendMessageW(app->settings, message, w, l);
            if (h == app->settings)
                app->number_slide(reinterpret_cast<HWND>(l));
            break;
        case WM_COMMAND:
            if (h == app->settings && LOWORD(w) == id_chat_target && HIWORD(w) == CBN_SELCHANGE &&
                app->model_window && IsWindowVisible(app->model_window) && !app->model_task) {
                app->choose_current_pack(); return 0;
            }
            if (h == app->model_window && LOWORD(w) == id_models_search && HIWORD(w) == EN_CHANGE) {
                app->fill_model_pairs(); return 0;
            }
            if (h == app->block_window && reinterpret_cast<HWND>(l) == app->block_list && HIWORD(w) == LBN_SELCHANGE) {
                auto index = SendMessageW(app->block_list, LB_GETCURSEL, 0, 0);
                if (index >= 0 && size_t(index) < app->block_names.size()) set(app->block_name, app->block_names[size_t(index)]);
                return 0;
            }
            if (h == app->body)
                return SendMessageW(app->settings, message, w, l);
            if (h == app->settings && LOWORD(w) >= id_nav0 &&
                LOWORD(w) < id_nav0 + int(app->navigation.size())) {
                app->switch_page(LOWORD(w) - id_nav0);
                return 0;
            }
            if (h == app->settings && HIWORD(w) == EN_CHANGE)
                app->appearance_edit(reinterpret_cast<HWND>(l));
            if (h == app->settings && LOWORD(w) >= id_skin_color0 && LOWORD(w) < id_skin_color0 + 4 &&
                HIWORD(w) == BN_CLICKED) {
                app->choose_skin_color(LOWORD(w) - id_skin_color0);
                return 0;
            }
            switch (LOWORD(w)) {
            case id_models_open:
                app->open_model_window(); return 0;
            case id_models_now:
                app->open_model_window();
                if (!app->model_task) { app->choose_current_pack(); app->start_model_job(); }
                return 0;
            case id_models_current:
                app->choose_current_pack(); return 0;
            case id_models_all:
                app->model_queue.clear(); app->update_model_queue(); return 0;
            case id_models_add:
                app->add_selected_model_pair(); return 0;
            case id_models_remove:
                app->remove_selected_model_pairs(); return 0;
            case id_models_prepare:
                app->start_model_job(); return 0;
            case id_models_cancel:
                app->cancel_model_job(); return 0;
            case id_models_folder:
                std::filesystem::create_directories(app->model_cache());
                ShellExecuteW(nullptr, L"open", app->model_cache().c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0;
            case id_blocks_open:
                app->open_block_window(); return 0;
            case id_blocks_add:
                app->change_block(text(app->block_name), true); return 0;
            case id_blocks_remove: {
                auto index = SendMessageW(app->block_list, LB_GETCURSEL, 0, 0);
                if (index >= 0 && size_t(index) < app->block_names.size()) app->change_block(app->block_names[size_t(index)], false);
                return 0;
            }
            case id_blocks_sync:
                app->copy_block_code(); return 0;
            case IDOK:
                if (h == app->block_window) { app->change_block(text(app->block_name), true); return 0; }
                break;
            case id_ocr_open:
                app->open_ocr_settings();
                break;
            case id_ocr_region:
                app->select_ocr_region();
                break;
            case id_ocr_close:
                app->close_ocr_settings();
                break;
            case id_ocr_install:
            case id_ocr_test:
                app->capture_ocr_draft();
                if (app->ocr_draft != app->engine->settings().at("ocr"))
                    set(app->ocr_info, "Return to main settings and save before installing language packs or testing");
                else if (LOWORD(w) == id_ocr_install)
                    app->engine->install_ocr_models();
                else
                    app->engine->test_ocr();
                break;
            case id_tab_manage:
                app->open_settings();
                app->switch_page(5);
                break;
            case id_tab_editor:
                if (HIWORD(w) == CBN_SELCHANGE)
                    app->select_tab_editor();
                break;
            case id_tab_add:
                app->add_tab();
                break;
            case id_tab_delete:
                app->delete_tab();
                break;
            case id_tab_import:
                app->import_game_tabs();
                break;
            case id_bubble_preview:
                app->toggle_bubble_preview();
                break;
            case id_chat_preview:
                app->toggle_message_preview();
                break;
            case id_theme_import:
                app->import_theme();
                break;
            case id_theme_export:
                app->export_theme();
                break;
            case id_theme_reload:
                app->reload_themes();
                break;
            case id_theme_folder:
                app->open_theme_directory();
                break;
            case id_design:
                if (HIWORD(w) == CBN_SELCHANGE)
                    app->select_design();
                break;
            case id_skin:
                if (HIWORD(w) == CBN_SELCHANGE)
                    app->select_skin();
                break;
            case id_skin_reset:
                SendMessageW(app->skin_select, CB_SETCURSEL, 0, 0);
                app->select_skin();
                break;
            case id_gear:
                app->open_settings();
                break;
            case id_save:
                app->save_settings();
                break;
            case id_exit:
                DestroyWindow(app->main);
                break;
            case id_translate:
                if (h == app->output)
                    app->send_output();
                else
                    app->open_output();
                break;
            case id_output_close:
                app->hide_output();
                break;
            case id_incoming_prompt_reset:
                for (auto &edit : app->edits)
                    if (edit.key == "incoming_prompt")
                        set(edit.h, default_settings().at(edit.key).get<std::string>());
                set(app->settings_status, "Default chat prompt restored. Save settings to apply.");
                break;
            case id_output_prompt_reset:
                for (auto &edit : app->edits)
                    if (edit.key == (SendMessageW(app->prompt_style, CB_GETCURSEL, 0, 0) == 1
                                         ? "outbound_default_template"
                                         : "outbound_template"))
                        set(edit.h, default_settings().at(edit.key).get<std::string>());
                set(app->settings_status, "Default prompt for current style filled in; click \"Save Settings\" to apply");
                break;
            case id_prompt_style:
                if (HIWORD(w) == CBN_SELCHANGE)
                    app->show_prompt_editor();
                break;
            case id_output_style:
                if (HIWORD(w) == CBN_SELCHANGE)
                    app->select_output_style();
                if (HIWORD(w) == CBN_SELENDOK)
                    SetFocus(app->input);
                break;
            case id_logs: {
                auto directory = platform::app_data() / "Logs";
                std::filesystem::create_directories(directory);
                ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            }
            case id_notices:
                app->show_notices();
                break;
            case id_privacy:
                app->show_notices();
                set(app->notices_text, bundled_json("usage-notice.json").at(ui_language() == "zh" ? "privacy_zh" : "privacy_en").get<std::string>());
                app->style_notices();
                break;
            case id_logs_clear:
                if (MessageBoxW(app->settings, wide(ui_text("Delete all chat logs? This cannot be undone.")).c_str(),
                                wide(ui_text("Delete saved chat logs")).c_str(), MB_YESNO | MB_ICONWARNING) == IDYES)
                    app->engine->clear_chat_logs();
                break;
            case id_provider_select:
                if (HIWORD(w) == CBN_SELCHANGE)
                    app->select_provider();
                return 0;
            case id_provider_add:
                app->add_provider();
                return 0;
            case id_provider_local:
                app->add_local_provider();
                return 0;
            case id_provider_bergamot:
                app->add_bergamot_provider();
                return 0;
            case id_ollama_setup:
                app->setup_ollama();
                return 0;
            case id_ollama_model:
                if (HIWORD(w) == CBN_SELCHANGE)
                    app->select_ollama_model();
                return 0;
            case id_ollama_custom:
                if (HIWORD(w) == EN_CHANGE)
                    app->select_ollama_model();
                return 0;
            case id_ollama_apply:
                app->apply_ollama_model();
                return 0;
            case id_provider_delete:
                app->delete_provider();
                return 0;
            case id_provider_advanced:
                app->open_provider_advanced();
                return 0;
            case id_provider_test:
                app->test_provider();
                return 0;
            case id_provider_apply:
                app->apply_provider_advanced();
                return 0;
            case id_provider_cancel:
                app->close_provider_advanced();
                return 0;
            case IDCANCEL:
                if (h == app->ocr_window) {
                    app->close_ocr_settings();
                    return 0;
                }
                if (h == app->provider_advanced) {
                    app->close_provider_advanced();
                    return 0;
                }
                break;
            case id_balance:
                app->refresh_balance();
                break;
            }
            return 0;
        case WM_NOTIFY:
            if (h == app->body)
                return SendMessageW(app->settings, message, w, l);
            break;
        case WM_MEASUREITEM:
            if (app->measure_message_menu(reinterpret_cast<MEASUREITEMSTRUCT *>(l))) return TRUE;
            break;
        case WM_DRAWITEM: {
            if (app->draw_message_menu(reinterpret_cast<DRAWITEMSTRUCT *>(l))) return TRUE;
            auto d = reinterpret_cast<DRAWITEMSTRUCT *>(l);
            if (d->CtlType == ODT_BUTTON) {
                app->buffered(d->hwndItem, d->hDC, [&](HWND, HDC memory) {
                    auto dc = memory;
                    int id = GetDlgCtrlID(d->hwndItem);
                    if (app->beveled() && id != id_gear) {
                        app->paint_beveled_button(*d, dc);
                        return;
                    }
                    auto brush = reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH));
                    bool nav = id >= id_nav0 && id < id_nav0 + int(app->navigation.size());
                    bool clear_gear = id == id_gear;
                    app->paint_control_background(d->hwndItem, dc);
                    auto oldBrush = SelectObject(dc, brush),
                         oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
                    bool selected = nav && id - id_nav0 == app->active_page;
                    const auto &tokens = app->active_theme.colors;
                    COLORREF fill = id == id_save ? accent
                                    : nav         ? app->rgb(tokens.selection)
                                                  : app->rgb(tokens.button);
                    if (id == id_gear)
                        fill = panel;
                    if ((!nav || selected) && !clear_gear) {
                        SetDCBrushColor(dc, (d->itemState & ODS_SELECTED) ? mix(fill, fg) : fill);
                        RECT shape = d->rcItem;
                        InflateRect(&shape, -1, -1);
                        auto color = (d->itemState & ODS_SELECTED) ? mix(fill, fg) : fill;
                        if (!IsWindowEnabled(d->hwndItem))
                            color = panel;
                        else if (d->itemState & ODS_SELECTED)
                            color = app->rgb(tokens.pressed);
                        rounded(dc, shape, color,
                                float(app->scale(id == id_gear ? app->active_theme.metrics.control_radius
                                                               : app->active_theme.metrics.button_radius,
                                                 d->hwndItem)),
                                app->rgb(tokens.border), app->active_theme.metrics.border_width);
                    }
                    SelectObject(dc, oldPen);
                    SelectObject(dc, oldBrush);
                    if (nav && !app->active_theme.widgets.plain_navigation) {
                        auto iconPen =
                            CreatePen(PS_SOLID, app->scale(1, d->hwndItem),
                                      selected ? app->rgb(tokens.selected_text) : app->rgb(tokens.muted));
                        auto old = SelectObject(dc, iconPen);
                        auto b = SelectObject(dc, GetStockObject(NULL_BRUSH));
                        int x = d->rcItem.left + app->scale(16, d->hwndItem),
                            y = d->rcItem.top + app->scale(11, d->hwndItem), u = app->scale(16, d->hwndItem);
                        RoundRect(dc, x, y, x + u, y + u, app->scale(4, d->hwndItem),
                                  app->scale(4, d->hwndItem));
                        MoveToEx(dc, x + u / 4, y + u / 3, nullptr);
                        LineTo(dc, x + 3 * u / 4, y + u / 3);
                        MoveToEx(dc, x + u / 4, y + 2 * u / 3, nullptr);
                        LineTo(dc, x + (id - id_nav0) % 2 * u / 4 + u / 2, y + 2 * u / 3);
                        SelectObject(dc, b);
                        SelectObject(dc, old);
                        DeleteObject(iconPen);
                    }
                    if (id == id_gear) {
                        app->draw_brand_icon(dc, d->rcItem, (d->itemState & ODS_SELECTED) != 0);
                        return;
                    }
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, !IsWindowEnabled(d->hwndItem) ? app->rgb(tokens.disabled)
                                     : id == id_save               ? app->rgb(tokens.on_accent)
                                     : clear_gear                  ? app->rgb(app->active_theme.chat.glow)
                                     : selected                    ? app->rgb(tokens.selected_text)
                                     : nav                         ? app->rgb(tokens.muted)
                                                                   : fg);
                    auto button_font = reinterpret_cast<HFONT>(SendMessageW(d->hwndItem, WM_GETFONT, 0, 0));
                    SelectObject(dc, button_font);
                    auto title = wide((nav && app->active_theme.widgets.plain_navigation
                                           ? std::string(selected ? "> " : "  ")
                                           : std::string()) +
                                      text(d->hwndItem));
                    RECT titleRect = d->rcItem;
                    if (nav)
                        titleRect.left +=
                            app->scale(app->active_theme.widgets.plain_navigation ? 14 : 44, d->hwndItem);
                    if (GetDlgCtrlID(d->hwndItem) >= id_skin_color0 &&
                        GetDlgCtrlID(d->hwndItem) < id_skin_color0 + 4)
                        titleRect.right -= app->scale(20, d->hwndItem);
                    app->draw_wrapped_caption(dc, title, titleRect, nav ? DT_LEFT : DT_CENTER,
                                              app->scale(nav ? 4 : 10, d->hwndItem));
                    int color_index = GetDlgCtrlID(d->hwndItem) - id_skin_color0;
                    if (color_index >= 0 && color_index < 4) {
                        auto color = app->draft_color(color_index);
                        RECT swatch = d->rcItem;
                        swatch.left = swatch.right - app->scale(18);
                        InflateRect(&swatch, -3, -5);
                        auto dcbrush = reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH));
                        SetDCBrushColor(dc, color);
                        FillRect(dc, &swatch, dcbrush);
                    }
                    if ((d->itemState & ODS_FOCUS) && !clear_gear) {
                        RECT focus = d->rcItem;
                        InflateRect(&focus, -3, -3);
                        DrawFocusRect(dc, &focus);
                    }
                });
                return TRUE;
            }
        } break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN: {
            SetTextColor(reinterpret_cast<HDC>(w), reinterpret_cast<HWND>(l) == app->balance_amount ? accent
                                                   : reinterpret_cast<HWND>(l) == app->balance_state
                                                       ? (!app->balance_error.empty() ? RGB(246, 174, 112)
                                                          : app->account.available    ? accent
                                                                                      : mix(fg, panel))
                                                   : app->muted_controls.contains(reinterpret_cast<HWND>(l))
                                                       ? app->rgb(app->active_theme.colors.muted)
                                                       : fg);
            auto control = reinterpret_cast<HWND>(l);
            if (app->beveled() && control == app->page_title)
                SetTextColor(reinterpret_cast<HDC>(w), RGB(245, 240, 222));
            if (control == app->brand || control == app->brand_detail || control == app->version_label) {
                SetBkColor(reinterpret_cast<HDC>(w), app->sidebar_color());
                return reinterpret_cast<LRESULT>(app->sidebar_brush);
            }
            bool card = GetParent(control) == app->body && app->in_card(control);
            bool raised = message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX || card;
            SetBkColor(reinterpret_cast<HDC>(w), raised ? panel : bg);
            return reinterpret_cast<LRESULT>(raised ? app->field : app->brush);
        }
        case WM_TIMER:
            if (w == 1)
                app->poll();
            else if (w == 3)
                app->animate_tabs();
            else if (w == 99) {
                app->open_settings();
                app->open_output();
                app->show_notices();
                DestroyWindow(app->main);
            }
            return 0;
        case tray_message:
            if (l == WM_LBUTTONDBLCLK) {
                app->toggle_lock();
                app->open_settings();
            } else if (l == WM_RBUTTONUP) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, id_gear, wide(ui_text("Settings")).c_str());
                AppendMenuW(menu, MF_STRING, 100, wide(ui_text("Unlock and show overlay")).c_str());
                AppendMenuW(menu, MF_STRING, id_exit, wide(ui_text("Exit")).c_str());
                POINT p{};
                GetCursorPos(&p);
                SetForegroundWindow(app->main);
                int selected =
                    TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, app->main, nullptr);
                DestroyMenu(menu);
                if (selected == 100)
                    app->toggle_lock();
                else if (selected)
                    SendMessageW(app->main, WM_COMMAND, selected, 0);
            }
            return 0;
        case WM_CLOSE:
            if (h == app->ocr_window) {
                app->close_ocr_settings();
                return 0;
            }
            if (h == app->provider_advanced) {
                app->close_provider_advanced();
                return 0;
            }
            if (h == app->output) {
                app->hide_output();
                return 0;
            }
            if (h == app->settings || h == app->notices || h == app->block_window || h == app->model_window) {
                if (h == app->settings)
                    app->restore_appearance();
                ShowWindow(h, SW_HIDE);
                return 0;
            }
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            if (h == app->main) {
                if (app->reply_surface)
                    ShowWindow(app->reply_surface, SW_HIDE);
                if (app->tab_input_surface)
                    ShowWindow(app->tab_input_surface, SW_HIDE);
                KillTimer(h, 1);
                UnregisterHotKey(h, 1);
                UnregisterHotKey(h, 2);
                PostQuitMessage(0);
            }
            return 0;
        }
    } catch (const std::exception &e) {
        if (app->model_window && IsWindowVisible(app->model_window) && app->model_progress) {
            set(app->model_progress, e.what());
        } else if (app->block_window && IsWindowVisible(app->block_window) && app->block_status) {
            app->block_sync_note = e.what();
            set(app->block_status, e.what());
        } else if (app->settings_status)
            set(app->settings_status, e.what());
        else
            app->status_note = e.what();
    }
    return DefWindowProcW(h, message, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    auto install_name = platform::executable_dir().generic_u8string();
    auto mutex_name = wide("Local\\BianLingo.Instance." +
                           sha256(lower_ascii(std::string(install_name.begin(), install_name.end()))));
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
    if (!mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_HOTKEY_CLASS |
                                                        ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int code = 0;
    try {
#ifndef BL_UI_TESTS
        platform::initialize_storage();
#endif
        App application(instance);
        app = &application;
        application.smoke = std::wstring_view(command).find(L"--smoke-test") != std::wstring_view::npos;
        application.create();
#ifdef BL_UI_TESTS
        application.verify_bridge_ui();
        DestroyWindow(application.main);
#endif
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (application.model_window && IsWindowVisible(application.model_window) &&
                IsDialogMessageW(application.model_window, &msg)) continue;
            if (application.block_window && IsWindowVisible(application.block_window) &&
                IsDialogMessageW(application.block_window, &msg)) continue;
            if (application.ocr_window && IsWindowVisible(application.ocr_window) &&
                IsDialogMessageW(application.ocr_window, &msg))
                continue;
            if (application.provider_advanced && IsWindowVisible(application.provider_advanced) &&
                IsDialogMessageW(application.provider_advanced, &msg))
                continue;
            if (application.settings && IsWindowVisible(application.settings) &&
                IsDialogMessageW(application.settings, &msg))
                continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        app = nullptr;
    } catch (const std::exception &e) {
#ifdef BL_UI_TESTS
        std::ofstream log(platform::executable_dir() / "ui-tests-error.log");
        log << e.what();
        std::ofstream locale_log(platform::executable_dir() / ("ui-tests-error-" + ui_language() + ".log"));
        locale_log << e.what();
#else
        auto message = wide(notification_text(e.what()));
        MessageBoxW(nullptr, message.c_str(), wide(ui_text("BianLingo failed to start")).c_str(), MB_OK | MB_ICONERROR);
#endif
        code = 1;
    }
    CoUninitialize();
    CloseHandle(mutex);
    return code;
}
