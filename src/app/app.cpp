#include "app/app.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

#include "app/demos.h"
#include "app_info.h"
#include "gfx/gl.h"
#include "gfx/program.h"
#include "platform/log.h"

namespace sp {

namespace {

constexpr double kFallbackFpsCap = 60.0;  // used only when vsync is unavailable

double seconds(uint64_t ticks) {
    return static_cast<double>(ticks) / static_cast<double>(SDL_GetPerformanceFrequency());
}

}  // namespace

bool App::startup(const AppOptions& opts) {
    opts_ = opts;

    paths_ = resolveAppPaths(APP_NAME, processEnv());
    std::string dirMessage;
    const bool dirsOk = ensureAppDirs(paths_, APP_NAME, &dirMessage);
    log::init(dirsOk ? (paths_.stateDir / (std::string(APP_NAME) + ".log")).string() : "", opts.verbose);
    LOG_INFO("%s %s starting", APP_DISPLAY_NAME, APP_VERSION);
    if (!dirMessage.empty()) LOG_WARN("%s", dirMessage.c_str());
    LOG_INFO("config: %s", paths_.configDir.c_str());
    LOG_INFO("data:   %s", paths_.dataDir.c_str());
    LOG_INFO("state:  %s (log file here)", paths_.stateDir.c_str());

    SDL_version compiled, linked;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);
    LOG_INFO("SDL compiled %d.%d.%d, running %d.%d.%d", compiled.major, compiled.minor, compiled.patch,
             linked.major, linked.minor, linked.patch);

    WindowOptions wo;
    wo.title = APP_DISPLAY_NAME;
    std::string error;
    if (!window_.create(wo, &error)) {
        LOG_ERROR("%s", error.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, APP_DISPLAY_NAME, error.c_str(), nullptr);
        return false;
    }

    caps_ = probeGlCaps();
    LOG_INFO("GL: %s", caps_.summary().c_str());
    LOG_INFO("GL vendor: %s, GLSL: %s", caps_.vendor.c_str(), caps_.glslVersion.c_str());
    if (caps_.bufferFormat == BufferFormat::RGBA32F && caps_.bufferLinearFilter)
        LOG_INFO("buffer format: %s, %s", bufferFormatName(caps_.bufferFormat), caps_.bufferFormatNote.c_str());
    else
        LOG_WARN("buffer format: %s, %s", bufferFormatName(caps_.bufferFormat), caps_.bufferFormatNote.c_str());
    if (caps_.parallelCompile) enableParallelCompile();

    const int interval = window_.setVsync(true);
    LOG_INFO("swap interval: %d%s", interval, interval == 0 ? " (no vsync, software frame cap)" : "");

    triangle_.init();
    neutral_.init();
    if (!blitter_.init(&error)) {
        LOG_ERROR("%s", error.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, APP_DISPLAY_NAME, error.c_str(), window_.sdl());
        return false;
    }
    renderContext_.caps = &caps_;
    renderContext_.triangle = &triangle_;
    renderContext_.neutral = &neutral_;
    renderContext_.textures = &textures_;
    audioTexture_.init();
    renderContext_.audioTexture = audioTexture_.id();

    if (!overlay_.init(window_.sdl(), window_.context(), (paths_.stateDir / "imgui.ini").string())) {
        // The shader can still be shown without UI; keep going.
        LOG_ERROR("UI could not be initialised; continuing without it");
    }

    library_ = std::make_unique<ShaderLibrary>(paths_.dataDir / "shaders");
    library_->scan();
    LOG_INFO("library: %zu shaders in %s", library_->entries().size(), library_->dir().c_str());
    playlists_ = std::make_unique<PlaylistStore>(paths_.dataDir / "playlists");
    playlists_->scan();
    LOG_INFO("playlists: %zu in %s", playlists_->playlists().size(), playlists_->dir().c_str());

    loadSettingsAtStartup();

