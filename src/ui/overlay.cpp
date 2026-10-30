#include "ui/overlay.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_internal.h>  // ClearActiveID

#include "platform/log.h"

namespace sp {

bool Overlay::init(SDL_Window* window, SDL_GLContext context, const std::string& iniPath) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    iniPath_ = iniPath;
    io.IniFilename = iniPath_.empty() ? nullptr : iniPath_.c_str();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().Alpha = 0.95f;

    if (!ImGui_ImplSDL2_InitForOpenGL(window, context)) {
        LOG_ERROR("ImGui SDL2 backend init failed");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOG_ERROR("ImGui OpenGL3 (GLES3) backend init failed");
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    initialized_ = true;
    return true;
}

void Overlay::shutdown() {
    if (!initialized_) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

void Overlay::processEvent(const SDL_Event& e) {
    if (initialized_) ImGui_ImplSDL2_ProcessEvent(&e);
}

void Overlay::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    if (clearFocusPending_) {
        ImGui::SetWindowFocus(nullptr);
        ImGui::ClearActiveID();
        clearFocusPending_ = false;
    }
}

void Overlay::endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool Overlay::wantsMouse() const { return initialized_ && ImGui::GetIO().WantCaptureMouse; }

bool Overlay::wantsTextInput() const { return initialized_ && ImGui::GetIO().WantTextInput; }

void Overlay::clearFocus() { clearFocusPending_ = true; }

}  // namespace sp
