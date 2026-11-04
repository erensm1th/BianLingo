#include "bianlingo/theme.hpp"
#include "bianlingo/bundle.hpp"
#include "bianlingo/chat_style.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace bl {
namespace {
constexpr size_t max_file = 65536;
void keys(const Json &j, std::initializer_list<const char *> allowed) {
    if (!j.is_object())
        throw std::runtime_error("Theme fields must be objects");
    for (auto it = j.begin(); it != j.end(); ++it)
        if (std::none_of(allowed.begin(), allowed.end(), [&](auto k) { return it.key() == k; }))
            throw std::runtime_error("Unsupported theme field: " + it.key());
}
std::string string_value(const Json &j, const char *key, size_t limit, std::string fallback = {}) {
    if (!j.contains(key))
        return fallback;
    if (!j[key].is_string())
        throw std::runtime_error(std::string("Invalid theme text format: ") + key);
    auto value = j[key].get<std::string>();
    if (value.size() > limit * 4 || utf32(value).size() > limit ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error(std::string("Invalid theme text length or content: ") + key);
    return value;
}
bool valid_id(std::string_view id) {
    if (id.empty() || id.size() > 64 || id.front() < 'a' || id.front() > 'z' || id.back() == '.' ||
        id.find("..") != id.npos)
        return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
}
int number(const Json &j, const char *key, int fallback, int lo, int hi) {
    if (!j.contains(key))
        return fallback;
    if (!j[key].is_number_integer())
        throw std::runtime_error(std::string("Theme dimensions must be integers: ") + key);
    auto n = j[key].get<int64_t>();
    if (n < lo || n > hi)
        throw std::runtime_error(std::string("Theme dimension is out of range: ") + key);
    return int(n);
}
bool boolean(const Json &j, const char *key, bool fallback) {
    if (!j.contains(key))
        return fallback;
    if (!j[key].is_boolean())
        throw std::runtime_error("Invalid theme boolean value");
    return j[key].get<bool>();
}
uint32_t mix_color(uint32_t a, uint32_t b) {
    return (((((a >> 16) & 255) + ((b >> 16) & 255)) / 2) << 16) |
           (((((a >> 8) & 255) + ((b >> 8) & 255)) / 2) << 8) | (((a & 255) + (b & 255)) / 2);
}
SkinPalette palette(const Json &j, SkinPalette p) {
    for (auto key : {"background", "surface", "text", "accent"})
        if (j.contains(key)) {
            auto c = parse_skin_color(j.at(key).get<std::string>());
            if (std::string_view(key) == "background")
                p.background = c;
            else if (std::string_view(key) == "surface")
                p.surface = c;
            else if (std::string_view(key) == "text")
                p.text = c;
            else
                p.accent = c;
        }
    return p;
}
ThemeColors colors(SkinPalette p, const ThemeWidgets &w, const Json &overrides) {
    auto luminance = ((p.accent >> 16) & 255) * 299 + ((p.accent >> 8) & 255) * 587 + (p.accent & 255) * 114;
    ThemeColors c{mix_color(p.text, p.background),
                  mix_color(p.background, p.text),
                  p.accent,
                  mix_color(p.surface, p.accent),
                  w.invert_selection ? p.accent : mix_color(p.surface, p.accent),
                  w.invert_selection ? p.background : p.text,
                  mix_color(p.text, p.surface),
                  mix_color(p.text, p.surface),
                  0xffffff,
                  mix_color(p.text, p.surface),
                  mix_color(p.accent, p.text),
                  mix_color(p.background, p.surface),
                  luminance > 160000 ? 0u : 0xffffffu};
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        auto n = parse_skin_color(it.value().get<std::string>());
        auto k = it.key();
        if (k == "muted")
            c.muted = n;
        else if (k == "border")
            c.border = n;
        else if (k == "focus")
            c.focus = n;
        else if (k == "hover")
            c.hover = n;
        else if (k == "selection")
            c.selection = n;
        else if (k == "selectedText")
            c.selected_text = n;
        else if (k == "disabled")
            c.disabled = n;
        else if (k == "track")
            c.track = n;
        else if (k == "thumb")
            c.thumb = n;
        else if (k == "toggleOff")
            c.toggle_off = n;
        else if (k == "pressed")
            c.pressed = n;
        else if (k == "button")
            c.button = n;
        else if (k == "onAccent")
            c.on_accent = n;
    }
    return c;
}
Json read_theme(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Could not read the theme file");
    std::string raw(max_file + 1, '\0');
    in.read(raw.data(), std::streamsize(raw.size()));
    raw.resize(size_t(in.gcount()));
    if (raw.size() > max_file)
        throw std::runtime_error("The theme file exceeds 64 KiB");
    std::vector<std::set<std::string>> objects;
    return Json::parse(raw, [&](int depth, Json::parse_event_t event, Json &value) {
        if (depth > 16)
            throw std::runtime_error("The theme is nested too deeply");
        if (event == Json::parse_event_t::object_start)
            objects.emplace_back();
        if (event == Json::parse_event_t::key && !objects.back().insert(value.get<std::string>()).second)
            throw std::runtime_error("The theme contains duplicate fields");
        if (event == Json::parse_event_t::object_end)
            objects.pop_back();
        return true;
    });
}
} // namespace
int SliderGeometry::pixel(int value, int minimum, int maximum) const {
    return maximum <= minimum
               ? start
               : start + int(std::llround(double(std::clamp(value, minimum, maximum) - minimum) *
                                          (end - start) / (maximum - minimum)));
}
int SliderGeometry::value(int x, int minimum, int maximum) const {
    return end <= start || maximum <= minimum
               ? minimum
               : minimum + int(std::llround(double(std::clamp(x, start, end) - start) * (maximum - minimum) /
                                            (end - start)));
}
SliderGeometry slider_geometry(const ThemeMetrics &m, int width, int height, int dpi) {
    auto scale = [&](int n) { return int(std::lround(double(n) * dpi / 96)); };
    int thumb = std::max(2, scale(m.thumb_size) / 2), pad = std::max(scale(m.slider_padding), thumb + 1);
    pad = std::min(pad, std::max(0, (width - 2) / 2));
    return {pad, std::max(pad + 1, width - pad), height / 2, std::max(1, scale(m.track_thickness) / 2),
            thumb};
}
ThemeRegistry::ThemeRegistry(std::filesystem::path directory) : directory_(std::move(directory)) {
    reload();
}
const ThemeRegistry::Entry &ThemeRegistry::entry(std::string_view id) const {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto &e) { return e.theme.id == id; });
    if (it == entries_.end())
        throw std::runtime_error("Skin not found: " + std::string(id));
    return *it;
}
ThemeRegistry::Entry ThemeRegistry::parse(const Json &j, bool builtin) const {
    keys(j, {"schemaVersion", "id", "name", "author", "description", "base", "colors", "metrics", "fonts",
             "widgets", "palettes", "chat"});
    if (!j.contains("schemaVersion") || !j["schemaVersion"].is_number_integer() || j["schemaVersion"] != 1)
        throw std::runtime_error("Unsupported theme schema version");
    auto id = string_value(j, "id", 64), name = string_value(j, "name", 64);
    if (!valid_id(id) || name.empty() || (!builtin && id.find('.') == id.npos))
        throw std::runtime_error("Theme IDs must use author.name and the name cannot be empty");
    Entry out;
    out.definition = j;
    out.semantic = Json::object();
    auto base = string_value(j, "base", 64);
    if (!builtin) {
        const auto &parent = entry(base);
        if (!parent.definition.value("base", std::string()).empty())
            throw std::runtime_error("Themes can inherit only from built-in bases");
        out.theme = parent.theme;
        out.semantic = parent.semantic;
        if (j.contains("palettes"))
            throw std::runtime_error("Community themes cannot define built-in palette lists");
    }
    out.theme.id = id;
    out.theme.name = name;
    out.theme.author = string_value(j, "author", 64);
    out.theme.description = string_value(j, "description", 256);
    if (j.contains("colors")) {
        auto &v = j["colors"];
        keys(v, {"background", "surface", "text", "accent", "muted", "border", "focus", "hover", "selection",
                 "selectedText", "disabled", "track", "thumb", "toggleOff", "pressed", "button", "onAccent"});
        for (auto it = v.begin(); it != v.end(); ++it) {
            parse_skin_color(it.value().get<std::string>());
            if (it.key() != "background" && it.key() != "surface" && it.key() != "text" &&
                it.key() != "accent")
                out.semantic[it.key()] = it.value();
        }
        out.theme.palette = palette(v, out.theme.palette);
    }
    auto &m = out.theme.metrics;
    if (j.contains("metrics")) {
        auto &v = j["metrics"];
        keys(v, {"cardRadius", "controlRadius", "buttonRadius", "borderWidth", "sliderPadding",
                 "trackThickness", "thumbSize", "toggleWidth", "toggleHeight", "popupRowHeight"});
        m.card_radius = number(v, "cardRadius", m.card_radius, 0, 20);
        m.control_radius = number(v, "controlRadius", m.control_radius, 0, 14);
        m.button_radius = number(v, "buttonRadius", m.button_radius, 0, 14);
        m.border_width = number(v, "borderWidth", m.border_width, 0, 3);
        m.slider_padding = number(v, "sliderPadding", m.slider_padding, 0, 40);
        m.track_thickness = number(v, "trackThickness", m.track_thickness, 1, 8);
        m.thumb_size = number(v, "thumbSize", m.thumb_size, 8, 24);
        m.toggle_width = number(v, "toggleWidth", m.toggle_width, 24, 44);
        m.toggle_height = number(v, "toggleHeight", m.toggle_height, 14, 22);
        m.popup_row_height = number(v, "popupRowHeight", m.popup_row_height, 26, 44);
        if (m.track_thickness > m.thumb_size)
            throw std::runtime_error("The slider track cannot be thicker than the thumb");
    }
    if (j.contains("fonts")) {
        auto &v = j["fonts"];
        keys(v, {"latin", "cjk", "size"});
        auto font = string_value(v, "latin", 32,
                                 out.theme.fonts.monospace ? "system-mono"
                                 : out.theme.fonts.serif   ? "system-serif"
                                                           : "system-sans");
        if (font != "system-mono" && font != "system-sans" && font != "system-serif")
            throw std::runtime_error("The font family must be system-sans, system-mono or system-serif");
        if (string_value(v, "cjk", 32, "system") != "system")
            throw std::runtime_error("CJK fonts must use the system fallback");
        out.theme.fonts.monospace = font == "system-mono";
        out.theme.fonts.serif = font == "system-serif";
        out.theme.fonts.size = number(v, "size", out.theme.fonts.size, 11, 14);
    }
    if (j.contains("widgets")) {
        auto &v = j["widgets"];
        keys(v, {"toggle", "slider", "navigation", "menuMark", "invertSelection", "cardBorder", "frame"});
        auto &w = out.theme.widgets;
        if (v.contains("toggle")) {
            auto x = v["toggle"].get<std::string>();
            if (x != "switch" && x != "ascii" && x != "checkbox")
                throw std::runtime_error("Unknown toggle style");
            w.toggle = x == "ascii"      ? ToggleVisual::Ascii
                       : x == "checkbox" ? ToggleVisual::Checkbox
                                         : ToggleVisual::Switch;
        }
        if (v.contains("slider")) {
            auto x = v["slider"].get<std::string>();
            if (x != "round" && x != "square" && x != "diamond")
                throw std::runtime_error("Unknown slider style");
            w.slider = x == "round"     ? SliderVisual::Round
                       : x == "diamond" ? SliderVisual::Diamond
                                        : SliderVisual::Square;
        }
        auto frame = string_value(v, "frame", 16, w.frame == FrameVisual::Beveled ? "beveled" : "flat");
        if (frame != "flat" && frame != "beveled")
            throw std::runtime_error("Unknown frame style");
        w.frame = frame == "beveled" ? FrameVisual::Beveled : FrameVisual::Flat;
        auto nav = string_value(v, "navigation", 16, w.plain_navigation ? "plain" : "icons");
        if (nav != "plain" && nav != "icons")
            throw std::runtime_error("Unknown navigation style");
        w.plain_navigation = nav == "plain";
        auto mark = string_value(v, "menuMark", 16, w.chevron_mark ? "chevron" : "tick");
        if (mark != "chevron" && mark != "tick")
            throw std::runtime_error("Unknown menu marker");
        w.chevron_mark = mark == "chevron";
        w.invert_selection = boolean(v, "invertSelection", w.invert_selection);
        w.outline_cards = boolean(v, "cardBorder", w.outline_cards);
    }
    if (j.contains("chat")) {
        const auto &v = j.at("chat");
        keys(v, {"tabs", "flashUnread", "unreadCount", "tabText", "tabFill", "tabBorder", "glow", "fadeInMs",
                 "fadeOutMs", "defaultBackgroundOpacity"});
        auto &c = out.theme.chat;
        auto tabs = string_value(v, "tabs", 16, c.tabs == ChatTabVisual::Classic ? "classic" : "pills");
        if (tabs != "classic" && tabs != "pills")
            throw std::runtime_error("Unknown chat tab style");
        c.tabs = tabs == "classic" ? ChatTabVisual::Classic : ChatTabVisual::Pills;
        c.flash_unread = boolean(v, "flashUnread", c.flash_unread);
        c.unread_count = boolean(v, "unreadCount", c.unread_count);
        for (auto [key, target] : {std::pair{"tabText", &c.tab_text},
                                   {"tabFill", &c.tab_fill},
                                   {"tabBorder", &c.tab_border},
                                   {"glow", &c.glow}})
            if (v.contains(key))
                *target = parse_skin_color(v.at(key).get<std::string>());
        c.fade_in_ms = number(v, "fadeInMs", c.fade_in_ms, 100, 10000);
        c.fade_out_ms = number(v, "fadeOutMs", c.fade_out_ms, 100, 10000);
        if (v.contains("defaultBackgroundOpacity")) {
            auto &opacity = v.at("defaultBackgroundOpacity");
            if (opacity.is_null())
                c.default_opacity = -1;
            else {
                if (!opacity.is_number())
                    throw std::runtime_error("Invalid theme background opacity format");
                c.default_opacity = opacity.get<double>();
                if (!std::isfinite(c.default_opacity) || c.default_opacity < 0 || c.default_opacity > 1)
                    throw std::runtime_error("Theme background opacity is out of range");
            }
        }
    }
    out.theme.colors = colors(out.theme.palette, out.theme.widgets, out.semantic);
    if (builtin) {
        if (!j.contains("palettes") || !j["palettes"].is_array() || j["palettes"].empty() ||
            j["palettes"].size() > 16)
            throw std::runtime_error("The built-in theme has no palette list");
        std::set<std::string> ids;
        for (auto &p : j["palettes"]) {
            keys(p, {"id", "name", "colors"});
            auto pid = string_value(p, "id", 64);
            if (!valid_id(pid) || !ids.insert(pid).second || pid == "custom")
                throw std::runtime_error("Duplicate palette ID");
            string_value(p, "name", 64);
            keys(p.at("colors"), {"background", "surface", "text", "accent"});
            palette(p["colors"], out.theme.palette);
            out.palettes.emplace_back(pid, p);
        }
    } else
        out.palettes.emplace_back("base",
                                  Json{{"name", "Theme palette"}, {"colors", skin_colors(out.theme.palette)}});
    return out;
}
void ThemeRegistry::reload() {
    ThemeRegistry staged = *this;
    staged.entries_.clear();
    staged.warnings_.clear();
    for (auto &j : bundled_json("themes.json"))
        staged.entries_.push_back(staged.parse(j, true));
    try {
        if (!directory_.empty() && std::filesystem::exists(directory_)) {
            std::vector<std::filesystem::path> files;
            if (std::filesystem::is_symlink(directory_))
                staged.warnings_.push_back("The skin directory cannot be a symbolic link");
            else
                for (auto &d : std::filesystem::directory_iterator(
                         directory_, std::filesystem::directory_options::skip_permission_denied))
                    if (!d.is_symlink() && d.is_directory())
                        files.push_back(d.path() / "theme.json");
            std::sort(files.begin(), files.end());
            if (files.size() > 64) {
                files.resize(64);
                staged.warnings_.push_back("Only the first 64 community skins are loaded");
            }
            for (auto &path : files)
                try {
                    if (std::filesystem::is_symlink(path))
                        throw std::runtime_error("Linked theme files are not supported");
                    auto candidate = staged.parse(read_theme(path), false);
                    if (path.parent_path().filename().string() != candidate.theme.id)
                        throw std::runtime_error("The skin directory name must match its ID");
                    if (std::any_of(staged.entries_.begin(), staged.entries_.end(),
                                    [&](auto &e) { return e.theme.id == candidate.theme.id; }))
                        throw std::runtime_error("Duplicate theme ID");
                    staged.entries_.push_back(std::move(candidate));
                } catch (const std::exception &e) {
                    staged.warnings_.push_back(path.parent_path().filename().string() + "：" + e.what());
                }
        }
    } catch (const std::filesystem::filesystem_error &e) {
        staged.warnings_.push_back(std::string("Could not scan the skin directory: ") + e.what());
    }
    entries_.swap(staged.entries_);
    warnings_.swap(staged.warnings_);
}
double chat_flash_alpha(const ThemeChat &chat, uint64_t elapsed_ms) {
    if (!chat.flash_unread)
        return 0;
    auto phase = elapsed_ms % uint64_t(chat.fade_in_ms + chat.fade_out_ms);
    return phase < uint64_t(chat.fade_in_ms) ? double(phase) / chat.fade_in_ms
                                             : 1.0 - double(phase - chat.fade_in_ms) / chat.fade_out_ms;
}
bool ThemeRegistry::is_builtin(std::string_view id) const {
    return std::any_of(entries_.begin(), entries_.end(), [&](const Entry &e) {
        return e.theme.id == id && e.definition.value("base", std::string()).empty();
    });
}
std::vector<std::pair<std::string, std::string>> ThemeRegistry::styles() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto &e : entries_)
        out.emplace_back(e.theme.id, e.theme.name);
    return out;
}
std::vector<std::pair<std::string, std::string>> ThemeRegistry::palettes(std::string_view id) const {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto &[pid, p] : entry(id).palettes)
        out.emplace_back(pid, p.at("name").get<std::string>());
    out.emplace_back("custom", "Custom colors");
    return out;
}
ResolvedTheme ThemeRegistry::resolve(const Json &s) const {
    const auto &e = entry(s.value("ui_style", std::string("ios")));
    auto out = e.theme;
    auto pid = s.value("skin", e.palettes.front().first);
    if (pid == "custom") {
        const auto &c = s.at("skin_colors");
        for (auto key : {"background", "surface", "text", "accent"})
            parse_skin_color(c.at(key).get<std::string>());
        out.palette = palette(c, out.palette);
    } else {
        auto found =
            std::find_if(e.palettes.begin(), e.palettes.end(), [&](auto &p) { return p.first == pid; });
        if (found == e.palettes.end())
            throw std::runtime_error("The palette does not match the skin");
        out.palette = palette(found->second.at("colors"), out.palette);
    }
    out.colors = colors(out.palette, out.widgets, e.semantic);
    return out;
}
ResolvedTheme ThemeRegistry::fallback(const Json &s, std::string *warning) const {
    if (warning)
        warning->clear();
    try {
        return resolve(s);
    } catch (const std::exception &e) {
        if (warning)
            *warning = std::string("Could not load the skin; a built-in appearance was selected: ") + e.what();
        return resolve(Json{{"ui_style", "ios"}, {"skin", "graphite"}});
    }
}
void ThemeRegistry::change_style(Json &s, std::string_view id) const {
    const auto &e = entry(id);
    auto previous = fallback(s);
    auto memory = s.value("style_memory", Json::object());
    auto previous_skin = s.value("ui_style", std::string("ios")) == previous.id
                             ? s.value("skin", std::string("graphite"))
                             : entry(previous.id).palettes.front().first;
    memory[previous.id] = {{"skin", previous_skin},
                           {"colors", skin_colors(previous.palette)},
                           {"background_opacity", background_opacity(s)}};
    auto saved = memory.value(std::string(id), Json::object());
    auto name = saved.value("skin", e.palettes.front().first);
    auto options = palettes(id);
    if (std::none_of(options.begin(), options.end(), [&](auto &p) { return p.first == name; }))
        name = e.palettes.front().first;
    if (s.value("ui_style", std::string("ios")) != id) {
        if (saved.contains("background_opacity"))
            s["background_opacity"] = saved.at("background_opacity");
        else if (e.theme.chat.default_opacity >= 0)
            s["background_opacity"] = e.theme.chat.default_opacity;
        s["ui_style"] = id;
        s["skin"] = name;
        s["skin_colors"] = name == "custom"
                               ? saved.at("colors")
                               : skin_colors(resolve(Json{{"ui_style", id}, {"skin", name}}).palette);
    }
    s["style_memory"] = std::move(memory);
}
SkinPalette ThemeRegistry::preset(std::string_view id) const {
    for (auto &e : entries_)
        for (auto &[pid, p] : e.palettes)
            if (pid == id)
                return palette(p.at("colors"), e.theme.palette);
    throw std::runtime_error("Unknown palette");
}
Json ThemeRegistry::export_theme(const Json &s, std::string id, std::string name) const {
    auto t = resolve(s);
    const auto &e = entry(t.id);
    auto base = e.definition.value("base", std::string());
    if (base.empty())
        base = t.id;
    auto c = skin_colors(t.palette);
    for (auto [key, value] : {std::pair{"muted", t.colors.muted},
                              {"border", t.colors.border},
                              {"focus", t.colors.focus},
                              {"hover", t.colors.hover},
                              {"selection", t.colors.selection},
                              {"selectedText", t.colors.selected_text},
                              {"disabled", t.colors.disabled},
                              {"track", t.colors.track},
                              {"thumb", t.colors.thumb},
                              {"toggleOff", t.colors.toggle_off},
                              {"pressed", t.colors.pressed},
                              {"button", t.colors.button},
                              {"onAccent", t.colors.on_accent}})
        c[key] = skin_color_hex(value);
    auto &m = t.metrics;
    Json j = {{"schemaVersion", 1},
              {"id", id},
              {"name", name},
              {"author", t.author},
              {"description", t.description},
              {"base", base},
              {"colors", c},
              {"metrics",
               {{"cardRadius", m.card_radius},
                {"controlRadius", m.control_radius},
                {"buttonRadius", m.button_radius},
                {"borderWidth", m.border_width},
                {"sliderPadding", m.slider_padding},
                {"trackThickness", m.track_thickness},
                {"thumbSize", m.thumb_size},
                {"toggleWidth", m.toggle_width},
                {"toggleHeight", m.toggle_height},
                {"popupRowHeight", m.popup_row_height}}},
              {"fonts",
               {{"latin", t.fonts.monospace ? "system-mono"
                          : t.fonts.serif   ? "system-serif"
                                            : "system-sans"},
                {"cjk", "system"},
                {"size", t.fonts.size}}},
              {"widgets",
               {{"toggle", t.widgets.toggle == ToggleVisual::Ascii      ? "ascii"
                           : t.widgets.toggle == ToggleVisual::Checkbox ? "checkbox"
                                                                        : "switch"},
                {"slider", t.widgets.slider == SliderVisual::Round     ? "round"
                           : t.widgets.slider == SliderVisual::Diamond ? "diamond"
                                                                       : "square"},
                {"frame", t.widgets.frame == FrameVisual::Beveled ? "beveled" : "flat"},
                {"navigation", t.widgets.plain_navigation ? "plain" : "icons"},
                {"menuMark", t.widgets.chevron_mark ? "chevron" : "tick"},
                {"invertSelection", t.widgets.invert_selection},
                {"cardBorder", t.widgets.outline_cards}}}};
    const auto &chat = t.chat;
    j["chat"] = {
        {"tabs", chat.tabs == ChatTabVisual::Classic ? "classic" : "pills"},
        {"flashUnread", chat.flash_unread},
        {"unreadCount", chat.unread_count},
        {"tabText", skin_color_hex(chat.tab_text)},
        {"tabFill", skin_color_hex(chat.tab_fill)},
        {"tabBorder", skin_color_hex(chat.tab_border)},
        {"glow", skin_color_hex(chat.glow)},
        {"fadeInMs", chat.fade_in_ms},
        {"fadeOutMs", chat.fade_out_ms},
        {"defaultBackgroundOpacity", chat.default_opacity < 0 ? Json(nullptr) : Json(chat.default_opacity)}};
    parse(j, false);
    return j;
}
std::string ThemeRegistry::import_file(const std::filesystem::path &file) {
    auto candidate = parse(read_theme(file), false);
    if (directory_.empty())
        throw std::runtime_error("No community skin directory is configured");
    std::filesystem::create_directories(directory_);
    auto target = directory_ / candidate.theme.id;
    if (std::filesystem::is_symlink(directory_) || std::filesystem::is_symlink(target) ||
        std::filesystem::is_symlink(target / "theme.json"))
        throw std::runtime_error("The skin directory cannot be a symbolic link");
    if (std::filesystem::exists(target) &&
        std::filesystem::weakly_canonical(target) !=
            std::filesystem::weakly_canonical(directory_) / candidate.theme.id)
        throw std::runtime_error("The skin directory is out of bounds");
    auto next = entries_;
    auto found =
        std::find_if(next.begin(), next.end(), [&](auto &e) { return e.theme.id == candidate.theme.id; });
    if (found == next.end()) {
        if (std::count_if(next.begin(), next.end(), [](const auto &e) {
                return !e.definition.value("base", std::string()).empty();
            }) >= 64)
            throw std::runtime_error("The community skin limit has been reached");
        next.push_back(candidate);
    } else
        *found = candidate;
    std::filesystem::create_directories(target);
    write_json(target / "theme.json", candidate.definition);
    entries_.swap(next);
    return candidate.theme.id;
}
const ThemeRegistry &builtin_themes() {
    static const ThemeRegistry registry;
    return registry;
}
} // namespace bl
