#pragma once
#include "bianlingo/core.hpp"

namespace bl {
// Portable 0xRRGGBB colors, independent of platform byte order.
uint32_t chat_color(const Message &);
std::string chat_channel_label(const Message &);
struct ChatSpan {
    std::string text;
    uint32_t color;
};
std::vector<ChatSpan> chat_spans(std::string_view, uint32_t base);
struct ProtectedLinks {
    std::string text;
    std::vector<std::pair<std::string, std::string>> links;
    std::string instructions() const;
    std::string restore(std::string_view) const;
};
ProtectedLinks protect_chat_links(std::string_view);
// Chat background opacity is independent of text opacity.
double background_opacity(const Json &);
} // namespace bl
