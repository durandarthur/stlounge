// Multipass semantics and channel inputs, rendered on a headless GLES 3 context.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "gfx/gl_caps.h"
#include "gfx/neutral_textures.h"
#include "gfx/render_target.h"
#include "gfx/texture_library.h"
#include "headless_gl.h"
#include "shadertoy/renderer.h"
#include "test.h"

using namespace sp;
using sptest::requireGl;

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
        resize(w, h);
    }
    void resize(int w, int h) {
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
    void compile(ShaderRenderer& r, const Shader& s) {
        r.load(s);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (r.update() == ShaderRenderer::State::Compiling && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (r.state() == ShaderRenderer::State::Failed) std::printf("    log: %s\n", r.errorLog().c_str());
        REQUIRE(r.state() == ShaderRenderer::State::Ready);
    }
    void frames(ShaderRenderer& r, int count, int firstFrame = 0) {
        for (int i = 0; i < count; ++i) {
            FrameUniforms u;
            u.frame = firstFrame + i;
            r.render(u, target);
        }
    }
    void pixel(int x, int y, float out[4]) {
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer());
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, out);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};

Pass buffer(int index, const std::string& code) {
    Pass p;
    p.type = PassType::Buffer;
    p.bufferIndex = index;
    p.code = code;
    return p;
}

Pass image(const std::string& code) {
    Pass p;
    p.type = PassType::Image;
    p.code = code;
    return p;
}

void readBuffer(Pass& p, int channel, int bufferIndex, Filter f = Filter::Nearest) {
    ChannelInput& in = p.inputs[static_cast<size_t>(channel)];
    in.type = InputType::Buffer;
    in.bufferIndex = bufferIndex;
    in.sampler = {f, Wrap::Clamp, false, false};
}

}  // namespace

TEST(multipass_buffer_reads_its_previous_frame) {
    Fixture f;
    Shader s;
    Pass a = buffer(0, "void mainImage(out vec4 c, in vec2 p) { c = texelFetch(iChannel0, ivec2(p), 0) + vec4(1.0); }");
    readBuffer(a, 0, 0);
    Pass img = image("void mainImage(out vec4 c, in vec2 p) { c = texelFetch(iChannel0, ivec2(p), 0); }");
    readBuffer(img, 0, 0);
    s.passes = {img, a};  // file order must not matter
    ShaderRenderer r(f.ctx());
    f.compile(r, s);
    f.frames(r, 3);
    float px[4];
    f.pixel(4, 4, px);
    CHECK_EQ(px[0], 3.0f);  // accumulated over 3 frames, starting from zero
    CHECK_EQ(px[1], 3.0f);
}

TEST(multipass_order_semantics_match_shadertoy) {
    // A reads B (B runs later: previous frame). B reads A (A ran earlier: this frame).
    Fixture f;
    Shader s;
    Pass a = buffer(0, "void mainImage(out vec4 c, in vec2 p) {\n"
                       "  c = vec4(float(iFrame), texelFetch(iChannel0, ivec2(0), 0).x, 0.0, 1.0); }");
    readBuffer(a, 0, 1);
    Pass b = buffer(1, "void mainImage(out vec4 c, in vec2 p) {\n"
                       "  c = vec4(texelFetch(iChannel0, ivec2(0), 0).x, 0.0, 0.0, 1.0); }");
    readBuffer(b, 0, 0);
    Pass img = image("void mainImage(out vec4 c, in vec2 p) {\n"
                     "  c = vec4(texelFetch(iChannel0, ivec2(0), 0).xy, texelFetch(iChannel1, ivec2(0), 0).x, 1.0); }");
    readBuffer(img, 0, 0);
    readBuffer(img, 1, 1);
    s.passes = {b, img, a};
    ShaderRenderer r(f.ctx());
    f.compile(r, s);
    f.frames(r, 6);  // iFrame 0..5
    float px[4];
    f.pixel(0, 0, px);
    CHECK_EQ(px[0], 5.0f);  // A this frame
    CHECK_EQ(px[1], 4.0f);  // A saw B from the previous frame
    CHECK_EQ(px[2], 5.0f);  // B saw A from this frame
}

