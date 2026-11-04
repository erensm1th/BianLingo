#pragma once
#include "bianlingo/core.hpp"

namespace bl {
// Display-only fixtures. Never send these to Engine, the journal, or the transport.
std::deque<DisplayMessage> chat_preview_messages(uint32_t now);
} // namespace bl
