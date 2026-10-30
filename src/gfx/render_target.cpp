#include "gfx/render_target.h"

#include <utility>

#include "gfx/program.h"

namespace sp {

const char* const kFullscreenVertexShader = R"(#version 300 es
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

RenderTarget& RenderTarget::operator=(RenderTarget&& o) noexcept {
    if (this != &o) {
        destroy();
        std::swap(texture_, o.texture_);
        std::swap(fbo_, o.fbo_);
        std::swap(width_, o.width_);
        std::swap(height_, o.height_);
    }
    return *this;
}

bool RenderTarget::create(int width, int height, GLenum internalFormat, GLenum format, GLenum type,
                          std::string* error) {
    destroy();
    while (glGetError() != GL_NO_ERROR) {}
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), width, height, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    const GLenum err = glGetError();
    if (status != GL_FRAMEBUFFER_COMPLETE || err != GL_NO_ERROR) {
        if (error)
            *error = "render target " + std::to_string(width) + "x" + std::to_string(height) +
                     " incomplete (status " + std::to_string(status) + ", GL error " + std::to_string(err) + ")";
        destroy();
        return false;
    }
    width_ = width;
    height_ = height;
    clear();
    return true;
}

void RenderTarget::clear() {
    if (!fbo_) return;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::destroy() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (texture_) glDeleteTextures(1, &texture_);
    fbo_ = texture_ = 0;
    width_ = height_ = 0;
}

FullscreenTriangle::~FullscreenTriangle() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

void FullscreenTriangle::init() {
    if (!vao_) glGenVertexArrays(1, &vao_);
}

void FullscreenTriangle::draw() const {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

Blitter::~Blitter() {
    if (program_) glDeleteProgram(program_);
}

bool Blitter::init(std::string* error) {
    static const char* fs = R"(#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec2 uSize;
out vec4 outColor;
void main() { outColor = vec4(texture(uTex, gl_FragCoord.xy / uSize).rgb, 1.0); }
)";
    ProgramBuild b;
    b.start(kFullscreenVertexShader, fs, false);
    if (b.status() != ProgramBuild::Status::Ready) {
        if (error) *error = "blit shader failed: " + b.log();
        return false;
    }
    program_ = b.takeProgram();
    texLoc_ = glGetUniformLocation(program_, "uTex");
    sizeLoc_ = glGetUniformLocation(program_, "uSize");
    return true;
}

void Blitter::draw(GLuint texture, int dstWidth, int dstHeight, const FullscreenTriangle& tri) const {
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(texLoc_, 0);
    glUniform2f(sizeLoc_, static_cast<float>(dstWidth), static_cast<float>(dstHeight));
    glViewport(0, 0, dstWidth, dstHeight);
    tri.draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

}  // namespace sp
