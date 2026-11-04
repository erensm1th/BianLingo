#pragma once
#include "bianlingo/core.hpp"

namespace bl {
struct SkinPalette {
    uint32_t background, surface, text, accent;
    bool operator==(const SkinPalette &) const = default;
};
uint32_t parse_skin_color(std::string_view);
std::string skin_color_hex(uint32_t);
SkinPalette skin_preset(std::string_view);
std::vector<std::pair<std::string, std::string>> skin_options(std::string_view style);
void change_ui_style(Json &, std::string_view style);
SkinPalette skin_palette(const Json &);
Json skin_colors(const SkinPalette &);
void validate_skin(const Json &);
void normalize_player_display(Json &);
std::string player_display_name(const Message &);
std::string player_badges(const Message &, const Json &);
} // namespace bl
