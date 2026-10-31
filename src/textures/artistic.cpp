#include "textures/artistic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace sp {

namespace {

// ---------------------------------------------------------------- noise

uint32_t hash3(int32_t x, int32_t y, int32_t z, uint32_t seed) {
    uint32_t h = seed ^ (static_cast<uint32_t>(x) * 0x8da6b343u) ^ (static_cast<uint32_t>(y) * 0xd8163841u) ^
                 (static_cast<uint32_t>(z) * 0xcb1ab31fu);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}
float hashf(int32_t x, int32_t y, int32_t z, uint32_t seed) {
    return static_cast<float>(hash3(x, y, z, seed) >> 8) * (1.0f / 16777216.0f);
}
int wrap(int i, int n) { return ((i % n) + n) % n; }
float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float clamp01(float x) { return std::min(1.0f, std::max(0.0f, x)); }
float smoothstep(float a, float b, float x) {
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// Value noise on a torus of px * py cells: (u, v) in [0, 1) map to one period.
float tileNoise(float u, float v, int px, int py, uint32_t seed) {
    const float x = u * static_cast<float>(px), y = v * static_cast<float>(py);
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const float tx = fade(x - fx), ty = fade(y - fy);
    const int x0 = wrap(ix, px), x1 = wrap(ix + 1, px), y0 = wrap(iy, py), y1 = wrap(iy + 1, py);
    return lerp(lerp(hashf(x0, y0, 0, seed), hashf(x1, y0, 0, seed), tx),
                lerp(hashf(x0, y1, 0, seed), hashf(x1, y1, 0, seed), tx), ty);
}

// Tileable fBm in [0, 1]: each octave doubles the period, so it still tiles.
float tileFbm(float u, float v, int px, int py, int octaves, uint32_t seed, float gain = 0.5f) {
    float sum = 0, amp = 1, total = 0;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * tileNoise(u, v, px, py, seed + static_cast<uint32_t>(o) * 1013u);
        total += amp;
        amp *= gain;
        px *= 2;
        py *= 2;
    }
    return sum / total;
}
float tileFbm(float u, float v, int period, int octaves, uint32_t seed, float gain = 0.5f) {
    return tileFbm(u, v, period, period, octaves, seed, gain);
}

// Ridged fBm: sharp crests where the noise crosses its middle (veins, cracks).
float tileRidged(float u, float v, int period, int octaves, uint32_t seed) {
    float sum = 0, amp = 1, total = 0;
    for (int o = 0; o < octaves; ++o) {
        const float n = tileNoise(u, v, period, period, seed + static_cast<uint32_t>(o) * 7919u);
        sum += amp * (1.0f - std::fabs(2.0f * n - 1.0f));
        total += amp;
        amp *= 0.5f;
        period *= 2;
    }
    return sum / total;
}

struct Cells {
    float f1 = 9, f2 = 9;  // distances to the nearest and second-nearest point, in cells
    uint32_t id = 0;       // hash of the nearest cell
};

// Tileable Worley (cellular) noise, one jittered point per cell.
Cells tileCells(float u, float v, int period, uint32_t seed, float jitter = 0.9f) {
    const float x = u * static_cast<float>(period), y = v * static_cast<float>(period);
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    Cells c;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const int cx = ix + dx, cy = iy + dy;
            const int wx = wrap(cx, period), wy = wrap(cy, period);
            const float px = static_cast<float>(cx) + 0.5f + jitter * (hashf(wx, wy, 1, seed) - 0.5f);
            const float py = static_cast<float>(cy) + 0.5f + jitter * (hashf(wx, wy, 2, seed) - 0.5f);
            const float d = std::hypot(px - x, py - y);
            if (d < c.f1) {
                c.f2 = c.f1;
                c.f1 = d;
                c.id = hash3(wx, wy, 3, seed);
            } else if (d < c.f2) {
                c.f2 = d;
            }
        }
    return c;
}

