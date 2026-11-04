#include "bianlingo/core.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace bl {
uint32_t adler(std::span<const uint8_t> bytes) {
    uint32_t a = 1, b = 0;
    for (auto c : bytes) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}
static uint32_t u32(std::span<const uint8_t> b, size_t p) {
    return uint32_t(b[p]) << 24 | uint32_t(b[p + 1]) << 16 | uint32_t(b[p + 2]) << 8 | b[p + 3];
}
static uint16_t u16(std::span<const uint8_t> b, size_t p) {
    return uint16_t(b[p] << 8 | b[p + 1]);
}
std::optional<Packet> decode_packet(std::span<const uint8_t> raw) {
    if (raw.size() < 28 || std::string_view(reinterpret_cast<const char *>(raw.data()), 4) != "BLG2")
        return {};
    Packet p{u32(raw, 4), u32(raw, 8), u32(raw, 12), u16(raw, 16), u16(raw, 18), uint8_t(raw[22] & 127),
             raw[23],     {}};
    auto len = u16(raw, 20);
    if (!p.part || p.part > p.count || p.count > 45 || len > capacity || raw.size() < 28 + size_t(len) ||
        p.event >= events.size() || (p.player & player_class_mask) >= classes.size() ||
        (!p.event && p.player) || ((p.player & (64 | player_same_realm)) && (p.event < 1 || p.event > 17)))
        return {};
    Bytes check(raw.begin(), raw.begin() + 24);
    check.insert(check.end(), raw.begin() + 28, raw.begin() + 28 + len);
    if (adler(check) != u32(raw, 24))
        return {};
    p.payload.assign(raw.begin() + 28, raw.begin() + 28 + len);
    if (raw[22] & 128) {
        if (len < 2 || p.event < 1 || p.event > 17)
            return {};
        p.replay_batch = u16(p.payload, 0);
        if (!p.replay_batch)
            return {};
        p.payload.erase(p.payload.begin(), p.payload.begin() + 2);
    }
    return p;
}
static bool valid_image(const Image &im) {
    return im.width > 0 && im.height > 0 && im.stride >= im.width * 4 &&
           im.pixels.size() >= size_t(im.stride) * im.height;
}
static const uint8_t *pixel(const Image &im, int x, int y) {
    return im.pixels.data() + size_t(y) * im.stride + size_t(x) * 4;
}
static bool white(const uint8_t *c) {
    return int(c[0]) + c[1] + c[2] > 384;
}
static std::optional<Packet> decode_compact(const Image &im, Location loc) {
    if (!valid_image(im) || loc.x < 0 || loc.y < 0 || !std::isfinite(loc.x + loc.y + loc.cell) ||
        loc.cell < 0.9 || loc.cell > 16 || loc.x + compact_cols * loc.cell > im.width + 0.01 ||
        loc.y + compact_rows * loc.cell > im.height + 0.01)
        return {};
    auto sample = [&](int cell) {
        return pixel(im, int(std::floor(loc.x + (cell % compact_cols + 0.5) * loc.cell)),
                     int(std::floor(loc.y + (cell / compact_cols + 0.5) * loc.cell)));
    };
    // MCT1 optical layer: binary locator on the left, eight-level vertical
    // references on the right, three independent bit planes in between.
    constexpr std::array<int, 10> rail{3, 2, 2, 3, 1, 0, 3, 0, 2, 1};
    for (int y = 0; y < compact_rows; ++y)
        for (int x = 0; x < 2; ++x) {
            auto c = sample(y * compact_cols + x);
            const bool high = (rail[y] >> (1 - x)) & 1;
            for (int k = 0; k < 3; ++k)
                if (high ? c[k] < 180 : c[k] > 75) return {};
        }
    std::array<std::array<int, 8>, 3> calibration{};
    for (int channel = 0; channel < 3; ++channel) {
        for (int level = 0; level < 8; ++level) {
            calibration[channel][level] = sample((level + 1) * compact_cols + compact_cols - 1)[2 - channel];
            if (level && calibration[channel][level] - calibration[channel][level - 1] < 6)
                return {};
        }
        if (calibration[channel][7] - calibration[channel][0] < 120) return {};
    }
    auto level_at = [&](int cell, int channel) {
        int value = sample(cell)[2 - channel];
        const auto &levels = calibration[channel];
        auto next = std::lower_bound(levels.begin(), levels.end(), value);
        if (next == levels.begin()) return 0;
        if (next == levels.end()) return 7;
        int index = int(next - levels.begin());
        return value - levels[index - 1] < levels[index] - value ? index - 1 : index;
    };
    std::array<uint8_t, 405> wire{};
    for (int i = 0; i < 360; ++i) {
        int x = 2 + i / 10, row = i % 10;
        int cell = (x % 2 ? 9 - row : row) * compact_cols + x;
        for (int channel = 0; channel < 3; ++channel) {
            auto value = level_at(cell, channel);
            size_t bit = size_t(channel * 1080 + i * 3);
            for (int k = 2; k >= 0; --k, ++bit)
                wire[bit / 8] |= uint8_t(((value >> k) & 1) << (7 - bit % 8));
        }
    }
    if (std::string_view(reinterpret_cast<const char *>(wire.data()), 4) != "MCT1" || wire.back()) return {};
    auto raw = std::span<const uint8_t>(wire).subspan(8, 396);
    if (adler(raw) != u32(wire, 4)) return {};
    return decode_packet(raw);
}
std::optional<Packet> decode_image(const Image &im, Location loc) {
    if (loc.compact)
        return decode_compact(im, loc);
    if (!valid_image(im) || !std::isfinite(loc.x + loc.y + loc.cell) || loc.x < 0 || loc.y < 0 ||
        loc.cell < 1.9 || loc.cell > 16)
        return {};
    std::array<int, cols> xs;
    std::array<int, rows> ys;
    for (int i = 0; i < cols; i++)
        xs[i] = int(std::floor(loc.x + loc.cell * (i + 0.5)));
    for (int i = 0; i < rows; i++)
        ys[i] = int(std::floor(loc.y + loc.cell * (i + 0.5)));
    if (xs.back() >= im.width || ys.back() >= im.height)
        return {};
    for (int i = 0; i < cols; i++)
        if (white(pixel(im, xs[i], ys[0])) != (sync_bits[i] == '1'))
            return {};
    std::array<uint8_t, 396> raw{};
    size_t bit = 0;
    for (int y = 1; y < rows; y++)
        for (int x = 0; x < cols; x++) {
            const auto *c = pixel(im, xs[x], ys[y]);
            for (int k = 2; k >= 0; k--, bit++) {
                if (c[k] > 75 && c[k] < 180)
                    return {};
                raw[bit / 8] |= uint8_t((c[k] > 128 ? 1 : 0) << (7 - bit % 8));
            }
        }
    return decode_packet(raw);
}
static std::optional<Location> locate_original(const Image &im) {
    if (!valid_image(im) || im.width < 192 || im.height < 24)
        return {};
    std::vector<int> boundaries{0};
    for (int i = 1; i < cols; i++)
        if (sync_bits[i] != sync_bits[i - 1])
            boundaries.push_back(i);
    std::vector<int> edges;
    edges.reserve(im.width / 2);
    for (int y = 0; y < im.height - 22; y += 2) {
        edges.clear();
        bool prev = white(pixel(im, 0, y));
        if (prev)
            edges.push_back(0); // Exact cached crops can start on the first sync cell.
        for (int x = 1; x < im.width; x++) {
            bool w = white(pixel(im, x, y));
            if (w != prev)
                edges.push_back(x);
            prev = w;
        }
        for (size_t i = 0; i + boundaries.size() <= edges.size(); i++) {
            int start = edges[i], width = edges[i + 1] - start;
            if (!white(pixel(im, start, y)) || width < 8 || width > 64)
                continue;
            double cell = double(edges[i + boundaries.size() - 1] - start) / boundaries.back();
            if (cell < 1.9 || cell > 16 || y + cell * rows > im.height + 1)
                continue;
            double x = 0;
            for (size_t j = 0; j < boundaries.size(); j++)
                x += edges[i + j] - cell * boundaries[j];
            x /= boundaries.size();
            bool good = true;
            for (size_t j = 0; j < boundaries.size(); j++)
                if (std::abs(edges[i + j] - (x + cell * boundaries[j])) > 1.2) {
                    good = false;
                    break;
                }
            if (!good)
                continue;
            auto sync_at = [&](int yy) {
                for (int j = 0; j < cols; j++) {
                    int xx = int(std::floor(x + cell * (j + 0.5)));
                    if (xx < 0 || xx >= im.width || white(pixel(im, xx, yy)) != (sync_bits[j] == '1'))
                        return false;
                }
                return true;
            };
            if (!sync_at(y))
                continue;
            int top = y;
            for (int up = 1; up <= int(std::ceil(cell)) && y - up >= 0; up++) {
                if (!sync_at(y - up))
                    break;
                top = y - up;
            }
            if (decode_image(im, {x, double(top), cell}))
                return Location{x, double(top), cell};
        }
    }
    return {};
}

