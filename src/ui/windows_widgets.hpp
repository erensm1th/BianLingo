#pragma once
#include "bianlingo/theme.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <windows.h>

namespace bl::winui {
enum class Kind { Toggle, Slider, Choice, Hotkey, Label };
using Painter = std::function<void(HWND, HDC)>;
using Input = std::function<std::optional<LRESULT>(HWND, UINT, WPARAM, LPARAM)>;
using Error = std::function<void(const char *)>;
using Geometry = std::function<SliderGeometry(HWND)>;

// A retained, opaque client buffer. Allocation happens only on creation/resize.
// Rendering never exposes an erased background or an intermediate drawing step.
class Buffer {
  public:
    Buffer();
    ~Buffer();
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    void draw(HWND window, HDC target, const Painter &paint);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// These are independent window classes, not painted-over BUTTON/TRACKBAR/COMBOBOX.
// Standard state messages are supported for the existing settings bindings.
HWND create(HINSTANCE, HWND parent, const wchar_t *caption, int x, int y, int w, int h, DWORD style, int id,
            Kind, Painter, Input = {}, Error = {}, Geometry = {});
std::optional<Kind> kind(HWND);
bool hovered(HWND);

struct Diagnostics {
    unsigned long long paints{}, allocations{}, notifications{}, live_widgets{}, buffer_bytes{},
        peak_buffer_bytes{};
};
// Test instrumentation is compiled out of the distributed executable.
Diagnostics diagnostics();
} // namespace bl::winui
