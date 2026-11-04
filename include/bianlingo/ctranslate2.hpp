#pragma once
#include "bianlingo/core.hpp"
#include <string_view>

namespace bl {
std::string ctranslate2_translate(std::string_view text, bool incoming, std::string_view target,
                                  std::string_view chat_language = {}, StopToken cancel = {});
} // namespace bl
