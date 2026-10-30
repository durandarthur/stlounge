#include "gfx/gl_caps.h"

#include <sstream>

#include "platform/log.h"

namespace sp {

const char* bufferFormatName(BufferFormat f) {
    switch (f) {
        case BufferFormat::RGBA32F: return "RGBA32F";
        case BufferFormat::RGBA16F: return "RGBA16F";
        case BufferFormat::RGBA8: return "RGBA8";
    }
    return "?";
}

void bufferFormatGl(BufferFormat f, GLenum* internalFormat, GLenum* format, GLenum* type) {
    *format = GL_RGBA;
    switch (f) {
        case BufferFormat::RGBA32F: *internalFormat = GL_RGBA32F; *type = GL_FLOAT; break;
        case BufferFormat::RGBA16F: *internalFormat = GL_RGBA16F; *type = GL_HALF_FLOAT; break;
        case BufferFormat::RGBA8: *internalFormat = GL_RGBA8; *type = GL_UNSIGNED_BYTE; break;
    }
}

static std::string glString(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? reinterpret_cast<const char*>(s) : "";
}

bool canRenderTo(GLenum internalFormat, GLenum format, GLenum type) {
    while (glGetError() != GL_NO_ERROR) {}

    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), 4, 4, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    bool ok = glGetError() == GL_NO_ERROR;

    if (ok) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (ok) {
            glViewport(0, 0, 4, 4);
            glClearColor(2.0f, -1.0f, 0.5f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ok = glGetError() == GL_NO_ERROR;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    while (glGetError() != GL_NO_ERROR) {}
    return ok;
}

GlCaps probeGlCaps() {
    GlCaps c;
    c.vendor = glString(GL_VENDOR);
    c.renderer = glString(GL_RENDERER);
    c.version = glString(GL_VERSION);
    c.glslVersion = glString(GL_SHADING_LANGUAGE_VERSION);
    glGetIntegerv(GL_MAJOR_VERSION, &c.major);
    glGetIntegerv(GL_MINOR_VERSION, &c.minor);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &c.maxTextureSize);

    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        if (const GLubyte* e = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)))
            c.extensions.insert(reinterpret_cast<const char*>(e));
    }
    while (glGetError() != GL_NO_ERROR) {}
    c.parallelCompile = c.has("GL_KHR_parallel_shader_compile");

    // Buffer format fallback chain. In GLES 3.0 neither float format is
    // renderable without an extension, so each step is also verified with a
    // real framebuffer.
    const bool float32 = c.has("GL_EXT_color_buffer_float");
    const bool float16 = float32 || c.has("GL_EXT_color_buffer_half_float");
    const bool linear32 = c.has("GL_OES_texture_float_linear");

    if (float32 && linear32 && canRenderTo(GL_RGBA32F, GL_RGBA, GL_FLOAT)) {
        c.bufferFormat = BufferFormat::RGBA32F;
        c.bufferFormatNote = "full precision (EXT_color_buffer_float)";
    } else if (float16 && canRenderTo(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT)) {
        c.bufferFormat = BufferFormat::RGBA16F;
        c.bufferFormatNote = !float32 ? "no EXT_color_buffer_float; half precision fallback"
                             : !linear32 ? "32-bit float can't be filtered linearly; half precision fallback"
                                         : "32-bit float target failed; half precision fallback";
    } else if (float32 && canRenderTo(GL_RGBA32F, GL_RGBA, GL_FLOAT)) {
        c.bufferFormat = BufferFormat::RGBA32F;
        c.bufferLinearFilter = false;
        c.bufferFormatNote = "32-bit float without linear filtering (buffers sampled with nearest)";
    } else {
        c.bufferFormat = BufferFormat::RGBA8;
        c.bufferFormatNote = "no float render targets; 8-bit fallback, shaders that store state in buffers will break";
    }
    return c;
}

std::string GlCaps::summary() const {
    std::ostringstream s;
    s << renderer << " | " << version << " | " << extensions.size() << " extensions"
      << " | buffers " << bufferFormatName(bufferFormat) << (bufferLinearFilter ? "" : " (nearest only)")
      << " | parallel compile " << (parallelCompile ? "yes" : "no");
    return s.str();
}

}  // namespace sp
