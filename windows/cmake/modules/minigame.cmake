# Merge! OrbiPom! on the design library (Mac 1.3.0 OrbiPomCanvas; inventory
# MODS-200..230). The canvas is portable (modules/orbipom_canvas.*); the game
# session (modules/orbipom_state.*) and the original runtime
# (modules/orbipom_runtime.*: Matter 0.20.0 + orbipom.js on QuickJS) are
# unchanged services in CMakeLists.txt. The application builds one
# DesignModuleOwner for it (app/application.cpp, constructGame).
add_library(hud_orbipom_canvas modules/orbipom_canvas.cpp)
target_include_directories(hud_orbipom_canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_orbipom_canvas PUBLIC hud_design hud_orbipom)
if(MSVC)
  target_compile_options(hud_orbipom_canvas PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hud_orbipom_canvas PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Portable contracts of the canvas (Swift-derived rules over a scripted
# runtime port) and, node by node, the Mac oracle:
# tools/play_canvases_reference.sh -> tests/fixtures/play-canvases-source.json.
if(BUILD_TESTING)
  add_executable(orbipom_canvas_tests tests/orbipom_canvas_tests.cpp)
  target_link_libraries(orbipom_canvas_tests PRIVATE hud_orbipom_canvas)
  add_test(NAME orbipom_canvas_contracts COMMAND orbipom_canvas_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/play-canvases-source.json")
endif()

if(TARGET hud_application)
  target_link_libraries(hud_application PUBLIC hud_orbipom_canvas)
endif()
