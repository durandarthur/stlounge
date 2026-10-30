#include "shadertoy/renderer.h"

#include <algorithm>

#include "platform/log.h"
#include "shadertoy/wrapper.h"
#include "textures/builtin_list.h"

namespace sp {

namespace {

const char* kindName(InputType t) {
    switch (t) {
        case InputType::Cubemap: return "cubemap";
        case InputType::Volume: return "volume";
        default: return "texture";
    }
}

GLenum targetFor(InputType t) {
    switch (t) {
        case InputType::Cubemap: return GL_TEXTURE_CUBE_MAP;
        case InputType::Volume: return GL_TEXTURE_3D;
        default: return GL_TEXTURE_2D;
    }
}

}  // namespace

ShaderRenderer::~ShaderRenderer() {
    for (auto& p : passes_)
        if (p->program) glDeleteProgram(p->program);
    if (!samplers_.empty()) glDeleteSamplers(static_cast<GLsizei>(samplers_.size()), samplers_.data());
    destroyVolume();
}

void ShaderRenderer::destroyVolume() {
    for (GLuint& t : volume_.textures)
        if (t) glDeleteTextures(1, &t), t = 0;
    if (volume_.fbo) glDeleteFramebuffers(1, &volume_.fbo), volume_.fbo = 0;
}

bool ShaderRenderer::ensureVolume() {
    if (!volume_.exists || volume_.failed) return false;
    if (volume_.textures[0]) return true;

    // ShaderToy: 128^3, 1 channel, Float32, linear. Fall back if not renderable.
    struct Candidate {
        GLenum internal, format, type;
        bool ok;
        const char* name;
    };
    const GlCaps& c = *ctx_.caps;
    const bool f32 = c.has("GL_EXT_color_buffer_float");
    const bool f16 = f32 || c.has("GL_EXT_color_buffer_half_float");
    const Candidate candidates[] = {
        // R32F only if it can also be filtered linearly; R16F and R8 always can.
        {GL_R32F, GL_RED, GL_FLOAT, f32 && c.has("GL_OES_texture_float_linear"), "R32F"},
        {GL_R16F, GL_RED, GL_HALF_FLOAT, f16, "R16F"},
        {GL_R8, GL_RED, GL_UNSIGNED_BYTE, true, "R8"},
    };
    const int n = VolumeTargets::kSize;
    const int count = volume_.selfRead ? 2 : 1;
    glGenFramebuffers(1, &volume_.fbo);
    for (const Candidate& cand : candidates) {
        if (!cand.ok) continue;
        while (glGetError() != GL_NO_ERROR) {}
        bool good = true;
        for (int i = 0; i < count && good; ++i) {
            glGenTextures(1, &volume_.textures[i]);
            glBindTexture(GL_TEXTURE_3D, volume_.textures[i]);
            glTexImage3D(GL_TEXTURE_3D, 0, static_cast<GLint>(cand.internal), n, n, n, 0, cand.format, cand.type, nullptr);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, volume_.fbo);
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, volume_.textures[i], 0, 0);
            good = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && glGetError() == GL_NO_ERROR;
            if (good) {
                // Start from zero, like buffers.
                glViewport(0, 0, n, n);
                glClearColor(0, 0, 0, 0);
                for (int z = 0; z < n; ++z) {
                    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, volume_.textures[i], 0, z);
                    glClear(GL_COLOR_BUFFER_BIT);
                }
            }
        }
        glBindTexture(GL_TEXTURE_3D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (good) {
            if (cand.internal == GL_R32F) LOG_DEBUG("Volume A: %dx%dx%d %s", n, n, n, cand.name);
            else LOG_WARN("Volume A: R32F not renderable here, using %s", cand.name);
            return true;
        }
        for (GLuint& t : volume_.textures)
            if (t) glDeleteTextures(1, &t), t = 0;
    }
    glDeleteFramebuffers(1, &volume_.fbo);
    volume_.fbo = 0;
    volume_.failed = true;
    warn("Volume A: no renderable 3D texture format on this GPU; inputs reading it are black.");
    return false;
}

void ShaderRenderer::warn(const std::string& message) {
    if (std::find(warnings_.begin(), warnings_.end(), message) == warnings_.end()) warnings_.push_back(message);
}

