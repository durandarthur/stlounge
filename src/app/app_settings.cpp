// App: settings (load, save, panel) and screenshots.

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>

#include "app/app.h"
#include "app_info.h"
#include "gfx/gl.h"
#include "platform/log.h"
#include "textures/builtin_list.h"
#include "textures/image_io.h"

namespace sp {

namespace {

const ImVec4 kErrorColor(1.0f, 0.45f, 0.4f, 1.0f);

double nowSeconds() { return static_cast<double>(SDL_GetTicks()) / 1000.0; }

// "My Shader: v2!" -> "my-shader-v2", for file names.
std::string slug(const std::string& s) {
    std::string out;
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 128 && std::isalnum(u)) out += static_cast<char>(std::tolower(u));
        else if (!out.empty() && out.back() != '-') out += '-';
        if (out.size() >= 40) break;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "shader" : out;
}

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

}  // namespace

void App::loadSettingsAtStartup() {
    settingsPath_ = paths_.configDir / "settings.json";
    sessionPath_ = paths_.stateDir / "session.json";
    std::vector<std::string> warnings;
    if (!loadSettings(settingsPath_, &settings_, &warnings)) setStatus("Settings file unreadable; using defaults", true);
    for (const std::string& w : warnings) LOG_WARN("%s", w.c_str());
    savedSettings_ = settings_;
    LOG_INFO("settings: %s", settingsPath_.c_str());
    for (const auto& [id, path] : settings_.textureOverrides) {
        if (!TextureLibrary::canOverride(id)) {
            LOG_WARN("settings: texture override for unknown 2D texture '%s' ignored", id.c_str());
            continue;
        }
        const std::string error = textures_.setOverride(id, path);
        if (!error.empty()) LOG_WARN("texture override '%s': %s", id.c_str(), error.c_str());
    }
}

void App::applyTextureOverride(const std::string& id, const std::string& path) {
    if (path.empty()) settings_.textureOverrides.erase(id);
    else settings_.textureOverrides[id] = path;
    const std::string error = textures_.setOverride(id, path);
    if (!error.empty()) setStatus("Texture: " + error, true);
    // Reload so the shader picks up the new size (iChannelResolution).
    if (current_) {
        LoadRequest req;
        req.libraryId = currentLibraryId_;
        loadShader(current_->shader(), req);
    }
}

void App::saveSettingsIfChanged(bool force) {
    if (settings_ == savedSettings_) {
        settingsChangedAt_ = -1;
        return;
    }
    const double now = nowSeconds();
    if (settingsChangedAt_ < 0) settingsChangedAt_ = now;
    // Wait for a second of calm so dragging a slider doesn't write the file every frame.
    if (!force && now - settingsChangedAt_ < 1.0) return;
    std::string error;
    if (saveSettings(settingsPath_, settings_, &error)) {
        savedSettings_ = settings_;
        LOG_DEBUG("settings saved");
    } else {
        LOG_WARN("settings: %s", error.c_str());
        savedSettings_ = settings_;  // don't retry every frame; the next change tries again
    }
    settingsChangedAt_ = -1;
}

void App::drawAudioSourcePicker() {
    if (!audioSourcesListed_) {
        audioSources_ = AudioCapture::listSources(&audioListError_);
        audioSourcesListed_ = true;
    }
    std::string& chosen = settings_.audioSource;
    std::string current = chosen.empty() ? "Default output (follows changes)" : chosen;
    for (const AudioSource& src : audioSources_)
        if (src.name == chosen) current = src.description;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20);
    if (ImGui::BeginCombo("Source", current.c_str())) {
        if (ImGui::Selectable("Default output (follows changes)", chosen.empty())) chosen.clear();
        for (const AudioSource& src : audioSources_) {
            const std::string label = (src.isMonitor ? "Output: " : "Input: ") + src.description;
            if (ImGui::Selectable(label.c_str(), src.name == chosen)) chosen = src.name;
        }
        ImGui::EndCombo();
        if (chosen != audio_.requestedDevice() && audio_.state() != AudioCapture::State::Off) {
            audio_.start(chosen);
            audioFailedAt_ = -1;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) audioSourcesListed_ = false;
    if (!audioListError_.empty()) ImGui::TextDisabled("%s", audioListError_.c_str());
    ImGui::TextDisabled("Audio is only captured while a shader with an audio input plays.");
}

