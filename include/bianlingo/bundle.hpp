#pragma once
#include "bianlingo/core.hpp"

namespace bl {
std::string_view bundled_text(std::string_view name);
Json bundled_json(std::string_view name);
std::string bundled_notices();
} // namespace bl
