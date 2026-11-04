#include "bianlingo/bubble.hpp"
#include "bianlingo/bundle.hpp"
#include "bianlingo/chat_preview.hpp"
#include "bianlingo/chat_style.hpp"
#include "bianlingo/core.hpp"
#include "bianlingo/localization.hpp"
#include "bianlingo/quick_reply.hpp"
#include "bianlingo/reply_target.hpp"
#include "bianlingo/skin.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace bl;
int checks = 0;
void require(bool good, const std::string &text) {
    checks++;
    if (!good)
        throw std::runtime_error(text);
}
Bytes from_hex(std::string_view s) {
    Bytes out;
    for (size_t i = 0; i < s.size(); i += 2)
        out.push_back(uint8_t(std::stoi(std::string(s.substr(i, 2)), nullptr, 16)));
    return out;
}
template <class F> void rejects(F f, const std::string &message) {
    try {
        f();
    } catch (const std::exception &) {
        checks++;
        return;
    }
    throw std::runtime_error(message);
}
void verify_bubble() {
    auto settings = default_settings();
    auto appearance = bubble_appearance(settings);
    require(appearance.transparency == 50 && appearance.scale == 100 && appearance.opacity() == 0.5,
            "Bubble defaults are not 50% transparent / 100% scale");
    for (auto work :
         {Rect{0, 0, 1920, 1080}, Rect{-1920, -400, 1920, 1080}, Rect{1920, 0, 1280, 720}, Rect{0, 0, 1, 1}})
        for (auto size : {Rect{0, 0, 160, 50}, Rect{0, 0, 320, 100}, Rect{0, 0, 8000, 5000}}) {
            auto bounds = bubble_bounds(work, size.w, size.h, 8);
            require(bounds.x >= work.x && bounds.y >= work.y && bounds.x + bounds.w <= work.x + work.w &&
                        bounds.y + bounds.h <= work.y + work.h,
                    "Bubble escapes the selected monitor work area");
            if (work.w > 1000 && size.w < 1000) {
                require(std::abs(bounds.x + bounds.w / 2.0 - (work.x + work.w / 2.0)) <= 1 &&
                            std::abs(bounds.y + bounds.h / 2.0 - (work.y + work.h * 0.4)) <= 1,
                        "Bubble is not centered slightly above the screen center");
            }
        }
    for (auto [key, value] : std::vector<std::pair<std::string, Json>>{{"bubble_transparency", -1},
                                                                       {"bubble_transparency", 101},
                                                                       {"bubble_transparency", "50"},
                                                                       {"bubble_scale", 49},
                                                                       {"bubble_scale", 201},
                                                                       {"bubble_scale", 100.5}}) {
        auto bad = settings;
        bad[key] = value;
        rejects([&] { bubble_appearance(bad); }, "Invalid bubble setting accepted");
        bad["model"] = "keep-model";
        require(normalize_bubble_settings(bad) && bad["model"] == "keep-model" &&
                    bubble_appearance(bad).transparency == 50 && bubble_appearance(bad).scale == 100,
                "Bubble repair resets unrelated settings");
    }
    require(!normalize_bubble_settings(settings), "Valid bubble settings changed during normalization");
}
void verify_chat_preview() {
    auto samples = chat_preview_messages(1000);
    require(samples.size() == 16, "Preview fixture coverage changed unexpectedly");
    std::set<std::string> preview_classes, preview_events;
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto &d = samples[i];
        const auto &m = d.message;
        require(m.seq == i + 1 && m.sent == 1000 - (samples.size() - i - 1), "Preview ordering/time changed");
        require(!m.author.empty() && !m.player_class.empty() && !m.race.empty() && !m.english_race.empty() &&
                    m.sex >= 2,
                "Preview metadata missing");
        require(!d.translation.empty() && !m.text.empty() && d.error.empty() && d.source == "Local test preview",
                "Preview requires network translation");
        preview_classes.insert(m.player_class);
        preview_events.insert(m.event);
    }
    require(preview_classes.size() == 9 && preview_events.size() >= 12,
            "Preview misses classic preview_classes or channel kinds");
    require(samples.back().message.replay_batch == 1, "Preview replay marker missing");
    auto early = chat_preview_messages(0);
    require(early.front().message.sent == 0, "Preview timestamp underflow");
    auto spans = chat_spans(samples[13].translation, chat_color(samples[13].message));
    for (auto color : {0x9d9d9du, 0x1eff00u, 0x0070ddu, 0xa335eeu, 0xff8000u})
        require(std::any_of(spans.begin(), spans.end(), [&](const auto &s) { return s.color == color; }),
                "Preview item rarity missing");
}
void verify_skin() {
    auto s = default_settings();
    require(skin_palette(s) == skin_preset("graphite"), "Default skin should be neutral graphite");
    for (auto name :
         {"graphite", "paper", "navy", "warm", "terminal_green", "terminal_amber", "terminal_ice"}) {
        s["ui_style"] = std::string_view(name).starts_with("terminal_") ? "terminal" : "ios";
        s["skin"] = name;
        validate_skin(s);
        s["skin_colors"] = skin_colors(skin_preset(name));
        s["skin"] = "custom";
        require(skin_palette(s) == skin_preset(name), "Preset/custom color round trip failed");
    }
    require(parse_skin_color("#00aAfF") == 0x00aaff && skin_color_hex(0x00aaff) == "#00AAFF",
            "RGB format failed");
    for (auto bad : {"#12345", "#1234567", "white", "#GG0000", "123456", "#112233x"})
        rejects([&] { parse_skin_color(bad); }, "Invalid skin RGB accepted");
    s["skin"] = "missing";
    rejects([&] { validate_skin(s); }, "Unknown skin accepted");
    s = default_settings();
    s["skin"] = "navy";
    change_ui_style(s, "terminal");
    require(s["skin"] == "terminal_green" && skin_palette(s).background == 0 && skin_palette(s).surface == 0,
            "Terminal defaults are not pure black");
    s["skin"] = "terminal_amber";
    change_ui_style(s, "ios");
    require(s["skin"] == "navy", "Original family palette not remembered");
    s["skin"] = "custom";
    s["skin_colors"] = skin_colors(skin_preset("paper"));
    change_ui_style(s, "terminal");
    require(s["skin"] == "terminal_amber", "Terminal palette not remembered");
    change_ui_style(s, "ios");
    require(s["skin"] == "custom" && skin_palette(s) == skin_preset("paper"),
            "Custom family colors not restored");
    validate_skin(s);
    for (auto name : {"terminal_green", "terminal_amber", "terminal_ice"})
        require(skin_preset(name).background == 0 && skin_preset(name).surface == 0,
                "Terminal palette not black");
    s["ui_style"] = "invalid";
    rejects([&] { validate_skin(s); }, "Unknown UI style accepted");
    s = default_settings();
    s["style_memory"] = {{"terminal", {{"skin", "navy"}, {"colors", skin_colors(skin_preset("navy"))}}}};
    rejects([&] { validate_skin(s); }, "Cross-family remembered palette accepted");
    Json fallback = {{"show_localized_class", true},
                   {"show_english_class", false},
                   {"show_english_race", true},
                   {"show_realm", true},
                   {"show_player_name", true}};
    normalize_player_display(fallback);
    require(fallback.size() == 2 && fallback.value("show_class", false) && fallback.value("show_race", false),
            "Display settings not simplified on normalization");
    Message player;
    player.event = "CHAT_MSG_PARTY";
    player.author = "Name-Server";
    player.realm = "Server";
    player.player_class = "HUNTER";
    player.english_race = "Human";
    player.race = "Human";
    player.sex = 3;
    require(player_display_name(player) == "Name", "Realm suffix still displayed");
    require(player_badges(player, Json{{"show_class", true}, {"show_race", true}, {"show_sex", true}}) ==
                "[猎人][人类][女]",
            "Simplified player badges duplicate or omit information");
    auto whisper = whisper_target(player);
    require(whisper && whisper->prefix == "/w Name-Server ",
            "Removing realm display broke cross-realm whisper");
    require(player_badges(player, default_settings()).empty(), "Player details should remain optional");
    player.author = "Runga-WestFall";
    player.realm = "WestFall";
    require(player_display_name(player) == "Runga" && whisper_target(player)->prefix == "/w Runga-WestFall ",
            "Classic realm suffix lost or duplicated in whisper target");
    player.author = "Runga";
    require(whisper_target(player)->prefix == "/w Runga-WestFall ",
            "Captured realm was not appended when author is short");
    player.event = "CHAT_MSG_WHISPER_INFORM";
    require(reply_target(player)->prefix == "/w Runga-WestFall ",
            "Outgoing whisper channel reply lost recipient");
    player.same_realm = true;
    require(reply_target(player)->prefix == "/w Runga ", "Confirmed local realm was not omitted");
    player.author = "Runga-WestFall";
    require(whisper_target(player)->prefix == "/w Runga " && player.author == "Runga-WestFall",
            "Local realm shortening changed the stored original identity");
    player.same_realm = false;
    require(whisper_target(player)->prefix == "/w Runga-WestFall ", "Unknown realm must retain full name");
    player.same_realm = true;
    player.author = "Runga-WestFall /unsafe";
    require(!whisper_target(player), "Shortening bypasses complete-name input validation");
}
void verify_chat_style() {
    Message colored;
    colored.event = "CHAT_MSG_PARTY";
    require(chat_color(colored) == 0xaaaaff, "Fallback party color fallback wrong");
    colored.chat_rgb = 0x123456;
    require(chat_color(colored) == 0x123456, "Captured game color was ignored");
    colored.chat_rgb = 0;
    require(chat_color(colored) == 0, "Black channel color lost");
    auto spans = chat_spans("hi |cff0070dd|Hitem:1|h[Blue Item]|h|r free |cffa335ee[epic]|r!", 0xffc0c0);
    require(spans.size() == 5 && spans[0].text == "hi " && spans[0].color == 0xffc0c0 &&
                spans[1].text == "[Blue Item]" && spans[1].color == 0x0070dd && spans[2].text == " free " &&
                spans[3].color == 0xa335ee && spans[4].color == 0xffc0c0,
            "Item link colors or channel-color reset wrong");
    auto protectedLinks = protect_chat_links("|cff0070dd|Hitem:1|h[啤酒烤猪排]|h|r x10 free at the inn");
    require(protectedLinks.text == "[[BLG_LINK_1]] x10 free at the inn" && protectedLinks.links.size() == 1,
            "Link protection removed surrounding chat or label");
    auto restored = protectedLinks.restore("旅馆免费送10个[[BLG_LINK_1]]");
    require(restored == "旅馆免费送10个|cff0070dd|Hitem:1|h[啤酒烤猪排]|h|r",
            "Translated link did not retain exact original markup");
    rejects([&] { protectedLinks.restore("丢失了链接"); }, "Missing link placeholder silently accepted");
    rejects([&] { protectedLinks.restore("[[BLG_LINK_1]][[BLG_LINK_1]]"); },
            "Duplicate link placeholder accepted");
    auto pairLinks = protect_chat_links("|cffffffff|Hitem:1|h[A]|h|r + |cffa335ee|Hspell:2|h[B]|h|r");
    require(pairLinks.links.size() == 2 &&
                clean_chat(pairLinks.restore("[[BLG_LINK_2]]和[[BLG_LINK_1]]")) == "[B]和[A]",
            "Multiple reordered links could not be restored");
    require(background_opacity(Json{{"clear_background", true}, {"opacity", 0.4}}) == 0 &&
                background_opacity(Json{{"clear_background", false}, {"opacity", 0.4}}) == 0.4 &&
                background_opacity(Json{{"clear_background", true}, {"background_opacity", 0.7}}) == 0.7,
            "Fallback transparency normalization or new background setting wrong");
}
int main() {
    try {
        verify_bubble();
        verify_chat_preview();
        verify_skin();
        verify_chat_style();
        auto root = std::filesystem::path(TEST_RESOURCE_DIR);
        auto bridge = read_json(root / "bridge.json");
        // Fixtures are independently rendered by the unchanged Lua sender and decoded by Python.
        // Arrays deliberately bypass read_json(), which only accepts object configuration files.
        bridge = Json::parse(read_bytes(root / "bridge.json"));
        for (auto &test : bridge) {
            Assembler assembler;
            std::optional<Message> message;
            auto packets = test["packets"];
            for (auto i = packets.rbegin(); i != packets.rend(); ++i) {
                auto raw = from_hex(i->get<std::string>());
                auto packet = decode_packet(raw);
                require(packet.has_value(), "Lua packet rejected");
                message = assembler.accept(*packet);
                require(!assembler.accept(*packet), "Duplicate fragment/message accepted");
                auto bad = raw;
                bad[30] ^= 1;
                require(!decode_packet(bad), "Checksum mismatch accepted");
            }
            require(message.has_value(), "Message not reassembled");
            auto expected = test["expected"];
            require(message->text == expected["text"].get<std::string>(), "UTF-8 message mismatch");
            require(message->author == expected["author"].get<std::string>(), "Author mismatch");
            require(message->player_class == expected["class"].get<std::string>(), "Class mismatch");
            require(message->race == expected["race"].get<std::string>(), "Race mismatch");
            require(message->sex == expected["sex"].get<int>(), "Sex mismatch");
            require(message->channel_number ==
                        (expected["channel_number"].is_null() ? 0 : expected["channel_number"].get<int>()),
                    "Channel mismatch");
        }
        for (auto &test : bridge)
            for (auto &file : test["images"]) {
                Image im{192, 24, 768, read_bytes(root / file.get<std::string>())};
                require(decode_image(im, {0, 0, 2}).has_value(), "Pixel decoding failed");
                Image padded{240, 50, 960, Bytes(240 * 50 * 4, 32)};
                for (int y = 0; y < im.height; y++)
                    std::copy_n(im.pixels.begin() + y * im.stride, im.stride,
                                padded.pixels.begin() + (y + 6) * padded.stride + 12 * 4);
                auto loc = locate(padded);
                require(loc.has_value(), "Sync locate failed");
                require(std::abs(loc->x - 12) < 0.1 && loc->y == 6, "Sync coordinates mismatch");
                require(!decode_image(im, {-1, 0, 2}), "Negative sample coordinates accepted");
            }
        auto filters = Json::parse(read_bytes(root / "filter.json"));
        for (auto &test : filters) {
            auto s = test["text"].get<std::string>();
            require(should_translate(s) == test["translate"].get<bool>(), "Filter regression: " + s);
        }
        require(clean_chat("|cnIQ1:|Hitem:1|h[炉石]|h|r") == "[炉石]", "Item link cleaning failed");
        require(should_translate("|Hitem:1|h[繁體物品]|h x10 free at the inn"),
                "Item label influenced language filter");
        require(!should_translate("来个 DPS"), "Chinese abbreviation called API");
        for (auto s : {"12345", "a", "a1234!", "A!@#$123", "1a2"})
            require(!should_translate(s), "Noise not skipped");
        rejects([] { utf32(std::string("\xc0\x80", 2)); }, "Overlong UTF-8 accepted");
        rejects([] { utf32(std::string("\xed\xa0\x80", 3)); }, "Surrogate UTF-8 accepted");
        std::atomic<bool> stopped = false;
        auto stop_started = Clock::now();
        {
            Worker worker([&](StopToken stop) {
                stop.wait_for(std::chrono::minutes(5));
                stopped = true;
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            worker.request_stop();
        }
        require(stopped && Clock::now() - stop_started < std::chrono::seconds(1),
                "Worker shutdown did not interrupt sleep");
        auto glossary = Glossary(read_json(std::filesystem::path(DATA_RESOURCE_DIR) / "glossary_zhCN.json"));
        auto matches = glossary.matches("LFM Deadmines, need a healer");
        require(!matches.empty(), "Glossary not loaded");
        require(glossary.local("DPS", glossary.matches("DPS")).has_value(),
                "Exact shorthand did not use local glossary");
        auto hot = glossary.matches("LFM HOT need DPS");
        require(
            std::any_of(hot.begin(), hot.end(),
                        [](auto &t) { return t.translation.find("Hall of Thanes") != std::string::npos; }),
            "HoT dungeon context lost");
        auto settings = default_settings();
        for (auto name : {"glossary_zhCN.json", "outbound_terms.json"}) {
            require(bundled_json(name) == read_json(std::filesystem::path(DATA_RESOURCE_DIR) / name),
                    "Embedded dictionary differs from the source resource");
        }
        auto notices = bundled_notices();
        require(notices.find("Apache License") != std::string::npos, "OpenSSL notice missing");
        require(notices.find("Niels Lohmann") != std::string::npos, "JSON notice missing");
        require(notices.find("Copyright (c) 2026 Pirson") != std::string::npos,
                "WoWTranslator notice missing");
        rejects([&] { bundled_text("unknown.json"); }, "Unknown resource accepted");
        auto prompt_resource =
            read_json(std::filesystem::path(DATA_RESOURCE_DIR).parent_path() / "prompts.json");
        auto fallback_settings = settings;
        fallback_settings.erase("outbound_default_template");
        fallback_settings.erase("outbound_style");
        fallback_settings["outbound_template"] = prompt_resource.at("OUTBOUND_TEMPLATE_STANDARD");
        fallback_settings["incoming_prompt"] = "my independent incoming prompt";
        require(normalize_outbound_prompt(fallback_settings), "Fallback built-in output prompt was not upgraded");
        require(fallback_settings["outbound_template"] == settings["outbound_template"],
                "Wrong output default after upgrade");
        require(fallback_settings["incoming_prompt"] == "my independent incoming prompt",
                "Output upgrade modified incoming prompt");
        require(fallback_settings["outbound_default_template"] == prompt_resource["OUTBOUND_TEMPLATE_STANDARD"],
                "Original formal default was not retained");
        require(!normalize_outbound_prompt(fallback_settings), "Prompt upgrade is not idempotent");
        fallback_settings["outbound_template"] = "my custom {language} prompt";
        auto custom = fallback_settings;
        require(!normalize_outbound_prompt(fallback_settings) && fallback_settings == custom,
                "Custom prompt was overwritten");
        Json old_custom = {{"outbound_template", "my old custom {language} prompt"}};
        require(normalize_outbound_prompt(old_custom) &&
                    old_custom["outbound_default_template"] == "my old custom {language} prompt" &&
                    old_custom["outbound_template"] == settings["outbound_template"],
                "Old custom prompt was not preserved separately from short style");
        Json missing_prompt = Json::object();
        require(!normalize_outbound_prompt(missing_prompt) && missing_prompt.empty(),
                "Missing output prompt modified");
        require(settings["incoming_prompt"] == prompt_resource["PROMPT"],
                "Incoming prompt changed with outgoing style");
        require(settings["outbound_style"] == "short", "Short style must be initially selected");
        require(outbound_prompt(settings, "short", "English") !=
                    outbound_prompt(settings, "default", "English"),
                "Output styles must use separate prompts");
        auto custom_styles = settings;
        custom_styles["outbound_template"] = "short {language}";
        custom_styles["outbound_default_template"] = "formal {language}";
        require(outbound_prompt(custom_styles, "short", "English") == "short English",
                "Short prompt selection failed");
        require(outbound_prompt(custom_styles, "default", "English") == "formal English",
                "Formal prompt selection failed");
        require(outbound_prompt(custom_styles, "short", "Korean") == "short Korean",
                "Target language substitution failed");
        custom_styles["outbound_style"] = "default";
        require(outbound_prompt(custom_styles, "short", "English") == "short English",
                "Stored style replaced captured submission style");
        require(!normalize_outbound_prompt(custom_styles) &&
                    custom_styles["outbound_template"] == "short {language}" &&
                    custom_styles["outbound_default_template"] == "formal {language}",
                "Configured style prompts overwritten");
        rejects([&] { outbound_prompt(settings, "unknown", "English"); }, "Unknown output style accepted");
        auto output_terms = read_json(std::filesystem::path(DATA_RESOURCE_DIR) / "outbound_terms.json");
        require(local_outbound_translation(settings, "缺治疗", "English", output_terms) == "lf healer",
                "Casual local output missing");
        require(local_outbound_translation(settings, " 缺治疗！ ", "English", output_terms) == "lf healer",
                "Local output punctuation normalization broken");
        require(local_outbound_translation(settings, "还差一个治疗", "English", output_terms) ==
                    "lf1m healer",
                "Local output lost explicit quantity");
        require(!local_outbound_translation(settings, "缺治疗吗？", "English", output_terms),
                "Question incorrectly treated as recruiting statement");
        require(!local_outbound_translation(settings, "缺治疗", "Korean", output_terms),
                "English shortcut leaked into another language");
        auto output_settings = settings;
        output_settings["outbound_style"] = "default";
        require(local_outbound_translation(output_settings, "缺治疗", "English", output_terms) == "LF healer",
                "Formal style must retain original local glossary output without AI");
        output_settings["outbound_style"] = "short";
        require(local_outbound_translation(output_settings, "缺治疗", "English", output_terms) == "lf healer",
                "Switching back to short style lost local translation");
        output_settings["outbound_template"] = "Use formal {language}, including punctuation.";
        require(local_outbound_translation(output_settings, "缺治疗", "English", output_terms) == "lf healer",
                "Custom prompt must not send exact glossary hits to AI");
        output_settings = settings;
        output_settings["outbound_terms_prefix"] = "custom instructions";
        require(local_outbound_translation(output_settings, "缺治疗", "English", output_terms) == "lf healer",
                "Custom term prefix must not send exact glossary hits to AI");
        output_settings = settings;
        output_settings["incoming_prompt"] = "unrelated incoming instructions";
        require(local_outbound_translation(output_settings, "缺治疗", "English", output_terms) == "lf healer",
                "Incoming instructions affected outgoing translation");
        output_settings["terms_enabled"] = false;
        require(!local_outbound_translation(output_settings, "缺治疗", "English", output_terms),
                "Disabled local glossary still used");
        require(!settings.at("skip_target_language_nicknames").get<bool>(), "Nickname filter must remain opt-in");
        for (auto author : {"中文玩家", "繁體玩家", "Alice猫", "\U00020000"}) {
            Message chinese;
            chinese.author = author;
            require(nickname_uses_target_script(chinese, "zh-Hans"), "Chinese nickname not detected");
        }
        Message named;
        named.author = "Alice-中文服务器";
        named.realm = "中文服务器";
        require(!nickname_uses_target_script(named, "zh-Hans"), "Chinese realm incorrectly classified as nickname");
        named.player_name = "小貓";
        require(nickname_uses_target_script(named, "zh-Hant"), "API player name fallback not checked");
        named.author.clear();
        require(nickname_uses_target_script(named, "zh-Hans"), "Empty author lost API player name fallback");
        named.player_name = "Alice-中文服务器";
        require(!nickname_uses_target_script(named, "zh-Hans"), "Chinese realm in API name incorrectly filtered");
        named.player_name.clear();
        require(!nickname_uses_target_script(named, "zh-Hans"), "Empty nickname incorrectly filtered");
        for (auto [name, language] : {std::pair{"Alice-Westfall", "en-US"}, {"Élodie", "fr"},
                                      {"Jäger", "de"}, {"Наталья", "ru"}, {"Олена", "uk"},
                                      {"Αθηνά", "el"}, {"مرحبا", "ar"}, {"ねこ", "ja"}, {"고양이", "ko"}}) {
            Message player;
            player.author = name;
            require(nickname_uses_target_script(player, language), "Target nickname script not recognized");
            auto filter = settings;
            filter["target_language"] = language;
            require(!skip_nickname_translation(filter, player), "Nickname filtering must remain optional");
            filter["skip_target_language_nicknames"] = true;
            require(skip_nickname_translation(filter, player), "Configured target language nickname not skipped");
            player.event = "OCR_CHAT";
            require(!skip_nickname_translation(filter, player), "Unverified OCR names must not skip translation");
        }
        named.author = "Alice-中文服务器";
        require(nickname_uses_target_script(named, "fr") && !nickname_uses_target_script(named, "ja"),
                "Shared Latin script heuristic or realm exclusion differs");
        named.author = "小猫Alice";
        require(!nickname_uses_target_script(named, "en-US"), "Mixed Chinese names should not become English names");
        named.author = "ねこ猫";
        require(!nickname_uses_target_script(named, "zh-Hans"), "Known Japanese kana misclassified as Chinese nickname");
        named.author = "1234_🙂";
        require(!nickname_uses_target_script(named, "en-US"), "Punctuation or numbers classified as a language");
        Json old_filter{{"skip_chinese_nicknames", true}};
        require(normalize_nickname_filter(old_filter) && old_filter["skip_target_language_nicknames"] == true &&
                    !old_filter.contains("skip_chinese_nicknames") && !normalize_nickname_filter(old_filter),
                "Nickname preference normalization must preserve the switch and be idempotent");
        old_filter = {{"skip_chinese_nicknames", true}, {"skip_target_language_nicknames", false}};
        normalize_nickname_filter(old_filter);
        require(old_filter["skip_target_language_nicknames"] == false, "Fallback switch overrides a newer choice");
        Message m;
        m.event = "CHAT_MSG_PARTY";
        require(allows(settings, m), "Party rejected");
        m.event = "CHAT_MSG_CHANNEL";
        m.channel_number = 2;
        require(allows(settings, m), "Fresh settings must enable public channels");
        settings["groups"] = {"party"};
        require(!allows(settings, m), "Unselected channel accepted");
        settings["groups"].push_back("public");
        settings["channels"] = {2};
        require(allows(settings, m), "Selected public channel rejected");
        m.channel_number = 1;
        require(!allows(settings, m), "Wrong channel accepted");
        settings["auto"] = false;
        m.event = "BRIDGE_TEST";
        require(!allows(settings, m), "Pause ignored");
        QuickReply reply;
        auto now = Clock::now();
        require(!reply.receive("stale result"), "Unsolicited output must not replace clipboard");
        require(reply.begin() && reply.busy(), "Quick reply did not begin");
        require(!reply.begin(), "Repeated Enter must not enqueue another translation");
        require(!reply.tick(now + std::chrono::hours(1)) && reply.busy(),
                "Waiting output timed out silently");
        require(reply.receive("LF healer"), "Output result rejected");
        require(reply.caption() == "Translating…" && reply.busy(), "Uncopied result claimed success");
        reply.copied(now);
        require(!reply.busy() && reply.caption() == "Translated", "Clipboard success not confirmed");
        require(!reply.tick(now + std::chrono::milliseconds(649)), "Completion bubble ended too early");
        require(reply.tick(now + std::chrono::milliseconds(650)) && reply.caption() == "Copied to clipboard",
                "Copied feedback sequence missing");
        require(!reply.tick(now + std::chrono::milliseconds(2149)), "Copied feedback ended too early");
        require(reply.tick(now + std::chrono::milliseconds(2150)) && reply.phase == QuickReply::Hidden,
                "Copied feedback did not hide");
        require(reply.begin() && reply.receive("test"), "Second request did not begin");
        reply.fail("clipboard busy", now);
        require(!reply.busy() && reply.caption() == "clipboard busy", "Clipboard failure reported success");
        require(reply.begin(), "Failure blocked retry");
        require(!reply.tick(now + std::chrono::seconds(3)) && reply.busy(),
                "Old feedback timer hid new request");
        reply.fail("cancelled", now);
        require(!reply.receive("obsolete translation"), "Cancelled translation replaced clipboard");
        require(reply.tick(now + std::chrono::milliseconds(2500)) && reply.phase == QuickReply::Hidden,
                "Error feedback did not hide");
        require(output_languages[0].first == "English" && output_languages[0].second == "English",
                "Output language display name leaked into API language");
        Message reply_message;
        reply_message.event = "CHAT_MSG_WHISPER";
        reply_message.author = "Friend";
        reply_message.realm = "AnotherRealm";
        auto destination = reply_target(reply_message);
        require(destination && destination->prefix == "/w Friend-AnotherRealm ",
                "Cross-realm whisper lost its recipient");
        require(compose_reply(*destination, "  Hello\nthere  ") == "/w Friend-AnotherRealm Hello there",
                "Reply must contain exactly one chat line");
        reply_message.author = "/g bad";
        require(!reply_target(reply_message), "Unsafe whisper recipient became a command");
        reply_message.author.clear();
        require(!reply_target(reply_message), "Realm alone became a whisper recipient");
        reply_message.author = std::string("Friend\0Other", 12);
        require(!reply_target(reply_message), "Control byte entered a whisper command");
        reply_message = {};
        reply_message.event = "CHAT_MSG_CHANNEL";
        reply_message.channel_number = 3;
        require(reply_target(reply_message)->prefix == "/3 ", "Public channel command incorrect");
        reply_message.channel_number = 0;
        require(!reply_target(reply_message), "Unknown public channel was guessed");
        reply_message.event = "CHAT_MSG_BN_WHISPER";
        require(!reply_target(reply_message), "Battle.net whisper target was guessed");
        reply_message.event = "CHAT_MSG_PARTY_LEADER";
        require(reply_target(reply_message)->prefix == "/p ", "Party reply command incorrect");
        for (auto [event, prefix] : std::array<std::pair<std::string_view, std::string_view>, 7>{
                 {{"CHAT_MSG_RAID_WARNING", "/ra "},
                  {"CHAT_MSG_INSTANCE_CHAT", "/i "},
                  {"CHAT_MSG_GUILD", "/g "},
                  {"CHAT_MSG_OFFICER", "/o "},
                  {"CHAT_MSG_SAY", "/s "},
                  {"CHAT_MSG_YELL", "/y "},
                  {"CHAT_MSG_COMMUNITIES_CHANNEL", ""}}}) {
            reply_message.event = event;
            auto mapped = reply_target(reply_message);
            require(prefix.empty() ? !mapped : mapped && mapped->prefix == prefix,
                    "Chat event reply destination incorrect");
        }
        require(reply_language("こんにちは") == "Japanese" && reply_language("안녕하세요") == "Korean",
                "Clear script language was not suggested");
        require(reply_language("bonjour") == "", "Latin language was guessed");
        require(normalized_ui_language("fr-FR") == "fr" &&
                    normalized_ui_language("de-DE") == "de" &&
                    normalized_ui_language("es-ES") == "es" &&
                    normalized_ui_language("zh-CN") == "zh" &&
                    normalized_ui_language("ja-JP") == "ja" && normalized_ui_language("ko-KR") == "ko" &&
                    normalized_ui_language("pt-PT") == "pt-BR" && normalized_ui_language("ru-RU") == "ru",
                "UI language normalization failed");
        const auto catalog = bundled_json("ui-locales.json");
        for (auto entry = catalog.begin(); entry != catalog.end(); ++entry) {
            for (const auto &language : ui_languages) {
                if (language.code == "zh") continue;
                require(entry.value().contains(language.code) && entry.value().at(language.code).is_string() &&
                            !entry.value().at(language.code).get_ref<const std::string &>().empty(),
                        "Selectable interface language has missing catalog entries: " + std::string(language.code));
                const auto translated = ui_text_for_language(entry.key(), language.code);
                if (entry.value().at("en").get<std::string>().find("{language}") != std::string::npos)
                    require(translated.find("{language}") != std::string::npos,
                            "Localized prompt lost its target-language placeholder");
            }
        }
        require(ui_text_for_language("Save", "not-supported") == ui_text_for_language("Save", "en") &&
                    ui_text_for_language("player-supplied text", "ja") == "player-supplied text",
                "English fallback or verbatim unknown captions failed");
        require(local_translation_pair_for_language("fr", true, "Hello", "") == "en-fr" &&
                    local_translation_pair_for_language("de", true, "Hello", "") == "en-de" &&
                    local_translation_pair_for_language("es", true, "Hello", "") == "en-es" &&
                    local_translation_pair_for_language("zh", true, "Hello", "") == "en-zh-Hans" &&
                    local_translation_pair_for_language("en", true, "你好", "") == "zh-Hans-en" &&
                    local_translation_pair_for_language("fr", false, "Bonjour", "English") == "fr-en" &&
                    local_translation_pair_for_language("en", false, "Hello", "Spanish") == "en-es",
                "Local model routing failed");
        require(local_translation_pair_for_language("fr", false, "Bonjour", "German") == "fr-de",
                "Requested pivot direction was lost before catalogue planning");
        require(supported_target_language("ja") && supported_target_language("zh-Hant") &&
                    !supported_target_language("xx") && target_language_name("pt-PT") == "Portuguese (Portugal)",
                "Chat target catalogue invalid");
        require(local_translation_pair(true, "Hello", "", "fr") == "en-fr" &&
                    local_translation_pair(false, "Bonjour", "English", "fr") == "fr-en" &&
                    local_translation_pair(true, "你好", "", "en-GB") == "zh-Hans-en" &&
                    local_translation_pair(false, "Hello", "Spanish", "en-US") == "en-es",
                "Chat language must route independently of UI language");
        require(!should_translate_for_target("Hello, friend", "en-US") &&
                    should_translate_for_target("你好", "en-US") &&
                    should_translate_for_target("Hello, friend", "fr"),
                "Chat target filtering failed");
        require(local_translation_pair(true, "Hello", "", "ja") == "en-ja", "Japanese target did not reach catalogue routing");
        auto actual_device = platform::hardware_id();
        require(actual_device.size() == 64 &&
                    std::all_of(actual_device.begin(), actual_device.end(),
                                [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }),
                "Device identity invalid");
#ifdef _WIN32
        Bytes secret{1, 2, 3, 4, 5};
        require(platform::protect(platform::protect(secret), true) == secret, "DPAPI roundtrip failed");
#endif
        auto started = Clock::now();
        auto image = read_bytes(root / bridge[0]["images"][0].get<std::string>());
        Image benchmark{192, 24, 768, std::move(image)};
        for (int i = 0; i < 10000; i++)
            require(decode_image(benchmark, {0, 0, 2}).has_value(), "Decode benchmark failed");
        auto ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        std::cout << "Passed " << checks << " assertions; 10000 packet decodes in " << ms << " ms\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
