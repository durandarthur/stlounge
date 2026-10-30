#pragma once
// One loaded ShaderToy shader on the GPU: compiled passes, buffer render
// targets and channel bindings. Several can exist at once (the playing one
// and the one being compiled; later both sides of a crossfade).

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "gfx/gl_caps.h"
#include "gfx/neutral_textures.h"
#include "gfx/program.h"
#include "gfx/render_target.h"
#include "gfx/texture_library.h"
#include "shadertoy/shader.h"
#include "shadertoy/uniforms.h"

namespace sp {

struct RenderContext {
    const GlCaps* caps = nullptr;
    const FullscreenTriangle* triangle = nullptr;
    const NeutralTextures* neutral = nullptr;
    TextureLibrary* textures = nullptr;
    GLuint audioTexture = 0;  // 512x2 FFT/waveform, 0 = not available
};

class ShaderRenderer {
public:
    enum class State { Empty, Compiling, Ready, Failed };

    explicit ShaderRenderer(const RenderContext& ctx) : ctx_(ctx) {}
    ~ShaderRenderer();
    ShaderRenderer(const ShaderRenderer&) = delete;
    ShaderRenderer& operator=(const ShaderRenderer&) = delete;

    // Starts compiling every pass (in the background if the driver supports it).
    void load(const Shader& shader);
    // Polls compilation. Cheap; call once per frame while Compiling.
    State update();
    State state() const { return state_; }

    // Compile errors with locations mapped to tab names ("Image:12 ...").
    const std::string& errorLog() const { return errorLog_; }
    // Non-fatal problems (unsupported inputs, missing textures...).
    const std::vector<std::string>& warnings() const { return warnings_; }
    bool usesAudio() const { return usesAudio_; }
    double compileMs() const { return compileMs_; }
    const Shader& shader() const { return shader_; }

    // Renders all passes for one frame; the Image pass goes into `output`.
    // Buffers follow the output's size (content is kept when it changes).
    void render(const FrameUniforms& u, const RenderTarget& output);

    // Buffer texture currently readable (latest output), for tests and debugging.
    GLuint bufferTexture(int index) const;

private:
    struct Locations {
        GLint resolution = -1, time = -1, timeDelta = -1, frameRate = -1, frame = -1;
        GLint channelTime = -1, channelResolution = -1, mouse = -1, date = -1, sampleRate = -1;
        GLint channel[4] = {-1, -1, -1, -1};
    };
    struct Binding {
        GLenum target = GL_TEXTURE_2D;
        GLuint texture = 0;    // fixed texture, unless bufferIndex >= 0
        int bufferIndex = -1;  // read the buffer's latest output
        bool audio = false;
        GLuint sampler = 0;
        float resolution[3] = {0, 0, 0};
    };
    struct PassRuntime {
        Pass def;
        ProgramBuild build;
        GLuint program = 0;
        Locations loc;
        std::array<Binding, 4> bindings;
    };
    struct BufferTargets {
        RenderTarget targets[2];
        int read = 0;  // index of the target holding the latest output
        bool exists = false;
        bool needsMipmaps = false;
    };

    void resolveBindings(PassRuntime& p);
    GLuint makeSampler(const SamplerSettings& s, bool allowLinear, bool hasMipmaps);
    void lookupLocations(PassRuntime& p);
    void ensureBuffers(int width, int height);
    void drawPass(PassRuntime& p, const FrameUniforms& u, GLuint fbo, int width, int height);
    void warn(const std::string& message);

    RenderContext ctx_;
    Shader shader_;
    std::vector<std::unique_ptr<PassRuntime>> passes_;  // A, B, C, D, Image order
    std::array<BufferTargets, 4> buffers_;
    std::vector<GLuint> samplers_;
    State state_ = State::Empty;
    std::string errorLog_;
    std::vector<std::string> warnings_;
    bool usesAudio_ = false;
    double compileMs_ = 0;
};

}  // namespace sp
