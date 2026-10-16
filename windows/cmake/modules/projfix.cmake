# Projection drawing through the production input route (live RC: Projection
# drew nothing): real mouse messages on the shown OverlayHost window, the
# Application's Projection capture policy, the shared ProjectionWorkspace with
# its composition renderer and backdrop, and a renderer readback of the stroke.
# It then draws through the host's own pump with the production frame path
# (the swap chain's admission) and checks the drawing latency contract: each
# burst of samples is drawn by one frame, the very next one presented, which
# holds the admission (RC 128 queued ~35 frames, 200 ms at 165 Hz); the stroke
# keeps every sample; an idle workspace presents nothing.
# Composition swap chains need an interactive desktop session: in the SSH test
# session the test exits 77 and ctest reports it skipped. Run it from an
# Interactive scheduled task, as the integration validator runs ctest.
if(WIN32 AND BUILD_TESTING AND TARGET hud_projection_preview)
  add_executable(projection_capture_route_tests tests/projection_capture_route_tests.cpp app/windows.manifest)
  target_compile_definitions(projection_capture_route_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(projection_capture_route_tests PRIVATE hud_projection_preview windowsapp CoreMessaging dwmapi)
  add_test(NAME projection_capture_route_contracts COMMAND projection_capture_route_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl ${CMAKE_CURRENT_SOURCE_DIR}/resources)
  set_tests_properties(projection_capture_route_contracts PROPERTIES SKIP_RETURN_CODE 77)
endif()
