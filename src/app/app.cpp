#include "app/app.h"

#include <imgui.h>

#include <cmath>

#include "app_info.h"
#include "gfx/gl.h"
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

    const int interval = window_.setVsync(true);
    LOG_INFO("swap interval: %d%s", interval, interval == 0 ? " (no vsync, software frame cap)" : "");

    if (!overlay_.init(window_.sdl(), window_.context(), (paths_.stateDir / "imgui.ini").string())) {
        // The shader can still be shown without UI; keep going.
        LOG_ERROR("UI could not be initialised; continuing without it");
    }
    return true;
}

int App::run(const AppOptions& opts) {
    if (!startup(opts)) {
        log::shutdown();
        return 1;
    }

    fpsWindowStart_ = SDL_GetPerformanceCounter();
    while (running_) {
        const uint64_t frameStart = SDL_GetPerformanceCounter();

        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        if (!running_) break;

        renderFrame(seconds(frameStart));
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
    overlay_.shutdown();
    window_.destroy();
    log::shutdown();
    return 0;
}

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
        default:
            break;
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
        default:
            LOG_INFO("hotkey '%s' is not implemented yet", actionName(a));
            break;
    }
}

void App::renderFrame(double now) {
    int w = 0, h = 0;
    window_.drawableSize(&w, &h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);

    // Milestone 1 placeholder: a slowly cycling dark colour so you can see the
    // window is alive. Replaced by the shader renderer in milestone 2.
    const float t = static_cast<float>(now);
    glClearColor(0.15f + 0.1f * std::sin(t * 0.7f), 0.1f + 0.08f * std::sin(t * 0.5f + 2.0f),
                 0.2f + 0.1f * std::sin(t * 0.3f + 4.0f), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (uiVisible_) {
        overlay_.beginFrame();
        drawUi();
        overlay_.endFrame();
    }
}

void App::drawUi() {
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(APP_DISPLAY_NAME " - info")) {
        int ww = 0, wh = 0, dw = 0, dh = 0;
        SDL_GetWindowSize(window_.sdl(), &ww, &wh);
        window_.drawableSize(&dw, &dh);

        ImGui::Text("%.1f FPS (%.2f ms)  vsync interval %d", fps_, frameMs_, window_.swapInterval());
        ImGui::Text("Window %dx%d, drawable %dx%d%s", ww, wh, dw, dh, window_.isFullscreen() ? ", fullscreen" : "");
        ImGui::Separator();
        ImGui::Text("Video driver: %s", window_.videoDriver());
        ImGui::TextWrapped("Renderer: %s", caps_.renderer.c_str());
        ImGui::TextWrapped("Version: %s", caps_.version.c_str());
        ImGui::Text("Buffers: %s%s", bufferFormatName(caps_.bufferFormat),
                    caps_.bufferLinearFilter ? "" : " (nearest only)");
        ImGui::TextWrapped("  %s", caps_.bufferFormatNote.c_str());
        ImGui::Text("Parallel shader compile: %s", caps_.parallelCompile ? "yes" : "no");
        ImGui::Separator();
        ImGui::TextDisabled("H / Tab: hide UI   F / F11: fullscreen   Esc: leave fullscreen, else hide UI");
        ImGui::TextDisabled("Mouse over UI: %s", overlay_.wantsMouse() ? "yes" : "no");
    }
    ImGui::End();
}

void App::limitFrameRate(uint64_t frameStart) {
    if (window_.swapInterval() != 0) return;  // vsync paces us
    const double target = 1.0 / kFallbackFpsCap;
    const double spent = seconds(SDL_GetPerformanceCounter() - frameStart);
    if (spent < target) SDL_Delay(static_cast<Uint32>((target - spent) * 1000.0));
}

}  // namespace sp
