#pragma once
// Small ImGui helpers shared by the panels.

#include <imgui.h>

#include <string>

namespace sp {

// Coloured text that wraps at the window edge (ImGui::TextColored doesn't wrap).
inline void textWrappedColored(const ImVec4& color, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

}  // namespace sp
