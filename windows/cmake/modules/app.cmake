# Production application owner (host area): EndfieldHUD.exe, its lifecycle
# policies, data root, resource locator, single-instance activation and the
# shared module-owner registry. Included after every target in CMakeLists.txt.

# --- Portable lifecycle policy, arguments and data root ------------------------
add_library(hud_system_overlay core/system_overlay_state.cpp)
target_include_directories(hud_system_overlay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_system_overlay PUBLIC hud_motion)
add_library(hud_application_core core/application_arguments.cpp core/application_event_recorder.cpp core/data/application_root.cpp)
target_include_directories(hud_application_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_application_core PUBLIC hud_system_overlay hud_data hud_event_log hud_work_mode)
if(WIN32)
  target_compile_definitions(hud_application_core PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_application_core PRIVATE advapi32 shell32 ole32)
endif()
add_library(hud_quit_confirmation_state app/quit_confirmation_state.cpp)
target_include_directories(hud_quit_confirmation_state PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_quit_confirmation_state PUBLIC hud_settings hud_localization)
if(BUILD_TESTING)
  add_executable(system_overlay_state_tests tests/system_overlay_state_tests.cpp)
  target_link_libraries(system_overlay_state_tests PRIVATE hud_system_overlay hud_data)
  add_test(NAME system_overlay_state_contracts COMMAND system_overlay_state_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/system_overlay_state_source.json")
  add_executable(application_root_tests tests/application_root_tests.cpp)
  target_link_libraries(application_root_tests PRIVATE hud_application_core)
  if(WIN32)
    target_compile_definitions(application_root_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(application_root_tests PRIVATE advapi32)
  endif()
  add_test(NAME application_root_contracts COMMAND application_root_tests)
  add_executable(application_event_recorder_tests tests/application_event_recorder_tests.cpp)
  target_link_libraries(application_event_recorder_tests PRIVATE hud_application_core)
  add_test(NAME application_event_recorder_contracts COMMAND application_event_recorder_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/application_event_recorder_source.json")
  add_executable(application_quit_tests tests/application_quit_tests.cpp)
  target_link_libraries(application_quit_tests PRIVATE hud_quit_confirmation_state hud_data)
  add_test(NAME application_quit_contracts COMMAND application_quit_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/application_quit_source.json")
endif()

# --- Application owner (Windows) -----------------------------------------------
if(WIN32)
  set(EHUD_APP_DEFINITIONS _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  # Module-owner registry and the confirmation card are independent of the
  # concrete module set, so other areas and tests can link them alone.
  add_library(hud_module_registry app/module_registry.cpp app/quit_confirmation.cpp)
  target_include_directories(hud_module_registry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_module_registry PRIVATE ${EHUD_APP_DEFINITIONS})
  target_link_libraries(hud_module_registry PUBLIC hud_quit_confirmation_state hud_native hud_overlay_host hud_settings hud_localization)
  # The single application owner. The six module owners that CMakeLists.txt
  # compiles directly into watch_session_preview are provided to the
  # production executable by hud_application_module_sources below.
  add_library(hud_application app/application.cpp app/application_events.cpp app/application_lifetime.cpp app/application_modules.cpp)
  target_include_directories(hud_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_application PRIVATE ${EHUD_APP_DEFINITIONS})
  target_link_libraries(hud_application PUBLIC hud_module_registry hud_system_overlay hud_application_core hud_single_instance
    hud_battery hud_settings_save hud_archive_preview hud_storage_preview hud_activity_preview hud_activity_provider
    hud_reader_preview hud_reader_import hud_projection_preview hud_shelf_preview hud_notes_preview
    hud_map_preview hud_map_store hud_map_assets hud_orbipom_presentation hud_orbipom_runtime
    hud_calendar_preview hud_calendar_notifications
    hud_clock hud_watch_session hud_watch_content hud_watch_runtime hud_overlay_host hud_services hud_backdrop
    hud_shelf_icons hud_shelf_transfer hud_localization windowsapp CoreMessaging dwmapi psapi advapi32 wtsapi32)
  add_library(hud_application_verification app/application_verification.cpp)
  target_compile_definitions(hud_application_verification PRIVATE ${EHUD_APP_DEFINITIONS})
  target_link_libraries(hud_application_verification PUBLIC hud_application)
  if(MSVC)
    foreach(target hud_module_registry hud_application hud_application_verification)
      target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /bigobj)
    endforeach()
  endif()
  # The development preview is now a thin harness over the same owner.
  target_link_libraries(watch_session_preview PRIVATE hud_application_verification hud_application)
endif()

# --- Packaged resources, single instance and EndfieldHUD.exe -------------------
add_library(hud_resource_locator core/resource_locator.cpp)
target_include_directories(hud_resource_locator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_resource_locator PUBLIC hud_packet hud_data)
# Package -> module asset roots; a damaged resource disables only its modules.
add_library(hud_application_package app/application_package.cpp)
target_include_directories(hud_application_package PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_application_package PUBLIC hud_resource_locator hud_motion)
if(BUILD_TESTING)
  add_executable(resource_locator_tests tests/resource_locator_tests.cpp)
  target_link_libraries(resource_locator_tests PRIVATE hud_resource_locator)
  add_test(NAME resource_locator_contracts COMMAND resource_locator_tests)
  add_executable(application_package_tests tests/application_package_tests.cpp)
  target_link_libraries(application_package_tests PRIVATE hud_application_package)
  add_test(NAME application_package_contracts COMMAND application_package_tests)
  add_executable(application_persistence_tests tests/application_persistence_tests.cpp)
  target_include_directories(application_persistence_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME application_persistence_contracts COMMAND application_persistence_tests)
endif()

# Runtime resources of EndfieldHUD.exe, staged into <exe dir>/resources with a
# SHA-256 manifest (app/stage_resources.cmake, core/resource_locator.hpp).
# Module areas wired into the production owner declare their own runtime
# inputs from their fragment, before or after this one:
#   ehud_app_resource(<id> file|directory <absolute source> <relative destination>)
# The id is what their owner resolves through core::ResourceLocator; an input
# that does not exist at build time is left out, and only its module is off.
function(ehud_app_resource id kind source destination)
  if(NOT kind MATCHES "^(file|directory)$" OR source STREQUAL "" OR destination STREQUAL "" OR destination MATCHES "(^/|/$|\\.\\.|\\\\|:|;|\\|)")
    message(FATAL_ERROR "ehud_app_resource(${id}): invalid kind, source or destination")
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_RESOURCE_SPECS "${id}|${kind}|${source}|${destination}")
endfunction()
# Written once every fragment has been processed (end of this directory).
function(ehud_app_write_resource_specs)
  get_property(specs GLOBAL PROPERTY EHUD_APP_RESOURCE_SPECS)
  list(JOIN specs "\n" text)
  file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/endfieldhud-resources.txt" CONTENT "${text}\n" @ONLY)
endfunction()
if(WIN32)
  cmake_language(DEFER CALL ehud_app_write_resource_specs)
endif()

# Runtime inputs exported on the Mac (runtime-compiled-animation,
# profile-outline-catalog.ehscene, notes-controls-assets, source-cursor.png and
# fixtures/watch-blur.json). Defaults to the folder that holds the map inputs.
set(EHUD_APP_INPUT_DIR "" CACHE PATH "Mac-exported EndfieldHUD runtime inputs for EndfieldHUD.exe staging")
set(ehud_app_inputs "${EHUD_APP_INPUT_DIR}")
if(NOT ehud_app_inputs AND EHUD_MAP_RESOURCE_DIR)
  get_filename_component(ehud_inputs_parent "${EHUD_MAP_RESOURCE_DIR}" DIRECTORY)
  if(EXISTS "${ehud_inputs_parent}/runtime-compiled-animation/runtime-input.json")
    set(ehud_app_inputs "${ehud_inputs_parent}")
  endif()
endif()
set(ehud_watch_blur "${CMAKE_CURRENT_SOURCE_DIR}/../Resources/WatchSource/Scene/watch-blur.json")
if(NOT EXISTS "${ehud_watch_blur}" AND ehud_app_inputs)
  set(ehud_watch_blur "${ehud_app_inputs}/fixtures/watch-blur.json")
endif()

if(WIN32)
  add_library(hud_single_instance app/single_instance.cpp app/application_login_item.cpp)
  target_include_directories(hud_single_instance PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_single_instance PRIVATE ${EHUD_APP_DEFINITIONS})
  target_link_libraries(hud_single_instance PUBLIC hud_application_core hud_localization user32 advapi32)
  # The module owners CMakeLists.txt compiles directly into the preview.
  add_library(hud_application_module_sources tools/clipboard_preview.cpp tools/volume_preview.cpp tools/event_log_preview.cpp
    tools/work_mode_preview.cpp tools/battery_preview.cpp tools/settings_preview.cpp)
  target_include_directories(hud_application_module_sources PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_application_module_sources PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
  target_link_libraries(hud_application_module_sources PUBLIC hud_native hud_notes_preview hud_battery hud_settings_save hud_overlay_host)

  # Version from the ported source authority; flagged prerelease/private.
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/source-authority.json" ehud_authority)
  string(JSON EHUD_APP_RELEASE GET "${ehud_authority}" release)
  string(JSON EHUD_APP_BUILD GET "${ehud_authority}" build)
  string(JSON EHUD_APP_COMMIT GET "${ehud_authority}" commit)
  string(REGEX MATCH "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)$" ehud_version_match "${EHUD_APP_RELEASE}")
  if(NOT ehud_version_match)
    message(FATAL_ERROR "source-authority.json release is not a version: ${EHUD_APP_RELEASE}")
  endif()
  set(EHUD_APP_VERSION_COMMAS "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},${EHUD_APP_BUILD}")
  set(EHUD_APP_VERSION_DOTS "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}.${EHUD_APP_BUILD}")
  set(EHUD_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/app/EndfieldHUD.ico")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/app/EndfieldHUD.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/EndfieldHUD.rc" @ONLY)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/app/EndfieldHUD.manifest.in" "${CMAKE_CURRENT_BINARY_DIR}/EndfieldHUD.manifest" @ONLY)

  add_executable(EndfieldHUD WIN32 app/main.cpp "${CMAKE_CURRENT_BINARY_DIR}/EndfieldHUD.rc" "${CMAKE_CURRENT_BINARY_DIR}/EndfieldHUD.manifest")
  target_compile_definitions(EndfieldHUD PRIVATE ${EHUD_APP_DEFINITIONS})
  target_link_libraries(EndfieldHUD PRIVATE hud_application hud_application_module_sources hud_single_instance hud_application_package shell32)
  if(MSVC)
    target_compile_options(EndfieldHUD PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(hud_single_instance PRIVATE /W4 /permissive- /EHsc)
  endif()
  add_dependencies(EndfieldHUD hud_private_fonts)

  # Runtime package next to the executable: resources/ + resources.json, one
  # resource per module asset folder (app/application_package.hpp). Map and
  # Minigame inputs are external build inputs.
  if(ehud_app_inputs)
    ehud_app_resource(shell.runtimeInput directory "${ehud_app_inputs}/runtime-compiled-animation" "shell/runtime-input")
    ehud_app_resource(shell.catalog file "${ehud_app_inputs}/profile-outline-catalog.ehscene" "shell/catalog.ehscene")
    ehud_app_resource(shell.cursor file "${ehud_app_inputs}/source-cursor.png" "shell/cursor.png")
    ehud_app_resource(notes.controls directory "${ehud_app_inputs}/notes-controls-assets" "notes-controls")
  endif()
  ehud_app_resource(shell.shader file "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl" "shell/hud.hlsl")
  ehud_app_resource(shell.watchBlur file "${ehud_watch_blur}" "shell/watch-blur.json")
  foreach(ehud_common IN ITEMS notesFormat:notes-format shelf:shelf clipboard:clipboard applicationIcons:application-icons
          watchAppearance:watch-appearance archive:archive storage:storage activity:activity mapPlayer:map-player)
    string(REPLACE ":" ";" ehud_common "${ehud_common}")
    list(GET ehud_common 0 ehud_common_id)
    list(GET ehud_common 1 ehud_common_folder)
    ehud_app_resource(common.${ehud_common_id} directory "${CMAKE_CURRENT_SOURCE_DIR}/resources/${ehud_common_folder}" "common/${ehud_common_folder}")
  endforeach()
  if(EHUD_MAP_RESOURCE_DIR)
    ehud_app_resource(map.geography directory "${EHUD_MAP_RESOURCE_DIR}" "map")
  endif()
  if(EHUD_ORBIPOM_RESOURCE_DIR)
    ehud_app_resource(orbipom directory "${EHUD_ORBIPOM_RESOURCE_DIR}/OrbiPom" "orbipom/OrbiPom")
  endif()
  # Staged after every relink and on every full build, so an edited input
  # never leaves a stale package; unchanged files are not rewritten.
  set(ehud_stage_command "${CMAKE_COMMAND}" "-DSTAGE_DIR=$<TARGET_FILE_DIR:EndfieldHUD>/resources" "-DSOURCE_COMMIT=${EHUD_APP_COMMIT}"
      "-DRESOURCE_SPECS_FILE=${CMAKE_CURRENT_BINARY_DIR}/endfieldhud-resources.txt" -P "${CMAKE_CURRENT_SOURCE_DIR}/app/stage_resources.cmake")
  add_custom_command(TARGET EndfieldHUD POST_BUILD COMMAND ${ehud_stage_command} VERBATIM)
  add_custom_target(endfieldhud_resources ALL COMMAND ${ehud_stage_command} COMMENT "Staging EndfieldHUD runtime resources" VERBATIM)
  add_dependencies(endfieldhud_resources EndfieldHUD)

  # Runtime-only install: executable, private fonts with their OFL texts, and
  # the verified resource package (no tests, fixtures, symbols or user data).
  install(TARGETS EndfieldHUD RUNTIME DESTINATION . COMPONENT EndfieldHUD)
  install(DIRECTORY "$<TARGET_FILE_DIR:EndfieldHUD>/resources/" DESTINATION resources COMPONENT EndfieldHUD)
  install(FILES ${hud_font_sources} DESTINATION fonts COMPONENT EndfieldHUD)

  if(BUILD_TESTING)
    add_executable(application_registry_tests tests/application_registry_tests.cpp)
    target_compile_definitions(application_registry_tests PRIVATE ${EHUD_APP_DEFINITIONS})
    target_link_libraries(application_registry_tests PRIVATE hud_module_registry)
    add_test(NAME application_registry_contracts COMMAND application_registry_tests)
    add_executable(single_instance_tests tests/single_instance_tests.cpp)
    target_compile_definitions(single_instance_tests PRIVATE ${EHUD_APP_DEFINITIONS})
    target_link_libraries(single_instance_tests PRIVATE hud_single_instance ole32)
    add_test(NAME single_instance_contracts COMMAND single_instance_tests)
    add_executable(application_login_item_tests tests/application_login_item_tests.cpp)
    target_compile_definitions(application_login_item_tests PRIVATE ${EHUD_APP_DEFINITIONS})
    target_link_libraries(application_login_item_tests PRIVATE hud_single_instance ole32)
    add_test(NAME application_login_item_contracts COMMAND application_login_item_tests)
    # The built executable (read only, never run): GUI subsystem, icon group
    # identical to the Mac-rendered ICO, VERSIONINFO and manifest.
    add_executable(application_executable_tests tests/application_executable_tests.cpp)
    target_compile_definitions(application_executable_tests PRIVATE ${EHUD_APP_DEFINITIONS})
    target_link_libraries(application_executable_tests PRIVATE hud_resource_locator version)
    add_dependencies(application_executable_tests EndfieldHUD)
    add_test(NAME application_executable_contracts COMMAND application_executable_tests "$<TARGET_FILE:EndfieldHUD>" "${CMAKE_CURRENT_SOURCE_DIR}")
    if(ehud_app_inputs AND EXISTS "${ehud_app_inputs}/notes-controls-assets" AND EXISTS "${ehud_watch_blur}")
      # The production package staged from the configured inputs verifies.
      add_test(NAME endfieldhud_package_contracts COMMAND EndfieldHUD --verify-package)
      # Hidden module coverage plus the production lifecycle/quit flow.
      add_test(NAME application_owner_coverage COMMAND "${CMAKE_COMMAND}" "-DPREVIEW=$<TARGET_FILE:watch_session_preview>"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/owner-coverage-work" "-DINPUTS=${ehud_app_inputs}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DMAP=${EHUD_MAP_RESOURCE_DIR}" "-DORBIPOM=${EHUD_ORBIPOM_RESOURCE_DIR}" -DCHECKS=19 -P "${CMAKE_CURRENT_SOURCE_DIR}/app/run_owner_coverage.cmake")
      set_tests_properties(application_owner_coverage PROPERTIES TIMEOUT 600)
      # Same owner ending through WM_QUERYENDSESSION/WM_ENDSESSION instead of Quit.
      add_test(NAME application_session_end_coverage COMMAND "${CMAKE_COMMAND}" "-DPREVIEW=$<TARGET_FILE:watch_session_preview>"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/session-end-coverage-work" "-DINPUTS=${ehud_app_inputs}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DMAP=${EHUD_MAP_RESOURCE_DIR}" "-DORBIPOM=${EHUD_ORBIPOM_RESOURCE_DIR}" -DCHECKS=15 -DEXTRA=--session-end-coverage
        -P "${CMAKE_CURRENT_SOURCE_DIR}/app/run_owner_coverage.cmake")
      set_tests_properties(application_session_end_coverage PROPERTIES TIMEOUT 600)
      add_test(NAME endfieldhud_install_contracts COMMAND "${CMAKE_COMMAND}" "-DBUILD=${CMAKE_BINARY_DIR}" "-DCONFIG=$<CONFIG>"
        "-DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/endfieldhud-install-check" -P "${CMAKE_CURRENT_SOURCE_DIR}/app/run_install_check.cmake")
    endif()
  endif()
endif()
