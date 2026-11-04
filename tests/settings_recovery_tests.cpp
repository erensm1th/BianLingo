#include "bianlingo/provider.hpp"
#include "bianlingo/localization.hpp"
#include <iostream>
using namespace bl;
namespace fs = std::filesystem;
void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
int main() {
    auto directory = fs::temp_directory_path() / ("settings-recovery-" + sha256(random_bytes(12)).substr(0,16));
    struct Cleanup { fs::path p; ~Cleanup() { std::error_code ec; fs::remove_all(p,ec); } } cleanup{directory};
    try {
        check(default_settings().at("ui_language") == "system", "New installations must follow system language");
        for (const auto &language : ui_languages) {
            check(supported_ui_language(language.code), "Listed UI language is not selectable");
            set_ui_language_preference(language.code);
            check(ui_language() == language.code, "Explicit UI preference did not override the system");
            check(ui_text("Save") != "Save" || language.code == "en", "Selectable UI language lacks core captions");
        }
        set_ui_language_preference("system");
        check(!supported_ui_language("invalid") && normalized_ui_language("ar-SA") == "en",
              "Unsupported interface language did not fall back safely");
        auto preference_dir = directory / "language";
        Engine preferences(preference_dir);
        auto chosen = preferences.settings();
        chosen["active_connection"] = "default"; // This fixture exercises API key persistence.
        chosen["ui_language"] = "ja";
        chosen["target_language"] = "fr";
        chosen["target"] = "English";
        chosen["incoming_prompt"] = "custom exact instructions {language}";
        chosen["outbound_template"] = "custom outgoing {language}";
        preferences.configure(chosen, "synthetic-preference-key");
        auto persisted = Engine(preference_dir).settings();
        check(persisted.at("ui_language") == "ja" && persisted.at("target_language") == "fr" &&
              persisted.at("target") == "English" && persisted.at("incoming_prompt") == chosen.at("incoming_prompt") &&
              persisted.at("outbound_template") == chosen.at("outbound_template") && Engine(preference_dir).has_key(),
              "UI preference restart lost targets, custom prompts or credentials");
        auto valid_bytes = read_bytes(preference_dir / "settings.json");
        persisted["ui_language"] = "unknown";
        bool invalid_language = false;
        try { preferences.configure(persisted); } catch (const std::exception &) { invalid_language = true; }
        check(invalid_language && read_bytes(preference_dir / "settings.json") == valid_bytes,
              "Invalid UI choice changed the saved configuration");
        write_json(preference_dir / "settings.json", persisted);
        Engine repaired(preference_dir);
        check(repaired.settings().at("ui_language") == "system" && repaired.has_key() &&
              repaired.settings().at("incoming_prompt") == chosen.at("incoming_prompt"),
              "Invalid saved UI preference discarded unrelated settings");
        auto s = default_settings();
        s["incoming_prompt"] = "synthetic custom prompt";
        s["font_size"] = 21;
        s["channels"] = Json::array({0}); // One invalid field must not erase unrelated state.
        auto profiles = Json::array({make_connection(default_settings(), "synthetic-recovery-key")});
        s["connection_store"] = seal_connections(profiles);
        auto path = directory / "settings.json";
        write_json(path, s);
        auto original = read_bytes(path);
        Engine engine(directory);
        auto recovered = engine.settings();
        check(recovered["incoming_prompt"] == s["incoming_prompt"] && recovered["font_size"] == 21 &&
              recovered["channels"] == default_settings()["channels"] && engine.has_key(),
              "One rejected setting discarded valid prompts, preferences or credentials");
        check(read_bytes(path) == original, "Loading a recovered configuration overwrote the original");
        int backups = 0;
        for (const auto &entry : fs::directory_iterator(directory))
            if (entry.path().filename().string().starts_with("settings.recovery-")) {
                ++backups; check(read_bytes(entry.path()) == original, "Recovery backup is not byte-identical");
            }
        check(backups == 1, "Damaged configuration was not backed up exactly once");
        recovered["x"] = 321; engine.configure(recovered);
        check(active_connection(open_connections(read_json(path)["connection_store"].get<std::string>()), "default")["key"] == "synthetic-recovery-key",
              "Saving geometry after recovery lost credentials");
        check(Engine(directory).settings()["incoming_prompt"] == s["incoming_prompt"], "Recovered preferences did not persist");
        auto invalid_geometry = read_json(path);
        invalid_geometry["width"] = "bad-width";
        invalid_geometry["active_connection"] = "missing-profile";
        write_json(path,invalid_geometry);
        Engine geometry_recovered(directory);
        check(geometry_recovered.has_key() && geometry_recovered.settings()["width"] == default_settings()["width"] &&
              geometry_recovered.settings()["active_connection"] == "default",
              "Bad geometry or a missing active profile discarded valid connection material");
        auto malformed = directory / "malformed";
        const std::string raw = "{ broken JSON and synthetic data\n";
        atomic_write(malformed / "settings.json", std::span(reinterpret_cast<const uint8_t *>(raw.data()), raw.size()));
        Engine broken(malformed); auto fresh = broken.settings(); fresh["x"] = 111; broken.configure(fresh);
        backups = 0;
        for (const auto &entry : fs::directory_iterator(malformed))
            if (entry.path().filename().string().starts_with("settings.recovery-")) {
                ++backups; const auto bytes = read_bytes(entry.path());
                check(std::string(bytes.begin(),bytes.end()) == raw, "Malformed bytes were not preserved");
            }
        check(backups == 1, "Malformed JSON did not trigger a recovery backup");
        fs::create_directories(directory / "unwritable" / "settings.json");
        Engine blocked(directory / "unwritable");
        bool rejected = false;
        try { blocked.configure(blocked.settings()); } catch (const std::exception &) { rejected = true; }
        check(rejected && fs::is_directory(directory / "unwritable" / "settings.json"), "Backup failure allowed an overwrite");
        std::cout << "PASS isolated field recovery, encrypted credentials, byte backups, reload and failed-backup protection\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
