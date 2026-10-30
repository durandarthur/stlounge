#pragma once
// In-window hotkeys (SPEC.md section 8). Pure mapping, no side effects.

#include <SDL_keycode.h>

namespace sp {

enum class Action {
    None,
    TogglePause,
    Next,
    Previous,
    Random,
    ToggleLock,
    ToggleShuffle,
    ToggleUi,
    ToggleFullscreen,
    Reload,
    Escape,  // exit fullscreen, else hide UI
    OpenSettings,
    Paste,
    Screenshot,
};

const char* actionName(Action a);

// textInputActive: an ImGui text field has keyboard focus. Hotkeys are then
// suppressed so typing (and Ctrl+V inside a text box) works normally.
// Key repeats are ignored so holding N doesn't skip through the playlist.
Action actionForKey(SDL_Keycode key, Uint16 mod, bool repeat, bool textInputActive);

}  // namespace sp
