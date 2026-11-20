#include "bianlingo/bridge_compatibility.hpp"
#include "bianlingo/bundle.hpp"
#include "version.hpp"
#include <array>
#include <charconv>

namespace bl {
namespace {
std::vector<std::string_view> fields(std::string_view text, char delimiter) {
    std::vector<std::string_view> out;
    for (size_t start = 0;;) {
        const auto end = text.find(delimiter, start);
        out.push_back(text.substr(start, end == text.npos ? text.size() - start : end - start));
        if (end == text.npos) return out;
        start = end + 1;
    }
}
unsigned number(std::string_view s, unsigned maximum) {
    unsigned value = 0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (s.empty() || s.size() > 10 || result.ec != std::errc{} || result.ptr != s.data() + s.size() || value > maximum)
        throw std::runtime_error("Invalid compatibility version field");
    return value;
}
std::array<unsigned, 3> version(std::string_view s) {
    if (s.size() > 17) throw std::runtime_error("Invalid compatibility version field");
    const auto parts = fields(s, '.');
    if (parts.size() != 3) throw std::runtime_error("Invalid compatibility version field");
    return {number(parts[0], 65535), number(parts[1], 65535), number(parts[2], 65535)};
}
const Json &policy() {
    static const auto value = bundled_json("bridge-compatibility.json");
    return value;
}
}
BridgeCompatibilityReport waiting_bridge_report() {
    return {BridgeCompatibilityKind::Waiting, {}, {},
            "Addon compatibility not confirmed. Run /bl version in-game while the game is visible."};
}
BridgeCompatibilityReport legacy_bridge_report() {
    return {BridgeCompatibilityKind::Legacy, {}, {},
            "Addon version could not be confirmed. Run /bl version; update the addon if no version appears. Older chat formats remain available."};
}
BridgeCompatibilityReport assess_bridge_hello(std::string_view text, uint32_t receipt_nonce) {
    if (!text.starts_with("BLV")) return legacy_bridge_report();
    BridgeCompatibilityReport report;
    report.kind = BridgeCompatibilityKind::Invalid;
    report.text = "Invalid or unsupported addon compatibility message. Update both components; pixel chat is paused.";
    try {
        if (text.size() > 128) return report;
        const auto parts = fields(text, '\t');
        if ((parts.size() != 5 && parts.size() != 7 && parts.size() != 8) || parts[0] != "BLV1") return report;
        const auto addon = version(parts[1]);
        const auto desktop_min = version(parts[2]);
        const auto low = number(parts[3], 65535), high = number(parts[4], 65535);
        if (!low || low > high) return report;
        bool confirmed = false;
        if (parts.size() >= 7) {
            const auto nonce = number(parts[6], UINT32_MAX);
            if (parts[5] != "-") {
                version(parts[5]);
                confirmed = receipt_nonce && nonce == receipt_nonce && parts[5] == BL_VERSION;
            } else if (nonce) return report;
        }
        const bool game_blocked = parts.size() == 8 && number(parts[7], 1) != 0;
        report.addon_version = parts[1];
        report.minimum_desktop = parts[2];
        if (version(BL_VERSION) < desktop_min || low > policy().at("protocolMax").get<unsigned>()) {
            report.kind = BridgeCompatibilityKind::UpdateDesktop;
            report.text = "Addon " + report.addon_version + " requires a newer desktop app (minimum " +
                          report.minimum_desktop + "). Update the desktop app; pixel chat is paused.";
            if (version(BL_VERSION) >= desktop_min)
                report.text = "Addon bridge protocol is newer than this desktop supports. Update the desktop app; pixel chat is paused.";
        } else if (addon < version(policy().at("minimumAddon").get<std::string>()) ||
                   high < policy().at("protocolMin").get<unsigned>()) {
            report.kind = BridgeCompatibilityKind::UpdateAddon;
            report.text = "Desktop app requires addon " + policy().at("minimumAddon").get<std::string>() +
                          " or newer. Update the addon; pixel chat is paused.";
            if (addon >= version(policy().at("minimumAddon").get<std::string>()))
                report.text = "Addon bridge protocol is too old for this desktop. Update the addon; pixel chat is paused.";
        } else if (game_blocked) {
            report.kind = BridgeCompatibilityKind::Invalid;
            report.text = "Game-side version check failed; pixel chat is paused. Update both components or reset the manual confirmation.";
        } else {
            report.kind = BridgeCompatibilityKind::Compatible;
            report.text = "Compatible: desktop " + std::string(BL_VERSION) + ", addon " + report.addon_version + ".";
            if (confirmed) report.text += "\nGame-side version confirmation received for this desktop session.";
        }
    } catch (const std::exception &) {}
    return report;
}
std::string desktop_compatibility_receipt(uint32_t session, uint32_t nonce) {
    return "/bl pair BLP1:" + std::string(BL_VERSION) + ":" + policy().at("minimumAddon").get<std::string>() +
           ":" + std::to_string(policy().at("protocolMin").get<unsigned>()) + ":" +
           std::to_string(policy().at("protocolMax").get<unsigned>()) + ":" + std::to_string(session) + ":" + std::to_string(nonce);
}
} // namespace bl
