#pragma once
#include "bianlingo/skin.hpp"

namespace bl {
enum class ToggleVisual { Switch, Ascii, Checkbox };
enum class SliderVisual { Round, Square, Diamond };
enum class FrameVisual { Flat, Beveled };
struct ThemeMetrics {
    int card_radius = 10, control_radius = 7, button_radius = 7, border_width = 1;
    int slider_padding = 12, track_thickness = 6, thumb_size = 22, toggle_width = 36, toggle_height = 20,
        popup_row_height = 34;
    bool operator==(const ThemeMetrics &) const = default;
};
struct ThemeFonts {
    bool monospace = false, serif = false;
    int size = 13;
    bool operator==(const ThemeFonts &) const = default;
};
struct ThemeWidgets {
    ToggleVisual toggle = ToggleVisual::Switch;
    SliderVisual slider = SliderVisual::Round;
    FrameVisual frame = FrameVisual::Flat;
    bool plain_navigation = false, chevron_mark = false, invert_selection = false, outline_cards = false;
    bool operator==(const ThemeWidgets &) const = default;
};
struct ThemeColors {
    uint32_t muted{}, border{}, focus{}, hover{}, selection{}, selected_text{}, disabled{}, track{}, thumb{},
        toggle_off{}, pressed{}, button{}, on_accent{};
    bool operator==(const ThemeColors &) const = default;
};
enum class ChatTabVisual { Pills, Classic };
struct ThemeChat {
    ChatTabVisual tabs = ChatTabVisual::Pills;
    bool flash_unread = false, unread_count = true;
    uint32_t tab_text = 0xffd100, tab_fill = 0x17120d, tab_border = 0x6b5538, glow = 0xff8040;
    int fade_in_ms = 1000, fade_out_ms = 1000;
    double default_opacity = -1; // -1 keeps the user's existing opacity.
    bool operator==(const ThemeChat &) const = default;
};
double chat_flash_alpha(const ThemeChat &, uint64_t elapsed_ms);
struct ResolvedTheme {
    std::string id, name, author, description;
    SkinPalette palette{};
    ThemeMetrics metrics;
    ThemeFonts fonts;
    ThemeWidgets widgets;
    ThemeColors colors;
    ThemeChat chat;
    bool operator==(const ResolvedTheme &) const = default;
};
// Pixel geometry is shared by hit testing and painting, including DPI and thumb clearance.
struct SliderGeometry {
    int start{}, end{}, center_y{}, track_half{}, thumb_half{};
    int pixel(int value, int minimum, int maximum) const;
    int value(int x, int minimum, int maximum) const;
};
SliderGeometry slider_geometry(const ThemeMetrics &, int width, int height, int dpi);

class ThemeRegistry {
  public:
    explicit ThemeRegistry(std::filesystem::path directory = {});
    void reload();
    bool is_builtin(std::string_view id) const;
    const std::vector<std::string> &warnings() const {
        return warnings_;
    }
    std::vector<std::pair<std::string, std::string>> styles() const;
    std::vector<std::pair<std::string, std::string>> palettes(std::string_view id) const;
    ResolvedTheme resolve(const Json &) const;
    ResolvedTheme fallback(const Json &, std::string *warning = nullptr) const;
    void change_style(Json &, std::string_view id) const;
    Json export_theme(const Json &, std::string id, std::string name) const;
    std::string import_file(const std::filesystem::path &);
    const std::filesystem::path &directory() const {
        return directory_;
    }
    SkinPalette preset(std::string_view palette) const;

  private:
    struct Entry {
        Json definition;
        ResolvedTheme theme;
        Json semantic;
        std::vector<std::pair<std::string, Json>> palettes;
    };
    std::vector<Entry> entries_;
    std::filesystem::path directory_;
    std::vector<std::string> warnings_;
    Entry parse(const Json &, bool builtin) const;
    const Entry &entry(std::string_view) const;
};
const ThemeRegistry &builtin_themes();
bool normalize_skin_settings(Json &); // Repairs only appearance fields, never provider/filter/hotkey data.
} // namespace bl
