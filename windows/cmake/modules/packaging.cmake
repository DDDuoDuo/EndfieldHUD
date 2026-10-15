# Packaging area: unpackaged per-user installer, portable zip, release
# staging, CI support, the section 9 performance-gate tooling and the Windows
# updater (windows/packaging/README.md). Only the updater (the windowsUpdates
# area) is linked into EndfieldHUD.exe; the installer is its own small
# program. Tests use temporary folders, a loopback HTTP fixture server and a
# private HKCU\Software\EndfieldHUD-Test-<id> subtree only: never the real
# Start menu, Run key, Classes key, notification platform, network or user data.
set(EHUD_PACKAGING_TARGETS "")

# --- Portable format, arguments, five-language Setup text, gate evaluation ----
# (perf_report: cold/warm openings from the app's startup trace and the
# readable Markdown report of the section 9 gate).
add_library(hud_packaging_format packaging/package_format.cpp packaging/setup_arguments.cpp packaging/setup_text.cpp packaging/perf_gate.cpp
  packaging/perf_report.cpp)
target_include_directories(hud_packaging_format PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_packaging_format PUBLIC hud_data hud_localization)
list(APPEND EHUD_PACKAGING_TARGETS hud_packaging_format)
if(BUILD_TESTING)
  add_executable(packaging_format_tests packaging/tests/package_format_tests.cpp)
  target_link_libraries(packaging_format_tests PRIVATE hud_packaging_format)
  add_test(NAME packaging_format_contracts COMMAND packaging_format_tests)
  add_executable(packaging_perf_gate_tests packaging/tests/perf_gate_tests.cpp)
  target_link_libraries(packaging_perf_gate_tests PRIVATE hud_packaging_format)
  add_test(NAME packaging_perf_gate_contracts COMMAND packaging_perf_gate_tests)
  list(APPEND EHUD_PACKAGING_TARGETS packaging_format_tests packaging_perf_gate_tests)
endif()

# --- Windows updater: release checker and controller (packaging/updater.hpp) --
# Portable decisions over the windows-v* GitHub releases (never the Mac feed),
# checked against the unchanged Mac HUDGitHubRelease.swift through its oracle
# (tools/package_update_reference.sh).
add_library(hud_packaging_updates packaging/update_check.cpp packaging/updater.cpp packaging/update_fixture.cpp)
target_include_directories(hud_packaging_updates PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_packaging_updates PUBLIC hud_packaging_format hud_data hud_localization)
list(APPEND EHUD_PACKAGING_TARGETS hud_packaging_updates)
if(BUILD_TESTING)
  add_executable(packaging_update_check_tests packaging/tests/update_check_tests.cpp)
  target_link_libraries(packaging_update_check_tests PRIVATE hud_packaging_updates)
  add_test(NAME packaging_update_check_contracts COMMAND packaging_update_check_tests "${CMAKE_CURRENT_SOURCE_DIR}/packaging/tests/fixtures/update-release-source.json")
  list(APPEND EHUD_PACKAGING_TARGETS packaging_update_check_tests)
endif()

# --- EHUDZ01 (Mac Watch runtime payloads): bounded decoder and verifier ------
add_library(hud_packaging_ehudz01 packaging/ehudz01.cpp)
target_include_directories(hud_packaging_ehudz01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_packaging_ehudz01 PUBLIC hud_data PRIVATE hud_packet hud_reader_zlib)
add_executable(package_watch_resources tools/package_watch_resources.cpp)
target_link_libraries(package_watch_resources PRIVATE hud_packaging_ehudz01)
list(APPEND EHUD_PACKAGING_TARGETS hud_packaging_ehudz01 package_watch_resources)
if(BUILD_TESTING)
  # Oracle: tools/package_ehudz01_reference.py over the unchanged Mac packager.
  add_executable(packaging_ehudz01_tests packaging/tests/ehudz01_tests.cpp)
  target_link_libraries(packaging_ehudz01_tests PRIVATE hud_packaging_ehudz01 hud_packet)
  add_test(NAME packaging_ehudz01_contracts COMMAND packaging_ehudz01_tests "${CMAKE_CURRENT_SOURCE_DIR}/packaging/tests/fixtures/ehudz01-source.json")
  list(APPEND EHUD_PACKAGING_TARGETS packaging_ehudz01_tests)
  # The real Watch runtime staged by the Mac packager, verified natively
  # (where the repository's Resources/ and a Python interpreter are present).
  find_package(Python3 COMPONENTS Interpreter QUIET)
  set(ehud_watch_source "${CMAKE_CURRENT_SOURCE_DIR}/../Resources/WatchSource")
  set(ehud_watch_packager "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/package-watch-resources.py")
  if(Python3_Interpreter_FOUND AND EXISTS "${ehud_watch_source}/textures.json" AND EXISTS "${ehud_watch_packager}")
    add_test(NAME packaging_watch_runtime_staged COMMAND "${CMAKE_COMMAND}" "-DPYTHON=${Python3_EXECUTABLE}" "-DSCRIPT=${ehud_watch_packager}"
      "-DSOURCE=${ehud_watch_source}" "-DTOOL=$<TARGET_FILE:package_watch_resources>" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/packaging-watch-runtime"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/run_watch_runtime_check.cmake")
    set_tests_properties(packaging_watch_runtime_staged PROPERTIES TIMEOUT 300)
  endif()