std::optional<Location> locate(const Image &im) {
    if (valid_image(im)) {
        for (int y = 0; y + compact_rows <= im.height; ++y)
            for (int x = 0; x + compact_cols <= im.width; ++x) {
                auto c = pixel(im, x, y);
                if (c[0] < 180 || c[1] < 180 || c[2] < 180)
                    continue;
                for (int cell = 1; cell <= 4; ++cell) {
                    if (x + compact_cols * cell > im.width || y + compact_rows * cell > im.height)
                        continue;
                    // White UI regions are common. Reject them using the first
                    // black locator cell before calibrating or sampling a frame.
                    auto dark = pixel(im, x + cell + cell / 2, y + cell + cell / 2);
                    if (dark[0] > 75 || dark[1] > 75 || dark[2] > 75)
                        continue;
                    Location location{double(x), double(y), double(cell), true};
                    if (decode_compact(im, location))
                        return location;
                }
            }
    }
    return locate_original(im);
}
std::optional<Marker> decode_marker(const Image &im, int x, int y) {
    if (!valid_image(im) || x < 0 || y < 0 || x + 4 > im.width || y + 4 > im.height)
        return {};
    std::array<uint8_t, 6> raw{};
    size_t bit = 0;
    for (int i = 0; i < 16; ++i) {
        auto c = pixel(im, x + i % 4, y + i / 4);
        for (int k = 2; k >= 0; --k, ++bit) {
            if (c[k] > 75 && c[k] < 180)
                return {};
            raw[bit / 8] |= uint8_t((c[k] > 128 ? 1 : 0) << (7 - bit % 8));
        }
    }
    uint8_t crc = 0;
    for (int i = 0; i < 5; ++i) {
        crc ^= raw[i];
        for (int k = 0; k < 8; ++k)
            crc = uint8_t((crc << 1) ^ (crc & 128 ? 7 : 0));
    }
    if (raw[0] != 252 || raw[1] != 177 || ((raw[2] & 0xF0) != 0x10 && (raw[2] & 0xF0) != 0x20 && (raw[2] & 0xF0) != 0x30 && (raw[2] & 0xF0) != 0x40) ||
        ((raw[2] & 0xF0) != 0x40 && (raw[2] & 3) > 2) || raw[5] != crc)
        return {};
    // Old multi-level ribbon markers are deliberately not treated as MCT1.
    if (((raw[2] & 0xF0) >= 0x30) != bool(raw[2] & 8)) return {};
    auto hint = u16(raw, 3);
    if ((raw[2] & 0xF0) != 0x10 && ((hint >> 12) & 3) == 3)
        return {};
    return Marker{x,
                  y,
                  (raw[2] & 3) + ((raw[2] & 0xF0) == 0x40 ? 1 : 2),
                  hint >> 14,
                  (raw[2] & 4) != 0,
                  uint16_t(hint & ((raw[2] & 0xF0) != 0x10 ? 4095 : 16383)),
                  (raw[2] & 8) != 0,
                  uint8_t((raw[2] & 0xF0) != 0x10 ? ((hint >> 12) & 3) : 0)};
}
std::optional<Marker> locate_marker(const Image &im) {
    if (!valid_image(im))
        return {};
    // Signature's first RGB pixel is white; reject the vast majority cheaply.
    for (int y = 0; y + 4 <= im.height; ++y)
        for (int x = 0; x + 4 <= im.width; ++x) {
            auto c = pixel(im, x, y);
            if (c[0] < 180 || c[1] < 180 || c[2] < 180)
                continue;
            if (auto m = decode_marker(im, x, y))
                return m;
        }
    return {};
}
Location marker_location(const Marker &m) {
    return {double(m.x + (m.corner & 1 ? -1 - (grid_cols(m.compact) + 1) * m.cell : 5 + m.cell)),
            double(m.y + (m.corner & 2 ? -1 - (grid_rows(m.compact) + 1) * m.cell : 5 + m.cell)),
            double(m.cell), m.compact};
}
HistoryChange update_history(std::deque<DisplayMessage> &history, DisplayMessage value, size_t limit) {
    auto less = [](const DisplayMessage &a, const DisplayMessage &b) {
        auto &x = a.message;
        auto &y = b.message;
        return std::tie(x.sent, x.session_order, x.seq, x.receive_order) <
               std::tie(y.sent, y.session_order, y.seq, y.receive_order);
    };
    auto at = std::upper_bound(history.begin(), history.end(), value, less);
    auto position = size_t(std::distance(history.begin(), at));
    history.insert(at, std::move(value));
    auto removed = history.size() > limit ? history.size() - limit : 0;
    for (size_t i = 0; i < removed; ++i)
        history.pop_front();
    return {position >= removed, position, removed};
}
bool insert_history(std::deque<DisplayMessage> &history, DisplayMessage value, size_t limit) {
    return update_history(history, std::move(value), limit).retained;
}

