#include "ui/paste_dialog.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include "shadertoy/json_format.h"
#include "shadertoy/shader_ref.h"

namespace sp {

namespace {

// Canonical form of a shader's passes, to tell whether the user edited it.
std::string canonicalPasses(const Shader& s) { return shaderToJson(s)["renderpass"].dump(); }

}  // namespace

void PasteDialog::reset() {
    editor_.clear();
    notes_.clear();
    status_.clear();
    originalJson_.clear();
    originalPasses_.clear();
    info_ = ShaderInfo{};
    name_.clear();
    author_.clear();
    source_ = "import";
}

bool PasteDialog::fillJson(const std::string& text) {
    std::string error;
    std::vector<nlohmann::json> objs = shaderObjects(text, &error);
    if (objs.empty()) {
        notes_.push_back("This looks like JSON but isn't a shader: " + error);
        return false;
    }
    if (objs.size() > 1)
        notes_.push_back("This holds " + std::to_string(objs.size()) +
                         " shaders; only the first is shown. Use Import JSON for all of them.");
    ImportResult r = shaderFromJson(objs[0]);
    if (!r.ok) {
        notes_.push_back("Can't read the shader JSON: " + r.error);
        return false;
    }
    editor_.setShader(r.shader);
    info_ = r.shader.info;
    name_ = r.shader.info.name;
    author_ = r.shader.info.author;
    notes_.insert(notes_.end(), r.warnings.begin(), r.warnings.end());
    originalJson_ = objs[0].dump();
    originalPasses_ = canonicalPasses(editor_.build());
    return true;
}

void PasteDialog::open(const std::string& clipboard) {
    open_ = true;
    fetching_ = false;
    link_.clear();
    reset();

    if (looksLikeShaderJson(clipboard)) {
        if (fillJson(clipboard)) notes_.insert(notes_.begin(), "Filled from ShaderToy JSON in the clipboard.");
        return;
    }
    if (isShaderLink(clipboard)) {
        link_ = clipboard.substr(0, 256);
        notes_.push_back("The clipboard holds a ShaderToy link: press Fetch, or paste the shader's JSON export.");
        return;
    }
    if (clipboard.find("mainImage") != std::string::npos) {
        Shader s;
        Pass image;
        image.type = PassType::Image;
        image.code = clipboard;
        s.passes.push_back(image);
        editor_.setShader(s);
        notes_.push_back("Clipboard code put in the Image tab. Paste other tabs and set the channels if needed.");
        return;
    }
    notes_.push_back("Paste each tab's code and set the channels as on the shader's ShaderToy page, "
                     "or enter a ShaderToy link above.");
}

bool PasteDialog::fillFromJson(const std::string& text, const std::string& note, const std::string& source) {
    reset();
    if (!fillJson(text)) return false;
    source_ = source;
    notes_.insert(notes_.begin(), note);
    return true;
}

Shader PasteDialog::shader() const {
    Shader s = editor_.build();
    s.info = info_;
    s.info.name = name_.empty() ? "Untitled" : name_;
    s.info.author = author_;
    return s;
}

std::string PasteDialog::unchangedOriginalJson() const {
    if (originalJson_.empty()) return {};
    if (canonicalPasses(editor_.build()) != originalPasses_) return {};
    if (name_ != info_.name || author_ != info_.author) return {};
    return originalJson_;
}

PasteDialog::Result PasteDialog::draw() {
    if (!open_) return Result::None;
    Result result = Result::None;
    ImGui::SetNextWindowSize(ImVec2(760, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    bool keepOpen = true;
    if (ImGui::Begin("Add shader", &keepOpen)) {
        const float w = ImGui::GetFontSize() * 16.0f;
        ImGui::SetNextItemWidth(w * 2.0f);
        const bool enter = ImGui::InputTextWithHint("##link", "shadertoy.com link or shader ID", &link_,
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(fetching_ || link_.empty());
        if (ImGui::Button(fetching_ ? "Fetching..." : "Fetch") || (enter && !fetching_ && !link_.empty()))
            result = Result::Fetch;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Uses shadertoy.com's unofficial endpoint, which often answers with a browser check.");

        ImGui::SetNextItemWidth(w);
        ImGui::InputText("Name", &name_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w);
        ImGui::InputText("Author", &author_);
        if (!info_.url.empty()) ImGui::TextDisabled("%s", info_.url.c_str());

        for (const std::string& n : notes_) ImGui::TextWrapped("%s", n.c_str());
        if (!status_.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", status_.c_str());

        if (ImGui::Button("Add to library")) result = Result::Save;
        ImGui::SameLine();
        if (ImGui::Button("Try without saving")) result = Result::Try;
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) result = Result::Cancel;
        ImGui::Separator();
        editor_.draw();
    }
    ImGui::End();
    if (!keepOpen) result = Result::Cancel;
    if (result == Result::Cancel) open_ = false;
    return result;
}

}  // namespace sp
