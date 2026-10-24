# E-Reader and Projection on the design library (Mac 1.3.0 ReaderCanvas,
# ProjectionCanvas, ProjectionSettings, ProjectionControls and the workspace
# chrome of ProjectionWorkspaceView; inventory DOCS-600..637, DOCS-700..781).
# The canvases are portable (modules/reader_canvas.*, modules/projection_*);
# their platform services stay where they were: the Reader library, provider
# and import queue (modules/reader_state.*, native/reader_document.*,
# native/reader_owner.*, native/reader_import.*) and the Projection session
# model, handoff, ink, media and workspace (modules/projection_model.*,
# app/projection_handoff.*, native/projection_*, tools/projection_*).
add_library(hud_reader_canvas modules/reader_canvas.cpp)
target_include_directories(hud_reader_canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_reader_canvas PUBLIC hud_design hud_reader hud_reader_localization)
if(MSVC)
  target_compile_options(hud_reader_canvas PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hud_reader_canvas PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The Projection page, the shared pen settings (projection.settings.v1) and
# the workspace chrome (toolbar, menus, pop-overs: modules/projection_toolbar.*);
# the custom pen colour is the shared colour pop-over (hud_notes_format).
add_library(hud_projection_canvas modules/projection_settings.cpp modules/projection_canvas.cpp modules/projection_toolbar.cpp)
target_include_directories(hud_projection_canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_projection_canvas PUBLIC hud_design hud_notes_format hud_projection_model hud_data)
if(MSVC)
  target_compile_options(hud_projection_canvas PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hud_projection_canvas PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The Reader's module owner: the canvas in a DesignOwnerCore, its page
# textures composed inside the canvas (native/reader_pages.*) and the root's
# picker / shelf / validated-import route (tools/reader_canvas_owner.*).
if(WIN32 AND TARGET hud_design_native AND TARGET hud_reader_import)
  add_library(hud_reader_canvas_owner tools/reader_canvas_owner.cpp native/reader_pages.cpp)
  target_include_directories(hud_reader_canvas_owner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_reader_canvas_owner PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_reader_canvas_owner PUBLIC hud_reader_canvas hud_design_native hud_reader_import hud_utility)
  if(MSVC)
    target_compile_options(hud_reader_canvas_owner PRIVATE /W4 /permissive- /EHsc)
  endif()
endif()

# The application builds the Reader's owner and the Projection page's
# DesignModuleOwner (app/application.cpp constructReader / constructProjection).
if(TARGET hud_application)
  target_link_libraries(hud_application PUBLIC hud_projection_canvas)
endif()

# Both pages node by node against the Mac 1.3.0 oracle (tools/
# reader_projection_reference.sh -> tests/fixtures/reader-projection-source.json),
# plus the Swift-derived contracts of the canvases and the pen settings.
if(BUILD_TESTING)
  add_executable(reader_projection_canvas_tests tests/reader_projection_canvas_tests.cpp)
  target_link_libraries(reader_projection_canvas_tests PRIVATE hud_reader_canvas hud_projection_canvas)
  add_test(NAME reader_projection_canvas_contracts COMMAND reader_projection_canvas_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/reader-projection-source.json)
endif()
