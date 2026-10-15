# Registered hidden memory census over the production Application owner:
# runs watch_session_preview --memory-trace with the module coverage inputs in
# a NEW owned work directory (synthetic providers, staged package), then checks
# the report: every stage was recorded with its process census, owner
# attribution and thread origins; every presented module was visited; and the
# closed HUD does not grow across ten more open/close cycles (private bytes,
# threads, retained raster entries and renderer resources). Prints the stage
# numbers. Never shows a window or touches user data.
cmake_minimum_required(VERSION 3.25)
foreach(required PREVIEW WORK INPUTS SOURCE)
  if(NOT ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
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
  --memory-trace "${WORK}/memory-trace.json" --compiled-sha 64852432949d3354e50c43ebb983cfa5afcf5dae29a4512879aca974902a9ba8
  --notes-assets "${INPUTS}/notes-controls-assets" --notes-assets-sha 9d2fcf0eead9eeeab1be4fdbb0e940a0e1e3eedf840ab9e4c63499e40ae70c00
  --notes-data "${WORK}/notes-data" --notes-format-assets "${resources}/notes-format"
  --shelf-assets "${resources}/shelf" --shelf-data "${WORK}/shelf-data" --shelf-mask "${resources}/shelf/reveal-mask.bin"
  --clipboard-assets "${resources}/clipboard" --work-mode-fixture --volume-fixture --event-log-fixture --battery-fixture
  --settings-assets "${resources}" --archive-assets "${resources}" --storage-assets "${resources}" --activity-assets "${resources}/activity"
  ${EXTRA} ${package_arguments}
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 500)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Hidden memory trace failed (${result}):\n${output}\n${errors}")
endif()
file(READ "${WORK}/memory-trace.json" report)
if(KEEP)
  # Opt-in copy of the numeric report for a measurement run (-DKEEP=<file>).
  file(WRITE "${KEEP}" "${report}")
endif()
string(JSON snapshot_count LENGTH "${report}" snapshots)
string(JSON visit_count LENGTH "${report}" visits)
set(expected constructed launch-idle first-opening after-use closed after-cycles)
list(LENGTH expected expected_count)
if(NOT snapshot_count EQUAL expected_count)
  message(FATAL_ERROR "Memory trace recorded ${snapshot_count} stages instead of ${expected_count}")
endif()
math(EXPR last "${snapshot_count} - 1")
set(summary "")
foreach(index RANGE ${last})
  list(GET expected ${index} name)
  string(JSON stage GET "${report}" snapshots ${index} stage)
  if(NOT stage STREQUAL name)
    message(FATAL_ERROR "Memory trace stage ${index} is ${stage}, expected ${name}")
  endif()
  string(JSON private GET "${report}" snapshots ${index} census process privateBytes)
  string(JSON working GET "${report}" snapshots ${index} census process workingSetBytes)
  string(JSON threads GET "${report}" snapshots ${index} census process threads)
  string(JSON heap GET "${report}" snapshots ${index} census heaps committedBytes)
  string(JSON raster GET "${report}" snapshots ${index} rasterCache resourceBytes)
  string(JSON entries GET "${report}" snapshots ${index} rasterCache entries)
  string(JSON gpu GET "${report}" snapshots ${index} renderer resourceBytes)
  string(JSON owners LENGTH "${report}" snapshots ${index} owners)
  if(private LESS_EQUAL 0 OR threads LESS_EQUAL 0 OR owners LESS 2)
    message(FATAL_ERROR "Memory trace stage ${stage} lacks its process census or owner attribution")
  endif()
  set(threads_by_start 0)
  string(JSON threads_by_start ERROR_VARIABLE no_threads LENGTH "${report}" snapshots ${index} census threadsByStart)
  if(NOT stage STREQUAL "first-opening" AND (no_threads OR threads_by_start LESS 1))
    message(FATAL_ERROR "Memory trace stage ${stage} lacks its thread origins")
  endif()
  math(EXPR private_mb "${private} / 1048576")
  math(EXPR working_mb "${working} / 1048576")
  math(EXPR heap_mb "${heap} / 1048576")
  math(EXPR raster_mb "${raster} / 1048576")
  math(EXPR gpu_mb "${gpu} / 1048576")
  string(APPEND summary "\n  ${stage}: private ${private_mb} MiB, working set ${working_mb} MiB, heaps ${heap_mb} MiB, raster cache ${raster_mb} MiB (${entries} entries), renderer ${gpu_mb} MiB, ${threads} threads")
  set(private_${index} ${private})
  set(threads_${index} ${threads})
  set(entries_${index} ${entries})
  set(gpu_${index} ${gpu})
endforeach()
# Every presented module was visited once (the hidden coverage presents at least 18).
if(visit_count LESS 18)
  message(FATAL_ERROR "Memory trace visited ${visit_count} modules")
endif()
# Repeat lifecycle (WINDOWS-MIGRATION.md section 9): no growth across ten more
# open/close cycles once caches settled. Stage 4 is closed, 5 after cycles.
math(EXPR private_growth "${private_5} - ${private_4}")
math(EXPR thread_growth "${threads_5} - ${threads_4}")
if(private_growth GREATER 33554432)
  message(FATAL_ERROR "Private bytes grew by ${private_growth} across ten open/close cycles")
endif()
if(thread_growth GREATER 2)
  message(FATAL_ERROR "Threads grew by ${thread_growth} across ten open/close cycles")
endif()
if(entries_5 GREATER entries_4 OR gpu_5 GREATER gpu_4)
  message(FATAL_ERROR "Retained raster entries (${entries_4} -> ${entries_5}) or renderer resources (${gpu_4} -> ${gpu_5}) grew across ten open/close cycles")
endif()
# The closed HUD gives back what only a drawn frame uses (Renderer::
# releaseFrameTargets after the overlayConcealed broadcast): no native group
# target stays resident, the frame targets are released and the driver
# trimmed its internal buffers.
foreach(index 4 5)
  list(GET expected ${index} name)
  string(JSON released GET "${report}" snapshots ${index} renderer releasedTargetBytes)
  string(JSON trimmed GET "${report}" snapshots ${index} renderer driverTrimmed)
  string(JSON groups GET "${report}" snapshots ${index} renderer nativeGroupBytes)
  if(released LESS_EQUAL 0 OR NOT trimmed OR NOT groups EQUAL 0)
    message(FATAL_ERROR "The ${name} HUD keeps its frame targets (released ${released} bytes, trimmed ${trimmed}, resident group targets ${groups} bytes)")
  endif()
endforeach()
message(STATUS "Hidden memory trace: ${snapshot_count} stages, ${visit_count} module visits${summary}")
file(REMOVE_RECURSE "${WORK}")
