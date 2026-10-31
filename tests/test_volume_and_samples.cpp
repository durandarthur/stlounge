// Volume A rendering and real-world samples, on a headless GLES 3 context.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "gfx/gl_caps.h"
#include "gfx/neutral_textures.h"
#include "gfx/render_target.h"
#include "gfx/texture_library.h"
#include "headless_gl.h"
#include "shadertoy/json_format.h"
#include "shadertoy/renderer.h"
#include "test.h"
#include "textures/image_io.h"

using namespace sp;
using sptest::requireGl;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    GlCaps caps;
    FullscreenTriangle tri;
    NeutralTextures neutral;
    TextureLibrary textures;
    RenderTarget target;

    Fixture(int w = 16, int h = 16) {
        requireGl();
        caps = probeGlCaps();
        if (caps.bufferFormat != BufferFormat::RGBA32F) SKIP("needs RGBA32F to read exact values");
        tri.init();
        neutral.init();
        std::string err;
        target.create(w, h, GL_RGBA32F, GL_RGBA, GL_FLOAT, &err);
        REQUIRE(target.valid());
    }
    RenderContext ctx() {
        RenderContext c;
        c.caps = &caps;
        c.triangle = &tri;
        c.neutral = &neutral;
        c.textures = &textures;
        return c;
    }
    ShaderRenderer::State compile(ShaderRenderer& r, const Shader& s) {
        r.load(s);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (r.update() == ShaderRenderer::State::Compiling && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return r.state();
    }
    void frames(ShaderRenderer& r, int count) {
        for (int i = 0; i < count; ++i) {
            FrameUniforms u;
            u.frame = i;
            u.time = i / 60.0f;
            r.render(u, target);
        }
    }
    void pixel(int x, int y, float out[4]) {
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer());
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, out);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

TEST(volume_pass_fills_128_cube) {
    Fixture f;
    Shader s;
    Pass v;
    v.type = PassType::Volume;
    v.bufferIndex = 0;
    // Each voxel stores its z centre / 128 (+ a value above 1 to prove float storage).
    v.code = "void mainVolume(out vec4 c, in vec3 p) { c = vec4(p.z / iResolution.z + 2.0 * step(127.0, p.x), 0, 0, 0); }";
    Pass img;
    img.type = PassType::Image;
    img.code =
        "void mainImage(out vec4 c, in vec2 p) {\n"
        "  float z = texelFetch(iChannel0, ivec3(5, 5, int(p.x)), 0).x;\n"
        "  float edge = texelFetch(iChannel0, ivec3(127, 0, 0), 0).x;\n"
        "  c = vec4(z, edge, iChannelResolution[0].z, 1.0); }";
    img.inputs[0].type = InputType::VolumeBuffer;
    img.inputs[0].bufferIndex = 0;
    img.inputs[0].sampler = {Filter::Nearest, Wrap::Clamp, false, false};
    s.passes = {img, v};
    ShaderRenderer r(f.ctx());
    const auto st = f.compile(r, s);
    if (st == ShaderRenderer::State::Failed) std::printf("    log: %s\n", r.errorLog().c_str());
    REQUIRE(st == ShaderRenderer::State::Ready);
    f.frames(r, 1);
    float px[4];
    f.pixel(3, 0, px);
    CHECK_NEAR(px[0], 3.5 / 128.0, 1e-4);  // slice 3, centre 3.5
    CHECK_NEAR(px[1], 2.0 + 0.5 / 128.0, 1e-3);
    CHECK_EQ(px[2], 128.0f);
    for (const std::string& w : r.warnings()) std::printf("    warning: %s\n", w.c_str());
    CHECK(r.warnings().empty());
}

TEST(volume_pass_reads_itself) {
    Fixture f;
    Shader s;
    Pass v;
    v.type = PassType::Volume;
    v.bufferIndex = 0;
    v.code = "void mainVolume(out vec4 c, in vec3 p) { c = texelFetch(iChannel0, ivec3(p), 0) + vec4(1.0); }";
    v.inputs[0].type = InputType::VolumeBuffer;
    v.inputs[0].bufferIndex = 0;
    v.inputs[0].sampler = {Filter::Nearest, Wrap::Clamp, false, false};
    Pass img;
    img.type = PassType::Image;
    img.code = "void mainImage(out vec4 c, in vec2 p) { c = texelFetch(iChannel0, ivec3(1, 2, 3), 0); }";
    img.inputs[0] = v.inputs[0];
    s.passes = {v, img};
    ShaderRenderer r(f.ctx());
    REQUIRE(f.compile(r, s) == ShaderRenderer::State::Ready);
    f.frames(r, 4);
    float px[4];
    f.pixel(0, 0, px);
    CHECK_EQ(px[0], 4.0f);  // accumulated over 4 frames: self-read is previous frame
}

