# Clipboard, Event Log and Volume on the design library (Mac 1.3.0
# ClipboardCanvas, EventLogCanvas, VolumeCanvas; inventory DOCS-531..552,
# MODS-240..260, MODS-700..721, MODS-747). The canvases are portable
# (modules/*_canvas.*); their platform services stay where they were: the
# clipboard history and its provider (native/system_services.*,
# native/clipboard_provider.*), the Event Log store, owner and save queue
# (core/data/event_log_store.*, native/event_log_owner.*,
# app/event_log_save_queue.*), and the shared audio runtime, Volume provider
# and app icon queue (native/audio_runtime.*, native/volume_provider.*,
# native/volume_icon_provider.*). Each module is one DesignModuleOwner
# (tools/hud_design_owner.hpp) built by app/application_modules.cpp.
add_library(hud_utility_canvases modules/clipboard_canvas.cpp modules/event_log_canvas.cpp modules/volume_canvas.cpp)
target_include_directories(hud_utility_canvases PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# The Event Log sentences use Work Mode's duration title, the charge metric and
# centre logo titles; Volume reads the audio runtime's snapshot model.
target_link_libraries(hud_utility_canvases PUBLIC hud_design hud_event_log hud_work_mode_canvas hud_charge hud_display_center_logo hud_volume_model)
if(MSVC)
  target_compile_options(hud_utility_canvases PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hud_utility_canvases PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Portable contracts of the three canvases (Swift-derived rules) and, node by
# node, the Mac oracle: tools/utility_canvases_reference.sh (unchanged Mac
# Sources, real stores, private pasteboard) -> tests/fixtures/utility-canvases-source.json.
if(BUILD_TESTING)
  add_executable(utility_canvases_tests tests/utility_canvases_tests.cpp)
  target_link_libraries(utility_canvases_tests PRIVATE hud_utility_canvases)
  add_test(NAME utility_canvases_contracts COMMAND utility_canvases_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/utility-canvases-source.json")
endif()

# The application builds the three DesignModuleOwners (app/application.cpp);
# the Clipboard provider names a record's source app with the Activity
# catalog's FileDescription reader (native/clipboard_provider.cpp).
if(TARGET hud_application)
  target_link_libraries(hud_application PUBLIC hud_utility_canvases)
endif()
if(WIN32 AND TARGET hud_native AND TARGET hud_activity_provider)
  target_link_libraries(hud_native PRIVATE hud_activity_provider)
endif()