// 3D value noise and fBm, for cubemaps (seamless because they follow the direction).
float noise3(float x, float y, float z, uint32_t seed) {
    const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
    const float tx = fade(x - fx), ty = fade(y - fy), tz = fade(z - fz);
    auto h = [&](int a, int b, int c) { return hashf(ix + a, iy + b, iz + c, seed); };
    return lerp(lerp(lerp(h(0, 0, 0), h(1, 0, 0), tx), lerp(h(0, 1, 0), h(1, 1, 0), tx), ty),
                lerp(lerp(h(0, 0, 1), h(1, 0, 1), tx), lerp(h(0, 1, 1), h(1, 1, 1), tx), ty), tz);
}
float fbm3(float x, float y, float z, int octaves, uint32_t seed) {
    float sum = 0, amp = 1, total = 0;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * noise3(x, y, z, seed + static_cast<uint32_t>(o) * 131u);
        total += amp;
        amp *= 0.5f;
        x *= 2.03f;
        y *= 2.03f;
        z *= 2.03f;
    }
    return sum / total;
}

// ---------------------------------------------------------------- colour and images

struct Rgb {
    float r = 0, g = 0, b = 0;
};
Rgb operator*(Rgb c, float k) { return {c.r * k, c.g * k, c.b * k}; }
Rgb operator+(Rgb a, Rgb b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
Rgb mix(Rgb a, Rgb b, float t) { return a * (1.0f - t) + b * t; }

// Piecewise-linear palette through `stops` (evenly spaced over t in [0, 1]).
Rgb ramp(std::initializer_list<Rgb> stops, float t) {
    const std::vector<Rgb> s(stops);
    const float x = clamp01(t) * static_cast<float>(s.size() - 1);
    const size_t i = std::min(static_cast<size_t>(x), s.size() - 2);
    return mix(s[i], s[i + 1], x - static_cast<float>(i));
}

uint8_t toByte(float x) { return static_cast<uint8_t>(std::lround(clamp01(x) * 255.0f)); }

ImageData blankImage(int w, int h, int channels, int depth = 1) {
    ImageData img;
    img.width = w;
    img.height = h;
    img.depth = depth;
    img.channels = channels;
    img.pixels.assign(static_cast<size_t>(w) * h * depth * channels, 0);
    return img;
}

// Runs fn(row) for every row, spread over the CPU cores.
void parallelRows(int rows, const std::function<void(int)>& fn) {
    const int threads = std::max(1, std::min(8, static_cast<int>(std::thread::hardware_concurrency())));
    if (threads == 1 || rows < 64) {
        for (int y = 0; y < rows; ++y) fn(y);
        return;
    }
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            for (int y = t; y < rows; y += threads) fn(y);
        });
    for (std::thread& th : pool) th.join();
}

// A 2D RGBA texture from colour(u, v), u and v in [0, 1) (v = 0 is the top row).
ImageData paint(int w, int h, const std::function<Rgb(float, float)>& colour) {
    ImageData img = blankImage(w, h, 4);
    parallelRows(h, [&](int y) {
        uint8_t* p = img.pixels.data() + static_cast<size_t>(y) * w * 4;
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        for (int x = 0; x < w; ++x) {
            const Rgb c = colour((static_cast<float>(x) + 0.5f) / static_cast<float>(w), v);
            p[x * 4 + 0] = toByte(c.r);
            p[x * 4 + 1] = toByte(c.g);
            p[x * 4 + 2] = toByte(c.b);
            p[x * 4 + 3] = 255;
        }
    });
    return img;
}