endif()

if(WIN32)
  set(EHUD_PACKAGING_DEFINITIONS _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  # SHA-256 (BCrypt), LZMS (Windows Compression API), packer and reader.
  add_library(hud_packaging_io packaging/package_io.cpp)
  target_compile_definitions(hud_packaging_io PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
  target_link_libraries(hud_packaging_io PUBLIC hud_packaging_format PRIVATE bcrypt cabinet)
  # Install/upgrade/uninstall engine. Reuses the app's launch-at-login
  # registration, data root, settings store and notification identity code.
  add_library(hud_packaging_installer packaging/installer.cpp)
  target_compile_definitions(hud_packaging_installer PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
  target_link_libraries(hud_packaging_installer PUBLIC hud_packaging_io hud_app_notification_identity hud_single_instance hud_application_core
    PRIVATE rstrtmgr ole32 shell32 advapi32 bcrypt windowsapp)
  list(APPEND EHUD_PACKAGING_TARGETS hud_packaging_io hud_packaging_installer)

  # EndfieldHUD-Setup.exe (stub; the release step appends the payload) and
  # uninstall.exe, from one source with the version of source-authority.json.
  set(EHUD_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/app/EndfieldHUD.ico")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/source-authority.json" ehud_packaging_authority)
  string(JSON EHUD_APP_RELEASE GET "${ehud_packaging_authority}" release)
  string(JSON EHUD_APP_BUILD GET "${ehud_packaging_authority}" build)
  string(JSON EHUD_APP_COMMIT GET "${ehud_packaging_authority}" commit)
  string(REGEX MATCH "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)$" ehud_packaging_version "${EHUD_APP_RELEASE}")
  set(EHUD_APP_VERSION_COMMAS "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},${EHUD_APP_BUILD}")
  set(EHUD_APP_VERSION_DOTS "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}.${EHUD_APP_BUILD}")
  set(ehud_update_release "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
  foreach(role IN ITEMS setup uninstall)
    if(role STREQUAL "setup")
      set(EHUD_SETUP_NAME "EndfieldHUD-Setup")
      set(EHUD_SETUP_FILE "EndfieldHUD-Setup.exe")
      set(EHUD_SETUP_DESCRIPTION "EndfieldHUD Setup")
      set(ehud_target endfieldhud_setup_stub)
      set(ehud_output "EndfieldHUD-Setup-stub")
      set(ehud_role EHUD_SETUP_ROLE_INSTALL)
    else()
      set(EHUD_SETUP_NAME "EndfieldHUD-Uninstall")
      set(EHUD_SETUP_FILE "uninstall.exe")
      set(EHUD_SETUP_DESCRIPTION "Uninstall EndfieldHUD")
      set(ehud_target endfieldhud_uninstaller)
      set(ehud_output "uninstall")
      set(ehud_role EHUD_SETUP_ROLE_UNINSTALL)
    endif()
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/setup.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/packaging/${role}.rc" @ONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/packaging/setup.manifest.in" "${CMAKE_CURRENT_BINARY_DIR}/packaging/${role}.manifest" @ONLY)
    add_executable(${ehud_target} WIN32 packaging/setup_main.cpp "${CMAKE_CURRENT_BINARY_DIR}/packaging/${role}.rc"
      "${CMAKE_CURRENT_BINARY_DIR}/packaging/${role}.manifest")
    set_target_properties(${ehud_target} PROPERTIES OUTPUT_NAME "${ehud_output}")
    target_compile_definitions(${ehud_target} PRIVATE ${EHUD_PACKAGING_DEFINITIONS} ${ehud_role})
    target_link_libraries(${ehud_target} PRIVATE hud_packaging_installer comctl32 shell32)
    list(APPEND EHUD_PACKAGING_TARGETS ${ehud_target})
  endforeach()

  # Release packer and size report (windows/tools/package_payload.cpp).
  add_executable(package_payload tools/package_payload.cpp)
  target_compile_definitions(package_payload PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
  target_link_libraries(package_payload PRIVATE hud_packaging_io)
  list(APPEND EHUD_PACKAGING_TARGETS package_payload)

  # Windows updater services (WinHTTP, installed-copy check, verification,
  # Setup hand-off) and the windowsUpdates area in EndfieldHUD.exe.
  add_library(hud_packaging_update_platform packaging/update_platform.cpp)
  target_compile_definitions(hud_packaging_update_platform PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
  target_link_libraries(hud_packaging_update_platform PUBLIC hud_packaging_updates hud_packaging_io PRIVATE winhttp wintrust crypt32 advapi32 user32)
  add_library(hud_windows_updates_area STATIC app/areas/update_module.cpp)
  target_include_directories(hud_windows_updates_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_windows_updates_area PRIVATE ${EHUD_PACKAGING_DEFINITIONS}
    "EHUD_UPDATE_RELEASE=\"${ehud_update_release}\"" "EHUD_UPDATE_BUILD=${EHUD_APP_BUILD}")
  target_link_libraries(hud_windows_updates_area PUBLIC hud_module_registry hud_packaging_update_platform PRIVATE hud_utility user32)
  list(APPEND EHUD_PACKAGING_TARGETS hud_packaging_update_platform hud_windows_updates_area)
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "windowsUpdates|hud_windows_updates_area|app/areas/update_module.hpp|endfield::areas::windowsUpdatesModule")

  # Section 9 performance gates against EndfieldHUD.exe (temporary data root),
  # measured from outside: windows/tools/package_perf_gate.cpp. The real run
  # shows the HUD, so no test, CI step or build target ever starts it against
  # EndfieldHUD.exe: the user runs "package_perf_gate --desktop" on the
  # laptop's desktop (windows/packaging/README.md, Performance gates).
  add_library(hud_packaging_perf packaging/perf_probe.cpp packaging/perf_runner.cpp)
  target_compile_definitions(hud_packaging_perf PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
  target_link_libraries(hud_packaging_perf PUBLIC hud_packaging_io hud_application_core PRIVATE pdh psapi dxgi version rstrtmgr bcrypt advapi32 user32)
  add_executable(package_perf_gate tools/package_perf_gate.cpp)
  target_compile_definitions(package_perf_gate PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
  target_link_libraries(package_perf_gate PRIVATE hud_packaging_perf user32)
  list(APPEND EHUD_PACKAGING_TARGETS hud_packaging_perf package_perf_gate)

  # Release staging: install tree -> portable zip + setup + size report +
  # SHA256SUMS in <build>/packaging/dist (never a GitHub release).
  set(EHUD_RELEASE_ALLOW_INCOMPLETE OFF CACHE BOOL "Stage a developer package even when Mac-exported runtime inputs are missing")
  set(EHUD_SIGN_CERTIFICATE_SHA1 "" CACHE STRING "Thumbprint of DDDuoDuo's code-signing certificate (empty: unsigned)")
  set(EHUD_SIGN_TIMESTAMP_URL "" CACHE STRING "RFC 3161 timestamp server for signtool")
  set(ehud_release_command "${CMAKE_COMMAND}" "-DBUILD=${CMAKE_BINARY_DIR}" "-DCONFIG=$<CONFIG>" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/packaging/dist" "-DPACKER=$<TARGET_FILE:package_payload>" "-DSTUB=$<TARGET_FILE:endfieldhud_setup_stub>"
    "-DUNINSTALLER=$<TARGET_FILE:endfieldhud_uninstaller>" "-DSYMBOLS=$<TARGET_PDB_FILE:EndfieldHUD>" "-DALLOW_INCOMPLETE=${EHUD_RELEASE_ALLOW_INCOMPLETE}"
    "-DSIGN_SHA1=${EHUD_SIGN_CERTIFICATE_SHA1}" "-DSIGN_TIMESTAMP=${EHUD_SIGN_TIMESTAMP_URL}" -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/stage_release.cmake")
  if(TARGET EndfieldHUD)
    add_custom_target(endfieldhud_release COMMAND ${ehud_release_command} COMMENT "Staging EndfieldHUD release artifacts" VERBATIM USES_TERMINAL)
    add_dependencies(endfieldhud_release EndfieldHUD endfieldhud_resources endfieldhud_setup_stub endfieldhud_uninstaller package_payload)
  endif()

  if(BUILD_TESTING)
    # Stand-in EndfieldHUD.exe (session-end protocol, restart registration,
    # performance-gate fixture modes); never the real app.
    add_executable(packaging_fixture_app WIN32 packaging/tests/package_fixture_app.cpp)
    target_compile_definitions(packaging_fixture_app PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
    target_link_libraries(packaging_fixture_app PRIVATE shell32 gdi32 user32)
    list(APPEND EHUD_PACKAGING_TARGETS packaging_fixture_app)
    # Synthetic payloads through the real setup and uninstall programs.
    add_executable(packaging_installer_tests packaging/tests/package_installer_tests.cpp)
    target_compile_definitions(packaging_installer_tests PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
    target_link_libraries(packaging_installer_tests PRIVATE hud_packaging_installer ole32 advapi32)
    add_dependencies(packaging_installer_tests endfieldhud_setup_stub endfieldhud_uninstaller packaging_fixture_app)
    add_test(NAME packaging_installer_contracts COMMAND packaging_installer_tests "$<TARGET_FILE:endfieldhud_setup_stub>"
      "$<TARGET_FILE:endfieldhud_uninstaller>" "$<TARGET_FILE:packaging_fixture_app>")
    set_tests_properties(packaging_installer_contracts PROPERTIES TIMEOUT 600)
    list(APPEND EHUD_PACKAGING_TARGETS packaging_installer_tests)
    # The gate runner and tool against the stand-in app (never EndfieldHUD).
    add_executable(packaging_perf_probe_tests packaging/tests/perf_probe_tests.cpp)
    target_compile_definitions(packaging_perf_probe_tests PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
    target_link_libraries(packaging_perf_probe_tests PRIVATE hud_packaging_perf)
    add_dependencies(packaging_perf_probe_tests packaging_fixture_app package_perf_gate)
    add_test(NAME packaging_perf_probe_contracts COMMAND packaging_perf_probe_tests "$<TARGET_FILE:packaging_fixture_app>" "$<TARGET_FILE:package_perf_gate>")
    set_tests_properties(packaging_perf_probe_contracts PROPERTIES TIMEOUT 300)
    add_test(NAME packaging_perf_gate_tool COMMAND package_perf_gate --target fixture --exe "$<TARGET_FILE:packaging_fixture_app>"
      --report "${CMAKE_CURRENT_BINARY_DIR}/packaging/perf-gate-fixture.json" --markdown "${CMAKE_CURRENT_BINARY_DIR}/packaging/perf-gate-fixture.md"
      --seconds 2 --interval 0.25 --settle 0.3 --open-settle 0.05
      --close-settle 0.05 --cycles 20 --limit lifecycle.warmup=4 --limit lifecycle.window=5 --limit closed.cpu=2 --limit open.cpu=2 --limit open.wakeups=20
      --limit cold-open.first-frame=100 --limit closed.threads=32 --limit open.handles=4000 --limit after-cycles.private=67108864
      --limit after-cycles.working-set=none)
    set_tests_properties(packaging_perf_gate_tool PROPERTIES TIMEOUT 300 FIXTURES_SETUP packaging_perf_gate_report
      PASS_REGULAR_EXPRESSION "  openings +PASS  cold: first frame [0-9]+ ms" FAIL_REGULAR_EXPRESSION "performance gate: (FAILED|cancelled)|package_perf_gate: ")
    # The saved JSON report rendered again (--render), without running anything.
    add_test(NAME packaging_perf_gate_render COMMAND package_perf_gate --render "${CMAKE_CURRENT_BINARY_DIR}/packaging/perf-gate-fixture.json"
      --markdown "${CMAKE_CURRENT_BINARY_DIR}/packaging/perf-gate-fixture-rendered.md")
    set_tests_properties(packaging_perf_gate_render PROPERTIES FIXTURES_REQUIRED packaging_perf_gate_report
      PASS_REGULAR_EXPRESSION "Readable report: .*perf-gate-fixture-rendered.md")
    add_test(NAME packaging_perf_gate_refuses_unsupported COMMAND package_perf_gate --exe "$<TARGET_FILE:packaging_fixture_app>"
      --report "${CMAKE_CURRENT_BINARY_DIR}/packaging/perf-gate-refused.json")
    set_tests_properties(packaging_perf_gate_refuses_unsupported PROPERTIES PASS_REGULAR_EXPRESSION "no --data-root/--toggle support")
    list(APPEND EHUD_PACKAGING_TARGETS packaging_perf_probe_tests)
    # WinHTTP against a loopback fixture server, the installed-copy check in a
    # private HKCU subtree, verification of synthetic setups and the Setup
    # launcher with a stand-in Setup (never the real installer or network).
    add_executable(packaging_update_setup_fixture WIN32 packaging/tests/update_setup_fixture.cpp)
    target_compile_definitions(packaging_update_setup_fixture PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
    target_link_libraries(packaging_update_setup_fixture PRIVATE shell32)
    add_executable(packaging_update_platform_tests packaging/tests/update_platform_tests.cpp)
    target_compile_definitions(packaging_update_platform_tests PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
    target_link_libraries(packaging_update_platform_tests PRIVATE hud_packaging_update_platform winhttp ws2_32 advapi32 user32 ole32 shell32)
    add_dependencies(packaging_update_platform_tests packaging_update_setup_fixture)
    add_test(NAME packaging_update_platform_contracts COMMAND packaging_update_platform_tests "$<TARGET_FILE:packaging_update_setup_fixture>")
    set_tests_properties(packaging_update_platform_contracts PROPERTIES TIMEOUT 300)
    list(APPEND EHUD_PACKAGING_TARGETS packaging_update_setup_fixture packaging_update_platform_tests)
    # The OS ICU and time-zone data the date and text suites compare against
    # (CI runs the same tool before ctest): a clear failure on an older OS.
    add_executable(package_os_capabilities tools/package_os_capabilities.cpp)
    target_compile_definitions(package_os_capabilities PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
    target_link_libraries(package_os_capabilities PRIVATE icu)
    add_test(NAME packaging_os_text_data_contracts COMMAND package_os_capabilities
      --requirements "${CMAKE_CURRENT_SOURCE_DIR}/packaging/os-requirements.json")
    list(APPEND EHUD_PACKAGING_TARGETS package_os_capabilities)
    if(TARGET EndfieldHUD)
      # The real install tree staged, packed, installed and removed in
      # temporary folders (complete when the Mac-exported inputs are configured).
      set(ehud_release_complete OFF)
      if(ehud_app_inputs AND EXISTS "${ehud_app_inputs}/notes-controls-assets")
        set(ehud_release_complete ON)
      endif()
      add_test(NAME endfieldhud_release_contracts COMMAND "${CMAKE_COMMAND}" "-DBUILD=${CMAKE_BINARY_DIR}" "-DCONFIG=$<CONFIG>"
        "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/packaging-release-check" "-DPACKER=$<TARGET_FILE:package_payload>"
        "-DSTUB=$<TARGET_FILE:endfieldhud_setup_stub>" "-DUNINSTALLER=$<TARGET_FILE:endfieldhud_uninstaller>" "-DSYMBOLS=$<TARGET_PDB_FILE:EndfieldHUD>"
        "-DREQUIRE_COMPLETE=${ehud_release_complete}" "-DCHECKER=$<TARGET_FILE:packaging_installer_tests>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/run_release_check.cmake")
      set_tests_properties(endfieldhud_release_contracts PROPERTIES TIMEOUT 900)
      add_dependencies(packaging_installer_tests EndfieldHUD package_payload)
    endif()
  endif()

  # The installer's notification identity must equal the one the Calendar
  # reminders area looks up (native/calendar_notification_identity.hpp).
  if(BUILD_TESTING AND TARGET hud_calendar_notification_identity)
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/packaging/identity_consistency.cpp" CONTENT
      "#include \"native/calendar_notification_identity.hpp\"\n#include \"packaging/installer.hpp\"\n\
static_assert(endfield::native::calendarNotificationAppUserModelID == endfield::packaging::installedAppUserModelID);\n\
static_assert(endfield::native::calendarNotificationActivatorCLSID == endfield::packaging::installedActivatorCLSID);\n\
int main() { return endfield::native::calendarNotificationShortcutName == \"EndfieldHUD.lnk\" ? 0 : 1; }\n" @ONLY)
    add_executable(packaging_identity_consistency "${CMAKE_CURRENT_BINARY_DIR}/packaging/identity_consistency.cpp")
    target_compile_definitions(packaging_identity_consistency PRIVATE ${EHUD_PACKAGING_DEFINITIONS})
    target_link_libraries(packaging_identity_consistency PRIVATE hud_calendar_notification_identity hud_packaging_installer)
    add_test(NAME packaging_identity_consistency COMMAND packaging_identity_consistency)
    list(APPEND EHUD_PACKAGING_TARGETS packaging_identity_consistency)
  endif()
endif()

foreach(target IN LISTS EHUD_PACKAGING_TARGETS)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()