void ShaderRenderer::load(const Shader& shader) {
    for (auto& p : passes_)
        if (p->program) glDeleteProgram(p->program);
    passes_.clear();
    buffers_ = {};
    destroyVolume();
    volume_ = {};
    if (!samplers_.empty()) glDeleteSamplers(static_cast<GLsizei>(samplers_.size()), samplers_.data());
    samplers_.clear();
    shader_ = shader;
    errorLog_.clear();
    warnings_.clear();
    compileMs_ = 0;
    usesAudio_ = false;

    const Pass* image = shader_.imagePass();
    if (!image) {
        state_ = State::Failed;
        errorLog_ = "The shader has no Image pass.";
        return;
    }

    // Order: Volume A, Buffer A, B, C, D, then Image (whatever the file order).
    // ShaderToy's exact position of Volume A is unknown; first lets buffers
    // read this frame's volume.
    std::vector<const Pass*> ordered;
    if (const Pass* vol = shader_.volumePass()) {
        ordered.push_back(vol);
        volume_.exists = true;
        for (const ChannelInput& in : vol->inputs)
            if (in.type == InputType::VolumeBuffer) volume_.selfRead = true;
        volume_.dynamic = passIsDynamic(shader_.common, *vol);
    }
    for (int b = 0; b < 4; ++b) {
        const Pass* found = nullptr;
        for (const Pass& p : shader_.passes) {
            if (p.type != PassType::Buffer || p.bufferIndex != b) continue;
            if (found) warn("Buffer " + std::string(1, static_cast<char>('A' + b)) + " appears twice; using the first.");
            else found = &p;
        }
        if (found) {
            ordered.push_back(found);
            buffers_[static_cast<size_t>(b)].exists = true;
        }
    }
    ordered.push_back(image);

    for (const Pass* def : ordered) {
        auto p = std::make_unique<PassRuntime>();
        p->def = *def;
        passes_.push_back(std::move(p));
    }
    // Bindings need every buffer's existence known, so resolve after collecting.
    for (auto& p : passes_) {
        resolveBindings(*p);
        p->build.start(kFullscreenVertexShader, wrapPass(shader_.common, p->def), ctx_.caps->parallelCompile);
    }
    state_ = State::Compiling;
    update();
}

GLuint ShaderRenderer::makeSampler(const SamplerSettings& s, bool allowLinear, bool hasMipmaps) {
    GLuint sm = 0;
    glGenSamplers(1, &sm);
    samplers_.push_back(sm);
    GLenum minF = GL_NEAREST, magF = GL_NEAREST;
    if (allowLinear && s.filter != Filter::Nearest) {
        magF = GL_LINEAR;
        minF = (s.filter == Filter::Mipmap && hasMipmaps) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
    } else if (s.filter == Filter::Mipmap && hasMipmaps) {
        minF = GL_NEAREST_MIPMAP_NEAREST;
    }
    const GLenum wrap = s.wrap == Wrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glSamplerParameteri(sm, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minF));
    glSamplerParameteri(sm, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(magF));
    glSamplerParameteri(sm, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrap));
    glSamplerParameteri(sm, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrap));
    glSamplerParameteri(sm, GL_TEXTURE_WRAP_R, static_cast<GLint>(wrap));
    return sm;
}

