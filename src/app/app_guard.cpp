// App: crash guard (skip a shader that took the app down) and frame-time guard
// (skip a shader so slow on this GPU that it freezes the desktop).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#include "app/app.h"
#include "app/demos.h"
#include "gfx/gl.h"
#include "platform/log.h"

namespace sp {

namespace {

constexpr int kStableFrames = 30;          // the crash guard stays armed this many frames...
constexpr double kStableSeconds = 1.0;     // ...and at least this long
constexpr int kSlowFramesToSkip = 2;       // consecutive measured frames over the limit
constexpr int kWarmupFrames = 1;           // the first frame may include driver set-up

std::string formatMs(double ms) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f", ms);
    return buf;
}

}  // namespace

void App::handleCrashLeftover(const PendingShader& p) {
    const std::string name = p.name.empty() ? "a shader" : "'" + p.name + "'";
    const LibraryEntry* e = p.libraryId.empty() ? nullptr : library_->find(p.libraryId);
    std::string message;
    if (e) {
        std::string error;
        if (!library_->setCrashed(p.libraryId, true, &error)) LOG_WARN("crash guard: %s", error.c_str());
        message = "The app closed unexpectedly while " + name +
                  " was starting (a driver crash or a frozen desktop?). It is skipped from now on; click it in the "
                  "Library to try it again.";
    } else {
        message = "The app closed unexpectedly while " + name + " was starting.";
    }
    LOG_WARN("crash guard: %s", message.c_str());
    errorLog_ = "=== " + (p.name.empty() ? std::string("Previous run") : p.name) + " ===\n" + message;
    setStatus(message, true);
}

void App::markCrashedAsFailed() {
    for (const std::string& id : player_.items())
        if (const LibraryEntry* e = library_->find(id); e && e->crashed) player_.markFailed(id);
}

void App::updateCrashGuard() {
    if (!crashGuard_ || !crashGuard_->armed() || !current_ || armedSerial_ != currentSerial_) return;
    if (framesSinceStart_ >= kStableFrames && clock_.time() >= kStableSeconds) crashGuard_->disarm();
}

void App::beginFrameMeasure() {
    if (!measureNextFrame_ || guardExempt_ || outgoing_ || paused_ || settings_.frameTimeGuardMs <= 0) return;
    if (framesSinceStart_ < kWarmupFrames) return;
    guardStart_ = std::chrono::steady_clock::now();
    measuring_ = true;
}

void App::endFrameMeasure() {
    if (!measuring_) return;
    measuring_ = false;
    if (!current_) return;
    // Wait for the GPU without spinning: some drivers busy-wait inside glClientWaitSync.
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!fence) return;
    glFlush();
    const double limit = settings_.frameTimeGuardMs;
    double ms = 0;
    for (;;) {
        const GLenum r = glClientWaitSync(fence, 0, 0);
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - guardStart_).count();
        if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED || r == GL_WAIT_FAILED) break;
        if (ms > 2.0 * limit) break;  // slow enough; no need to wait for the end
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    glDeleteSync(fence);
    LOG_DEBUG("frame-time guard: frame %d took %.0f ms on the GPU", framesSinceStart_, ms);
    if (ms <= limit) {
        slowFrames_ = 0;
        return;
    }
    if (++slowFrames_ >= kSlowFramesToSkip) abandonSlowShader(ms);
}

void App::abandonSlowShader(double ms) {
    const std::string name = current_->shader().info.name;
    const std::string message = "Skipped '" + name + "': a frame takes about " + formatMs(ms) +
                                " ms on this GPU, over the " + std::to_string(settings_.frameTimeGuardMs) +
                                " ms limit (Settings > Advanced). A lower render scale may help.";
    LOG_WARN("frame-time guard: %s", message.c_str());
    errorLog_ = "=== " + name + " ===\n" + message;
    setStatus(message, true);
    showToast("Too slow on this GPU, skipped");

    if (!currentLibraryId_.empty()) player_.markFailed(currentLibraryId_);
    if (crashGuard_ && armedSerial_ == currentSerial_) crashGuard_->disarm();
    current_.reset();  // stop drawing it right away
    currentLibraryId_.clear();
    slowFrames_ = 0;
    if (player_.playableCount() > 0)
        skipRequested_ = +1;
    else
        loadShader(makeTestPattern());
}

}  // namespace sp
