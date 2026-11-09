# The relink ledger (<app root>/Migration/relink.json,
# core/migration/relink_ledger.hpp): the external references an earlier Mac
# import listed for an explicit relink on Windows. Notes, Archive, Reader, App
# shortcuts and the File Shelf resolve, dismiss and reconcile their rows
# through its owner API; other fragments link it by name.
add_library(hud_relink_ledger core/migration/relink_ledger.cpp)
target_include_directories(hud_relink_ledger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_relink_ledger PUBLIC hud_data hud_reader)
if(MSVC)
  target_compile_options(hud_relink_ledger PRIVATE /W4 /permissive- /EHsc)
  target_compile_definitions(hud_relink_ledger PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  target_compile_options(hud_relink_ledger PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  # The owner API (resolve/dismiss/reconcile by store and record id) over
  # synthetic ledgers, including concurrent writers.
  add_executable(relink_ledger_tests tests/relink_ledger_tests.cpp)
  target_link_libraries(relink_ledger_tests PRIVATE hud_relink_ledger Threads::Threads)
  if(MSVC)
    target_compile_definitions(relink_ledger_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(relink_ledger_tests PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(relink_ledger_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME relink_ledger_contracts COMMAND relink_ledger_tests)
endif()
