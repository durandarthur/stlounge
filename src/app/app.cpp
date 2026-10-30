#include "app/app.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

#include "app/builtin_shader.h"
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

Shader makeImageShader(const std::string& name, const std::string& code) {
    Shader s;
    s.info.name = name;
    Pass image;
    image.type = PassType::Image;
    image.code = code;
    s.passes.push_back(image);
    return s;
}

Shader makeMultipassDemo() {
    Shader s;
    s.info.name = "Multipass demo";
    Pass a;
    a.type = PassType::Buffer;
    a.bufferIndex = 0;
    a.code = kDemoBufferA;
    a.inputs[0].type = InputType::Buffer;
    a.inputs[0].bufferIndex = 0;
    a.inputs[0].sampler = {Filter::Linear, Wrap::Clamp, false, false};
    Pass image;
    image.type = PassType::Image;
    image.code = kDemoImage;
    image.inputs[0] = a.inputs[0];
    image.inputs[1].type = InputType::Texture;
    image.inputs[1].asset = "rgba_noise_medium";
    image.inputs[1].sampler = {Filter::Mipmap, Wrap::Repeat, true, false};
    s.passes = {a, image};
    return s;
}

Shader makeAudioDemo() {
    Shader s = makeImageShader("Audio demo", kDemoAudio);
    s.passes[0].inputs[0].type = InputType::Audio;
    s.passes[0].inputs[0].sampler = {Filter::Linear, Wrap::Clamp, false, false};
    return s;
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

    const Shader pattern = makeImageShader("Built-in test pattern", kBuiltinShader);
    editor_.setShader(pattern);
    loadShader(pattern);
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

        updateLoading();
        updateAudio(seconds(frameStart));
        renderFrame(dt);
        window_.swap();
        limitFrameRate(frameStart);

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
    // GL objects must go before the context. Members declared after window_
    // are destroyed before it, but release the big ones explicitly anyway.
    audio_.stop();
    current_.reset();
    pending_.reset();
    imageTarget_.destroy();
    textures_.clear();
    overlay_.shutdown();
    return 0;
}

// ---------------------------------------------------------------- shaders

void App::loadShader(const Shader& shader) {
    pending_ = std::make_unique<ShaderRenderer>(renderContext_);
    pending_->load(shader);
    LOG_INFO("compiling '%s'%s", shader.info.name.c_str(), caps_.parallelCompile ? " (background)" : "");
}

void App::updateLoading() {
    if (!pending_) return;
    switch (pending_->update()) {
        case ShaderRenderer::State::Ready:
            LOG_INFO("'%s' ready (compile %.0f ms)", pending_->shader().info.name.c_str(), pending_->compileMs());
            for (const std::string& w : pending_->warnings()) LOG_WARN("%s", w.c_str());
            current_ = std::move(pending_);
            clock_.reset();
            mouse_.reset();
            errorLog_.clear();
            redrawOnce_ = true;
            break;
        case ShaderRenderer::State::Failed:
            errorLog_ = pending_->errorLog();
            LOG_WARN("'%s' failed to compile:\n%s", pending_->shader().info.name.c_str(), errorLog_.c_str());
            pending_.reset();
            break;
        default:
            break;
    }
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
        case Action::Reload:
            if (current_) {
                const Shader copy = current_->shader();
                loadShader(copy);
            }
            break;
        default:
            LOG_INFO("hotkey '%s' is not implemented yet", actionName(a));
            break;
    }
}

// ---------------------------------------------------------------- rendering

void App::ensureImageTarget(int drawableW, int drawableH) {
    const int w = std::max(1, static_cast<int>(std::lround(drawableW * renderScale_)));
    const int h = std::max(1, static_cast<int>(std::lround(drawableH * renderScale_)));
    if (imageTarget_.valid() && imageTarget_.width() == w && imageTarget_.height() == h) return;
    std::string error;
    // The Image pass writes to an 8-bit canvas, like ShaderToy's.
    if (!imageTarget_.create(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, &error)) {
        LOG_ERROR("%s", error.c_str());
        return;
    }
    LOG_DEBUG("render resolution %dx%d (scale %.2f)", w, h, renderScale_);
    redrawOnce_ = true;
}

