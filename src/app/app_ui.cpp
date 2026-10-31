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
    if (showSettings_) drawSettingsPanel();
    drawPlaylistDialogs();
    drawPasteDialog();
    const std::vector<fs::path> chosen = fileBrowser_.draw();
    if (!chosen.empty()) {
        std::vector<std::string> paths;
        for (const fs::path& p : chosen) paths.push_back(p.string());
        importFiles(paths);
    }
    if (!errorLog_.empty()) drawErrors();
}

namespace {
constexpr double kToastSeconds = 1.5;
constexpr double kToastFade = 0.4;
double toastNow() { return static_cast<double>(SDL_GetTicks()) / 1000.0; }
}  // namespace

void App::showToast(std::string text) {
    toast_ = std::move(text);
    toastTime_ = toastNow();
}

bool App::toastVisible() const { return !toast_.empty() && toastNow() - toastTime_ < kToastSeconds; }

void App::drawToast() {
    const double left = kToastSeconds - (toastNow() - toastTime_);
    const float alpha = static_cast<float>(std::clamp(left / kToastFade, 0.0, 1.0));
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 24.0f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.6f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##toast", nullptr, flags)) ImGui::TextUnformatted(toast_.c_str());
    ImGui::End();
    ImGui::PopStyleVar();
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
        ImGui::Text("iResolution %d x %d (render scale %.2f)", imageTarget_.width(), imageTarget_.height(),
                    settings_.renderScale);
        if (outgoing_) ImGui::TextDisabled("Crossfading (%.1f s left)", std::max(0.0, fadeDuration_ - fadeElapsed_));

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
        drawAudioStatus();
        ImGui::Checkbox("Library", &showLibrary_);
        ImGui::SameLine();
        ImGui::Checkbox("Settings (O)", &showSettings_);
        ImGui::TextDisabled("N/P next/prev  R random  S shuffle  L lock  Space pause  F5 reload");
        ImGui::TextDisabled("Ctrl+V add  H/Tab hide UI  F/F11 fullscreen  F12 screenshot");
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
    if (ImGui::SmallButton("Rescan")) {
        library_->scan();
        playlists_->scan();
        playlistDirty_ = true;
    }
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

    if (selectMode_ && !playlists_->playlists().empty()) {
        ImGui::SameLine();
        ImGui::BeginDisabled(selected_.empty());
        if (ImGui::SmallButton("Add to...")) ImGui::OpenPopup("##addsel");
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("##addsel")) {
            for (const Playlist& p : playlists_->playlists()) {
                if (!ImGui::MenuItem(p.name.c_str())) continue;
                std::string error;
                int failed = 0;
                for (const std::string& id : selected_)
                    if (!playlists_->addShader(p.name, id, &error)) ++failed;
                if (failed) setStatus("Couldn't add " + std::to_string(failed) + ": " + error, true);
                else setStatus("Added " + std::to_string(selected_.size()) + " shaders to '" + p.name + "'");
                playlistDirty_ = true;
            }
            ImGui::EndPopup();
        }
    }

    ImGui::Separator();
    drawPlaylistControls();
    ImGui::Separator();

    if (!status_.empty()) {
        const double age = static_cast<double>(SDL_GetTicks()) / 1000.0 - statusTime_;
        if (age < 15.0) ImGui::TextColored(statusIsError_ ? kErrorColor : kWarnColor, "%s", status_.c_str());
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filter by name or author", &libraryFilter_);

    // Rows: the active playlist (all shaders also lists unreadable files).
    const bool userList = !activePlaylist_.empty() && activePlaylist_ != kFavoritesPlaylist;
    std::vector<const LibraryEntry*> rows;
    if (userList) {
        if (const Playlist* p = playlists_->find(activePlaylist_))
            for (const std::string& id : p->ids)
                if (const LibraryEntry* e = library_->find(id)) rows.push_back(e);
    } else {
        for (const LibraryEntry& e : library_->entries())
            if (activePlaylist_.empty() || e.favorite) rows.push_back(&e);
    }
    const bool filtering = !libraryFilter_.empty();

    if (ImGui::BeginChild("##entries", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders)) {
        if (library_->entries().empty())
            ImGui::TextWrapped("No shaders yet. Press Ctrl+V to paste one (code or ShaderToy JSON), or import a "
                               "JSON file. Shaders are saved in %s",
                               library_->dir().c_str());
        else if (rows.empty())
            ImGui::TextWrapped(activePlaylist_ == kFavoritesPlaylist
                                   ? "No favorites yet. Click the * next to a shader in 'All shaders'."
                                   : "This playlist is empty. Right-click shaders in 'All shaders' to add them.");
        for (size_t i = 0; i < rows.size(); ++i) {
            const LibraryEntry& e = *rows[i];
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
            if (e.loadError.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, e.favorite ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f)
                                                                : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (ImGui::SmallButton("*")) {
                    std::string error;
                    if (!library_->setFavorite(e.id, !e.favorite, &error)) setStatus(error, true);
                    playlistDirty_ = true;
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(e.favorite ? "Remove from favorites" : "Add to favorites");
                ImGui::SameLine();
            }
            std::string label = e.author.empty() ? e.name : e.name + "  -  " + e.author;
            if (e.duration > 0) label += "  [" + std::to_string(static_cast<int>(e.duration)) + " s]";
            const bool failed = player_.isFailed(e.id);
            if (!e.loadError.empty()) {
                ImGui::TextDisabled("%s (unreadable)", label.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.loadError.c_str());
            } else {
                if (failed) ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
                if (ImGui::Selectable((label + (failed ? "  (failed, click to retry)" : "")).c_str(),
                                      e.id == currentLibraryId_)) {
                    player_.clearFailed();  // a manual pick gives every shader another chance
                    LoadRequest req;
                    req.fromPlaylist = player_.contains(e.id);
                    playFromLibrary(e.id, req);
                }
                if (failed) ImGui::PopStyleColor();
            }
            if (ImGui::BeginPopupContextItem("##ctx")) {
                if (e.loadError.empty()) {
                    if (ImGui::MenuItem(e.favorite ? "Remove from favorites" : "Add to favorites")) {
                        std::string error;
                        if (!library_->setFavorite(e.id, !e.favorite, &error)) setStatus(error, true);
                        playlistDirty_ = true;
                    }
                    if (ImGui::BeginMenu("Add to playlist")) {
                        for (const Playlist& p : playlists_->playlists()) {
                            const bool has = std::find(p.ids.begin(), p.ids.end(), e.id) != p.ids.end();
                            if (ImGui::MenuItem(p.name.c_str(), nullptr, has, !has)) {
                                std::string error;
                                if (!playlists_->addShader(p.name, e.id, &error)) setStatus(error, true);
                                playlistDirty_ = true;
                            }
                        }
                        if (ImGui::MenuItem("New playlist...")) {
                            nameDialog_ = NameDialog::New;
                            nameDialogText_.clear();
                            nameDialogError_.clear();
                            nameDialogAddId_ = e.id;
                        }
                        ImGui::EndMenu();
                    }
                    if (userList) {
                        std::string error;
                        if (!filtering && ImGui::MenuItem("Move up", nullptr, false, i > 0)) {
                            playlists_->move(activePlaylist_, i, -1, &error);
                            playlistDirty_ = true;
                        }
                        if (!filtering && ImGui::MenuItem("Move down", nullptr, false, i + 1 < rows.size())) {
                            playlists_->move(activePlaylist_, i, +1, &error);
                            playlistDirty_ = true;
                        }
                        if (ImGui::MenuItem(("Remove from '" + activePlaylist_ + "'").c_str())) {
                            playlists_->removeShader(activePlaylist_, e.id, &error);
                            playlistDirty_ = true;
                        }
                        if (!error.empty()) setStatus(error, true);
                    }
                    if (ImGui::MenuItem("Play time...")) {
                        durationEditId_ = e.id;
                        durationEditValue_ = static_cast<int>(e.duration);
                    }
                }
                if (!e.url.empty() && ImGui::MenuItem("Copy link")) SDL_SetClipboardText(e.url.c_str());
                if (ImGui::MenuItem("Delete from library...")) deleteCandidates_ = {e.id};
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::TextDisabled("%zu shaders in the library. Right-click an entry for more.", library_->entries().size());
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
                else playlists_->forgetShader(id);
                if (id == currentLibraryId_) currentLibraryId_.clear();
                selected_.erase(id);
            }
            library_->scan();
            playlistDirty_ = true;
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
            playlistDirty_ = true;
            playFromLibrary(id);
            break;
        }
        default:
            break;
    }
}

void App::drawAudioStatus() {
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
    drawAudioSourcePicker();
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
        ImGui::TextColored(kErrorColor, "Compile failed. The previous shader keeps running; playlist shaders that "
                                        "fail are skipped.");
        ImGui::InputTextMultiline("##log", &errorLog_, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::End();
    if (!open) errorLog_.clear();
}

}  // namespace sp
