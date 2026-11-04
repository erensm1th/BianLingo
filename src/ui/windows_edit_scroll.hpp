#pragma once
#include "windows_theme_paint.hpp"
#include "windows_widgets.hpp"
#include <commctrl.h>
#include <windowsx.h>

namespace bl::winui {
// Decorates a native multiline EDIT without replacing its text input/IME logic.
// Its child scrollbar is a real buffered control, never a nonclient paint overlay.
class EditScrollbar {
  public:
    EditScrollbar(HINSTANCE instance, HWND edit) : edit_(edit) {
        bar_ = create(
            instance, edit, L"Text scrolling", 0, 0, 1, 1, 0, 0, Kind::Label, [this](HWND, HDC dc) { draw(dc); },
            [this](HWND, UINT msg, WPARAM w, LPARAM l) { return input(msg, w, l); });
        ShowWindow(bar_, SW_HIDE);
        SetWindowLongPtrW(edit, GWL_STYLE, GetWindowLongPtrW(edit, GWL_STYLE) | WS_CLIPCHILDREN);
        if (!SetWindowSubclass(edit, host_proc, 14, reinterpret_cast<DWORD_PTR>(this))) {
            DestroyWindow(bar_);
            throw std::runtime_error("Failed to connect text scrollbar");
        }
    }
    ~EditScrollbar() {
        if (IsWindow(edit_))
            RemoveWindowSubclass(edit_, host_proc, 14);
        if (IsWindow(bar_))
            DestroyWindow(bar_);
    }
    EditScrollbar(const EditScrollbar &) = delete;
    HWND edit() const {
        return edit_;
    }
    HWND window() const {
        return bar_;
    }
    void configure(bool enabled, COLORREF track, COLORREF thumb, COLORREF border) {
        const bool changed = enabled_ != enabled;
        enabled_ = enabled;
        track_ = track;
        thumb_ = thumb;
        border_ = border;
        if (!IsWindow(edit_))
            return;
        if (changed) {
            ShowScrollBar(edit_, SB_VERT, !enabled);
            if (!enabled)
                SendMessageW(edit_, EM_SETRECTNP, 0, 0);
        }
        sync();
        InvalidateRect(bar_, nullptr, FALSE);
    }

