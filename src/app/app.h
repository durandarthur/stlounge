#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "app/hotkeys.h"
#include "gfx/gl_caps.h"
#include "gfx/neutral_textures.h"
#include "gfx/render_target.h"
#include "gfx/texture_library.h"
#include "platform/log.h"
#include "platform/paths.h"
#include "platform/window.h"
#include "shadertoy/mouse.h"
#include "shadertoy/renderer.h"
#include "shadertoy/uniforms.h"
#include "ui/overlay.h"
#include "ui/shader_editor.h"

namespace sp {

struct AppOptions {
    bool verbose = false;
    bool safeMode = false;  // don't auto-load the last shader on startup
};

class App {
public:
    ~App() { log::shutdown(); }
    int run(const AppOptions& opts);

private:
    bool startup(const AppOptions& opts);
    void handleEvent(const SDL_Event& e);
    void handleMouse(const SDL_Event& e);
    void doAction(Action a);
    void setUiVisible(bool visible);

    void loadShader(const Shader& shader);
    void updateLoading();
    void ensureImageTarget(int drawableW, int drawableH);
    void renderFrame(double dt);

    void drawUi();
    void drawInfoPanel();
    void drawEditor();
    void drawErrors();
    void limitFrameRate(uint64_t frameStart);

    AppOptions opts_;
    AppPaths paths_;
    Window window_;
    Overlay overlay_;
    GlCaps caps_;

    // Rendering
    FullscreenTriangle triangle_;
    NeutralTextures neutral_;
    TextureLibrary textures_;
    Blitter blitter_;
    RenderContext renderContext_;
    RenderTarget imageTarget_;  // Image pass output at render resolution
    std::unique_ptr<ShaderRenderer> current_, pending_;
    PlaybackClock clock_;
    MouseState mouse_;
    float renderScale_ = 1.0f;
    bool paused_ = false;
    bool redrawOnce_ = false;  // render a frame even while paused (resize, new shader)

    // UI state
    bool running_ = true;
    bool uiVisible_ = true;
    bool showEditor_ = true;
    ShaderEditor editor_;
    std::string errorLog_;

    // Frame timing
    double fps_ = 0.0, frameMs_ = 0.0;
    uint64_t fpsWindowStart_ = 0;
    int fpsFrames_ = 0;
};

}  // namespace sp
