# Test-only module area (host area). Proves the area registration contract of
# app/module_owner.hpp end to end: this fragment registers a factory with the
# "|coverage" scope and a coverage fixture; app.cmake's generated translation
# unit links it into watch_session_preview only (never EndfieldHUD.exe); the
# hidden module coverage constructs it, delivers every shared event and host
# broadcast, runs its coverage steps and releases it, and
# application_owner_coverage requires it in the report by name.
if(WIN32 AND BUILD_TESTING)
  add_library(hud_application_area_probe STATIC tests/application_area_probe.cpp)
  target_include_directories(hud_application_area_probe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_application_area_probe PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_application_area_probe PUBLIC hud_module_registry hud_utility hud_data PRIVATE user32)
  if(MSVC)
    target_compile_options(hud_application_area_probe PRIVATE /W4 /permissive- /EHsc)
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "areaProbe|hud_application_area_probe|tests/application_area_probe.hpp|endfield::tests::applicationAreaProbe|coverage")
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREA_FIXTURES
    "applicationAreaProbe.fixture|${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/application_area_probe.json")
endif()
