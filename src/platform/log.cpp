#include "platform/log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

namespace sp::log {
namespace {

constexpr size_t kRingSize = 500;

std::mutex g_mutex;
FILE* g_file = nullptr;
bool g_verbose = false;
uint64_t g_seq = 0;
std::deque<Entry> g_ring;
const auto g_start = std::chrono::steady_clock::now();

double elapsed() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
}

}  // namespace

const char* levelName(Level level) {
    switch (level) {
        case Level::Debug: return "debug";
        case Level::Info: return "info";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
    }
    return "?";
}

void init(const std::string& filePath, bool verbose) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_verbose = verbose;
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    if (!filePath.empty()) {
        std::rename(filePath.c_str(), (filePath + ".old").c_str());  // ok if missing
        g_file = std::fopen(filePath.c_str(), "w");
        if (!g_file) std::fprintf(stderr, "[log] cannot open log file %s\n", filePath.c_str());
    }
}

void setVerbose(bool verbose) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_verbose = verbose;
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void write(Level level, const char* fmt, ...) {
    std::string text;
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (n > 0) {
        text.resize(static_cast<size_t>(n));
        std::vsnprintf(text.data(), text.size() + 1, fmt, args);
    }
    va_end(args);

    const double t = elapsed();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (level != Level::Debug || g_verbose)
        std::fprintf(stderr, "[%8.3f] %-5s %s\n", t, levelName(level), text.c_str());
    if (g_file) {
        // Flush every line so the log survives a driver crash.
        std::fprintf(g_file, "[%8.3f] %-5s %s\n", t, levelName(level), text.c_str());
        std::fflush(g_file);
    }
    if (level != Level::Debug) {
        g_ring.push_back(Entry{++g_seq, level, t, std::move(text)});
        if (g_ring.size() > kRingSize) g_ring.pop_front();
    }
}

std::vector<Entry> recent(size_t maxEntries) {
    std::lock_guard<std::mutex> lock(g_mutex);
    size_t start = g_ring.size() > maxEntries ? g_ring.size() - maxEntries : 0;
    return std::vector<Entry>(g_ring.begin() + static_cast<long>(start), g_ring.end());
}

uint64_t lastSeq() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_seq;
}

}  // namespace sp::log