// A cubemap from colour(direction), direction normalised.
ImageData paintCube(int size, const std::function<Rgb(float, float, float)>& colour) {
    ImageData img = blankImage(size, size, 4, 6);
    img.cube = true;
    parallelRows(size * 6, [&](int row) {
        const int face = row / size, y = row % size;
        uint8_t* p = img.pixels.data() + (static_cast<size_t>(face) * size + y) * size * 4;
        const float t = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size) - 1.0f;
        for (int x = 0; x < size; ++x) {
            const float s = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 1.0f;
            // Inverse of the GL cube map face selection (GLES 3.0 spec, table 3.21).
            float dx = 0, dy = 0, dz = 0;
            switch (face) {
                case 0: dx = 1, dy = -t, dz = -s; break;
                case 1: dx = -1, dy = -t, dz = s; break;
                case 2: dx = s, dy = 1, dz = t; break;
                case 3: dx = s, dy = -1, dz = -t; break;
                case 4: dx = s, dy = -t, dz = 1; break;
                default: dx = -s, dy = -t, dz = -1; break;
            }
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const Rgb c = colour(dx / len, dy / len, dz / len);
            p[x * 4 + 0] = toByte(c.r);
            p[x * 4 + 1] = toByte(c.g);
            p[x * 4 + 2] = toByte(c.b);
            p[x * 4 + 3] = 255;
        }
    });
    return img;
}

// ---------------------------------------------------------------- 2D textures

// Domain-warped fBm, the base of the Abstract and Organic look-alikes.
float warped(float u, float v, int period, uint32_t seed, float amount) {
    const float qx = tileFbm(u, v, period, 5, seed);
    const float qy = tileFbm(u + 0.31f, v + 0.17f, period, 5, seed + 1);
    return tileFbm(u + amount * (qx - 0.5f), v + amount * (qy - 0.5f), period, 6, seed + 2);
}

ImageData abstract1() {
    return paint(1024, 1024, [](float u, float v) {
        const float w = warped(u, v, 3, 101, 0.8f);
        const float d = tileFbm(u, v, 12, 3, 109);
        return ramp({{0.03f, 0.05f, 0.20f}, {0.08f, 0.35f, 0.55f}, {0.95f, 0.55f, 0.15f}, {1.0f, 0.92f, 0.75f}},
                    smoothstep(0.25f, 0.75f, w)) *
               (0.85f + 0.3f * d);
    });
}

ImageData abstract2() {
    return paint(512, 512, [](float u, float v) {
        const float w = warped(u, v, 3, 201, 1.0f);
        return ramp({{0.15f, 0.05f, 0.25f}, {0.55f, 0.15f, 0.50f}, {0.80f, 0.75f, 0.30f}, {0.35f, 0.65f, 0.35f}},
                    smoothstep(0.3f, 0.7f, w));
    });
}

ImageData abstract3() {
    return paint(1024, 1024, [](float u, float v) {
        const float r = tileRidged(u + 0.2f * tileFbm(u, v, 4, 4, 301), v, 4, 6, 303);
        const float t = smoothstep(0.35f, 0.95f, r);
        return ramp({{0.05f, 0.06f, 0.09f}, {0.30f, 0.36f, 0.45f}, {0.70f, 0.74f, 0.80f}, {0.98f, 0.98f, 1.0f}}, t);
    });
}

ImageData lichen() {
    return paint(1024, 1024, [](float u, float v) {
        const float base = tileFbm(u, v, 8, 6, 401);
        Rgb c = mix(Rgb{0.22f, 0.24f, 0.20f}, Rgb{0.45f, 0.47f, 0.40f}, base);
        const Cells k = tileCells(u, v, 20, 403);
        const float blob = 1.0f - smoothstep(0.25f, 0.45f, k.f1 + 0.25f * (tileFbm(u, v, 32, 3, 405) - 0.5f));
        const float hue = static_cast<float>(k.id & 255u) / 255.0f;
        const Rgb spot = mix(Rgb{0.62f, 0.68f, 0.38f}, Rgb{0.78f, 0.74f, 0.42f}, hue);
        c = mix(c, spot * (0.8f + 0.4f * tileFbm(u, v, 64, 2, 407)), blob);
        return c;
    });
}

