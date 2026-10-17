# System providers area fragment: Clipboard, Event Log, Activity Monitor and
# Storage (included after every target in windows/CMakeLists.txt). These
# modules stay built-in owners of the production Application; this fragment
# extends their provider libraries and adds the provider tests. Tests use
# synthetic fixtures and temporary directories only: no real clipboard, user
# data, installed app, account, startup entry or registry state is touched.

set(EHUD_PROVIDER_DEFINITIONS _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)

# --- Event Log: EventLogCanvas.timestamp in the current Windows time zone -----
if(TARGET hud_event_log_platform)
  target_sources(hud_event_log_platform PRIVATE native/event_log_time.cpp)
  if(WIN32)
    set_property(SOURCE native/event_log_time.cpp APPEND PROPERTY COMPILE_DEFINITIONS ${EHUD_PROVIDER_DEFINITIONS})
    # EnumDynamicTimeZoneInformation (tests resolve an explicit zone).
    target_link_libraries(hud_event_log_platform PRIVATE advapi32)
  endif()
  if(BUILD_TESTING)
    # Source: tools/event_log_timestamp_reference.sh (Mac-only oracle export).
    add_executable(event_log_time_tests tests/event_log_time_tests.cpp)
    target_link_libraries(event_log_time_tests PRIVATE hud_event_log_platform hud_data)
    if(WIN32)
      target_compile_definitions(event_log_time_tests PRIVATE ${EHUD_PROVIDER_DEFINITIONS})
    endif()
    add_test(NAME event_log_time_contracts COMMAND event_log_time_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/event_log_timestamps.json")
  endif()
endif()

# --- Activity Monitor: app display names and app-row icons --------------------
if(WIN32 AND TARGET hud_activity_provider)
  # Version-resource FileDescription and AppsFolder display names.
  target_link_libraries(hud_activity_provider PRIVATE version shell32 ole32 uuid)
endif()
# --- Authored HUDSubsectionTransition reveals ---------------------------------
# activitySortShutter and clipboardRevealShutter are portable (no Windows API):
# they interpolate core::subsectionMaskKeyframe with the CA ease-out.
if(TARGET hud_activity_scene)
  target_link_libraries(hud_activity_scene PUBLIC hud_motion)
endif()
if(TARGET clipboard_scene_tests)
  target_link_libraries(clipboard_scene_tests PRIVATE hud_motion)
endif()

# --- Activity Monitor: GPU engine usage (Windows addition) --------------------
# "\GPU Engine(*)\Running Time" over PDH (already linked), only while
# Activity is visible; the preview sets that demand.
if(TARGET hud_activity_provider)
  target_sources(hud_activity_provider PRIVATE native/activity_gpu.cpp)
  if(TARGET hud_activity_preview)
    target_link_libraries(hud_activity_preview PUBLIC hud_activity_provider)
  endif()
  if(BUILD_TESTING)
    # Instance names, collection bounds, the visibility gate and (Windows) the
    # live collector over this test process only.
    add_executable(activity_gpu_tests tests/activity_gpu_tests.cpp)
    target_link_libraries(activity_gpu_tests PRIVATE hud_activity_provider)
    if(WIN32)
      target_compile_definitions(activity_gpu_tests PRIVATE ${EHUD_PROVIDER_DEFINITIONS})
      target_link_libraries(activity_gpu_tests PRIVATE d3d11 pdh)
    endif()
    if(MSVC)
      target_compile_options(activity_gpu_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME activity_gpu_contracts COMMAND activity_gpu_tests)
  endif()
endif()
if(WIN32 AND TARGET hud_activity_scene)
  # Visible-row icon loads on the shared UtilityExecutor (ActivityPreviewOptions::iconExecutor).
  add_library(hud_activity_icons STATIC native/activity_icons.cpp)
  target_include_directories(hud_activity_icons PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_activity_icons PRIVATE ${EHUD_PROVIDER_DEFINITIONS})
  target_link_libraries(hud_activity_icons PUBLIC hud_activity_scene hud_layer_images hud_utility hud_data PRIVATE shell32 ole32 windowscodecs uuid)
  if(TARGET hud_activity_preview)
    target_link_libraries(hud_activity_preview PUBLIC hud_activity_icons)
  endif()
endif()
if(WIN32 AND TARGET activity_provider_tests)
  # Reads the test process's own memory counters.
  target_link_libraries(activity_provider_tests PRIVATE psapi)
endif()
if(WIN32 AND TARGET activity_catalog_tests)
  # Synthetic FileDescription of the test executable itself.
  target_sources(activity_catalog_tests PRIVATE tests/activity_catalog_tests.rc)
endif()

# Storage: Explorer's name for an unlabelled startup volume uses shell32/ole32,
# which hud_storage_files already links.

# --- Clipboard: link parity (ClipboardStore.parsedURL) ------------------------
# Non-ASCII hosts convert through ICU UTS #46 like macOS URL(string:): the
# installed Windows ICU, or a found ICU in portable verification builds.
if(TARGET hud_services)
  if(WIN32)
    target_link_libraries(hud_services PRIVATE icu)
  elseif(ICU_FOUND)
    target_compile_definitions(hud_services PRIVATE EHUD_CLIPBOARD_URL_ICU)
    target_link_libraries(hud_services PRIVATE ICU::uc)
  endif()
  if(BUILD_TESTING AND (WIN32 OR ICU_FOUND))
    # Source: tools/clipboard_url_reference.sh (Mac-only oracle export).
    add_executable(clipboard_url_tests tests/clipboard_url_tests.cpp)
    target_link_libraries(clipboard_url_tests PRIVATE hud_services hud_data)
    if(MSVC)
      target_compile_options(clipboard_url_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME clipboard_url_contracts COMMAND clipboard_url_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/clipboard_urls.json")
    # History identity (ClipboardPayload.matches): standardized file paths and
    # canonically equivalent strings. Source: tools/clipboard_matches_reference.sh.
    add_executable(clipboard_matches_tests tests/clipboard_matches_tests.cpp)
    target_link_libraries(clipboard_matches_tests PRIVATE hud_services hud_data)
    if(MSVC)
      target_compile_options(clipboard_matches_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME clipboard_matches_contracts COMMAND clipboard_matches_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/clipboard_matches.json")
  endif()
endif()
if(WIN32 AND TARGET system_services_tests)
  # Message-only test window for the WM_POWERBROADCAST storm and the
  # synthetic-clipboard service runs.
  target_link_libraries(system_services_tests PRIVATE user32)
endif()

# --- Hidden coverage of the provider pieces ----------------------------------
# Test-only module area (app/module_owner.hpp, "|coverage" scope): never linked
# into EndfieldHUD.exe. It runs the new Clipboard/Event Log/Activity/Storage
# provider pieces on the production harness's shared executor with synthetic
# inputs in its own coverage root (app/areas/system_activity_providers.hpp);
# application_owner_coverage requires it by name.
if(WIN32 AND BUILD_TESTING AND TARGET hud_event_log_platform AND TARGET hud_activity_icons)
  add_library(hud_system_providers_coverage STATIC app/areas/system_activity_providers.cpp)
  target_include_directories(hud_system_providers_coverage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_system_providers_coverage PRIVATE ${EHUD_PROVIDER_DEFINITIONS})
  target_link_libraries(hud_system_providers_coverage PUBLIC hud_module_registry hud_utility hud_event_log_platform hud_activity hud_activity_icons hud_activity_provider hud_native hud_storage_files hud_data hud_localization)
  if(MSVC)
    target_compile_options(hud_system_providers_coverage PRIVATE /W4 /permissive- /EHsc)
  endif()
  # The built-in Activity and Storage owners the host may publish (their
  # memory checks); Clipboard and Event Log compile into the harness itself.
  foreach(lib hud_activity_preview hud_storage_preview)
    if(TARGET ${lib})
      target_link_libraries(hud_system_providers_coverage PUBLIC ${lib})
    endif()
  endforeach()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "systemProviders|hud_system_providers_coverage|app/areas/system_activity_providers.hpp|endfield::providers::systemProvidersCoverage|coverage")
endif()

# --- Clipboard rows reveal and Activity sort reveal, frame by frame -----------
# Through the owners' previews, published as the HUD host publishes owners
# (WARP readback of a hidden window, synthetic rows and apps; optional PNGs).
# The authored HUDSubsectionTransition shutter never re-covers revealed rows.
if(WIN32 AND BUILD_TESTING AND TARGET hud_activity_preview AND TARGET hud_native)
  add_executable(activity_clipboard_reveal_frames_tests tests/activity_clipboard_reveal_frames_tests.cpp tools/clipboard_preview.cpp)
  target_compile_definitions(activity_clipboard_reveal_frames_tests PRIVATE ${EHUD_PROVIDER_DEFINITIONS})
  target_link_libraries(activity_clipboard_reveal_frames_tests PRIVATE hud_activity_preview hud_native hud_motion ole32 windowscodecs)
  if(MSVC)
    target_compile_options(activity_clipboard_reveal_frames_tests PRIVATE /W4 /permissive- /EHsc)
  endif()
  foreach(mode clipboard activity)
    add_test(NAME ${mode}_reveal_frames_contracts COMMAND activity_clipboard_reveal_frames_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl"
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/clipboard" "${CMAKE_CURRENT_SOURCE_DIR}/resources/activity" ${mode})
    set_tests_properties(${mode}_reveal_frames_contracts PROPERTIES TIMEOUT 180)
  endforeach()
endif()

# --- Storage: volume choice (Windows addition) -------------------------------
# The chooser and its secondary menu through the Storage owner with synthetic
# volumes on a hidden window and WARP; no volume of this machine is read.
if(WIN32 AND BUILD_TESTING AND TARGET hud_storage_preview AND TARGET hud_storage_options)
  add_executable(storage_volume_tests tests/storage_volume_tests.cpp)
  target_compile_definitions(storage_volume_tests PRIVATE ${EHUD_PROVIDER_DEFINITIONS})
  target_link_libraries(storage_volume_tests PRIVATE hud_storage_preview hud_storage_options)
  if(MSVC)
    target_compile_options(storage_volume_tests PRIVATE /W4 /permissive- /EHsc)
  endif()
  add_test(NAME storage_volume_contracts COMMAND storage_volume_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/resources")
  set_tests_properties(storage_volume_contracts PROPERTIES TIMEOUT 180)
endif()

# --- What the provider modules keep resident ---------------------------------
# Clipboard, Activity Monitor, Event Log and Storage through their owners on a
# hidden window and WARP: a close on another module releases every raster,
# rasterizer entry and GPU texture, showing the module again rebuilds artwork
# that paints identically, and the module shown at the close keeps its
# artwork; Storage rasterizes nothing before it is shown. "activityCounters": the
# Activity GPU engine and disk queries with this process's threads and
# private bytes. Synthetic rows, apps, events and volumes only.
if(WIN32 AND BUILD_TESTING AND TARGET hud_activity_preview AND TARGET hud_storage_preview AND TARGET hud_native)
  add_executable(activity_clipboard_resources_tests tests/activity_clipboard_resources_tests.cpp tools/clipboard_preview.cpp tools/event_log_preview.cpp)
  target_compile_definitions(activity_clipboard_resources_tests PRIVATE ${EHUD_PROVIDER_DEFINITIONS})
  target_link_libraries(activity_clipboard_resources_tests PRIVATE hud_activity_preview hud_activity_provider hud_storage_preview hud_storage_options hud_native hud_motion psapi)
  if(MSVC)
    target_compile_options(activity_clipboard_resources_tests PRIVATE /W4 /permissive- /EHsc)
  endif()
  foreach(mode clipboard activity eventLog storage activityCounters)
    string(REGEX REPLACE "([A-Z])" "_\\1" name "${mode}")
    string(TOLOWER "${name}" name)
    add_test(NAME ${name}_resources_contracts COMMAND activity_clipboard_resources_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/resources" ${mode})
    set_tests_properties(${name}_resources_contracts PROPERTIES TIMEOUT 300)
  endforeach()
endif()

# --- UI Automation peers of the provider modules ------------------------------
# Mac HUDClipboardInteraction, HUDEventLogInteraction and HUDTelemetryInteraction
# accessibility buttons (and the telemetry status text) as screen reader peers
# (native/screen_reader_peers.hpp) of the built-in Clipboard, Event Log,
# Activity Monitor and Storage owners, for their groups under the HUD window's
# UIA root (the host's module adapters list them, app/application_modules.cpp).
if(WIN32 AND TARGET hud_local_automation AND TARGET hud_activity_preview AND TARGET hud_storage_preview)
  add_library(hud_provider_automation STATIC tools/clipboard_automation.cpp tools/event_log_automation.cpp
    tools/activity_automation.cpp tools/storage_automation.cpp)
  target_include_directories(hud_provider_automation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_provider_automation PRIVATE ${EHUD_PROVIDER_DEFINITIONS})
  target_link_libraries(hud_provider_automation PUBLIC hud_local_automation hud_activity_preview hud_storage_preview hud_native hud_localization)
  if(MSVC)
    target_compile_options(hud_provider_automation PRIVATE /W4 /permissive- /EHsc)
  endif()
  if(TARGET hud_application)
    target_link_libraries(hud_application PUBLIC hud_provider_automation)
  endif()
  if(BUILD_TESTING AND TARGET hud_storage_options)
    # The peers through their provider interfaces over the owners on a hidden
    # window and WARP; synthetic rows, events, apps and volumes only.
    add_executable(activity_clipboard_automation_tests tests/activity_clipboard_automation_tests.cpp tools/clipboard_preview.cpp tools/event_log_preview.cpp)
    target_compile_definitions(activity_clipboard_automation_tests PRIVATE ${EHUD_PROVIDER_DEFINITIONS})
    target_link_libraries(activity_clipboard_automation_tests PRIVATE hud_provider_automation hud_activity_preview hud_storage_preview hud_storage_options
      hud_native hud_motion uiautomationcore oleaut32 ole32 user32)
    if(MSVC)
      target_compile_options(activity_clipboard_automation_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    foreach(mode clipboard eventLog activity storage)
      string(REGEX REPLACE "([A-Z])" "_\\1" name "${mode}")
      string(TOLOWER "${name}" name)
      add_test(NAME ${name}_automation_contracts COMMAND activity_clipboard_automation_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/resources" ${mode})
      set_tests_properties(${name}_automation_contracts PROPERTIES TIMEOUT 180)
    endforeach()
  endif()
endif()