TEST(multipass_resize_keeps_buffer_content) {
    Fixture f(8, 8);
    Shader s;
    Pass a = buffer(0, "void mainImage(out vec4 c, in vec2 p) {\n"
                       "  c = iFrame == 0 ? vec4(5.0) : texelFetch(iChannel0, ivec2(p), 0); }");
    readBuffer(a, 0, 0);
    Pass img = image("void mainImage(out vec4 c, in vec2 p) { c = texelFetch(iChannel0, ivec2(p), 0); }");
    readBuffer(img, 0, 0);
    s.passes = {a, img};
    ShaderRenderer r(f.ctx());
    f.compile(r, s);
    f.frames(r, 2);
    f.resize(32, 24);
    f.frames(r, 1, 2);
    float px[4];
    f.pixel(30, 20, px);
    CHECK_EQ(px[0], 5.0f);
    f.pixel(0, 0, px);
    CHECK_EQ(px[0], 5.0f);
}

TEST(multipass_buffer_mipmaps) {
    Fixture f(64, 64);
    Shader s;
    Pass a = buffer(0, "void mainImage(out vec4 c, in vec2 p) {\n"
                       "  ivec2 q = ivec2(p); c = vec4(float((q.x + q.y) & 1) * 4.0); }");
    Pass img = image("void mainImage(out vec4 c, in vec2 p) { c = textureLod(iChannel0, vec2(0.5), 6.0); }");
    readBuffer(img, 0, 0, Filter::Mipmap);
    s.passes = {a, img};
    ShaderRenderer r(f.ctx());
    f.compile(r, s);
    f.frames(r, 1);
    float px[4];
    f.pixel(0, 0, px);
    CHECK_NEAR(px[0], 2.0, 0.01);  // average of a 0/4 checkerboard
}

TEST(channel_rgba_noise_offset_on_gpu) {
    Fixture f(64, 64);
    Shader s;
    // For each pixel: |G(x,y) - R(x+37, y+17)| (vflip off) — must be 0 everywhere.
    Pass img = image(
        "void mainImage(out vec4 c, in vec2 p) {\n"
        "  ivec2 q = ivec2(p);\n"
        "  float g = texelFetch(iChannel0, q, 0).g;\n"
        "  float r = texelFetch(iChannel0, (q + ivec2(37, 17)) & 255, 0).r;\n"
        "  float gf = texelFetch(iChannel1, q, 0).g;\n"
        "  float rf = texelFetch(iChannel1, (q + ivec2(37, 256 - 17)) & 255, 0).r;\n"
        "  c = vec4(abs(g - r), abs(gf - rf), iChannelResolution[0].x + 1000.0 * iChannelResolution[2].z, 1.0);\n"
        "}\n");
    img.inputs[0].type = InputType::Texture;
    img.inputs[0].asset = "rgba_noise_medium";
    img.inputs[0].sampler = {Filter::Nearest, Wrap::Repeat, false, false};
    img.inputs[1] = img.inputs[0];
    img.inputs[1].sampler.vflip = true;
    img.inputs[2].type = InputType::Volume;
    img.inputs[2].asset = "gray_noise_3d";
    s.passes = {img};
    ShaderRenderer r(f.ctx());
    f.compile(r, s);
    CHECK(r.warnings().empty());
    f.frames(r, 1);
    float worst[2] = {0, 0};
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            float px[4];
            f.pixel(x, y, px);
            worst[0] = std::max(worst[0], px[0]);
            worst[1] = std::max(worst[1], px[1]);
        }
    CHECK_EQ(worst[0], 0.0f);
    CHECK_EQ(worst[1], 0.0f);
    float px[4];
    f.pixel(0, 0, px);
    CHECK_EQ(px[2], 256.0f + 1000.0f * 32.0f);  // iChannelResolution: 2D width, volume depth
}

