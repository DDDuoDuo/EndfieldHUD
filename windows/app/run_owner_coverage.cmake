# Registered hidden integration test over the production Application owner:
# runs watch_session_preview with the validator's --module-coverage arguments
# in a NEW owned work directory, then requires the report's production
# lifecycle/quit section. With PACKAGE (the build's staged EndfieldHUD
# resources) the module areas resolve the verified package; every area named
# in EHUD_EXPECTED_AREAS (set by cmake/modules/app.cmake) must then be
# constructed and covered. Never shows a window or touches user data.
cmake_minimum_required(VERSION 3.25)
foreach(required PREVIEW WORK INPUTS SOURCE CHECKS)
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
  --benchmark "${WORK}/module-coverage.json" --compiled-sha 64852432949d3354e50c43ebb983cfa5afcf5dae29a4512879aca974902a9ba8
  --notes-assets "${INPUTS}/notes-controls-assets" --notes-assets-sha 9d2fcf0eead9eeeab1be4fdbb0e940a0e1e3eedf840ab9e4c63499e40ae70c00
  --notes-data "${WORK}/notes-data" --notes-format-assets "${resources}/notes-format"
  --shelf-assets "${resources}/shelf" --shelf-data "${WORK}/shelf-data" --shelf-mask "${resources}/shelf/reveal-mask.bin"
  --clipboard-assets "${resources}/clipboard" --work-mode-fixture --volume-fixture --event-log-fixture --battery-fixture
  --settings-assets "${resources}" --archive-assets "${resources}" --storage-assets "${resources}" --activity-assets "${resources}/activity"
  ${EXTRA} ${package_arguments}
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
# Host integration (UI Automation root over Volume, restart service, drop route).
string(JSON host_checks GET "${report}" hostIntegrationChecks)
if(host_checks LESS 7)
  message(FATAL_ERROR "Host integration coverage incomplete: ${host_checks} checks")
endif()
# Module areas: the modules they present ran the same three combined cycles,
# and every registered area is reported (constructed and covered when the
# staged package was supplied).
string(JSON area_count LENGTH "${report}" areaCoverage)
string(JSON area_results LENGTH "${report}" areaModuleResults)
string(JSON area_modules LENGTH "${report}" areaPresentedModules)
math(EXPR area_expected "${area_modules} * 3")
if(NOT area_results EQUAL area_expected)
  message(FATAL_ERROR "Area module cycles incomplete: ${area_results} results for ${area_modules} presented modules")
endif()
set(expected_areas "$ENV{EHUD_EXPECTED_AREAS}")
string(REPLACE "," ";" expected_areas "${expected_areas}")
foreach(name IN LISTS expected_areas)
  set(found FALSE)
  if(area_count GREATER 0)
    math(EXPR area_last "${area_count} - 1")
    foreach(index RANGE ${area_last})
      string(JSON reported GET "${report}" areaCoverage ${index} area)
      if(reported STREQUAL name)
        set(found TRUE)
        string(JSON constructed GET "${report}" areaCoverage ${index} constructed)
        if(PACKAGE AND NOT constructed)
          message(FATAL_ERROR "Module area ${name} was not constructed with the staged package")
        endif()
        if(constructed)
          string(JSON steps GET "${report}" areaCoverage ${index} coverageSteps)
          if(steps LESS 3)
            message(FATAL_ERROR "Module area ${name} received ${steps} coverage steps")
          endif()
        endif()
      endif()
    endforeach()
  endif()
  if(NOT found)
    message(FATAL_ERROR "Registered module area ${name} is missing from the coverage report")
  endif()
endforeach()
list(LENGTH expected_areas expected_count)
string(JSON id_card GET "${report}" idCardConnected)
message(STATUS "Owner module coverage: ${modules} module results, ${lifecycle} production lifecycle checks, ${host_checks} host integration checks (ID card connected: ${id_card}), ${expected_count} registered areas (${area_results} area module results)")
file(REMOVE_RECURSE "${WORK}")
