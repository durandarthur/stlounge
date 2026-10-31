// App: playlists, auto-advance, session state.

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "app/demos.h"
#include "platform/log.h"

namespace sp {

std::vector<std::string> App::playlistIds() const {
    std::vector<std::string> out;
    if (activePlaylist_.empty() || activePlaylist_ == kFavoritesPlaylist) {
        for (const LibraryEntry& e : library_->entries())
            if (e.loadError.empty() && (activePlaylist_.empty() || e.favorite)) out.push_back(e.id);
    } else if (const Playlist* p = playlists_->find(activePlaylist_)) {
        for (const std::string& id : p->ids)
            if (const LibraryEntry* e = library_->find(id); e && e->loadError.empty()) out.push_back(id);
    }
    return out;
}

std::string App::playlistLabel(const std::string& name) const {
    if (name.empty()) return "All shaders";
    if (name == kFavoritesPlaylist) return "Favorites";
    return name;
}

void App::setActivePlaylist(const std::string& name) {
    if (name == activePlaylist_) return;
    activePlaylist_ = name;
    rebuildPlaylist();
    saveSessionNow();
    LOG_INFO("playlist: %s (%zu shaders)", playlistLabel(name).c_str(), player_.items().size());
}

void App::rebuildPlaylist() {
    playlistDirty_ = false;
    if (!activePlaylist_.empty() && activePlaylist_ != kFavoritesPlaylist && !playlists_->find(activePlaylist_))
        activePlaylist_.clear();  // it was deleted
    player_.setItems(playlistIds());
    if (player_.contains(currentLibraryId_)) player_.setCurrent(currentLibraryId_);
}

void App::playlistStep(int direction, bool isSkip) {
    if (playlistDirty_) rebuildPlaylist();
    if (player_.items().empty()) {
        setStatus(activePlaylist_.empty() ? "The library is empty: press Ctrl+V to add a shader."
                                          : "'" + playlistLabel(activePlaylist_) + "' has no shaders.");
        return;
    }
    // After a demo or pasted shader, this continues from the last playlist shader.
    const std::string id = direction > 0 ? player_.next() : direction < 0 ? player_.previous() : player_.random();
    if (id.empty()) {
        if (player_.playableCount() == 0)
            setStatus("Every shader in '" + playlistLabel(activePlaylist_) +
                          "' failed to load. Click one to retry it.",
                      true);
        else
            setStatus("Nothing earlier to go back to.");
        if (!current_ && !pending_) loadShader(makeTestPattern());  // keep something on screen
        return;
    }
    LoadRequest req;
    req.libraryId = id;
    req.fromPlaylist = true;
    req.direction = direction;
    req.isSkip = isSkip;
    playFromLibrary(id, req);
}

void App::updateAutoAdvance(double dt) {
    if (paused_ || !current_) return;
    shaderElapsed_ += dt;
    if (!settings_.autoAdvance || settings_.locked || pending_ || outgoing_) return;
    if (currentLibraryId_.empty() || !player_.contains(currentLibraryId_) || player_.playableCount() < 2) return;
    const LibraryEntry* e = library_->find(currentLibraryId_);
    if (shaderElapsed_ < advanceSecondsFor(settings_, e ? e->duration : 0)) return;
    shaderElapsed_ = 0;
    LOG_INFO("auto-advance");
    playlistStep(+1);
}

void App::setShuffle(bool on) {
    settings_.shuffle = on;
    player_.setShuffle(on);
    LOG_INFO("shuffle %s", on ? "on" : "off");
}

void App::setLocked(bool on) {
    settings_.locked = on;
    // Unlocking starts the current shader's time over instead of advancing at once.
    if (!on) shaderElapsed_ = 0;
    LOG_INFO("playlist %s", on ? "locked" : "unlocked");
}

void App::saveSessionNow() {
    // Demos and unsaved shaders aren't remembered; the last library shader is.
    const std::string shader = currentLibraryId_.empty() ? lastSavedSessionShader_ : currentLibraryId_;
    if (shader == lastSavedSessionShader_ && activePlaylist_ == lastSavedSessionPlaylist_) return;
    std::string error;
    if (!saveSession(sessionPath_, {shader, activePlaylist_}, &error)) {
        LOG_WARN("session: %s", error.c_str());
        return;
    }
    lastSavedSessionShader_ = shader;
    lastSavedSessionPlaylist_ = activePlaylist_;
}

// ---------------------------------------------------------------- UI

void App::drawPlaylistControls() {
    const bool userList = !activePlaylist_.empty() && activePlaylist_ != kFavoritesPlaylist;
    auto countLabel = [](const std::string& name, size_t n) { return name + " (" + std::to_string(n) + ")"; };
    size_t favorites = 0;
    for (const LibraryEntry& e : library_->entries()) favorites += e.favorite ? 1 : 0;

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    if (ImGui::BeginCombo("##playlist", countLabel(playlistLabel(activePlaylist_), player_.items().size()).c_str())) {
        if (ImGui::Selectable(countLabel("All shaders", library_->entries().size()).c_str(), activePlaylist_.empty()))
            setActivePlaylist("");
        if (ImGui::Selectable(countLabel("Favorites", favorites).c_str(), activePlaylist_ == kFavoritesPlaylist))
            setActivePlaylist(kFavoritesPlaylist);
        if (!playlists_->playlists().empty()) ImGui::Separator();
        for (const Playlist& p : playlists_->playlists()) {
            ImGui::PushID(p.name.c_str());
            if (ImGui::Selectable(countLabel(p.name, p.ids.size()).c_str(), p.name == activePlaylist_))
                setActivePlaylist(p.name);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("New")) {
        nameDialog_ = NameDialog::New;
        nameDialogText_.clear();
        nameDialogError_.clear();
        nameDialogAddId_.clear();
    }
    if (userList) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Rename")) {
            nameDialog_ = NameDialog::Rename;
            nameDialogText_ = activePlaylist_;
            nameDialogError_.clear();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete list")) deletePlaylistAsked_ = true;
    }

    if (ImGui::Button("< Prev")) playlistStep(-1);
    ImGui::SameLine();
    if (ImGui::Button("Next >")) playlistStep(+1);
    ImGui::SameLine();
    if (ImGui::Button("Random")) playlistStep(0);
    ImGui::SameLine();
    bool shuffle = settings_.shuffle;
    if (ImGui::Checkbox("Shuffle", &shuffle)) setShuffle(shuffle);
    ImGui::SameLine();
    ImGui::Checkbox("Auto", &settings_.autoAdvance);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Auto-advance (interval in Settings, O)");
    ImGui::SameLine();
    bool locked = settings_.locked;
    if (ImGui::Checkbox("Lock", &locked)) setLocked(locked);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop auto-advance and transitions (L)");

    if (settings_.locked) {
        ImGui::TextDisabled("Locked: no auto-advance or transitions.");
    } else if (settings_.autoAdvance && player_.contains(currentLibraryId_) && player_.playableCount() > 1) {
        const LibraryEntry* e = library_->find(currentLibraryId_);
        const double left = advanceSecondsFor(settings_, e ? e->duration : 0) - shaderElapsed_;
        ImGui::TextDisabled("Next shader in %.0f s%s", std::max(0.0, left), paused_ ? " (paused)" : "");
    } else if (!currentLibraryId_.empty() && !player_.contains(currentLibraryId_)) {
        ImGui::TextDisabled("Playing a shader that isn't in this playlist.");
    }
}

void App::drawPlaylistDialogs() {
    // New / rename playlist
    if (nameDialog_ != NameDialog::None && !ImGui::IsPopupOpen("Playlist name")) ImGui::OpenPopup("Playlist name");
    if (ImGui::BeginPopupModal("Playlist name", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
        const bool enter = ImGui::InputText("##name", &nameDialogText_, ImGuiInputTextFlags_EnterReturnsTrue);
        if (!nameDialogError_.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", nameDialogError_.c_str());
        const char* verb = nameDialog_ == NameDialog::New ? "Create" : "Rename";
        if (ImGui::Button(verb) || enter) {
            std::string name = nameDialogText_;
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            std::string error;
            const bool ok = nameDialog_ == NameDialog::New ? playlists_->create(name, &error)
                                                           : playlists_->rename(activePlaylist_, name, &error);
            if (ok) {
                if (nameDialog_ == NameDialog::New && !nameDialogAddId_.empty()) {
                    playlists_->addShader(name, nameDialogAddId_, &error);
                } else if (nameDialog_ == NameDialog::New && selectMode_) {
                    for (const std::string& id : selected_) playlists_->addShader(name, id, &error);
                }
                if (nameDialog_ == NameDialog::Rename) activePlaylist_ = name;  // follow the rename
                setActivePlaylist(name);
                playlistDirty_ = true;
                saveSessionNow();
                nameDialog_ = NameDialog::None;
                ImGui::CloseCurrentPopup();
            } else {
                nameDialogError_ = error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            nameDialog_ = NameDialog::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Delete playlist
    if (deletePlaylistAsked_ && !ImGui::IsPopupOpen("Delete playlist?")) ImGui::OpenPopup("Delete playlist?");
    if (ImGui::BeginPopupModal("Delete playlist?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete the playlist '%s'?", activePlaylist_.c_str());
        ImGui::TextDisabled("Its shaders stay in the library.");
        if (ImGui::Button("Delete")) {
            std::string error;
            if (!playlists_->remove(activePlaylist_, &error)) setStatus(error, true);
            setActivePlaylist("");
            deletePlaylistAsked_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            deletePlaylistAsked_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Per-shader play time
    if (!durationEditId_.empty() && !ImGui::IsPopupOpen("Play time")) ImGui::OpenPopup("Play time");
    if (ImGui::BeginPopupModal("Play time", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const LibraryEntry* e = library_->find(durationEditId_);
        ImGui::Text("How long '%s' plays before auto-advance", e ? e->name.c_str() : durationEditId_.c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputInt("seconds", &durationEditValue_, 5, 30);
        durationEditValue_ = std::clamp(durationEditValue_, 0, 3600);
        ImGui::TextDisabled("0 = the global setting (%.0f s)", settings_.autoAdvanceSeconds);
        if (ImGui::Button("OK")) {
            std::string error;
            if (!library_->setDuration(durationEditId_, durationEditValue_, &error)) setStatus(error, true);
            durationEditId_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            durationEditId_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  // namespace sp
