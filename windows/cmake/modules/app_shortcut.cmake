# App shortcuts / + Add App area fragment (included after every target in
# windows/CMakeLists.txt; app/module_owner.hpp, "Adding a module area").
# The model, service, tasks, navigation, scene and canvas owner targets
# (hud_shortcuts, hud_app_shortcut_service, hud_app_shortcut_tasks,
# hud_app_shortcut_presentation, hud_app_shortcut_preview) and their contract
# suites stay in CMakeLists.txt. This fragment adds the production module
# owner that EndfieldHUD.exe and the hidden owner coverage construct through
# the area factory, its packaged source artwork and its coverage fixture.
if(WIN32 AND TARGET hud_app_shortcut_preview AND TARGET hud_app_shortcut_tasks)
  add_library(hud_app_shortcut_area STATIC app/areas/app_shortcut_module.cpp)
  target_include_directories(hud_app_shortcut_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_app_shortcut_area PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_app_shortcut_area PUBLIC hud_module_registry hud_utility hud_app_shortcut_preview hud_app_shortcut_tasks hud_data hud_localization
    PRIVATE user32)
  if(MSVC)
    target_compile_options(hud_app_shortcut_area PRIVATE /W4 /permissive- /EHsc)
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "appShortcuts|hud_app_shortcut_area|app/areas/app_shortcut_module.hpp|endfield::areas::appShortcutModule")
  # Hidden coverage seeds the area's coverage root with the Mac oracle's
  # accepted archive (tools/shortcut_reference.py): one imported record.
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREA_FIXTURES
    "appShortcut.macArchive|${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/shortcut-source.json")
  # Original canvas artwork, masks and bundled game icons (hash-pinned by
  # native/app_shortcut_assets.cpp), staged into the verified package.
  ehud_app_resource(appShortcut.assets directory "${CMAKE_CURRENT_SOURCE_DIR}/resources/app-shortcut" "common/app-shortcut")
endif()
