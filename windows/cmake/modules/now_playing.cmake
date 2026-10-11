# Now Playing area fragment (included after every target in windows/CMakeLists.txt).
# Tests use synthetic fixtures and injected providers/transports/audio routes
# only: no real GSMTC session, network request, audio route or user data.

# Every Now Playing target folds text with ICU (the OS icu.dll on Windows, a
# local ICU elsewhere) and builds on hud_now_playing/hud_archive. A portable
# configure without ICU leaves the whole area out, like the other ICU users
# (CMakeLists.txt: hud_shortcuts, hud_archive, ...), instead of linking
# ICU::uc/ICU::i18n targets that do not exist.
if(NOT TARGET hud_now_playing OR NOT TARGET hud_archive OR NOT (WIN32 OR (TARGET ICU::uc AND TARGET ICU::i18n)))
  message(STATUS "Now Playing area: ICU not found; Now Playing lyric, web, volume, fixture, acceptance, probe and area targets are skipped")
  return()
endif()

# Lyric sources (embedded LRC, LRCLIB, NetEase public catalog) with the source
# matcher, URL rules, decoders, caches and precedence, plus the playing-app
# volume bridge rules. Portable.
add_library(hud_now_playing_lyrics modules/now_playing_lyrics_sources.cpp modules/now_playing_volume.cpp)
target_include_directories(hud_now_playing_lyrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_now_playing_lyrics PUBLIC hud_now_playing PRIVATE hud_archive hud_data)
if(WIN32)
  target_link_libraries(hud_now_playing_lyrics PRIVATE icu)
else()
  target_link_libraries(hud_now_playing_lyrics PRIVATE ICU::uc ICU::i18n)
endif()

# Shared per-app audio rows -> Now Playing routes; packaged process identity.
add_library(hud_now_playing_volume native/now_playing_volume.cpp)
target_include_directories(hud_now_playing_volume PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_now_playing_volume PUBLIC hud_now_playing_lyrics hud_data)
if(WIN32)
  target_compile_definitions(hud_now_playing_volume PRIVATE UNICODE _UNICODE NOMINMAX)
endif()

# Visible-only public metadata downloads (LRCLIB/NetEase JSON, catalog covers):
# owner-thread core with an injected transport, Windows.Web.Http on Windows.
add_library(hud_now_playing_web native/now_playing_web.cpp)
target_include_directories(hud_now_playing_web PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_now_playing_web PUBLIC hud_now_playing_lyrics)
if(WIN32)
  target_compile_definitions(hud_now_playing_web PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_now_playing_web PRIVATE windowsapp ole32)
endif()

# UIA descriptor model for projected buttons/sliders (host provider input).
add_library(hud_now_playing_accessibility modules/now_playing_accessibility.cpp)
target_include_directories(hud_now_playing_accessibility PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_now_playing_accessibility PUBLIC hud_now_playing_presentation)

# The existing session owner gains lyric sources, web, volume and Event Log.
target_link_libraries(hud_now_playing_session PUBLIC hud_now_playing_lyrics hud_now_playing_web)

