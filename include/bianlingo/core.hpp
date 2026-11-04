#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bl {
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;
// Apple libc++ deployments do not uniformly expose C++20 jthread/stop_token.
// Keep cooperative shutdown on portable std::thread + condition_variable.
struct StopState {
    std::atomic<bool> stopped{false};
    std::mutex mutex;
    std::condition_variable changed;
};
class StopToken {
    std::shared_ptr<StopState> state_;

  public:
    StopToken() = default;
    explicit StopToken(std::shared_ptr<StopState> state) : state_(std::move(state)) {}
    bool stop_requested() const {
        return state_ && state_->stopped.load();
    }
    void wait_for(std::chrono::milliseconds duration) const {
        if (!state_) { std::this_thread::sleep_for(duration); return; }
        std::unique_lock lock(state_->mutex);
        state_->changed.wait_for(lock, duration, [&] { return stop_requested(); });
    }
};
class Worker {
    std::shared_ptr<StopState> state_ = std::make_shared<StopState>();
    std::thread thread_;

  public:
    explicit Worker(std::function<void(StopToken)> work)
        : thread_([state = state_, work = std::move(work)] { work(StopToken(state)); }) {}
    Worker(Worker &&) noexcept = default;
    Worker(const Worker &) = delete;
    ~Worker() {
        request_stop();
        if (thread_.joinable())
            thread_.join();
    }
    void request_stop() {
        if (state_) {
            {
                std::lock_guard lock(state_->mutex);
                state_->stopped = true;
            }
            state_->changed.notify_all();
        }
    }
};
inline constexpr int cols = 96, rows = 12, capacity = 368;
inline constexpr int compact_cols = 40, compact_rows = 10;
inline int grid_cols(bool compact) {
    return compact ? compact_cols : cols;
}
inline int grid_rows(bool compact) {
    return compact ? compact_rows : rows;
}
inline constexpr std::string_view sync_bits =
    "111100001010110011001010001111010101001110000101110010100110101011110000110101001001011000111001";
inline constexpr std::array<std::string_view, 24> events = {"",
                                                            "CHAT_MSG_SAY",
                                                            "CHAT_MSG_YELL",
                                                            "CHAT_MSG_WHISPER",
                                                            "CHAT_MSG_WHISPER_INFORM",
                                                            "CHAT_MSG_PARTY",
                                                            "CHAT_MSG_PARTY_LEADER",
                                                            "CHAT_MSG_RAID",
                                                            "CHAT_MSG_RAID_LEADER",
                                                            "CHAT_MSG_RAID_WARNING",
                                                            "CHAT_MSG_INSTANCE_CHAT",
                                                            "CHAT_MSG_INSTANCE_CHAT_LEADER",
                                                            "CHAT_MSG_GUILD",
                                                            "CHAT_MSG_OFFICER",
                                                            "CHAT_MSG_CHANNEL",
                                                            "CHAT_MSG_BN_WHISPER",
                                                            "CHAT_MSG_BN_WHISPER_INFORM",
                                                            "CHAT_MSG_COMMUNITIES_CHANNEL",
                                                            "BRIDGE_READY",
                                                            "BRIDGE_TEST",
                                                            "BRIDGE_REPLAY_BEGIN",
                                                            "BRIDGE_REPLAY_END",
                                                            "BRIDGE_CHAT_LAYOUT",
                                                            "BRIDGE_BLOCKLIST"};
inline constexpr std::array<std::string_view, 14> classes = {
    "",       "WARRIOR", "PALADIN", "HUNTER", "ROGUE", "PRIEST",      "DEATHKNIGHT",
    "SHAMAN", "MAGE",    "WARLOCK", "MONK",   "DRUID", "DEMONHUNTER", "EVOKER"};
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const Rect &) const = default;
};
// BGRA, top-down, stride in bytes. Capture adapters own/reuse storage.
struct Image {
    int width = 0, height = 0, stride = 0;
    Bytes pixels;
};
struct Location {
    double x = 0, y = 0, cell = 2;
    bool compact = false;
};
// BLG2 player byte: bits 0–4 class, bit 5 confirmed same realm, bit 6 RGB, bit 7 metadata.
inline constexpr uint8_t player_class_mask = 31, player_same_realm = 32;
struct Packet {
    uint32_t session = 0, seq = 0, sent = 0;
    uint16_t part = 0, count = 0;
    uint8_t event = 0, player = 0;
    Bytes payload;
    uint16_t replay_batch = 0;
};
struct Message {
    uint32_t session = 0, seq = 0, sent = 0;
    std::string event, author, text, player_class, localized_class, race, english_race, player_name, realm,
        channel_name;
    int sex = 0, channel_number = 0;
    uint16_t replay_batch = 0;
    uint64_t session_order = 0, receive_order = 0;
    int64_t received = 0;
    std::optional<uint32_t> chat_rgb;
    bool same_realm = false; // Unknown / old senders must retain the full whisper identity.
};
uint32_t adler(std::span<const uint8_t> bytes);
std::optional<Packet> decode_packet(std::span<const uint8_t> bytes);
std::optional<Packet> decode_image(const Image &, Location);
std::optional<Location> locate(const Image &);
class Assembler {
    struct Entry {
        Packet first;
        std::map<uint16_t, Bytes> parts;
    };
    using Key = std::pair<uint64_t, uint16_t>;
    std::map<Key, Entry> pending_;
    std::deque<Key> pending_order_;
    // Merge adjacent completed sequence numbers instead of expiring identities.
    // Long sessions retain exact replay dedup without one allocation per chat.
    std::map<uint32_t, std::map<uint32_t, uint32_t>> seen_;
    void remember(const Packet &);