ImageData organic1() {
    return paint(1024, 1024, [](float u, float v) {
        const float wu = u + 0.05f * (tileFbm(u, v, 6, 4, 501) - 0.5f);
        const Cells k = tileCells(wu, v, 14, 503);
        const float edge = smoothstep(0.0f, 0.25f, k.f2 - k.f1);
        const Rgb c = mix(Rgb{0.20f, 0.11f, 0.05f}, Rgb{0.72f, 0.55f, 0.36f}, edge);
        return c * (0.75f + 0.5f * tileFbm(u, v, 24, 4, 505));
    });
}

ImageData organic2() {
    // Mid-toned and colourful: shaders use it as nebula colour and as noise around 0.5.
    return paint(1024, 1024, [](float u, float v) {
        const float w = warped(u, v, 4, 601, 0.9f);
        const float r = tileRidged(u, v + 0.15f * tileFbm(u, v, 3, 4, 603), 6, 5, 605);
        const float t = clamp01(smoothstep(0.25f, 0.8f, w) * 0.8f + 0.35f * r * r - 0.1f);
        const Rgb c = ramp({{0.08f, 0.16f, 0.18f}, {0.20f, 0.45f, 0.40f}, {0.55f, 0.62f, 0.35f},
                            {0.90f, 0.70f, 0.38f}, {0.97f, 0.92f, 0.80f}},
                           t);
        return c * (0.85f + 0.3f * tileFbm(u, v, 32, 3, 607));
    });
}

ImageData organic3() {
    return paint(1024, 1024, [](float u, float v) {
        const float w = warped(u, v, 4, 701, 0.6f);
        return ramp({{0.25f, 0.04f, 0.03f}, {0.70f, 0.30f, 0.22f}, {0.93f, 0.75f, 0.62f}}, smoothstep(0.3f, 0.75f, w));
    });
}

ImageData organic4() {
    return paint(1024, 1024, [](float u, float v) {
        // Vertical bark-like streaks: the noise period is 8x finer across than along.
        const float s = smoothstep(0.25f, 0.75f, tileFbm(u, v, 16, 2, 6, 801));
        const float crack = smoothstep(0.34f, 0.22f, tileFbm(u, v, 24, 3, 3, 803));
        const Rgb c = ramp({{0.10f, 0.08f, 0.06f}, {0.36f, 0.28f, 0.20f}, {0.70f, 0.60f, 0.48f}}, s);
        return c * (1.0f - 0.8f * crack);
    });
}

ImageData pebbles() {
    return paint(512, 512, [](float u, float v) {
        const Cells k = tileCells(u, v, 10, 901, 0.75f);
        const float tone = static_cast<float>(k.id & 1023u) / 1023.0f;
        const Rgb stone = mix(Rgb{0.38f, 0.35f, 0.31f}, Rgb{0.70f, 0.64f, 0.56f}, tone);
        const float dome = 1.0f - 0.55f * k.f1;
        const float gap = smoothstep(0.02f, 0.12f, k.f2 - k.f1);
        const Rgb c = stone * (dome * (0.85f + 0.3f * tileFbm(u, v, 64, 3, 903)));
        return mix(Rgb{0.10f, 0.09f, 0.08f}, c, gap);
    });
}

ImageData rockTiles() {
    return paint(512, 512, [](float u, float v) {
        const int rows = 4, cols = 4;
        const float y = v * rows;
        const int row = static_cast<int>(y);
        const float x = u * cols + (row % 2 ? 0.5f : 0.0f);
        const int col = wrap(static_cast<int>(std::floor(x)), cols);
        const float fx = x - std::floor(x), fy = y - std::floor(y);
        const float edge = std::min(std::min(fx, 1.0f - fx), std::min(fy, 1.0f - fy));
        const float mortar = smoothstep(0.03f, 0.06f, edge + 0.02f * (tileFbm(u, v, 32, 3, 1001) - 0.5f));
        const float tone = hashf(col, row, 0, 1003);
        const Rgb stone = mix(Rgb{0.42f, 0.40f, 0.37f}, Rgb{0.62f, 0.58f, 0.52f}, tone) *
                          (0.75f + 0.45f * tileFbm(u, v, 16, 5, 1005));
        return mix(Rgb{0.18f, 0.17f, 0.15f}, stone * (0.8f + 0.2f * smoothstep(0.0f, 0.2f, edge)), mortar);
    });
}

