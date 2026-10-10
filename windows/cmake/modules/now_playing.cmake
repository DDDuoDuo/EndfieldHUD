# Now Playing area fragment (included after every target in windows/CMakeLists.txt).
# Tests use synthetic fixtures and injected providers/transports/audio routes
# only: no real GSMTC session, network request, audio route or user data.

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

# User-run live acceptance probe (GSMTC, thumbnails, lyric sources, commands).
# The registered test only checks that the default invocation is inert.
if(WIN32)
  add_executable(now_playing_probe tools/now_playing_probe.cpp)
  target_compile_definitions(now_playing_probe PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(now_playing_probe PRIVATE hud_now_playing_service hud_now_playing_image hud_now_playing_web hud_now_playing_lyrics hud_data)
  if(BUILD_TESTING)
    add_test(NAME now_playing_probe_inert COMMAND now_playing_probe)
    set_tests_properties(now_playing_probe_inert PROPERTIES PASS_REGULAR_EXPRESSION "Run only on your own machine")
  endif()
endif()

# Source diagnostic fixture player (OverlayController diagnostic mode).
add_library(hud_now_playing_fixture native/now_playing_fixture.cpp)
target_include_directories(hud_now_playing_fixture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_now_playing_fixture PUBLIC hud_now_playing_service)
if(BUILD_TESTING)
  add_executable(now_playing_fixture_tests tests/now_playing_fixture_tests.cpp)
  target_link_libraries(now_playing_fixture_tests PRIVATE hud_now_playing_fixture hud_now_playing_session hud_data)
  add_test(NAME now_playing_fixture_contracts COMMAND now_playing_fixture_tests)
endif()

# Make the Now Playing owner, diagnostic fixture and volume adapter available
# to the development preview and to the production application owner when the
# host area defines it (wiring itself lives in the host).
if(WIN32 AND TARGET watch_session_preview AND TARGET hud_now_playing_preview)
  target_link_libraries(watch_session_preview PRIVATE hud_now_playing_preview hud_now_playing_fixture hud_now_playing_volume)
endif()
if(WIN32 AND TARGET hud_application AND TARGET hud_now_playing_preview)
  target_link_libraries(hud_application PUBLIC hud_now_playing_preview hud_now_playing_fixture hud_now_playing_volume)
endif()