  public:
    bool seen(const Packet &) const;
    void clear_partial() {
        pending_.clear();
        pending_order_.clear();
    }
    std::optional<Message> accept(const Packet &);
};
class ReplayTracker {
    struct Batch {
        size_t total = 0;
        bool ended = false;
        std::set<uint32_t> observed, added;
    };
    std::map<std::pair<uint32_t, uint16_t>, Batch> batches_;
    std::deque<std::pair<uint32_t, uint16_t>> order_;

  public:
    std::optional<std::string> observe(const Packet &, const std::optional<Message> &, const Assembler &);
};
std::u32string utf32(std::string_view);
std::string utf8(std::u32string_view);
std::string trim(std::string);
std::string lower_ascii(std::string);
std::string clean_chat(std::string_view);
std::string prose_text(std::string_view);
bool should_translate(std::string_view);
bool nickname_uses_target_script(const Message &, std::string_view language);
bool skip_nickname_translation(const Json &settings, const Message &);
bool normalize_nickname_filter(Json &settings);
std::string script_language(std::string_view);
bool allows(const Json &, const Message &);
struct Term {
    size_t start = 0, end = 0;
    std::string original, translation;
};
class Glossary {
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> index_;
    std::unordered_map<std::string, bool> ambiguous_, uppercase_;

  public:
    explicit Glossary(const Json &data = Json::object());
    std::vector<Term> matches(std::string_view) const;
    std::optional<std::string> local(std::string_view, const std::vector<Term> &) const;
};
std::string canonical(const Json &);
Bytes unbase64(std::string_view);
std::string base64(std::span<const uint8_t>);
std::string sha256(std::span<const uint8_t>);
std::string sha256(std::string_view);
Bytes random_bytes(size_t);
Json default_settings();
bool normalize_outbound_prompt(Json &settings);
bool normalize_locale_prompts(Json &settings);
std::string outbound_prompt(const Json &settings, std::string_view style, std::string_view language);
std::optional<std::string> local_outbound_translation(const Json &settings, std::string_view text,
                                                      std::string_view target, const Json &terms);
Json read_json(const std::filesystem::path &, const Json &fallback = Json::object());
void write_json(const std::filesystem::path &, const Json &);
void atomic_write(const std::filesystem::path &, std::span<const uint8_t>);
Bytes read_bytes(const std::filesystem::path &);

