# Work Mode and Calendar on the redesign (Mac Sources/WorkModeCanvas.swift and
# HUDCalendarCanvas.swift, 1.3.0): portable canvases over the design library.
# The controllers stay where they were: hud_work_mode (modules/work_mode.*,
# shared with the Personal Profile and the Account gauge) and hud_calendar
# (CalendarState, its repository and reminder plan). The app hosts both
# canvases through tools::DesignModuleOwner (app/application_modules.cpp).
add_library(hud_work_mode_canvas modules/work_mode_canvas.cpp)
target_include_directories(hud_work_mode_canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_work_mode_canvas PUBLIC hud_work_mode hud_design)
add_library(hud_calendar_canvas modules/calendar_canvas.cpp)
target_include_directories(hud_calendar_canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_calendar_canvas PUBLIC hud_calendar hud_calendar_localization hud_design)
foreach(target hud_work_mode_canvas hud_calendar_canvas)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()

# Both canvases against the Mac oracle (tools/work_mode_calendar_reference.sh
# -> tests/fixtures/work-mode-calendar-source.json): every scripted Mac state
# node by node with the recorded widths, line counts and date formats.
if(BUILD_TESTING)
  add_executable(work_mode_calendar_canvas_tests tests/work_mode_calendar_canvas_tests.cpp)
  target_link_libraries(work_mode_calendar_canvas_tests PRIVATE hud_work_mode_canvas hud_calendar_canvas)
  add_test(NAME work_mode_calendar_canvas_contracts COMMAND work_mode_calendar_canvas_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/work-mode-calendar-source.json")
endif()
# Both behind the design owners the app builds, on a hidden window and WARP
# (frames, services while closed, park; prints the residency / frame costs).
if(WIN32 AND BUILD_TESTING AND TARGET hud_design_native)
  add_executable(work_mode_calendar_owner_tests tests/work_mode_calendar_owner_tests.cpp)
  target_compile_definitions(work_mode_calendar_owner_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(work_mode_calendar_owner_tests PRIVATE hud_work_mode_canvas hud_calendar_canvas hud_calendar_civil hud_design_native ole32 user32)
  if(MSVC)
    target_compile_options(work_mode_calendar_owner_tests PRIVATE /W4 /permissive- /EHsc)
  endif()
  add_test(NAME work_mode_calendar_owner_contracts COMMAND work_mode_calendar_owner_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl")
  set_tests_properties(work_mode_calendar_owner_contracts PROPERTIES TIMEOUT 300)
endif()