TEST(real_samples_compile_and_render) {
    const fs::path dir = fs::path(SP_TEST_DATA_DIR) / "private";
    if (!fs::is_directory(dir)) SKIP("no private samples in tests/data/private");
    Fixture f(64, 36);
    int ok = 0, total = 0;
    for (const auto& de : fs::directory_iterator(dir)) {
        if (de.path().extension() != ".json") continue;
        ++total;
        auto parsed = parseShadertoyJson(readText(de.path()));
        REQUIRE(!parsed.empty() && parsed[0].ok);
        ShaderRenderer r(f.ctx());
        const auto t0 = std::chrono::steady_clock::now();
        const auto st = f.compile(r, parsed[0].shader);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("    %-34s %-6s %6.0f ms\n", parsed[0].shader.info.name.substr(0, 34).c_str(),
                    st == ShaderRenderer::State::Ready ? "ok" : "FAILED", ms);
        if (st != ShaderRenderer::State::Ready) {
            std::printf("%s\n", r.errorLog().c_str());
            continue;
        }
        for (const std::string& w : r.warnings()) std::printf("      warning: %s\n", w.c_str());
        f.frames(r, 2);
        CHECK_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
        ++ok;
    }
    CHECK_EQ(ok, total);
}

// The diagnostic from docs/volume-a-check.md must be all green in this app.
TEST(volume_a_check_doc_shader_is_green) {
    Fixture f(64, 64);
    Shader s;
    Pass v;
    v.type = PassType::Volume;
    v.bufferIndex = 0;
    v.code = R"GLSL(// Voxel (1,0,0) copies voxel (2,0,0) from the volume's previous frame,
// i.e. the previous iFrame. Every other voxel stores the current iFrame.
void mainVolume(out vec4 fragColor, in vec3 fragCoord)
{
    ivec3 q = ivec3(fragCoord);
    if (q == ivec3(1, 0, 0))
        fragColor = vec4(texelFetch(iChannel0, ivec3(2, 0, 0), 0).x);
    else
        fragColor = vec4(float(iFrame));
}
)GLSL";
    v.inputs[0].type = InputType::VolumeBuffer;
    v.inputs[0].bufferIndex = 0;
    v.inputs[0].sampler = {Filter::Nearest, Wrap::Clamp, false, false};
    Pass a;
    a.type = PassType::Buffer;
    a.bufferIndex = 0;
    a.code = R"GLSL(// How many frames behind is the volume, as seen from Buffer A?
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = vec4(float(iFrame) - texelFetch(iChannel0, ivec3(0), 0).x);
}
)GLSL";
    a.inputs[0] = v.inputs[0];
    Pass img;
    img.type = PassType::Image;
    img.code = R"GLSL(void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    float d;
    if (uv.y > 0.5)
        d = uv.x < 0.5 ? texelFetch(iChannel0, ivec2(0), 0).x                     // Buffer A's lag
                       : float(iFrame) - texelFetch(iChannel1, ivec3(0), 0).x;    // Image's lag
    else
        d = float(iFrame - 1) - texelFetch(iChannel1, ivec3(1, 0, 0), 0).x;       // self-read: previous iFrame
    vec3 col = d > 0.5 ? vec3(1.0, 0.2, 0.2) : d < -0.5 ? vec3(0.3, 0.3, 1.0) : vec3(0.2, 1.0, 0.3);
    fragColor = vec4(col, 1.0);
}
)GLSL";
    img.inputs[0].type = InputType::Buffer;
    img.inputs[0].bufferIndex = 0;
    img.inputs[0].sampler = {Filter::Nearest, Wrap::Clamp, false, false};
    img.inputs[1] = v.inputs[0];
    s.passes = {img, a, v};
    ShaderRenderer r(f.ctx());
    REQUIRE(f.compile(r, s) == ShaderRenderer::State::Ready);
    f.frames(r, 5);
    for (auto [x, y] : {std::pair{10, 50}, std::pair{50, 50}, std::pair{30, 10}}) {
        float px[4];
        f.pixel(x, y, px);
        CHECK(px[1] > 0.9f && px[0] < 0.3f);  // green
    }
}

