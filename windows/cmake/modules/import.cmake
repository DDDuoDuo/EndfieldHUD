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
  file(REMOVE_RECURSE "${SCRATCH}")
  message(STATUS "PASS mac_import_tool dry run and commit on the synthetic golden export")
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
  core/migration/mac_import.cpp)
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

set(EHUD_MAC_IMPORT_TARGETS hud_mac_import_codec hud_mac_import hud_mac_import_service)
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
