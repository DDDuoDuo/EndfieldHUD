# The Map on the design library (Mac 1.3.0 Sources/WorldMapCanvas.swift,
# WorldMapSurface.swift). hud_map_canvas (portable): the canvas over the
# surface model (modules/map_state); no window, timer or provider.
# hud_map_owner (WIN32): its owner (tools/map_owner) with the surface's
# native pixels (native/map_scene), the retained raster and the desktop Map
# occlusion around the canvas on tools::DesignOwnerCore.
add_library(hud_map_canvas modules/map_canvas.cpp)
target_include_directories(hud_map_canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_map_canvas PUBLIC hud_design hud_map)
if(MSVC)
  target_compile_options(hud_map_canvas PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hud_map_canvas PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  # The canvas driven through its host entries in the Mac's scripted session
  # (tools/map_canvas_reference.*: the surface maths in map px, the canvas
  # geometry, and after every step the camera, the pins, the selection, the
  # plates, the controls, the accessibility items, the cursor and the toast,
  # with the Mac's recorded text widths).
  add_executable(map_canvas_tests tests/map_canvas_tests.cpp)
  target_link_libraries(map_canvas_tests PRIVATE hud_map_canvas)
  if(MSVC)
    target_compile_options(map_canvas_tests PRIVATE /W4 /permissive- /EHsc)
  endif()
  add_test(NAME map_canvas_contracts COMMAND map_canvas_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/map-canvas-source.json")
endif()

if(WIN32 AND TARGET hud_design_native)
  add_library(hud_map_owner tools/map_owner.cpp)
  target_include_directories(hud_map_owner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_map_owner PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_map_owner PUBLIC hud_map_canvas hud_map_scene hud_map_native hud_map_cutout hud_design_native hud_overlay_host hud_localization)
  if(MSVC)
    target_compile_options(hud_map_owner PRIVATE /W4 /permissive- /EHsc)
  endif()
  if(BUILD_TESTING)
    # The Map through its owner on a hidden window and WARP (synthetic
    # geography, painter and store): presentation, routing in box px and
    # outside the box, the wheel detent, pinch, keys, the gesture hold,
    # accessibility, the raster lifecycle, the host cut, the pulse fallback
    # and the prewarm; then the reopen cost with the bundled geography and the
    # real painter, keeping the terrain on close against releasing it.
    add_executable(map_owner_tests tests/map_owner_tests.cpp)
    target_compile_definitions(map_owner_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(map_owner_tests PRIVATE hud_map_owner hud_map_assets uiautomationcore oleaut32 ole32 user32)
    if(MSVC)
      target_compile_options(map_owner_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME map_owner_contracts COMMAND map_owner_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl" "${EHUD_MAP_RESOURCE_DIR}")
    set_tests_properties(map_owner_contracts PROPERTIES TIMEOUT 300)
  endif()
endif()
