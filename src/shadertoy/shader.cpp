#include "shadertoy/shader.h"

namespace sp {

std::string Pass::name() const {
    if (type == PassType::Image) return "Image";
    if (type == PassType::Volume) return "Volume A";
    if (bufferIndex >= 0 && bufferIndex < 4) return std::string("Buffer ") + static_cast<char>('A' + bufferIndex);
    return "Buffer ?";
}

const Pass* Shader::imagePass() const {
    for (const Pass& p : passes)
        if (p.type == PassType::Image) return &p;
    return nullptr;
}

const Pass* Shader::volumePass() const {
    for (const Pass& p : passes)
        if (p.type == PassType::Volume) return &p;
    return nullptr;
}

}  // namespace sp
