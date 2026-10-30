#include "app/hotkeys.h"

namespace sp {

const char* actionName(Action a) {
    switch (a) {
        case Action::None: return "none";
        case Action::TogglePause: return "pause";
        case Action::Next: return "next";
        case Action::Previous: return "previous";
        case Action::Random: return "random";
        case Action::ToggleLock: return "lock";
        case Action::ToggleShuffle: return "shuffle";
        case Action::ToggleUi: return "toggle UI";
        case Action::ToggleFullscreen: return "fullscreen";
        case Action::Reload: return "reload";
        case Action::Escape: return "escape";
        case Action::OpenSettings: return "settings";
        case Action::Paste: return "paste";
        case Action::Screenshot: return "screenshot";
    }
    return "?";
}

Action actionForKey(SDL_Keycode key, Uint16 mod, bool repeat, bool textInputActive) {
    if (repeat || textInputActive) return Action::None;

    const bool ctrl = (mod & KMOD_CTRL) != 0;
    const bool alt = (mod & KMOD_ALT) != 0;
    const bool gui = (mod & KMOD_GUI) != 0;

    if (ctrl && !alt && !gui && key == SDLK_v) return Action::Paste;
    if (ctrl || alt || gui) return Action::None;  // leave other combos to the compositor / ImGui

    switch (key) {
        case SDLK_SPACE: return Action::TogglePause;
        case SDLK_n:
        case SDLK_RIGHT: return Action::Next;
        case SDLK_p:
        case SDLK_LEFT: return Action::Previous;
        case SDLK_r: return Action::Random;
        case SDLK_l: return Action::ToggleLock;
        case SDLK_s: return Action::ToggleShuffle;
        case SDLK_h:
        case SDLK_TAB: return Action::ToggleUi;
        case SDLK_f:
        case SDLK_F11: return Action::ToggleFullscreen;
        case SDLK_F5: return Action::Reload;
        case SDLK_ESCAPE: return Action::Escape;
        case SDLK_o: return Action::OpenSettings;
        case SDLK_F12: return Action::Screenshot;
        default: return Action::None;
    }
}

}  // namespace sp
