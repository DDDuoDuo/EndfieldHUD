# Notes area fragment (included after every target in windows/CMakeLists.txt).
# Notes, Projection and Archive are built-in owners of the production
# Application (app/application.cpp); this fragment adds Notes sources to the
# shared native library and registers their contract tests. Tests use
# synthetic documents, hidden owned windows and WARP offscreen targets only.

set(EHUD_NOTES_TARGETS "")
if(WIN32)
  # Paragraph-chunked long-document editor for Notes text fields: typing cost
  # follows the edited chunk, not the document length (native/notes_long_editor).
  target_sources(hud_native PRIVATE native/notes_long_editor.cpp)
  # Edit-menu clipboard for Notes/Archive fields (native/notes_clipboard):
  # keypress-only reads/writes, CF_HDROP media paste through the Shelf codec.
  target_sources(hud_native PRIVATE native/notes_clipboard.cpp)
  # Word/paragraph navigation and multi-click selection (OS icu.dll word rules
  # loaded on demand, character-class fallback): native/notes_text_navigation.
  target_sources(hud_native PRIVATE native/notes_text_navigation.cpp)
  # Pasted images as source managed "<UUID>.png" copies under Notes/Images.
  target_sources(hud_native PRIVATE native/notes_managed_image.cpp)
  target_link_libraries(hud_native PRIVATE hud_shelf_transfer shell32)

  if(BUILD_TESTING)
    # Geometry, pixel and behaviour parity with the single-layout editor, TSF
    # geometry, bounded chunk work for 32k-256k unit documents.
    add_executable(notes_long_editor_tests tests/notes_long_editor_tests.cpp)
    target_compile_definitions(notes_long_editor_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(notes_long_editor_tests PRIVATE hud_native)
    add_test(NAME notes_long_editor_contracts COMMAND notes_long_editor_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl")
    set_tests_properties(notes_long_editor_contracts PROPERTIES TIMEOUT 300)
    list(APPEND EHUD_NOTES_TARGETS notes_long_editor_tests)
    # Source NotesMediaError states on media cards and the explicit relink.
    add_executable(notes_media_relink_tests tests/notes_media_relink_tests.cpp)
    target_compile_definitions(notes_media_relink_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(notes_media_relink_tests PRIVATE hud_native)
    add_test(NAME notes_media_relink_contracts COMMAND notes_media_relink_tests)
    list(APPEND EHUD_NOTES_TARGETS notes_media_relink_tests)
    # Clipboard codecs, rich paste runs, cut/copy/paste and short-to-chunked
    # promotion through a real workspace; synthetic data objects only.
    add_executable(notes_clipboard_tests tests/notes_clipboard_tests.cpp)
    target_compile_definitions(notes_clipboard_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(notes_clipboard_tests PRIVATE hud_native shlwapi windowscodecs ole32)
    add_test(NAME notes_clipboard_contracts COMMAND notes_clipboard_tests)
    list(APPEND EHUD_NOTES_TARGETS notes_clipboard_tests)
    add_executable(notes_text_navigation_tests tests/notes_text_navigation_tests.cpp)
    target_compile_definitions(notes_text_navigation_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(notes_text_navigation_tests PRIVATE hud_native)
    add_test(NAME notes_text_navigation_contracts COMMAND notes_text_navigation_tests)
    list(APPEND EHUD_NOTES_TARGETS notes_text_navigation_tests)

    # The Notes owner suites that need the Mac-exported Notes controls bundle
    # (app.cmake's runtime inputs). They build in CMakeLists.txt; registering
    # them here runs the production Notes owner, its menus, TODO/drawing/media
    # import and the movie fixtures in every configured test run.
    set(ehud_notes_controls "${ehud_app_inputs}/notes-controls-assets")
    set(ehud_notes_pins 9d2fcf0eead9eeeab1be4fdbb0e940a0e1e3eedf840ab9e4c63499e40ae70c00 ca04f142185c7de40acd8523bdb563195d90a1d1)
    set(ehud_notes_movie "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/notes-owned-silent.mp4")
    if(ehud_app_inputs AND EXISTS "${ehud_notes_controls}")
      foreach(suite notes_preview notes_media_import notes_checklist_preview notes_media_menu notes_drawing_preview)
        if(TARGET ${suite}_tests)
          add_test(NAME ${suite}_contracts COMMAND ${suite}_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl" "${ehud_notes_controls}" ${ehud_notes_pins})
          set_tests_properties(${suite}_contracts PROPERTIES TIMEOUT 300)
        endif()
      endforeach()
      if(TARGET notes_format_host_tests)
        add_test(NAME notes_format_host_contracts COMMAND notes_format_host_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl" "${ehud_notes_controls}" ${ehud_notes_pins} "${CMAKE_CURRENT_SOURCE_DIR}/resources/notes-format")
        set_tests_properties(notes_format_host_contracts PROPERTIES TIMEOUT 300)
      endif()
      if(TARGET notes_video_import_tests)
        add_test(NAME notes_video_import_contracts COMMAND notes_video_import_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl" "${ehud_notes_controls}" ${ehud_notes_pins} "${ehud_notes_movie}")
        set_tests_properties(notes_video_import_contracts PROPERTIES TIMEOUT 300)
      endif()
    endif()
    if(TARGET notes_video_poster_tests)
      add_test(NAME notes_video_poster_contracts COMMAND notes_video_poster_tests "${ehud_notes_movie}")
    endif()
    if(TARGET notes_video_decode_tests)
      add_test(NAME notes_video_decode_contracts COMMAND notes_video_decode_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl" "${ehud_notes_movie}")
      set_tests_properties(notes_video_decode_contracts PROPERTIES TIMEOUT 180)
    endif()
  endif()
endif()

# Hidden coverage of the Notes long-document path inside the application
# (app/areas/notes_coverage.hpp). Test-only area: watch_session_preview only.
if(WIN32 AND BUILD_TESTING)
  add_library(hud_notes_coverage_area STATIC app/areas/notes_coverage.cpp)
  target_include_directories(hud_notes_coverage_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_notes_coverage_area PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_notes_coverage_area PUBLIC hud_module_registry hud_native)
  list(APPEND EHUD_NOTES_TARGETS hud_notes_coverage_area)
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "notesLongDocument|hud_notes_coverage_area|app/areas/notes_coverage.hpp|endfield::notes::notesLongDocumentCoverage|coverage")
endif()

foreach(target IN LISTS EHUD_NOTES_TARGETS)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()
