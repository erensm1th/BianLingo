#pragma once
#include "bianlingo/core.hpp"
#include <string_view>

namespace bl {
// Uses the optional, bundled Bergamot helper and Firefox translation models.
std::string bergamot_translate(std::string_view text, bool incoming, std::string_view target,
                               std::string_view chat_language = {}, StopToken cancel = {});
} // namespace bl
