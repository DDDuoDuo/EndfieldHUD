# Idle cost of the HUD (WINDOWS-MIGRATION.md sections 4 and 9: closed = no
# frame submission and minimal wake-ups; open idle = redraw only at the
# cadence actually needed).
#
# application_idle_probe: a measurement tool, not a ctest. What wakes a
# process that holds the HUD's graphics while nothing is drawn - no device,
# the production device after a frame, after the concealed release, after
# the device is released, and comparison devices - per thread start module.
#   application_idle_probe <native/hud.hlsl> [--seconds N] [--warp] [--composition]
# (--composition needs an interactive desktop: the swap chain, released and
# created again as the closed and reopened HUD do.)
#
# application_idle_trace: the production owner hidden on the REAL clock
# through the production frame scheduling (watch_session_preview
# --idle-trace, short windows) with the production Notes/Archive media stack:
# the closed HUD runs no frame and arms no timer, and closed past its
# graphics grace (shortened to 3 s; production keeps ten minutes) it has
# released and destroyed its device and wakes fewer than 20 times a second;
# a media owner holding the device refuses the release until it lets go; a
# reopening within the grace keeps the device; open idle with the ambient
# animation on draws at the source's 30 Hz ambient cadence and no faster,
# ambient off draws (almost) nothing, and the frames stay within a CPU
# bound. Never shows a window or touches user data.
if(WIN32 AND BUILD_TESTING)
  if(TARGET hud_native AND TARGET hud_data AND TARGET hud_activity_provider AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/application_idle_probe.cpp")
    add_executable(application_idle_probe tests/application_idle_probe.cpp app/process_diagnostics.cpp)
    target_include_directories(application_idle_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(application_idle_probe PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(application_idle_probe PRIVATE hud_native hud_data hud_activity_provider hud_utility ole32 psapi dxgi winmm iphlpapi)
    if(MSVC)
      target_compile_options(application_idle_probe PRIVATE /W4 /permissive- /EHsc)
    endif()
  endif()
  if(TARGET watch_session_preview AND TARGET EndfieldHUD AND ehud_app_inputs AND EXISTS "${ehud_app_inputs}/runtime-compiled-animation/runtime-input.json"
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/run_idle_trace.cmake")
    add_test(NAME application_idle_trace COMMAND "${CMAKE_COMMAND}" "-DPREVIEW=$<TARGET_FILE:watch_session_preview>"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/idle-trace-work" "-DINPUTS=${ehud_app_inputs}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
      "-DMAP=${EHUD_MAP_RESOURCE_DIR}" "-DORBIPOM=${EHUD_ORBIPOM_RESOURCE_DIR}" "-DPACKAGE=$<TARGET_FILE_DIR:EndfieldHUD>/resources"
      -DSECONDS=4 -DGRACE=3 -P "${CMAKE_CURRENT_SOURCE_DIR}/app/run_idle_trace.cmake")
    set_tests_properties(application_idle_trace PROPERTIES TIMEOUT 600)
  endif()
endif()