if(BUILD_TESTING)
  # Source: python3 windows/tools/now_playing_warm_reference.py
  add_executable(now_playing_warm_tests tests/now_playing_warm_tests.cpp)
  target_link_libraries(now_playing_warm_tests PRIVATE hud_now_playing_session hud_data)
  add_test(NAME now_playing_warm_contracts COMMAND now_playing_warm_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/now-playing-warm-source.json)
  # Source: python3 windows/tools/now_playing_lyrics_reference.py
  add_executable(now_playing_lyrics_sources_tests tests/now_playing_lyrics_sources_tests.cpp)
  target_link_libraries(now_playing_lyrics_sources_tests PRIVATE hud_now_playing_lyrics hud_data)
  add_test(NAME now_playing_lyrics_sources_contracts COMMAND now_playing_lyrics_sources_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/now-playing-lyrics-source.json)
  add_executable(now_playing_web_tests tests/now_playing_web_tests.cpp)
  target_link_libraries(now_playing_web_tests PRIVATE hud_now_playing_web)
  add_test(NAME now_playing_web_contracts COMMAND now_playing_web_tests)
  if(WIN32)
    # The Windows.Web.Http transport through an in-process IHttpFilter (no socket).
    add_executable(now_playing_web_winrt_tests tests/now_playing_web_winrt_tests.cpp)
    target_compile_definitions(now_playing_web_winrt_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(now_playing_web_winrt_tests PRIVATE hud_now_playing_web windowsapp ole32)
    add_test(NAME now_playing_web_winrt_contracts COMMAND now_playing_web_winrt_tests)
    set_tests_properties(now_playing_web_winrt_contracts PROPERTIES TIMEOUT 180)
  endif()
  add_executable(now_playing_volume_tests tests/now_playing_volume_tests.cpp)
  target_link_libraries(now_playing_volume_tests PRIVATE hud_now_playing_volume)
  add_test(NAME now_playing_volume_contracts COMMAND now_playing_volume_tests)
  add_executable(now_playing_sources_session_tests tests/now_playing_sources_session_tests.cpp)
  target_link_libraries(now_playing_sources_session_tests PRIVATE hud_now_playing_session hud_event_log hud_data)
  add_test(NAME now_playing_sources_session_contracts COMMAND now_playing_sources_session_tests)
  add_executable(now_playing_accessibility_tests tests/now_playing_accessibility_tests.cpp)
  target_link_libraries(now_playing_accessibility_tests PRIVATE hud_now_playing_accessibility hud_data)
  add_test(NAME now_playing_accessibility_contracts COMMAND now_playing_accessibility_tests)
endif()

if(TARGET hud_now_playing_preview)
  target_link_libraries(hud_now_playing_preview PUBLIC hud_now_playing_accessibility)
endif()

# UI Automation fragments for the projected controls (Invoke/RangeValue/Value),
# listed by the HUD window's UIA root provider.
if(WIN32)
  add_library(hud_now_playing_automation native/now_playing_automation.cpp)
  target_include_directories(hud_now_playing_automation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_now_playing_automation PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_now_playing_automation PUBLIC hud_now_playing_accessibility PRIVATE uiautomationcore oleaut32 ole32)
  if(MSVC)
    target_compile_options(hud_now_playing_automation PRIVATE /W4 /permissive- /EHsc)
  endif()
  if(BUILD_TESTING)
    add_executable(now_playing_automation_tests tests/now_playing_automation_tests.cpp)
    target_compile_definitions(now_playing_automation_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(now_playing_automation_tests PRIVATE hud_now_playing_automation uiautomationcore oleaut32 ole32)
    add_test(NAME now_playing_automation_contracts COMMAND now_playing_automation_tests)
  endif()
endif()

# Source diagnostic fixture player (OverlayController diagnostic mode) and the
# synthetic per-app audio route behind the AudioRuntime members Now Playing uses.
add_library(hud_now_playing_fixture native/now_playing_fixture.cpp)
target_include_directories(hud_now_playing_fixture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_now_playing_fixture PUBLIC hud_now_playing_service hud_data)
if(BUILD_TESTING)
  add_executable(now_playing_fixture_tests tests/now_playing_fixture_tests.cpp)
  target_link_libraries(now_playing_fixture_tests PRIVATE hud_now_playing_fixture hud_now_playing_session hud_data)
  add_test(NAME now_playing_fixture_contracts COMMAND now_playing_fixture_tests)
  # The production volume bridge (nowPlayingAppVolume) over the real shared
  # AudioRuntime with injected endpoint/session backends, and over the fixture.
  if(TARGET hud_audio_runtime)
    add_executable(now_playing_runtime_volume_tests tests/now_playing_runtime_volume_tests.cpp)
    target_link_libraries(now_playing_runtime_volume_tests PRIVATE hud_now_playing_fixture hud_now_playing_session hud_now_playing_volume hud_audio_runtime hud_data)
    if(WIN32)
      target_compile_definitions(now_playing_runtime_volume_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    endif()
    add_test(NAME now_playing_runtime_volume_contracts COMMAND now_playing_runtime_volume_tests)
    set_tests_properties(now_playing_runtime_volume_contracts PROPERTIES TIMEOUT 120)
  endif()
endif()

# Window-title fallback for Win32 players that publish no SMTC session
# (NetEase Cloud Music, QQ Music, Kugou; the Mac NowPlayingAccessibility
# compatibility fallback) and the composite provider that puts it beside
# GSMTC. Portable rules, provider and synthetic desktop; WinEvent hooks,
# EnumWindows and WM_APPCOMMAND on Windows. Tests use synthetic windows only.
add_library(hud_now_playing_players modules/now_playing_players.cpp native/now_playing_players.cpp)
target_include_directories(hud_now_playing_players PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_now_playing_players PUBLIC hud_now_playing_service hud_now_playing_lyrics PRIVATE hud_archive hud_data)
if(WIN32)
  target_compile_definitions(hud_now_playing_players PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_now_playing_players PRIVATE icu user32)
else()
  target_link_libraries(hud_now_playing_players PRIVATE ICU::uc ICU::i18n)
endif()
if(MSVC)
  target_compile_options(hud_now_playing_players PRIVATE /W4 /permissive- /EHsc)
endif()
if(BUILD_TESTING)
  # Source: python3 windows/tools/now_playing_players_reference.py
  add_executable(now_playing_players_tests tests/now_playing_players_tests.cpp)
  target_link_libraries(now_playing_players_tests PRIVATE hud_now_playing_players hud_now_playing_session hud_data)
  if(WIN32)
    target_compile_definitions(now_playing_players_tests PRIVATE UNICODE _UNICODE NOMINMAX)
  endif()
  add_test(NAME now_playing_players_contracts COMMAND now_playing_players_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/now-playing-players-source.json)
endif()

# Per-player live acceptance matrix (tools/now_playing_acceptance.hpp): the
# probe drives the production NowPlayingSession and records, per GSMTC or
# window-title player, what the HUD would present; reports merge across runs
# and store no track metadata. Portable; the synthetic replays are what the
# tests run.
add_library(hud_now_playing_acceptance tools/now_playing_acceptance.cpp)
target_include_directories(hud_now_playing_acceptance PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_now_playing_acceptance PUBLIC hud_now_playing_session PRIVATE hud_now_playing_fixture hud_data)
if(WIN32)
  target_compile_definitions(hud_now_playing_acceptance PRIVATE UNICODE _UNICODE NOMINMAX)
endif()
if(MSVC)
  target_compile_options(hud_now_playing_acceptance PRIVATE /W4 /permissive- /EHsc)
endif()
if(BUILD_TESTING)
  add_executable(now_playing_acceptance_tests tests/now_playing_acceptance_tests.cpp)
  target_link_libraries(now_playing_acceptance_tests PRIVATE hud_now_playing_acceptance hud_data)
  if(WIN32)
    target_compile_definitions(now_playing_acceptance_tests PRIVATE UNICODE _UNICODE NOMINMAX)
  endif()
  add_test(NAME now_playing_acceptance_contracts COMMAND now_playing_acceptance_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/now-playing-acceptance-replay.json
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/now-playing-acceptance-windows.json)
  set_tests_properties(now_playing_acceptance_contracts PROPERTIES TIMEOUT 120)
endif()

# User-run live acceptance probe (GSMTC, covers, lyric sources, the per-app
# volume route read-only, one explicit command) writing the matrix report.
# Tests run only the inert default invocation and --replay of the synthetic
# scenario in a private build directory (no media session, network or audio).
if(WIN32)
  add_executable(now_playing_probe tools/now_playing_probe.cpp)
  target_compile_definitions(now_playing_probe PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(now_playing_probe PRIVATE hud_now_playing_acceptance hud_now_playing_players hud_now_playing_service hud_now_playing_image hud_now_playing_web
    hud_now_playing_lyrics hud_now_playing_volume hud_audio_runtime hud_localization hud_data)
  if(MSVC)
    target_compile_options(now_playing_probe PRIVATE /W4 /permissive- /EHsc)
  endif()
  if(BUILD_TESTING)
    add_test(NAME now_playing_probe_inert COMMAND now_playing_probe)
    set_tests_properties(now_playing_probe_inert PROPERTIES PASS_REGULAR_EXPRESSION "Run only on your own machine")
    add_test(NAME now_playing_probe_matrix COMMAND ${CMAKE_COMMAND} "-DPROBE=$<TARGET_FILE:now_playing_probe>"
      "-DSCENARIO=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/now-playing-acceptance-replay.json"
      "-DWINDOWS_SCENARIO=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/now-playing-acceptance-windows.json"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/now_playing_probe_matrix" -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/now_playing_probe_check.cmake")
    set_tests_properties(now_playing_probe_matrix PROPERTIES TIMEOUT 300)
  endif()
endif()

# Production module area (app/module_owner.hpp, "Adding a module area"):
# EndfieldHUD.exe and the hidden owner coverage construct Now Playing through
# endfield::areas::nowPlayingModule. Production: GSMTC with the window-title
# fallback players, public lyric/cover downloads, the shared AudioRuntime
# route and Event Log playbackAction. Development previews: the fixture player
# and the synthetic per-app route only; hidden coverage adds a synthetic
# desktop behind the same composite provider. app.cmake links this library into
# EndfieldHUD.exe and watch_session_preview from the registration below.
if(WIN32)
  add_library(hud_now_playing_area STATIC app/areas/now_playing_module.cpp)
  target_include_directories(hud_now_playing_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_now_playing_area PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_now_playing_area PUBLIC hud_module_registry
    PRIVATE hud_now_playing_preview hud_now_playing_fixture hud_now_playing_players hud_now_playing_volume hud_now_playing_web hud_now_playing_accessibility
            hud_now_playing_automation hud_audio_runtime hud_event_log hud_utility user32 uiautomationcore oleaut32 ole32)
  if(MSVC)
    target_compile_options(hud_now_playing_area PRIVATE /W4 /permissive- /EHsc)
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "nowPlaying|hud_now_playing_area|app/areas/now_playing_module.hpp|endfield::areas::nowPlayingModule")
endif()

# One build target for every Now Playing test executable, tool and the area
# library (cmake --build ... --target now_playing_all), so a verification run
# names them without listing each.
add_custom_target(now_playing_all)
foreach(now_playing_target now_playing_model_tests now_playing_blend_tests now_playing_presentation_tests now_playing_scene_tests
    now_playing_service_tests now_playing_receipt_tests now_playing_image_tests now_playing_session_tests now_playing_preview_tests
    now_playing_warm_tests now_playing_lyrics_sources_tests now_playing_web_tests now_playing_web_winrt_tests now_playing_volume_tests
    now_playing_sources_session_tests now_playing_accessibility_tests now_playing_automation_tests now_playing_fixture_tests
    now_playing_runtime_volume_tests now_playing_acceptance_tests now_playing_players_tests now_playing_probe hud_now_playing_area)
  if(TARGET ${now_playing_target})
    add_dependencies(now_playing_all ${now_playing_target})
  endif()
endforeach()
