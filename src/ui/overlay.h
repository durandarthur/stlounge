#pragma once
// Dear ImGui lifetime and per-frame plumbing. Panels live elsewhere.

#include <SDL.h>

#include <string>

namespace sp {

class Overlay {
public:
    ~Overlay() { shutdown(); }

    bool init(SDL_Window* window, SDL_GLContext context, const std::string& iniPath);
    void shutdown();

    // Always forward events, even while the UI is hidden.
    void processEvent(const SDL_Event& e);

    void beginFrame();
    void endFrame();  // renders ImGui into the current framebuffer

    // Valid after the last frame that was drawn.
    bool wantsMouse() const;
    bool wantsTextInput() const;

    // Drop keyboard focus on the next frame (used when hiding the UI so a
    // stale text field doesn't keep swallowing hotkeys).
    void clearFocus();

private:
    std::string iniPath_;
    bool initialized_ = false;
    bool clearFocusPending_ = false;
};

}  // namespace sp
