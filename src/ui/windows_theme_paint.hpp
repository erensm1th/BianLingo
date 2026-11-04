#pragma once
#include <algorithm>
#include <windows.h>

// Small, deterministic GDI primitives for the beveled theme. No textures,
// timers or retained resources: the existing widget buffer owns each frame.
namespace bl::winui::paint {
inline COLORREF blend(COLORREF a, COLORREF b, int amount) {
    auto channel = [&](int x, int y) { return (x * (255 - amount) + y * amount) / 255; };
    return RGB(channel(GetRValue(a), GetRValue(b)), channel(GetGValue(a), GetGValue(b)),
               channel(GetBValue(a), GetBValue(b)));
}
inline void solid(HDC dc, RECT box, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &box, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
inline void gradient(HDC dc, RECT box, COLORREF top, COLORREF bottom) {
    const int height = std::max(1L, box.bottom - box.top - 1);
    for (int y = box.top; y < box.bottom; ++y)
        solid(dc, {box.left, y, box.right, y + 1}, blend(top, bottom, (y - box.top) * 255 / height));
}
inline void edge(HDC dc, RECT box, COLORREF light, COLORREF dark, int width = 1) {
    width = std::max(1, std::min({width, int((box.right - box.left) / 2), int((box.bottom - box.top) / 2)}));
    solid(dc, {box.left, box.top, box.right, box.top + width}, light);
    solid(dc, {box.left, box.top, box.left + width, box.bottom}, light);
    solid(dc, {box.left, box.bottom - width, box.right, box.bottom}, dark);
    solid(dc, {box.right - width, box.top, box.right, box.bottom}, dark);
}
inline void frame(HDC dc, RECT box, COLORREF fill, COLORREF rim, int unit = 1, bool inset = false,
                  bool lit = false, bool shaded = true) {
    if (box.right <= box.left || box.bottom <= box.top)
        return;
    unit = std::max(1, unit);
    solid(dc, box, RGB(3, 4, 5));
    InflateRect(&box, -unit, -unit);
    auto hi = blend(rim, RGB(255, 246, 210), lit ? 125 : 60);
    auto lo = blend(rim, RGB(0, 0, 0), 155);
    edge(dc, box, inset ? lo : hi, inset ? hi : lo, unit);
    InflateRect(&box, -unit, -unit);
    edge(dc, box, RGB(8, 9, 9), blend(rim, RGB(0, 0, 0), 165), unit);
    InflateRect(&box, -unit, -unit);
    if (shaded)
        gradient(dc, box, blend(fill, RGB(255, 255, 255), inset ? 0 : 18),
                 blend(fill, RGB(0, 0, 0), inset ? 0 : 85));
    else
        solid(dc, box, fill);
}
inline void diamond(HDC dc, RECT box, COLORREF fill, COLORREF rim) {
    auto old_pen = SelectObject(dc, GetStockObject(DC_PEN));
    auto old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    int x = (box.left + box.right) / 2, y = (box.top + box.bottom) / 2;
    POINT points[] = {{x, box.top}, {box.right - 1, y}, {x, box.bottom - 1}, {box.left, y}};
    SetDCBrushColor(dc, fill);
    SetDCPenColor(dc, blend(rim, RGB(0, 0, 0), 150));
    Polygon(dc, points, 4);
    SetDCPenColor(dc, blend(fill, RGB(255, 255, 235), 100));
    Polyline(dc, points + 2, 2);
    POINT cap[] = {{box.left, y}, {x, box.top}, {box.right - 1, y}};
    Polyline(dc, cap, 3);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
}
inline void tick(HDC dc, RECT box, COLORREF color, int unit) {
    // Two filled quads scale together; no font-dependent check glyph.
    auto old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    auto old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, color);
    const int w = box.right - box.left, h = box.bottom - box.top;
    POINT points[] = {{box.left + w / 5, box.top + h / 2},
                      {box.left + 2 * w / 5, box.top + 3 * h / 4},
                      {box.right - w / 8, box.top + h / 6},
                      {box.right - w / 8, box.top + h / 6 + unit * 2},
                      {box.left + 2 * w / 5, box.top + 3 * h / 4 + unit * 2},
                      {box.left + w / 5, box.top + h / 2 + unit * 2}};
    Polygon(dc, points, 6);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
}
} // namespace bl::winui::paint
