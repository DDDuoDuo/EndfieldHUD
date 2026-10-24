# Archive on the design library (Mac 1.3.0 ArchiveCanvas.swift; inventory
# DOCS-400..483). Portable: the canvas, its body area and ArchiveRichText
# (shared with the Notes centre TEXT note). The data model and repository
# (hud_archive), the service (hud_archive_service) and the DPAPI vault
# (hud_archive_vault) stay in CMakeLists.txt; the native owner, body editor
# and media binding are hud_archive_owner (WIN32, below). hud_design and
# hud_notes_format are declared by later fragments (linking resolves at
# generation time).
if(TARGET hud_archive)
  add_library(hud_archive_canvas modules/archive_rich_text.cpp modules/archive_body_area.cpp modules/archive_canvas.cpp)
  target_include_directories(hud_archive_canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(hud_archive_canvas PUBLIC hud_archive hud_design hud_notes_format hud_notes)
  if(MSVC)
    target_compile_options(hud_archive_canvas PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(hud_archive_canvas PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  if(BUILD_TESTING)
    add_executable(archive_canvas_tests tests/archive_canvas_tests.cpp)
    target_link_libraries(archive_canvas_tests PRIVATE hud_archive_canvas)
    add_test(NAME archive_canvas_contracts COMMAND archive_canvas_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/archive-canvas-source.json")
  endif()
endif()

# The native owner: the canvas over tools::DesignOwnerCore with the body
# editor, the media binding (posters, the strip's attachment, import
# inspection) and recovery's owner authentication (Windows Hello / the
# credential prompt). app/application.cpp constructs it (constructArchive).
if(WIN32 AND TARGET hud_archive_canvas)
  add_library(hud_archive_owner tools/archive_owner.cpp tools/archive_media_binding.cpp native/archive_body_editor.cpp native/archive_owner_authentication.cpp)
  target_include_directories(hud_archive_owner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_archive_owner PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_archive_owner PUBLIC hud_archive_canvas hud_design_native hud_archive_service hud_native PRIVATE windowsapp credui secur32 advapi32)
  if(MSVC)
    target_compile_options(hud_archive_owner PRIVATE /W4 /permissive- /EHsc)
  endif()
  if(TARGET hud_application)
    target_link_libraries(hud_application PUBLIC hud_archive_owner)
  endif()
  if(BUILD_TESTING)
    add_executable(archive_owner_tests tests/archive_owner_tests.cpp)
    target_compile_definitions(archive_owner_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(archive_owner_tests PRIVATE hud_archive_owner ole32 user32)
    if(MSVC)
      target_compile_options(archive_owner_tests PRIVATE /EHsc)
    endif()
    add_test(NAME archive_owner_contracts COMMAND archive_owner_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/resources/notes-format")
    set_tests_properties(archive_owner_contracts PROPERTIES TIMEOUT 300 PASS_REGULAR_EXPRESSION "PASS [0-9]+ Archive owner checks")
  endif()
endif()
