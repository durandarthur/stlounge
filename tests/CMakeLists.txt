# Unit tests: pure logic, no display or GPU needed.
add_executable(sp_unit_tests
  test_main.cpp
  test_paths.cpp
  test_hotkeys.cpp
  test_wrapper.cpp
  test_mouse_uniforms.cpp
  test_textures.cpp
  test_audio.cpp
  test_import.cpp
  test_playlist.cpp)
target_include_directories(sp_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(sp_unit_tests PRIVATE ${SP_WARNINGS})
target_link_libraries(sp_unit_tests PRIVATE sp_core sp_audio sp_app)
add_test(NAME unit COMMAND sp_unit_tests)

# GL tests: offscreen GLES 3 context (Mesa surfaceless, EGL device, or default display).
# Exit code 77 = skipped (no context available).
add_executable(sp_gl_tests
  test_main.cpp
  headless_gl.cpp
  test_gl_caps.cpp
  test_renderer.cpp
  test_multipass.cpp
  test_volume_and_samples.cpp)
target_include_directories(sp_gl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(sp_gl_tests PRIVATE ${SP_WARNINGS})
target_link_libraries(sp_gl_tests PRIVATE sp_gfx PkgConfig::EGL)
add_test(NAME gl COMMAND sp_gl_tests)
set_tests_properties(gl PROPERTIES SKIP_RETURN_CODE 77)

foreach(t sp_unit_tests sp_gl_tests)
  target_compile_definitions(${t} PRIVATE SP_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
endforeach()
