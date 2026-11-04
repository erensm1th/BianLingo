#include "bianlingo/chat_preview.hpp"
#include "bianlingo/bundle.hpp"
#include <iterator>

namespace bl {
std::deque<DisplayMessage> chat_preview_messages(uint32_t now) {
    static const auto examples = bundled_json("chat-preview.json");
    std::deque<DisplayMessage> result;
    for (const auto &example : examples) {
        DisplayMessage display;
        auto &m = display.message;
        m.session = 0xffffffffu;
        m.seq = uint32_t(result.size() + 1);
        auto age = uint32_t(examples.size() - result.size() - 1);
        m.sent = now >= age ? now - age : 0;
        m.event = example.at("event").get<std::string>();
        m.author = example.at("name").get<std::string>() + "-PreviewRealm";
        m.player_name = example.at("name").get<std::string>();
        m.realm = "PreviewRealm";
        m.player_class = example.at("player_class").get<std::string>();
        m.race = example.at("race").get<std::string>();
        m.english_race = example.at("english_race").get<std::string>();
        m.sex = m.seq % 2 ? 2 : 3;
        m.text = example.at("original").get<std::string>();
        display.translation = example.at("translated").get<std::string>();
        display.source = "Local test preview";
        if (m.event == "CHAT_MSG_CHANNEL") {
            m.channel_number = 2;
            m.channel_name = "Trade";
        }
        if (m.seq == examples.size())
            m.replay_batch = 1;
        result.push_back(std::move(display));
    }
    return result;
}
} // namespace bl
