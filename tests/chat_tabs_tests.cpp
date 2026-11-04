#include "bianlingo/chat_tabs.hpp"
#include "bianlingo/localization.hpp"
#include <iostream>
using namespace bl;
namespace {
int checks = 0;
void require(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Invalid tab/layout accepted");
}
Message chat(std::string event = "CHAT_MSG_CHANNEL", int channel = 2, std::string name = "Trade - Westfall") {
    Message m;
    m.event = event;
    m.channel_number = channel;
    m.channel_name = name;
    return m;
}
} // namespace
int main() {
    try {
        auto settings = default_settings();
        auto tabs = settings.at("chat_tabs");
        auto parsed = parse_chat_tabs(tabs);
        require(parsed.size() == 1 && parsed[0].name == ui_text("General"), "Default normalization differs");
        for (auto &[event, _] : chat_event_options())
            require(parsed[0].matches(chat(event)), "Default tab lost supported chat");
        auto scoped = new_chat_tab("private", "Whisper");
        scoped["events"] = {"CHAT_MSG_WHISPER", "CHAT_MSG_WHISPER_INFORM"};
        parsed = parse_chat_tabs(Json::array({scoped}));
        require(parsed[0].matches(chat("CHAT_MSG_WHISPER")) && !parsed[0].matches(chat("CHAT_MSG_PARTY")),
                "Private filter leaked");
        scoped = new_chat_tab("public", "Trade");
        scoped["events"] = {"CHAT_MSG_CHANNEL"};
        scoped["channels"] = {"Trade", 3};
        scoped["exclude_channels"] = {1, "LookingForGroup"};
        parsed = parse_chat_tabs(Json::array({scoped}));
        auto &t = parsed[0];
        require(t.matches(chat()) && t.matches(chat("CHAT_MSG_CHANNEL", 4, "trade - New Zone")),
                "Named channel did not survive number/zone change");
        require(t.matches(chat("CHAT_MSG_CHANNEL", 3, "Custom")), "Channel number include failed");
        require(!t.matches(chat("CHAT_MSG_CHANNEL", 1, "Trade")) &&
                    !t.matches(chat("CHAT_MSG_CHANNEL", 3, "LookingForGroup")),
                "Exclusion must win");
        require(!t.matches(chat("CHAT_MSG_CHANNEL", 2, "General")), "Unselected channel leaked");
        require(!t.matches(chat("CHAT_MSG_GUILD")), "Unselected type leaked");
        auto channel_values = Json::array({1, 2, "Trade - Westfall", "Trade"});
        require(parse_channel_filter(format_channel_filter(channel_values)) == channel_values,
                "Channel filter roundtrip loses spaces");
        require(parse_channel_filter("1 2，交易； 4") == Json::array({1, 2, "交易", 4}),
                "Chinese separators or space-separated numbers rejected");
        rejects([&] { parse_channel_filter("0, 交易"); });
        rejects([&] { parse_channel_filter("1000"); });
        Json source = {{"schemaVersion", 1},
                       {"windows", Json::array({new_chat_tab("wow-1", "综合 \"测试\" \\"),
                                                new_chat_tab("wow-4", "Party")})}};
        source["windows"][1]["events"] = {"CHAT_MSG_PARTY", "CHAT_MSG_PARTY_LEADER"};
        auto snapshot = parse_chat_layout(source.dump());
        require(snapshot == source, "Snapshot parse lost Unicode/escaping");
        auto imported = import_chat_layout(snapshot);
        require(imported == source["windows"] && imported.size() == 2,
                "Import appended the default comprehensive tab instead of replacing it");
        require(import_chat_layout(snapshot) == imported, "Repeated import changed source order");
        source["windows"][0]["name"] = "游戏已改名";
        source["windows"].erase(source["windows"].begin() + 1);
        auto updated = import_chat_layout(source);
        require(updated == source["windows"] && updated.size() == 1,
                "Reimport retained a deleted game window or lost a rename");
        auto active = settings;
        active["chat_tabs"] = updated;
        normalize_chat_tabs(active);
        require(active["active_chat_tab"] == "wow-1" && active["groups"] == settings["groups"],
                "Replacement left a stale active ID or changed translation range");
        auto sync_settings = settings;
        sync_settings["model"] = "committed-model";
        sync_settings["incoming_prompt"] = "committed-prompt";
        auto path = std::filesystem::temp_directory_path() /
                    ("bianlingo-layout-sync-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove(path, error);
            }
        } cleanup{path};
        size_t writes = 0;
        auto persist = [&](const Json &next) {
            write_json(path, next);
            ++writes;
        };
        auto synced = synchronize_chat_layout(sync_settings, snapshot, persist);
        require(writes == 1 && synced["chat_tabs"] == snapshot["windows"] && read_json(path) == synced,
                "One-click layout was not persisted before returning");
        auto untouched = synced;
        untouched["chat_tabs"] = sync_settings["chat_tabs"];
        untouched["active_chat_tab"] = sync_settings["active_chat_tab"];
        require(untouched == sync_settings, "Layout synchronization changed unrelated settings");
        require(synchronize_chat_layout(synced, snapshot, persist) == synced && writes == 1,
                "Identical export unnecessarily rewrites settings");
        auto selected = synced;
        selected["active_chat_tab"] = "wow-4";
        require(synchronize_chat_layout(selected, snapshot, persist)["active_chat_tab"] == "wow-4",
                "Sync reset a still-existing active game tab");
        auto before_failed_save = read_json(path);
        rejects([&] {
            synchronize_chat_layout(synced, source,
                                    [](const Json &) { throw std::runtime_error("simulated disk failure"); });
        });
        require(read_json(path) == before_failed_save && synced["chat_tabs"] == snapshot["windows"],
                "Persistence failure damaged existing settings");
        rejects([&] {
            synchronize_chat_layout(synced, source, [&](const Json &candidate) {
                write_json(path / "blocked.json", candidate); // Parent is a file: real I/O failure.
            });
        });
        require(read_json(path) == before_failed_save, "Real I/O failure damaged saved settings");
        auto invalid_sync = snapshot;
        invalid_sync["windows"][0]["events"] = {"BAD_EVENT"};
        auto before_invalid_writes = writes;
        rejects([&] { synchronize_chat_layout(synced, invalid_sync, persist); });
        require(writes == before_invalid_writes && read_json(path) == before_failed_save,
                "Invalid snapshot reached persistence or changed the current layout");
        auto broken = source;
        broken["windows"][0]["id"] = "main";
        rejects([&] { parse_chat_layout(broken.dump()); });
        broken = source;
        broken["schemaVersion"] = 2;
        rejects([&] { parse_chat_layout(broken.dump()); });
        broken = source;
        broken["windows"][0]["events"] = {"CHAT_MSG_COMBAT_LOG"};
        rejects([&] { parse_chat_layout(broken.dump()); });
        rejects([&] { parse_chat_layout(std::string(12289, ' ')); });
        rejects([&] { parse_chat_layout("{\"schemaVersion\":1,\"schemaVersion\":1}"); });
        for (auto id : {"", "../../escape", "bad/path", "not allowed"}) {
            auto bad = tabs;
            bad[0]["id"] = id;
            rejects([&] { parse_chat_tabs(bad); });
        }
        for (auto name : {"", "\n", "1234567890123456789012345"}) {
            auto bad = tabs;
            bad[0]["name"] = name;
            rejects([&] { parse_chat_tabs(bad); });
        }
        auto dupe = tabs;
        dupe.push_back(tabs[0]);
        rejects([&] { parse_chat_tabs(dupe); });
        rejects([&] { parse_chat_tabs(Json::array()); });
        Json maximum = Json::array();
        for (int i = 0; i < 16; ++i)
            maximum.push_back(new_chat_tab("tab-" + std::to_string(i), "窗口" + std::to_string(i)));
        require(parse_chat_tabs(maximum).size() == 16, "Maximum tab count rejected");
        maximum.push_back(new_chat_tab("overflow", "溢出"));
        rejects([&] { parse_chat_tabs(maximum); });
        auto business = settings;
        business["model"] = "kept-model";
        business["incoming_prompt"] = "kept-prompt";
        business["chat_tabs"] = false;
        business["active_chat_tab"] = "missing";
        require(normalize_chat_tabs(business) && business["model"] == "kept-model" &&
                    business["incoming_prompt"] == "kept-prompt" && business["groups"] == settings["groups"],
                "Normalization damages translation settings");
        require(!normalize_chat_tabs(business), "Normalization not idempotent");
        std::deque<DisplayMessage> history;
        for (uint32_t seq : {10, 20, 30}) {
            DisplayMessage d;
            d.message.seq = seq;
            d.message.sent = seq;
            update_history(history, d, 3);
        }
        DisplayMessage middle;
        middle.message.seq = 15;
        middle.message.sent = 15;
        auto change = update_history(history, middle, 3);
        require(change.retained && change.position == 1 && change.removed == 1 &&
                    history[0].message.seq == 15,
                "History delta for middle insertion wrong");
        middle.message.seq = 1;
        middle.message.sent = 1;
        change = update_history(history, middle, 3);
        require(!change.retained && change.position == 0 && change.removed == 1 &&
                    history[0].message.seq == 15,
                "Outside-limit delta wrong");
        std::cout << checks << " chat tab checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
