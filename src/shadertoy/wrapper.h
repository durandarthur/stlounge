#pragma once
// Turns a ShaderToy pass into a complete GLSL ES 3.00 fragment shader.
//
// Layout of the generated source (GLSL source-string numbers via #line):
//   string 0: version, precision, uniforms      (wrapper)
//   string 1: Common tab
//   string 2: the pass code
//   string 0: main() calling mainImage
// so compile errors can be mapped back to "Common:12" / "Image:12".

#include <string>

#include "shadertoy/shader.h"

namespace sp {

// Name of the per-slice z uniform in Volume passes (fragCoord.z of the slice centre).
inline constexpr const char* kVolumeSliceUniform = "shadertoy_volume_z";

std::string wrapPass(const std::string& common, const Pass& pass);

// True if a pass's output can change from frame to frame: it uses time,
// frame, mouse or date uniforms, or reads buffers/audio. Used to render a
// static Volume A only once.
bool passIsDynamic(const std::string& common, const Pass& pass);

// Rewrites driver log locations ("0:12(5):", "2(12) :", "ERROR: 2:12:") so
// the source string number becomes the tab name. Unknown formats pass through.
std::string remapCompileLog(const std::string& log, const std::string& passName);

}  // namespace sp