  private:
    HWND edit_{}, bar_{};
    HFONT font_{};
    bool enabled_ = false, syncing_ = false, dragging_ = false;
    int total_ = 1, page_ = 1, first_ = 0, line_height_ = 1, grab_ = 0, wheel_remainder_ = 0;
    COLORREF track_{}, thumb_{}, border_{};
    int px(int n) const {
        return std::max(1, MulDiv(n, GetDpiForWindow(edit_), 96));
    }
    RECT thumb() const {
        RECT r{};
        GetClientRect(bar_, &r);
        int height = std::max(1L, r.bottom);
        int size = std::clamp(int(int64_t(height) * page_ / std::max(page_, total_)),
                              std::min(height, px(18)), height);
        int y = total_ > page_ ? int(int64_t(height - size) * first_ / (total_ - page_)) : 0;
        return {0, std::clamp(y, 0, height - size), r.right, std::clamp(y, 0, height - size) + size};
    }
    void sync() {
        if (syncing_ || !IsWindow(edit_) || !IsWindow(bar_))
            return;
        syncing_ = true;
        if (!enabled_) {
            ShowWindow(bar_, SW_HIDE);
            syncing_ = false;
            return;
        }
        RECT client{}, format{};
        GetClientRect(edit_, &client);
        SendMessageW(edit_, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
        RECT desired{px(3), px(2), std::max(px(4), int(client.right) - px(14)),
                     std::max(px(3), int(client.bottom) - px(2))};
        if (!EqualRect(&format, &desired))
            SendMessageW(edit_, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&desired));
        auto font = reinterpret_cast<HFONT>(SendMessageW(edit_, WM_GETFONT, 0, 0));
        if (font != font_) {
            font_ = font;
            auto dc = GetDC(edit_);
            auto old = SelectObject(dc, font);
            TEXTMETRICW metrics{};
            GetTextMetricsW(dc, &metrics);
            line_height_ = std::max(1L, metrics.tmHeight);
            SelectObject(dc, old);
            ReleaseDC(edit_, dc);
        }
        int total = std::max(1, int(SendMessageW(edit_, EM_GETLINECOUNT, 0, 0)));
        int page = std::max(1L, (desired.bottom - desired.top) / line_height_);
        int first = std::max(0, int(SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0)));
        bool changed = total != total_ || page != page_ || first != first_;
        total_ = total;
        page_ = page;
        first_ = first;
        RECT bounds{client.right - px(11), px(3), client.right - px(3), client.bottom - px(3)}, old{};
        GetWindowRect(bar_, &old);
        MapWindowPoints(nullptr, edit_, reinterpret_cast<POINT *>(&old), 2);
        if (!EqualRect(&bounds, &old))
            MoveWindow(bar_, bounds.left, bounds.top, std::max(1L, bounds.right - bounds.left),
                       std::max(1L, bounds.bottom - bounds.top), FALSE);
        bool visible = total > page;
        if (bool(GetWindowLongPtrW(bar_, GWL_STYLE) & WS_VISIBLE) != visible)
            ShowWindow(bar_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (changed)
            InvalidateRect(bar_, nullptr, FALSE);
        syncing_ = false;
    }
    void scroll_to(int line) {
        line = std::clamp(line, 0, std::max(0, total_ - page_));
        SendMessageW(edit_, EM_LINESCROLL, 0, line - first_);
        sync();
    }
    void wheel(WPARAM w) {
        sync();
        wheel_remainder_ += GET_WHEEL_DELTA_WPARAM(w);
        int steps = wheel_remainder_ / WHEEL_DELTA;
        wheel_remainder_ %= WHEEL_DELTA;
        if (!steps)
            return;
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        int amount = lines == WHEEL_PAGESCROLL ? page_ : int(std::min(lines, UINT(total_)));
        auto target =
            std::clamp<int64_t>(int64_t(first_) - int64_t(steps) * amount, 0, std::max(0, total_ - page_));
        scroll_to(int(target));
    }
    void draw(HDC dc) {
        RECT r{};
        GetClientRect(bar_, &r);
        paint::solid(dc, r, track_);
        auto handle = thumb();
        paint::solid(dc, handle, thumb_);
        paint::edge(dc, handle, paint::blend(thumb_, RGB(255, 255, 230), 60),
                    paint::blend(border_, RGB(0, 0, 0), 100));
    }
    std::optional<LRESULT> input(UINT msg, WPARAM w, LPARAM l) {
        int y = GET_Y_LPARAM(l);
        if (msg == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        if (msg == WM_LBUTTONDOWN) {
            sync();
            auto handle = thumb();
            if (y >= handle.top && y < handle.bottom) {
                dragging_ = true;
                grab_ = y - handle.top;
                SetCapture(bar_);
            } else
                scroll_to(first_ + (y < handle.top ? -page_ : page_));
            return 0;
        }
        if (msg == WM_MOUSEMOVE && dragging_) {
            RECT r{};
            GetClientRect(bar_, &r);
            auto handle = thumb();
            int room = r.bottom - (handle.bottom - handle.top);
            if (room > 0)
                scroll_to(int(int64_t(std::clamp(y - grab_, 0, room)) * (total_ - page_) / room));
            return 0;
        }
        if (msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED) {
            dragging_ = false;
            if (GetCapture() == bar_)
                ReleaseCapture();
            return 0;
        }
        if (msg == WM_MOUSEWHEEL) {
            wheel(w);
            return 0;
        }
        return {};
    }
    static LRESULT CALLBACK host_proc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<EditScrollbar *>(data);
        if (self->enabled_ && msg == WM_MOUSEWHEEL) {
            self->wheel(w);
            return 0;
        }
        auto result = DefSubclassProc(h, msg, w, l);
        if (msg == WM_NCDESTROY) {
            self->edit_ = nullptr;
            self->bar_ = nullptr;
            return result;
        }
        if (self->enabled_ && (msg == WM_SIZE || msg == WM_SETFONT || msg == WM_PAINT || msg == WM_SETTEXT ||
                               msg == EM_REPLACESEL || msg == EM_LINESCROLL || msg == WM_VSCROLL ||
                               msg == WM_MOUSEWHEEL || msg == WM_KEYDOWN || msg == WM_KEYUP ||
                               msg == WM_CHAR || msg == WM_MOUSEMOVE || msg == WM_TIMER))
            self->sync();
        return result;
    }
};
} // namespace bl::winui
