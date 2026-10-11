# Temporary File Shelf area fragment (included after every target in
# windows/CMakeLists.txt). The Shelf owner itself (tools/shelf_preview.cpp,
# hud_shelf_preview) is a built-in production owner of app/application.cpp;
# its libraries and original suites are declared in CMakeLists.txt. This
# fragment adds what the Windows-only Shell services need, the hidden-coverage
# area and the owner-level services suite. Tests use synthetic files in new
# temporary directories, hidden windows and injected handlers only: no user
# files, clipboard, Explorer window or preview handler of the user is touched.

if(WIN32)
  # Shell type names (SHGetFileInfoW SHGFI_TYPENAME|SHGFI_USEFILEATTRIBUTES)
  # and identity relocation (OpenFileById) of shelf references.
  target_link_libraries(hud_shelf_files PRIVATE shell32)
  # Outgoing Shell drag images (IDragSourceHelper::InitializeFromBitmap on a
  # DIB section) and incoming ones (IDropTargetHelper).
  target_link_libraries(hud_shelf_transfer PRIVATE gdi32 shell32)
  # Ctrl+V paste (OleGetClipboard) and the preview's date formatting.
  target_link_libraries(hud_shelf_preview PRIVATE ole32 shell32)
  # Preview containment: the Shell preview controller runs on a dedicated STA
  # thread with its own pump and a one-shot watchdog (CoCancelCall), next to
  # the controller in the same library.
  target_sources(hud_shelf_icons PRIVATE native/shelf_preview_thread.cpp)
  # UI Automation peers of the Shelf (Mac HUDShelfActionButton) and the relink
  # of Mac-imported Shelf items through the import ledger (core/migration).
  target_sources(hud_shelf_preview PRIVATE native/shelf_automation.cpp)
  target_link_libraries(hud_shelf_preview PRIVATE uiautomationcore oleaut32 hud_mac_import)

  # Hidden-coverage area (app/module_owner.hpp, "Adding a module area"): drives
  # the production Shelf class with synthetic files in the harness, given the
  # staged package's common.shelf artwork. Never linked into EndfieldHUD.exe,
  # whose Shelf is the built-in owner.
  add_library(hud_shelf_area STATIC app/areas/shelf_module.cpp)
  target_include_directories(hud_shelf_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_shelf_area PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_shelf_area PUBLIC hud_module_registry hud_shelf_preview hud_shelf_files hud_data PRIVATE hud_mac_import uiautomationcore oleaut32 user32)
  if(MSVC)
    target_compile_options(hud_shelf_area PRIVATE /W4 /permissive- /EHsc)
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "fileShelfCoverage|hud_shelf_area|app/areas/shelf_module.hpp|endfield::shelf::shelfCoverageModule|coverage")

  if(BUILD_TESTING)
    # Owner-level Shelf services: Event Log records (picker/paste/drop alike),
    # stale-path relocation saved by refresh, the generic preview on the
    # preview thread, virtual-file drops, localized platform text and error
    # descriptions, the UI Automation peers and the relink of Mac-imported items.
    add_executable(shelf_services_tests tests/shelf_services_tests.cpp)
    target_compile_definitions(shelf_services_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(shelf_services_tests PRIVATE hud_shelf_preview hud_shelf_files hud_mac_import ole32 shell32 uiautomationcore oleaut32)
    if(MSVC)
      target_compile_options(shelf_services_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME shelf_services_contracts COMMAND shelf_services_tests "${CMAKE_CURRENT_SOURCE_DIR}/resources/shelf")
    set_tests_properties(shelf_services_contracts PROPERTIES TIMEOUT 180)

    # The contained preview thread: lifecycle, progress, hangs outside COM and
    # a hung out-of-process COM server (a hidden child copy of the test).
    add_executable(shelf_preview_thread_tests tests/shelf_preview_thread_tests.cpp)
    target_compile_definitions(shelf_preview_thread_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(shelf_preview_thread_tests PRIVATE hud_shelf_icons ole32)
    if(MSVC)
      target_compile_options(shelf_preview_thread_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME shelf_preview_thread_contracts COMMAND shelf_preview_thread_tests)
    set_tests_properties(shelf_preview_thread_contracts PROPERTIES TIMEOUT 240)
  endif()
endif()
