#include "bianlingo/capture.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace bl;
namespace {
int checks = 0;
void require(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
Bytes dense_fixture(const Bytes &original) {
    Bytes raw(396);
    size_t bit = 0;
    for (int y = 1; y < rows; ++y)
        for (int x = 0; x < cols; ++x)
            for (int k = 2; k >= 0; --k, ++bit) {
                auto value = original[(size_t(y * 2 + 1) * cols * 2 + x * 2 + 1) * 4 + k];
                raw[bit / 8] |= uint8_t((value > 128 ? 1 : 0) << (7 - bit % 8));
            }
    std::vector<int> cells(compact_cols * compact_rows);
    constexpr int rail[]{3,2,2,3,1,0,3,0,2,1};
    for (int y=0;y<10;++y) {
        cells[y*40]=(rail[y]>>1)*511; cells[y*40+1]=(rail[y]&1)*511;
    }
    for(int n=0;n<8;++n) cells[(n+1)*40+39]=n*73;
    Bytes wire{'M','C','T','1'};
    auto checksum=adler(raw);
    for(int shift=24;shift>=0;shift-=8) wire.push_back(uint8_t(checksum>>shift));
    wire.insert(wire.end(),raw.begin(),raw.end()); wire.push_back(0);
    for(int i=0;i<360;++i) {
        int x=2+i/10, y=x%2?9-i%10:i%10, value=0;
        for(int channel=0;channel<3;++channel) {
            int level=0;
            for(int k=0;k<3;++k) {int bit=channel*1080+i*3+k; level=level*2+((wire[bit/8]>>(7-bit%8))&1);}
            value=value*8+level;
        }
        cells[y*40+x]=value;
    }
    Bytes pixels(compact_cols * compact_rows * 16);
    for (int y = 0; y < compact_rows * 2; ++y)
        for (int x = 0; x < compact_cols * 2; ++x) {
            int value = cells[(y / 2) * compact_cols + x / 2];
            auto offset = (size_t(y) * compact_cols * 2 + x) * 4;
            pixels[offset] = uint8_t(std::lround(value % 8 * 255.0 / 7));
            pixels[offset + 1] = uint8_t(std::lround(value / 8 % 8 * 255.0 / 7));
            pixels[offset + 2] = uint8_t(std::lround(value / 64 * 255.0 / 7));
            pixels[offset + 3] = 255;
        }
    return pixels;
}
class Screen final : public platform::Capture {
    Image image_;
    Bytes fixture_, original_;

  public:
    bool scoped = true, visible_strip = true, marker_visible = false, marker_active = false,
         corrupt_marker = false, compact = false;
    int marker_corner = 0;
    std::optional<GameWindow> window = GameWindow{1, {100, 200, 1280, 720}};
    Rect desktop{0, 0, 1280, 720};
    int left = 8, top = 8, cell = 2, monitor_queries = 0;
    std::vector<Rect> reads;
    explicit Screen(Bytes fixture) : fixture_(fixture), original_(std::move(fixture)) {}
    void dense(bool enabled = true) {
        compact = enabled;
        fixture_ = enabled ? dense_fixture(original_) : original_;
    }
    bool window_scoped() const override {
        return scoped;
    }
    std::optional<GameWindow> game_window() override {
        return window;
    }
    std::vector<Rect> monitors() override {
        ++monitor_queries;
        if (scoped)
            throw std::runtime_error("Window capture enumerated monitors");
        return {desktop};
    }
    const Image &grab(Rect r) override {
        Rect client = scoped ? window.value().client : desktop;
        require(r.x >= client.x && r.y >= client.y && r.x + r.w <= client.x + client.w &&
                    r.y + r.h <= client.y + client.h,
                "Read escaped the game client area");
        reads.push_back(r);
        image_ = {r.w, r.h, r.w * 4, Bytes(size_t(r.w) * r.h * 4, 0)};
        Bytes marker{252,
                     177,
                     uint8_t((compact ? 48 : 16) + (marker_active ? 4 : 0) + (compact ? 8 : 0) + cell - 2),
                     uint8_t(marker_corner << 6),
                     123,
                     0};
        uint8_t crc = 0;
        for (int i = 0; i < 5; ++i) {
            crc ^= marker[i];
            for (int k = 0; k < 8; ++k)
                crc = uint8_t((crc << 1) ^ (crc & 128 ? 7 : 0));
        }
        marker[5] = crc ^ (corrupt_marker ? 1 : 0);
        int mx = marker_corner & 1 ? client.w - 5 : 1, my = marker_corner & 2 ? client.h - 5 : 1;
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                int bx = r.x + x - client.x - left, by = r.y + y - client.y - top;
                if (visible_strip && bx >= 0 && by >= 0 && bx < grid_cols(compact) * cell &&
                    by < grid_rows(compact) * cell) {
                    size_t from = (size_t(by * 2 / cell) * grid_cols(compact) * 2 + bx * 2 / cell) * 4;
                    std::copy_n(fixture_.begin() + from, 4,
                                image_.pixels.begin() + (size_t(y) * r.w + x) * 4);
                }
                int xmark = r.x + x - client.x - mx, ymark = r.y + y - client.y - my;
                if (marker_visible && xmark >= 0 && ymark >= 0 && xmark < 4 && ymark < 4) {
                    size_t bit = size_t(ymark * 4 + xmark) * 3;
                    for (int k = 2; k >= 0; --k, ++bit)
                        image_.pixels[(size_t(y) * r.w + x) * 4 + k] =
                            (marker[bit / 8] >> (7 - bit % 8)) & 1 ? 255 : 0;
                }
            }
        return image_;
    }
    void bottom_right() {
        left = window->client.w - 8 - cols * cell;
        top = window->client.h - 8 - rows * cell;
    }
};
} // namespace

