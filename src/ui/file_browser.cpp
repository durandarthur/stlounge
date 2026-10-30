#include "ui/file_browser.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>

namespace sp {

namespace fs = std::filesystem;

void FileBrowser::open(const fs::path& startDir) {
    open_ = true;
    std::error_code ec;
    dir_ = fs::is_directory(startDir, ec) ? startDir : fs::current_path(ec);
    list();
}

void FileBrowser::list() {
    items_.clear();
    ticked_.clear();
    error_.clear();
    pathInput_ = dir_.string();
    std::error_code ec;
    for (fs::directory_iterator it(dir_, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (!showHidden_ && !name.empty() && name[0] == '.') continue;
        std::error_code ec2;
        const bool isDir = it->is_directory(ec2);
        if (isDir || it->path().extension() == ".json") items_.push_back({name, isDir});
    }
    if (ec) error_ = ec.message();
    std::sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return a.name < b.name;
    });
}

std::vector<fs::path> FileBrowser::draw() {
    std::vector<fs::path> chosen;
    if (!open_) return chosen;
    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Import ShaderToy JSON", &open_)) {
        if (ImGui::Button("Up") && dir_.has_parent_path()) {
            dir_ = dir_.parent_path();
            list();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 9.0f);
        if (ImGui::InputText("##path", &pathInput_, ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::error_code ec;
            const fs::path p(pathInput_);
            if (fs::is_directory(p, ec)) {
                dir_ = p;
                list();
            } else if (fs::is_regular_file(p, ec)) {
                chosen.push_back(p);
            } else {
                error_ = "no such file or folder";
            }
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Hidden", &showHidden_)) list();
        if (!error_.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", error_.c_str());
        ImGui::TextDisabled("Double-click a .json file, tick several, or drag files onto the main window.");
        int jsonCount = 0;
        for (const Item& item : items_) jsonCount += item.isDir ? 0 : 1;
        ImGui::BeginDisabled(ticked_.empty());
        if (ImGui::Button(("Import ticked (" + std::to_string(ticked_.size()) + ")").c_str()))
            for (const std::string& name : ticked_) chosen.push_back(dir_ / name);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(jsonCount == 0);
        if (ImGui::Button(("Import all .json here (" + std::to_string(jsonCount) + ")").c_str()))
            for (const Item& item : items_)
                if (!item.isDir) chosen.push_back(dir_ / item.name);
        ImGui::EndDisabled();

        if (ImGui::BeginChild("##files", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
            for (const Item& item : items_) {
                ImGui::PushID(item.name.c_str());
                if (!item.isDir) {
                    bool tick = ticked_.count(item.name) != 0;
                    if (ImGui::Checkbox("##tick", &tick)) {
                        if (tick) ticked_.insert(item.name);
                        else ticked_.erase(item.name);
                    }
                    ImGui::SameLine();
                }
                const std::string label = item.isDir ? "[" + item.name + "]" : item.name;
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (item.isDir) {
                        dir_ /= item.name;
                        ImGui::PopID();
                        list();
                        break;
                    }
                    chosen.push_back(dir_ / item.name);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
    if (!chosen.empty()) open_ = false;
    return chosen;
}

}  // namespace sp
