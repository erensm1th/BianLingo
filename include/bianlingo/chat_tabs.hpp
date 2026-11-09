#pragma once
#include "bianlingo/core.hpp"
#include <set>

namespace bl {
inline constexpr size_t max_chat_tabs = 16;
struct ChatTab {
    std::string id, name;
    std::set<std::string> events;
    std::set<int> channels, excluded_channels;
    std::set<std::string> channel_names, excluded_channel_names;
    bool matches(const Message &) const;
};
const std::vector<std::pair<std::string, std::string>> &chat_event_options();
Json new_chat_tab(std::string id, std::string name);
std::vector<ChatTab> parse_chat_tabs(const Json &);
bool normalize_chat_tabs(Json &settings);
// Bounded, strict snapshot from the Lua bridge. No chat bodies or account data.
Json parse_chat_layout(std::string_view);
// Import replaces the complete tab draft, preserving the source order and filters.
Json import_chat_layout(const Json &layout);
// Validate and persist only the layout delta. Returns after persistence succeeds; unchanged is a no-op.
Json synchronize_chat_layout(const Json &settings, const Json &layout,
                             const std::function<void(const Json &)> &persist);
Json parse_channel_filter(std::string_view); // Number or name, separated by comma/semicolon/space.
std::string format_channel_filter(const Json &);
std::string chat_scope_warning(const Json &, const std::vector<ChatTab> &);
struct TabHistoryChange {
    bool retained;
    // Old row index -> new row index; history_index_removed marks an evicted row.
    std::vector<size_t> old_to_new;
};
inline constexpr size_t history_index_removed = size_t(-1);
// Keep the latest limit matches for each tab in one deduplicated, ordered history.
TabHistoryChange update_tab_history(std::deque<DisplayMessage> &, DisplayMessage, size_t limit,
                                    const std::vector<ChatTab> &);
void trim_tab_history(std::deque<DisplayMessage> &, size_t limit, const std::vector<ChatTab> &);
} // namespace bl
