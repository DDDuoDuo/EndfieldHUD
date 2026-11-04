# Settings on the design library (Mac 1.3.0 HUDSettingsCanvas; inventory
# SHELL-90..105): the four canvases (System, Display, Hotkeys, About) are
# portable (modules/settings_canvas.*) over the shared preference controller
# (modules/settings.*, hud_settings). The platform side (the colour wheel and
# logo file picker results, the display list, decoded logo artwork, the
# right-hand list glyphs and app icons, the update area) is the Settings owner
# tools/settings_preview.* (WIN32), one DesignOwnerCore with four canvases.
add_library(hud_settings_canvas modules/settings_canvas.cpp)
target_include_directories(hud_settings_canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_settings_canvas PUBLIC hud_settings hud_design)
if(MSVC)
  target_compile_options(hud_settings_canvas PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hud_settings_canvas PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The Settings owner (WIN32): the canvases in one DesignOwnerCore, the icon
# roster and glyphs, the center logo chooser rasters. Linked into the
# application's module sources and the development harness.
if(WIN32 AND TARGET hud_design_native)
  add_library(hud_settings_owner tools/settings_preview.cpp)
  target_include_directories(hud_settings_owner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_settings_owner PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_settings_owner PUBLIC hud_settings_canvas hud_design_native hud_display_center_logo hud_native)
  if(MSVC)
    target_compile_options(hud_settings_owner PRIVATE /W4 /permissive- /EHsc)
  endif()
  foreach(consumer hud_application watch_session_preview)
    if(TARGET ${consumer})
      target_link_libraries(${consumer} PUBLIC hud_settings_owner)
    endif()
  endforeach()
  if(BUILD_TESTING)
    # Hidden window + WARP: the owner on the shared design Context, the packaged
    # glyphs and icons, the chooser's decoded logos, the prompt's deadline, park.
    add_executable(settings_owner_tests tests/settings_owner_tests.cpp)
    target_compile_definitions(settings_owner_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(settings_owner_tests PRIVATE hud_settings_owner ole32)
    if(MSVC)
      target_compile_options(settings_owner_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME settings_owner_contracts COMMAND settings_owner_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl"
      "${CMAKE_CURRENT_SOURCE_DIR}/resources" "${CMAKE_CURRENT_SOURCE_DIR}/../Resources/EndfieldIndustriesSource.png")
  endif()
endif()
# The Center logo chooser's ENDFIELD INDUSTRIES lettering is the band of the
# Mac's own Resources/EndfieldIndustriesSource.png (HUDSettingsCanvas
# .endfieldWordmark), staged from the shared Resources folder, not copied.
if(COMMAND ehud_app_resource)
  ehud_app_resource(settings.endfieldWordmark file "${CMAKE_CURRENT_SOURCE_DIR}/../Resources/EndfieldIndustriesSource.png" "settings/endfield-industries.png")
endif()

if(BUILD_TESTING)
  # The four canvases replayed against the Mac oracle (tools/settings_reference.sh).
  add_executable(settings_canvas_tests tests/settings_canvas_tests.cpp)
  target_link_libraries(settings_canvas_tests PRIVATE hud_settings_canvas)
  if(MSVC)
    target_compile_options(settings_canvas_tests PRIVATE /W4 /permissive- /EHsc)
  endif()
  add_test(NAME settings_canvas_contracts COMMAND settings_canvas_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/settings-source.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/watch-appearance/manifest.json" "${CMAKE_CURRENT_SOURCE_DIR}/modules/settings_canvas.cpp")
endif()
