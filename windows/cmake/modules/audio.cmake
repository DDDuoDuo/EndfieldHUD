# Volume and per-app audio area (included after every target in CMakeLists.txt).
# Tests use injected endpoint/session backends, synthetic Mac-derived fixtures
# and temporary directories only: no real audio device, mixer level, process,
# default-device change or user data is touched.

# Portable endpoint rules: classification, capabilities, ordering, topology.
add_library(hud_audio_model native/audio_endpoint_model.cpp)
target_include_directories(hud_audio_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_audio_model PUBLIC hud_services)

# The one sleeping audio worker (endpoints + per-app sessions + device list),
# the owned-attenuation recovery journal and the Volume binding access.
add_library(hud_audio_service native/audio_service.cpp native/audio_route_journal.cpp native/volume_audio_access.cpp)
target_include_directories(hud_audio_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_audio_service PUBLIC hud_audio_model hud_services hud_data hud_scene)
if(WIN32)
  # Native WASAPI endpoint backend (IMMDeviceEnumerator, IMMNotificationClient,
  # IAudioEndpointVolume); constructed only on the audio worker's MTA.
  target_sources(hud_audio_service PRIVATE native/audio_endpoint_wasapi.cpp)
  target_compile_definitions(hud_audio_service PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_audio_service PRIVATE ole32 uuid)
endif()

# Five-language Volume captions/errors and the UIA descriptor model.
add_library(hud_volume_strings native/volume_strings.cpp native/volume_accessibility.cpp)
target_include_directories(hud_volume_strings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_volume_strings PUBLIC hud_localization)
if(WIN32)
  target_link_libraries(hud_volume_strings PUBLIC hud_native)
else()
  # hud_native (which owns volume_scene.cpp on Windows) is Windows-only.
  target_sources(hud_volume_strings PRIVATE native/volume_scene.cpp)
  target_link_libraries(hud_volume_strings PUBLIC hud_data hud_scene hud_motion hud_services)
endif()

# Production owner: the one AudioService worker + the Volume provider binding
# + localized failure text, Event Log device labels and Now Playing routes.
add_library(hud_audio_runtime native/audio_runtime.cpp)
target_include_directories(hud_audio_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_audio_runtime PUBLIC hud_audio_service hud_volume_strings hud_localization)
if(NOT WIN32)
  # hud_native (which owns volume_provider.cpp on Windows) is Windows-only.
  target_sources(hud_audio_runtime PRIVATE native/volume_provider.cpp)
  target_link_libraries(hud_audio_runtime PUBLIC hud_audio_default)
endif()

set(EHUD_AUDIO_TARGETS hud_audio_model hud_audio_service hud_volume_strings hud_audio_runtime)
if(WIN32)
  # UI Automation fragments for the projected Volume controls.
  add_library(hud_volume_automation native/volume_automation.cpp)
  target_include_directories(hud_volume_automation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_volume_automation PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_volume_automation PUBLIC hud_volume_strings PRIVATE uiautomationcore oleaut32 ole32)
  list(APPEND EHUD_AUDIO_TARGETS hud_volume_automation)
endif()

foreach(target ${EHUD_AUDIO_TARGETS})
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endforeach()

