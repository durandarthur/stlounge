#include <cstdio>

#include "gfx/gl_caps.h"
#include "headless_gl.h"
#include "test.h"

using namespace sp;
using sptest::requireGl;

TEST(gl_context_is_gles3) {
    requireGl();
    GlCaps caps = probeGlCaps();
    std::printf("    %s\n", caps.summary().c_str());
    CHECK(caps.isGles3());
    CHECK(caps.version.rfind("OpenGL ES", 0) == 0);
    CHECK(caps.maxTextureSize >= 2048);
}

TEST(gl_buffer_format_matches_extensions) {
    requireGl();
    GlCaps caps = probeGlCaps();
    if (caps.has("GL_EXT_color_buffer_float") && caps.has("GL_OES_texture_float_linear")) {
        CHECK(caps.bufferFormat == BufferFormat::RGBA32F);
        CHECK(caps.bufferLinearFilter);
    }
    GLenum internal, format, type;
    bufferFormatGl(caps.bufferFormat, &internal, &format, &type);
    CHECK(canRenderTo(internal, format, type));
    CHECK(canRenderTo(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE));
}

TEST(gl_float_target_keeps_values_outside_0_1) {
    requireGl();
    GlCaps caps = probeGlCaps();
    if (caps.bufferFormat != BufferFormat::RGBA32F) SKIP("no RGBA32F on this driver");

    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 2, 2, 0, GL_RGBA, GL_FLOAT, nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, 2, 2);
    glClearColor(12.5f, -3.25f, 0.125f, 1000.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    float px[4] = {};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, px);
    CHECK_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    CHECK_NEAR(px[0], 12.5, 1e-4);
    CHECK_NEAR(px[1], -3.25, 1e-4);
    CHECK_NEAR(px[2], 0.125, 1e-4);
    CHECK_NEAR(px[3], 1000.0, 1e-2);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
}
