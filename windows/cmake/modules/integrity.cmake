# Calendar and minigame integrity area (included after every target in
# windows/CMakeLists.txt). Tests use synthetic in-memory data and Mac-derived
# fixtures only: no notification center, user calendar, settings or game data.

# Display-time Calendar localization: the Mac HUDCalendarError/permission pairs
# through the shared five-language catalog, plus the Windows wording for a
# denied notification permission. Portable; the Windows owner links it.
add_library(hud_calendar_localization modules/calendar_localization.cpp)
target_include_directories(hud_calendar_localization PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_calendar_localization PUBLIC hud_calendar hud_localization)
if(TARGET hud_calendar_preview)
  target_link_libraries(hud_calendar_preview PUBLIC hud_calendar_localization)
endif()

if(BUILD_TESTING)
  # tools/calendar_localization_reference.py -> calendar-localization-source.json
  add_executable(calendar_localization_tests tests/calendar_localization_tests.cpp)
  target_link_libraries(calendar_localization_tests PRIVATE hud_calendar_localization)
  add_test(NAME calendar_localization_contracts COMMAND calendar_localization_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/calendar-localization-source.json)
endif()

# Source minigame accessibility names (HUDOrbiPomInteraction) for the future
# shared UI Automation provider. Portable; reads only the borrowed session and
# state (the host calls it with OrbiPomPreview::state() on content events).
add_library(hud_orbipom_accessibility modules/orbipom_accessibility.cpp)
target_include_directories(hud_orbipom_accessibility PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_orbipom_accessibility PUBLIC hud_orbipom_presentation)

if(BUILD_TESTING)
  # tools/orbipom_accessibility_reference.py -> orbipom-accessibility-source.json
  add_executable(orbipom_accessibility_tests tests/orbipom_accessibility_tests.cpp)
  target_link_libraries(orbipom_accessibility_tests PRIVATE hud_orbipom_accessibility)
  add_test(NAME orbipom_accessibility_contracts COMMAND orbipom_accessibility_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/orbipom-accessibility-source.json)
endif()
