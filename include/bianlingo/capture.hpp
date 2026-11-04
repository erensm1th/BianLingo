#pragma once
#include "bianlingo/core.hpp"

namespace bl {
struct CaptureStep {
    std::optional<Packet> packet;
    uintptr_t window_id = 0;
    bool connected = false;
    bool idle = false;
    bool color_warning = false;
    uint8_t chat_state = 0;
    std::chrono::milliseconds interval{1000};
};

// Windows coordinates stay relative to the client area so moving the game does
// not require rediscovering the bridge. Other adapters retain monitor discovery.
class BridgeCapture {
    std::optional<Location> location_;
    std::optional<Marker> marker_;
    std::optional<Rect> last_box_;
    std::optional<platform::Capture::GameWindow> window_;
    Clock::time_point next_scan_{}, next_full_{}, last_good_{}, compact_bad_{};

  public:
    CaptureStep poll(platform::Capture &, Clock::time_point);
    void reset();
};
} // namespace bl