if(BUILD_TESTING)
  # Source: bash windows/tools/audio_reference.sh (unchanged Mac sources).
  add_executable(audio_endpoint_model_tests tests/audio_endpoint_model_tests.cpp)
  target_link_libraries(audio_endpoint_model_tests PRIVATE hud_audio_model hud_data)
  add_test(NAME audio_endpoint_model_contracts COMMAND audio_endpoint_model_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/audio-endpoint-source.json")

  add_executable(audio_route_journal_tests tests/audio_route_journal_tests.cpp)
  target_link_libraries(audio_route_journal_tests PRIVATE hud_audio_service hud_data)
  add_test(NAME audio_route_journal_contracts COMMAND audio_route_journal_tests)

  add_executable(audio_service_tests tests/audio_service_tests.cpp)
  target_link_libraries(audio_service_tests PRIVATE hud_audio_service hud_data)
  if(WIN32)
    target_compile_definitions(audio_service_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(audio_service_tests PRIVATE hud_native)
  else()
    target_sources(audio_service_tests PRIVATE native/volume_provider.cpp native/volume_scene.cpp)
    target_link_libraries(audio_service_tests PRIVATE hud_audio_default hud_scene hud_motion)
  endif()
  add_test(NAME audio_service_contracts COMMAND audio_service_tests)

  # Source: bash windows/tools/audio_reference.sh (HUDVolumeInteraction export).
  add_executable(volume_accessibility_tests tests/volume_accessibility_tests.cpp)
  target_link_libraries(volume_accessibility_tests PRIVATE hud_volume_strings hud_data)
  add_test(NAME volume_accessibility_contracts COMMAND volume_accessibility_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/audio-endpoint-source.json")

  # The production audio owner over injected backends (votes, drain fan-out,
  # Event Log device labels, Now Playing routes, language, power, stop).
  add_executable(audio_runtime_tests tests/audio_runtime_tests.cpp)
  target_link_libraries(audio_runtime_tests PRIVATE hud_audio_runtime hud_data)
  add_test(NAME audio_runtime_contracts COMMAND audio_runtime_tests)

  if(WIN32)
    # Volume UI Automation fragments over a real controller (no window/audio).
    add_executable(volume_automation_tests tests/volume_automation_tests.cpp)
    target_compile_definitions(volume_automation_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(volume_automation_tests PRIVATE hud_volume_automation uiautomationcore oleaut32 ole32)
    add_test(NAME volume_automation_contracts COMMAND volume_automation_tests)

    # Volume owner accessibility projection + UIA over the hidden owner.
    add_executable(volume_preview_automation_tests tests/volume_preview_automation_tests.cpp tools/volume_preview.cpp)
    target_compile_definitions(volume_preview_automation_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(volume_preview_automation_tests PRIVATE hud_native hud_motion hud_services hud_shelf_icons hud_volume_automation uiautomationcore oleaut32 ole32)
    add_test(NAME volume_preview_automation_contracts COMMAND volume_preview_automation_tests)

    # Volume owner app-row icons over an injected icon plan (no Shell worker).
    add_executable(volume_preview_icon_tests tests/volume_preview_icon_tests.cpp tools/volume_preview.cpp)
    target_compile_definitions(volume_preview_icon_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(volume_preview_icon_tests PRIVATE hud_native hud_motion hud_services hud_shelf_icons)
    add_test(NAME volume_preview_icon_contracts COMMAND volume_preview_icon_tests)

    # Native contracts that never open COM/audio: factories are inert, the
    # undocumented default-device ABI is absent, and CompareStringEx ordering
    # matches the source ordering for the oracle names.
    add_executable(audio_native_contract_tests tests/audio_native_contract_tests.cpp)
    target_compile_definitions(audio_native_contract_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(audio_native_contract_tests PRIVATE hud_audio_service hud_audio_default hud_data)
    add_test(NAME audio_native_contracts COMMAND audio_native_contract_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/audio-endpoint-source.json")
  endif()
endif()

# User-run live acceptance probe (endpoints, traits, defaults, sessions and
# device events; read-only unless --app-gain). The registered test only checks
# that the default invocation is inert: no audio interface is opened.
if(WIN32)
  add_executable(audio_probe tools/audio_probe.cpp)
  target_compile_definitions(audio_probe PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(audio_probe PRIVATE hud_audio_service hud_data)
  if(BUILD_TESTING)
    add_test(NAME audio_probe_inert COMMAND audio_probe)
    set_tests_properties(audio_probe_inert PROPERTIES PASS_REGULAR_EXPRESSION "Run only on your own machine")
  endif()
endif()

# Make the audio owner, Volume strings and Volume UI Automation available to
# the development preview and the production application owner (additive
# links only; their sources are not changed here).
if(TARGET watch_session_preview)
  target_link_libraries(watch_session_preview PRIVATE hud_audio_runtime hud_volume_strings)
  if(TARGET hud_volume_automation)
    target_link_libraries(watch_session_preview PRIVATE hud_volume_automation)
  endif()
endif()
if(TARGET hud_application)
  target_link_libraries(hud_application PUBLIC hud_audio_runtime hud_volume_strings)
  if(TARGET hud_volume_automation)
    target_link_libraries(hud_application PUBLIC hud_volume_automation)
  endif()
endif()
