#include "bianlingo/theme.hpp"
#include "bianlingo/chat_style.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
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
    throw std::runtime_error("Invalid theme accepted");
}
struct Scratch {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        std::filesystem::path("bianlingo-themes-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch() {
        std::filesystem::create_directories(path);
    }
    ~Scratch() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
} // namespace
int main() {
    try {
        Scratch scratch;
        auto root = scratch.path / "Skins", input = scratch.path / "input.json";
        ThemeRegistry registry(root);
        auto settings = default_settings();
        require(registry.styles().size() == 3, "Built-ins missing");
        require(registry.resolve(settings).metrics.card_radius == 10, "Built-in JSON metrics ignored");
        require(registry.is_builtin("classic") && !registry.is_builtin("example.missing"),
                "Built-in recognition is not driven by registered themes");
        for (const auto &id : {"ios", "terminal", "classic"}) {
            auto fresh = settings;
            // First visit must use the theme default, not inherit another style's opacity.
            registry.change_style(fresh, id == std::string_view("ios") ? "terminal" : "ios");
            fresh["style_memory"] = Json::object();
            fresh["background_opacity"] = 0.9;
            registry.change_style(fresh, id);
            require(std::abs(background_opacity(fresh) - 0.30) < 0.0001,
                    "A built-in theme did not default to 70% transparency");
        }
        auto classic_settings = settings;
        registry.change_style(classic_settings, "classic");
        validate_skin(classic_settings);
        auto classic = registry.resolve(classic_settings);
        require(classic.palette.background == 0 && classic.chat.tab_text == 0xffd100 &&
                    classic.chat.glow == 0xff8040 && classic.chat.tabs == ChatTabVisual::Classic &&
                    !classic.chat.unread_count && classic.chat.flash_unread &&
                    classic_settings.at("background_opacity") == 0.30,
                "Classic colors or default 70% transparency differ");
        require(classic.widgets.frame == FrameVisual::Beveled &&
                    classic.widgets.slider == SliderVisual::Diamond && classic.fonts.serif &&
                    !classic.fonts.monospace,
                "Classic settings did not select beveled chrome, diamond slider and serif text");
        require(registry.resolve(settings).widgets.frame == FrameVisual::Flat &&
                    registry.resolve(settings).widgets.slider == SliderVisual::Round &&
                    !registry.resolve(settings).fonts.serif,
                "Classic chrome leaked into the default theme");
        for (auto [time, expected] :
             {std::pair{0u, 0.0}, {500u, 0.5}, {1000u, 1.0}, {1500u, 0.5}, {2000u, 0.0}, {2500u, 0.5}})
            require(std::abs(chat_flash_alpha(classic.chat, time) - expected) < 1e-8,
                    "Classic fade is not the native 1-second-in / 1-second-out cycle");
        require(chat_flash_alpha(registry.resolve(settings).chat, 500) == 0,
                "Nonflashing theme pulses while idle");
        classic_settings["background_opacity"] = 0.6;
        registry.change_style(classic_settings, "ios");
        require(classic_settings["background_opacity"] == settings["background_opacity"],
                "Classic selection changed the prior style's opacity");
        registry.change_style(classic_settings, "classic");
        require(classic_settings["background_opacity"] == 0.6, "Custom classic opacity was not remembered");
        auto classic_export = registry.export_theme(classic_settings, "example.classic", "Classic community");
        write_json(input, classic_export);
        registry.import_file(input);
        auto classic_copy = classic_settings;
        registry.change_style(classic_copy, "example.classic");
        auto classic_roundtrip = registry.resolve(classic_copy);
        classic_roundtrip.id = classic.id;
        classic_roundtrip.name = classic.name;
        require(classic_roundtrip == classic, "Classic chat tokens did not survive community export/import");
        auto theme = registry.export_theme(settings, "example.obsidian", "Obsidian");
        theme["metrics"]["sliderPadding"] = 31;
        theme["metrics"]["thumbSize"] = 16;
        theme["metrics"]["popupRowHeight"] = 38;
        theme["metrics"]["borderWidth"] = 2;
        theme["widgets"]["toggle"] = "checkbox";
        theme["widgets"]["slider"] = "square";
        theme["fonts"]["latin"] = "system-mono";
        theme["colors"]["focus"] = "#FF3355";
        write_json(input, theme);
        require(registry.import_file(input) == "example.obsidian", "Import ID changed");
        registry.change_style(settings, "example.obsidian");
        validate_skin(settings);
        auto resolved = registry.resolve(settings);
        require(resolved.colors.focus == 0xff3355 && resolved.fonts.monospace &&
                    resolved.widgets.toggle == ToggleVisual::Checkbox &&
                    resolved.widgets.slider == SliderVisual::Square,
                "Typed tokens lost");
        for (int dpi : {96, 120, 144, 192})
            for (int width : {80, 212, 390}) {
                auto g = slider_geometry(resolved.metrics, width, 50, dpi);
                require(g.value(g.start, 0, 1000) == 0 && g.value(g.end, 0, 1000) == 1000,
                        "Slider endpoints diverge");
                require(g.start >= g.thumb_half + 1, "Thumb leaves control bounds");
                for (int x = g.start; x <= g.end; ++x)
                    require(std::abs(g.pixel(g.value(x, 0, 1000), 0, 1000) - x) <= 1,
                            "Paint/input geometry diverges");
            }
        auto exported = registry.export_theme(settings, "example.roundtrip", "Roundtrip");
        write_json(input, exported);
        registry.import_file(input);
        auto copy = settings;
        registry.change_style(copy, "example.roundtrip");
        auto roundtrip = registry.resolve(copy);
        roundtrip.id = resolved.id;
        roundtrip.name = resolved.name;
        require(roundtrip == resolved, "Export/import changes resolved tokens");
        registry.change_style(settings, "ios");
        settings["skin"] = "navy";
        registry.change_style(settings, "example.obsidian");
        settings["skin"] = "custom";
        settings["skin_colors"]["background"] = "#123456";
        registry.change_style(settings, "ios");
        require(settings["skin"] == "navy", "Built-in palette memory lost");
        registry.change_style(settings, "example.obsidian");
        require(registry.resolve(settings).palette.background == 0x123456,
                "Community custom palette memory lost");
        ThemeRegistry loaded(root);
        require(loaded.resolve(settings) == registry.resolve(settings), "Restart reload lost installed skin");
        auto bad = [&](Json value) {
            write_json(input, value);
            auto before = registry.resolve(settings);
            rejects([&] { registry.import_file(input); });
            require(before == registry.resolve(settings), "Failed import mutated active registry");
        };
        for (auto id : {"../escape", "ios", "a..b", "a/b", "A.theme", "con"}) {
            auto value = theme;
            value["id"] = id;
            bad(value);
        }
        for (auto [key, value] :
             std::vector<std::pair<std::string, Json>>{{"tabs", "unknown"},
                                                       {"flashUnread", 1},
                                                       {"fadeInMs", 0},
                                                       {"fadeOutMs", 10001},
                                                       {"tabText", "#bad"},
                                                       {"defaultBackgroundOpacity", 1.2},
                                                       {"defaultBackgroundOpacity", "0.25"}}) {
            auto invalid = classic_export;
            invalid["chat"][key] = value;
            bad(invalid);
        }
        for (auto [field, value] : std::vector<std::pair<std::string, Json>>{
                 {"frame", "unknown"}, {"frame", 2}, {"slider", "unknown"}}) {
            auto invalid = classic_export;
            invalid["widgets"][field] = value;
            bad(invalid);
        }
        for (auto field : {"metrics", "fonts", "widgets", "colors", "chat"}) {
            auto value = theme;
            value[field]["typo"] = 1;
            bad(value);
        }
        {
            auto value = theme;
            value["schemaVersion"] = 2;
            bad(value);
        }
        {
            auto value = theme;
            value["base"] = "example.obsidian";
            bad(value);
        }
        for (auto field : {"sliderPadding", "thumbSize", "borderWidth", "popupRowHeight", "toggleWidth"})
            for (int n : {-1, 1000}) {
                auto value = theme;
                value["metrics"][field] = n;
                bad(value);
            }
        {
            auto value = theme;
            value["metrics"]["sliderPadding"] = 12.5;
            bad(value);
        }
        {
            auto value = theme;
            value["fonts"]["latin"] = "https://font";
            bad(value);
        }
        {
            auto value = theme;
            value["colors"]["accent"] = "red";
            bad(value);
        }
        {
            std::ofstream out(input);
            out << "{\"schemaVersion\":1,\"schemaVersion\":1}";
        }
        rejects([&] { registry.import_file(input); });
        {
            std::ofstream out(input);
            out << std::string(65537, ' ');
        }
        rejects([&] { registry.import_file(input); });
        {
            std::ofstream out(input);
            out << std::string(18, '[') << "0" << std::string(18, ']');
        }
        rejects([&] { registry.import_file(input); });
        auto business = default_settings();
        business["model"] = "custom-model";
        business["incoming_prompt"] = "custom instructions";
        business["channels"] = {4, 9};
        auto expected = business;
        business["skin_colors"] = false;
        business["ui_style"] = "invalid";
        business["style_memory"] = {{"broken", false}};
        require(normalize_skin_settings(business), "Broken appearance not repaired");
        for (auto key : {"model", "incoming_prompt", "channels", "hotkey", "groups", "base_url"})
            require(business[key] == expected[key], "Appearance repair destroyed business settings");
        validate_skin(business);
        require(!normalize_skin_settings(business), "Repair is not idempotent");
        auto missing = settings;
        missing["ui_style"] = "missing.theme";
        std::string warning;
        require(registry.fallback(missing, &warning).id == "ios" && !warning.empty(),
                "Missing skin did not fall back");
        std::filesystem::create_directories(root / "bad.theme");
        write_json(root / "bad.theme" / "theme.json", Json{{"broken", true}});
        loaded.reload();
        require(!loaded.warnings().empty() && loaded.resolve(settings) == registry.resolve(settings),
                "Bad neighbor skin breaks valid skins");
        {
            auto file = scratch.path / "not-a-directory";
            std::ofstream out(file);
            out << "x";
            out.close();
            ThemeRegistry invalid(file);
            require(invalid.styles().size() == 3 && !invalid.warnings().empty(),
                    "Unreadable directory prevents startup");
        }
        std::error_code error;
        auto link = root / "linked.theme";
        std::filesystem::create_directory_symlink(scratch.path, link, error);
        if (!error) {
            auto value = theme;
            value["id"] = "linked.theme";
            bad(value);
            std::filesystem::remove(link);
        }
        std::cout << checks << " theme checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