ImageData rustyMetal() {
    return paint(512, 512, [](float u, float v) {
        const Rgb steel = Rgb{0.55f, 0.57f, 0.60f} * (0.6f + 0.6f * tileFbm(u, v, 64, 3, 1101));
        const float m = tileFbm(u, v, 4, 6, 1103) + 0.25f * (tileFbm(u, v, 32, 3, 1105) - 0.5f);
        const float rust = smoothstep(0.42f, 0.58f, m);
        const Rgb rustCol = ramp({{0.22f, 0.08f, 0.03f}, {0.55f, 0.24f, 0.08f}, {0.85f, 0.50f, 0.20f}},
                                 tileFbm(u, v, 16, 4, 1107));
        const float pit = smoothstep(0.75f, 0.90f, tileFbm(u, v, 48, 2, 1109));
        return mix(steel, rustCol, rust) * (1.0f - 0.7f * pit);
    });
}

ImageData stars() {
    // A star field over faint coloured nebulae (shaders multiply it into their own colours,
    // so the space between the stars must not be pure black).
    ImageData img = paint(512, 512, [](float u, float v) {
        const float n = warped(u, v, 3, 1203, 0.7f);
        const float glow = smoothstep(0.35f, 0.85f, n);
        const Rgb neb = ramp({{0.02f, 0.03f, 0.07f}, {0.10f, 0.08f, 0.22f}, {0.30f, 0.18f, 0.40f}, {0.45f, 0.40f, 0.60f}},
                             glow);
        return neb * (0.8f + 0.4f * tileFbm(u, v, 32, 3, 1205));
    });
    auto add = [&](int x, int y, float k, Rgb tint) {
        uint8_t* p = img.pixels.data() + (static_cast<size_t>(wrap(y, 512)) * 512 + wrap(x, 512)) * 4;
        p[0] = toByte(p[0] / 255.0f + k * tint.r);
        p[1] = toByte(p[1] / 255.0f + k * tint.g);
        p[2] = toByte(p[2] / 255.0f + k * tint.b);
    };
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x) {
            if (hashf(x, y, 0, 1201) > 0.03f) continue;
            const float b = std::pow(hashf(x, y, 1, 1201), 2.0f);
            const float hue = hashf(x, y, 2, 1201);
            const Rgb tint = hue < 0.3f ? Rgb{0.8f, 0.85f, 1.0f} : hue > 0.85f ? Rgb{1.0f, 0.85f, 0.7f} : Rgb{1, 1, 1};
            add(x, y, 0.25f + 0.75f * b, tint);
            if (b > 0.4f) {  // bright stars get a small glow
                for (int d = 0; d < 4; ++d) add(x + (d == 0) - (d == 1), y + (d == 2) - (d == 3), 0.4f * b, tint);
            }
        }
    return img;
}

ImageData wood() {
    return paint(1024, 1024, [](float u, float v) {
        const float t = v * 9.0f + 1.6f * tileFbm(u, v, 2, 6, 5, 1301);
        const float ring = t - std::floor(t);
        const float band = smoothstep(0.0f, 0.75f, ring) * (1.0f - smoothstep(0.85f, 1.0f, ring));
        const float grain = tileFbm(u, v, 64, 3, 3, 1303);
        const Rgb c = mix(Rgb{0.42f, 0.25f, 0.12f}, Rgb{0.78f, 0.56f, 0.33f}, band);
        return c * (0.85f + 0.3f * grain);
    });
}

