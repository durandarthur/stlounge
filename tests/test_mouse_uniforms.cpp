#include "shadertoy/mouse.h"
#include "shadertoy/uniforms.h"
#include "test.h"

using namespace sp;

namespace {

struct M {
    float v[4];
};
M get(const MouseState& m) {
    M r;
    m.uniform(r.v);
    return r;
}

}  // namespace

TEST(mouse_initial_is_zero) {
    MouseState m;
    M r = get(m);
    for (float f : r.v) CHECK_EQ(f, 0.0f);
}

TEST(mouse_click_drag_release_semantics) {
    MouseState m;
    m.press(100, 50);
    M r = get(m);  // first frame after the click: z and w positive
    CHECK_EQ(r.v[0], 100.0f);
    CHECK_EQ(r.v[1], 50.0f);
    CHECK_EQ(r.v[2], 100.0f);
    CHECK_EQ(r.v[3], 50.0f);
    m.frameRendered();

    m.move(120, 70);
    r = get(m);  // dragging: xy follows, z stays positive, w now negative
    CHECK_EQ(r.v[0], 120.0f);
    CHECK_EQ(r.v[1], 70.0f);
    CHECK_EQ(r.v[2], 100.0f);
    CHECK_EQ(r.v[3], -50.0f);
    m.frameRendered();

    m.release();
    m.move(500, 500);  // moves after release are ignored
    r = get(m);
    CHECK_EQ(r.v[0], 120.0f);
    CHECK_EQ(r.v[1], 70.0f);
    CHECK_EQ(r.v[2], -100.0f);
    CHECK_EQ(r.v[3], -50.0f);

    m.press(10, 20);  // a new click restarts the cycle
    r = get(m);
    CHECK_EQ(r.v[2], 10.0f);
    CHECK_EQ(r.v[3], 20.0f);
}

TEST(mouse_click_and_release_within_one_frame) {
    // A quick click between two frames must still show w > 0 for one frame.
    MouseState m;
    m.press(30, 40);
    m.release();
    M r = get(m);
    CHECK_EQ(r.v[2], -30.0f);
    CHECK_EQ(r.v[3], 40.0f);
    m.frameRendered();
    CHECK_EQ(get(m).v[3], -40.0f);
}

TEST(mouse_window_to_shader_pixels) {
    float x, y;
    // 800x600 window, half-resolution render: origin moves to bottom-left.
    toShaderPixels(0, 0, 800, 600, 400, 300, &x, &y);
    CHECK_EQ(x, 0.0f);
    CHECK_EQ(y, 299.0f);
    toShaderPixels(400, 300, 800, 600, 400, 300, &x, &y);
    CHECK_EQ(x, 200.0f);
    CHECK_EQ(y, 150.0f);
    toShaderPixels(799, 599, 800, 600, 400, 300, &x, &y);
    CHECK_EQ(x, 399.0f);
    CHECK_EQ(y, 0.0f);
    toShaderPixels(-50, 9999, 800, 600, 400, 300, &x, &y);  // outside the window: clamped
    CHECK_EQ(x, 0.0f);
    CHECK_EQ(y, 0.0f);
}

TEST(date_matches_shadertoy_layout) {
    std::tm t{};
    t.tm_year = 2026 - 1900;
    t.tm_mon = 9;  // October
    t.tm_mday = 30;
    t.tm_hour = 13;
    t.tm_min = 2;
    t.tm_sec = 3;
    t.tm_isdst = -1;
    const std::time_t tt = std::mktime(&t);
    float d[4];
    computeDate(tt, 0.5, d);
    CHECK_EQ(d[0], 2026.0f);
    CHECK_EQ(d[1], 9.0f);  // 0-based month
    CHECK_EQ(d[2], 30.0f);
    CHECK_NEAR(d[3], 13 * 3600 + 2 * 60 + 3.5, 1e-3);
}

TEST(clock_advances_and_clamps) {
    PlaybackClock c;
    CHECK_EQ(c.frame(), 0);
    c.advance(1.0 / 60.0);
    c.frameRendered();
    CHECK_NEAR(c.time(), 1.0 / 60.0, 1e-9);
    CHECK_EQ(c.frame(), 1);
    c.advance(5.0);  // a 5 s stall only moves time by 0.25 s
    CHECK_NEAR(c.time(), 1.0 / 60.0 + 0.25, 1e-9);
    c.advance(-1.0);  // clock going backwards: ignored
    CHECK_NEAR(c.time(), 1.0 / 60.0 + 0.25, 1e-9);
    c.reset();
    CHECK_EQ(c.time(), 0.0);
}
