#include "shadertoy/wrapper.h"
#include "test.h"

using namespace sp;

namespace {

bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

Pass imagePass(const std::string& code) {
    Pass p;
    p.type = PassType::Image;
    p.code = code;
    return p;
}

}  // namespace

TEST(wrapper_has_header_uniforms_and_main) {
    const std::string src = wrapPass("float helper() { return 1.0; }", imagePass("void mainImage(out vec4 c, in vec2 f) { c = vec4(1); }"));
    CHECK(src.rfind("#version 300 es\n", 0) == 0);  // must be the very first line
    for (const char* u : {"uniform vec3 iResolution;", "uniform float iTime;", "uniform float iTimeDelta;",
                          "uniform float iFrameRate;", "uniform int iFrame;", "uniform float iChannelTime[4];",
                          "uniform vec3 iChannelResolution[4];", "uniform vec4 iMouse;", "uniform vec4 iDate;",
                          "uniform float iSampleRate;", "#define HW_PERFORMANCE 1", "precision highp float;"})
        CHECK(contains(src, u));
    CHECK(contains(src, "mainImage(color, gl_FragCoord.xy);"));
    CHECK(contains(src, "shadertoy_out_color = vec4(color.rgb, 1.0);"));  // Image pass is opaque
    // Common comes before the pass code, each with its own #line marker.
    const size_t common = src.find("#line 1 1\nfloat helper()");
    const size_t pass = src.find("#line 1 2\nvoid mainImage");
    CHECK(common != std::string::npos);
    CHECK(pass != std::string::npos);
    CHECK(common < pass);
}

TEST(wrapper_buffer_pass_keeps_alpha) {
    Pass p = imagePass("void mainImage(out vec4 c, in vec2 f) { c = vec4(1); }");
    p.type = PassType::Buffer;
    p.bufferIndex = 0;
    const std::string src = wrapPass("", p);
    CHECK(contains(src, "shadertoy_out_color = color;"));
    CHECK_EQ(p.name(), std::string("Buffer A"));
}

TEST(wrapper_sampler_types_follow_inputs) {
    Pass p = imagePass("void mainImage(out vec4 c, in vec2 f) { c = vec4(1); }");
    p.inputs[1].type = InputType::Cubemap;
    p.inputs[2].type = InputType::Volume;
    p.inputs[3].type = InputType::Buffer;
    const std::string src = wrapPass("", p);
    CHECK(contains(src, "uniform sampler2D iChannel0;"));
    CHECK(contains(src, "uniform samplerCube iChannel1;"));
    CHECK(contains(src, "uniform sampler3D iChannel2;"));
    CHECK(contains(src, "uniform sampler2D iChannel3;"));
}

TEST(remap_log_mesa_nvidia_generic) {
    CHECK_EQ(remapCompileLog("2:7(12): error: `foo' undeclared", "Image"),
             std::string("Image:7(12): error: `foo' undeclared"));
    CHECK_EQ(remapCompileLog("1:3(1): error: syntax error", "Buffer A"), std::string("Common:3(1): error: syntax error"));
    CHECK_EQ(remapCompileLog("2(15) : error C1008: undefined variable \"foo\"", "Buffer B"),
             std::string("Buffer B:15 : error C1008: undefined variable \"foo\""));
    CHECK_EQ(remapCompileLog("ERROR: 2:4: 'x' : undeclared identifier", "Image"),
             std::string("ERROR: Image:4: 'x' : undeclared identifier"));
    CHECK_EQ(remapCompileLog("0:40(2): error: wrapper", "Image"), std::string("wrapper:40(2): error: wrapper"));
    CHECK_EQ(remapCompileLog("some other text\n2:1(1): error: x", "Image"),
             std::string("some other text\nImage:1(1): error: x"));
}
