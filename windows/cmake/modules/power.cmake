# Power area fragment (included after every target in windows/CMakeLists.txt).
# Tests use the detached Mac oracle fixture and synthetic readings only: no
# real power state, battery device, settings file, window or user data.

# Charge indicator/badge models, OverlayController presentation, DisplayPolicy,
# AppDelegate battery rules, event recorder, tray text, device readings and
# overlay geometry. Portable, explicit-clock state; no timer or window.
add_library(hud_charge modules/charge_indicator.cpp modules/charge_badge.cpp modules/charge_overlay.cpp modules/power_policy.cpp)
target_include_directories(hud_charge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_charge PUBLIC hud_battery hud_event_log hud_motion hud_localization hud_data hud_scene)

# Observer of the shared SystemServices battery provider plus the battery
# class capacity pair (utility worker, one read in flight + one trailing).
add_library(hud_power_service native/power_service.cpp native/battery_capacity.cpp)
target_include_directories(hud_power_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_power_service PUBLIC hud_charge hud_utility)
if(WIN32)
  target_compile_definitions(hud_power_service PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_power_service PRIVATE setupapi)
endif()

set(EHUD_POWER_TARGETS hud_charge hud_power_service)

if(WIN32)
  # Shared native renderer of one ChargeIndicatorView canvas (badge + alert).
  add_library(hud_charge_scene native/charge_indicator_scene.cpp)
  target_include_directories(hud_charge_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(hud_charge_scene PUBLIC hud_charge hud_native)
  target_compile_definitions(hud_charge_scene PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
  # HUD charge badge owner (core plane, hover expansion, Power selection).
  add_library(hud_charge_badge tools/charge_badge_preview.cpp)
  target_include_directories(hud_charge_badge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(hud_charge_badge PUBLIC hud_charge_scene hud_overlay_host)
  target_compile_definitions(hud_charge_badge PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
  # Floating desktop charge alert window (own small device, its own UI
  # Automation root and the editor buttons' system tool tips) and the Power
  # production owner (provider observer, alert rules, recorder, tray text).
  add_library(hud_charge_panel native/charge_indicator_panel.cpp native/charge_alert_automation.cpp)
  target_include_directories(hud_charge_panel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(hud_charge_panel PUBLIC hud_charge_scene PRIVATE user32 shcore comctl32 uiautomationcore oleaut32 ole32)
  target_compile_definitions(hud_charge_panel PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
  add_library(hud_power_owner tools/power_owner.cpp)
  target_include_directories(hud_power_owner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(hud_power_owner PUBLIC hud_charge_panel hud_power_service)
  target_compile_definitions(hud_power_owner PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
  list(APPEND EHUD_POWER_TARGETS hud_charge_scene hud_charge_badge hud_charge_panel hud_power_owner)
  # UI Automation peer of the Power navigation entry (the source shell's one
  # native navigation button, framed on the charge badge).
  add_library(hud_power_automation native/power_automation.cpp)
  target_include_directories(hud_power_automation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_power_automation PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_power_automation PUBLIC hud_scene PRIVATE uiautomationcore oleaut32 ole32)
  list(APPEND EHUD_POWER_TARGETS hud_power_automation)

  # Power module area (app/module_owner.hpp, "Adding a module area"): the HUD
  # charge badge, the floating alert and the battery rules as ONE ModuleOwner,
  # registered for EndfieldHUD.exe and the hidden coverage harness. The alert
  # window renders with the packaged shell.shader resource (app.cmake).
  add_library(hud_power_area STATIC app/areas/power_module.cpp)
  target_include_directories(hud_power_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_power_area PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_power_area PUBLIC hud_module_registry hud_power_owner hud_charge_badge hud_power_automation hud_utility hud_data hud_services
    PRIVATE user32 oleaut32)
  list(APPEND EHUD_POWER_TARGETS hud_power_area)
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "powerCharge|hud_power_area|app/areas/power_module.hpp|endfield::power::powerModule")
endif()

if(BUILD_TESTING)
  # Source: windows/tools/charge_indicator_reference.sh (Mac-only oracle export).
  add_executable(charge_indicator_tests tests/charge_indicator_tests.cpp)
  target_link_libraries(charge_indicator_tests PRIVATE hud_charge)
  add_test(NAME charge_indicator_contracts COMMAND charge_indicator_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/charge-indicator-source.json)
  add_executable(charge_badge_tests tests/charge_badge_tests.cpp)
  target_link_libraries(charge_badge_tests PRIVATE hud_charge)
  add_test(NAME charge_badge_contracts COMMAND charge_badge_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/charge-indicator-source.json)
  add_executable(power_policy_tests tests/power_policy_tests.cpp)
  target_link_libraries(power_policy_tests PRIVATE hud_charge)
  add_test(NAME power_policy_contracts COMMAND power_policy_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/charge-indicator-source.json)
  add_executable(power_service_tests tests/power_service_tests.cpp)
  target_link_libraries(power_service_tests PRIVATE hud_power_service)
  add_test(NAME power_service_contracts COMMAND power_service_tests)
  list(APPEND EHUD_POWER_TARGETS charge_indicator_tests charge_badge_tests power_policy_tests power_service_tests)
  if(WIN32)
    add_executable(charge_indicator_scene_tests tests/charge_indicator_scene_tests.cpp)
    target_compile_definitions(charge_indicator_scene_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(charge_indicator_scene_tests PRIVATE hud_charge_scene ole32)
    add_test(NAME charge_indicator_scene_contracts COMMAND charge_indicator_scene_tests ${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl)
    set_tests_properties(charge_indicator_scene_contracts PROPERTIES TIMEOUT 180)
    add_executable(charge_badge_preview_tests tests/charge_badge_preview_tests.cpp)
    target_compile_definitions(charge_badge_preview_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(charge_badge_preview_tests PRIVATE hud_charge_badge ole32)
    add_test(NAME charge_badge_preview_contracts COMMAND charge_badge_preview_tests ${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl)
    set_tests_properties(charge_badge_preview_contracts PROPERTIES TIMEOUT 180)
    add_executable(charge_indicator_panel_tests tests/charge_indicator_panel_tests.cpp)
    target_compile_definitions(charge_indicator_panel_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(charge_indicator_panel_tests PRIVATE hud_charge_panel uiautomationcore oleaut32 ole32 user32)
    add_test(NAME charge_indicator_panel_contracts COMMAND charge_indicator_panel_tests ${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl)
    set_tests_properties(charge_indicator_panel_contracts PROPERTIES TIMEOUT 180)
    add_executable(power_owner_tests tests/power_owner_tests.cpp)
    target_compile_definitions(power_owner_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(power_owner_tests PRIVATE hud_power_owner ole32)
    add_test(NAME power_owner_contracts COMMAND power_owner_tests ${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl)
    set_tests_properties(power_owner_contracts PROPERTIES TIMEOUT 180)
    # The Power area over an in-memory ApplicationHost (edit/close, quitting,
    # suspension reasons, Projection, the page sink and its coverage step).
    add_executable(power_area_tests tests/power_area_tests.cpp)
    target_compile_definitions(power_area_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(power_area_tests PRIVATE hud_power_area uiautomationcore oleaut32 ole32)
    add_test(NAME power_area_contracts COMMAND power_area_tests ${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl)
    set_tests_properties(power_area_contracts PROPERTIES TIMEOUT 300)
    # The Power navigation entry's UIA peer under a fake owner group.
    add_executable(power_automation_tests tests/power_automation_tests.cpp)
    target_compile_definitions(power_automation_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(power_automation_tests PRIVATE hud_power_automation uiautomationcore oleaut32 ole32)
    add_test(NAME power_automation_contracts COMMAND power_automation_tests)
    list(APPEND EHUD_POWER_TARGETS charge_indicator_scene_tests charge_badge_preview_tests charge_indicator_panel_tests power_owner_tests power_area_tests
      power_automation_tests)
    # User-run read-only capacity probe; the registered test checks it is inert.
    add_executable(battery_capacity_probe tools/battery_capacity_probe.cpp)
    target_compile_definitions(battery_capacity_probe PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(battery_capacity_probe PRIVATE hud_power_service hud_services)
    add_test(NAME battery_capacity_probe_inert COMMAND battery_capacity_probe)
    set_tests_properties(battery_capacity_probe_inert PROPERTIES PASS_REGULAR_EXPRESSION "inert")
    # User-run alert diagnostics (screens, hidden layered composition target).
    add_executable(charge_alert_probe tools/charge_alert_probe.cpp)
    target_compile_definitions(charge_alert_probe PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(charge_alert_probe PRIVATE hud_charge_panel)
    add_test(NAME charge_alert_probe_inert COMMAND charge_alert_probe)
    set_tests_properties(charge_alert_probe_inert PROPERTIES PASS_REGULAR_EXPRESSION "inert")
    list(APPEND EHUD_POWER_TARGETS battery_capacity_probe charge_alert_probe)
  endif()
endif()

foreach(target IN LISTS EHUD_POWER_TARGETS)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()
