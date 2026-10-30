#pragma once

#include <cstdint>

#include "app/hotkeys.h"
#include "gfx/gl_caps.h"
#include "platform/paths.h"
#include "platform/window.h"
#include "ui/overlay.h"

namespace sp {

struct AppOptions {
    bool verbose = false;
    bool safeMode = false;  // don't auto-load the last shader on startup
};

class App {
public:
    int run(const AppOptions& opts);

private:
    bool startup(const AppOptions& opts);
    void handleEvent(const SDL_Event& e);
    void doAction(Action a);
    void setUiVisible(bool visible);
    void renderFrame(double now);
    void drawUi();
    void limitFrameRate(uint64_t frameStart);

    AppOptions opts_;
    AppPaths paths_;
    Window window_;
    Overlay overlay_;
    GlCaps caps_;

    bool running_ = true;
    bool uiVisible_ = true;

    // Frame timing
    double fps_ = 0.0, frameMs_ = 0.0;
    uint64_t fpsWindowStart_ = 0;
    int fpsFrames_ = 0;
};

}  // namespace sp