void ShaderRenderer::resolveBindings(PassRuntime& p) {
    const std::string passName = p.def.name();
    for (size_t i = 0; i < 4; ++i) {
        ChannelInput& in = p.def.inputs[i];
        Binding& b = p.bindings[i];
        const std::string where = passName + " iChannel" + std::to_string(i) + ": ";
        auto neutral = [&](InputType kind) {
            b.target = targetFor(kind);
            b.texture = kind == InputType::Cubemap  ? ctx_.neutral->cube()
                        : kind == InputType::Volume ? ctx_.neutral->volume()
                                                    : ctx_.neutral->tex2D();
            b.resolution[0] = b.resolution[1] = b.resolution[2] = 0;
        };

        switch (in.type) {
            case InputType::None:
                neutral(InputType::Texture);
                break;

            case InputType::Texture:
            case InputType::Cubemap:
            case InputType::Volume: {
                const BuiltinTexture* desc = findBuiltinTexture(in.asset);
                const GpuTexture* tex = ctx_.textures ? ctx_.textures->get(in.asset, in.sampler.vflip, in.sampler.srgb) : nullptr;
                if (tex && tex->target != targetFor(in.type)) {
                    warn(where + "'" + in.asset + "' is not a " + kindName(in.type) + "; using black.");
                    tex = nullptr;
                }
                if (!tex) {
                    const std::string stPrefix = "shadertoy:";
                    if (!desc && in.asset.rfind(stPrefix, 0) == 0)
                        warn(where + "ShaderToy " + kindName(in.type) + " " + in.asset.substr(stPrefix.size()) +
                             " isn't identified yet; using black.");
                    else if (!desc)
                        warn(where + "unknown " + kindName(in.type) + " '" + in.asset + "'; using black.");
                    else if (!desc->available)
                        warn(where + "'" + desc->label + "' is not available yet; using black.");
                    neutral(in.type);
                    break;
                }
                b.target = tex->target;
                b.texture = tex->id;
                b.resolution[0] = static_cast<float>(tex->width);
                b.resolution[1] = static_cast<float>(tex->height);
                b.resolution[2] = static_cast<float>(tex->depth);
                b.sampler = makeSampler(in.sampler, true, true);
                break;
            }

            case InputType::Buffer:
                if (in.bufferIndex < 0 || in.bufferIndex > 3 || !buffers_[static_cast<size_t>(in.bufferIndex)].exists) {
                    const std::string name = in.bufferIndex >= 0 && in.bufferIndex <= 3
                                                 ? std::string("Buffer ") + static_cast<char>('A' + in.bufferIndex)
                                                 : "an unknown buffer";
                    warn(where + "reads " + name + ", which this shader doesn't define; using black.");
                    neutral(InputType::Texture);
                    break;
                }
                b.target = GL_TEXTURE_2D;
                b.bufferIndex = in.bufferIndex;
                // Mipmaps need a filterable format; without one, sample the base level.
                if (in.sampler.filter == Filter::Mipmap && ctx_.caps->bufferLinearFilter)
                    buffers_[static_cast<size_t>(in.bufferIndex)].needsMipmaps = true;
                b.sampler = makeSampler(in.sampler, ctx_.caps->bufferLinearFilter, ctx_.caps->bufferLinearFilter);
                break;

            case InputType::VolumeBuffer:
                if (!volume_.exists) {
                    warn(where + "reads Volume A, which this shader doesn't define; using black.");
                    neutral(InputType::Volume);
                    break;
                }
                b.target = GL_TEXTURE_3D;
                b.volume = true;
                b.texture = ctx_.neutral->volume();  // until the volume exists
                b.resolution[0] = b.resolution[1] = b.resolution[2] = VolumeTargets::kSize;
                b.sampler = makeSampler(in.sampler, true, false);  // every chosen format is filterable
                break;

            case InputType::Audio:
                usesAudio_ = true;
                b.audio = true;
                b.target = GL_TEXTURE_2D;
                b.texture = ctx_.neutral->tex2D();
                b.resolution[0] = 512;
                b.resolution[1] = 2;
                b.resolution[2] = 1;
                b.sampler = makeSampler(in.sampler, true, false);
                break;

            case InputType::Unsupported:
                warn(where + (in.unsupportedWhat.empty() ? std::string("this input") : in.unsupportedWhat) +
                     " is not supported; using black.");
                neutral(InputType::Texture);
                break;
        }
    }
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
                p->build.cancel();
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
    l.volumeZ = glGetUniformLocation(prog, kVolumeSliceUniform);
}

void ShaderRenderer::ensureBuffers(int width, int height) {
    GLenum internal, format, type;
    bufferFormatGl(ctx_.caps->bufferFormat, &internal, &format, &type);
    for (size_t i = 0; i < 4; ++i) {
        BufferTargets& b = buffers_[i];
        if (!b.exists) continue;
        RenderTarget& latest = b.targets[b.read];
        if (latest.valid() && latest.width() == width && latest.height() == height) continue;

        RenderTarget fresh[2];
        std::string error;
        for (auto& t : fresh) {
            if (!t.create(width, height, internal, format, type, &error)) {
                LOG_ERROR("Buffer %c: %s", static_cast<char>('A' + i), error.c_str());
                return;
            }
        }
        // Keep the content across a resize, scaled, like ShaderToy does.
        if (latest.valid()) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, latest.framebuffer());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fresh[0].framebuffer());
            glBlitFramebuffer(0, 0, latest.width(), latest.height(), 0, 0, width, height, GL_COLOR_BUFFER_BIT,
                              ctx_.caps->bufferLinearFilter ? GL_LINEAR : GL_NEAREST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        }
        b.targets[0] = std::move(fresh[0]);
        b.targets[1] = std::move(fresh[1]);
        b.read = 0;
        if (b.needsMipmaps) {
            glBindTexture(GL_TEXTURE_2D, b.targets[0].texture());
            glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
}

