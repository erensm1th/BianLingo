#include "bianlingo/journal.hpp"
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <regex>
namespace bl {
namespace {
std::string stamp(int64_t seconds, const char *format) {
    auto t = std::time_t(seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, format);
    return out.str();
}
std::string escaped(std::string_view value) {
    std::string out;
    for (char c : value) {
        if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (uint8_t(c) < 32)
            out += " ";
        else
            out += c;
    }
    return out;
}
std::string identity(const Message &m, bool output) {
    return std::string(output                  ? "Send translation"
                       : m.event == "OCR_CHAT" ? "Chat [OCR · first observed]"
                                               : "Chat") +
           " id=" + std::to_string(m.session) + ":" + std::to_string(m.seq) +
           (m.replay_batch ? " [Replay] batch=" + std::to_string(m.replay_batch) : "");
}
} // namespace
ChatJournal::ChatJournal(std::filesystem::path directory, bool enabled, int retention_days)
    : directory_(std::move(directory)), enabled_(enabled) {
    if (retention_days < 0 || retention_days > 3650) throw std::runtime_error("Log retention must be 0–3650 days");
    queue_.push_back({std::time(nullptr), {}, 1, retention_days});
    writer_ = std::thread([this] { run(); });
    append(std::time(nullptr), "Chat log started; output translations do not mean chat was sent to the game");
}
ChatJournal::~ChatJournal() {
    close();
}
void ChatJournal::close() {
    {
        std::lock_guard lock(mutex_);
        if (closing_)
            return;
        if (enabled_) queue_.push_back({std::time(nullptr), "Chat log ended"});
        closing_ = true;
    }
    changed_.notify_one();
    if (writer_.joinable())
        writer_.join();
}
std::string ChatJournal::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}
void ChatJournal::append(int64_t when, std::string line) {
    std::lock_guard lock(mutex_);
    if (closing_ || !enabled_)
        return;
    if (bytes_ + line.size() > 4 * 1024 * 1024) {
        error_ = "The chat log queue is full; some entries were not written";
        return;
    }
    bytes_ += line.size();
    queue_.push_back({when, std::move(line)});
}
void ChatJournal::set_policy(bool enabled, int retention_days) {
    if (retention_days < 0 || retention_days > 3650) throw std::runtime_error("Log retention must be 0–3650 days");
    std::lock_guard lock(mutex_);
    if (closing_) return;
    enabled_ = enabled;
    if (!enabled) { queue_.clear(); bytes_ = 0; }
    queue_.push_back({std::time(nullptr), {}, 1, retention_days});
    changed_.notify_one();
}
void ChatJournal::clear() {
    std::lock_guard lock(mutex_);
    if (closing_) return;
    queue_.clear(); bytes_ = 0;
    queue_.push_back({std::time(nullptr), {}, 2});
    changed_.notify_one();
}
void ChatJournal::received(const Message &m, bool output) {
    if (!enabled_) return;
    append(m.received,
           identity(m, output) + " original_time=" + stamp(m.sent, "%Y-%m-%d %H:%M:%S") +
               " received_time=" + stamp(m.received, "%Y-%m-%d %H:%M:%S") + " channel=" + escaped(m.event) +
               (m.channel_number ? " #" + std::to_string(m.channel_number) : "") + " " +
               escaped(m.channel_name) + " player=" + escaped(m.author) + " original=" + escaped(m.text));
}
void ChatJournal::result(const Message &m, std::string_view state, std::string_view text, bool output) {
    if (!enabled_) return;
    append(std::time(nullptr), identity(m, output) + " outcome=" + escaped(state) + " result=" + escaped(text));
}
void ChatJournal::run() {
    std::ofstream file;
    std::string day;
    int retention = 0;
    auto purge = [&](bool all) {
        if (!std::filesystem::exists(directory_)) return;
        if (std::filesystem::is_symlink(directory_)) throw std::runtime_error("The log directory cannot be a link");
        const std::regex owned(R"(^chat-([0-9]{4}-[0-9]{2}-[0-9]{2})\.log$)");
        auto cutoff = stamp(std::time(nullptr) - int64_t(retention) * 86400, "%Y-%m-%d");
        for (const auto &entry : std::filesystem::directory_iterator(directory_)) {
            std::smatch match; auto name = entry.path().filename().string();
            if (!entry.is_symlink() && entry.is_regular_file() && std::regex_match(name, match, owned) &&
                (all || (retention && match[1].str() < cutoff))) std::filesystem::remove(entry.path());
        }
    };
    for (;;) {
        std::deque<Line> batch;
        bool done;
        {
            std::unique_lock lock(mutex_);
            changed_.wait_for(lock, std::chrono::seconds(1), [&] {
                return closing_ || std::any_of(queue_.begin(), queue_.end(), [](const Line &l) { return l.command != 0; });
            });
            batch.swap(queue_);
            bytes_ = 0;
            done = closing_;
        }
        try {
            for (const auto &line : batch) {
                if (line.command) {
                    file.close(); day.clear();
                    if (line.command == 1) retention = line.retention_days;
                    purge(line.command == 2);
                    continue;
                }
                auto date = stamp(line.when, "%Y-%m-%d");
                if (date != day || !file.is_open()) {
                    file.close();
                    file.clear();
                    if (retention) purge(false);
                    std::filesystem::create_directories(directory_);
                    file.open(directory_ / ("chat-" + date + ".log"), std::ios::binary | std::ios::app);
                    if (!file)
                        throw std::runtime_error("Could not open the log file");
                    day = date;
                }
                file << "[" << stamp(line.when, "%Y-%m-%d %H:%M:%S") << "] " << line.text << "\n";
                if (!file)
                    throw std::runtime_error("Could not write the log file");
            }
            if (file.is_open()) {
                file.flush();
                if (!file)
                    throw std::runtime_error("Could not flush the log file");
            }
        } catch (const std::exception &e) {
            file.close();
            day.clear();
            std::string detail = e.what();
            try {
                utf32(detail);
            } catch (...) {
                detail = "Check log directory permissions and free disk space";
            }
            std::lock_guard lock(mutex_);
            error_ = std::string("Chat log write failed: ") + detail;
        }
        if (done)
            break;
    }
}
} // namespace bl