void App::drawSettingsPanel() {
    ImGui::SetNextWindowPos(ImVec2(940, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(470, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &showSettings_)) {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const int caps[] = {0, 30, 60, 120, 144};
        const std::string capLabel = settings_.fpsCap == 0 ? "Display refresh" : std::to_string(settings_.fpsCap) + " FPS";
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
        if (ImGui::BeginCombo("FPS cap", capLabel.c_str())) {
            for (int c : caps)
                if (ImGui::Selectable(c == 0 ? "Display refresh" : (std::to_string(c) + " FPS").c_str(),
                                      settings_.fpsCap == c))
                    settings_.fpsCap = c;
            ImGui::EndCombo();
        }
        helpMarker("A lower cap saves GPU power. 'Display refresh' follows vsync; without vsync the app caps at 60.");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
        if (ImGui::SliderFloat("Render scale", &settings_.renderScale, 0.25f, 2.0f, "%.2f"))
            settings_.renderScale = std::round(settings_.renderScale * 20.0f) / 20.0f;
        helpMarker("Renders at this fraction of the window size and scales up. 0.5 is about 4x cheaper.");
    }

    if (ImGui::CollapsingHeader("Playlist", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Auto-advance", &settings_.autoAdvance);
        ImGui::SameLine();
        int secs = static_cast<int>(settings_.autoAdvanceSeconds);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        if (ImGui::InputInt("seconds##advance", &secs, 5, 30))
            settings_.autoAdvanceSeconds = std::clamp(secs, 5, 3600);
        helpMarker("A shader's own play time (right-click it in the library) overrides this.");
        bool shuffle = settings_.shuffle;
        if (ImGui::Checkbox("Shuffle (S)", &shuffle)) setShuffle(shuffle);
        ImGui::SameLine();
        bool locked = settings_.locked;
        if (ImGui::Checkbox("Lock (L)", &locked)) setLocked(locked);
        helpMarker("Lock stops auto-advance and transitions without changing these settings.");
        ImGui::Checkbox("Crossfade", &settings_.crossfade);
        ImGui::SameLine();
        float fade = static_cast<float>(settings_.crossfadeSeconds);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        if (ImGui::SliderFloat("seconds##fade", &fade, 0.1f, 10.0f, "%.1f"))
            settings_.crossfadeSeconds = std::clamp(static_cast<double>(fade), 0.1, 30.0);
        helpMarker("Blends into the next playlist shader. Both shaders render during the fade, so it costs about "
                   "twice the GPU time for those seconds.");
    }

    if (ImGui::CollapsingHeader("Audio")) drawAudioSourcePicker();

    if (ImGui::CollapsingHeader("Textures")) {
        ImGui::TextWrapped("Use an image of yours (PNG, JPEG, BMP, TGA) for a built-in texture. Cubemaps and "
                           "volumes can't be replaced yet.");
        if (ImGui::BeginTable("##tex", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            for (const BuiltinTexture& t : builtinTextures()) {
                if (t.kind != InputType::Texture) continue;
                ImGui::PushID(t.id);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(t.label);
                if (!t.available && !textures_.hasOverride(t.id)) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(none yet)");
                }
                ImGui::TableNextColumn();
                auto it = settings_.textureOverrides.find(t.id);
                const std::string saved = it == settings_.textureOverrides.end() ? "" : it->second;
                std::string& path = textureEdits_.try_emplace(t.id, saved).first->second;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputTextWithHint("##path", "built-in", &path, ImGuiInputTextFlags_EnterReturnsTrue))
                    applyTextureOverride(t.id, path);
                else if (!ImGui::IsItemActive() && path != saved)
                    path = saved;  // abandoned edit
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Full path to an image, then Enter");
                const std::string error = textures_.overrideError(t.id);
                if (!error.empty()) ImGui::TextColored(kErrorColor, "%s", error.c_str());
                ImGui::TableNextColumn();
                if (it != settings_.textureOverrides.end() && ImGui::SmallButton("Clear")) {
                    textureEdits_[t.id].clear();
                    applyTextureOverride(t.id, "");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Advanced")) {
        bool capOn = settings_.compileTimeCapMs > 0;
        if (ImGui::Checkbox("Compile-time cap", &capOn)) settings_.compileTimeCapMs = capOn ? 5000 : 0;
        if (capOn) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
            if (ImGui::InputInt("ms", &settings_.compileTimeCapMs, 500, 2000))
                settings_.compileTimeCapMs = std::clamp(settings_.compileTimeCapMs, 100, 60000);
        }
        helpMarker("Gives up on shaders that take longer than this to compile (and skips them in the playlist).");
        if (!caps_.parallelCompile)
            ImGui::TextColored(kErrorColor, "Your driver compiles in the foreground, so the cap can't interrupt it.");
    }

    ImGui::Separator();
    if (ImGui::Button("Reset to defaults")) {
        const Settings keep = settings_;
        settings_ = Settings{};
        settings_.audioSource = keep.audioSource;            // device choice isn't a "default"
        settings_.textureOverrides = keep.textureOverrides;  // nor are the user's files
        player_.setShuffle(settings_.shuffle);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Saved to %s", settingsPath_.c_str());
    ImGui::End();
}

// ---------------------------------------------------------------- screenshots

void App::takeScreenshot() {
    if (!current_ || !imageTarget_.valid()) {
        setStatus("Nothing to screenshot yet", true);
        return;
    }
    if (screenshot_.valid() && screenshot_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        setStatus("Still saving the previous screenshot", true);
        return;
    }
    pollScreenshot();

    const int w = imageTarget_.width(), h = imageTarget_.height();
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, imageTarget_.framebuffer());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    char stamp[32];
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    const fs::path dir = paths_.picturesDir;
    const fs::path file = dir / (std::string(APP_NAME) + "-" + slug(current_->shader().info.name) + "-" + stamp + ".png");

    // PNG encoding takes a moment at 4K, so it runs on a worker thread.
    screenshot_ = std::async(std::launch::async, [px = std::move(px), w, h, dir, file]() mutable {
        // GL rows start at the bottom; PNG rows at the top.
        const size_t row = static_cast<size_t>(w) * 4;
        for (int y = 0; y < h / 2; ++y)
            std::swap_ranges(px.begin() + static_cast<long>(y * row), px.begin() + static_cast<long>((y + 1) * row),
                             px.begin() + static_cast<long>((h - 1 - y) * row));
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::string error;
        if (!savePng(file, w, h, px.data(), &error)) return "!Screenshot failed: " + error;
        return "Screenshot saved: " + file.string() + " (" + std::to_string(w) + "x" + std::to_string(h) + ")";
    });
}

void App::pollScreenshot() {
    if (!screenshot_.valid() || screenshot_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const std::string msg = screenshot_.get();
    if (!msg.empty() && msg[0] == '!') setStatus(msg.substr(1), true);
    else setStatus(msg);
}

}  // namespace sp
