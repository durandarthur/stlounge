#pragma once
// One loaded ShaderToy shader on the GPU: compiled passes plus their state.
// Several can exist at once (the playing one and the one being compiled;
// later both sides of a crossfade).

#include <memory>
#include <string>
#include <vector>

#include "gfx/gl_caps.h"
#include "gfx/neutral_textures.h"
#include "gfx/program.h"
#include "gfx/render_target.h"
#include "shadertoy/shader.h"
#include "shadertoy/uniforms.h"

namespace sp {

struct RenderContext {
    const GlCaps* caps = nullptr;
    const FullscreenTriangle* triangle = nullptr;
    const NeutralTextures* neutral = nullptr;
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
    double compileMs() const { return compileMs_; }
    const Shader& shader() const { return shader_; }

    // Renders all passes for one frame; the Image pass goes into `output`.
    void render(const FrameUniforms& u, const RenderTarget& output);

private:
    struct Locations {
        GLint resolution = -1, time = -1, timeDelta = -1, frameRate = -1, frame = -1;
        GLint channelTime = -1, channelResolution = -1, mouse = -1, date = -1, sampleRate = -1;
        GLint channel[4] = {-1, -1, -1, -1};
    };
    struct PassRuntime {
        Pass def;
        ProgramBuild build;
        GLuint program = 0;
        Locations loc;
    };

    void lookupLocations(PassRuntime& p);
    void drawPass(PassRuntime& p, const FrameUniforms& u, GLuint fbo, int width, int height);

    RenderContext ctx_;
    Shader shader_;
    std::vector<std::unique_ptr<PassRuntime>> passes_;
    State state_ = State::Empty;
    std::string errorLog_;
    double compileMs_ = 0;
};

}  // namespace sp
