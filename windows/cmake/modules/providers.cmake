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
  target_link_libraries(hud_system_providers_coverage PUBLIC hud_module_registry hud_utility hud_event_log_platform hud_activity hud_activity_icons hud_native hud_storage_files hud_data hud_localization)
  if(MSVC)
    target_compile_options(hud_system_providers_coverage PRIVATE /W4 /permissive- /EHsc)
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "systemProviders|hud_system_providers_coverage|app/areas/system_activity_providers.hpp|endfield::providers::systemProvidersCoverage|coverage")
endif()
