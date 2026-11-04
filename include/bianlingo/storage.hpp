#pragma once
#include "bianlingo/core.hpp"
namespace bl {
std::filesystem::path portable_data(const std::filesystem::path &executable_directory);
// Initialize writable storage next to the executable.
void prepare_portable_data(const std::filesystem::path &executable_directory);
// Commit a verified staged directory; restore the old directory if commit fails.
// The rename seam is for filesystem fault-injection tests.
void commit_directory(const std::filesystem::path &staged, const std::filesystem::path &destination,
                      const std::filesystem::path &backup,
                      const std::function<void(const std::filesystem::path &, const std::filesystem::path &)> &rename = {});
}
