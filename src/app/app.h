#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/hotkeys.h"
#include "audio/analyser.h"
#include "audio/capture.h"
#include "gfx/audio_texture.h"
#include "gfx/gl_caps.h"
#include "gfx/neutral_textures.h"
#include "gfx/render_target.h"
#include "gfx/texture_library.h"
#include "library/library.h"
#include "platform/log.h"
#include "platform/paths.h"
#include "platform/window.h"
#include "shadertoy/mouse.h"
#include "shadertoy/renderer.h"
#include "shadertoy/uniforms.h"
#include "ui/overlay.h"
#include "ui/file_browser.h"
#include "ui/paste_dialog.h"

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

    // libraryId: the library entry being played ("" for demos / unsaved shaders).
    void loadShader(const Shader& shader, const std::string& libraryId = "");
    void updateLoading();
    void ensureImageTarget(int drawableW, int drawableH);
    void renderFrame(double dt);
    void updateAudio(double now);

    // Library and sources (app_library.cpp)
    void playFromLibrary(const std::string& id);
    void importFile(const std::string& path);
    void importText(const std::string& text, const std::string& what);
    void openPasteDialog(const std::string& clipboard);
    void setStatus(const std::string& message, bool error = false);

    // UI (app_ui.cpp)
    void drawUi();
    void drawInfoPanel();
    void drawCurrentShaderInfo();
    void drawLibraryPanel();
    void drawPasteDialog();
    void drawErrors();
    void drawAudioSection();
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
    AudioTexture audioTexture_;
    Blitter blitter_;
    RenderContext renderContext_;
    RenderTarget imageTarget_;  // Image pass output at render resolution
    std::unique_ptr<ShaderRenderer> current_, pending_;
    std::string currentLibraryId_, pendingLibraryId_;
    PlaybackClock clock_;
    MouseState mouse_;
    float renderScale_ = 1.0f;
    bool paused_ = false;
    bool redrawOnce_ = false;  // render a frame even while paused (resize, new shader)

    // Audio: captured only while the current shader has an audio input
    AudioCapture audio_;
    SpectrumAnalyser analyser_;
    std::string audioDevice_;          // "" = monitor of the default output
    double audioUnusedSince_ = -1.0;   // stop capture 5 s after the last use
    double audioFailedAt_ = -1.0;      // retry 10 s after a failure
    bool audioTextureSilent_ = true;
    uint8_t audioFrequency_[SpectrumAnalyser::kBins] = {};
    std::vector<AudioSource> audioSources_;
    std::string audioListError_;
    bool audioSourcesListed_ = false;

    // UI state
    bool running_ = true;
    bool uiVisible_ = true;
    bool showLibrary_ = true;
    std::string errorLog_;

    // Library and sources
    std::unique_ptr<ShaderLibrary> library_;
    PasteDialog pasteDialog_;
    FileBrowser fileBrowser_;
    std::string libraryFilter_;
    std::string deleteCandidate_;  // id waiting for delete confirmation
    std::string status_;
    bool statusIsError_ = false;
    double statusTime_ = 0;

    // Frame timing
    double fps_ = 0.0, frameMs_ = 0.0;
    uint64_t fpsWindowStart_ = 0;
    int fpsFrames_ = 0;
};

}  // namespace sp
