#pragma once
#include "bianlingo/core.hpp"

namespace bl {
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 10> output_languages = {
    {{"English", "English"},
     {"Korean", "Korean"},
     {"Russian", "Russian"},
     {"German", "German"},
     {"French", "French"},
     {"Spanish", "Spanish"},
     {"Brazilian Portuguese", "Brazilian Portuguese"},
     {"Japanese", "Japanese"},
     {"Italian", "Italian"},
     {"Chinese", "Chinese"}}};

// Clipboard confirmation is a separate step: a translation result alone must never
// tell the user it was copied. UI status/capture notices do not enter this state.
class QuickReply {
  public:
    enum Phase { Hidden, Translating, Clipboard, Complete, Copied, Error } phase = Hidden;
    std::string result, error;
    Clock::time_point deadline{};
    bool busy() const {
        return phase == Translating || phase == Clipboard;
    }
    bool begin() {
        if (busy())
            return false;
        phase = Translating;
        result.clear();
        error.clear();
        deadline = {};
        return true;
    }
    bool receive(std::string value) {
        if (phase != Translating)
            return false;
        result = std::move(value);
        phase = Clipboard;
        return true;
    }
    void copied(Clock::time_point now) {
        if (phase != Clipboard)
            return;
        phase = Complete;
        deadline = now + std::chrono::milliseconds(650);
    }
    void fail(std::string value, Clock::time_point now) {
        error = std::move(value);
        phase = Error;
        deadline = now + std::chrono::milliseconds(2500);
    }
    bool tick(Clock::time_point now) {
        if (deadline == Clock::time_point{} || now < deadline)
            return false;
        if (phase == Complete) {
            phase = Copied;
            deadline = now + std::chrono::milliseconds(1500);
        } else {
            phase = Hidden;
            deadline = {};
        }
        return true;
    }
    std::string caption() const {
        switch (phase) {
        case Translating:
        case Clipboard:
            return "Translating…";
        case Complete:
            return "Translated";
        case Copied:
            return "Copied to clipboard";
        case Error:
            return error;
        default:
            return {};
        }
    }
};
} // namespace bl
