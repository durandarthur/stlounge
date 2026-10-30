#pragma once
// SDL2 window with an EGL / OpenGL ES 3.0 context, on Wayland or X11.

#include <SDL.h>

#include <string>

namespace sp {

struct WindowOptions {
    std::string title;
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
};

class Window {
public:
    Window() = default;
    ~Window() { destroy(); }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // On failure returns false and fills `error` with a readable message.
    bool create(const WindowOptions& opts, std::string* error);
    void destroy();

    SDL_Window* sdl() const { return window_; }
    SDL_GLContext context() const { return context_; }
    const char* videoDriver() const;

    void swap() { SDL_GL_SwapWindow(window_); }
    void drawableSize(int* w, int* h) const { SDL_GL_GetDrawableSize(window_, w, h); }

    bool isFullscreen() const;
    void setFullscreen(bool on);

    // Tries adaptive vsync, then regular vsync. Returns the interval in effect.
    int setVsync(bool on);
    int swapInterval() const { return swapInterval_; }

private:
    bool initVideo(std::string* error);

    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool sdlInitialized_ = false;
    int swapInterval_ = 0;
};

}  // namespace sp