namespace platform {
bool is_letter(char32_t);
std::string user_locale();
std::filesystem::path app_data();
void initialize_storage();
std::filesystem::path executable_dir();
std::filesystem::path package_root();
Bytes protect(std::span<const uint8_t>, bool decrypt = false);
std::string hardware_id();
Json http_json(std::string_view url, const Json &body, std::string_view bearer = {}, bool get = false,
               const std::map<std::string, std::string> &headers = {}, StopToken stop = {});
class Capture {
  public:
    struct GameWindow {
        uintptr_t id = 0;
        Rect client;
    };
    virtual ~Capture() = default;
    // A missing window is a pause, not permission to search the desktop.
    virtual bool window_scoped() const {
        return false;
    }
    virtual std::optional<GameWindow> game_window() {
        return std::nullopt;
    }
    virtual std::vector<Rect> monitors() = 0;
    virtual const Image &grab(Rect) = 0;
};
std::unique_ptr<Capture> make_capture();
} // namespace platform
struct Marker {
    int x = 0, y = 0, cell = 2, corner = 0;
    bool active = false;
    uint16_t hint = 0;
    bool compact = false;
    uint8_t chat_state = 0; // 0 unknown, 1 readable, 2 restricted.
};
std::optional<Marker> decode_marker(const Image &, int x = 0, int y = 0);
std::optional<Marker> locate_marker(const Image &);
Location marker_location(const Marker &);
struct DisplayMessage {
    Message message;
    std::string translation, source, error;
};
// Returns false when an old insertion is immediately outside the display limit.
struct HistoryChange {
    bool retained;
    size_t position, removed;
};
HistoryChange update_history(std::deque<DisplayMessage> &, DisplayMessage, size_t limit);
bool insert_history(std::deque<DisplayMessage> &, DisplayMessage, size_t limit);
struct BalanceView {
    std::string amount = "—", detail, state = "Not queried yet", updated;
    bool available = false;
};
struct UsageView {
    std::string total, detail;
};
BalanceView format_balance(const Json &, int64_t queried_at);
UsageView format_usage(const Json &, std::string_view language = "en");
struct Notice {
    enum Kind {
        Chat,
        Status,
        Outbound,
        OutboundError,
        Balance,
        BalanceError,
        ChatLayout,
        Blocklist,
        ProviderTest,
        ProviderTestError
    } kind = Status;
    DisplayMessage chat;
    std::string text;
    std::shared_ptr<const BalanceView> balance;
    std::shared_ptr<const Json> layout;
};
class ChatJournal;
class PlayerBlocklist;
struct ChatTab;
class Engine {
    struct Job {
        Message message;
        std::string target;
        uint64_t generation = 0;
        std::string style = "short";
    };
    mutable std::mutex mutex_;
    std::condition_variable_any jobs_changed_;
    std::deque<Job> jobs_;
    std::deque<Notice> notices_;
    std::vector<Worker> workers_;
    std::filesystem::path data_directory_;
    Json settings_, usage_, connections_;
    std::mutex configure_mutex_;
    bool settings_backup_required_ = false;
    void backup_recovered_settings();
    std::vector<ChatTab> display_tabs_;
    std::string api_key_;
    Glossary glossary_;
    Json outbound_terms_;
    std::unique_ptr<ChatJournal> journal_;
    std::unique_ptr<PlayerBlocklist> blocklist_;
    std::unordered_map<std::string, std::string> cache_;
    std::unordered_map<std::string, std::shared_future<std::string>> inflight_;
    std::deque<std::string> cache_order_;
    std::atomic<bool> connected_{false}, idle_{false};
    uint32_t output_seq_ = 0, output_session_ = 0;
    uint64_t generation_ = 0;
    bool outbound_pending_ = false;
    std::atomic<int> chat_state_{0};
    std::atomic<uint64_t> ocr_epoch_{0};
    std::atomic<uintptr_t> game_window_{0};
    std::atomic<bool> ocr_test_{false}, ocr_install_{false}, ocr_pure_{false};
    mutable std::mutex ocr_mutex_;
    std::deque<std::pair<int64_t, std::string>> recent_text_;
    std::string ocr_status_ = "OCR fallback is disabled", ocr_preview_;
    void ocr_loop(StopToken);
    void capture_loop(StopToken);
    void worker(StopToken);
    std::string translate(const Job &, std::string &source, StopToken);
    std::string provider_call(const Json &, std::string_view system, std::string_view text,
                              bool incoming = true, std::string_view target = "English",
                              std::string_view chat_language = {}, StopToken stop = {});
    void notice(Notice, std::optional<uint64_t> generation = std::nullopt);
    void update_blocklist(const std::function<bool(PlayerBlocklist &)> &change);

  public:
    explicit Engine(std::filesystem::path directory = platform::app_data());
    ~Engine();
    void start();
    void stop();
    Json settings() const;
    void configure(Json settings, std::optional<std::string> key = std::nullopt,
                   std::optional<Json> connections = std::nullopt);
    Json connections() const;
    bool has_balance() const;
    void test_connection();
    bool has_key() const;
    void submit(Message);
    void outbound(std::string, std::string target = "English");
    void query_balance();
    void test_ocr();
    void install_ocr_models();
    std::string ocr_status() const;
    std::string ocr_preview() const;
    std::vector<Notice> drain();
    std::string status() const;
    Json usage() const;
    void clear_chat_logs();
    Json blocklist() const;
    void merge_blocklist(const Json &snapshot);
    void set_player_blocked(std::string name, bool blocked);
    bool player_blocked(const Message &message) const;
    uintptr_t game_window() const { return game_window_.load(); }
};
} // namespace bl