std::optional<std::string> ReplayTracker::observe(const Packet &p, const std::optional<Message> &message,
                                                  const Assembler &assembler) {
    uint16_t batch = p.replay_batch;
    size_t total = 0;
    bool control =
        message && (message->event == "BRIDGE_REPLAY_BEGIN" || message->event == "BRIDGE_REPLAY_END");
    if (control) {
        auto tab = message->text.find('\t');
        if (tab == std::string::npos)
            return {};
        try {
            auto id = std::stoul(message->text.substr(0, tab));
            auto count = std::stoul(message->text.substr(tab + 1));
            if (!id || id > 65535 || !count || count > 100)
                return {};
            batch = uint16_t(id);
            total = count;
        } catch (...) {
            return {};
        }
    }
    if (!batch)
        return {};
    auto key = std::pair{p.session, batch};
    if (!batches_.contains(key)) {
        batches_[key] = {};
        order_.push_back(key);
    }
    while (order_.size() > 32) {
        batches_.erase(order_.front());
        order_.pop_front();
    }
    auto &b = batches_.at(key);
    if (control) {
        b.total = total;
        if (message->event == "BRIDGE_REPLAY_END")
            b.ended = true;
    } else {
        if (b.observed.size() < 100)
            b.observed.insert(p.seq);
        if (message && message->replay_batch)
            b.added.insert(p.seq);
    }
    if (!b.ended || (!control && !message))
        return {};
    size_t received = 0;
    for (auto seq : b.observed) {
        Packet original;
        original.session = p.session;
        original.seq = seq;
        if (assembler.seen(original))
            ++received;
    }
    auto added = b.added.size(), duplicate = received >= added ? received - added : 0;
    auto missing = b.total > received ? b.total - received : 0;
    return "Replay batch " + std::to_string(batch) + ": new " + std::to_string(added) + ", already received " +
           std::to_string(duplicate) + ", incomplete " + std::to_string(missing);
}
static std::string take(std::string_view &in) {
    auto n = in.find('\0');
    if (n == std::string_view::npos)
        throw std::runtime_error("Incomplete bridge fields");
    std::string out(in.substr(0, n));
    in.remove_prefix(n + 1);
    return out;
}
bool Assembler::seen(const Packet &p) const {
    auto session = seen_.find(p.session);
    if (session == seen_.end())
        return false;
    auto end = session->second.upper_bound(p.seq);
    return end != session->second.begin() && std::prev(end)->second >= p.seq;
}
void Assembler::remember(const Packet &p) {
    auto &ranges = seen_[p.session];
    auto next = ranges.upper_bound(p.seq);
    uint32_t start = p.seq, end = p.seq;
    if (next != ranges.begin()) {
        auto previous = std::prev(next);
        if (uint64_t(previous->second) + 1 >= p.seq) {
            start = previous->first;
            end = std::max(end, previous->second);
            ranges.erase(previous);
        }
    }
    if (next != ranges.end() && uint64_t(end) + 1 >= next->first) {
        end = next->second;
        ranges.erase(next);
    }
    ranges[start] = end;
}
std::optional<Message> Assembler::accept(const Packet &p) {
    if (!p.part || p.part > p.count || p.count > 45 || p.payload.size() > capacity ||
        p.event >= events.size() || (p.player & player_class_mask) >= classes.size() ||
        ((p.player & (64 | player_same_realm)) && (p.event < 1 || p.event > 17)))
        return {};
    uint64_t identity = (uint64_t(p.session) << 32) | p.seq;
    Key key{identity, p.replay_batch};
    if (seen(p))
        return {};
    if (!pending_.contains(key)) {
        pending_[key] = {p, {}};
        pending_order_.push_back(key);
    }
    auto &entry = pending_.at(key);
    if (entry.first.count != p.count || entry.first.event != p.event || entry.first.player != p.player ||
        entry.first.sent != p.sent)
        return {};
    entry.parts[p.part] = p.payload;
    while (pending_order_.size() > 256) {
        pending_.erase(pending_order_.front());
        pending_order_.pop_front();
    }
    if (entry.parts.size() != p.count)
        return {};
    std::string body;
    for (auto &[_, v] : entry.parts)
        body.append(reinterpret_cast<const char *>(v.data()), v.size());
    if (body.size() > 16384) {
        pending_.erase(key);
        std::erase(pending_order_, key);
        return {};
    }
    Message m;
    m.session = p.session;
    m.seq = p.seq;
    m.sent = p.sent;
    m.replay_batch = p.replay_batch;
    m.same_realm = (p.player & player_same_realm) != 0;
    try {
        std::string_view in(body);
        if (p.player & 64) {
            if (in.size() < 3)
                throw std::runtime_error("Bad chat color");
            m.chat_rgb = uint32_t(uint8_t(in[0])) << 16 | uint32_t(uint8_t(in[1])) << 8 | uint8_t(in[2]);
            in.remove_prefix(3);
        }
        utf32(in); // Color prefix is binary; all remaining fields must be UTF-8.
        if (p.event) {
            m.event = events[p.event];
            m.player_class = classes[p.player & player_class_mask];
            if (m.event == "CHAT_MSG_CHANNEL") {
                auto channel = take(in);
                auto tab = channel.find('\t');
                if (tab != std::string::npos) {
                    auto n = channel.substr(0, tab);
                    if (!n.empty() && n.size() < 4 &&
                        std::all_of(n.begin(), n.end(), [](char c) { return c >= '0' && c <= '9'; }))
                        m.channel_number = std::stoi(n);
                    m.channel_name = channel.substr(tab + 1);
                }
            }
            if (p.player & 128) {
                m.localized_class = take(in);
                m.race = take(in);
                m.english_race = take(in);
                m.player_name = take(in);
                m.realm = take(in);
                // Compatibility with the exact 0.4.3 gsub-count metadata bug.
                if (!in.empty() && in[0] >= '0' && in[0] <= '9') {
                    auto count = take(in);
                    if (count.size() > 3 ||
                        !std::all_of(count.begin(), count.end(), [](char c) { return c >= '0' && c <= '9'; }))
                        throw std::runtime_error("Bad metadata");
                }
                if (in.empty() || uint8_t(in[0]) > 3)
                    throw std::runtime_error("Bad sex");
                m.sex = uint8_t(in[0]);
                in.remove_prefix(1);
            }
            m.author = take(in);
            m.text = in;
        } else {
            m.event = take(in);
            m.author = take(in);
            m.text = in;
            auto tab = m.event.find('\t');
            if (tab != std::string::npos)
                m.event.resize(tab);
        }
        if (m.event.rfind("CHAT_MSG_", 0) != 0 && m.event != "BRIDGE_READY" && m.event != "BRIDGE_TEST" &&
            m.event != "BRIDGE_REPLAY_BEGIN" && m.event != "BRIDGE_REPLAY_END" &&
            m.event != "BRIDGE_CHAT_LAYOUT" && m.event != "BRIDGE_BLOCKLIST")
            throw std::runtime_error("Unknown event");
    } catch (const std::exception &) {
        pending_.erase(key);
        std::erase(pending_order_, key);
        return {};
    }
    pending_.erase(key);
    std::erase(pending_order_, key);
    remember(p);
    // Discard all partial copies, including other replay batches, after completion.
    std::erase_if(pending_, [&](const auto &v) { return v.first.first == identity; });
    std::erase_if(pending_order_, [&](const auto &v) { return v.first == identity; });
    return m;
}
} // namespace bl
