// Compiles and renders real shaders on a headless GLES 3 context.

#include <cstdio>
#include <string>

#include "app/builtin_shader.h"
#include "gfx/gl_caps.h"
#include "gfx/neutral_textures.h"
#include "gfx/program.h"
#include "gfx/render_target.h"
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
    RenderTarget target;

    explicit Fixture(int w = 64, int h = 32, bool floatTarget = true) {
        requireGl();
        caps = probeGlCaps();
        tri.init();
        neutral.init();
        std::string err;
        if (floatTarget && caps.bufferFormat == BufferFormat::RGBA32F)
            target.create(w, h, GL_RGBA32F, GL_RGBA, GL_FLOAT, &err);
        else
            target.create(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, &err);
        REQUIRE(target.valid());
    }
    RenderContext ctx() const { return RenderContext{&caps, &tri, &neutral}; }
    bool floatTarget() const { return caps.bufferFormat == BufferFormat::RGBA32F; }

    // Loads and waits for compilation (polling, like the app does).
    ShaderRenderer::State compile(ShaderRenderer& r, const Shader& s) {
        r.load(s);
        for (int i = 0; i < 100000 && r.update() == ShaderRenderer::State::Compiling; ++i) {}
        return r.state();
    }

    void readPixel(int x, int y, float out[4]) {
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer());
        if (floatTarget()) {
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, out);
        } else {
            unsigned char px[4];
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            for (int i = 0; i < 4; ++i) out[i] = px[i] / 255.0f;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};

Shader imageShader(const std::string& code, const std::string& common = "") {
    Shader s;
    s.info.name = "test";
    s.common = common;
    Pass p;
    p.type = PassType::Image;
    p.code = code;
    s.passes.push_back(p);
    return s;
}

}  // namespace

TEST(renderer_draws_solid_color) {
    Fixture f;
    ShaderRenderer r(f.ctx());
    REQUIRE(f.compile(r, imageShader("void mainImage(out vec4 c, in vec2 p) { c = vec4(0.25, 0.5, 0.75, 0.1); }")) ==
            ShaderRenderer::State::Ready);
    r.render(FrameUniforms{}, f.target);
    float px[4];
    f.readPixel(3, 3, px);
    CHECK_NEAR(px[0], 0.25, 0.01);
    CHECK_NEAR(px[1], 0.5, 0.01);
    CHECK_NEAR(px[2], 0.75, 0.01);
    CHECK_NEAR(px[3], 1.0, 0.001);  // Image pass alpha forced to 1
}

TEST(renderer_uniform_values_reach_the_shader) {
    Fixture f(64, 32);
    if (!f.floatTarget()) SKIP("needs a float target to read exact values");
    ShaderRenderer r(f.ctx());
    const char* code = R"(
void mainImage(out vec4 c, in vec2 p) {
    int col = int(p.x);
    if (col == 0) c = vec4(iResolution, 0.0);
    else if (col == 1) c = vec4(iTime, iTimeDelta, float(iFrame), 0.0);
    else if (col == 2) c = vec4(iMouse.xyz, 0.0);
    else if (col == 3) c = vec4(iMouse.w, iFrameRate, iSampleRate, 0.0);
    else if (col == 4) c = vec4(iDate.xyz, 0.0);
    else if (col == 5) c = vec4(iDate.w, iChannelTime[2], iChannelResolution[1].x, 0.0);
    else c = vec4(p, 0.0, 0.0);
}
)";
    REQUIRE(f.compile(r, imageShader(code)) == ShaderRenderer::State::Ready);

    FrameUniforms u;
    u.time = 12.5f;
    u.timeDelta = 0.016f;
    u.frame = 42;
    u.mouse[0] = 10; u.mouse[1] = 20; u.mouse[2] = -10; u.mouse[3] = 20;
    u.frameRate = 59.5f;
    u.sampleRate = 48000;
    u.date[0] = 2026; u.date[1] = 9; u.date[2] = 30; u.date[3] = 3600.5f;
    u.channelTime[2] = 7.0f;
    r.render(u, f.target);

    float px[4];
    f.readPixel(0, 0, px);
    CHECK_EQ(px[0], 64.0f);
    CHECK_EQ(px[1], 32.0f);
    CHECK_EQ(px[2], 1.0f);
    f.readPixel(1, 0, px);
    CHECK_EQ(px[0], 12.5f);
    CHECK_NEAR(px[1], 0.016, 1e-6);
    CHECK_EQ(px[2], 42.0f);
    f.readPixel(2, 0, px);
    CHECK_EQ(px[0], 10.0f);
    CHECK_EQ(px[1], 20.0f);
    CHECK_EQ(px[2], -10.0f);
    f.readPixel(3, 0, px);
    CHECK_EQ(px[0], 20.0f);
    CHECK_NEAR(px[1], 59.5, 1e-4);
    CHECK_EQ(px[2], 48000.0f);
    f.readPixel(4, 0, px);
    CHECK_EQ(px[0], 2026.0f);
    CHECK_EQ(px[1], 9.0f);
    CHECK_EQ(px[2], 30.0f);
    f.readPixel(5, 0, px);
    CHECK_EQ(px[0], 3600.5f);
    CHECK_EQ(px[1], 7.0f);
    CHECK_EQ(px[2], 0.0f);  // unbound channel resolution
    f.readPixel(10, 7, px);  // fragCoord is the pixel centre
    CHECK_EQ(px[0], 10.5f);
    CHECK_EQ(px[1], 7.5f);
}

