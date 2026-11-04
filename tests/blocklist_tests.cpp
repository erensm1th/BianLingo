#include "bianlingo/blocklist.hpp"
#include <iostream>
using namespace bl;
namespace {
void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
template<class F> void rejects(F work) { try { work(); } catch (...) { return; } throw std::runtime_error("Invalid blocklist accepted"); }
Message player(std::string name, std::string realm = {}) {
    Message m; m.event = "CHAT_MSG_SAY"; m.author = std::move(name); m.realm = std::move(realm); return m;
}
}
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--verify-lua") {
            auto fixture = read_json(argv[2]);
            PlayerBlocklist merged;
            Assembler assembler;
            int chunks = 0;
            for (const auto &raw : fixture.at("packets")) {
                const auto hex = raw.get<std::string>(); Bytes bytes;
                for (size_t i = 0; i < hex.size(); i += 2) bytes.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
                auto packet = decode_packet(bytes); require(packet.has_value(), "Real Lua blocklist packet failed decoding");
                if (auto message = assembler.accept(*packet)) {
                    require(message->event == "BRIDGE_BLOCKLIST", "Blocklist control was decoded as chat");
                    merged.merge(Json::parse(message->text)); ++chunks;
                }
            }
            require(chunks >= 3, "Large Lua blocklist did not cross independent control messages");
            merged.merge(fixture.at("original"));
            require(!merged.blocks(player("Alice-Westfall")) && merged.blocks(player("Bob-Other")) &&
                        merged.blocks(player("玩家-比格沃斯")), "Roundtrip resurrected a removed block or lost a player");
            std::cout << "PASS C++ export -> real Lua import/edit -> real BLG2 -> C++ merge\n";
            return 0;
        }
        PlayerBlocklist list;
        require(list.names().empty() && !list.blocks(player("Alice")), "Default blocklist is not empty");
        list.set("Alice-Westfall", true);
        require(list.blocks(player("ALICE-West Fall")) && list.blocks(player("Alice", "Westfall")) &&
                    !list.blocks(player("Alice-Other")), "Realm-aware block identity differs");
        list.set("Other-Quel'Thalas", true);
        require(list.blocks(player("Other", "Quel'Thalas")), "Apostrophe in a real realm name was rejected");
        auto old = list.snapshot();
        list.set("Alice-Westfall", false); list.merge(old);
        require(!list.blocks(player("Alice-Westfall")), "An old snapshot resurrects an unblocked player");
        list.set("Alice", true);
        require(list.blocks(player("Alice-Other")), "Explicit realm-less rule did not match all namesakes");
        auto ocr = player("Alice"); ocr.event = "OCR_CHAT";
        require(!list.blocks(ocr), "Unverified OCR identity was blocked");
        ocr.event = "CHAT_MSG_BN_WHISPER";
        require(!list.blocks(ocr), "Character rule was applied to an unrelated BNet identity");
        PlayerBlocklist a, b;
        a.set("One-Realm", true); b.set("Two-Realm", true);
        auto a_state = a.snapshot(), b_state = b.snapshot();
        a.merge(b_state); b.merge(a_state);
        require(a.snapshot() == b.snapshot(), "Independent concurrent additions do not converge");
        Json conflict{{"schemaVersion", 1}, {"entries", {{"One-Realm", 12, "same-node", true}, {"One-Realm", 12, "same-node", false}}}};
        PlayerBlocklist removal(conflict);
        require(!removal.blocks(player("One-Realm")), "Same-version tie must prefer removal");
        auto before = a.snapshot();
        auto invalid = b_state; invalid["entries"].push_back({"bad;name", 123, "x", true});
        rejects([&] { a.merge(invalid); });
        require(a.snapshot() == before, "Malformed snapshot partially changes records");
        rejects([&] { list.set("/run evil", true); });
        rejects([&] { list.set("Bad\nName", true); });
        PlayerBlocklist reopened(list.snapshot(true));
        require(reopened.snapshot() == list.snapshot(), "Stored blocks or tombstones were lost on restart");
        const auto root = std::filesystem::temp_directory_path() / ("bianlingo-blocks-" + sha256(random_bytes(16)).substr(0, 16));
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path root, parent;
            ~Cleanup() {
                std::error_code e;
                auto resolved = std::filesystem::weakly_canonical(root, e);
                if (!e && resolved.parent_path() == parent && resolved.filename().string().starts_with("bianlingo-blocks-"))
                    std::filesystem::remove_all(resolved, e);
            }
        } cleanup{root, std::filesystem::weakly_canonical(std::filesystem::temp_directory_path())};
        Engine engine(root);
        auto fallback_settings = engine.settings(); fallback_settings["blocklist_addon_directory"] = "Z:/must-not-be-touched";
        engine.configure(fallback_settings);
        require(!engine.settings().contains("blocklist_addon_directory"), "Obsolete game path was retained");
        engine.set_player_blocked("Stored-Realm", true);
        require(engine.player_blocked(player("Stored-Realm")), "Engine block does not take effect immediately");
        Engine restart(root);
        require(restart.player_blocked(player("Stored-Realm")), "Engine block does not persist");
        auto notices = engine.drain();
        require(std::any_of(notices.begin(), notices.end(), [](auto &n) { return n.kind == Notice::Blocklist; }), "Blocklist change notice missing");
        PlayerBlocklist exported;
        exported.set("Alice-Westfall", true); exported.set("玩家-比格沃斯", true); exported.set("Removed-Realm", false);
        auto code = blocklist_import_code(exported);
        require(code.starts_with("BLGB1|") && code.size() <= max_block_import_bytes, "Invalid data-only code");
        require(!std::filesystem::exists(root / "BlocklistSync.lua"), "Block action wrote game Lua");
        if (argc == 2) {
            atomic_write(std::filesystem::path(argv[1]), std::span(reinterpret_cast<const uint8_t *>(code.data()), code.size()));
            write_json(std::filesystem::path(argv[1]).replace_extension("json"), exported.snapshot());
        }
        std::cout << "PASS block identities, convergence, removals, persistence and data-only export\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
