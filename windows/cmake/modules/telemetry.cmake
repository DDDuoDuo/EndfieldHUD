# Storage, Activity Monitor and Battery on the design library (Mac 1.3.0
# Sources/TelemetryCanvases.swift, ActivityMonitorCanvas.swift,
# BatteryCanvas.swift). hud_telemetry (portable): the shared number formats and
# the three canvases over the existing models (modules/activity_state,
# modules/storage_state, modules/battery_reading); no window, timer or OS
# provider. The native owners (WIN32) bind the platform services to them.
add_library(hud_telemetry modules/telemetry_format.cpp modules/battery_canvas.cpp modules/storage_canvas.cpp modules/activity_canvas.cpp)
target_include_directories(hud_telemetry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_telemetry PUBLIC hud_design hud_charge hud_battery hud_activity hud_storage)
if(MSVC)
  target_compile_options(hud_telemetry PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hud_telemetry PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  # The canvases in their Mac states (geometry, strings, values, controls,
  # accessibility, lifecycle), compared with the Mac 1.3.0 oracle
  # (tools/telemetry_reference.*: formats, graph paths, tones, diagnostics,
  # the StorageController script and every canvas state's accessibility and
  # text layers, measured with the Mac's recorded widths).
  add_executable(telemetry_canvas_tests tests/telemetry_canvas_tests.cpp)
  target_link_libraries(telemetry_canvas_tests PRIVATE hud_telemetry)
  if(MSVC)
    target_compile_options(telemetry_canvas_tests PRIVATE /W4 /permissive- /EHsc)
  endif()
  add_test(NAME telemetry_canvas_contracts COMMAND telemetry_canvas_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/telemetry-source.json")
endif()

# The native owners (WIN32): Activity Monitor's readings, icon loads and
# sampling demand (tools/activity_owner) and Storage's controller, capacity
# probe, remembered disk and mount events (tools/storage_owner) around their
# canvases on tools::DesignModuleOwner. Battery needs no service: the host
# forwards readings and the alert metric to its canvas (app/application.cpp).
if(WIN32 AND TARGET hud_design_native AND TARGET hud_activity_icons)
  add_library(hud_telemetry_owners tools/activity_owner.cpp tools/storage_owner.cpp)
  target_include_directories(hud_telemetry_owners PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_telemetry_owners PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_telemetry_owners PUBLIC hud_telemetry hud_design_native hud_activity_icons hud_activity_provider hud_storage_files)
  if(MSVC)
    target_compile_options(hud_telemetry_owners PRIVATE /W4 /permissive- /EHsc)
  endif()
  # The built-in owners the host publishes (their memory checks).
  if(TARGET hud_system_providers_coverage)
    target_link_libraries(hud_system_providers_coverage PUBLIC hud_telemetry_owners)
  endif()
  if(BUILD_TESTING)
    # The three modules through their owners on a hidden window and WARP
    # (synthetic readings, apps, icons and volumes; no volume, process or
    # battery of this machine is read): presentation, routing, accessibility,
    # live updates, the storage disk choice and its remembered key, mount
    # events, the charging pulse and parking on close.
    add_executable(telemetry_owner_tests tests/telemetry_owner_tests.cpp)
    target_compile_definitions(telemetry_owner_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(telemetry_owner_tests PRIVATE hud_telemetry_owners hud_overlay_host uiautomationcore oleaut32 ole32 user32)
    if(MSVC)
      target_compile_options(telemetry_owner_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME telemetry_owner_contracts COMMAND telemetry_owner_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl")
    set_tests_properties(telemetry_owner_contracts PROPERTIES TIMEOUT 300)
  endif()
endif()
