# Projection drawing latency probe: a measurement tool for an interactive
# desktop session (real DWM and display refresh), not a ctest test. It draws a
# 1000 Hz stroke through the production route and reports input-to-display
# latency in milliseconds and refresh periods, the samples the stroke keeps,
# per-move and per-frame CPU time, the presentation rate while drawing, back
# to back and during a finite animation, and open/closed idle frames and CPU.
# By default its window lies outside every monitor and its input is posted to
# that window only; --sliver shows one pixel column, --real draws with real
# SendInput moves, --gpu-load saturates the GPU as a game does (those three
# refuse to run while someone uses the desktop). Usage:
#   projection_latency_probe <native/hud.hlsl> <resources> [report.txt]
#       [--sliver|--real] [--gpu-load] [--normal-priority] [--force]
if(WIN32 AND BUILD_TESTING AND TARGET hud_projection_preview)
  # The shown workspace raises the HUD's presentation priority, as
  # Application::Impl::projectionCommand(showProjection) does.
  add_executable(projection_latency_probe tests/projection_latency_probe.cpp app/presentation_priority.cpp app/windows.manifest)
  target_compile_definitions(projection_latency_probe PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(projection_latency_probe PRIVATE hud_projection_preview windowsapp CoreMessaging dwmapi avrt)
endif()
