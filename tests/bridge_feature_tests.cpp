#include "bianlingo/chat_style.hpp"
#include "bianlingo/chat_tabs.hpp"
#include "bianlingo/core.hpp"
#include "bianlingo/journal.hpp"
#include "bianlingo/reply_target.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iostream>
#include <random>
using namespace bl;
namespace {
int checks = 0;
void require(bool value, const char *reason) {
    ++checks;
    if (!value)
        throw std::runtime_error(reason);
}
Bytes encoded(Packet p) {
    Bytes raw{'B', 'L', 'G', '2'};
    auto put = [&](uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i)
            raw.push_back(uint8_t(v >> (8 * i)));
    };
    put(p.session, 4);
    put(p.seq, 4);
    put(p.sent, 4);
    put(p.part, 2);
    put(p.count, 2);
    put(uint32_t(p.payload.size() + (p.replay_batch ? 2 : 0)), 2);
    put(p.event | (p.replay_batch ? 128 : 0), 1);
    put(p.player, 1);
    Bytes body = p.payload;
    if (p.replay_batch) {
        body.insert(body.begin(), uint8_t(p.replay_batch));
        body.insert(body.begin(), uint8_t(p.replay_batch >> 8));
    }
    auto check = raw;
    check.insert(check.end(), body.begin(), body.end());
    put(adler(check), 4);
    raw.insert(raw.end(), body.begin(), body.end());
    return raw;
}
Packet chat(uint32_t seq = 5, uint16_t batch = 0) {
    Packet p{42, seq, 1792880000, 1, 1, 1, 3, {'P', 0, 'h', 'e', 'l', 'l', 'o'}};
    p.replay_batch = batch;
    return p;
}
Packet control(uint32_t seq, uint8_t event, uint16_t batch, int total) {
    auto p = chat(seq);
    p.event = event;
    p.player = 0;
    auto text = std::to_string(batch) + "\t" + std::to_string(total);
    p.payload = {0};
    p.payload.insert(p.payload.end(), text.begin(), text.end());
    return p;
}
Bytes marker_bytes(int corner, int cell, bool active, bool matrix = false) {
    uint16_t hint = uint16_t((corner << 14) | 1234);
    Bytes b{252, 177, uint8_t((matrix ? 48 + 8 : 16) + (active ? 4 : 0) + cell - 2), uint8_t(hint >> 8), uint8_t(hint), 0};
    uint8_t crc = 0;
    for (int i = 0; i < 5; ++i) {
        crc ^= b[i];
        for (int k = 0; k < 8; ++k)
            crc = uint8_t((crc << 1) ^ (crc & 128 ? 7 : 0));
    }
    b[5] = crc;
    return b;
}
Image marker_image(Bytes raw, int x = 0, int y = 0) {
    Image im{x + 4, y + 4, (x + 4) * 4, Bytes(size_t(x + 4) * (y + 4) * 4)};
    size_t bit = 0;
    for (int i = 0; i < 16; ++i)
        for (int k = 2; k >= 0; --k, ++bit)
            im.pixels[(size_t(y + i / 4) * im.width + x + i % 4) * 4 + k] =
                (raw[bit / 8] >> (7 - bit % 8)) & 1 ? 255 : 0;
    return im;
}
std::string all_logs(const std::filesystem::path &dir) {
    std::string text;
    for (const auto &e : std::filesystem::directory_iterator(dir)) {
        auto b = read_bytes(e.path());
        text.append(b.begin(), b.end());
    }
    return text;
}
int64_t local(int day, int hour, int minute, int second) {
    std::tm t{};
    t.tm_year = 126;
    t.tm_mon = 9;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    t.tm_isdst = -1;
    return std::mktime(&t);
}
void protocol() {
    Assembler a;
    auto p = chat();
    auto bytes = encoded(p);
    auto decoded = decode_packet(bytes);
    require(decoded && !decoded->replay_batch, "Plain BLG2 packet rejected");
    auto original = a.accept(*decoded);
    require(original && original->text == "hello" && original->sent == p.sent, "Plain message body changed");
    a.clear_partial();
    require(!a.accept(*decoded), "source window switch forgot completed identities");
    auto local = chat(25);
    local.player |= player_same_realm;
    auto local_raw = encoded(local);
    auto local_packet = decode_packet(local_raw);
    Assembler local_assembler;
    auto local_message = local_packet ? local_assembler.accept(*local_packet) : std::nullopt;
    require(local_message && local_message->same_realm && local_message->player_class == "HUNTER",
            "Same-realm bit changed the player class");
    local_raw[23] ^= player_same_realm;
    require(!decode_packet(local_raw), "Same-realm flag was not checksum protected");
    local.event = 22;
    require(!decode_packet(encoded(local)) && !local_assembler.accept(local),
            "Layout control accepted a chat-only same-realm flag");
    p.replay_batch = 17;
    auto replay = decode_packet(encoded(p));
    require(replay && replay->replay_batch == 17 && replay->payload == p.payload,
            "manual batch/body not decoded");
    require(!a.accept(*replay), "replay duplicated an already completed original");
    bytes = encoded(p);
    bytes[29] ^= 1;
    require(!decode_packet(bytes), "batch corruption escaped checksum");
    bytes = encoded(p);
    bytes[22] ^= 128;
    require(!decode_packet(bytes), "replay flag escaped checksum");
    p = chat(9, 1);
    p.count = 2;
    p.payload = {'P', 0, 'h', 'e'};
    auto other = p;
    other.replay_batch = 2;
    other.part = 2;
    other.payload = {'l', 'l', 'o'};
    require(!a.accept(p) && !a.accept(other), "cross-batch partials merged");
    auto final = p;
    final.part = 2;
    final.payload = other.payload;
    auto recovered = a.accept(final);
    require(recovered && recovered->text == "hello" && recovered->replay_batch == 1,
            "same-batch parts failed");
    require(!a.accept(other) && !a.accept(chat(9)), "cross-batch dedup failed");
    // Reversed packet ordering and independent live/replay identities.
    Assembler shuffled;
    auto longFirst = chat(15, 4);
    longFirst.count = 2;
    longFirst.payload = {'P', 0, 'a'};
    auto last = longFirst;
    last.part = 2;
    last.payload = {'b', 'c'};
    require(!shuffled.accept(last), "incomplete out-of-order message completed");
    require(bool(shuffled.accept(chat(16))), "live message blocked behind replay");
    auto complete = shuffled.accept(longFirst);
    require(complete && complete->text == "abc", "out-of-order replay failed");
    // All 16KiB complete-message bytes, 45 manual parts.
    Assembler large;
    std::string body = "P";
    body.push_back(0);
    body += std::string(16382, 'x');
    std::optional<Message> big;
    for (int i = 44; i >= 0; --i) {
        auto chunk = chat(19, 5);
        chunk.count = 45;
        chunk.part = uint16_t(i + 1);
        auto from = size_t(i) * 366;
        chunk.payload.assign(body.begin() + from, body.begin() + std::min(from + 366, body.size()));
        auto packet = decode_packet(encoded(chunk));
        require(bool(packet), "366 byte chunk rejected");
        big = large.accept(*packet);
    }
    require(big && big->text.size() == 16382, "long complete replay not recovered");
    // Dedup remains exact beyond the previous 10,000-identity FIFO horizon.
    Assembler durable;
    for (uint32_t i = 1; i <= 11000; ++i)
        require(bool(durable.accept(chat(i))), "sequential identity rejected");
    require(!durable.accept(chat(1, 23)), "long-running receiver forgot an already received original");
    Assembler holes;
    for (uint32_t i : {5u, 1u, 3u, 2u, 4u, 8u, 7u, 6u})
        require(bool(holes.accept(chat(i))), "sparse identity merge lost a hole");
    for (uint32_t i = 1; i <= 8; ++i)
        require(!holes.accept(chat(i, 2)), "merged completion range lost dedup");
    ReplayTracker tracker;
    Assembler stats;
    auto observe = [&](Packet packet) {
        auto m = stats.accept(packet);
        return tracker.observe(packet, m, stats);
    };
    observe(chat(1));
    observe(control(20, 20, 8, 3));
    observe(chat(1, 8));
    observe(chat(2, 8));
    auto partial = chat(3, 8);
    partial.count = 2;
    observe(partial);
    auto summary = observe(control(21, 21, 8, 3));
    require(summary && summary->find("new 1, already received 1, incomplete 1") != std::string::npos,
            "replay summary counts incorrect");
    require(!observe(control(21, 21, 8, 3)), "control redundancy produced repeated summary");
    partial.part = 2;
    partial.payload = {'!'};
    summary = observe(partial);
    require(summary && summary->find("new 2, already received 1, incomplete 0") != std::string::npos,
            "late replay completion not accounted for");
}
void markers() {
    for (bool matrix : {false, true})
    for (int corner = 0; corner < 4; ++corner)
        for (int cell = 2; cell <= 4; ++cell)
            for (bool active : {false, true}) {
                auto raw = marker_bytes(corner, cell, active, matrix);
                auto im = marker_image(raw, 5, 3);
                auto m = locate_marker(im);
                require(m && m->x == 5 && m->y == 3 && m->corner == corner && m->cell == cell &&
                            m->active == active && m->hint == 1234 && m->compact == matrix,
                        "marker state/layout invalid");
                auto loc = marker_location(*m);
                require(loc.cell == cell, "marker cell lost");
                for (int bit = 0; bit < 48; ++bit) {
                    auto changed = raw;
                    changed[bit / 8] ^= uint8_t(1 << (bit % 8));
                    require(!decode_marker(marker_image(changed)), "corrupt marker accepted");
                }
                im = marker_image(raw);
                im.pixels[0] = 110;
                require(!decode_marker(im), "ambiguous marker pixel accepted");
            }
    // A valid CRC does not make an old ribbon marker a current matrix marker.
    auto old = marker_bytes(0, 2, true);
    old[2] |= 8;
    uint8_t crc = 0;
    for (int i = 0; i < 5; ++i) {
        crc ^= old[i];
        for (int k = 0; k < 8; ++k) crc = uint8_t((crc << 1) ^ (crc & 128 ? 7 : 0));
    }
    old[5] = crc;
    require(!decode_marker(marker_image(old)), "Obsolete optical marker was silently reused");
}
void history() {
    std::deque<DisplayMessage> history;
    auto add = [&](uint32_t time, uint64_t session, uint32_t seq, uint16_t replay = 0) {
        DisplayMessage d;
        d.message.sent = time;
        d.message.session_order = session;
        d.message.seq = seq;
        d.message.replay_batch = replay;
        insert_history(history, std::move(d), 4);
    };
    add(100, 1, 3);
    add(102, 1, 6);
    add(100, 1, 1, 2);
    add(100, 2, 1);
    add(100, 1, 2);
    require(history.size() == 4 && history[0].message.seq == 2 && history[1].message.seq == 3 &&
                history[2].message.session_order == 2 && history[3].message.sent == 102,
            "stable chronological insertion/limit failed");
    add(50, 1, 0, 7);
    require(history.size() == 4 && history[0].message.seq == 2,
            "out-of-display replay displaced a newer row");
}
void journal() {
    auto dir = std::filesystem::temp_directory_path() /
               ("bianlingo-log-test-" + sha256(random_bytes(12)).substr(0, 12));
    auto m = Message{};
    m.session = 42;
    m.seq = 1;
    m.sent = 1792880000;
    m.received = local(24, 23, 59, 59);
    m.event = "CHAT_MSG_SAY";
    m.author = "玩家-Realm";
    m.text = "hello\nworld";
    m.replay_batch = 8;
    {
        ChatJournal log(dir);
        log.received(m);
        m.seq = 2;
        m.received = local(25, 0, 0, 0);
        log.received(m);
        log.result(m, "Skipped", "频道过滤");
        log.result(m, "Failed", "测试失败");
        log.result(m, "AI", "中文译文");
        m.seq = 3;
        log.received(m, true);
        log.result(m, "Glossary", "LF healer", true);
        // Verify normal periodic flush while process is still running.
        auto until = Clock::now() + std::chrono::seconds(3);
        while (!std::filesystem::exists(dir / "chat-2026-10-25.log") && Clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        require(std::filesystem::exists(dir / "chat-2026-10-25.log"),
                "writer did not flush within normal interval");
        require(log.error().empty(), "healthy journal reported an error");
        log.close();
    }
    auto first = all_logs(dir);
    require(first.find("original=hello\\nworld") != std::string::npos &&
                first.find("[Replay]") != std::string::npos && first.find("Send translation") != std::string::npos &&
                first.find("频道过滤") != std::string::npos && first.find("中文译文") != std::string::npos,
            "readable linked journal content missing");
    require(std::filesystem::exists(dir / "chat-2026-10-24.log"), "local midnight did not rotate");
    auto count = std::filesystem::file_size(dir / "chat-2026-10-25.log");
    {
        ChatJournal again(dir);
        m.text = "append after restart";
        again.received(m);
    }
    require(std::filesystem::file_size(dir / "chat-2026-10-25.log") > count,
            "restart truncated existing log");
    auto broken = dir / "not-directory";
    {
        std::ofstream file(broken);
        file << "x";
    }
    {
        ChatJournal log(broken);
        log.received(m);
        log.close();
        require(!log.error().empty(), "write failure not persistent");
        require(!utf32(log.error()).empty(), "write warning is not valid UTF-8 for native UI");
    }
    // Concurrent producers and shutdown drain.
    auto concurrent = dir / "parallel";
    {
        ChatJournal log(concurrent);
        std::vector<std::thread> producers;
        for (int i = 0; i < 4; ++i)
            producers.emplace_back([&, i] {
                auto value = m;
                value.seq = 100 + i;
                for (int n = 0; n < 100; ++n)
                    log.received(value);
            });
        for (auto &t : producers)
            t.join();
        log.close();
    }
    auto many = all_logs(concurrent);
    size_t n = 0, pos = 0;
    while ((pos = many.find("original=", pos)) != std::string::npos) {
        ++n;
        ++pos;
    }
    require(n == 400, "shutdown lost queued concurrent logs");
    // New-install opt-in: disabling must not create plaintext chat files.
    auto disabled = dir / "disabled";
    { ChatJournal log(disabled, false); log.received(m); log.result(m, "AI", "private"); }
    require(!std::filesystem::exists(disabled), "disabled journal created plaintext files");
    auto managed = dir / "managed";
    std::filesystem::create_directories(managed);
    { std::ofstream old(managed / "chat-2000-01-01.log"); old << "old"; }
    { std::ofstream other(managed / "other.txt"); other << "keep"; }
    { ChatJournal log(managed, false, 7); log.close(); }
    require(!std::filesystem::exists(managed / "chat-2000-01-01.log") &&
                std::filesystem::exists(managed / "other.txt"), "retention deleted unrelated files or kept expired logs");
    {
        ChatJournal log(managed, false);
        log.set_policy(true, 0); log.received(m);
        log.set_policy(false, 0); log.clear(); log.close();
    }
    require(all_logs(managed) == "keep" && std::filesystem::exists(managed / "other.txt"),
            "clear/disable re-created chat data or removed unrelated files");
    std::filesystem::remove_all(dir);
}
void lua_fixture(const char *file) {
    auto data = Json::parse(read_bytes(file));
    for (const auto &color : data.at("colors")) {
        Bytes raw;
        auto hex = color.at("raw").get<std::string>();
        for (size_t i = 0; i < hex.size(); i += 2)
            raw.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
        auto packet = decode_packet(raw);
        require(packet.has_value(), "Colored Lua packet rejected");
        Assembler assembler;
        auto message = assembler.accept(*packet);
        require(message && message->chat_rgb == color.at("rgb").get<uint32_t>() &&
                    message->text == color.at("text").get<std::string>() && message->player_class == "HUNTER",
                "Binary RGB prefix corrupted UTF-8, item markup or player metadata");
        require(chat_color(*message) == *message->chat_rgb, "Native chat RGB ignored");
        auto truncated = *packet;
        truncated.session += 1;
        truncated.payload = {1, 2};
        require(!assembler.accept(truncated), "Truncated RGB metadata accepted");
    }
    const auto &fixtures = data.at("packets");
    for (const auto &fixture : data.at("markers")) {
        Bytes raw;
        auto hex = fixture.at("raw").get<std::string>();
        for (size_t i = 0; i < hex.size(); i += 2)
            raw.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
        auto marker = decode_marker(marker_image(raw));
        require(marker && marker->corner == fixture["corner"].get<int>() &&
                    marker->cell == fixture["cell"].get<int>() &&
                    marker->active == fixture["active"].get<bool>() &&
                    marker->compact == fixture.value("compact", false) &&
                    marker->chat_state == fixture.value("chat_state", 0),
                "real Lua marker pixels rejected by C++");
    }
    bool checked_matrix_damage = false;
    for (const auto &fixture : data.at("images")) {
        Bytes pixels;
        const auto image_data = fixture["pixels"].get<std::string>();
        for (size_t offset = 0; offset < image_data.size(); offset += 8192) {
            auto chunk = unbase64(std::string_view(image_data).substr(offset, 8192));
            pixels.insert(pixels.end(), chunk.begin(), chunk.end());
        }
        Image image{fixture["width"].get<int>(), fixture["height"].get<int>(),
                    fixture["width"].get<int>() * 4, std::move(pixels)};
        Location location{0, 0, fixture["cell"].get<double>(), fixture["compact"].get<bool>()};
        auto decoded = decode_image(image, location);
        require(bool(decoded), "Actual Lua pixel frame rejected");
        Bytes expected;
        auto hex = fixture.at("raw").get<std::string>();
        for (size_t i = 0; i < hex.size(); i += 2)
            expected.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
        auto original = decode_packet(expected);
        require(original && decoded->session == original->session && decoded->seq == original->seq &&
                    decoded->sent == original->sent && decoded->part == original->part &&
                    decoded->count == original->count && decoded->event == original->event &&
                    decoded->player == original->player && decoded->replay_batch == original->replay_batch &&
                    decoded->payload == original->payload, "Optical transport changed packet bytes");
        auto found = locate(image);
        require(found && found->compact == location.compact, "Compact/native frame discovery failed");
        if (location.compact) {
            if (!checked_matrix_damage) {
                checked_matrix_damage = true;
                // Exercise every data cell in each channel, including optical
                // header, logical metadata, payload, and padded tail bytes.
                for (int i = 0; i < 360; ++i)
                    for (int channel = 0; channel < 3; ++channel) {
                        auto corrupt = image;
                        int x = 2 + i / 10, row = i % 10, y = x % 2 ? 9 - row : row;
                        auto sample = (size_t(y * int(location.cell)) * image.width + x * int(location.cell)) * 4 + channel;
                        int level = int(std::lround(image.pixels[sample] * 7.0 / 255)) ^ 1;
                        for (int dy = 0; dy < int(location.cell); ++dy)
                            for (int dx = 0; dx < int(location.cell); ++dx)
                                corrupt.pixels[(size_t(y * int(location.cell) + dy) * image.width + x * int(location.cell) + dx) * 4 + channel] =
                                    uint8_t(std::lround(level * 255.0 / 7));
                        require(!decode_image(corrupt, location), "Matrix cell corruption escaped validation");
                    }
            }
            // Monotonic gamma conversion is correctable by the reference ramp.
            auto gamma = image;
            for (size_t i = 0; i < gamma.pixels.size(); ++i)
                if (i % 4 != 3)
                    gamma.pixels[i] = uint8_t(std::lround(255 * std::pow(gamma.pixels[i] / 255.0, 1.25)));
            auto corrected = decode_image(gamma, location);
            require(corrected && corrected->payload == decoded->payload,
                    "Calibrated colour shift changed the payload");
            auto damaged = image;
            for (int y = 0; y < image.height; ++y)
                for (int x = 0; x < image.width; ++x)
                    if ((x / int(location.cell)) == 20 && y < int(location.cell))
                        damaged.pixels[(size_t(y) * image.width + x) * 4 + 2] ^= 128;
            require(!decode_image(damaged, location), "Compact data corruption escaped checksum");
            auto clipped = image;
            for (size_t i = 0; i < clipped.pixels.size(); ++i)
                if (i % 4 != 3)
                    clipped.pixels[i] = std::min(uint8_t(150), clipped.pixels[i]);
            require(!decode_image(clipped, location), "Collapsed HDR-like calibration was accepted");
        }
    }
    for (const auto &layout : data.value("layouts", Json::array())) {
        Assembler assembler;
        std::optional<Message> complete;
        auto packets = layout.at("packets");
        for (auto it = packets.rbegin(); it != packets.rend(); ++it) {
            auto hex = it->get<std::string>();
            Bytes raw;
            for (size_t i = 0; i < hex.size(); i += 2)
                raw.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
            auto p = decode_packet(raw);
            require(p && p->event == 22 && !p->player && !p->replay_batch, "Lua layout packet flags invalid");
            if (auto message = assembler.accept(*p))
                complete = message;
        }
        require(complete && complete->event == "BRIDGE_CHAT_LAYOUT" && complete->author.empty(),
                "Lua layout did not reassemble out of order");
        require(parse_chat_layout(complete->text) == layout.at("expected"),
                "Lua layout escaping/metadata differs from C++");
    }
    for (const auto &fixture : data.value("realms", Json::array())) {
        for (auto key : {"raw", "replay_raw"}) {
            auto hex = fixture.at(key).get<std::string>();
            Bytes raw;
            for (size_t i = 0; i < hex.size(); i += 2)
                raw.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
            auto packet = decode_packet(raw);
            require(bool(packet), "Same-realm flag corrupted packet decoding");
            Assembler assembler;
            auto message = assembler.accept(*packet);
            require(message && message->same_realm == fixture.at("same_realm").get<bool>() &&
                        message->author == fixture.at("author").get<std::string>() &&
                        message->player_class == fixture.at("player_class").get<std::string>(),
                    "Lua realm flag or full identity did not survive live/replay reassembly");
            auto target = whisper_target(*message);
            require(target && target->prefix == fixture.at("prefix").get<std::string>(),
                    "Confirmed-local/cross/unknown realm whisper policy differs");
        }
    }
    std::vector<Packet> packets;
    for (const auto &fixture : fixtures) {
        Bytes raw;
        auto hex = fixture.at("raw").get<std::string>();
        for (size_t i = 0; i < hex.size(); i += 2)
            raw.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
        auto packet = decode_packet(raw);
        require(bool(packet), "real Lua packet rejected by C++");
        require(packet->replay_batch == fixture["batch"].get<int>() &&
                    packet->sent == fixture["sent"].get<uint32_t>(),
                "real Lua identity/batch/time changed");
        packets.push_back(*packet);
    }
    std::shuffle(packets.begin(), packets.end(), std::mt19937{42});
    Assembler a;
    size_t completed = 0, manual = 0;
    for (int repeat = 0; repeat < 3; ++repeat)
        for (const auto &p : packets)
            if (auto m = a.accept(p)) {
                if (m->event.rfind("CHAT_MSG_", 0) == 0) {
                    ++completed;
                    if (m->replay_batch)
                        ++manual;
                    require(m->author == "Player-Realm" && m->player_class == "HUNTER",
                            "Lua player metadata lost");
                }
            }
    require(completed == 12 && manual == 11,
            "real Lua cross-batch/short-repeat dedup or reconstruction failed");
}
} // namespace
int main(int argc, char **argv) {
    try {
        protocol();
        markers();
        history();
        journal();
        if (argc > 1)
            lua_fixture(argv[1]);
        std::cout << "Passed " << checks << " bridge feature assertions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