void App::renderFrame(double dt) {
    int dw = 0, dh = 0;
    window_.drawableSize(&dw, &dh);
    ensureImageTarget(dw, dh);

    if (current_ && imageTarget_.valid() && (!paused_ || redrawOnce_)) {
        if (!paused_ && clock_.frame() > 0) clock_.advance(dt);

        FrameUniforms u;
        u.time = static_cast<float>(clock_.time());
        u.timeDelta = clock_.delta();
        u.frameRate = clock_.frameRate();
        u.frame = clock_.frame();
        mouse_.uniform(u.mouse);
        const auto now = std::chrono::system_clock::now();
        const std::time_t tt = std::chrono::system_clock::to_time_t(now);
        const double frac =
            std::chrono::duration<double>(now - std::chrono::system_clock::from_time_t(tt)).count();
        computeDate(tt, frac, u.date);
        for (float& t : u.channelTime) t = u.time;
        u.sampleRate = static_cast<float>(AudioCapture::kSampleRate);

        current_->render(u, imageTarget_);
        if (!paused_) {
            clock_.frameRendered();
            mouse_.frameRendered();
        }
        redrawOnce_ = false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, dw, dh);
    if (current_ && imageTarget_.valid()) {
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

// ---------------------------------------------------------------- UI

void App::drawUi() {
    drawInfoPanel();
    if (showEditor_) drawEditor();
    if (!errorLog_.empty()) drawErrors();
}

void App::drawInfoPanel() {
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(APP_DISPLAY_NAME " - info")) {
        if (current_) {
            ImGui::Text("Shader: %s", current_->shader().info.name.c_str());
            ImGui::Text("Compile time: %.0f ms", current_->compileMs());
        } else {
            ImGui::TextDisabled("No shader running");
        }
        if (pending_) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Compiling...");
        if (current_)
            for (const std::string& w : current_->warnings())
                ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", w.c_str());
        ImGui::Text("iTime %.2f   iFrame %d   %s", clock_.time(), clock_.frame(), paused_ ? "[PAUSED]" : "");
        float m[4];
        mouse_.uniform(m);
        ImGui::Text("iMouse (%.0f, %.0f, %.0f, %.0f)", m[0], m[1], m[2], m[3]);
        ImGui::Text("iResolution %d x %d", imageTarget_.width(), imageTarget_.height());
        if (ImGui::SliderFloat("Render scale", &renderScale_, 0.25f, 2.0f, "%.2f"))
            renderScale_ = std::round(renderScale_ * 20.0f) / 20.0f;

        ImGui::Separator();
        int ww = 0, wh = 0, dw = 0, dh = 0;
        SDL_GetWindowSize(window_.sdl(), &ww, &wh);
        window_.drawableSize(&dw, &dh);
        ImGui::Text("%.1f FPS (%.2f ms)  vsync interval %d", fps_, frameMs_, window_.swapInterval());
        ImGui::Text("Window %dx%d, drawable %dx%d%s", ww, wh, dw, dh, window_.isFullscreen() ? ", fullscreen" : "");
        if (ImGui::CollapsingHeader("GPU")) {
            ImGui::Text("Video driver: %s", window_.videoDriver());
            ImGui::TextWrapped("Renderer: %s", caps_.renderer.c_str());
            ImGui::TextWrapped("Version: %s", caps_.version.c_str());
            ImGui::Text("Buffers: %s%s", bufferFormatName(caps_.bufferFormat),
                        caps_.bufferLinearFilter ? "" : " (nearest only)");
            ImGui::Text("Parallel shader compile: %s", caps_.parallelCompile ? "yes" : "no");
        }
        drawAudioSection();
        ImGui::Checkbox("Show test editor", &showEditor_);
        ImGui::TextDisabled("Space pause  F5 reload  H/Tab hide UI  F/F11 fullscreen");
    }
    ImGui::End();
}

void App::drawEditor() {
    ImGui::SetNextWindowPos(ImVec2(500, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(700, 560), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Shader editor (temporary)", &showEditor_)) {
        if (ImGui::Button("Compile")) {
            Shader s = editor_.build();
            s.info.name = "Editor";
            loadShader(s);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear all")) editor_.clear();
        ImGui::SameLine();
        if (ImGui::Button("Test pattern")) editor_.setShader(makeImageShader("Built-in test pattern", kBuiltinShader));
        ImGui::SameLine();
        if (ImGui::Button("Load multipass demo")) {
            editor_.setShader(makeMultipassDemo());
            loadShader(makeMultipassDemo());
        }
        ImGui::SameLine();
        if (ImGui::Button("Load audio demo")) {
            editor_.setShader(makeAudioDemo());
            loadShader(makeAudioDemo());
        }
        editor_.draw();
    }
    ImGui::End();
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
        audio_.start(audioDevice_);
    } else if (st == AudioCapture::State::Failed) {
        // The server may come back (e.g. PipeWire restarted): retry now and then.
        if (audioFailedAt_ < 0) {
            audioFailedAt_ = now;
        } else if (now - audioFailedAt_ > 10.0) {
            audioFailedAt_ = -1;
            audio_.start(audioDevice_);
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

void App::drawAudioSection() {
    if (!ImGui::CollapsingHeader("Audio")) return;
    switch (audio_.state()) {
        case AudioCapture::State::Off:
            ImGui::TextDisabled("Capture off (the current shader has no audio input)");
            break;
        case AudioCapture::State::Connecting:
            ImGui::Text("Connecting...");
            break;
        case AudioCapture::State::Running:
            ImGui::TextWrapped("Capturing from %s", audio_.sourceName().c_str());
            break;
        case AudioCapture::State::Failed:
            ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "Audio unavailable: %s", audio_.error().c_str());
            ImGui::TextDisabled("Audio inputs stay silent. Retrying every 10 s.");
            break;
    }

    if (!audioSourcesListed_) {
        audioSources_ = AudioCapture::listSources(&audioListError_);
        audioSourcesListed_ = true;
    }
    std::string current = "Default output (follows changes)";
    for (const AudioSource& src : audioSources_)
        if (src.name == audioDevice_) current = src.description;
    if (ImGui::BeginCombo("Source", current.c_str())) {
        if (ImGui::Selectable("Default output (follows changes)", audioDevice_.empty())) audioDevice_.clear();
        for (const AudioSource& src : audioSources_) {
            const std::string label = (src.isMonitor ? "Output: " : "Input: ") + src.description;
            if (ImGui::Selectable(label.c_str(), src.name == audioDevice_)) audioDevice_ = src.name;
        }
        ImGui::EndCombo();
        if (audioDevice_ != audio_.requestedDevice() && audio_.state() != AudioCapture::State::Off) {
            audio_.start(audioDevice_);
            audioFailedAt_ = -1;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) audioSourcesListed_ = false;
    if (!audioListError_.empty()) ImGui::TextDisabled("%s", audioListError_.c_str());

    float bins[SpectrumAnalyser::kBins];
    for (int i = 0; i < SpectrumAnalyser::kBins; ++i) bins[i] = audioFrequency_[i];
    ImGui::PlotHistogram("##fft", bins, SpectrumAnalyser::kBins, 0, "FFT (texture row 0)", 0.0f, 255.0f,
                         ImVec2(-1, 60));
}

void App::drawErrors() {
    ImGui::SetNextWindowPos(ImVec2(16, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(620, 220), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("Shader errors", &open)) {
        ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "Compile failed. The previous shader keeps running.");
        ImGui::InputTextMultiline("##log", &errorLog_, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::End();
    if (!open) errorLog_.clear();
}

void App::limitFrameRate(uint64_t frameStart) {
    if (window_.swapInterval() != 0) return;  // vsync paces us
    const double target = 1.0 / kFallbackFpsCap;
    const double spent = seconds(SDL_GetPerformanceCounter() - frameStart);
    if (spent < target) SDL_Delay(static_cast<Uint32>((target - spent) * 1000.0));
}

}  // namespace sp
