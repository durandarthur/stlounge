#include "shadertoy/wrapper.h"

#include <regex>
#include <sstream>

namespace sp {

namespace {

const char* samplerType(InputType t) {
    switch (t) {
        case InputType::Cubemap: return "samplerCube";
        case InputType::Volume: return "sampler3D";
        default: return "sampler2D";
    }
}

constexpr int kStringWrapper = 0;
constexpr int kStringCommon = 1;
constexpr int kStringPass = 2;

int countLines(const std::string& s) {
    int n = 1;
    for (char c : s)
        if (c == '\n') ++n;
    return n;
}

}  // namespace

std::string wrapPass(const std::string& common, const Pass& pass) {
    std::ostringstream s;
    s << "#version 300 es\n"
         "#ifdef GL_ES\n"
         "precision highp float;\n"
         "precision highp int;\n"
         "precision highp sampler2D;\n"
         "precision highp samplerCube;\n"
         "precision highp sampler3D;\n"
         "#endif\n"
         "#define HW_PERFORMANCE 1\n"
         "uniform vec3 iResolution;\n"
         "uniform float iTime;\n"
         "uniform float iTimeDelta;\n"
         "uniform float iFrameRate;\n"
         "uniform int iFrame;\n"
         "uniform float iChannelTime[4];\n"
         "uniform vec3 iChannelResolution[4];\n"
         "uniform vec4 iMouse;\n"
         "uniform vec4 iDate;\n"
         "uniform float iSampleRate;\n";
    for (int i = 0; i < 4; ++i)
        s << "uniform " << samplerType(pass.inputs[static_cast<size_t>(i)].type) << " iChannel" << i << ";\n";
    s << "out vec4 shadertoy_out_color;\n";

    s << "#line 1 " << kStringCommon << "\n" << common << "\n";
    s << "#line 1 " << kStringPass << "\n" << pass.code << "\n";

    std::string head = s.str();
    std::ostringstream tail;
    tail << "#line " << countLines(head) + 1 << " " << kStringWrapper << "\n"
         << "void main() {\n"
            "    vec4 color = vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    mainImage(color, gl_FragCoord.xy);\n";
    if (pass.type == PassType::Image)
        tail << "    shadertoy_out_color = vec4(color.rgb, 1.0);\n";  // the canvas is opaque
    else
        tail << "    shadertoy_out_color = color;\n";
    tail << "}\n";
    return head + tail.str();
}

std::string remapCompileLog(const std::string& log, const std::string& passName) {
    auto nameFor = [&](const std::string& num) -> std::string {
        if (num == "1") return "Common";
        if (num == "2") return passName;
        if (num == "0") return "wrapper";
        return num;
    };
    // Mesa:   0:12(5): error: ...
    static const std::regex mesa(R"(^(\d+):(\d+)\((\d+)\):)");
    // NVIDIA: 0(12) : error C0000: ...
    static const std::regex nvidia(R"(^(\d+)\((\d+)\)\s*:)");
    // ANGLE / others: ERROR: 0:12: ...
    static const std::regex generic(R"(^(ERROR|WARNING): (\d+):(\d+):)");

    std::istringstream in(log);
    std::ostringstream out;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        std::smatch m;
        if (std::regex_search(line, m, mesa))
            line = nameFor(m[1]) + ":" + m[2].str() + "(" + m[3].str() + "):" + m.suffix().str();
        else if (std::regex_search(line, m, nvidia))
            line = nameFor(m[1]) + ":" + m[2].str() + " :" + m.suffix().str();
        else if (std::regex_search(line, m, generic))
            line = m[1].str() + ": " + nameFor(m[2]) + ":" + m[3].str() + ":" + m.suffix().str();
        if (!first) out << "\n";
        out << line;
        first = false;
    }
    return out.str();
}

}  // namespace sp
