#pragma once

#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <set>
#include <vector>

#include "app/hotkeys.h"
#include "audio/analyser.h"
#include "audio/capture.h"
#include "config/settings.h"
#include "gfx/audio_texture.h"
#include "gfx/gl_caps.h"
#include "gfx/neutral_textures.h"
#include "gfx/render_target.h"
#include "gfx/texture_library.h"
#include "library/crash_guard.h"
#include "library/library.h"
#include "library/playlist_player.h"
#include "library/playlists.h"
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

// How a shader load came about; decides crossfades and auto-skip.
struct LoadRequest {
    std::string libraryId;      // "" for demos / unsaved shaders
    bool fromPlaylist = false;  // failures are skipped in the playlist
    int direction = 1;          // where to skip to: +1 next, -1 previous, 0 random
    bool isSkip = false;        // replaces a shader that just failed
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

    void loadShader(const Shader& shader, const LoadRequest& req = {});
    void updateLoading();
    void failPending(std::string log);  // by value: it outlives pending_

    // Crash guard and frame-time guard (app_guard.cpp)
    void handleCrashLeftover(const PendingShader& p);
    void markCrashedAsFailed();             // keeps crashed shaders skipped in the playlist
    void updateCrashGuard();                // disarms once the current shader has run a while
    void beginFrameMeasure();               // before the current shader renders
    void endFrameMeasure();                 // waits for the GPU, then checks the time
    void abandonSlowShader(double ms);
    void ensureImageTarget(int drawableW, int drawableH);
    void renderFrame(double dt);
    void renderShader(ShaderRenderer& r, PlaybackClock& clock, const MouseState& mouse, const RenderTarget& target,
                      bool advance, double dt);
    void updateAudio(double now);
    void limitFrameRate();

    // Playlist (app_playlist.cpp)
    std::vector<std::string> playlistIds() const;
    std::string playlistLabel(const std::string& name) const;
    void setActivePlaylist(const std::string& name);
    void rebuildPlaylist();
    void playlistStep(int direction, bool isSkip = false);  // +1 next, -1 previous, 0 random
    void updateAutoAdvance(double dt);
    void setShuffle(bool on);
    void setLocked(bool on);
    void saveSessionNow();
    void drawPlaylistControls();
    void drawPlaylistDialogs();

    // Settings, screenshots (app_settings.cpp)
    void loadSettingsAtStartup();
    void applyTextureOverride(const std::string& id, const std::string& path);
    void saveSettingsIfChanged(bool force);
    void drawSettingsPanel();
    void takeScreenshot();
    void pollScreenshot();

    // Library and sources (app_library.cpp)
    void playFromLibrary(const std::string& id, const LoadRequest& req = {});
    struct ImportStats {
        int imported = 0, failed = 0, warned = 0;
        std::string firstId, lastError;
    };
    void importFiles(const std::vector<std::string>& paths);
    void importText(const std::string& text, const std::string& what, ImportStats& stats);
    void reportImport(const ImportStats& stats, const std::string& what);
    void openPasteDialog(const std::string& clipboard);
    void setStatus(const std::string& message, bool error = false);
    // A short message at the top of the window, shown even with the UI hidden.
    void showToast(std::string text);
    bool toastVisible() const;
    void drawToast();

    // UI (app_ui.cpp)
    void drawUi();
    void drawInfoPanel();
    void drawCurrentShaderInfo();
    void drawLibraryPanel();
    void drawPasteDialog();
    void drawErrors();
    void drawAudioStatus();
    void drawAudioSourcePicker();

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
    std::string currentLibraryId_;
    LoadRequest pendingRequest_;
    LoadRequest currentRequest_;
    std::chrono::steady_clock::time_point pendingStart_;
    PlaybackClock clock_;
    MouseState mouse_;
    bool paused_ = false;

    // Crossfade: the previous shader keeps rendering into its own target until the fade ends.
    std::unique_ptr<ShaderRenderer> outgoing_;
    RenderTarget outgoingTarget_;
    PlaybackClock outgoingClock_;
    double fadeElapsed_ = 0, fadeDuration_ = 0;
    bool redrawOnce_ = false;  // render a frame even while paused (resize, new shader)

    // Audio: captured only while the current shader has an audio input
    AudioCapture audio_;
    SpectrumAnalyser analyser_;
    double audioUnusedSince_ = -1.0;   // stop capture 5 s after the last use
    double audioFailedAt_ = -1.0;      // retry 10 s after a failure
    bool audioTextureSilent_ = true;
    uint8_t audioFrequency_[SpectrumAnalyser::kBins] = {};
    std::vector<AudioSource> audioSources_;
    std::string audioListError_;
    bool audioSourcesListed_ = false;

    // Settings and session
    Settings settings_;
    Settings savedSettings_;           // what's on disk
    double settingsChangedAt_ = -1;    // saved one second after the last change
    fs::path settingsPath_, sessionPath_;
    std::string lastSavedSessionShader_, lastSavedSessionPlaylist_;

    // Playlist
    std::unique_ptr<PlaylistStore> playlists_;
    PlaylistPlayer player_;
    std::string activePlaylist_;       // "" = all shaders, kFavoritesPlaylist, or a name
    bool playlistDirty_ = true;        // rebuild the player's items next frame
    double shaderElapsed_ = 0;         // seconds the current shader has played
    int skipRequested_ = 2;            // 2 = none, else the direction to skip to
    enum class NameDialog { None, New, Rename };
    NameDialog nameDialog_ = NameDialog::None;
    std::string nameDialogText_, nameDialogError_, nameDialogAddId_;
    bool deletePlaylistAsked_ = false;
    std::string durationEditId_;
    int durationEditValue_ = 0;
    std::map<std::string, std::string> textureEdits_;  // settings panel text boxes

    // Screenshot written in the background; result message when done.
    std::future<std::string> screenshot_;

    // UI state
    bool running_ = true;
    bool uiVisible_ = true;
    bool showLibrary_ = true;
    bool showSettings_ = false;
    std::string errorLog_;

    // Library and sources
    std::unique_ptr<ShaderLibrary> library_;
    PasteDialog pasteDialog_;
    FileBrowser fileBrowser_;
    std::string libraryFilter_;
    std::vector<std::string> deleteCandidates_;  // ids waiting for delete confirmation
    bool selectMode_ = false;                    // library: tick entries for bulk delete
    std::set<std::string> selected_;
    std::vector<std::string> droppedFiles_;      // collected during one event batch
    std::string status_;
    bool statusIsError_ = false;
    double statusTime_ = 0;
    std::string toast_;
    double toastTime_ = -1e9;

    // Crash guard: the marker names the shader of load number armedSerial_.
    std::unique_ptr<CrashGuard> crashGuard_;
    uint64_t loadSerial_ = 0, pendingSerial_ = 0, currentSerial_ = 0, armedSerial_ = 0;
    int framesSinceStart_ = 0;  // frames the current shader has rendered

    // Frame-time guard: the GPU time of a frame is measured only after a slow frame,
    // so normal frames don't wait for the GPU.
    bool measureNextFrame_ = false;
    bool measuring_ = false;
    std::chrono::steady_clock::time_point guardStart_;
    int slowFrames_ = 0;
    bool guardExempt_ = false;  // the test pattern is never skipped

    // Frame timing
    double nextFrameAt_ = 0;  // frame cap deadline (seconds)
    double fps_ = 0.0, frameMs_ = 0.0;
    uint64_t fpsWindowStart_ = 0;
    int fpsFrames_ = 0;
};

}  // namespace sp
