#include "ui/shader_editor.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <vector>

#include "textures/builtin_list.h"

namespace sp {

namespace {

struct SourceItem {
    const char* section;  // grouping in the dropdown, like ShaderToy's input picker
    std::string label;
    ChannelInput input;   // type/asset/bufferIndex plus default sampler settings
};

const std::vector<SourceItem>& sourceItems() {
    static const std::vector<SourceItem> items = [] {
        std::vector<SourceItem> v;
        v.push_back({"Misc", "(none)", ChannelInput{}});
        for (int b = 0; b < 4; ++b) {
            ChannelInput in;
            in.type = InputType::Buffer;
            in.bufferIndex = b;
            in.sampler = {Filter::Linear, Wrap::Clamp, false, false};
            v.push_back({"Misc", std::string("Buffer ") + static_cast<char>('A' + b), in});
        }
        {
            ChannelInput in;
            in.type = InputType::VolumeBuffer;
            in.bufferIndex = 0;
            in.sampler = {Filter::Linear, Wrap::Clamp, false, false};
            v.push_back({"Misc", "Volume A", in});
        }
        {
            ChannelInput in;
            in.type = InputType::Audio;
            in.sampler = {Filter::Linear, Wrap::Clamp, false, false};
            v.push_back({"Misc", "Audio (desktop loopback)", in});
        }
        const std::pair<InputType, const char*> sections[] = {
            {InputType::Texture, "Textures"}, {InputType::Cubemap, "Cubemaps"}, {InputType::Volume, "Volumes"}};
        for (const auto& [kind, section] : sections) {
            for (const BuiltinTexture& t : builtinTextures()) {
                if (t.kind != kind) continue;
                ChannelInput in;
                in.type = t.kind;
                in.asset = t.id;
                in.sampler = {Filter::Mipmap, Wrap::Repeat, t.kind != InputType::Cubemap, false};
                std::string label = t.label;
                if (!t.available) label += " (not available)";
                else if (t.quality == TextureQuality::Approximation && t.kind == InputType::Cubemap)
                    label += " (look-alike)";  // 2D textures ship as images; cubemaps are generated
                v.push_back({section, label, in});
            }
        }
        // Listed so a shader's channels can be set up like on ShaderToy, with
        // a clear warning instead of silently missing inputs.
        for (const char* what : {"Cubemap A", "Keyboard", "Webcam", "Video"}) {
            ChannelInput in;
            in.type = InputType::Unsupported;
            in.unsupportedWhat = what;
            v.push_back({"Not supported (black)", what, in});
        }
        return v;
    }();
    return items;
}

bool sameSource(const ChannelInput& a, const ChannelInput& b) {
    if (a.type != b.type) return false;
    if (a.type == InputType::Buffer) return a.bufferIndex == b.bufferIndex;
    if (a.type == InputType::Texture || a.type == InputType::Cubemap || a.type == InputType::Volume)
        return a.asset == b.asset;
    if (a.type == InputType::Unsupported) return a.unsupportedWhat == b.unsupportedWhat;
    return true;
}

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

}  // namespace

std::string describeInput(const ChannelInput& in) {
    if (in.type == InputType::Unsupported)
        return "Unsupported: " + (in.unsupportedWhat.empty() ? std::string("unknown") : in.unsupportedWhat);
    for (const SourceItem& item : sourceItems()) {
        if (!sameSource(item.input, in)) continue;
        if (in.type == InputType::Cubemap) return item.label + " (cubemap)";
        if (in.type == InputType::Volume) return item.label + " (volume)";
        return item.label;
    }
    return "Unknown texture '" + in.asset + "'";
}

ShaderEditor::ShaderEditor() { clear(); }

void ShaderEditor::clear() {
    common_.clear();
    for (Tab& t : tabs_) t = Tab{};
    info_ = ShaderInfo{};
}

void ShaderEditor::setShader(const Shader& shader) {
    clear();
    info_ = shader.info;
    common_ = shader.common;
    for (const Pass& p : shader.passes) {
        const int idx = p.type == PassType::Image ? 4 : p.type == PassType::Volume ? 5 : p.bufferIndex;
        if (idx < 0 || idx > 5) continue;
        tabs_[static_cast<size_t>(idx)].code = p.code;
        tabs_[static_cast<size_t>(idx)].inputs = p.inputs;
    }
}

Shader ShaderEditor::build() const {
    Shader s;
    s.info = info_;
    s.common = common_;
    for (int b = 0; b < 4; ++b) {
        const Tab& t = tabs_[static_cast<size_t>(b)];
        if (trimmed(t.code).empty()) continue;
        Pass p;
        p.type = PassType::Buffer;
        p.bufferIndex = b;
        p.code = t.code;
        p.inputs = t.inputs;
        s.passes.push_back(p);
    }
    if (!trimmed(tabs_[5].code).empty()) {
        Pass v;
        v.type = PassType::Volume;
        v.bufferIndex = 0;
        v.code = tabs_[5].code;
        v.inputs = tabs_[5].inputs;
        s.passes.push_back(v);
    }
    Pass image;
    image.type = PassType::Image;
    image.code = tabs_[4].code;
    image.inputs = tabs_[4].inputs;
    s.passes.push_back(image);
    return s;
}

void ShaderEditor::drawChannels(Tab& tab, int tabIndex) {
    static const char* filters[] = {"nearest", "linear", "mipmap"};
    static const char* wraps[] = {"clamp", "repeat"};
    const float sourceWidth = ImGui::GetFontSize() * 15.0f;
    const float smallWidth = ImGui::GetFontSize() * 5.5f;

    for (int c = 0; c < 4; ++c) {
        ChannelInput& in = tab.inputs[static_cast<size_t>(c)];
        ImGui::PushID(tabIndex * 10 + c);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("iChannel%d", c);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(sourceWidth);
        const std::string preview = describeInput(in);
        if (ImGui::BeginCombo("##src", preview.c_str())) {
            const char* section = nullptr;
            for (const SourceItem& item : sourceItems()) {
                if (section != item.section) {
                    section = item.section;
                    ImGui::SeparatorText(section);
                }
                const bool selected = sameSource(item.input, in);
                if (ImGui::Selectable(item.label.c_str(), selected)) in = item.input;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (in.type != InputType::None && in.type != InputType::Unsupported) {
            int f = static_cast<int>(in.sampler.filter);
            int w = static_cast<int>(in.sampler.wrap);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(smallWidth);
            if (ImGui::Combo("##filter", &f, filters, 3)) in.sampler.filter = static_cast<Filter>(f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(smallWidth);
            if (ImGui::Combo("##wrap", &w, wraps, 2)) in.sampler.wrap = static_cast<Wrap>(w);
            ImGui::SameLine();
            ImGui::Checkbox("vflip", &in.sampler.vflip);
        }
        ImGui::PopID();
    }
}

void ShaderEditor::draw() {
    if (!ImGui::BeginTabBar("##passes")) return;

    if (ImGui::BeginTabItem(trimmed(common_).empty() ? "Common###common" : "Common*###common")) {
        ImGui::TextDisabled("Code shared by all passes.");
        ImGui::InputTextMultiline("##common", &common_, ImVec2(-1, -1), ImGuiInputTextFlags_AllowTabInput);
        ImGui::EndTabItem();
    }
    // Tab order as on ShaderToy: Common, Volume A, Buffer A-D, Image.
    for (int i : {5, 0, 1, 2, 3, 4}) {
        Tab& tab = tabs_[static_cast<size_t>(i)];
        const std::string name = i == 4   ? "Image"
                                 : i == 5 ? "Volume A"
                                          : std::string("Buffer ") + static_cast<char>('A' + i);
        const bool hasCode = !trimmed(tab.code).empty();
        const std::string label = name + (i != 4 && hasCode ? "*" : "") + "###tab" + std::to_string(i);
        if (ImGui::BeginTabItem(label.c_str())) {
            if (i == 5)
                ImGui::TextDisabled("Leave empty if unused. Defines mainVolume(out vec4, in vec3); 128^3, 1 channel.");
            else if (i < 4)
                ImGui::TextDisabled("Leave empty if the shader has no %s.", name.c_str());
            drawChannels(tab, i);
            ImGui::PushID(i);
            ImGui::InputTextMultiline("##code", &tab.code, ImVec2(-1, -1), ImGuiInputTextFlags_AllowTabInput);
            ImGui::PopID();
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
}

}  // namespace sp
