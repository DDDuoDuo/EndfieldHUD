# File Shelf fragment (included after every target in windows/CMakeLists.txt).
# The canvas is portable (modules/file_shelf_canvas.*, hud_file_shelf in
# CMakeLists.txt: Mac 1.3.0 FileShelfCanvas on the design library). Its
# platform service (tools/shelf_owner.*: the reference store, bookmark-like
# identity renewal, the picker, OLE drops and the copy-only drag-out with Shell
# drag images, the Shell icon worker, the properties reads on the shared
# utility executor, the import-ledger relink, Event Log records) is a built-in
# production owner of app/application.cpp, a tools::DesignModuleOwner. Tests
# use synthetic files in new temporary directories, hidden windows and
# injected handlers only: no user files, clipboard, Explorer window or picker
# of the user is touched.

# Portable contracts of the canvas (Swift-derived rules) and, node by node, the
# Mac oracle: tools/file_shelf_reference.sh (unchanged Mac Sources, the real
# FileShelfStore over synthetic files) -> tests/fixtures/file-shelf-source.json.
if(BUILD_TESTING)
  add_executable(file_shelf_canvas_tests tests/file_shelf_canvas_tests.cpp)
  target_link_libraries(file_shelf_canvas_tests PRIVATE hud_file_shelf)
  if(MSVC)
    target_compile_options(file_shelf_canvas_tests PRIVATE /W4 /permissive- /EHsc)
  endif()
  add_test(NAME file_shelf_canvas_contracts COMMAND file_shelf_canvas_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/file-shelf-source.json")
endif()

if(WIN32)
  # Shell type names (SHGetFileInfoW SHGFI_TYPENAME|SHGFI_USEFILEATTRIBUTES)
  # and identity relocation (OpenFileById) of shelf references.
  target_link_libraries(hud_shelf_files PRIVATE shell32)
  # Outgoing Shell drag images (IDragSourceHelper::InitializeFromBitmap on a
  # DIB section) and incoming ones (IDropTargetHelper).
  target_link_libraries(hud_shelf_transfer PRIVATE gdi32 shell32)

  # The service and its owner: Ctrl+V paste (OleGetClipboard), the properties
  # reads (native/shelf_properties.cpp) on the shared utility worker, the
  # relink of Mac-imported items through the import ledger (core/migration).
  add_library(hud_shelf_owner tools/shelf_owner.cpp native/shelf_properties.cpp)
  target_compile_definitions(hud_shelf_owner PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_shelf_owner PUBLIC hud_file_shelf hud_design_native hud_native hud_shelf_icons hud_shelf_transfer hud_shelf_files
    PRIVATE hud_utility hud_mac_import ole32 shell32)
  if(MSVC)
    target_compile_options(hud_shelf_owner PRIVATE /W4 /permissive- /EHsc)
  endif()

  # Hidden-coverage area (app/module_owner.hpp, "Adding a module area"): drives
  # the production Shelf service with synthetic files in the harness. Never
  # linked into EndfieldHUD.exe, whose Shelf is the built-in owner.
  add_library(hud_shelf_area STATIC app/areas/shelf_module.cpp)
  target_include_directories(hud_shelf_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_shelf_area PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_shelf_area PUBLIC hud_module_registry hud_shelf_owner hud_shelf_files hud_data PRIVATE hud_mac_import uiautomationcore oleaut32 user32)
  if(MSVC)
    target_compile_options(hud_shelf_area PRIVATE /W4 /permissive- /EHsc)
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "fileShelfCoverage|hud_shelf_area|app/areas/shelf_module.hpp|endfield::shelf::shelfCoverageModule|coverage")

  if(BUILD_TESTING)
    # Owner-level Shelf services through the design owner on a hidden window
    # (WARP): Event Log records (picker, paste and drops alike), stale-path
    # renewal saved by refresh, the properties card's reads (private and
    # shared utility executors), virtual-file drops, the drop routes, the
    # localized platform text and error descriptions, the UI Automation tree,
    # the relink of Mac-imported items through the import ledger API, the drag
    # payload, and what the Shelf keeps resident (icon worker only while
    # shown, no artwork once the HUD closed on another module).
    add_executable(shelf_services_tests tests/shelf_services_tests.cpp)
    target_compile_definitions(shelf_services_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(shelf_services_tests PRIVATE hud_shelf_owner hud_shelf_files hud_mac_import hud_utility ole32 shell32 uiautomationcore oleaut32 psapi)
    if(MSVC)
      target_compile_options(shelf_services_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME shelf_services_contracts COMMAND shelf_services_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl")
    set_tests_properties(shelf_services_contracts PROPERTIES TIMEOUT 300)

    # Properties of synthetic files and folders in a NEW temporary directory
    # (sizes, times, child counts without hidden entries and their bound,
    # identity checks).
    add_executable(shelf_properties_tests tests/shelf_properties_tests.cpp native/shelf_properties.cpp)
    target_compile_definitions(shelf_properties_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(shelf_properties_tests PRIVATE hud_shelf_files hud_file_shelf)
    if(MSVC)
      target_compile_options(shelf_properties_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME shelf_properties_contracts COMMAND shelf_properties_tests)
    set_tests_properties(shelf_properties_contracts PROPERTIES TIMEOUT 120)
  endif()
endif()