ImageData london() {
    const int w = 512, h = 512;
    return paint(w, h, [](float u, float v) {
        // v = 0 is the top: sky, then a row of buildings with a clock tower, then the street.
        const Rgb sky = mix(Rgb{0.78f, 0.82f, 0.88f}, Rgb{0.58f, 0.64f, 0.74f}, v * 1.6f) *
                        (0.92f + 0.12f * tileFbm(u, v * 0.5f, 4, 4, 1401));
        const int col = static_cast<int>(u * 24.0f);
        float top = 0.40f + 0.18f * hashf(col, 0, 0, 1403);
        const bool tower = u > 0.68f && u < 0.74f;
        if (tower) top = u > 0.70f && u < 0.72f ? 0.05f : 0.12f;
        if (v < top) return sky;
        if (v > 0.86f) {
            const float lane = std::fabs(u - 0.5f) < 0.01f && std::fmod(v * 40.0f, 1.0f) < 0.5f ? 0.4f : 0.0f;
            return Rgb{0.20f, 0.20f, 0.21f} * (0.9f + 0.2f * tileFbm(u, v, 64, 2, 1405)) + Rgb{lane, lane, lane};
        }
        const float tone = hashf(col, 1, 0, 1403);
        Rgb wall = tower ? Rgb{0.62f, 0.52f, 0.34f} : mix(Rgb{0.32f, 0.27f, 0.24f}, Rgb{0.55f, 0.50f, 0.44f}, tone);
        if (tower && v > 0.17f && v < 0.22f) {  // the clock face
            const float d = std::hypot((u - 0.71f) * 1.0f, (v - 0.195f) * 1.0f);
            if (d < 0.022f) wall = Rgb{0.92f, 0.90f, 0.82f};
        }
        const float wx = std::fmod(u * 96.0f, 1.0f), wy = std::fmod(v * 64.0f, 1.0f);
        const bool window = wx > 0.3f && wx < 0.7f && wy > 0.3f && wy < 0.75f && v > top + 0.03f;
        if (window) wall = hashf(static_cast<int>(u * 96.0f), static_cast<int>(v * 64.0f), 0, 1407) > 0.7f
                               ? Rgb{0.85f, 0.75f, 0.45f}
                               : wall * 0.45f;
        return wall * (0.85f + 0.2f * tileFbm(u, v, 32, 2, 1409));
    });
}

// Six frames of a small pixel-art cat on a pop-tart, 40 pixels apart, on a
// transparent background (256 x 32, RGBA).
ImageData nyancat() {
    ImageData img = blankImage(256, 32, 4);
    auto put = [&](int x, int y, Rgb c) {
        if (x < 0 || x >= 256 || y < 0 || y >= 32) return;
        uint8_t* p = img.pixels.data() + (static_cast<size_t>(y) * 256 + x) * 4;
        p[0] = toByte(c.r), p[1] = toByte(c.g), p[2] = toByte(c.b), p[3] = 255;
    };
    auto rect = [&](int x0, int y0, int x1, int y1, Rgb c) {
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) put(x, y, c);
    };
    const Rgb black{0, 0, 0}, crust{1.0f, 0.80f, 0.60f}, pink{1.0f, 0.60f, 0.80f}, dot{1.0f, 0.2f, 0.6f};
    const Rgb gray{0.6f, 0.6f, 0.6f}, white{1, 1, 1};
    for (int f = 0; f < 6; ++f) {
        const int ox = f * 40, bob = (f / 2) % 2;
        // tail and legs move with the frame
        rect(ox + 1, 16 + (f % 3) - 1, ox + 6, 18 + (f % 3) - 1, gray);
        for (int l = 0; l < 4; ++l) rect(ox + 7 + l * 6 + (f % 2), 25, ox + 10 + l * 6 + (f % 2), 28, gray);
        // the pop-tart body
        rect(ox + 5, 6 + bob, ox + 29, 26 + bob, black);
        rect(ox + 6, 7 + bob, ox + 28, 25 + bob, crust);
        rect(ox + 8, 9 + bob, ox + 26, 23 + bob, pink);
        for (int d = 0; d < 7; ++d) put(ox + 10 + (d * 5) % 15, 11 + bob + (d * 3) % 10, dot);
        // the head
        rect(ox + 23, 13, ox + 37, 24, black);
        rect(ox + 24, 14, ox + 36, 23, gray);
        rect(ox + 24, 11, ox + 27, 14, gray);
        rect(ox + 33, 11, ox + 36, 14, gray);
        rect(ox + 26, 16, ox + 28, 18, black);
        rect(ox + 32, 16, ox + 34, 18, black);
        put(ox + 26, 16, white);
        put(ox + 32, 16, white);
        rect(ox + 28, 20, ox + 32, 21, black);
        rect(ox + 25, 19, ox + 26, 21, pink);
        rect(ox + 34, 19, ox + 35, 21, pink);
    }
    return img;
}

