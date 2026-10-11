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
# Settings > Display > Center logo (Mac HUDCenterLogo/HUDCenterLogoStore/
# HUDCenterLogoPresentation): portable artwork, Windows WIC decode/import.
add_library(hud_display_center_logo modules/display_center_logo.cpp)
target_include_directories(hud_display_center_logo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_display_center_logo PUBLIC hud_localization)
if(WIN32)
  target_sources(hud_display_center_logo PRIVATE native/display_center_logo.cpp)
  target_compile_definitions(hud_display_center_logo PRIVATE _WIN32_WINNT=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_display_center_logo PUBLIC hud_data hud_packet PRIVATE windowscodecs ole32)
endif()
if(BUILD_TESTING)
  # Oracle: tools/display_center_logo_reference.sh (unchanged Mac sources).
  add_executable(display_center_logo_tests tests/display_center_logo_tests.cpp)
  target_link_libraries(display_center_logo_tests PRIVATE hud_display_center_logo hud_data)
  if(WIN32)
    target_compile_definitions(display_center_logo_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    add_test(NAME display_center_logo_contracts COMMAND display_center_logo_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/display-center-logo-source.json" "${CMAKE_CURRENT_SOURCE_DIR}/resources/center-logos")
  else()
    add_test(NAME display_center_logo_contracts COMMAND display_center_logo_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/display-center-logo-source.json")
  endif()
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
    # Isolated --data-root single-instance names (app/single_instance.hpp).
    target_link_libraries(application_root_tests PRIVATE advapi32 hud_single_instance)
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
  # Data layer regressions (core/data): Json is never relocated with memcpy
  # (libc++ 19), and ProfileStore normalizes a loaded record like the Mac
  # store (Mac oracle: tools/profile_reference.sh decode cases).
  add_executable(json_tests tests/json_tests.cpp)
  target_link_libraries(json_tests PRIVATE hud_data)
  add_test(NAME json_contracts COMMAND json_tests)
  add_executable(data_store_tests tests/data_store_tests.cpp)
  target_link_libraries(data_store_tests PRIVATE hud_data)
  add_test(NAME data_store_contracts COMMAND data_store_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/profile-state-source.json")
  foreach(target json_tests data_store_tests)
    if(MSVC)
      target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    else()
      target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endforeach()
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
  add_library(hud_application app/application.cpp app/application_events.cpp app/application_lifetime.cpp app/application_modules.cpp app/application_display.cpp app/hud_automation.cpp)
  target_include_directories(hud_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_application PRIVATE ${EHUD_APP_DEFINITIONS})
  target_link_libraries(hud_application PUBLIC hud_module_registry hud_system_overlay hud_application_core hud_single_instance
    hud_battery hud_settings_save hud_archive_preview hud_storage_preview hud_activity_preview hud_activity_provider
    hud_reader_preview hud_reader_import hud_projection_preview hud_shelf_preview hud_notes_preview
    hud_map_preview hud_map_store hud_map_assets hud_orbipom_presentation hud_orbipom_runtime
    hud_calendar_preview hud_calendar_notifications
    hud_clock hud_watch_session hud_watch_content hud_watch_runtime hud_overlay_host hud_services hud_backdrop
    hud_shelf_icons hud_shelf_transfer hud_localization hud_display_center_logo windowsapp CoreMessaging dwmapi psapi advapi32 wtsapi32 uiautomationcore oleaut32)
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
  target_link_libraries(hud_single_instance PUBLIC hud_application_core hud_localization user32 advapi32 bcrypt)
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
  # Settings > About (HUDAboutInformation): "<version> (<build>)".
  target_compile_definitions(hud_application PRIVATE "EHUD_APP_VERSION_NAME=\"${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}\"" "EHUD_APP_BUILD_NUMBER=\"${EHUD_APP_BUILD}\"")
  set(EHUD_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/app/EndfieldHUD.ico")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/app/EndfieldHUD.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/EndfieldHUD.rc" @ONLY)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/app/EndfieldHUD.manifest.in" "${CMAKE_CURRENT_BINARY_DIR}/EndfieldHUD.manifest" @ONLY)

  add_executable(EndfieldHUD WIN32 app/main.cpp "${CMAKE_CURRENT_BINARY_DIR}/EndfieldHUD.rc" "${CMAKE_CURRENT_BINARY_DIR}/EndfieldHUD.manifest")
  target_compile_definitions(EndfieldHUD PRIVATE ${EHUD_APP_DEFINITIONS})
  target_link_libraries(EndfieldHUD PRIVATE hud_application hud_application_module_sources hud_single_instance hud_application_package shell32)
  # main.cpp rolls an interrupted Mac import activation back before the root
  # opens (core/migration/mac_import_work.hpp; target from cmake/modules/import.cmake).
  target_link_libraries(EndfieldHUD PRIVATE hud_mac_import)
  if(MSVC)
    target_compile_options(EndfieldHUD PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(hud_single_instance PRIVATE /W4 /permissive- /EHsc)
    # Separate developer symbols (stage_release.cmake archives the PDB) with
    # unchanged code generation: /DEBUG would otherwise turn REF/ICF off.
    target_link_options(EndfieldHUD PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL /OPT:REF /OPT:ICF>)
  endif()
  add_dependencies(EndfieldHUD hud_private_fonts)
  # The shared audio worker, five-language Volume captions (audio area).
  target_link_libraries(hud_application PUBLIC hud_audio_runtime hud_volume_strings)

  # --- Module areas (app/module_owner.hpp, "Adding a module area") ----------
  # Every area fragment registers, from any position in the sorted fragment
  # list (a GLOBAL property needs no helper defined first):
  #   set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
  #     "<name>|<library target>|<header relative to windows/>|<qualified factory>[|coverage]")
  # and, for hidden coverage only, fixtures resolved by context.fixture(id):
  #   set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREA_FIXTURES "<id>|<absolute path>")
  # Once every fragment is read, ehud_app_write_areas() generates the two
  # translation units behind app/application_areas.hpp and links the listed
  # libraries: production areas into EndfieldHUD.exe, every area (including
  # "|coverage" test areas) plus the fixture table into watch_session_preview.
  set(ehud_area_dir "${CMAKE_CURRENT_BINARY_DIR}/application-areas")
  add_library(hud_application_areas STATIC "${ehud_area_dir}/production_areas.cpp")
  add_library(hud_application_coverage_areas STATIC "${ehud_area_dir}/coverage_areas.cpp")
  foreach(target hud_application_areas hud_application_coverage_areas)
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${target} PRIVATE ${EHUD_APP_DEFINITIONS})
    target_link_libraries(${target} PUBLIC hud_module_registry)
    if(MSVC)
      target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    endif()
  endforeach()
  target_link_libraries(EndfieldHUD PRIVATE hud_application_areas)
  target_link_libraries(watch_session_preview PRIVATE hud_application_coverage_areas hud_resource_locator)

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
          watchAppearance:watch-appearance archive:archive storage:storage activity:activity mapPlayer:map-player centerLogos:center-logos)
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
    if(ehud_app_inputs AND EXISTS "${ehud_app_inputs}/notes-controls-assets")
      # Production Notes/File Shelf roots: the real owners on temporary roots
      # that already hold an earlier run's records, across a restart.
      add_executable(application_data_root_tests tests/application_data_root_tests.cpp)
      target_compile_definitions(application_data_root_tests PRIVATE ${EHUD_APP_DEFINITIONS})
      target_link_libraries(application_data_root_tests PRIVATE hud_notes_preview hud_shelf_preview hud_overlay_host ole32)
      if(MSVC)
        target_compile_options(application_data_root_tests PRIVATE /W4 /permissive- /EHsc)
      endif()
      add_test(NAME application_data_root_contracts COMMAND application_data_root_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl"
        "${ehud_app_inputs}/notes-controls-assets" 9d2fcf0eead9eeeab1be4fdbb0e940a0e1e3eedf840ab9e4c63499e40ae70c00 ca04f142185c7de40acd8523bdb563195d90a1d1
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/shelf" "${CMAKE_CURRENT_SOURCE_DIR}/resources/shelf/reveal-mask.bin")
      set_tests_properties(application_data_root_contracts PROPERTIES TIMEOUT 300)
    endif()
    if(ehud_app_inputs AND EXISTS "${ehud_app_inputs}/notes-controls-assets" AND EXISTS "${ehud_watch_blur}")
      # The production package staged from the configured inputs verifies.
      add_test(NAME endfieldhud_package_contracts COMMAND EndfieldHUD --verify-package)
      # Hidden module coverage plus the production lifecycle/quit flow.
      add_test(NAME application_owner_coverage COMMAND "${CMAKE_COMMAND}" "-DPREVIEW=$<TARGET_FILE:watch_session_preview>"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/owner-coverage-work" "-DINPUTS=${ehud_app_inputs}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DMAP=${EHUD_MAP_RESOURCE_DIR}" "-DORBIPOM=${EHUD_ORBIPOM_RESOURCE_DIR}" -DCHECKS=19 -DHOST_CHECKS=78 "-DPACKAGE=$<TARGET_FILE_DIR:EndfieldHUD>/resources"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/app/run_owner_coverage.cmake")
      set_tests_properties(application_owner_coverage PROPERTIES TIMEOUT 600)
      # Same owner ending through WM_QUERYENDSESSION/WM_ENDSESSION instead of Quit.
      add_test(NAME application_session_end_coverage COMMAND "${CMAKE_COMMAND}" "-DPREVIEW=$<TARGET_FILE:watch_session_preview>"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/session-end-coverage-work" "-DINPUTS=${ehud_app_inputs}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DMAP=${EHUD_MAP_RESOURCE_DIR}" "-DORBIPOM=${EHUD_ORBIPOM_RESOURCE_DIR}" -DCHECKS=15 -DHOST_CHECKS=75 -DEXTRA=--session-end-coverage
        "-DPACKAGE=$<TARGET_FILE_DIR:EndfieldHUD>/resources" -P "${CMAKE_CURRENT_SOURCE_DIR}/app/run_owner_coverage.cmake")
      set_tests_properties(application_session_end_coverage PROPERTIES TIMEOUT 600)
      add_test(NAME endfieldhud_install_contracts COMMAND "${CMAKE_COMMAND}" "-DBUILD=${CMAKE_BINARY_DIR}" "-DCONFIG=$<CONFIG>"
        "-DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/endfieldhud-install-check" -P "${CMAKE_CURRENT_SOURCE_DIR}/app/run_install_check.cmake")
    endif()
  endif()
endif()

# Generates the module-area translation units (see "Module areas" above).
function(ehud_app_write_areas)
  get_property(entries GLOBAL PROPERTY EHUD_APP_AREAS)
  get_property(fixtures GLOBAL PROPERTY EHUD_APP_AREA_FIXTURES)
  set(names "")
  set(production_includes "")
  set(coverage_includes "")
  set(production_rows "")
  set(coverage_rows "")
  set(production_libraries "")
  set(coverage_libraries "")
  set(production_count 0)
  set(coverage_count 0)
  foreach(entry IN LISTS entries)
    string(REPLACE "|" ";" fields "${entry}")
    list(LENGTH fields count)
    if(count LESS 4 OR count GREATER 5)
      message(FATAL_ERROR "EHUD_APP_AREAS entry '${entry}' is not <name>|<library>|<header>|<factory>[|coverage]")
    endif()
    list(GET fields 0 name)
    list(GET fields 1 library)
    list(GET fields 2 header)
    list(GET fields 3 factory)
    set(scope production)
    if(count EQUAL 5)
      list(GET fields 4 scope)
    endif()
    if(NOT name MATCHES "^[a-z][A-Za-z0-9]*$" OR name IN_LIST names)
      message(FATAL_ERROR "EHUD_APP_AREAS: area name '${name}' must be unique camelCase ([a-z][A-Za-z0-9]*)")
    endif()
    if(NOT TARGET "${library}")
      message(FATAL_ERROR "EHUD_APP_AREAS ${name}: library target '${library}' does not exist")
    endif()
    if(NOT header MATCHES "^[A-Za-z0-9_][A-Za-z0-9_./-]*\\.hpp$" OR header MATCHES "\\.\\." OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
      message(FATAL_ERROR "EHUD_APP_AREAS ${name}: header '${header}' must be an existing .hpp relative to windows/")
    endif()
    if(NOT factory MATCHES "^(::)?[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")
      message(FATAL_ERROR "EHUD_APP_AREAS ${name}: factory '${factory}' must be a qualified function name")
    endif()
    if(NOT scope MATCHES "^(production|coverage)$")
      message(FATAL_ERROR "EHUD_APP_AREAS ${name}: scope '${scope}' must be production or coverage")
    endif()
    list(APPEND names "${name}")
    string(APPEND coverage_includes "#include \"${header}\"\n")
    list(APPEND coverage_libraries "${library}")
    math(EXPR coverage_count "${coverage_count} + 1")
    if(scope STREQUAL "production")
      string(APPEND production_includes "#include \"${header}\"\n")
      string(APPEND production_rows "    ApplicationArea{\"${name}\", &${factory}, false},\n")
      string(APPEND coverage_rows "    ApplicationArea{\"${name}\", &${factory}, false},\n")
      list(APPEND production_libraries "${library}")
      math(EXPR production_count "${production_count} + 1")
    else()
      string(APPEND coverage_rows "    ApplicationArea{\"${name}\", &${factory}, true},\n")
    endif()
  endforeach()
  set(fixture_rows "")
  set(fixture_ids "")
  set(fixture_count 0)
  foreach(entry IN LISTS fixtures)
    string(REPLACE "|" ";" fields "${entry}")
    list(LENGTH fields count)
    if(NOT count EQUAL 2)
      message(FATAL_ERROR "EHUD_APP_AREA_FIXTURES entry '${entry}' is not <id>|<absolute path>")
    endif()
    list(GET fields 0 id)
    list(GET fields 1 path)
    if(NOT id MATCHES "^[A-Za-z0-9_.-]+$" OR id IN_LIST fixture_ids OR NOT IS_ABSOLUTE "${path}" OR NOT EXISTS "${path}" OR path MATCHES "\\)ehud\"")
      message(FATAL_ERROR "EHUD_APP_AREA_FIXTURES: '${id}' needs a unique id and an existing absolute path")
    endif()
    list(APPEND fixture_ids "${id}")
    string(APPEND fixture_rows "    std::pair<std::string_view, std::u8string_view>{\"${id}\", u8R\"ehud(${path})ehud\"},\n")
    math(EXPR fixture_count "${fixture_count} + 1")
  endforeach()
  # Zero-length std::array initializers stay empty braces.
  foreach(kind production coverage fixture)
    if(${kind}_count EQUAL 0)
      set(${kind}_init "{}")
    else()
      set(${kind}_init "{{\n${${kind}_rows}}}")
    endif()
  endforeach()
  set(header_text "// Generated by cmake/modules/app.cmake from the EHUD_APP_AREAS registrations. Do not edit.\n#include \"app/application_areas.hpp\"\n#include <array>\n#include <string_view>\n#include <utility>\n")
  set(production_text "${header_text}${production_includes}\nnamespace endfield::app {\nnamespace {\nconst std::array<ApplicationArea, ${production_count}> areas${production_init};\n}\nstd::span<const ApplicationArea> registeredApplicationAreas() noexcept { return areas; }\nstd::filesystem::path registeredCoverageFixture(std::string_view) { return {}; }\n} // namespace endfield::app\n")
  set(coverage_text "${header_text}${coverage_includes}\nnamespace endfield::app {\nnamespace {\nconst std::array<ApplicationArea, ${coverage_count}> areas${coverage_init};\nconst std::array<std::pair<std::string_view, std::u8string_view>, ${fixture_count}> fixtures${fixture_init};\n}\nstd::span<const ApplicationArea> registeredApplicationAreas() noexcept { return areas; }\nstd::filesystem::path registeredCoverageFixture(std::string_view id) {\n    for (const auto& [key, path] : fixtures) if (key == id) return std::filesystem::path(path);\n    return {};\n}\n} // namespace endfield::app\n")
  foreach(kind production coverage)
    set(file "${ehud_area_dir}/${kind}_areas.cpp")
    set(old "")
    if(EXISTS "${file}")
      file(READ "${file}" old)
    endif()
    if(NOT old STREQUAL "${${kind}_text}")
      file(WRITE "${file}" "${${kind}_text}")
    endif()
  endforeach()
  if(production_libraries)
    list(REMOVE_DUPLICATES production_libraries)
    target_link_libraries(hud_application_areas PRIVATE ${production_libraries})
  endif()
  if(coverage_libraries)
    list(REMOVE_DUPLICATES coverage_libraries)
    target_link_libraries(hud_application_coverage_areas PRIVATE ${coverage_libraries})
  endif()
  # Hidden coverage reports every registered area by name (run_owner_coverage.cmake).
  list(JOIN names "," expected)
  foreach(test application_owner_coverage application_session_end_coverage)
    if(TEST ${test})
      set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT "EHUD_EXPECTED_AREAS=${expected}")
    endif()
  endforeach()
  message(STATUS "EndfieldHUD module areas: ${production_count} production, ${coverage_count} in hidden coverage, ${fixture_count} coverage fixtures")
endfunction()
if(WIN32)
  cmake_language(DEFER CALL ehud_app_write_areas)
endif()