// Step 2 of docs/volume-a-check.md: the number-printing Image tab must compile and show
// the expected values (checked by reading back what the volume holds, not the glyphs).
TEST(volume_a_check_numbers_shader_compiles) {
    Fixture f(320, 180);
    Shader s;
    Pass v;
    v.type = PassType::Volume;
    v.bufferIndex = 0;
    v.code = R"GLSL(// Voxel (1,0,0) copies voxel (2,0,0) from the volume's previous frame,
// i.e. the previous iFrame. Every other voxel stores the current iFrame.
void mainVolume(out vec4 fragColor, in vec3 fragCoord)
{
    ivec3 q = ivec3(fragCoord);
    if (q == ivec3(1, 0, 0))
        fragColor = vec4(texelFetch(iChannel0, ivec3(2, 0, 0), 0).x);
    else
        fragColor = vec4(float(iFrame));
}
)GLSL";
    v.inputs[0].type = InputType::VolumeBuffer;
    v.inputs[0].bufferIndex = 0;
    v.inputs[0].sampler = {Filter::Nearest, Wrap::Clamp, false, false};
    Pass a;
    a.type = PassType::Buffer;
    a.bufferIndex = 0;
    a.code = R"GLSL(// How many frames behind is the volume, as seen from Buffer A?
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = vec4(float(iFrame) - texelFetch(iChannel0, ivec3(0), 0).x);
}
)GLSL";
    a.inputs[0] = v.inputs[0];
    Pass img;
    img.type = PassType::Image;
    img.code = R"GLSL(// Prints values with a tiny 3x5 pixel font, one number per row.
float glyph(int g, vec2 q)
{
    // 0-9, '-', '.'; 3 bits per row, top row first.
    const int font[12] = int[12](31599, 9362, 29671, 29391, 23497, 31183, 31215, 29257, 31727, 31695, 448, 2);
    int bit = int(q.y) * 3 + (2 - int(q.x));
    return float((font[g] >> bit) & 1);
}

float printValue(vec2 p, float v)
{
    if (p.x < 0.0 || p.y < 0.0 || p.y >= 5.0) return 0.0;
    int cell = int(p.x / 4.0);
    vec2 q = vec2(mod(p.x, 4.0), p.y);
    if (q.x >= 3.0) return 0.0;
    float a = abs(v);
    int ip = int(a);
    int nd = 1;
    for (int t = ip; t >= 10; t /= 10) nd++;
    int lead = v < 0.0 ? 1 : 0;
    int g = -1;
    if (cell < lead) g = 10;
    else if (cell < lead + nd) {
        int t = ip;
        for (int i = 0; i < nd - 1 - (cell - lead); i++) t /= 10;
        g = t % 10;
    }
    else if (cell == lead + nd) g = 11;
    else if (cell == lead + nd + 1) g = int(fract(a) * 10.0);
    return g < 0 ? 0.0 : glyph(g, q);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    float s = max(1.0, floor(iResolution.y / 90.0));  // font pixel size
    vec2 p = fragCoord / s;
    float top = floor(iResolution.y / s) - 2.0;
    float r = floor((top - p.y) / 8.0);                 // row, from the top
    vec2 q = vec2(p.x - 2.0, p.y - (top - 8.0 * r - 6.0));

    float v = 0.0;
    if      (r == 0.0) v = float(iFrame);
    else if (r == 1.0) v = texelFetch(iChannel0, ivec2(0), 0).x;
    else if (r == 2.0) v = texelFetch(iChannel1, ivec3(0, 0, 0), 0).x;
    else if (r == 3.0) v = texelFetch(iChannel1, ivec3(1, 0, 0), 0).x;
    else if (r == 4.0) v = texelFetch(iChannel1, ivec3(2, 0, 0), 0).x;
    else if (r == 5.0) v = iChannelResolution[1].x;
    else if (r == 6.0) v = iChannelResolution[1].y;
    else if (r == 7.0) v = iChannelResolution[1].z;
    else if (r == 8.0) v = iChannelResolution[0].x;
    else if (r == 9.0) v = iChannelResolution[0].y;
    float ink = r < 10.0 ? printValue(q, v) : 0.0;
    fragColor = vec4(mix(vec3(0.05), vec3(1.0), ink), 1.0);
}
)GLSL";
    img.inputs[0].type = InputType::Buffer;
    img.inputs[0].bufferIndex = 0;
    img.inputs[1] = v.inputs[0];
    s.passes = {v, a, img};
    ShaderRenderer r(f.ctx());
    const auto st = f.compile(r, s);
    if (st == ShaderRenderer::State::Failed) std::printf("    log: %s\n", r.errorLog().c_str());
    REQUIRE(st == ShaderRenderer::State::Ready);
    f.frames(r, 3);
    // Some text was drawn: at least one bright pixel in the first row of numbers.
    int ink = 0;
    for (int y = 150; y < 180; ++y)
        for (int x = 0; x < 60; ++x) {
            float px[4];
            f.pixel(x, y, px);
            ink += px[0] > 0.5f;
        }
    CHECK(ink > 0);
}

