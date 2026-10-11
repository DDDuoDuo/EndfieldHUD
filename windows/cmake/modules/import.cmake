if(CMAKE_SCRIPT_MODE_FILE AND DEFINED TOOL)
  # ctest script mode: dry run, then commit into a fresh scratch root.
  file(REMOVE_RECURSE "${SCRATCH}")
  execute_process(COMMAND "${TOOL}" --fixture "${FIXTURE}" "${SCRATCH}" RESULT_VARIABLE dry OUTPUT_VARIABLE dryOut ERROR_VARIABLE dryError)
  if(NOT dry EQUAL 0 OR NOT dryOut MATCHES "\"committable\":true" OR NOT dryOut MATCHES "\"committed\":false" OR NOT dryOut MATCHES "\"verifiedBytes\":[1-9]"
     OR EXISTS "${SCRATCH}/EndfieldHUD")
    message(FATAL_ERROR "Dry run failed or wrote the destination: ${dry} ${dryOut} ${dryError}")
  endif()
  file(REMOVE_RECURSE "${SCRATCH}")
  execute_process(COMMAND "${TOOL}" --fixture "${FIXTURE}" "${SCRATCH}" --commit RESULT_VARIABLE run OUTPUT_VARIABLE runOut ERROR_VARIABLE runError)
  if(NOT run EQUAL 0 OR NOT runOut MATCHES "\"committed\":true" OR NOT runOut MATCHES "\"workRemoved\":true" OR NOT EXISTS "${SCRATCH}/EndfieldHUD/Migration/import.json")
    message(FATAL_ERROR "Commit run failed: ${run} ${runOut} ${runError}")
  endif()
  # The read-only work-folder listing: nothing is left behind after a commit.
  execute_process(COMMAND "${TOOL}" --work "${SCRATCH}/EndfieldHUD" RESULT_VARIABLE work OUTPUT_VARIABLE workOut ERROR_VARIABLE workError)
  if(NOT work EQUAL 0 OR NOT workOut MATCHES "\"workFolders\":\\[\\]")
    message(FATAL_ERROR "Work listing failed: ${work} ${workOut} ${workError}")
  endif()
  file(REMOVE_RECURSE "${SCRATCH}")
  message(STATUS "PASS mac_import_tool dry run, commit and work listing on the synthetic golden export")
  return()
endif()

# Offline Mac -> Windows data import (WINDOWS-MIGRATION.md section 7).
# Portable codecs/orchestrator build everywhere; the Unicode/WIC wiring is
# gated exactly like the existing hud_archive/hud_shortcuts targets.

add_library(hud_mac_import_codec
  core/migration/mac_import_sha256.cpp
  core/migration/plist.cpp
  core/migration/mac_import_settings.cpp
  core/migration/mac_import_codecs.cpp)
target_include_directories(hud_mac_import_codec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_mac_import_codec PUBLIC hud_data)

add_library(hud_mac_import
  core/migration/mac_import_files.cpp
  core/migration/mac_import_relink.cpp
  core/migration/mac_import.cpp
  core/migration/mac_import_outcome.cpp
  core/migration/mac_import_work.cpp)
target_include_directories(hud_mac_import PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_mac_import PUBLIC hud_mac_import_codec hud_data hud_reader hud_calendar hud_event_log hud_map_store)
if(WIN32)
  target_link_libraries(hud_mac_import PRIVATE winsqlite3)
else()
  target_link_libraries(hud_mac_import PRIVATE SQLite::SQLite3)
endif()

add_library(hud_mac_import_service core/migration/mac_import_service.cpp)
target_include_directories(hud_mac_import_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_mac_import_service PUBLIC hud_mac_import hud_utility)

# The Windows-only "Import macOS data…" flow and its card (portable model:
# five-language content, confirmation-style card state and artwork).
add_library(hud_mac_import_flow app/areas/mac_import_flow.cpp app/areas/mac_import_card.cpp)
target_include_directories(hud_mac_import_flow PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_mac_import_flow PUBLIC hud_mac_import_service hud_settings hud_localization)

