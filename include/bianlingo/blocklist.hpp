#pragma once
#include "bianlingo/core.hpp"

namespace bl {
inline constexpr size_t max_block_records = 512;
std::string block_name_key(std::string_view name);
std::string block_player_name(const Message &message);
class PlayerBlocklist {
    struct Record {
        std::string name, origin;
        uint64_t clock;
        bool blocked;
    };
    std::map<std::string, Record> records_;
    std::string origin_;
    uint64_t clock_ = 0;
    bool has_active_ = false;
  public:
    explicit PlayerBlocklist(const Json &state = Json::object());
    Json snapshot(bool storage = false) const;
    bool merge(const Json &snapshot);
    bool set(std::string name, bool blocked);
    bool blocks(const Message &message) const;
    std::vector<std::string> names() const;
};
inline constexpr size_t max_block_import_bytes = 262144;
// Data only: player-controlled paste into the addon, never evaluated as Lua.
std::string blocklist_import_code(const PlayerBlocklist &list);
} // namespace bl
