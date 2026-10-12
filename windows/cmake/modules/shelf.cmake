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
  # Ctrl+V paste (OleGetClipboard).
  target_link_libraries(hud_shelf_preview PRIVATE ole32 shell32)
  # The properties card (replaces the Quick Look preview): the item's
  # properties are read on the shared utility worker (app/utility_executor)
  # through the Windows property system (propsys) for dimensions and duration.
  target_sources(hud_shelf_preview PRIVATE native/shelf_properties.cpp)
  target_link_libraries(hud_shelf_preview PRIVATE hud_utility propsys)
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
    # stale-path relocation saved by refresh, the properties card (private and
    # shared utility executors), virtual-file drops, localized platform text and
    # error descriptions, the UI Automation peers and the relink of
    # Mac-imported items through the import ledger API.
    add_executable(shelf_services_tests tests/shelf_services_tests.cpp)
    target_compile_definitions(shelf_services_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(shelf_services_tests PRIVATE hud_shelf_preview hud_shelf_files hud_mac_import hud_utility ole32 shell32 uiautomationcore oleaut32)
    if(MSVC)
      target_compile_options(shelf_services_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME shelf_services_contracts COMMAND shelf_services_tests "${CMAKE_CURRENT_SOURCE_DIR}/resources/shelf")
    set_tests_properties(shelf_services_contracts PROPERTIES TIMEOUT 180)

    # Properties of synthetic files, folders, a PNG and a WAV in a NEW
    # temporary directory (sizes, times, child counts and its bound, image
    # dimensions and duration through the property system, identity checks).
    add_executable(shelf_properties_tests tests/shelf_properties_tests.cpp native/shelf_properties.cpp)
    target_compile_definitions(shelf_properties_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(shelf_properties_tests PRIVATE hud_shelf_files ole32 shell32 propsys windowscodecs)
    if(MSVC)
      target_compile_options(shelf_properties_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME shelf_properties_contracts COMMAND shelf_properties_tests)
    set_tests_properties(shelf_properties_contracts PROPERTIES TIMEOUT 120)

    # Card removal frame by frame through the production owner, published as
    # the HUD host publishes owners (WARP readback; optional PNG frames).
    add_executable(shelf_removal_frames_tests tests/shelf_removal_frames_tests.cpp)
    target_compile_definitions(shelf_removal_frames_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(shelf_removal_frames_tests PRIVATE hud_shelf_preview ole32 windowscodecs)
    if(MSVC)
      target_compile_options(shelf_removal_frames_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME shelf_removal_frames_contracts COMMAND shelf_removal_frames_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/resources/shelf" "${CMAKE_CURRENT_SOURCE_DIR}/resources/shelf/reveal-mask.bin")
    set_tests_properties(shelf_removal_frames_contracts PROPERTIES TIMEOUT 180)

    # What the Shelf keeps resident (memory audit) through the production
    # owner, published like the host: no icon worker before the first shown
    # Shelf, no artwork (rasters, textures) and no icon thread while another
    # module is shown, a pixel-identical rebuild from cached icons, and a
    # worker restart only for a missing icon. Prints the measured numbers.
    add_executable(shelf_resources_tests tests/shelf_resources_tests.cpp)
    target_compile_definitions(shelf_resources_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(shelf_resources_tests PRIVATE hud_shelf_preview ole32 psapi)
    if(MSVC)
      target_compile_options(shelf_resources_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME shelf_resources_contracts COMMAND shelf_resources_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/resources/shelf")
    set_tests_properties(shelf_resources_contracts PROPERTIES TIMEOUT 180)
  endif()
endif()
