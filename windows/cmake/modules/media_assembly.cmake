# Media Assembly area (owner: media). Included after every target in
# windows/CMakeLists.txt, so existing libraries (hud_media_assembly_model,
# hud_media_assembly_assets, hud_native, ...) are available here.

# Portable MediaAssemblyEngine.apply port: geometry, colour chain and stickers.
add_library(hud_media_assembly_processor modules/media_assembly_processor.cpp)
target_include_directories(hud_media_assembly_processor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_media_assembly_processor PUBLIC hud_media_assembly_model)
if(BUILD_TESTING)
  add_executable(media_assembly_processor_tests tests/media_assembly_processor_tests.cpp)
  target_link_libraries(media_assembly_processor_tests PRIVATE hud_media_assembly_processor hud_data hud_reader_zlib)
  add_test(NAME media_assembly_processor_contracts COMMAND media_assembly_processor_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/media-assembly-processor-source.json ${CMAKE_CURRENT_SOURCE_DIR}/resources/media-assembly)
endif()

# Portable controller: export ticket/commit rules, secondary menus, keyboard,
# localized errors and the debounced event/progress deadlines.
add_library(hud_media_assembly_controller modules/media_assembly_export.cpp modules/media_assembly_controller.cpp modules/media_assembly_menu.cpp)
target_include_directories(hud_media_assembly_controller PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_media_assembly_controller PUBLIC hud_media_assembly_session hud_media_assembly_artwork)
if(BUILD_TESTING)
  add_executable(media_assembly_controller_tests tests/media_assembly_controller_tests.cpp)
  target_link_libraries(media_assembly_controller_tests PRIVATE hud_media_assembly_controller)
  add_test(NAME media_assembly_controller_contracts COMMAND media_assembly_controller_tests)
endif()

if(WIN32)
  # GPU evaluation of the same colour chain and geometry on an existing
  # D3D11 device (previews/played frames on the renderer's media device,
  # explicit video exports on their own job device).
  add_library(hud_media_assembly_gpu native/media_assembly_gpu.cpp)
  target_include_directories(hud_media_assembly_gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_media_assembly_gpu PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_media_assembly_gpu PUBLIC hud_media_assembly_processor PRIVATE d3d11 dxgi d3dcompiler)
  if(BUILD_TESTING)
    add_executable(media_assembly_gpu_tests tests/media_assembly_gpu_tests.cpp)
    target_compile_definitions(media_assembly_gpu_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(media_assembly_gpu_tests PRIVATE hud_media_assembly_gpu d3d11 dxgi)
    add_test(NAME media_assembly_gpu_contracts COMMAND media_assembly_gpu_tests ${CMAKE_CURRENT_SOURCE_DIR}/resources/media-assembly)
  endif()

  # WIC still/GIF decode, Media Foundation movie frames and MP4 export, WIC
  # encoders and the atomic Windows commit (the production MediaAssemblyEngine).
  add_library(hud_media_assembly_codec native/media_assembly_codec.cpp)
  target_include_directories(hud_media_assembly_codec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_media_assembly_codec PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_media_assembly_codec PUBLIC hud_media_assembly_controller hud_media_assembly_processor hud_media_assembly_assets hud_media_assembly_gpu
    PRIVATE windowscodecs mfplat mfreadwrite mfuuid shlwapi ole32 propsys)
  if(BUILD_TESTING)
    add_executable(media_assembly_codec_tests tests/media_assembly_codec_tests.cpp)
    target_compile_definitions(media_assembly_codec_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(media_assembly_codec_tests PRIVATE hud_media_assembly_codec windowscodecs mfplat mfreadwrite mfuuid ole32 d3d11)
    add_test(NAME media_assembly_codec_contracts COMMAND media_assembly_codec_tests ${CMAKE_CURRENT_SOURCE_DIR}/resources/media-assembly)
  endif()

  # UI Automation fragments for the projected controls (the host's HUD root
  # provider lists them as children).
  add_library(hud_media_assembly_accessibility native/media_assembly_accessibility.cpp)
  target_include_directories(hud_media_assembly_accessibility PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_media_assembly_accessibility PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_media_assembly_accessibility PUBLIC hud_media_assembly_presentation PRIVATE uiautomationcore oleaut32 ole32)
  if(BUILD_TESTING)
    add_executable(media_assembly_accessibility_tests tests/media_assembly_accessibility_tests.cpp)
    target_compile_definitions(media_assembly_accessibility_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(media_assembly_accessibility_tests PRIVATE hud_media_assembly_accessibility uiautomationcore oleaut32 ole32 user32)
    add_test(NAME media_assembly_accessibility_contracts COMMAND media_assembly_accessibility_tests)
  endif()

  # Host owner (source HUDMediaAssemblyInteraction + canvas wiring), the
  # retained secondary menus and the native open/save dialogs.
  add_library(hud_media_assembly_preview tools/media_assembly_preview.cpp native/media_assembly_dialog.cpp)
  target_include_directories(hud_media_assembly_preview PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_media_assembly_preview PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_media_assembly_preview PUBLIC hud_media_assembly_scene hud_media_assembly_codec hud_media_assembly_controller hud_media_assembly_gpu hud_media_assembly_accessibility hud_notes hud_native hud_overlay_host hud_utility
    PRIVATE ole32 shell32)
  if(BUILD_TESTING)
    add_executable(media_assembly_preview_tests tests/media_assembly_preview_tests.cpp)
    target_compile_definitions(media_assembly_preview_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(media_assembly_preview_tests PRIVATE hud_media_assembly_preview hud_media_assembly_accessibility hud_packet windowscodecs ole32 oleaut32 uiautomationcore d3d11 dxgi)
    add_test(NAME media_assembly_preview_contracts COMMAND media_assembly_preview_tests ${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl ${CMAKE_CURRENT_SOURCE_DIR}/resources/media-assembly ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/notes-owned-silent.mp4)
  endif()
endif()
