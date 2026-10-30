#pragma once
// Tabbed shader editor: Common, Buffer A-D, Image, each pass with four
// channel inputs and their sampler settings. Builds a Shader on demand.

#include <array>
#include <string>

#include "shadertoy/shader.h"

namespace sp {

class ShaderEditor {
public:
    ShaderEditor();

    void setShader(const Shader& shader);
    // Buffer tabs with empty code are left out.
    Shader build() const;
    void clear();

    // Draws the tab bar and the selected tab's contents into the current window.
    void draw();

private:
    struct Tab {
        std::string code;
        std::array<ChannelInput, 4> inputs;
    };
    void drawChannels(Tab& tab, int tabIndex);

    std::string common_;
    std::array<Tab, 5> tabs_;  // 0-3 = Buffer A-D, 4 = Image
    ShaderInfo info_;
};

// Short human-readable description of a channel input ("Buffer A", "RGBA Noise Medium").
std::string describeInput(const ChannelInput& in);

}  // namespace sp