TEST(renderer_common_tab_is_shared) {
    Fixture f;
    ShaderRenderer r(f.ctx());
    REQUIRE(f.compile(r, imageShader("void mainImage(out vec4 c, in vec2 p) { c = vec4(half3(), 1.0); }",
                                     "vec3 half3() { return vec3(0.5); }")) == ShaderRenderer::State::Ready);
    r.render(FrameUniforms{}, f.target);
    float px[4];
    f.readPixel(0, 0, px);
    CHECK_NEAR(px[0], 0.5, 0.01);
}

TEST(renderer_cubemap_and_volume_channels_compile) {
    Fixture f;
    ShaderRenderer r(f.ctx());
    Shader s = imageShader(
        "void mainImage(out vec4 c, in vec2 p) {\n"
        "  c = texture(iChannel0, p / iResolution.xy) + texture(iChannel1, vec3(1, 0, 0)) +\n"
        "      texture(iChannel2, vec3(0.5));\n"
        "}\n");
    s.passes[0].inputs[1].type = InputType::Cubemap;
    s.passes[0].inputs[2].type = InputType::Volume;
    REQUIRE(f.compile(r, s) == ShaderRenderer::State::Ready);
    r.render(FrameUniforms{}, f.target);
    float px[4];
    f.readPixel(0, 0, px);
    CHECK_NEAR(px[0], 0.0, 0.001);  // neutral textures are black
    CHECK_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST(renderer_error_log_points_at_tab_and_line) {
    Fixture f;
    ShaderRenderer r(f.ctx());
    const std::string code =
        "void mainImage(out vec4 c, in vec2 p)\n"  // line 1
        "{\n"                                      // line 2
        "    c = vec4(undefinedThing, 1.0);\n"     // line 3
        "}\n";
    CHECK(f.compile(r, imageShader(code)) == ShaderRenderer::State::Failed);
    std::printf("    log: %s\n", r.errorLog().c_str());
    CHECK(r.errorLog().find("Image:3") != std::string::npos);

    ShaderRenderer r2(f.ctx());
    CHECK(f.compile(r2, imageShader("void mainImage(out vec4 c, in vec2 p) { c = vec4(1); }",
                                    "float ok() { return 1.0; }\nfloat bad() { return nope; }\n")) ==
          ShaderRenderer::State::Failed);
    std::printf("    log: %s\n", r2.errorLog().c_str());
    CHECK(r2.errorLog().find("Common:2") != std::string::npos);
}

TEST(renderer_missing_image_pass_fails_cleanly) {
    Fixture f;
    ShaderRenderer r(f.ctx());
    Shader s;
    r.load(s);
    CHECK(r.state() == ShaderRenderer::State::Failed);
    CHECK(!r.errorLog().empty());
    r.render(FrameUniforms{}, f.target);  // must be a harmless no-op
}

TEST(program_build_parallel_polling) {
    Fixture f;
    if (!f.caps.parallelCompile) SKIP("no KHR_parallel_shader_compile");
    enableParallelCompile();
    ProgramBuild b;
    b.start(kFullscreenVertexShader,
            "#version 300 es\nprecision highp float; out vec4 o; void main() { o = vec4(1); }", true);
    int polls = 0;
    while (b.poll() == ProgramBuild::Status::Pending && polls < 1000000) ++polls;
    CHECK(b.status() == ProgramBuild::Status::Ready);
    GLuint p = b.takeProgram();
    CHECK(p != 0);
    glDeleteProgram(p);
}

TEST(renderer_builtin_test_pattern_compiles) {
    Fixture f;
    ShaderRenderer r(f.ctx());
    CHECK(f.compile(r, imageShader(kBuiltinShader)) == ShaderRenderer::State::Ready);
    if (r.state() == ShaderRenderer::State::Failed) std::printf("    log: %s\n", r.errorLog().c_str());
}
