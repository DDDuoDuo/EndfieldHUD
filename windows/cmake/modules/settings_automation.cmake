# Settings UI Automation (Windows): the projected System, Display, Hotkeys
# and About controls, their choice pages and the layout confirmation card as
# fragments under the Settings owner's group of the HUD window's UIA root
# (native/settings_automation: Button/Toggle/Slider/List/ListItem/Hyperlink/
# Text/dialog fragments; tools/settings_automation: the nodes over the
# Settings owner and its presses). The built-in Settings adapter
# (app/application_modules.cpp) forwards its ModuleOwner automation hooks to
# it. The suite drives the real Settings owner on a hidden window and WARP
# with injected services: no UIA client, no visible window, no input
# injection, no user data.
if(WIN32 AND TARGET hud_local_automation AND TARGET hud_native)
  add_library(hud_settings_automation STATIC native/settings_automation.cpp tools/settings_automation.cpp)
  target_include_directories(hud_settings_automation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_settings_automation PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_settings_automation PUBLIC hud_local_automation hud_native hud_overlay_host PRIVATE uiautomationcore oleaut32 ole32 user32)
  # The UIA element implementation is the design library's AutomationTree
  # (native/hud_design_automation.cpp; LIBRARY-DESIGN.md Q2).
  target_link_libraries(hud_settings_automation PUBLIC hud_design_native)
  if(MSVC)
    target_compile_options(hud_settings_automation PRIVATE /W4 /permissive- /EHsc)
  endif()
  if(TARGET hud_application)
    target_link_libraries(hud_application PUBLIC hud_settings_automation)
  endif()
  if(BUILD_TESTING)
    # The HUD window's UIA root (app/hud_automation.cpp, unchanged host
    # source) is compiled in to drive the fragments under it.
    add_executable(settings_automation_tests tests/settings_automation_tests.cpp tools/settings_preview.cpp app/hud_automation.cpp)
    target_compile_definitions(settings_automation_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(settings_automation_tests PRIVATE hud_settings_automation hud_module_registry hud_native hud_overlay_host uiautomationcore oleaut32 ole32 user32)
    if(MSVC)
      target_compile_options(settings_automation_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME settings_automation_contracts COMMAND settings_automation_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl")
    set_tests_properties(settings_automation_contracts PROPERTIES TIMEOUT 300)
  endif()
endif()
