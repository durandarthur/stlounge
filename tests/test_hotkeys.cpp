#include "app/hotkeys.h"
#include "test.h"

using namespace sp;

TEST(hotkeys_spec_table) {
    struct Row {
        SDL_Keycode key;
        Action action;
    };
    const Row rows[] = {
        {SDLK_SPACE, Action::TogglePause}, {SDLK_n, Action::Next},           {SDLK_RIGHT, Action::Next},
        {SDLK_p, Action::Previous},        {SDLK_LEFT, Action::Previous},    {SDLK_r, Action::Random},
        {SDLK_l, Action::ToggleLock},      {SDLK_s, Action::ToggleShuffle},  {SDLK_h, Action::ToggleUi},
        {SDLK_TAB, Action::ToggleUi},      {SDLK_f, Action::ToggleFullscreen}, {SDLK_F11, Action::ToggleFullscreen},
        {SDLK_F5, Action::Reload},         {SDLK_ESCAPE, Action::Escape},    {SDLK_o, Action::OpenSettings},
        {SDLK_F12, Action::Screenshot},
    };
    for (const Row& r : rows) CHECK_EQ(actionName(actionForKey(r.key, KMOD_NONE, false, false)), actionName(r.action));
    CHECK(actionForKey(SDLK_v, KMOD_LCTRL, false, false) == Action::Paste);
    CHECK(actionForKey(SDLK_v, KMOD_NONE, false, false) == Action::None);
    CHECK(actionForKey(SDLK_q, KMOD_NONE, false, false) == Action::None);
}

TEST(hotkeys_suppressed_while_typing) {
    CHECK(actionForKey(SDLK_n, KMOD_NONE, false, true) == Action::None);
    CHECK(actionForKey(SDLK_v, KMOD_LCTRL, false, true) == Action::None);  // paste goes into the text box
    CHECK(actionForKey(SDLK_ESCAPE, KMOD_NONE, false, true) == Action::None);
}

TEST(hotkeys_ignore_repeat_and_modifiers) {
    CHECK(actionForKey(SDLK_n, KMOD_NONE, true, false) == Action::None);
    CHECK(actionForKey(SDLK_n, KMOD_LALT, false, false) == Action::None);
    CHECK(actionForKey(SDLK_n, KMOD_LGUI, false, false) == Action::None);
    CHECK(actionForKey(SDLK_r, KMOD_LCTRL, false, false) == Action::None);
    CHECK(actionForKey(SDLK_n, KMOD_LSHIFT, false, false) == Action::Next);
    CHECK(actionForKey(SDLK_n, KMOD_NUM, false, false) == Action::Next);  // NumLock must not block hotkeys
}
