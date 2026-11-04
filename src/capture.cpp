#include "bianlingo/capture.hpp"
#include <algorithm>
#include <cmath>

namespace bl {
void BridgeCapture::reset() {
    *this = BridgeCapture{};
}

CaptureStep BridgeCapture::poll(platform::Capture &screen, Clock::time_point now) {
    CaptureStep step;
    std::optional<Rect> scope;
    if (screen.window_scoped()) {
        auto current = screen.game_window();
        if (!current || current->client.w < (compact_cols + 2) * 2 + 6 ||
            current->client.h < (compact_rows + 2) * 2 + 6) {
            last_good_ = {};
            return step; // No screenshot or monitor enumeration while hidden/minimized/closed.
        }
        step.window_id = current->id;
        if (!window_ || window_->id != current->id || window_->client.w != current->client.w ||
            window_->client.h != current->client.h) {
            reset();
        }
        window_ = current;
        scope = current->client;
    }
    auto absolute = [&](Rect box) {
        if (scope) {
            box.x += scope->x;
            box.y += scope->y;
        }
        return box;
    };
    // Idle costs a single 4x4 read. Active wake reads the complete packet below,
    // in this same poll, rather than waiting for the next timer tick.
    if (marker_) {
        auto box = absolute({marker_->x, marker_->y, 4, 4});
        auto state = decode_marker(screen.grab(box));
        if (state) {
            state->x = marker_->x;
            state->y = marker_->y;
            marker_ = state;
            step.chat_state = state->chat_state;
            location_ = marker_location(*state);
            step.connected = true;
            step.idle = !state->active;
            step.interval = std::chrono::milliseconds(100);
            last_good_ = now;
            if (step.idle) {
                compact_bad_ = {};
                return step;
            }
        } else {
            marker_.reset();
            location_.reset();
            last_good_ = {};
        }
    }
    if (!location_ && now >= next_scan_) {
        next_scan_ = now + std::chrono::seconds(5);
        std::vector<Rect> boxes;
        auto add_box = [&](Rect box) {
            if (std::find(boxes.begin(), boxes.end(), box) == boxes.end())
                boxes.push_back(box);
        };
        if (last_box_)
            add_box(*last_box_);
        auto monitors = scope ? std::vector<Rect>{Rect{0, 0, scope->w, scope->h}} : screen.monitors();
        for (auto r : monitors) {
            int w = std::min(440, r.w), h = std::min(120, r.h);
            for (auto x : {r.x, r.x + r.w - w})
                for (auto y : {r.y, r.y + r.h - h})
                    add_box({x, y, w, h});
        }
        auto search = [&](Rect box) {
            const auto &image = screen.grab(absolute(box));
            if (auto state = locate_marker(image)) {
                state->x += box.x;
                state->y += box.y;
                marker_ = state;
                step.chat_state = state->chat_state;
                location_ = marker_location(*state);
                last_good_ = now;
                return true;
            }
            auto found = locate(image);
            if (!found)
                return false;
            found->x += box.x;
            found->y += box.y;
            location_ = found;
            last_good_ = now;
            return true;
        };
        for (auto box : boxes)
            if (search(box))
                break;
        // Fallback discovery is kept for adapters without window tracking only.
        if (!scope && !location_ && now >= next_full_) {
            next_full_ = now + std::chrono::seconds(30);
            for (auto r : monitors)
                if (search(r))
                    break;
        }
    }
    if (marker_ && !marker_->active) {
        step.chat_state = marker_->chat_state;
        step.connected = step.idle = true;
        step.interval = std::chrono::milliseconds(100);
        return step;
    }
    if (location_) {
        auto p = *location_;
        Rect box{int(std::floor(p.x)), int(std::floor(p.y)),
                 int(std::ceil(p.x + grid_cols(p.compact) * p.cell)) - int(std::floor(p.x)),
                 int(std::ceil(p.y + grid_rows(p.compact) * p.cell)) - int(std::floor(p.y))};
        last_box_ = box;
        step.packet = decode_image(screen.grab(absolute(box)), {p.x - box.x, p.y - box.y, p.cell, p.compact});
        if (step.packet) {
            last_good_ = now;
            compact_bad_ = {};
        } else {
            if (p.compact) {
                if (compact_bad_ == Clock::time_point{})
                    compact_bad_ = now;
                step.color_warning = now - compact_bad_ > std::chrono::seconds(2);
            }
            // Enabling idle hiding on a fallback visible bar can hide it before
            // discovery runs again. Check its corresponding marker immediately.
            for (int corner = 0; corner < 4 && !marker_; ++corner) {
                Marker candidate;
                candidate.cell = int(std::lround(p.cell));
                candidate.compact = p.compact;
                candidate.corner = corner;
                candidate.x =
                    int(std::lround(p.x)) -
                    (corner & 1 ? -1 - (grid_cols(p.compact) + 1) * candidate.cell : 5 + candidate.cell);
                candidate.y =
                    int(std::lround(p.y)) -
                    (corner & 2 ? -1 - (grid_rows(p.compact) + 1) * candidate.cell : 5 + candidate.cell);
                if (scope && (candidate.x < 0 || candidate.y < 0 || candidate.x + 4 > scope->w ||
                              candidate.y + 4 > scope->h))
                    continue;
                if (scope && (candidate.x != (corner & 1 ? scope->w - 5 : 1) ||
                              candidate.y != (corner & 2 ? scope->h - 5 : 1)))
                    continue;
                auto state = decode_marker(screen.grab(absolute({candidate.x, candidate.y, 4, 4})));
                if (state) {
                    state->x = candidate.x;
                    state->y = candidate.y;
                    marker_ = state;
                    step.chat_state = state->chat_state;
                    location_ = marker_location(*state);
                    last_good_ = now;
                    step.idle = !state->active;
                }
            }
            if (!marker_ && now - last_good_ > std::chrono::milliseconds(1500))
                location_.reset();
        }
    }
    step.connected = location_.has_value() && now - last_good_ < std::chrono::seconds(1);
    if (location_)
        step.interval = std::chrono::milliseconds(100);
    return step;
}
} // namespace bl
