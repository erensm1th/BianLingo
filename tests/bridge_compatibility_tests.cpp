#include "bianlingo/bridge_compatibility.hpp"
#include "bianlingo/bundle.hpp"
#include <iostream>

using namespace bl;
void require(bool value, const char *why) {
    if (!value) throw std::runtime_error(why);
}
int main() {
    try {
        const auto config = bundled_json("bridge-compatibility.json");
        const auto addon = config.at("minimumAddon").get<std::string>();
        const auto desktop = config.at("minimumDesktop").get<std::string>();
        auto hello = "BLV1\t" + addon + "\t" + desktop + "\t2\t2";
        require(assess_bridge_hello(hello).kind == BridgeCompatibilityKind::Compatible,
                "Current compatibility range rejected");
        const auto old = assess_bridge_hello("Screen bridge ready");
        require(old.kind == BridgeCompatibilityKind::Legacy && !old.blocking(), "Legacy status was marked compatible");
        require(assess_bridge_hello("BLV2\tignored").blocking(), "Unknown manifest schema accepted");
        require(assess_bridge_hello("BLV1\t" + addon + "\t65535.0.0\t2\t2").kind == BridgeCompatibilityKind::UpdateDesktop,
                "Minimum desktop version ignored");
        require(assess_bridge_hello("BLV1\t0.0.0\t0.0.0\t2\t2").kind == BridgeCompatibilityKind::UpdateAddon,
                "Minimum addon version ignored");
        require(assess_bridge_hello("BLV1\t" + addon + "\t0.0.0\t3\t3").kind == BridgeCompatibilityKind::UpdateDesktop,
                "Newer protocol range ignored");
        require(assess_bridge_hello("BLV1\t" + addon + "\t0.0.0\t1\t1").kind == BridgeCompatibilityKind::UpdateAddon,
                "Older protocol range ignored");
        for (auto bad : {"BLV1\t0.2.x\t0.2.11\t2\t2", "BLV1\t0.2.11\t0.2.11\t0\t2",
                         "BLV1\t0.2.11\t0.2.11\t3\t2", "BLV1\t0.2.11\t0.2.11\t2\t2\t-\t1",
                         "BLV1\t0.2.11\t0.2.11\t2\t2\t-\t0\t2"})
            require(assess_bridge_hello(bad).kind == BridgeCompatibilityKind::Invalid, "Malformed manifest accepted");
        const auto receipt = desktop_compatibility_receipt(1234, 42);
        require(receipt.starts_with("/bl pair BLP1:") && receipt.ends_with(":1234:42"), "Receipt is not session-bound");
        const auto start = std::string("/bl pair BLP1:").size();
        const auto current = receipt.substr(start, receipt.find(':', start) - start);
        const auto acknowledged = hello + "\t" + current + "\t42\t0";
        require(assess_bridge_hello(acknowledged, 42).text.find("confirmation received") != std::string::npos,
                "Matching desktop receipt not confirmed");
        require(assess_bridge_hello(acknowledged, 99).text.find("confirmation received") == std::string::npos,
                "Another desktop session's receipt was accepted");
        require(assess_bridge_hello(hello + "\t" + current + "\t42\t1", 42).blocking(),
                "Game-side mismatch did not pause traffic");
        std::cout << "Bridge compatibility cases passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
