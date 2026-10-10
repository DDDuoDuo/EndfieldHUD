# Local modules area fragment: Reader, Map, Calendar and the minigame
# (included after every target in windows/CMakeLists.txt). The four HUD
# modules themselves are built-in owners of app/application_modules.cpp; this
# fragment adds what they need beyond that owner. Tests use synthetic inputs,
# temporary folders and private volatile registry subtrees only: no user data,
# Start-menu entry, HKCU\Software\Classes value, COM class registration,
# process identity or scheduled notification.
set(EHUD_LOCAL_TARGETS "")

# --- Reader display-time localization -----------------------------------------
# Mac Reader captions/ReaderError pairs through the shared five-language
# catalog plus the Windows-only chooser/picker/import rows. Portable; the
# Windows Reader owner renders typed errors through it.
add_library(hud_reader_localization modules/reader_localization.cpp)
target_include_directories(hud_reader_localization PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_reader_localization PUBLIC hud_reader hud_localization)
if(TARGET hud_reader_preview)
  target_link_libraries(hud_reader_preview PUBLIC hud_reader_localization)
endif()
list(APPEND EHUD_LOCAL_TARGETS hud_reader_localization)
if(BUILD_TESTING)
  # tools/reader_localization_reference.py -> reader-localization-source.json;
  # also the typed ReaderState error and its confirmed-write Event Log actions.
  add_executable(reader_localization_tests tests/reader_localization_tests.cpp)
  target_link_libraries(reader_localization_tests PRIVATE hud_reader_localization)
  add_test(NAME reader_localization_contracts COMMAND reader_localization_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/reader-localization-source.json)
  list(APPEND EHUD_LOCAL_TARGETS reader_localization_tests)
endif()

# --- Calendar reminder identity (unpackaged per-user install) -----------------
# The installer-owned Start-menu AppUserModelID/toast activator of this
# executable, its read-only runtime lookup and the uninstall order
# (native/calendar_notification_identity.hpp).
add_library(hud_calendar_notification_identity native/calendar_notification_identity.cpp)
target_include_directories(hud_calendar_notification_identity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_calendar_notification_identity PUBLIC hud_app_notification_identity hud_calendar_notifications)
# The native provider schedules under the process identity the area
# establishes (GetCurrentProcessExplicitAppUserModelID).
target_link_libraries(hud_calendar_notifications PRIVATE hud_app_notification_identity)
if(WIN32)
  target_compile_definitions(hud_calendar_notification_identity PRIVATE _WIN32_WINNT=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_calendar_notification_identity PRIVATE ole32 shell32 advapi32)
  target_link_libraries(hud_calendar_notifications PRIVATE shell32)
endif()
list(APPEND EHUD_LOCAL_TARGETS hud_calendar_notification_identity)
if(BUILD_TESTING)
  add_executable(calendar_notification_identity_tests tests/calendar_notification_identity_tests.cpp)
  target_link_libraries(calendar_notification_identity_tests PRIVATE hud_calendar_notification_identity)
  if(WIN32)
    target_compile_definitions(calendar_notification_identity_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(calendar_notification_identity_tests PRIVATE ole32 advapi32)
  endif()
  add_test(NAME calendar_notification_identity_contracts COMMAND calendar_notification_identity_tests)
  list(APPEND EHUD_LOCAL_TARGETS calendar_notification_identity_tests)
endif()

if(WIN32)
  # Installer step (install/remove/inspect for an absolute EndfieldHUD.exe);
  # the registered test only proves it is inert without a command.
  add_executable(calendar_notification_setup tools/calendar_notification_setup.cpp)
  target_compile_definitions(calendar_notification_setup PRIVATE _WIN32_WINNT=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(calendar_notification_setup PRIVATE hud_calendar_notification_identity ole32 advapi32)
  list(APPEND EHUD_LOCAL_TARGETS calendar_notification_setup)
  if(BUILD_TESTING)
    add_test(NAME calendar_notification_setup_inert COMMAND calendar_notification_setup)
    set_tests_properties(calendar_notification_setup_inert PROPERTIES PASS_REGULAR_EXPRESSION "inert without a command")
  endif()

  # Calendar reminders area (app/module_owner.hpp, "Adding a module area"):
  # the installed identity, the toast activator on the HUD's STA and click
  # routing, registered for EndfieldHUD.exe and the hidden coverage harness.
  add_library(hud_calendar_reminder_area STATIC app/areas/calendar_reminder_module.cpp)
  target_include_directories(hud_calendar_reminder_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_calendar_reminder_area PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_calendar_reminder_area PUBLIC hud_module_registry hud_calendar_notification_identity PRIVATE ole32 user32)
  list(APPEND EHUD_LOCAL_TARGETS hud_calendar_reminder_area)
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "calendarReminders|hud_calendar_reminder_area|app/areas/calendar_reminder_module.hpp|endfield::areas::calendarRemindersModule")
  if(BUILD_TESTING)
    add_executable(calendar_reminder_area_tests tests/calendar_reminder_area_tests.cpp)
    target_compile_definitions(calendar_reminder_area_tests PRIVATE _WIN32_WINNT=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(calendar_reminder_area_tests PRIVATE hud_calendar_reminder_area hud_native hud_utility ole32 user32)
    add_test(NAME calendar_reminder_area_contracts COMMAND calendar_reminder_area_tests)
    list(APPEND EHUD_LOCAL_TARGETS calendar_reminder_area_tests)
  endif()
endif()

foreach(target IN LISTS EHUD_LOCAL_TARGETS)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()
