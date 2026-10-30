// Volume A rendering and real-world samples, on a headless GLES 3 context.

#include <algorithm>
#include <chrono>
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
