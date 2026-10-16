# Registered hidden idle trace over the production Application owner: runs
# watch_session_preview --idle-trace with the module coverage inputs in a NEW
# owned work directory (synthetic providers, staged package, hardware GPU,
# real clock, production frame scheduling paced at 165 Hz), then checks the
# scheduling contract of WINDOWS-MIGRATION.md section 4 in each window:
#   closed (after launch and after a close): no frame and no frame timer;
#     with NO_SHARED_MEDIA (no Notes/Archive media stack, whose video owner
#     holds the graphics device) the device is released after the grace and
#     the process wakes fewer than 20 times a second;
#   open idle, ambient animation on: the source's 30 Hz ambient cadence (the
#     Mac's idleTick) besides occasional finite animations, within a
#     per-frame CPU bound;
#   open idle, ambient animation off: no continuous frames.
# The trace itself checks that a reopening within the grace keeps the device.
# Prints the numbers of every window. Never shows a window or touches user data.
cmake_minimum_required(VERSION 3.25)
foreach(required PREVIEW WORK INPUTS SOURCE)
  if(NOT ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
if(NOT SECONDS)
  set(SECONDS 4)
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(package_arguments "")
if(PACKAGE)
  if(NOT IS_DIRECTORY "${PACKAGE}" OR NOT EXISTS "${PACKAGE}/resources.json")
    message(FATAL_ERROR "The staged EndfieldHUD package is missing: ${PACKAGE}")
  endif()
  set(package_arguments --package "${PACKAGE}")
endif()
set(resources "${SOURCE}/resources")
set(media_arguments --archive-assets "${resources}")
if(NO_SHARED_MEDIA)
  set(media_arguments "")
endif()
execute_process(COMMAND "${PREVIEW}" "${INPUTS}/runtime-compiled-animation" "${INPUTS}/profile-outline-catalog.ehscene" "${SOURCE}/native/hud.hlsl"
  --runtime-input --module-coverage --reader --calendar
  --map-geography "${MAP}" --map-player-assets "${resources}/map-player" --orbipom-assets "${ORBIPOM}"
  --idle-trace "${WORK}/idle-trace.json" --idle-trace-seconds ${SECONDS} --compiled-sha 64852432949d3354e50c43ebb983cfa5afcf5dae29a4512879aca974902a9ba8
  --notes-assets "${INPUTS}/notes-controls-assets" --notes-assets-sha 9d2fcf0eead9eeeab1be4fdbb0e940a0e1e3eedf840ab9e4c63499e40ae70c00
  --notes-data "${WORK}/notes-data" --notes-format-assets "${resources}/notes-format"
  --shelf-assets "${resources}/shelf" --shelf-data "${WORK}/shelf-data" --shelf-mask "${resources}/shelf/reveal-mask.bin"
  --clipboard-assets "${resources}/clipboard" --work-mode-fixture --volume-fixture --event-log-fixture --battery-fixture
  --settings-assets "${resources}" ${media_arguments} --storage-assets "${resources}" --activity-assets "${resources}/activity"
  ${EXTRA} ${package_arguments}
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 500)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Hidden idle trace failed (${result}):\n${output}\n${errors}")
endif()
file(READ "${WORK}/idle-trace.json" report)
if(KEEP)
  # Opt-in copy of the numeric report for a measurement run (-DKEEP=<file>).
  file(WRITE "${KEEP}" "${report}")
endif()
string(JSON refresh GET "${report}" refreshHz)
string(JSON opening GET "${report}" openingFirstFrameSeconds)
string(JSON resume GET "${report}" openingDeviceResumeSeconds)
string(JSON quick GET "${report}" quickReopeningFirstFrameSeconds)
string(JSON count LENGTH "${report}" windows)
set(expected closed-after-launch open-idle-ambient-on open-idle-ambient-off closed)
list(LENGTH expected expected_count)
if(NOT count EQUAL expected_count)
  message(FATAL_ERROR "Idle trace recorded ${count} windows instead of ${expected_count}")
endif()
set(summary "")
math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
  list(GET expected ${index} name)
  string(JSON window GET "${report}" windows ${index} window)
  if(NOT window STREQUAL name)
    message(FATAL_ERROR "Idle trace window ${index} is ${window}, expected ${name}")
  endif()
  string(JSON seconds GET "${report}" windows ${index} seconds)
  string(JSON cpu GET "${report}" windows ${index} cpuPercentOfOneCore)
  string(JSON wakeups GET "${report}" windows ${index} activity wakeupsPerSecond)
  string(JSON fps GET "${report}" windows ${index} frames perSecond)
  string(JSON frames GET "${report}" windows ${index} frames count)
  string(JSON paced GET "${report}" windows ${index} frames displayPaced)
  string(JSON frame_timer GET "${report}" windows ${index} frames frameTimerWakes)
  string(JSON pending GET "${report}" windows ${index} frames framePendingAtEnd)
  string(JSON suspended GET "${report}" windows ${index} graphics deviceSuspended)
  string(JSON finite_by GET "${report}" windows ${index} demand finiteFramesBy)
  set(frame_ms 0)
  string(JSON frame_ms ERROR_VARIABLE no_frame_stage GET "${report}" windows ${index} stages frame meanMS)
  if(no_frame_stage)
    set(frame_ms 0)
  endif()
  math(EXPR steady "${frames} - ${paced}")
  string(APPEND summary "\n  ${name}: ${fps} frames/s (${paced} display-paced: ${finite_by}), ${frame_ms} ms per frame, CPU ${cpu}% of one core, ${wakeups} wake-ups/s, device released ${suspended}")
  if(name MATCHES "^closed")
    # Closed: no frame submission and no frame timer (section 4).
    if(NOT frames EQUAL 0 OR pending OR frame_timer GREATER 0)
      message(FATAL_ERROR "The closed HUD scheduled frames in ${name}: ${frames} frames, frame pending ${pending}, ${frame_timer} frame timer wakes")
    endif()
    if(NO_SHARED_MEDIA AND (NOT suspended OR wakeups GREATER 20))
      message(FATAL_ERROR "The closed HUD kept its graphics device in ${name} (released ${suspended}, ${wakeups} wake-ups/s)")
    endif()
  else()
    # Occasional finite animations (a charge badge change) run display-paced;
    # nothing idle does so continuously.
    if(paced GREATER 0)
      math(EXPR paced_limit "${SECONDS} * 15")
      if(paced GREATER paced_limit)
        message(FATAL_ERROR "Open idle ran finite motion continuously in ${name}: ${paced} display-paced frames (${finite_by})")
      endif()
    endif()
    if(name STREQUAL "open-idle-ambient-on")
      # The source's ambient cadence (Mac idleTick: every second 60 Hz tick).
      math(EXPR low "${SECONDS} * 24")
      math(EXPR high "${SECONDS} * 32 + 2")
      if(steady LESS low OR steady GREATER high)
        message(FATAL_ERROR "Open idle with ambient animation drew ${steady} steady frames in ${seconds} s instead of the 30 Hz ambient cadence")
      endif()
      if(frame_ms GREATER 6)
        message(FATAL_ERROR "An ambient frame took ${frame_ms} ms of CPU (bound 6 ms)")
      endif()
    elseif(steady GREATER SECONDS)
      message(FATAL_ERROR "Open idle without ambient animation kept drawing: ${steady} steady frames in ${seconds} s")
    endif()
  endif()
endforeach()
message(STATUS "Hidden idle trace at ${refresh} Hz: opening after a long close ${opening} s (device ${resume} s), reopening within the grace ${quick} s:${summary}")
file(REMOVE_RECURSE "${WORK}")