// ---------------------------------------------------------------- cubemaps
// `detail` is the number of fBm octaves: the blurred variants use fewer and
// softer transitions, which is what a blurred environment map mostly is.

ImageData forest(int size, int detail, float soft) {
    return paintCube(size, [=](float x, float y, float z) {
        const float az = std::atan2(z, x);
        const float ring = fbm3(std::cos(az) * 3.0f, std::sin(az) * 3.0f, 0.0f, detail, 1501);
        const float treeline = -0.02f + 0.30f * ring;
        Rgb sky = mix(Rgb{0.85f, 0.88f, 0.90f}, Rgb{0.30f, 0.50f, 0.82f}, std::sqrt(clamp01(y)));
        const float cloud = smoothstep(0.55f, 0.75f, fbm3(x * 2.5f, y * 2.5f, z * 2.5f, detail, 1503));
        sky = mix(sky, Rgb{0.95f, 0.95f, 0.95f}, cloud * clamp01(y * 3.0f));
        const Rgb trees = Rgb{0.07f, 0.16f, 0.06f} * (0.6f + 0.8f * fbm3(x * 24.0f, y * 24.0f, z * 24.0f, detail, 1505));
        const Rgb ground = Rgb{0.26f, 0.22f, 0.12f} * (0.7f + 0.6f * fbm3(x * 12.0f, y * 12.0f, z * 12.0f, detail, 1507));
        const Rgb low = mix(ground, trees, smoothstep(-0.25f - soft, -0.12f + soft, y));
        return mix(low, sky, smoothstep(treeline - 0.01f - soft, treeline + 0.01f + soft, y));
    });
}

ImageData stPeters(int size, int detail, float soft) {
    return paintCube(size, [=](float x, float y, float z) {
        const float az = std::atan2(z, x);
        const float n = fbm3(x * 8.0f, y * 8.0f, z * 8.0f, detail, 1601);
        // dome: bright gold with ribs
        const float ribs = 0.5f + 0.5f * std::cos(az * 16.0f);
        const Rgb dome = mix(Rgb{0.85f, 0.70f, 0.42f}, Rgb{1.0f, 0.93f, 0.72f}, ribs * (1.0f - soft) + soft * 0.5f);
        // walls: warm stone with pilasters and high windows
        const float pilaster = smoothstep(0.6f - soft, 0.8f + soft, 0.5f + 0.5f * std::cos(az * 12.0f));
        Rgb wall = mix(Rgb{0.45f, 0.34f, 0.22f}, Rgb{0.70f, 0.58f, 0.42f}, pilaster) * (0.8f + 0.4f * n);
        const float win = smoothstep(0.75f - soft, 0.9f + soft * 0.1f, 0.5f + 0.5f * std::cos(az * 6.0f)) *
                          smoothstep(0.25f - soft, 0.32f, y) * (1.0f - smoothstep(0.42f, 0.48f + soft, y));
        wall = mix(wall, Rgb{1.0f, 0.96f, 0.86f}, win);
        // floor: marble checker
        const float fx = x / std::max(0.05f, -y), fz = z / std::max(0.05f, -y);
        const bool check = (static_cast<int>(std::floor(fx * 2.0f)) + static_cast<int>(std::floor(fz * 2.0f))) % 2;
        const Rgb floorCol = (check ? Rgb{0.62f, 0.56f, 0.50f} : Rgb{0.38f, 0.30f, 0.26f}) * (0.85f + 0.3f * n);
        const Rgb floorMix = mix(floorCol, Rgb{0.50f, 0.43f, 0.38f}, soft * 2.0f);
        const Rgb upper = mix(wall, dome, smoothstep(0.50f - soft, 0.58f + soft, y));
        return mix(floorMix, upper, smoothstep(-0.22f - soft, -0.15f + soft, y));
    });
}

