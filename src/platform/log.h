#pragma once
// Thread-safe logger. Writes to stderr, to a log file (all levels), and keeps
// recent Info+ messages in memory for the UI's log panel.

#include <cstdint>
#include <string>
#include <vector>

namespace sp::log {

enum class Level { Debug, Info, Warn, Error };

struct Entry {
    uint64_t seq;     // increasing id, lets the UI detect new entries
    Level level;
    double time;      // seconds since program start
    std::string text;
};

// filePath may be empty (no log file). An existing file is kept as "<file>.old".
void init(const std::string& filePath, bool verbose);
void setVerbose(bool verbose);
void shutdown();

void write(Level level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Copy of the most recent entries (Info and above), oldest first.
std::vector<Entry> recent(size_t maxEntries = 300);
uint64_t lastSeq();

const char* levelName(Level level);

}  // namespace sp::log

#define LOG_DEBUG(...) ::sp::log::write(::sp::log::Level::Debug, __VA_ARGS__)
#define LOG_INFO(...) ::sp::log::write(::sp::log::Level::Info, __VA_ARGS__)
#define LOG_WARN(...) ::sp::log::write(::sp::log::Level::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::sp::log::write(::sp::log::Level::Error, __VA_ARGS__)
