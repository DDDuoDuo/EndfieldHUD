# Renderer memory fragment (native/renderer.*, native/layer_*): layer pixels
# given back once uploaded, parked textures and four-byte (or alpha-only)
# texture storage.
#
# layer_switch_contracts: module switches inside the open HUD never paint
# again. Through the real owners of Clipboard, Event Log, Media Assembly and
# Archive, published as the host publishes owners (hidden window, WARP,
# synthetic data, temporary folders): the switch away parks the textures,
# the switch back restores them (no repaint, identical pixels); it prints the
# first showing, switch-away and switch-back frame times, the parked bytes
# and, for comparison, the switch back after the ten-minute release.
if(WIN32 AND BUILD_TESTING AND TARGET hud_activity_preview AND TARGET hud_storage_preview AND TARGET hud_media_assembly_preview
   AND TARGET hud_archive_preview AND TARGET hud_native)
  add_executable(layer_switch_probe tests/layer_switch_probe.cpp tools/clipboard_preview.cpp tools/event_log_preview.cpp)
  target_compile_definitions(layer_switch_probe PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(layer_switch_probe PRIVATE hud_activity_preview hud_activity_provider hud_storage_preview hud_storage_options
    hud_media_assembly_preview hud_archive_preview hud_native hud_motion psapi ole32)
  if(MSVC)
    target_compile_options(layer_switch_probe PRIVATE /EHsc)
  endif()
  add_test(NAME layer_switch_contracts COMMAND layer_switch_probe "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/archive-source-v1.json")
  set_tests_properties(layer_switch_contracts PROPERTIES TIMEOUT 300)
endif()