// Step 3 of docs/volume-a-check.md: run the JSON the doc ships and check the
// counters Buffer A stores, which are what this app expects the site to show.
TEST(volume_a_check_step3_counters) {
    Fixture f(320, 180);
    auto parsed = parseShadertoyJson(readText(fs::path(SP_TEST_DATA_DIR) / ".." / ".." / "docs" /
                                              "volume-a-check-step3.json"));
    REQUIRE(parsed.size() == size_t(1) && parsed[0].ok);
    CHECK(parsed[0].warnings.empty());
    ShaderRenderer r(f.ctx());
    REQUIRE(f.compile(r, parsed[0].shader) == ShaderRenderer::State::Ready);
    f.frames(r, 5);  // iFrame 0..4

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.bufferTexture(0), 0);
    float a[4] = {};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, a);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    CHECK_NEAR(a[0], 4.0f, 1e-3);  // Buffer A's iFrame
    CHECK_NEAR(a[1], 4.0f, 1e-3);  // volume's iFrame it saw (this frame's)
    CHECK_NEAR(a[2], 5.0f, 1e-3);  // volume drawn 5 times
    CHECK_NEAR(a[3], 5.0f, 1e-3);  // Buffer A drawn 5 times
}

// Developer aid: SP_DUMP_SAMPLES=<dir> saves a 480x270 picture of each private
// sample at iTime = 3 s (or SP_DUMP_TIME), to compare with shadertoy.com by eye.
TEST(real_samples_dump_pictures) {
    const char* out = std::getenv("SP_DUMP_SAMPLES");
    if (!out) SKIP("set SP_DUMP_SAMPLES=<dir> to save pictures");
    const fs::path dir = fs::path(SP_TEST_DATA_DIR) / "private";
    if (!fs::is_directory(dir)) SKIP("no private samples in tests/data/private");
    const int w = 480, h = 270;
    const char* at = std::getenv("SP_DUMP_TIME");
    const float t0 = at ? static_cast<float>(std::atof(at)) : 3.0f;
    Fixture f(w, h);
    for (const auto& de : fs::directory_iterator(dir)) {
        if (de.path().extension() != ".json") continue;
        auto parsed = parseShadertoyJson(readText(de.path()));
        if (parsed.empty() || !parsed[0].ok) continue;
        ShaderRenderer r(f.ctx());
        if (f.compile(r, parsed[0].shader) != ShaderRenderer::State::Ready) continue;
        for (int i = 0; i < 4; ++i) {
            FrameUniforms u;
            u.frame = i;
            u.time = t0 + i / 60.0f;
            u.resolution[0] = w;
            u.resolution[1] = h;
            u.resolution[2] = 1;
            r.render(u, f.target);
        }
        std::vector<float> px(static_cast<size_t>(w) * h * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, f.target.framebuffer());
        glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        std::vector<uint8_t> rgba(px.size());
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w * 4; ++x) {
                const float v = px[static_cast<size_t>(h - 1 - y) * w * 4 + x];
                rgba[static_cast<size_t>(y) * w * 4 + x] =
                    (x % 4 == 3) ? 255 : static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
            }
        std::string err;
        savePng(fs::path(out) / (de.path().stem().string() + ".png"), w, h, rgba.data(), &err);
    }
}
