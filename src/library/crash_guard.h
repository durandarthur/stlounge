#pragma once
// Crash guard: before a shader starts, a small "pending" file names it, and it is
// removed once the shader has run for a little while. If the app (or the whole
// desktop) dies in between, the file is still there on the next start, so that
// shader can be skipped instead of crashing again.

#include <filesystem>
#include <optional>
#include <string>

namespace sp {

namespace fs = std::filesystem;

struct PendingShader {
    std::string libraryId;  // "" for a shader that isn't in the library
    std::string name;
};

class CrashGuard {
public:
    explicit CrashGuard(fs::path file) : file_(std::move(file)) {}

    // The shader that was starting when the previous run died, if any. Removes the
    // file, so it is reported once. Call before the first arm().
    std::optional<PendingShader> takeLeftover();

    // Marks `shader` as starting (written durably). Replaces an earlier mark.
    void arm(const PendingShader& shader);
    // The shader started fine (or the app is closing normally).
    void disarm();
    bool armed() const { return armed_; }

private:
    fs::path file_;
    bool armed_ = false;
};

}  // namespace sp
