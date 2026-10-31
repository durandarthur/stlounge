#include "platform/window.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "app_info.h"
#include "gfx/gl.h"
#include "platform/log.h"

namespace sp {

namespace {

bool envSet(const char* name) {
    const char* v = std::getenv(name);
    return v && *v;
}

}  // namespace

bool Window::initVideo(std::string* error) {
    // Plain SDL2 prefers X11 even on Wayland sessions. Ask for native Wayland
    // first and fall back to X11, unless the user forced a driver.
    std::vector<const char*> candidates;
    if (envSet("SDL_VIDEODRIVER")) {
        candidates.push_back(nullptr);  // respect the user's choice
    } else if (envSet("WAYLAND_DISPLAY")) {
        candidates = {"wayland", "x11"};
    } else {
        candidates.push_back(nullptr);
    }

    std::string errors;
    for (const char* driver : candidates) {
        if (driver) SDL_SetHint("SDL_VIDEODRIVER", driver);  // SDL_HINT_VIDEODRIVER, SDL >= 2.0.22
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0) return true;
        const char* name = driver ? driver : "default";
        LOG_WARN("video driver '%s' failed: %s", name, SDL_GetError());
        errors += std::string(errors.empty() ? "" : "; ") + name + ": " + SDL_GetError();
    }
    *error = "Could not initialise video (" + errors + ")";
    return false;
}

bool Window::create(const WindowOptions& opts, std::string* error) {
    // Window class / app id, so compositors (e.g. Hyprland window rules) can match it.
    setenv("SDL_VIDEO_WAYLAND_WMCLASS", APP_NAME, 0);
    setenv("SDL_VIDEO_X11_WMCLASS", APP_NAME, 0);
    setenv("SDL_APP_ID", APP_NAME, 0);  // SDL3's name for both (SDL2 on Arch is sdl2-compat over SDL3)
    SDL_SetHint("SDL_APP_NAME", APP_DISPLAY_NAME);
    // Use EGL for GLES on X11 too (SDL defaults to GLX there).
    SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "1");
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    // A fullscreen eye-candy window must not minimise when focus moves to another monitor.
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        *error = std::string("SDL_Init failed: ") + SDL_GetError();
        return false;
    }
    sdlInitialized_ = true;
    if (!initVideo(error)) return false;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);  // opaque window, never transparent
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (opts.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    window_ = SDL_CreateWindow(opts.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               opts.width, opts.height, flags);
    if (!window_) {
        *error = std::string("Could not create an OpenGL ES window: ") + SDL_GetError();
        return false;
    }

    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        *error = std::string("Could not create an OpenGL ES 3.0 context through EGL: ") + SDL_GetError() +
                 "\nCheck that your GPU driver provides EGL and GLES 3.0 (run `eglinfo`)."
                 " On NVIDIA, nvidia-utils must be installed and nvidia-drm.modeset=1 set.";
        return false;
    }
    if (SDL_GL_MakeCurrent(window_, context_) != 0) {
        *error = std::string("SDL_GL_MakeCurrent failed: ") + SDL_GetError();
        return false;
    }

    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    GLint major = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    if (!version || std::strncmp(version, "OpenGL ES", 9) != 0 || major < 3) {
        *error = std::string("The driver returned '") + (version ? version : "(null)") +
                 "' but OpenGL ES 3.0 or newer is required.";
        return false;
    }

    LOG_INFO("video driver: %s", videoDriver());
    return true;
}

void Window::destroy() {
    if (context_) {
        SDL_GL_DeleteContext(context_);
        context_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (sdlInitialized_) {
        SDL_Quit();
        sdlInitialized_ = false;
    }
}

const char* Window::videoDriver() const {
    const char* d = SDL_GetCurrentVideoDriver();
    return d ? d : "none";
}

bool Window::isFullscreen() const {
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Window::setFullscreen(bool on) {
    if (!window_ || on == isFullscreen()) return;
    if (SDL_SetWindowFullscreen(window_, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0)
        LOG_WARN("fullscreen toggle failed: %s", SDL_GetError());
}

int Window::setVsync(bool on) {
    if (!on) {
        SDL_GL_SetSwapInterval(0);
    } else if (SDL_GL_SetSwapInterval(-1) != 0 && SDL_GL_SetSwapInterval(1) != 0) {
        LOG_WARN("vsync unavailable: %s", SDL_GetError());
    }
    swapInterval_ = SDL_GL_GetSwapInterval();
    return swapInterval_;
}

}  // namespace sp
