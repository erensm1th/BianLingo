#pragma once
#include "bianlingo/core.hpp"

namespace bl::download {
inline constexpr uint64_t max_download = 512 * 1024 * 1024;
using Progress = std::function<void(uint64_t)>;
void download(std::string_view url, const std::filesystem::path &destination, uint64_t limit,
              const Progress &progress);
} // namespace bl::download