set(EHUD_MAC_IMPORT_TARGETS hud_mac_import_codec hud_mac_import hud_mac_import_service hud_mac_import_flow)
if(TARGET hud_archive AND TARGET hud_shortcuts AND TARGET hud_calendar_civil)
  add_library(hud_mac_import_native core/migration/mac_import_native.cpp)
  target_include_directories(hud_mac_import_native PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(hud_mac_import_native PUBLIC hud_mac_import hud_archive hud_shortcuts hud_calendar_civil)
  if(WIN32)
    target_link_libraries(hud_mac_import_native PRIVATE windowscodecs ole32)
  endif()
  list(APPEND EHUD_MAC_IMPORT_TARGETS hud_mac_import_native)
  # Dry-run-by-default acceptance tool (stages, reports, discards).
  add_executable(mac_import_tool tools/mac_import_tool.cpp)
  target_link_libraries(mac_import_tool PRIVATE hud_mac_import_native)
  list(APPEND EHUD_MAC_IMPORT_TARGETS mac_import_tool)
endif()

# The macImport module area (app/module_owner.hpp, "Adding a module area"):
# the import card above the HUD, opened from the Settings System row through
# the area service app/areas/mac_import_handoff.hpp. Production commits through
# the host's restart handoff; hidden coverage imports the Mac-generated golden
# export into the area's own synthetic destination.
# UI Automation parts of the import card (a dialog under the owner's group of
# the HUD window's root; app/hud_automation.hpp).
if(WIN32)
  add_library(hud_mac_import_automation native/mac_import_automation.cpp)
  target_include_directories(hud_mac_import_automation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(hud_mac_import_automation PUBLIC hud_scene PRIVATE uiautomationcore oleaut32 ole32)
  list(APPEND EHUD_MAC_IMPORT_TARGETS hud_mac_import_automation)
endif()
if(WIN32 AND TARGET hud_mac_import_native AND TARGET hud_module_registry)
  add_library(hud_mac_import_area STATIC app/areas/mac_import_module.cpp)
  target_include_directories(hud_mac_import_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(hud_mac_import_area PUBLIC hud_module_registry hud_utility hud_mac_import_flow hud_mac_import_native hud_mac_import_automation
    PRIVATE user32 uiautomationcore oleaut32 ole32)
  list(APPEND EHUD_MAC_IMPORT_TARGETS hud_mac_import_area)
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "macImport|hud_mac_import_area|app/areas/mac_import_module.hpp|endfield::areas::macImportModule")
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREA_FIXTURES
    "macImport.goldenExport|${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mac_import_golden_export.json")
endif()

foreach(ehud_import_target IN LISTS EHUD_MAC_IMPORT_TARGETS)
  if(MSVC)
    target_compile_options(${ehud_import_target} PRIVATE /W4 /permissive- /EHsc)
    target_compile_definitions(${ehud_import_target} PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  else()
    target_compile_options(${ehud_import_target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()

if(BUILD_TESTING)
  set(EHUD_MAC_IMPORT_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)
  add_executable(plist_codec_tests tests/plist_codec_tests.cpp)
  target_link_libraries(plist_codec_tests PRIVATE hud_mac_import_codec)
  add_test(NAME plist_codec_contracts COMMAND plist_codec_tests ${EHUD_MAC_IMPORT_FIXTURES}/plist_mac_import_source.json)
  add_executable(mac_import_settings_tests tests/mac_import_settings_tests.cpp)
  target_link_libraries(mac_import_settings_tests PRIVATE hud_mac_import_codec hud_settings)
  add_test(NAME mac_import_settings_contracts COMMAND mac_import_settings_tests ${EHUD_MAC_IMPORT_FIXTURES}/mac_import_settings_source.json)
  add_executable(mac_import_tests tests/mac_import_tests.cpp)
  target_link_libraries(mac_import_tests PRIVATE hud_mac_import hud_mac_import_service)
  if(WIN32)
    target_compile_definitions(mac_import_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(mac_import_tests PRIVATE winsqlite3)
  else()
    target_link_libraries(mac_import_tests PRIVATE SQLite::SQLite3)
  endif()
  add_test(NAME mac_import_contracts COMMAND mac_import_tests ${EHUD_MAC_IMPORT_FIXTURES}/map-store-source.json)
  # Private work folders: marker and lock, activation journal, the startup
  # sweep and the pre-open recovery of an interrupted activation.
  add_executable(mac_import_work_tests tests/mac_import_work_tests.cpp)
  target_link_libraries(mac_import_work_tests PRIVATE hud_mac_import hud_mac_import_service)
  if(WIN32)
    target_compile_definitions(mac_import_work_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(mac_import_work_tests PRIVATE winsqlite3)
  else()
    target_link_libraries(mac_import_work_tests PRIVATE SQLite::SQLite3)
  endif()
  if(MSVC)
    target_compile_options(mac_import_work_tests PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(mac_import_work_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME mac_import_work_contracts COMMAND mac_import_work_tests)
  # The import flow, card and restart-handoff outcome over synthetic exports.
  add_executable(mac_import_flow_tests tests/mac_import_flow_tests.cpp)
  target_link_libraries(mac_import_flow_tests PRIVATE hud_mac_import_flow hud_mac_import_service)
  if(WIN32)
    target_compile_definitions(mac_import_flow_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(mac_import_flow_tests PRIVATE winsqlite3)
  else()
    target_link_libraries(mac_import_flow_tests PRIVATE SQLite::SQLite3)
  endif()
  if(MSVC)
    target_compile_options(mac_import_flow_tests PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(mac_import_flow_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME mac_import_flow_contracts COMMAND mac_import_flow_tests)
  if(TARGET hud_mac_import_automation)
    add_executable(mac_import_automation_tests tests/mac_import_automation_tests.cpp)
    target_link_libraries(mac_import_automation_tests PRIVATE hud_mac_import_automation uiautomationcore oleaut32 ole32)
    target_compile_definitions(mac_import_automation_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
      target_compile_options(mac_import_automation_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME mac_import_automation_contracts COMMAND mac_import_automation_tests)
  endif()
  # Imported reminders through the unchanged Windows reminder plan/reconcile
  # (in-memory provider; the real notification center is never used).
  if(TARGET hud_calendar_notifications)
    add_executable(mac_import_reconcile_tests tests/mac_import_reconcile_tests.cpp)
    target_link_libraries(mac_import_reconcile_tests PRIVATE hud_mac_import hud_mac_import_service hud_calendar_notifications)
    if(WIN32)
      target_compile_definitions(mac_import_reconcile_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
      target_link_libraries(mac_import_reconcile_tests PRIVATE winsqlite3)
    else()
      target_link_libraries(mac_import_reconcile_tests PRIVATE SQLite::SQLite3)
    endif()
    add_test(NAME mac_import_reconcile_contracts COMMAND mac_import_reconcile_tests)
  endif()
  # The Mac-side exporter's synthetic self-test (POSIX; it never reads real data).
  if(NOT WIN32)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      add_test(NAME mac_import_exporter_contracts COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/mac_import_exporter.py --self-test)
    endif()
  endif()
  if(TARGET hud_mac_import_native)
    add_executable(mac_import_native_tests tests/mac_import_native_tests.cpp)
    target_link_libraries(mac_import_native_tests PRIVATE hud_mac_import_native hud_mac_import_service)
    if(WIN32)
      target_compile_definitions(mac_import_native_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
      target_link_libraries(mac_import_native_tests PRIVATE winsqlite3 windowscodecs ole32)
    else()
      target_link_libraries(mac_import_native_tests PRIVATE SQLite::SQLite3)
    endif()
    add_test(NAME mac_import_native_contracts COMMAND mac_import_native_tests ${EHUD_MAC_IMPORT_FIXTURES}/mac_import_golden_export.json)
    # The acceptance tool's dry run over the Mac-generated golden export.
    add_test(NAME mac_import_tool_contracts COMMAND ${CMAKE_COMMAND}
      -DTOOL=$<TARGET_FILE:mac_import_tool> -DFIXTURE=${EHUD_MAC_IMPORT_FIXTURES}/mac_import_golden_export.json
      -DSCRATCH=${CMAKE_CURRENT_BINARY_DIR}/mac_import_tool_scratch -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/import.cmake)
  endif()
endif()
