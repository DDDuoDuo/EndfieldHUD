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

# The Volume module's model (VolumeSnapshot, VolumeCallbacks, the endpoint
# mapping) and the five-language failure text of the provider binding.
add_library(hud_volume_model native/volume_snapshot.cpp native/volume_strings.cpp)
target_include_directories(hud_volume_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_volume_model PUBLIC hud_localization hud_data hud_services)
if(WIN32)
  # volume_provider.cpp (in hud_native on Windows) reads the model.
  target_link_libraries(hud_native PUBLIC hud_volume_model)
endif()

# Production owner: the one AudioService worker + the Volume provider binding
# + localized failure text, Event Log device labels and Now Playing routes.
add_library(hud_audio_runtime native/audio_runtime.cpp)
target_include_directories(hud_audio_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_audio_runtime PUBLIC hud_audio_service hud_volume_model hud_localization)
if(WIN32)
  # The provider binding (volume_provider.cpp) compiles into hud_native.
  target_link_libraries(hud_audio_runtime PUBLIC hud_native)
else()
  # hud_native (which owns volume_provider.cpp on Windows) is Windows-only.
  target_sources(hud_audio_runtime PRIVATE native/volume_provider.cpp)
  target_link_libraries(hud_audio_runtime PUBLIC hud_audio_default)
endif()

# Packaged audio application names (the ms-resource reference rule and the
# XmlLite AppxManifest reader) beside the WASAPI session backend, which runs
# them once per process identity on the audio worker.
target_sources(hud_services PRIVATE native/audio_application_name.cpp)
if(WIN32)
  target_link_libraries(hud_services PRIVATE xmllite shlwapi)
endif()

set(EHUD_AUDIO_TARGETS hud_audio_model hud_audio_service hud_volume_model hud_audio_runtime)

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

  # Synthetic manifests in a private temporary directory only.
  add_executable(audio_application_name_tests tests/audio_application_name_tests.cpp)
  target_link_libraries(audio_application_name_tests PRIVATE hud_services)
  if(WIN32)
    target_compile_definitions(audio_application_name_tests PRIVATE UNICODE _UNICODE NOMINMAX)
  endif()
  add_test(NAME audio_application_name_contracts COMMAND audio_application_name_tests)

  add_executable(audio_route_journal_tests tests/audio_route_journal_tests.cpp)
  target_link_libraries(audio_route_journal_tests PRIVATE hud_audio_service hud_data)
  add_test(NAME audio_route_journal_contracts COMMAND audio_route_journal_tests)

  add_executable(audio_service_tests tests/audio_service_tests.cpp)
  target_link_libraries(audio_service_tests PRIVATE hud_audio_service hud_data)
  if(WIN32)
    target_compile_definitions(audio_service_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(audio_service_tests PRIVATE hud_native)
  else()
    target_sources(audio_service_tests PRIVATE native/volume_provider.cpp)
    target_link_libraries(audio_service_tests PRIVATE hud_audio_default hud_volume_model)
  endif()
  add_test(NAME audio_service_contracts COMMAND audio_service_tests)

  # The production audio owner over injected backends (votes, drain fan-out,
  # Event Log device labels, Now Playing routes, language, power, stop).
  add_executable(audio_runtime_tests tests/audio_runtime_tests.cpp)
  target_link_libraries(audio_runtime_tests PRIVATE hud_audio_runtime hud_data)
  add_test(NAME audio_runtime_contracts COMMAND audio_runtime_tests)

  if(WIN32)
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

# Make the audio owner and the Volume model available to the development
# preview and the production application owner (additive links only).
if(TARGET watch_session_preview)
  target_link_libraries(watch_session_preview PRIVATE hud_audio_runtime hud_volume_model)
endif()
if(TARGET hud_application)
  target_link_libraries(hud_application PUBLIC hud_audio_runtime hud_volume_model)
endif()
