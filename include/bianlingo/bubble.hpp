#pragma once
#include "bianlingo/core.hpp"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace bl {
struct BubbleAppearance {
    int transparency = 50, scale = 100;
    double opacity() const {
        return (100 - transparency) / 100.0;
    }
};
inline BubbleAppearance bubble_appearance(const Json &settings) {
    BubbleAppearance out;
    for (auto [key, target, fallback, low, high] :
         {std::tuple{"bubble_transparency", &out.transparency, 50, 0, 100},
          std::tuple{"bubble_scale", &out.scale, 100, 50, 200}}) {
        const auto value = settings.value(key, Json(fallback));
        if (!value.is_number_integer() || value.get<int64_t>() < low || value.get<int64_t>() > high)
            throw std::runtime_error("Bubble settings are out of range");
        *target = value.get<int>();
    }
    return out;
}
inline bool normalize_bubble_settings(Json &settings) {
    bool changed = false;
    for (auto [key, fallback, low, high] :
         {std::tuple{"bubble_transparency", 50, 0, 100}, std::tuple{"bubble_scale", 100, 50, 200}}) {
        if (!settings.contains(key) || !settings[key].is_number_integer() ||
            settings[key].get<int64_t>() < low || settings[key].get<int64_t>() > high) {
            settings[key] = fallback;
            changed = true;
        }
    }
    return changed;
}
// Top-left coordinates; center at 40% of the chosen monitor's work area, clamped on-screen.
inline Rect bubble_bounds(Rect work, int width, int height, int margin) {
    work.w = std::max(1, work.w);
    work.h = std::max(1, work.h);
    margin = std::clamp(margin, 0, std::max(0, (std::min(work.w, work.h) - 1) / 2));
    width = std::clamp(width, 1, std::max(1, work.w - 2 * margin));
    height = std::clamp(height, 1, std::max(1, work.h - 2 * margin));
    int x = work.x + (work.w - width) / 2;
    int y = int(std::lround(work.y + work.h * 0.4 - height / 2.0));
    return {std::clamp(x, work.x + margin, work.x + work.w - margin - width),
            std::clamp(y, work.y + margin, work.y + work.h - margin - height), width, height};
}
} // namespace bl