int main() {
    try {
        auto root = std::filesystem::path(TEST_RESOURCE_DIR);
        auto fixtures = Json::parse(read_bytes(root / "bridge.json"));
        auto bytes = read_bytes(root / fixtures[0]["images"][0].get<std::string>());
        auto now = Clock::time_point{} + std::chrono::hours(1);
        Screen screen(bytes);
        BridgeCapture reader;

        screen.window.reset();
        for (int i = 0; i < 60; ++i) {
            auto step = reader.poll(screen, now + std::chrono::seconds(i));
            require(!step.connected && !step.packet && step.interval == std::chrono::seconds(1),
                    "Missing/minimized game did not pause capture");
        }
        require(screen.reads.empty() && !screen.monitor_queries,
                "Missing game caused desktop reads or monitor discovery");

        screen.window = platform::Capture::GameWindow{1, {100, 200, 1280, 720}};
        auto step = reader.poll(screen, now);
        require(step.connected && step.packet && step.window_id == 1, "Game corner was not decoded");
        require(screen.reads.size() == 2 && screen.reads.front() == Rect{100, 200, 440, 120},
                "First search did not stay in the client corner");
        screen.reads.clear();
        for (int i = 1; i <= 600; ++i) {
            step = reader.poll(screen, now + std::chrono::milliseconds(i * 100));
            require(step.connected && step.packet && step.interval == std::chrono::milliseconds(100),
                    "Stable bridge changed its sampling interval");
        }
        require(screen.reads.size() == 600 &&
                    std::all_of(screen.reads.begin(), screen.reads.end(),
                                [](Rect r) { return r == Rect{108, 208, 192, 24}; }),
                "Stable capture rescanned corners or the desktop");

        now += std::chrono::seconds(61);
        screen.window->client.x = -1800;
        screen.window->client.y = 60;
        screen.reads.clear();
        step = reader.poll(screen, now);
        require(step.connected && step.packet && screen.reads == std::vector<Rect>{{-1792, 68, 192, 24}},
                "Moving to a negative-coordinate monitor forced rediscovery");

        auto saved = screen.window;
        screen.window.reset();
        screen.reads.clear();
        step = reader.poll(screen, now + std::chrono::seconds(1));
        require(!step.connected && screen.reads.empty(), "Minimized game was still captured");
        screen.window = saved;
        step = reader.poll(screen, now + std::chrono::seconds(2));
        require(step.connected && step.packet && screen.reads.size() == 1,
                "Restored game lost its relative bridge location");

        for (int cell : {2, 3, 4}) {
            screen.cell = cell;
            screen.window->client.w += 100;
            screen.window->client.h += 100;
            screen.bottom_right();
            screen.reads.clear();
            step = reader.poll(screen, now + std::chrono::seconds(3));
            require(step.connected && step.packet && screen.reads.size() <= 5,
                    "Resize or cell size change lost the bottom-right bridge");
            require(std::all_of(screen.reads.begin(), screen.reads.end(),
                                [](Rect r) { return r.w <= 440 && r.h <= 120; }),
                    "Resize caused a full-window screenshot");
        }
        for (int corner = 0; corner < 4; ++corner) {
            screen.window->id++;
            screen.left = corner & 1 ? screen.window->client.w - 8 - cols * screen.cell : 8;
            screen.top = corner & 2 ? screen.window->client.h - 8 - rows * screen.cell : 8;
            screen.reads.clear();
            step = reader.poll(screen, now + std::chrono::seconds(4));
            require(step.connected && step.packet && step.window_id == screen.window->id &&
                        screen.reads.size() <= 5,
                    "Switching game windows did not reset corner discovery");
        }

        Screen blank(bytes);
        blank.visible_strip = false;
        BridgeCapture searching;
        for (int i = 0; i <= 60; ++i) {
            auto missing = searching.poll(blank, now + std::chrono::seconds(i));
            require(!missing.connected && !missing.packet, "Missing addon produced a packet");
        }
        require(blank.reads.size() == 52 && !blank.monitor_queries,
                "Missing addon caused an unbounded search or full-screen fallback");
        blank.window->client = {0, 0, 100, 20};
        blank.reads.clear();
        require(!searching.poll(blank, now + std::chrono::seconds(61)).connected && blank.reads.empty(),
                "Tiny/unavailable game client was captured");

        Screen fallback(bytes);
        fallback.scoped = false;
        fallback.left = 500;
        fallback.top = 300;
        BridgeCapture fallback_reader;
        auto old = fallback_reader.poll(fallback, now);
        require(old.connected && old.packet && fallback.monitor_queries == 1,
                "Non-Windows monitor discovery regressed");
        // Real idle marker path: one 4x4 grab every 100ms, immediate full-frame wake.
        Screen idle(bytes);
        idle.visible_strip = false;
        idle.marker_visible = true;
        BridgeCapture idle_reader;
        auto state = idle_reader.poll(idle, now);
        require(state.idle && state.connected && !state.packet, "Idle marker not discovered");
        idle.reads.clear();
        for (int i = 1; i <= 600; ++i) {
            state = idle_reader.poll(idle, now + std::chrono::milliseconds(i * 100));
            require(state.idle && state.connected && state.interval == std::chrono::milliseconds(100),
                    "Idle state lost");
        }
        require(idle.reads.size() == 600 && std::all_of(idle.reads.begin(), idle.reads.end(),
                                                        [](Rect r) { return r.w == 4 && r.h == 4; }),
                "Idle caused large captures/discovery");
        idle.marker_active = true;
        idle.visible_strip = true;
        idle.reads.clear();
        state = idle_reader.poll(idle, now + std::chrono::seconds(61));
        require(state.connected && !state.idle && state.packet && idle.reads.size() == 2 &&
                    idle.reads[0].w == 4 && idle.reads[1].w == 192,
                "Active marker failed to read full data in same poll");
        idle.marker_active = false;
        idle.visible_strip = false;
        idle.window->client.x = -1700;
        idle.window->client.y = 50;
        idle.reads.clear();
        state = idle_reader.poll(idle, now + std::chrono::seconds(62));
        require(state.idle && idle.reads == std::vector<Rect>{{-1699, 51, 4, 4}},
                "Moving idle window lost relative marker");
        for (int corner = 0; corner < 4; ++corner)
            for (int cell : {2, 3, 4}) {
                idle.marker_corner = corner;
                idle.cell = cell;
                idle.window->id++;
                idle.reads.clear();
                idle.left = corner & 1 ? idle.window->client.w - 6 - (cols + 1) * cell : 6 + cell;
                idle.top = corner & 2 ? idle.window->client.h - 6 - (rows + 1) * cell : 6 + cell;
                state = idle_reader.poll(idle, now + std::chrono::seconds(63));
                require(state.idle && state.connected, "Four-corner/cell idle discovery failed");
                idle.marker_active = true;
                idle.visible_strip = true;
                idle.reads.clear();
                state = idle_reader.poll(idle, now + std::chrono::seconds(64));
                require(state.packet && idle.reads.size() == 2,
                        "Four-corner/cell full frame geometry failed");
                idle.marker_active = false;
                idle.visible_strip = false;
            }
        idle.corrupt_marker = true;
        idle.reads.clear();
        state = idle_reader.poll(idle, now + std::chrono::seconds(65));
        require(!state.connected && !state.idle &&
                    std::all_of(idle.reads.begin(), idle.reads.end(),
                                [](Rect r) { return r.w <= 440 && r.h <= 120; }),
                "Invalid marker reported connected or caused desktop scan");
        idle.marker_visible = false;
        state = idle_reader.poll(idle, now + std::chrono::seconds(71));
        require(!state.connected && !state.idle, "Disabled addon marker persisted");
        // Enable idle hide after a fallback bar has already been located.
        Screen toggle(bytes);
        BridgeCapture toggle_reader;
        require(bool(toggle_reader.poll(toggle, now).packet), "Fallback toggle setup failed");
        toggle.visible_strip = false;
        toggle.marker_visible = true;
        state = toggle_reader.poll(toggle, now + std::chrono::milliseconds(100));
        require(state.idle && state.connected, "Switching idle hide on did not find corresponding marker");
        toggle.visible_strip = true;
        toggle.marker_visible = false;
        state = toggle_reader.poll(toggle, now + std::chrono::seconds(6));
        require(state.packet && !state.idle, "Switching idle hide off did not restore fallback bar capture");
        Screen mac_idle(bytes);
        mac_idle.scoped = false;
        mac_idle.visible_strip = false;
        mac_idle.marker_visible = true;
        BridgeCapture mac_reader;
        require(mac_reader.poll(mac_idle, now).idle, "Non-window marker discovery failed");

        Screen compact_screen(bytes);
        compact_screen.dense();
        compact_screen.marker_visible = true;
        compact_screen.visible_strip = false;
        BridgeCapture dense_reader;
        require(dense_reader.poll(compact_screen, now).idle, "Dense idle marker not discovered");
        for (int corner = 0; corner < 4; ++corner)
            for (int cs : {2, 3, 4}) {
                compact_screen.cell = cs;
                compact_screen.marker_corner = corner;
                compact_screen.window->id++;
                compact_screen.left =
                    corner & 1 ? compact_screen.window->client.w - 6 - (compact_cols + 1) * cs : 6 + cs;
                compact_screen.top =
                    corner & 2 ? compact_screen.window->client.h - 6 - (compact_rows + 1) * cs : 6 + cs;
                compact_screen.marker_active = false;
                compact_screen.visible_strip = false;
                require(dense_reader.poll(compact_screen, now).idle,
                        "Dense corner/cell idle detection failed");
                compact_screen.marker_active = true;
                compact_screen.visible_strip = true;
                compact_screen.reads.clear();
                auto dense = dense_reader.poll(compact_screen, now + std::chrono::milliseconds(100));
                require(dense.packet && compact_screen.reads.size() == 2 &&
                            compact_screen.reads.back().w == compact_cols * cs &&
                            compact_screen.reads.back().h == compact_rows * cs,
                        "Dense wake did not use the compact capture rectangle");
            }
        compact_screen.cell = 2;
        compact_screen.marker_corner = 0;
        compact_screen.left = compact_screen.top = 8;
        compact_screen.window->id++;
        dense_reader.poll(compact_screen, now);
        compact_screen.dense(false);
        compact_screen.reads.clear();
        require(dense_reader.poll(compact_screen, now + std::chrono::seconds(1)).packet &&
                    compact_screen.reads.back().w == 192,
                "Switching dense to native with marker lost the frame");
        compact_screen.dense();
        compact_screen.reads.clear();
        require(dense_reader.poll(compact_screen, now + std::chrono::seconds(2)).packet &&
                    compact_screen.reads.back().w == compact_cols * 2,
                "Switching native to dense with marker lost the frame");
        compact_screen.visible_strip = false;
        require(dense_reader.poll(compact_screen, now + std::chrono::seconds(3)).color_warning == false,
                "Dense warning fired before timeout");
        require(dense_reader.poll(compact_screen, now + std::chrono::seconds(6)).color_warning,
                "Persistent dense decode failure had no warning");
        compact_screen.visible_strip = true;
        require(!dense_reader.poll(compact_screen, now + std::chrono::seconds(7)).color_warning,
                "Valid dense data did not clear warning");
        compact_screen.marker_visible = false;
        BridgeCapture dense_no_marker;
        require(bool(dense_no_marker.poll(compact_screen, now).packet), "Dense data-only discovery failed");
        std::cout << "Passed " << checks << " capture assertions; no desktop reads while game is absent; "
                  << "600 steady polls used only 192x24 regions\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
