# Registered hidden integration test over the production Application owner:
# runs watch_session_preview with the validator's --module-coverage arguments
# in a NEW owned work directory, then requires the report's production
# lifecycle/quit section. Never shows a window or touches user data.
cmake_minimum_required(VERSION 3.25)
foreach(required PREVIEW WORK INPUTS SOURCE CHECKS)
  if(NOT ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(resources "${SOURCE}/resources")
execute_process(COMMAND "${PREVIEW}" "${INPUTS}/runtime-compiled-animation" "${INPUTS}/profile-outline-catalog.ehscene" "${SOURCE}/native/hud.hlsl"
  --runtime-input --module-coverage --reader --calendar
  --map-geography "${MAP}" --map-player-assets "${resources}/map-player" --orbipom-assets "${ORBIPOM}"
  --benchmark "${WORK}/module-coverage.json" --compiled-sha 64852432949d3354e50c43ebb983cfa5afcf5dae29a4512879aca974902a9ba8
  --notes-assets "${INPUTS}/notes-controls-assets" --notes-assets-sha 9d2fcf0eead9eeeab1be4fdbb0e940a0e1e3eedf840ab9e4c63499e40ae70c00
  --notes-data "${WORK}/notes-data" --notes-format-assets "${resources}/notes-format"
  --shelf-assets "${resources}/shelf" --shelf-data "${WORK}/shelf-data" --shelf-mask "${resources}/shelf/reveal-mask.bin"
  --clipboard-assets "${resources}/clipboard" --work-mode-fixture --volume-fixture --event-log-fixture --battery-fixture
  --settings-assets "${resources}" --archive-assets "${resources}" --storage-assets "${resources}" --activity-assets "${resources}/activity"
  ${EXTRA}
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 500)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Owner module coverage failed (${result}):\n${output}\n${errors}")
endif()
file(READ "${WORK}/module-coverage.json" report)
string(JSON modules LENGTH "${report}" moduleResults)
string(JSON lifecycle GET "${report}" productionLifecycleChecks)
# Every module cycle and every production lifecycle/quit assertion ran.
if(NOT modules EQUAL 54 OR NOT lifecycle EQUAL CHECKS)
  message(FATAL_ERROR "Owner coverage incomplete: ${modules} module results, ${lifecycle} lifecycle checks")
endif()
message(STATUS "Owner module coverage: ${modules} module results, ${lifecycle} production lifecycle checks")
file(REMOVE_RECURSE "${WORK}")
