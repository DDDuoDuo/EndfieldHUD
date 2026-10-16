# Desktop Map occlusion (core/map_source_cutout.hpp; Mac HUDSourceWatchView
# desktopMapOcclusion and SystemHUDView.applySourceMapOcclusion). Every module
# canvas is composed above the source shell, as the Mac stacks its module
# layers above HUDSourceWatchView; the Map alone is drawn 1.14x larger than its
# plane and reaches over the Storage and Activity Monitor plates below the
# circle. While the Map is posed on the source shell those plates stay above
# it: the Map's feather mask is cut under their projected sprite-mesh
# boundaries (native/map_scene) and presses inside them are left to the
# navigation (tools/map_preview covers/pointer). The host hands the Map its
# presented source frame (MapPreviewOptions::sourceFrame).
add_library(hud_map_cutout core/map_source_cutout.cpp)
target_include_directories(hud_map_cutout PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_map_cutout PUBLIC hud_source_motion)
target_link_libraries(hud_map_scene PUBLIC hud_map_cutout)
if(BUILD_TESTING)
  # tools/map_cutout_reference.py -> tests/fixtures/map_cutout_source.json
  add_executable(map_cutout_tests tests/map_cutout_tests.cpp)
  target_link_libraries(map_cutout_tests PRIVATE hud_map_cutout)
  add_test(NAME map_cutout_contracts COMMAND map_cutout_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/map_cutout_source.json)
endif()
# The same cut over the packaged source shell at rest on several displays:
# the runtime scene's plates are found, the Map's circle reaches over them,
# and with the presented frame they are drawn and pressed above the Map.
set(ehud_map_layer_inputs "${EHUD_APP_INPUT_DIR}")
if(NOT ehud_map_layer_inputs AND EHUD_MAP_RESOURCE_DIR)
  get_filename_component(ehud_map_layer_inputs "${EHUD_MAP_RESOURCE_DIR}" DIRECTORY)
endif()
if(WIN32 AND BUILD_TESTING AND TARGET hud_map_preview AND TARGET hud_watch_runtime
   AND EXISTS "${ehud_map_layer_inputs}/runtime-compiled-animation/runtime-input.json")
  add_executable(map_occlusion_tests tests/map_occlusion_tests.cpp)
  target_compile_definitions(map_occlusion_tests PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(map_occlusion_tests PRIVATE hud_map_preview hud_watch_runtime)
  add_test(NAME map_occlusion_contracts COMMAND map_occlusion_tests ${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl
    "${ehud_map_layer_inputs}/runtime-compiled-animation")
  set_tests_properties(map_occlusion_contracts PROPERTIES TIMEOUT 300)
endif()
