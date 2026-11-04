# Facility alerts, the daily boot log and the shell's Recording-mode pieces
# (Mac Sources/FacilityAlertMonitor.swift, HUDFacilityBanner.swift,
# HUDFacilityTags.swift, HUDBootLog.swift; SHELL-20..25, SHELL-69..78).
# Loads after hud_design.cmake (sorted fragments), whose targets it uses.
#
# hud_facility (portable): the alert model and monitor (explicit clock,
# injected readers and worker), the banner, the tag stack and the boot log on
# the design library. No window, timer, thread or OS call.
add_library(hud_facility modules/facility_alerts.cpp modules/facility_banner.cpp modules/facility_tags.cpp modules/boot_log.cpp)
target_include_directories(hud_facility PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_facility PUBLIC hud_design hud_localization hud_motion)
if(MSVC)
  target_compile_options(hud_facility PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hud_facility PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Portable contracts (derived from the Swift; the oracle suite below compares
# against the Mac's own output): thresholds, edge triggering, the recovery hold, the
# strings in the five languages, the Windows memory mapping (facility_alerts);
# banner and tag geometry, queue, countdown, hover pause, dismiss/collapse
# timing and Reduce Motion (facility_surfaces); boot log lines, timing,
# capacity, cursor, skip and the daily day key (boot_log).
if(BUILD_TESTING)
  foreach(suite facility_alerts facility_surfaces boot_log)
    add_executable(${suite}_tests tests/${suite}_tests.cpp)
    target_link_libraries(${suite}_tests PRIVATE hud_facility)
    add_test(NAME ${suite}_contracts COMMAND ${suite}_tests)
  endforeach()
  # The Mac 1.3.0 oracle (tools/facility_reference.sh -> tests/fixtures/facility-source.json):
  # strings in five languages, the banner, the tag stack and the boot log primitive by primitive.
  add_executable(facility_oracle_tests tests/facility_oracle_tests.cpp)
  target_link_libraries(facility_oracle_tests PRIVATE hud_facility)
  add_test(NAME facility_oracle_contracts COMMAND facility_oracle_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/facility-source.json")
endif()

# hud_facility_area (WIN32): the Windows readers (native/facility_sources)
# and the area owner (native/facility_owner): the banner flat in shell
# space, the tags on the core plane, the boot log on the content box, input,
# UI Automation stand-ins, the monitor's cadence on the host's wake-ups.
# Registered as the "facility" module area (app/module_owner.hpp contract).
if(WIN32 AND TARGET hud_design_native AND TARGET hud_module_registry)
  add_library(hud_facility_area STATIC native/facility_sources.cpp native/facility_owner.cpp)
  target_include_directories(hud_facility_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_facility_area PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  # The terminal line's version: source-authority.json's release (read by app.cmake), without a leading v.
  if(DEFINED EHUD_APP_RELEASE)
    string(REGEX REPLACE "^v" "" ehud_facility_version "${EHUD_APP_RELEASE}")
    target_compile_definitions(hud_facility_area PRIVATE "EHUD_FACILITY_VERSION=\"${ehud_facility_version}\"")
  endif()
  target_link_libraries(hud_facility_area PUBLIC hud_facility hud_design_native hud_module_registry hud_utility hud_storage_files
    hud_event_log_platform hud_work_mode PRIVATE uiautomationcore oleaut32 ole32 user32)
  if(MSVC)
    target_compile_options(hud_facility_area PRIVATE /W4 /permissive- /EHsc)
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS "facility|hud_facility_area|native/facility_owner.hpp|endfield::facility::facilityModule")
endif()

# The area owner over an in-memory host, the shared overlay owner, a hidden
# window and WARP (synthetic readings; no input injection).
if(WIN32 AND TARGET hud_facility_area AND BUILD_TESTING)
  add_executable(facility_owner_tests tests/facility_owner_tests.cpp)
  target_compile_definitions(facility_owner_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(facility_owner_tests PRIVATE hud_facility_area uiautomationcore oleaut32 ole32 user32)
  if(MSVC)
    target_compile_options(facility_owner_tests PRIVATE /W4 /permissive- /EHsc)
  endif()
  add_test(NAME facility_owner_contracts COMMAND facility_owner_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl")
  set_tests_properties(facility_owner_contracts PROPERTIES TIMEOUT 300)
endif()
