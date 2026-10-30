// App: ImGui panels.

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "app/app.h"
#include "app/demos.h"
#include "app_info.h"
#include "platform/log.h"

namespace sp {

namespace {

const ImVec4 kWarnColor(1.0f, 0.8f, 0.3f, 1.0f);
const ImVec4 kErrorColor(1.0f, 0.45f, 0.4f, 1.0f);

bool containsNoCase(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != hay.end();
}

}  // namespace

void App::drawUi() {
    drawInfoPanel();
    if (showLibrary_) drawLibraryPanel();
    drawPasteDialog();
    const std::vector<fs::path> chosen = fileBrowser_.draw();
    if (!chosen.empty()) {
        std::vector<std::string> paths;
        for (const fs::path& p : chosen) paths.push_back(p.string());
        importFiles(paths);
    }
    if (!errorLog_.empty()) drawErrors();
}

void App::drawCurrentShaderInfo() {
    if (!current_) {
        ImGui::TextDisabled("No shader running");
        return;
    }
    const ShaderInfo& info = current_->shader().info;
    ImGui::Text("%s", info.name.c_str());
    if (!info.author.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("by %s", info.author.c_str());
    }
    if (!info.url.empty()) {
        ImGui::TextDisabled("%s", info.url.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy link")) SDL_SetClipboardText(info.url.c_str());
        ImGui::TextDisabled("License: the author's (ShaderToy default: CC BY-NC-SA 3.0).");
    }
    ImGui::Text("Compile time: %.0f ms", current_->compileMs());
    for (const std::string& w : current_->warnings()) ImGui::TextColored(kWarnColor, "%s", w.c_str());
}

void App::drawInfoPanel() {
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(APP_DISPLAY_NAME " - info")) {
        drawCurrentShaderInfo();
        if (pending_) ImGui::TextColored(kWarnColor, "Compiling %s...", pending_->shader().info.name.c_str());
        ImGui::Separator();
        ImGui::Text("iTime %.2f   iFrame %d   %s", clock_.time(), clock_.frame(), paused_ ? "[PAUSED]" : "");
        float m[4];
        mouse_.uniform(m);
        ImGui::Text("iMouse (%.0f, %.0f, %.0f, %.0f)", m[0], m[1], m[2], m[3]);
        ImGui::Text("iResolution %d x %d", imageTarget_.width(), imageTarget_.height());
        if (ImGui::SliderFloat("Render scale", &renderScale_, 0.25f, 2.0f, "%.2f"))
            renderScale_ = std::round(renderScale_ * 20.0f) / 20.0f;

        ImGui::Separator();
        int ww = 0, wh = 0, dw = 0, dh = 0;
        SDL_GetWindowSize(window_.sdl(), &ww, &wh);
        window_.drawableSize(&dw, &dh);
        ImGui::Text("%.1f FPS (%.2f ms)  vsync interval %d", fps_, frameMs_, window_.swapInterval());
        ImGui::Text("Window %dx%d, drawable %dx%d%s", ww, wh, dw, dh, window_.isFullscreen() ? ", fullscreen" : "");
        if (ImGui::CollapsingHeader("GPU")) {
            ImGui::Text("Video driver: %s", window_.videoDriver());
            ImGui::TextWrapped("Renderer: %s", caps_.renderer.c_str());
            ImGui::TextWrapped("Version: %s", caps_.version.c_str());
            ImGui::Text("Buffers: %s%s", bufferFormatName(caps_.bufferFormat),
                        caps_.bufferLinearFilter ? "" : " (nearest only)");
            ImGui::Text("Parallel shader compile: %s", caps_.parallelCompile ? "yes" : "no");
        }
        drawAudioSection();
        ImGui::Checkbox("Show library", &showLibrary_);
        ImGui::TextDisabled("Ctrl+V add  Space pause  F5 reload  H/Tab hide UI  F/F11 fullscreen");
    }
    ImGui::End();
}

void App::drawLibraryPanel() {
    ImGui::SetNextWindowPos(ImVec2(500, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Library", &showLibrary_)) {
        ImGui::End();
        return;
    }
    if (ImGui::Button("Add shader (Ctrl+V)")) {
        char* text = SDL_GetClipboardText();
        openPasteDialog(text ? text : "");
        SDL_free(text);
    }
    ImGui::SameLine();
    if (ImGui::Button("Import JSON file...")) {
        std::error_code ec;
        const fs::path home = homeDir(processEnv());
        const fs::path downloads = home / "Downloads";
        fileBrowser_.open(fs::is_directory(downloads, ec) ? downloads : home);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) library_->scan();
    ImGui::SameLine();
    if (ImGui::Checkbox("Select", &selectMode_)) selected_.clear();
    if (selectMode_) {
        if (ImGui::SmallButton("All shown")) {
            for (const LibraryEntry& e : library_->entries())
                if (containsNoCase(e.name, libraryFilter_) || containsNoCase(e.author, libraryFilter_))
                    selected_.insert(e.id);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("None")) selected_.clear();
        ImGui::SameLine();
        ImGui::BeginDisabled(selected_.empty());
        if (ImGui::SmallButton(("Delete selected (" + std::to_string(selected_.size()) + ")...").c_str()))
            deleteCandidates_.assign(selected_.begin(), selected_.end());
        ImGui::EndDisabled();
    }

    if (!status_.empty()) {
        const double age = static_cast<double>(SDL_GetTicks()) / 1000.0 - statusTime_;
        if (age < 15.0) ImGui::TextColored(statusIsError_ ? kErrorColor : kWarnColor, "%s", status_.c_str());
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filter by name or author", &libraryFilter_);

    const auto& entries = library_->entries();
    if (ImGui::BeginChild("##entries", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders)) {
        if (entries.empty())
            ImGui::TextWrapped("No shaders yet. Press Ctrl+V to paste one (code or ShaderToy JSON), or import a "
                               "JSON file. Shaders are saved in %s",
                               library_->dir().c_str());
        for (const LibraryEntry& e : entries) {
            if (!containsNoCase(e.name, libraryFilter_) && !containsNoCase(e.author, libraryFilter_)) continue;
            ImGui::PushID(e.id.c_str());
            if (selectMode_) {
                bool tick = selected_.count(e.id) != 0;
                if (ImGui::Checkbox("##sel", &tick)) {
                    if (tick) selected_.insert(e.id);
                    else selected_.erase(e.id);
                }
                ImGui::SameLine();
            }
            const std::string label = e.author.empty() ? e.name : e.name + "  -  " + e.author;
            if (!e.loadError.empty()) {
                ImGui::TextDisabled("%s (unreadable)", label.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.loadError.c_str());
            } else if (ImGui::Selectable(label.c_str(), e.id == currentLibraryId_)) {
                playFromLibrary(e.id);
            }
            if (ImGui::BeginPopupContextItem("##ctx")) {
                if (!e.url.empty() && ImGui::MenuItem("Copy link")) SDL_SetClipboardText(e.url.c_str());
                if (ImGui::MenuItem("Delete...")) deleteCandidates_ = {e.id};
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::TextDisabled("%zu shaders. Right-click an entry for more.", entries.size());
    if (ImGui::SmallButton("Test pattern")) loadShader(makeTestPattern());
    ImGui::SameLine();
    if (ImGui::SmallButton("Multipass demo")) loadShader(makeMultipassDemo());
    ImGui::SameLine();
    if (ImGui::SmallButton("Audio demo")) loadShader(makeAudioDemo());

    if (!deleteCandidates_.empty() && !ImGui::IsPopupOpen("Delete shaders?")) ImGui::OpenPopup("Delete shaders?");
    if (ImGui::BeginPopupModal("Delete shaders?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (deleteCandidates_.size() == 1) {
            const LibraryEntry* e = library_->find(deleteCandidates_[0]);
            ImGui::Text("Delete '%s' from the library?", e ? e->name.c_str() : deleteCandidates_[0].c_str());
        } else {
            ImGui::Text("Delete %zu shaders from the library?", deleteCandidates_.size());
        }
        ImGui::TextDisabled("The files are removed from %s", library_->dir().c_str());
        if (ImGui::Button("Delete")) {
            int failed = 0;
            std::string error;
            for (const std::string& id : deleteCandidates_) {
                if (!library_->remove(id, &error, /*rescan=*/false)) ++failed;
                if (id == currentLibraryId_) currentLibraryId_.clear();
                selected_.erase(id);
            }
            library_->scan();
            if (failed) setStatus("Delete failed for " + std::to_string(failed) + ": " + error, true);
            else if (deleteCandidates_.size() > 1)
                setStatus("Deleted " + std::to_string(deleteCandidates_.size()) + " shaders");
            deleteCandidates_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            deleteCandidates_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

void App::drawPasteDialog() {
    switch (pasteDialog_.draw()) {
        case PasteDialog::Result::Try:
            loadShader(pasteDialog_.shader());
            break;
        case PasteDialog::Result::Save: {
            std::string error, id;
            const std::string original = pasteDialog_.unchangedOriginalJson();
            if (!original.empty())
                id = library_->add(nlohmann::json::parse(original, nullptr, false), "import", &error);
            else
                id = library_->addShader(pasteDialog_.shader(), "paste", &error);
            if (id.empty()) {
                pasteDialog_.setStatus("Could not save: " + error);
                break;
            }
            pasteDialog_.close();
            const LibraryEntry* e = library_->find(id);
            setStatus("Added '" + (e ? e->name : id) + "' to the library");
            playFromLibrary(id);
            break;
        }
        default:
            break;
    }
}

void App::drawAudioSection() {
    if (!ImGui::CollapsingHeader("Audio")) return;
    switch (audio_.state()) {
        case AudioCapture::State::Off:
            ImGui::TextDisabled("Capture off (the current shader has no audio input)");
            break;
        case AudioCapture::State::Connecting:
            ImGui::Text("Connecting...");
            break;
        case AudioCapture::State::Running:
            ImGui::TextWrapped("Capturing from %s", audio_.sourceName().c_str());
            break;
        case AudioCapture::State::Failed:
            ImGui::TextColored(kErrorColor, "Audio unavailable: %s", audio_.error().c_str());
            ImGui::TextDisabled("Audio inputs stay silent. Retrying every 10 s.");
            break;
    }

    if (!audioSourcesListed_) {
        audioSources_ = AudioCapture::listSources(&audioListError_);
        audioSourcesListed_ = true;
    }
    std::string current = "Default output (follows changes)";
    for (const AudioSource& src : audioSources_)
        if (src.name == audioDevice_) current = src.description;
    if (ImGui::BeginCombo("Source", current.c_str())) {
        if (ImGui::Selectable("Default output (follows changes)", audioDevice_.empty())) audioDevice_.clear();
        for (const AudioSource& src : audioSources_) {
            const std::string label = (src.isMonitor ? "Output: " : "Input: ") + src.description;
            if (ImGui::Selectable(label.c_str(), src.name == audioDevice_)) audioDevice_ = src.name;
        }
        ImGui::EndCombo();
        if (audioDevice_ != audio_.requestedDevice() && audio_.state() != AudioCapture::State::Off) {
            audio_.start(audioDevice_);
            audioFailedAt_ = -1;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) audioSourcesListed_ = false;
    if (!audioListError_.empty()) ImGui::TextDisabled("%s", audioListError_.c_str());

    float bins[SpectrumAnalyser::kBins];
    for (int i = 0; i < SpectrumAnalyser::kBins; ++i) bins[i] = audioFrequency_[i];
    ImGui::PlotHistogram("##fft", bins, SpectrumAnalyser::kBins, 0, "FFT (texture row 0)", 0.0f, 255.0f,
                         ImVec2(-1, 60));
}

void App::drawErrors() {
    ImGui::SetNextWindowPos(ImVec2(16, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(620, 220), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("Shader errors", &open)) {
        ImGui::TextColored(kErrorColor, "Compile failed. The previous shader keeps running.");
        ImGui::InputTextMultiline("##log", &errorLog_, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::End();
    if (!open) errorLog_.clear();
}

}  // namespace sp
