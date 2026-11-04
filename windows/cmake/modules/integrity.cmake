# Calendar and minigame integrity area (included after every target in
# windows/CMakeLists.txt). Tests use synthetic in-memory data and Mac-derived
# fixtures only: no notification center, user calendar, settings or game data.

# Display-time Calendar localization: the Mac HUDCalendarError/reminder status
# pairs through the shared five-language catalog, plus the Windows wording for
# a denied notification permission. Portable; the Calendar canvas links it.
add_library(hud_calendar_localization modules/calendar_localization.cpp)
target_include_directories(hud_calendar_localization PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_calendar_localization PUBLIC hud_calendar hud_localization)

if(BUILD_TESTING)
  # tools/calendar_localization_reference.py -> calendar-localization-source.json
  add_executable(calendar_localization_tests tests/calendar_localization_tests.cpp)
  target_link_libraries(calendar_localization_tests PRIVATE hud_calendar_localization)
  add_test(NAME calendar_localization_contracts COMMAND calendar_localization_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/calendar-localization-source.json)
endif()

