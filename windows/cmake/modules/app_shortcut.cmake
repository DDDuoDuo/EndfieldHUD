# App shortcuts / + Add App area fragment (included after every target in
# windows/CMakeLists.txt; app/module_owner.hpp, "Adding a module area").
# The model, service, tasks, navigation, scene and canvas owner targets
# (hud_shortcuts, hud_app_shortcut_service, hud_app_shortcut_tasks,
# hud_app_shortcut_presentation, hud_app_shortcut_preview) and their contract
# suites stay in CMakeLists.txt. This fragment adds the production module
# owner that EndfieldHUD.exe and the hidden owner coverage construct through
# the area factory, its packaged source artwork and its coverage fixture.

# The Mac import ledger rows of App shortcuts (Migration/relink.json,
# WINDOWS-MIGRATION.md section 7): reconciled by the one transaction owner
# (hud_app_shortcut_tasks) after its load and each committed save/remove.
# hud_mac_import is declared by cmake/modules/import.cmake (read later; link
# names resolve when the build is generated). Synthetic temporary roots only.
if(TARGET hud_shortcuts)
  add_library(hud_app_shortcut_relink STATIC modules/app_shortcut_relink.cpp)
  target_include_directories(hud_app_shortcut_relink PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(hud_app_shortcut_relink PUBLIC hud_shortcuts PRIVATE hud_mac_import)
  if(MSVC)
    target_compile_options(hud_app_shortcut_relink PRIVATE /W4 /permissive- /EHsc)
  endif()
  if(TARGET hud_app_shortcut_tasks)
    target_link_libraries(hud_app_shortcut_tasks PUBLIC hud_app_shortcut_relink)
  endif()
  if(BUILD_TESTING)
    add_executable(app_shortcut_relink_tests tests/app_shortcut_relink_tests.cpp)
    target_link_libraries(app_shortcut_relink_tests PRIVATE hud_app_shortcut_relink hud_mac_import)
    add_test(NAME app_shortcut_relink_contracts COMMAND app_shortcut_relink_tests)
    if(TARGET app_shortcut_tasks_tests)
      target_link_libraries(app_shortcut_tasks_tests PRIVATE hud_mac_import)
    endif()
  endif()
endif()

# Original application icons for the App icon preset: the .exe / .lnk icon
# resource via SHDefExtractIconW (no third-party Shell icon handler), and a
# packaged application's logo through AppInfo (DisplayInfo.GetLogo, bounded)
# decoded by WIC's built-in PNG/JPEG decoders, on the shared executor. Tested
# against the built EndfieldHUD.exe, temporary synthetic .ico/.lnk files and
# the synthetic packaged logo fixture (tools/app_shortcut_logo_fixture.py);
# no installed application is read.
if(WIN32 AND TARGET hud_app_shortcut_service)
  add_library(hud_app_shortcut_icons native/app_shortcut_icons.cpp)
  target_include_directories(hud_app_shortcut_icons PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_app_shortcut_icons PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_app_shortcut_icons PUBLIC hud_app_shortcut_service PRIVATE shell32 ole32 uuid windowscodecs windowsapp)
  if(MSVC)
    target_compile_options(hud_app_shortcut_icons PRIVATE /W4 /permissive- /EHsc)
  endif()
  if(BUILD_TESTING AND TARGET EndfieldHUD)
    add_executable(app_shortcut_icons_tests tests/app_shortcut_icons_tests.cpp)
    target_compile_definitions(app_shortcut_icons_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(app_shortcut_icons_tests PRIVATE hud_app_shortcut_icons ole32 shell32 uuid)
    add_dependencies(app_shortcut_icons_tests EndfieldHUD)
    add_test(NAME app_shortcut_icons_contracts COMMAND app_shortcut_icons_tests "$<TARGET_FILE:EndfieldHUD>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/app_shortcut_packaged_logo.png")
  endif()
endif()

# UI Automation peers of the + Add App canvas (HUDAppShortcutInteraction
# .layoutAccessibility): buttons with Invoke and the name field's edit peer.
# Tested over injected controls and host callbacks; no window is shown.
if(WIN32)
  add_library(hud_app_shortcut_automation STATIC native/app_shortcut_automation.cpp)
  target_include_directories(hud_app_shortcut_automation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_app_shortcut_automation PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_app_shortcut_automation PRIVATE uiautomationcore oleaut32 ole32)
  if(MSVC)
    target_compile_options(hud_app_shortcut_automation PRIVATE /W4 /permissive- /EHsc)
  endif()
  if(BUILD_TESTING)
    add_executable(app_shortcut_automation_tests tests/app_shortcut_automation_tests.cpp)
    target_compile_definitions(app_shortcut_automation_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(app_shortcut_automation_tests PRIVATE hud_app_shortcut_automation uiautomationcore oleaut32 ole32)
    add_test(NAME app_shortcut_automation_contracts COMMAND app_shortcut_automation_tests)
  endif()
endif()

if(WIN32 AND TARGET hud_app_shortcut_preview AND TARGET hud_app_shortcut_tasks AND TARGET hud_app_shortcut_icons AND TARGET hud_app_shortcut_automation)
  add_library(hud_app_shortcut_area STATIC app/areas/app_shortcut_module.cpp)
  target_include_directories(hud_app_shortcut_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_app_shortcut_area PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_app_shortcut_area PUBLIC hud_module_registry hud_utility hud_app_shortcut_preview hud_app_shortcut_tasks hud_app_shortcut_icons hud_app_shortcut_automation hud_data hud_localization
    PRIVATE hud_mac_import hud_mac_import_native user32 ole32 oleaut32 uiautomationcore)
  if(MSVC)
    target_compile_options(hud_app_shortcut_area PRIVATE /W4 /permissive- /EHsc)
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "appShortcuts|hud_app_shortcut_area|app/areas/app_shortcut_module.hpp|endfield::areas::appShortcutModule")
  # Hidden coverage seeds the area's coverage root with the Mac oracle's
  # accepted archive (tools/shortcut_reference.py): one imported record.
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREA_FIXTURES
    "appShortcut.macArchive|${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/shortcut-source.json")
  # A synthetic packaged application's logo for the App icon preset (never an
  # installed application's).
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREA_FIXTURES
    "appShortcut.packagedLogo|${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/app_shortcut_packaged_logo.png")
  # Original canvas artwork, masks and bundled game icons (hash-pinned by
  # native/app_shortcut_assets.cpp), staged into the verified package.
  ehud_app_resource(appShortcut.assets directory "${CMAKE_CURRENT_SOURCE_DIR}/resources/app-shortcut" "common/app-shortcut")
endif()