    const Session session = loadSession(sessionPath_);
    lastSavedSessionShader_ = session.lastShader;
    lastSavedSessionPlaylist_ = session.playlist;
    if (session.playlist.empty() || session.playlist == kFavoritesPlaylist || playlists_->find(session.playlist))
        activePlaylist_ = session.playlist;
    player_.setShuffle(settings_.shuffle);
    rebuildPlaylist();

    if (opts.safeMode) {
        LOG_INFO("safe mode: not loading the last shader");
        loadShader(makeTestPattern());
    } else if (!session.lastShader.empty() && library_->find(session.lastShader)) {
        LOG_INFO("resuming '%s'", session.lastShader.c_str());
        LoadRequest req;
        req.fromPlaylist = player_.contains(session.lastShader);
        playFromLibrary(session.lastShader, req);
    } else if (player_.playableCount() > 0) {
        playlistStep(+1);
    } else {
        loadShader(makeTestPattern());
    }
    return true;
}

int App::run(const AppOptions& opts) {
    if (!startup(opts)) {
        log::shutdown();
        return 1;
    }

    fpsWindowStart_ = SDL_GetPerformanceCounter();
    uint64_t lastFrame = fpsWindowStart_;
    while (running_) {
        const uint64_t frameStart = SDL_GetPerformanceCounter();
        const double dt = seconds(frameStart - lastFrame);
        lastFrame = frameStart;

        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        if (!running_) break;
        if (!droppedFiles_.empty()) {
            importFiles(droppedFiles_);
            droppedFiles_.clear();
        }

        if (playlistDirty_) rebuildPlaylist();
        if (skipRequested_ != 2) {
            const int direction = skipRequested_;
            skipRequested_ = 2;
            playlistStep(direction, /*isSkip=*/true);
        }
        updateLoading();
        updateAutoAdvance(dt);
        updateAudio(seconds(frameStart));
        renderFrame(dt);
        window_.swap();
        pollScreenshot();
        saveSettingsIfChanged(false);
        limitFrameRate();

        ++fpsFrames_;
        const uint64_t now = SDL_GetPerformanceCounter();
        const double windowSec = seconds(now - fpsWindowStart_);
        if (windowSec >= 0.5) {
            fps_ = fpsFrames_ / windowSec;
            frameMs_ = 1000.0 * windowSec / fpsFrames_;
            fpsFrames_ = 0;
            fpsWindowStart_ = now;
        }
    }

    LOG_INFO("exiting");
    saveSettingsIfChanged(true);
    saveSessionNow();
    if (screenshot_.valid()) LOG_INFO("%s", screenshot_.get().c_str());  // waits for the PNG
    // GL objects must go before the context. Members declared after window_
    // are destroyed before it, but release the big ones explicitly anyway.
    audio_.stop();
    current_.reset();
    pending_.reset();
    outgoing_.reset();
    outgoingTarget_.destroy();
    imageTarget_.destroy();
    textures_.clear();
    overlay_.shutdown();
    return 0;
}

// ---------------------------------------------------------------- shaders

void App::loadShader(const Shader& shader, const LoadRequest& req) {
    pending_ = std::make_unique<ShaderRenderer>(renderContext_);
    pendingRequest_ = req;
    pendingStart_ = std::chrono::steady_clock::now();
    pending_->load(shader);
    LOG_INFO("compiling '%s'%s", shader.info.name.c_str(), caps_.parallelCompile ? " (background)" : "");
}

void App::failPending(std::string log) {
    const std::string name = pending_->shader().info.name;
    const LoadRequest req = pendingRequest_;
    pending_.reset();
    LOG_WARN("'%s' failed:\n%s", name.c_str(), log.c_str());
    // Several failures in a row (auto-skip) are all kept in the log window.
    const std::string entry = "=== " + name + " ===\n" + log;
    errorLog_ = req.isSkip && !errorLog_.empty() ? errorLog_ + "\n" + entry : entry;
    if (req.fromPlaylist && !req.libraryId.empty()) {
        player_.markFailed(req.libraryId);
        setStatus("Skipped '" + name + "': it failed to compile", true);
        skipRequested_ = req.direction;
    } else if (!current_ && name != makeTestPattern().info.name) {
        loadShader(makeTestPattern());  // keep something on screen
    }
}

