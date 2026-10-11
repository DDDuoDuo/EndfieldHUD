# CTest packaging_watch_runtime_staged: stages the repository's Watch runtime
# with the UNCHANGED macOS packager (scripts/package-watch-resources.py) into
# a fresh build folder and verifies every EHUDZ01 payload natively
# (package_watch_resources verify), then removes the folder.
cmake_minimum_required(VERSION 3.25)
foreach(required PYTHON SCRIPT SOURCE TOOL WORK)
  if(NOT ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
# UTF-8 mode: the packager reads its JSON with Python's default text
# encoding, which is UTF-8 on macOS (where it runs for the Mac app) but the
# ANSI code page on Windows (cp1252 cannot decode Scene/scene.json).
execute_process(COMMAND "${PYTHON}" -I -X utf8 "${SCRIPT}" stage "${SOURCE}" "${WORK}/WatchSource" RESULT_VARIABLE staged OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT staged EQUAL 0)
  message(FATAL_ERROR "The macOS packager failed (${staged}):\n${output}\n${errors}")
endif()
message(STATUS "${output}")
execute_process(COMMAND "${TOOL}" verify "${WORK}/WatchSource" RESULT_VARIABLE verified OUTPUT_VARIABLE report ERROR_VARIABLE problems)
file(REMOVE_RECURSE "${WORK}")
if(NOT verified EQUAL 0)
  message(FATAL_ERROR "The staged Watch runtime does not verify natively (${verified}):\n${report}\n${problems}")
endif()
message(STATUS "${report}")
