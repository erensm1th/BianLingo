#pragma once
#include "bianlingo/core.hpp"

namespace bl {
// Verify and atomically install an embedded package into an application-owned cache.
void unpack_package(std::span<const uint8_t> payload, const Json &metadata,
                    const std::filesystem::path &destination);
namespace platform {
// Packaged executables use UserData/Package; development binaries use their directory.
std::filesystem::path package_root();
}
}
