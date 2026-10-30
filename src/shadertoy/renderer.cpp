#include "shadertoy/renderer.h"

#include <algorithm>

#include "shadertoy/wrapper.h"

namespace sp {

ShaderRenderer::~ShaderRenderer() {
    for (auto& p : passes_)
        if (p->program) glDeleteProgram(p->program);
}

void ShaderRenderer::load(const Shader& shader) {
    for (auto& p : passes_)
        if (p->program) glDeleteProgram(p->program);
    passes_.clear();
    shader_ = shader;
    errorLog_.clear();
    compileMs_ = 0;

    if (!shader_.imagePass()) {
        state_ = State::Failed;
        errorLog_ = "The shader has no Image pass.";
        return;
    }
    // Milestone 2: only the Image pass is rendered. Buffers arrive in milestone 3.
    for (const Pass& def : shader_.passes) {
        if (def.type != PassType::Image) continue;
        auto p = std::make_unique<PassRuntime>();
        p->def = def;
        p->build.start(kFullscreenVertexShader, wrapPass(shader_.common, def), ctx_.caps->parallelCompile);
        passes_.push_back(std::move(p));
    }
    state_ = State::Compiling;
    update();
}

ShaderRenderer::State ShaderRenderer::update() {
    if (state_ != State::Compiling) return state_;
    bool pending = false;
    for (auto& p : passes_) {
        if (p->program) continue;
        switch (p->build.poll()) {
            case ProgramBuild::Status::Pending:
                pending = true;
                break;
            case ProgramBuild::Status::Ready:
                compileMs_ = std::max(compileMs_, p->build.elapsedMs());
                p->program = p->build.takeProgram();
                lookupLocations(*p);
                break;
            case ProgramBuild::Status::Failed:
                compileMs_ = std::max(compileMs_, p->build.elapsedMs());
                if (!errorLog_.empty()) errorLog_ += "\n";
                errorLog_ += "--- " + p->def.name() + " ---\n" + remapCompileLog(p->build.log(), p->def.name());
                break;
            case ProgramBuild::Status::Idle:
                break;
        }
    }
    if (pending) return state_;
    state_ = errorLog_.empty() ? State::Ready : State::Failed;
    return state_;
}

void ShaderRenderer::lookupLocations(PassRuntime& p) {
    const GLuint prog = p.program;
    Locations& l = p.loc;
    l.resolution = glGetUniformLocation(prog, "iResolution");
    l.time = glGetUniformLocation(prog, "iTime");
    l.timeDelta = glGetUniformLocation(prog, "iTimeDelta");
    l.frameRate = glGetUniformLocation(prog, "iFrameRate");
    l.frame = glGetUniformLocation(prog, "iFrame");
    l.channelTime = glGetUniformLocation(prog, "iChannelTime");
    l.channelResolution = glGetUniformLocation(prog, "iChannelResolution");
    l.mouse = glGetUniformLocation(prog, "iMouse");
    l.date = glGetUniformLocation(prog, "iDate");
    l.sampleRate = glGetUniformLocation(prog, "iSampleRate");
    for (int i = 0; i < 4; ++i) {
        const std::string name = "iChannel" + std::to_string(i);
        l.channel[i] = glGetUniformLocation(prog, name.c_str());
    }
}

void ShaderRenderer::drawPass(PassRuntime& p, const FrameUniforms& u, GLuint fbo, int width, int height) {
    const Locations& l = p.loc;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glUseProgram(p.program);

    glUniform3f(l.resolution, static_cast<float>(width), static_cast<float>(height), 1.0f);
    glUniform1f(l.time, u.time);
    glUniform1f(l.timeDelta, u.timeDelta);
    glUniform1f(l.frameRate, u.frameRate);
    glUniform1i(l.frame, u.frame);
    glUniform1fv(l.channelTime, 4, u.channelTime);
    glUniform4fv(l.mouse, 1, u.mouse);
    glUniform4fv(l.date, 1, u.date);
    glUniform1f(l.sampleRate, u.sampleRate);

    float channelRes[12] = {};
    for (int i = 0; i < 4; ++i) {
        const ChannelInput& in = p.def.inputs[static_cast<size_t>(i)];
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
        // Milestone 2: every channel gets the neutral texture of the right kind.
        if (in.type == InputType::Cubemap)
            glBindTexture(GL_TEXTURE_CUBE_MAP, ctx_.neutral->cube());
        else if (in.type == InputType::Volume)
            glBindTexture(GL_TEXTURE_3D, ctx_.neutral->volume());
        else
            glBindTexture(GL_TEXTURE_2D, ctx_.neutral->tex2D());
        glUniform1i(l.channel[i], i);
    }
    glUniform3fv(l.channelResolution, 4, channelRes);

    ctx_.triangle->draw();

    for (int i = 3; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glBindTexture(GL_TEXTURE_3D, 0);
    }
    glUseProgram(0);
}

void ShaderRenderer::render(const FrameUniforms& u, const RenderTarget& output) {
    if (state_ != State::Ready || !output.valid()) return;
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    for (auto& p : passes_) {
        if (p->def.type == PassType::Image) drawPass(*p, u, output.framebuffer(), output.width(), output.height());
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}  // namespace sp
