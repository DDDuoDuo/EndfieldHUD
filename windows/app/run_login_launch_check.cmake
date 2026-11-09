# A launch at sign-in before the user's desktop takes input (the Run key's
# "EndfieldHUD.exe --login" at an early sign-in, or behind the lock screen
# after an update restart): GetCursorPos fails there, and 1.3.0 stopped with
# "EndfieldHUD stopped because of an error" while choosing its display.
#
# The hidden SSH test session (session 0) has no input desktop either, so it
# stands in for that moment: EndfieldHUD.exe --data-root <new temporary root>
# --login must get past choosing its display. Session 0 has no compositor, so
# the run then stops at the composition swap chain and shows its error box
# where nobody sees it; the run is ended at the timeout and only the recorded
# fatal stage is checked. An isolated root registers no login item, no
# restart and no update. With an input desktop (any other session) the check
# is skipped: the app would start for real.
cmake_minimum_required(VERSION 3.25)
foreach(required APP WORK)
  if(NOT ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
execute_process(COMMAND powershell -NoProfile -NonInteractive -Command "(Get-Process -Id $PID).SessionId"
  OUTPUT_VARIABLE session OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE probed)
if(NOT probed EQUAL 0 OR NOT session STREQUAL "0")
  message("Session '${session}' has an input desktop: login launch check skipped")
  return()
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${APP}" --data-root "${WORK}" --login WORKING_DIRECTORY "$ENV{SystemRoot}/System32"
  TIMEOUT 20 RESULT_VARIABLE result)
set(record "${WORK}/v1/Diagnostics/last-fatal.txt")
if(EXISTS "${record}")
  file(READ "${record}" fatal)
  if(fatal MATCHES "Cannot locate the current display|No usable display")
    message(FATAL_ERROR "A launch without an input desktop stopped while choosing its display (${result}):\n${fatal}")
  endif()
  string(REGEX MATCH "message: [^\n]*" reached "${fatal}")
  message(STATUS "Login launch got past choosing its display; it stopped later (${reached})")
else()
  message(STATUS "Login launch ran without a fatal record (${result})")
endif()
file(REMOVE_RECURSE "${WORK}")