void App::updateLoading() {
    if (!pending_) return;
    const ShaderRenderer::State st = pending_->update();
    if (st == ShaderRenderer::State::Compiling && settings_.compileTimeCapMs > 0) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                              pendingStart_).count();
        if (ms > settings_.compileTimeCapMs) {
            failPending("Compiling took longer than " + std::to_string(settings_.compileTimeCapMs) +
                        " ms (Settings > Advanced > compile-time cap), so it was cancelled.");
            return;
        }
    }
    if (st == ShaderRenderer::State::Failed) {
        failPending(pending_->errorLog());
        return;
    }
    if (st != ShaderRenderer::State::Ready) return;

    LOG_INFO("'%s' ready (compile %.0f ms)", pending_->shader().info.name.c_str(), pending_->compileMs());
    for (const std::string& w : pending_->warnings()) LOG_WARN("%s", w.c_str());

    // Crossfade only between playlist shaders, and never while locked.
    const bool fade = settings_.crossfade && !settings_.locked && settings_.crossfadeSeconds > 0 && current_ &&
                      pendingRequest_.fromPlaylist && imageTarget_.valid();
    if (fade) {
        outgoing_ = std::move(current_);
        outgoingClock_ = clock_;
        std::swap(outgoingTarget_, imageTarget_);  // the old shader keeps its last frame
        fadeElapsed_ = 0;
        fadeDuration_ = settings_.crossfadeSeconds;
    } else {
        outgoing_.reset();
        outgoingTarget_.destroy();
    }
    current_ = std::move(pending_);
    currentLibraryId_ = pendingRequest_.libraryId;
    if (!pendingRequest_.isSkip) errorLog_.clear();
    if (player_.contains(currentLibraryId_)) player_.setCurrent(currentLibraryId_);
    clock_.reset();
    mouse_.reset();
    shaderElapsed_ = 0;
    redrawOnce_ = true;
    if (!currentLibraryId_.empty()) saveSessionNow();
}

// ---------------------------------------------------------------- events

void App::handleEvent(const SDL_Event& e) {
    overlay_.processEvent(e);

    switch (e.type) {
        case SDL_QUIT:
            running_ = false;
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_CLOSE) running_ = false;
            break;
        case SDL_KEYDOWN: {
            // While the UI is hidden no text field can be active.
            const bool textInput = uiVisible_ && overlay_.wantsTextInput();
            const Action a = actionForKey(e.key.keysym.sym, e.key.keysym.mod, e.key.repeat != 0, textInput);
            if (a != Action::None) doAction(a);
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEMOTION:
            handleMouse(e);
            break;
        case SDL_DROPFILE:
            // Several files can arrive in one batch; imported together after polling.
            if (e.drop.file) {
                droppedFiles_.push_back(e.drop.file);
                SDL_free(e.drop.file);
            }
            break;
        case SDL_DROPTEXT:
            if (e.drop.file) {
                openPasteDialog(e.drop.file);
                SDL_free(e.drop.file);
            }
            break;
        default:
            break;
    }
}

void App::handleMouse(const SDL_Event& e) {
    if (!imageTarget_.valid()) return;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window_.sdl(), &ww, &wh);
    auto toPixels = [&](int x, int y, float* px, float* py) {
        toShaderPixels(static_cast<float>(x), static_cast<float>(y), ww, wh, imageTarget_.width(),
                       imageTarget_.height(), px, py);
    };
    float px = 0, py = 0;
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        // Clicks on the UI belong to the UI.
        if (uiVisible_ && overlay_.wantsMouse()) return;
        toPixels(e.button.x, e.button.y, &px, &py);
        mouse_.press(px, py);
    } else if (e.type == SDL_MOUSEMOTION && mouse_.held()) {
        // A drag that started on the shader keeps going even over the UI.
        toPixels(e.motion.x, e.motion.y, &px, &py);
        mouse_.move(px, py);
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && mouse_.held()) {
        toPixels(e.button.x, e.button.y, &px, &py);
        mouse_.move(px, py);
        mouse_.release();
    }
}

