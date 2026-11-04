#pragma once
#include "bianlingo/core.hpp"
namespace bl {
// File I/O runs exclusively on the writer thread. Producers never open files.
class ChatJournal {
    struct Line {
        int64_t when;
        std::string text;
        int command = 0;
        int retention_days = 0;
    };
    std::filesystem::path directory_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Line> queue_;
    size_t bytes_ = 0;
    bool closing_ = false;
    std::atomic<bool> enabled_{true};
    std::string error_;
    std::thread writer_;
    void run();
    void append(int64_t when, std::string line);

  public:
    explicit ChatJournal(std::filesystem::path directory, bool enabled = true, int retention_days = 0);
    ~ChatJournal();
    void close();
    void received(const Message &, bool output = false);
    void result(const Message &, std::string_view state, std::string_view text, bool output = false);
    std::string error() const;
    void set_policy(bool enabled, int retention_days);
    void clear();
};
} // namespace bl