GLuint ShaderRenderer::bufferTexture(int index) const {
    if (index < 0 || index > 3) return 0;
    const BufferTargets& b = buffers_[static_cast<size_t>(index)];
    return b.exists ? b.targets[b.read].texture() : 0;
}

void ShaderRenderer::drawPass(PassRuntime& p, const FrameUniforms& u, GLuint fbo, int width, int height, int slices,
                              GLuint volumeTexture) {
    const Locations& l = p.loc;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glUseProgram(p.program);

    glUniform3f(l.resolution, static_cast<float>(width), static_cast<float>(height),
                slices > 0 ? static_cast<float>(slices) : 1.0f);
    glUniform1f(l.time, u.time);
    glUniform1f(l.timeDelta, u.timeDelta);
    glUniform1f(l.frameRate, u.frameRate);
    glUniform1i(l.frame, u.frame);
    glUniform1fv(l.channelTime, 4, u.channelTime);
    glUniform4fv(l.mouse, 1, u.mouse);
    glUniform4fv(l.date, 1, u.date);
    glUniform1f(l.sampleRate, u.sampleRate);

    float channelRes[12] = {};
    for (size_t i = 0; i < 4; ++i) {
        const Binding& b = p.bindings[i];
        GLuint tex = b.texture;
        if (b.bufferIndex >= 0) {
            // Latest output: this frame's if the buffer already ran, else the
            // previous frame's (always the previous one for the pass itself).
            const BufferTargets& buf = buffers_[static_cast<size_t>(b.bufferIndex)];
            const RenderTarget& rt = buf.targets[buf.read];
            tex = rt.texture();
            channelRes[i * 3 + 0] = static_cast<float>(rt.width());
            channelRes[i * 3 + 1] = static_cast<float>(rt.height());
            channelRes[i * 3 + 2] = 1.0f;
        } else if (b.volume) {
            if (volume_.textures[volume_.read]) tex = volume_.textures[volume_.read];
            for (int k = 0; k < 3; ++k) channelRes[i * 3 + static_cast<size_t>(k)] = b.resolution[k];
        } else {
            if (b.audio && ctx_.audioTexture) tex = ctx_.audioTexture;
            for (int k = 0; k < 3; ++k) channelRes[i * 3 + static_cast<size_t>(k)] = b.resolution[k];
        }
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
        glBindTexture(b.target, tex);
        glBindSampler(static_cast<GLuint>(i), b.sampler);  // 0 = texture's own parameters
        glUniform1i(l.channel[i], static_cast<GLint>(i));
    }
    glUniform3fv(l.channelResolution, 4, channelRes);

    if (slices > 0) {
        for (int z = 0; z < slices; ++z) {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, volumeTexture, 0, z);
            glUniform1f(l.volumeZ, static_cast<float>(z) + 0.5f);
            ctx_.triangle->draw();
        }
    } else {
        ctx_.triangle->draw();
    }

    for (int i = 3; i >= 0; --i) {
        glBindSampler(static_cast<GLuint>(i), 0);
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
        glBindTexture(p.bindings[static_cast<size_t>(i)].target, 0);
    }
    glUseProgram(0);
}

void ShaderRenderer::render(const FrameUniforms& u, const RenderTarget& output) {
    if (state_ != State::Ready || !output.valid()) return;
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    ensureBuffers(output.width(), output.height());

    for (auto& p : passes_) {
        if (p->def.type == PassType::Image) {
            drawPass(*p, u, output.framebuffer(), output.width(), output.height());
            continue;
        }
        if (p->def.type == PassType::Volume) {
            if (!ensureVolume() || (!volume_.dynamic && volume_.rendered)) continue;
            const int dst = volume_.selfRead ? 1 - volume_.read : volume_.read;
            const int n = VolumeTargets::kSize;
            drawPass(*p, u, volume_.fbo, n, n, n, volume_.textures[dst]);
            volume_.read = dst;
            volume_.rendered = true;
            continue;
        }
        BufferTargets& buf = buffers_[static_cast<size_t>(p->def.bufferIndex)];
        RenderTarget& dst = buf.targets[1 - buf.read];
        if (!dst.valid()) continue;
        drawPass(*p, u, dst.framebuffer(), dst.width(), dst.height());
        buf.read = 1 - buf.read;
        if (buf.needsMipmaps) {
            glBindTexture(GL_TEXTURE_2D, dst.texture());
            glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}  // namespace sp