void App::setUiVisible(bool visible) {
    if (visible == uiVisible_) return;
    uiVisible_ = visible;
    if (!visible) overlay_.clearFocus();
}

void App::doAction(Action a) {
    switch (a) {
        case Action::ToggleUi:
            setUiVisible(!uiVisible_);
            break;
        case Action::ToggleFullscreen:
            window_.setFullscreen(!window_.isFullscreen());
            break;
        case Action::Escape:
            if (window_.isFullscreen())
                window_.setFullscreen(false);
            else
                setUiVisible(false);
            break;
        case Action::TogglePause:
            paused_ = !paused_;
            LOG_INFO("%s", paused_ ? "paused" : "resumed");
            break;
        case Action::Paste: {
            char* text = SDL_GetClipboardText();
            openPasteDialog(text ? text : "");
            SDL_free(text);
            break;
        }
        case Action::Reload:
            if (current_) {
                const Shader copy = current_->shader();
                LoadRequest req;
                req.libraryId = currentLibraryId_;
                loadShader(copy, req);
            }
            break;
        case Action::Next:
            playlistStep(+1);
            break;
        case Action::Previous:
            playlistStep(-1);
            break;
        case Action::Random:
            playlistStep(0);
            break;
        case Action::ToggleLock:
            setLocked(!settings_.locked);
            break;
        case Action::ToggleShuffle:
            setShuffle(!settings_.shuffle);
            break;
        case Action::OpenSettings:
            showSettings_ = !showSettings_ || !uiVisible_;
            if (showSettings_) setUiVisible(true);
            break;
        case Action::Screenshot:
            takeScreenshot();
            break;
        case Action::None:
            break;
    }
}

// ---------------------------------------------------------------- rendering

