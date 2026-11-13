# Registered hidden animation trace (a measurement, not a contract): runs
# watch_session_preview --animation-trace with the module coverage inputs in a
# NEW owned work directory (synthetic providers, staged package, hardware GPU,
# real clock, production frame scheduling paced at 165 Hz) and prints, for
# every opening and closing it records, the frames, their interval (p95, max,
# late), the main thread's busy time, the group renders and the group targets
# allocated (the cost of fading multi-surface parts through a group, which the
# HUD close's canvas fade adds from its delay on: Mac 5b2a9d3). The trace
# itself checks its own density and lifecycle contracts. -DKEEP=<file> keeps
# the numeric report. Never shows a window or touches user data.
cmake_minimum_required(VERSION 3.25)
foreach(required PREVIEW WORK INPUTS SOURCE)
  if(NOT ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/shell_catalog_pin.cmake")
if(NOT GRACE)
  set(GRACE 3)
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
execute_process(COMMAND "${PREVIEW}" "${INPUTS}/runtime-compiled-animation" "${INPUTS}/profile-outline-catalog.ehscene" "${SOURCE}/native/hud.hlsl"
  --runtime-input --module-coverage --reader --calendar
  --map-geography "${MAP}" --map-player-assets "${resources}/map-player" --orbipom-assets "${ORBIPOM}"
  --animation-trace "${WORK}/animation-trace.json" --idle-trace-grace ${GRACE} --compiled-sha ${ehud_catalog_sha}
  --notes-data "${WORK}/notes-data" --notes-format-assets "${resources}/notes-format"
  --shelf-data "${WORK}/shelf-data"
  --clipboard-fixture --work-mode-fixture --volume-fixture --event-log-fixture --battery-fixture --storage-fixture --activity-fixture
  --settings-assets "${resources}" --archive
  ${EXTRA} ${package_arguments}
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 500)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Hidden animation trace failed (${result}):\n${output}\n${errors}")
endif()
file(READ "${WORK}/animation-trace.json" report)
if(KEEP)
  file(WRITE "${KEEP}" "${report}")
endif()
string(JSON refresh GET "${report}" refreshHz)
string(JSON device GET "${report}" device)
string(JSON count LENGTH "${report}" segments)
set(summary "")
math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
  string(JSON name GET "${report}" segments ${index} segment)
  string(JSON frames GET "${report}" segments ${index} frames)
  string(JSON p95 GET "${report}" segments ${index} intervalP95)
  string(JSON maximum GET "${report}" segments ${index} intervalMax)
  string(JSON late GET "${report}" segments ${index} lateFrames)
  string(JSON busy GET "${report}" segments ${index} busyP95)
  string(JSON busy_max GET "${report}" segments ${index} busyMax)
  string(JSON rows LENGTH "${report}" segments ${index} rows)
  set(renders 0)
  set(allocations 0)
  set(bytes 0)
  if(rows GREATER 0)
    math(EXPR last_row "${rows} - 1")
    foreach(row RANGE ${last_row})
      string(JSON after GET "${report}" segments ${index} rows ${row} afterDone)
      if(after)
        continue()
      endif()
      string(JSON value GET "${report}" segments ${index} rows ${row} nativeGroupRenders)
      math(EXPR renders "${renders} + ${value}")
      string(JSON value GET "${report}" segments ${index} rows ${row} groupTargetAllocations)
      math(EXPR allocations "${allocations} + ${value}")
      string(JSON value GET "${report}" segments ${index} rows ${row} groupBytes)
      if(value GREATER bytes)
        set(bytes ${value})
      endif()
    endforeach()
  endif()
  math(EXPR megabytes "${bytes} / 1048576")
  string(APPEND summary "\n  ${name}: ${frames} frames, interval p95 ${p95} ms max ${maximum} ms, ${late} late, busy p95 ${busy} ms max ${busy_max} ms, ${renders} group renders, ${allocations} group targets allocated, group targets up to ${megabytes} MiB")
endforeach()
message(STATUS "Hidden animation trace on ${device} at ${refresh} Hz:${summary}")
string(REGEX MATCH "Animation trace peak deadline wakes in a second: [0-9]+" peak "${output}")
message(STATUS "${peak}")
file(REMOVE_RECURSE "${WORK}")
