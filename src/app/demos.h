#pragma once
// Built-in demo shaders (original code) as ready-to-run Shader objects.

#include <string>

#include "shadertoy/shader.h"

namespace sp {

Shader makeImageShader(const std::string& name, const std::string& code);
Shader makeTestPattern();
Shader makeMultipassDemo();
Shader makeAudioDemo();

}  // namespace sp
