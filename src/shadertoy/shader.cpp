#include "shadertoy/shader.h"

namespace sp {

std::string Pass::name() const {
    if (type == PassType::Image) return "Image";
    if (bufferIndex >= 0 && bufferIndex < 4) return std::string("Buffer ") + static_cast<char>('A' + bufferIndex);
    return "Buffer ?";
}

const Pass* Shader::imagePass() const {
    for (const Pass& p : passes)
        if (p.type == PassType::Image) return &p;
    return nullptr;
}

}  // namespace sp
