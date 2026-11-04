#include "windows_widgets.hpp"
#include <algorithm>
#include <commctrl.h>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <vector>
#include <windowsx.h>

namespace bl::winui {
namespace {
constexpr wchar_t class_name[] = L"BianLingo.Widget";
constexpr wchar_t property_name[] = L"BianLingo.WidgetState";
#ifdef BL_UI_TESTS
Diagnostics counters;
#define BL_COUNT(name) (++counters.name)
#define BL_UNCOUNT(name) (--counters.name)
#else
#define BL_COUNT(name) ((void)0)
#define BL_UNCOUNT(name) ((void)0)
#endif
void invalidate(HWND h) {
    InvalidateRect(h, nullptr, FALSE);
}
struct Widget {
    Kind type;
    Painter paint;
    Input input;
    Error error;
    Geometry geometry;
    Buffer buffer;
    HFONT font{};
    bool checked{}, pressed{}, key_pressed{}, dragging{}, hot{};
    int minimum{}, maximum{100}, position{}, page{10}, selection{-1};
    WORD hotkey{};
    std::vector<std::wstring> items;
    std::wstring search;
    ULONGLONG searched_at{};
    explicit Widget(Kind type, Painter paint, Input input, Error error, Geometry geometry)
        : type(type), paint(std::move(paint)), input(std::move(input)), error(std::move(error)),
          geometry(std::move(geometry)) {
        BL_COUNT(live_widgets);
    }
    ~Widget() {
        BL_UNCOUNT(live_widgets);
    }
    void notify(HWND h, int code) {
        BL_COUNT(notifications);
        SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), code),
                     reinterpret_cast<LPARAM>(h));
    }
    void toggle(HWND h) {
        if (!IsWindowEnabled(h))
            return;
        checked = !checked;
        invalidate(h);
        notify(h, BN_CLICKED);
    }
    void set_position(HWND h, int value, bool redraw) {
        value = std::clamp(value, minimum, maximum);
        if (position == value)
            return;
        position = value;
        if (redraw)
            invalidate(h);
    }
    void slide(HWND h, int value, int code) {
        int before = position;
        set_position(h, value, true);
        if (before == position && code == TB_THUMBTRACK)
            return;
        BL_COUNT(notifications);
        SendMessageW(GetParent(h), WM_HSCROLL, MAKEWPARAM(code, position), reinterpret_cast<LPARAM>(h));
    }
    void seek(HWND h, int x, int code) {
        RECT r{};
        GetClientRect(h, &r);
        auto g = geometry ? geometry(h)
                          : slider_geometry(ThemeMetrics{}, r.right, r.bottom, int(GetDpiForWindow(h)));
        slide(h, g.value(x, minimum, maximum), code);
    }
    LRESULT handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        if (type != Kind::Label && (msg == WM_MOUSEMOVE || msg == WM_MOUSELEAVE || msg == WM_ENABLE)) {
            RECT r{};
            GetClientRect(h, &r);
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            bool next = msg == WM_MOUSEMOVE && IsWindowEnabled(h) && PtInRect(&r, p);
            if (msg != WM_ENABLE || !w) {
                if (next != hot) {
                    hot = next;
                    invalidate(h);
                    if (hot) {
                        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, h, 0};
                        TrackMouseEvent(&tracking);
                    }
                }
            }
        }
        if (input)
            if (auto result = input(h, msg, w, l))
                return *result;
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_NCPAINT:
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT p{};
            auto dc = BeginPaint(h, &p);
            try {
                buffer.draw(h, dc, paint);
            } catch (...) {
                EndPaint(h, &p);
                throw;
            }
            EndPaint(h, &p);
            return 0;
        }
        case WM_PRINTCLIENT:
            buffer.draw(h, reinterpret_cast<HDC>(w), paint);
            return 0;
        case WM_SETFONT:
            if (font != reinterpret_cast<HFONT>(w)) {
                font = reinterpret_cast<HFONT>(w);
                if (l)
                    invalidate(h);
            }
            return 0;
        case WM_GETFONT:
            return reinterpret_cast<LRESULT>(font);
        case WM_SETTEXT: {
            const wchar_t *next = l ? reinterpret_cast<const wchar_t *>(l) : L"";
            int length = GetWindowTextLengthW(h);
            std::wstring previous(size_t(length) + 1, L'\0');
            GetWindowTextW(h, previous.data(), length + 1);
            previous.resize(length);
            if (previous == next)
                return TRUE;
            auto result = DefWindowProcW(h, msg, w, l);
            invalidate(h);
            return result;
        }
        case WM_SIZE:
        case WM_ENABLE:
        case WM_DPICHANGED_AFTERPARENT:
            invalidate(h);
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            key_pressed = false;
            pressed = false;
            invalidate(h);
            return 0;
        case WM_GETDLGCODE:
            if (type == Kind::Label)
                return DLGC_STATIC;
            if (type == Kind::Toggle)
                return DLGC_BUTTON;
            if (type == Kind::Hotkey) {
                auto event = reinterpret_cast<MSG *>(l);
                return event && event->message == WM_KEYDOWN && event->wParam == VK_TAB ? 0
                                                                                        : DLGC_WANTALLKEYS;
            }
            if (type == Kind::Choice) {
                auto event = reinterpret_cast<MSG *>(l);
                if (event && event->message == WM_KEYDOWN && event->wParam != VK_TAB)
                    return DLGC_WANTALLKEYS | DLGC_WANTARROWS;
            }
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        if (type == Kind::Toggle) {
            switch (msg) {
            case BM_GETCHECK:
                return checked ? BST_CHECKED : BST_UNCHECKED;
            case BM_SETCHECK:
                if (checked != (w == BST_CHECKED)) {
                    checked = w == BST_CHECKED;
                    invalidate(h);
                }
                return 0;
            case BM_GETSTATE:
                return (checked ? BST_CHECKED : 0) | (pressed ? BST_PUSHED : 0) |
                       (GetFocus() == h ? BST_FOCUS : 0);
            case BM_SETSTATE:
                if (pressed != bool(w)) {
                    pressed = bool(w);
                    invalidate(h);
                }
                return 0;
            case BM_CLICK:
                toggle(h);
                return 0;
            case WM_LBUTTONDOWN:
                if (IsWindowEnabled(h)) {
                    SetFocus(h);
                    pressed = true;
                    SetCapture(h);
                    invalidate(h);
                }
                return 0;
            case WM_LBUTTONUP: {
                bool activate = pressed;
                pressed = false;
                RECT r{};
                GetClientRect(h, &r);
                POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                if (GetCapture() == h)
                    ReleaseCapture();
                invalidate(h);
                if (activate && PtInRect(&r, p))
                    toggle(h);
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (pressed) {
                    pressed = false;
                    invalidate(h);
                }
                return 0;
            case WM_KEYDOWN:
                if (w == VK_SPACE && !key_pressed) {
                    key_pressed = true;
                    pressed = true;
                    invalidate(h);
                }
                return 0;
            case WM_KEYUP:
                if (w == VK_SPACE && key_pressed) {
                    key_pressed = pressed = false;
                    toggle(h);
                }
                return 0;
            case WM_CHAR:
                return 0;
            }
        }
        if (type == Kind::Slider) {
            switch (msg) {
            case TBM_GETPOS:
                return position;
            case TBM_GETRANGEMIN:
                return minimum;
            case TBM_GETRANGEMAX:
                return maximum;
            case TBM_SETRANGEMIN:
                minimum = int(l);
                maximum = std::max(minimum, maximum);
                set_position(h, position, false);
                if (w)
                    invalidate(h);
                return 0;
            case TBM_SETRANGEMAX:
                maximum = int(l);
                minimum = std::min(minimum, maximum);
                set_position(h, position, false);
                if (w)
                    invalidate(h);
                return 0;
            case TBM_SETPOS:
                set_position(h, int(l), w != 0);
                return 0;
            case TBM_SETPAGESIZE: {
                int old = page;
                page = std::max(1, int(l));
                return old;
            }
            case WM_LBUTTONDOWN:
                if (IsWindowEnabled(h)) {
                    SetFocus(h);
                    dragging = true;
                    SetCapture(h);
                    invalidate(h);
                    seek(h, GET_X_LPARAM(l), TB_THUMBTRACK);
                }
                return 0;
            case WM_MOUSEMOVE:
                if (dragging && GetCapture() == h)
                    seek(h, GET_X_LPARAM(l), TB_THUMBTRACK);
                return 0;
            case WM_LBUTTONUP:
                if (dragging) {
                    seek(h, GET_X_LPARAM(l), TB_THUMBPOSITION);
                    dragging = false;
                    if (GetCapture() == h)
                        ReleaseCapture();
                    invalidate(h);
                    slide(h, position, TB_ENDTRACK);
                }
                return 0;
            case WM_CAPTURECHANGED:
                if (dragging)
                    invalidate(h);
                dragging = false;
                return 0;
            case WM_KEYDOWN: {
                if (!IsWindowEnabled(h))
                    return 0;
                switch (w) {
                case VK_LEFT:
                case VK_UP:
                    slide(h, position - 1, TB_LINEUP);
                    break;
                case VK_RIGHT:
                case VK_DOWN:
                    slide(h, position + 1, TB_LINEDOWN);
                    break;
                case VK_HOME:
                    slide(h, minimum, TB_TOP);
                    break;
                case VK_END:
                    slide(h, maximum, TB_BOTTOM);
                    break;
                case VK_PRIOR:
                    slide(h, position - page, TB_PAGEUP);
                    break;
                case VK_NEXT:
                    slide(h, position + page, TB_PAGEDOWN);
                    break;
                }
                return 0;
            }
            }
        }
        if (type == Kind::Choice) {
            switch (msg) {
            case CB_GETCOUNT:
                return LRESULT(items.size());
            case CB_ADDSTRING:
                if (!l)
                    return CB_ERR;
                items.emplace_back(reinterpret_cast<const wchar_t *>(l));
                invalidate(h);
                return LRESULT(items.size() - 1);
            case CB_RESETCONTENT:
                items.clear();
                selection = -1;
                search.clear();
                invalidate(h);
                return 0;
            case CB_GETCURSEL:
                return selection;
            case CB_SETCURSEL: {
                int next = int(w);
                if (next < 0 || next >= int(items.size()))
                    next = -1;
                if (selection != next) {
                    selection = next;
                    invalidate(h);
                }
                return selection;
            }
            case CB_GETLBTEXTLEN:
                return size_t(w) < items.size() ? LRESULT(items[size_t(w)].size()) : CB_ERR;
            case CB_GETLBTEXT:
                if (size_t(w) >= items.size() || !l)
                    return CB_ERR;
                std::copy(items[size_t(w)].c_str(), items[size_t(w)].c_str() + items[size_t(w)].size() + 1,
                          reinterpret_cast<wchar_t *>(l));
                return LRESULT(items[size_t(w)].size());
            case WM_CHAR:
                if (w >= 32 && !items.empty()) {
                    ULONGLONG now = GetTickCount64();
                    if (now - searched_at > 1000)
                        search.clear();
                    searched_at = now;
                    search += wchar_t(std::towlower(wchar_t(w)));
                    for (size_t i = 0; i < items.size(); ++i) {
                        auto value = items[i];
                        std::transform(value.begin(), value.end(), value.begin(),
                                       [](wchar_t c) { return wchar_t(std::towlower(c)); });
                        if (value.starts_with(search) && selection != int(i)) {
                            selection = int(i);
                            invalidate(h);
                            notify(h, CBN_SELCHANGE);
                            break;
                        }
                    }
                }
                return 0;
            }
        }
        if (type == Kind::Hotkey) {
            switch (msg) {
            case WM_LBUTTONDOWN:
                if (IsWindowEnabled(h)) SetFocus(h);
                invalidate(h);
                return 0;
            case HKM_GETHOTKEY:
                return hotkey;
            case HKM_SETHOTKEY:
                if (hotkey != WORD(w)) {
                    hotkey = WORD(w);
                    invalidate(h);
                }
                return 0;
            case HKM_SETRULES:
                return 0; // The app permits all modifier combinations.
            case WM_SYSKEYDOWN:
            case WM_KEYDOWN:
                if (w == VK_TAB)
                    return DefWindowProcW(h, msg, w, l);
                if (w != VK_SHIFT && w != VK_CONTROL && w != VK_MENU && w != VK_LWIN && w != VK_RWIN) {
                    int modifiers = ((GetKeyState(VK_SHIFT) & 0x8000) ? HOTKEYF_SHIFT : 0) |
                                    ((GetKeyState(VK_CONTROL) & 0x8000) ? HOTKEYF_CONTROL : 0) |
                                    ((GetKeyState(VK_MENU) & 0x8000) ? HOTKEYF_ALT : 0) |
                                    ((l & (1 << 24)) ? HOTKEYF_EXT : 0);
                    WORD next = w == VK_BACK ? 0 : MAKEWORD(w, modifiers);
                    if (next != hotkey) {
                        hotkey = next;
                        invalidate(h);
                        notify(h, EN_CHANGE);
                    }
                }
                return 0;
            case WM_CHAR:
            case WM_SYSCHAR:
            case WM_KEYUP:
            case WM_SYSKEYUP:
                return 0;
            }
        }
        return DefWindowProcW(h, msg, w, l);
    }
};
LRESULT CALLBACK procedure(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto widget = reinterpret_cast<Widget *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto data = reinterpret_cast<CREATESTRUCTW *>(l);
        auto creator = static_cast<std::unique_ptr<Widget> *>(data->lpCreateParams);
        widget = creator->get();
        if (!SetPropW(h, property_name, widget))
            return FALSE;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(widget));
        creator->release();
        return DefWindowProcW(h, msg, w, l);
    }
    if (msg == WM_NCDESTROY) {
        if (widget && widget->input)
            try {
                widget->input(h, msg, w, l);
            } catch (...) {
            }
        RemovePropW(h, property_name);
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        delete widget;
        return DefWindowProcW(h, msg, w, l);
    }
    if (!widget)
        return DefWindowProcW(h, msg, w, l);
    try {
        return widget->handle(h, msg, w, l);
    } catch (const std::exception &e) {
        if (widget->error)
            try {
                widget->error(e.what());
            } catch (...) {
            }
        return 0;
    } catch (...) {
        if (widget->error)
            try {
                widget->error("Unknown widget failure");
            } catch (...) {
            }
        return 0;
    }
}
} // namespace
struct Buffer::Impl {
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ original{};
    int width{}, height{};
    ~Impl() {
        if (dc) {
            SelectObject(dc, original);
            if (bitmap)
                DeleteObject(bitmap);
            DeleteDC(dc);
#ifdef BL_UI_TESTS
            counters.buffer_bytes -= static_cast<unsigned long long>(width) * height * 4;
#endif
        }
    }
};
Buffer::Buffer() : impl(std::make_unique<Impl>()) {}
Buffer::~Buffer() = default;
void Buffer::draw(HWND h, HDC target, const Painter &paint) {
    RECT r{};
    GetClientRect(h, &r);
    if (r.right <= 0 || r.bottom <= 0 || !target)
        return;
    if (!impl->dc) {
        impl->dc = CreateCompatibleDC(target);
        if (!impl->dc)
            throw std::runtime_error("Cannot allocate UI memory DC");
        impl->original = GetCurrentObject(impl->dc, OBJ_BITMAP);
    }
    if (impl->width != r.right || impl->height != r.bottom) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = r.right;
        info.bmiHeader.biHeight = -r.bottom;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *bits{};
        auto next = CreateDIBSection(target, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!next)
            throw std::runtime_error("Cannot allocate UI pixel buffer");
        SelectObject(impl->dc, next);
        if (impl->bitmap)
            DeleteObject(impl->bitmap);
        impl->bitmap = next;
#ifdef BL_UI_TESTS
        counters.buffer_bytes -= static_cast<unsigned long long>(impl->width) * impl->height * 4;
        counters.buffer_bytes += static_cast<unsigned long long>(r.right) * r.bottom * 4;
        counters.peak_buffer_bytes = std::max(counters.peak_buffer_bytes, counters.buffer_bytes);
#endif
        impl->width = r.right;
        impl->height = r.bottom;
        BL_COUNT(allocations);
    }
    int saved = SaveDC(impl->dc);
    try {
        paint(h, impl->dc);
    } catch (...) {
        RestoreDC(impl->dc, saved);
        throw;
    }
    RestoreDC(impl->dc, saved);
    BitBlt(target, 0, 0, r.right, r.bottom, impl->dc, 0, 0, SRCCOPY);
    BL_COUNT(paints);
}
HWND create(HINSTANCE instance, HWND parent, const wchar_t *caption, int x, int y, int w, int h, DWORD style,
            int id, Kind type, Painter paint, Input input, Error error, Geometry geometry) {
    WNDCLASSEXW wc{sizeof(wc)};
    if (!GetClassInfoExW(instance, class_name, &wc)) {
        wc = {sizeof(wc)};
        wc.lpfnWndProc = procedure;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = class_name;
        if (!RegisterClassExW(&wc))
            throw std::runtime_error("Cannot register UI widget class");
    }
    auto widget = std::make_unique<Widget>(type, std::move(paint), std::move(input), std::move(error),
                                           std::move(geometry));
    // Native combo/slider flags must never create native nonclient scrollbars or frames.
    style &= ~(WS_VSCROLL | WS_HSCROLL | WS_BORDER);
    if (type != Kind::Label)
        style |= WS_TABSTOP;
    auto window = CreateWindowExW(0, class_name, caption, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                                  reinterpret_cast<HMENU>(INT_PTR(id)), instance, &widget);
    // WM_NCDESTROY owns deletion once WM_NCCREATE was accepted.
    if (!window)
        throw std::runtime_error("Cannot create UI widget");
    SetWindowTextW(window, caption);
    return window;
}
std::optional<Kind> kind(HWND h) {
    auto widget = static_cast<Widget *>(GetPropW(h, property_name));
    return widget ? std::optional(widget->type) : std::nullopt;
}
bool hovered(HWND h) {
    auto widget = static_cast<Widget *>(GetPropW(h, property_name));
    return widget && widget->hot;
}
Diagnostics diagnostics() {
#ifdef BL_UI_TESTS
    return counters;
#else
    return {};
#endif
}
} // namespace bl::winui