void App::ensureImageTarget(int drawableW, int drawableH) {
    const float scale = settings_.renderScale;
    const int w = std::max(1, static_cast<int>(std::lround(drawableW * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(drawableH * scale)));
    if (imageTarget_.valid() && imageTarget_.width() == w && imageTarget_.height() == h) return;
    std::string error;
    // The Image pass writes to an 8-bit canvas, like ShaderToy's.
    if (!imageTarget_.create(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, &error)) {
        LOG_ERROR("%s", error.c_str());
        return;
    }
    LOG_DEBUG("render resolution %dx%d (scale %.2f)", w, h, scale);
    redrawOnce_ = true;
}

void App::renderShader(ShaderRenderer& r, PlaybackClock& clock, const MouseState& mouse, const RenderTarget& target,
                       bool advance, double dt) {
    if (advance && clock.frame() > 0) clock.advance(dt);
    FrameUniforms u;
    u.time = static_cast<float>(clock.time());
    u.timeDelta = clock.delta();
    u.frameRate = clock.frameRate();
    u.frame = clock.frame();
    mouse.uniform(u.mouse);
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    const double frac = std::chrono::duration<double>(now - std::chrono::system_clock::from_time_t(tt)).count();
    computeDate(tt, frac, u.date);
    for (float& t : u.channelTime) t = u.time;
    u.sampleRate = static_cast<float>(AudioCapture::kSampleRate);
    r.render(u, target);
    if (advance) clock.frameRendered();
}

void App::renderFrame(double dt) {
    int dw = 0, dh = 0;
    window_.drawableSize(&dw, &dh);
    ensureImageTarget(dw, dh);

    if (current_ && imageTarget_.valid() && (!paused_ || redrawOnce_)) {
        renderShader(*current_, clock_, mouse_, imageTarget_, !paused_, dt);
        if (!paused_) mouse_.frameRendered();
        redrawOnce_ = false;
    }

    // The outgoing shader of a crossfade keeps animating (with no mouse input).
    float fade = 1.0f;
    if (outgoing_) {
        if (!paused_) fadeElapsed_ += dt;
        if (fadeElapsed_ >= fadeDuration_ || !current_) {
            outgoing_.reset();
            outgoingTarget_.destroy();
        } else {
            if (outgoingTarget_.width() != imageTarget_.width() || outgoingTarget_.height() != imageTarget_.height()) {
                std::string error;
                outgoingTarget_.create(imageTarget_.width(), imageTarget_.height(), GL_RGBA8, GL_RGBA,
                                       GL_UNSIGNED_BYTE, &error);
            }
            if (!paused_) renderShader(*outgoing_, outgoingClock_, MouseState{}, outgoingTarget_, true, dt);
            const float x = static_cast<float>(fadeElapsed_ / fadeDuration_);
            fade = x * x * (3.0f - 2.0f * x);  // smoothstep
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, dw, dh);
    if (current_ && imageTarget_.valid()) {
        if (outgoing_ && outgoingTarget_.valid())
            blitter_.drawMix(outgoingTarget_.texture(), imageTarget_.texture(), fade, dw, dh, triangle_);
        else
            blitter_.draw(imageTarget_.texture(), dw, dh, triangle_);
    } else {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    if (uiVisible_) {
        overlay_.beginFrame();
        drawUi();
        overlay_.endFrame();
    }
}

void App::updateAudio(double now) {
    const bool needed = current_ && current_->usesAudio();
    const AudioCapture::State st = audio_.state();

    if (!needed) {
        if (st == AudioCapture::State::Running || st == AudioCapture::State::Connecting) {
            if (audioUnusedSince_ < 0) {
                audioUnusedSince_ = now;
            } else if (now - audioUnusedSince_ > 5.0) {
                audio_.stop();
                analyser_.reset();
                audioUnusedSince_ = -1;
                LOG_INFO("audio capture stopped (no shader uses it)");
            }
        }
        return;
    }
    audioUnusedSince_ = -1;

    if (st == AudioCapture::State::Off) {
        audio_.start(settings_.audioSource);
    } else if (st == AudioCapture::State::Failed) {
        // The server may come back (e.g. PipeWire restarted): retry now and then.
        if (audioFailedAt_ < 0) {
            audioFailedAt_ = now;
        } else if (now - audioFailedAt_ > 10.0) {
            audioFailedAt_ = -1;
            audio_.start(settings_.audioSource);
        }
    }

    if (paused_) return;
    if (audio_.state() == AudioCapture::State::Running) {
        float samples[SpectrumAnalyser::kFftSize];
        uint8_t waveform[SpectrumAnalyser::kBins];
        audio_.latest(samples, SpectrumAnalyser::kFftSize);
        analyser_.process(samples, audioFrequency_, waveform);
        audioTexture_.upload(audioFrequency_, waveform);
        audioTextureSilent_ = false;
    } else if (!audioTextureSilent_) {
        audioTexture_.uploadSilence();
        std::fill(std::begin(audioFrequency_), std::end(audioFrequency_), uint8_t{0});
        audioTextureSilent_ = true;
    }
}

void App::limitFrameRate() {
    // The FPS cap applies on top of vsync; without vsync there's always a cap.
    double cap = settings_.fpsCap;
    if (cap <= 0 && window_.swapInterval() == 0) cap = kFallbackFpsCap;
    if (cap <= 0) {
        nextFrameAt_ = 0;
        return;
    }
    const double period = 1.0 / cap;
    const double now = seconds(SDL_GetPerformanceCounter());
    // Deadline-based, so the average rate is right even with millisecond sleeps.
    if (nextFrameAt_ <= 0 || now - nextFrameAt_ > period) nextFrameAt_ = now;
    nextFrameAt_ += period;
    const double wait = nextFrameAt_ - now;
    if (wait > 0.0005) SDL_Delay(static_cast<Uint32>(wait * 1000.0));
}

}  // namespace sp