TEST(channel_all_procedural_textures_upload) {
    Fixture f;
    for (const char* id : {"rgba_noise_small", "rgba_noise_medium", "gray_noise_small", "gray_noise_medium", "bayer",
                           "blue_noise", "gray_noise_3d", "rgba_noise_3d"}) {
        const GpuTexture* t = f.textures.get(id, true, false);
        CHECK(t != nullptr);
        if (t) CHECK(t->id != 0);
    }
    CHECK(f.textures.get("rgba_noise_medium", false, true) != nullptr);  // sRGB variant
    CHECK(f.textures.get("not_a_texture", true, false) == nullptr);
    CHECK_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST(channel_problems_become_warnings_not_failures) {
    Fixture f;
    Shader s;
    Pass img = image("void mainImage(out vec4 c, in vec2 p) {\n"
                     "  c = texture(iChannel0, vec2(0.5)) + texture(iChannel1, vec2(0.5)) +\n"
                     "      texture(iChannel2, vec2(0.5)) + texture(iChannel3, vec2(0.5)); }");
    img.inputs[0].type = InputType::Unsupported;
    img.inputs[0].unsupportedWhat = "keyboard";
    readBuffer(img, 1, 2);  // Buffer C doesn't exist
    img.inputs[2].type = InputType::Texture;
    img.inputs[2].asset = "shadertoy:Zz9Zz9";  // not identified
    img.inputs[3].type = InputType::Audio;
    s.passes = {img};
    ShaderRenderer r(f.ctx());
    f.compile(r, s);
    CHECK(r.usesAudio());
    CHECK_EQ(r.warnings().size(), size_t(3));
    for (const std::string& w : r.warnings()) std::printf("    warning: %s\n", w.c_str());
    f.frames(r, 1);
    float px[4];
    f.pixel(0, 0, px);
    CHECK_EQ(px[0], 0.0f);  // everything black
}

TEST(channel_look_alike_textures_and_cubemaps) {
    Fixture f;
    Shader s;
    // Cubemap: looking up must be sky (blue), looking down the ground (brown).
    Pass img = image("void mainImage(out vec4 c, in vec2 p) {\n"
                     "  vec3 up = texture(iChannel0, vec3(0.0, 1.0, 0.0)).rgb;\n"
                     "  vec3 down = texture(iChannel0, vec3(0.0, -1.0, 0.0)).rgb;\n"
                     "  c = p.x < 1.0 ? vec4(up, 1.0) : p.x < 2.0 ? vec4(down, 1.0)\n"
                     "                : vec4(iChannelResolution[1].xy, iChannelResolution[2].x, 1.0); }");
    img.inputs[0].type = InputType::Cubemap;
    img.inputs[0].asset = "forest";
    img.inputs[0].sampler = {Filter::Linear, Wrap::Clamp, true, false};  // vflip is ignored for cubemaps
    img.inputs[1].type = InputType::Texture;
    img.inputs[1].asset = "nyancat";
    img.inputs[2].type = InputType::Texture;
    img.inputs[2].asset = "font1";
    s.passes = {img};
    ShaderRenderer r(f.ctx());
    f.compile(r, s);
    for (const std::string& w : r.warnings()) std::printf("    warning: %s\n", w.c_str());
    CHECK(r.warnings().empty());
    f.frames(r, 1);
    float up[4], down[4], res[4];
    f.pixel(0, 0, up);
    f.pixel(1, 0, down);
    f.pixel(2, 0, res);
    CHECK(up[2] > up[0] + 0.2f);    // blue sky
    CHECK(down[0] > down[2]);       // brown ground
    CHECK_EQ(res[0], 256.0f);
    CHECK_EQ(res[1], 32.0f);
    CHECK_EQ(res[2], 1024.0f);
    for (const char* id : {"st_peters", "st_peters_blurred", "uffizi", "uffizi_blurred", "forest_blurred"}) {
        const GpuTexture* t = f.textures.get(id, false, false);
        REQUIRE(t != nullptr);
        CHECK_EQ(t->target, static_cast<GLenum>(GL_TEXTURE_CUBE_MAP));
    }
    CHECK_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}
