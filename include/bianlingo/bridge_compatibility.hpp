#pragma once
#include "bianlingo/core.hpp"

namespace bl {
enum class BridgeCompatibilityKind { Waiting, Legacy, Compatible, UpdateDesktop, UpdateAddon, Invalid };
struct BridgeCompatibilityReport {
    BridgeCompatibilityKind kind = BridgeCompatibilityKind::Waiting;
    std::string addon_version, minimum_desktop, text;
    bool blocking() const {
        return kind == BridgeCompatibilityKind::UpdateDesktop || kind == BridgeCompatibilityKind::UpdateAddon ||
               kind == BridgeCompatibilityKind::Invalid;
    }
};
BridgeCompatibilityReport assess_bridge_hello(std::string_view, uint32_t receipt_nonce = 0);
BridgeCompatibilityReport waiting_bridge_report();
BridgeCompatibilityReport legacy_bridge_report();
std::string desktop_compatibility_receipt(uint32_t addon_session, uint32_t receipt_nonce);
} // namespace bl