ImageData uffizi(int size, int detail, float soft) {
    return paintCube(size, [=](float x, float y, float z) {
        const float n = fbm3(x * 6.0f, y * 6.0f, z * 6.0f, detail, 1701);
        // a long gallery along z: bright skylights in the ceiling, beige walls, darker floor
        const float along = z / std::max(0.05f, y);
        const float sky = (1.0f - smoothstep(0.08f, 0.16f + soft, std::fabs(x))) *
                          smoothstep(0.65f - soft, 0.75f, y) *
                          (0.6f + 0.4f * smoothstep(0.3f - soft, 0.5f + soft, std::fabs(std::sin(along * 3.0f))));
        const Rgb ceiling = mix(Rgb{0.70f, 0.68f, 0.62f}, Rgb{1, 1, 1}, sky);
        const float frames = smoothstep(0.7f - soft, 0.85f + soft, 0.5f + 0.5f * std::sin(std::atan2(z, x) * 10.0f)) *
                             smoothstep(-0.05f - soft, 0.05f, y) * (1.0f - smoothstep(0.3f, 0.4f + soft, y));
        const Rgb wall = mix(Rgb{0.62f, 0.58f, 0.50f} * (0.85f + 0.3f * n), Rgb{0.35f, 0.25f, 0.18f}, frames * 0.8f);
        const Rgb floorCol = Rgb{0.34f, 0.29f, 0.24f} * (0.8f + 0.4f * n);
        const Rgb upper = mix(wall, ceiling, smoothstep(0.45f - soft, 0.55f + soft, y));
        return mix(floorCol, upper, smoothstep(-0.30f - soft, -0.22f + soft, y));
    });
}

}  // namespace

ImageData makeArtisticTexture(const std::string& id) {
    if (id == "abstract1") return abstract1();
    if (id == "abstract2") return abstract2();
    if (id == "abstract3") return abstract3();
    if (id == "font1") return makeFontTexture();
    if (id == "lichen") return lichen();
    if (id == "london") return london();
    if (id == "nyancat") return nyancat();
    if (id == "organic1") return organic1();
    if (id == "organic2") return organic2();
    if (id == "organic3") return organic3();
    if (id == "organic4") return organic4();
    if (id == "pebbles") return pebbles();
    if (id == "rock_tiles") return rockTiles();
    if (id == "rusty_metal") return rustyMetal();
    if (id == "stars") return stars();
    if (id == "wood") return wood();
    if (id == "forest") return forest(512, 5, 0.0f);
    if (id == "forest_blurred") return forest(128, 2, 0.12f);
    if (id == "st_peters") return stPeters(512, 5, 0.0f);
    if (id == "st_peters_blurred") return stPeters(128, 2, 0.12f);
    if (id == "uffizi") return uffizi(512, 5, 0.0f);
    if (id == "uffizi_blurred") return uffizi(128, 2, 0.12f);
    return {};
}

}  // namespace sp
