cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# App identity. To rename the app, change these three lines only.
# APP_NAME is used for the binary, XDG folders, log file and window class.
# ---------------------------------------------------------------------------
set(APP_NAME         "shaderplayer")
set(APP_DISPLAY_NAME "ShaderPlayer")
set(APP_VERSION      "0.1.0")

project(${APP_NAME} VERSION ${APP_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(SP_BUILD_TESTS "Build unit and headless GL tests" ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(nlohmann_json 3.2 REQUIRED)
pkg_check_modules(SDL2   REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(EGL    REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET glesv2)
pkg_check_modules(PULSE  REQUIRED IMPORTED_TARGET libpulse)

configure_file(src/app_info.h.in ${CMAKE_BINARY_DIR}/generated/app_info.h @ONLY)

# ---- Vendored Dear ImGui (SDL2 + OpenGL3 backends, GLES3 mode) ----
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
  ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3 IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC PkgConfig::SDL2 PkgConfig::GLESV2)

set(SP_WARNINGS -Wall -Wextra -Wno-unused-parameter)

# ---- Core: no SDL, no GL. Pure logic that tests can use anywhere. ----
add_library(sp_core STATIC
  src/platform/files.cpp
  src/platform/log.cpp
  src/platform/paths.cpp
  src/audio/analyser.cpp
  src/config/settings.cpp
  src/library/crash_guard.cpp
  src/library/library.cpp
  src/library/playlist_player.cpp
  src/library/playlists.cpp
  src/shadertoy/json_format.cpp
  src/shadertoy/mouse.cpp
  src/shadertoy/shader.cpp
  src/shadertoy/shader_ref.cpp
  src/shadertoy/uniforms.cpp
  src/shadertoy/wrapper.cpp
  src/textures/artistic.cpp
  src/textures/builtin_list.cpp
  src/textures/font_texture.cpp
  src/textures/image_io.cpp
  src/textures/procedural.cpp)
target_include_directories(sp_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
target_include_directories(sp_core SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_compile_options(sp_core PRIVATE ${SP_WARNINGS})
# imgui only for Font 1: its embedded font file and stb_truetype (see textures/font_texture.cpp).
target_link_libraries(sp_core PUBLIC Threads::Threads nlohmann_json::nlohmann_json PRIVATE imgui)

# ---- Audio capture (libpulse; works on PipeWire through pipewire-pulse) ----
add_library(sp_audio STATIC
  src/audio/capture.cpp)
target_compile_options(sp_audio PRIVATE ${SP_WARNINGS})
target_link_libraries(sp_audio PUBLIC sp_core PkgConfig::PULSE)

# ---- GL helpers: GLES 3.0 only, no SDL (usable from headless tests). ----
add_library(sp_gfx STATIC
  src/gfx/audio_texture.cpp
  src/gfx/gl_caps.cpp
  src/gfx/neutral_textures.cpp
  src/gfx/program.cpp
  src/gfx/render_target.cpp
  src/gfx/texture_library.cpp
  src/shadertoy/renderer.cpp)
target_compile_options(sp_gfx PRIVATE ${SP_WARNINGS})
target_link_libraries(sp_gfx PUBLIC sp_core PkgConfig::GLESV2 PkgConfig::EGL)

# ---- App: window, UI, main loop. ----
add_library(sp_app STATIC
  src/app/app.cpp
  src/app/app_guard.cpp
  src/app/app_library.cpp
  src/app/app_playlist.cpp
  src/app/app_settings.cpp
  src/app/app_ui.cpp
  src/app/demos.cpp
  src/app/hotkeys.cpp
  src/platform/window.cpp
  src/ui/file_browser.cpp
  src/ui/overlay.cpp
  src/ui/paste_dialog.cpp
  src/ui/shader_editor.cpp)
target_compile_options(sp_app PRIVATE ${SP_WARNINGS})
target_link_libraries(sp_app PUBLIC sp_gfx sp_audio imgui PkgConfig::SDL2 PkgConfig::EGL)

add_executable(${APP_NAME} src/main.cpp)
target_compile_options(${APP_NAME} PRIVATE ${SP_WARNINGS})
target_link_libraries(${APP_NAME} PRIVATE sp_app)

include(GNUInstallDirs)
install(TARGETS ${APP_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(SP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
