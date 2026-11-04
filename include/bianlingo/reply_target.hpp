#pragma once
#include "bianlingo/core.hpp"
#include <algorithm>
#include <optional>

namespace bl {
struct ReplyTarget {
    std::string prefix;
    std::string label;
};

// Only character chat has a stable /w name; BNet/community identities differ.
inline std::optional<ReplyTarget> whisper_target(const Message &m) {
    if (m.event.rfind("CHAT_MSG_", 0) != 0)
        return {};
    if (m.event.rfind("CHAT_MSG_BN_", 0) == 0 || m.event == "CHAT_MSG_COMMUNITIES_CHANNEL")
        return {};
    std::string name = m.author.empty() ? m.player_name : m.author;
    if (name.empty())
        return {};
    if (name.find('-') == std::string::npos && !m.realm.empty())
        name += "-" + m.realm;
    if (name.find_first_of(" /\\|;") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
        return {};
    if (m.same_realm) {
        auto suffix = name.find('-');
        if (suffix != name.npos)
            name.resize(suffix);
        if (name.empty())
            return {};
    }
    return ReplyTarget{"/w " + name + " ", "Whisper " + name};
}
inline std::optional<ReplyTarget> reply_target(const Message &m) {
    const auto &e = m.event;
    if (e == "CHAT_MSG_WHISPER" || e == "CHAT_MSG_WHISPER_INFORM")
        return whisper_target(m);
    if (e == "CHAT_MSG_PARTY" || e == "CHAT_MSG_PARTY_LEADER")
        return ReplyTarget{"/p ", "Party"};
    if (e == "CHAT_MSG_RAID" || e == "CHAT_MSG_RAID_LEADER" || e == "CHAT_MSG_RAID_WARNING")
        return ReplyTarget{"/ra ", "Raid"};
    if (e == "CHAT_MSG_INSTANCE_CHAT" || e == "CHAT_MSG_INSTANCE_CHAT_LEADER")
        return ReplyTarget{"/i ", "Instance"};
    if (e == "CHAT_MSG_GUILD")
        return ReplyTarget{"/g ", "Guild"};
    if (e == "CHAT_MSG_OFFICER")
        return ReplyTarget{"/o ", "Officer"};
    if (e == "CHAT_MSG_SAY")
        return ReplyTarget{"/s ", "Say"};
    if (e == "CHAT_MSG_YELL")
        return ReplyTarget{"/y ", "Yell"};
    if (e == "CHAT_MSG_CHANNEL" && m.channel_number > 0 && m.channel_number <= 99)
        return ReplyTarget{"/" + std::to_string(m.channel_number) + " ",
                           m.channel_name.empty() ? "Channel " + std::to_string(m.channel_number)
                                                  : m.channel_name};
    return std::nullopt;
}

inline std::string reply_language(std::string_view text) {
    auto script = script_language(text);
    if (script == "Korean")
        return "Korean";
    if (script == "Japanese")
        return "Japanese";
    return {};
}

inline std::string compose_reply(const ReplyTarget &target, std::string_view translation) {
    std::string line(translation);
    for (char &c : line)
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
    return target.prefix + trim(std::move(line));
}
} // namespace bl
