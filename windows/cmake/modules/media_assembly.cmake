# Media Assembly area (owner: media). Included after every target in
# windows/CMakeLists.txt, so existing libraries (hud_media_assembly_model,
# hud_media_assembly_assets, hud_native, ...) are available here.

# The shared PhotoMode folder (EHUD_MEDIA_ASSEMBLY_RESOURCE_DIR, CMakeLists.txt)
# holds exactly provenance.json and the files it lists, with their pins
# (cmake/media_assembly_resources.cmake); Windows builds refuse to configure
# otherwise, and the staging contract below runs on every platform.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/media_assembly_resources.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${EHUD_MEDIA_ASSEMBLY_RESOURCE_DIR}/provenance.json" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/media_assembly_resources.cmake")
if(WIN32)
  ehud_media_assembly_check("${EHUD_MEDIA_ASSEMBLY_RESOURCE_DIR}" ehud_media_assembly_files)
endif()
if(BUILD_TESTING)
  add_test(NAME media_assembly_resources_contracts COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${EHUD_MEDIA_ASSEMBLY_RESOURCE_DIR}"
    "-DSTAGE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/app/stage_resources.cmake" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/media-assembly-resources-check"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/media_assembly_resources.cmake")
endif()

# Portable MediaAssemblyEngine.apply port: geometry, colour chain and stickers.
add_library(hud_media_assembly_processor modules/media_assembly_processor.cpp)
target_include_directories(hud_media_assembly_processor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_media_assembly_processor PUBLIC hud_media_assembly_model)
if(BUILD_TESTING)
  add_executable(media_assembly_processor_tests tests/media_assembly_processor_tests.cpp)
  target_link_libraries(media_assembly_processor_tests PRIVATE hud_media_assembly_processor hud_data hud_reader_zlib)
  add_test(NAME media_assembly_processor_contracts COMMAND media_assembly_processor_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/media-assembly-processor-source.json ${EHUD_MEDIA_ASSEMBLY_RESOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/media-assembly-highlight-shadow-fit.json ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/media-assembly-icc-source.json
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/media-assembly-video-colour-source.json)
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
  # Its programs (native/media_assembly_gpu.hlsl) are compiled at build time by
  # the Windows SDK fxc that CMakeLists.txt found for hud.hlsl
  # (native/media_assembly_gpu.shaders.cmake), so the HUD compiles no HLSL for
  # Media Assembly; without fxc the generated header carries only the text,
  # compiled once at run time on the utility worker as before.
  set(ehud_media_assembly_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/media_assembly")
  set(ehud_media_assembly_shaders "${ehud_media_assembly_shader_dir}/media_assembly_gpu_shaders.hpp")
  set(ehud_media_assembly_fxc "")
  set(ehud_media_assembly_program_kind text)
  if(EHUD_FXC)
    set(ehud_media_assembly_fxc "-DFXC=${EHUD_FXC}")
    set(ehud_media_assembly_program_kind fxc)
  endif()
  add_custom_command(OUTPUT "${ehud_media_assembly_shaders}"
    COMMAND "${CMAKE_COMMAND}" ${ehud_media_assembly_fxc} "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/native/media_assembly_gpu.hlsl" "-DOUTPUT=${ehud_media_assembly_shaders}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/native/media_assembly_gpu.shaders.cmake"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/native/media_assembly_gpu.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/native/media_assembly_gpu.shaders.cmake"
    COMMENT "Compiling native/media_assembly_gpu.hlsl" VERBATIM)
  # MSBuild FileTracker ignores reads under LocalAppData (cmake/
  # msbuild_include_fingerprints.cmake): the program text's hash on the
  # command line recompiles the processor when the HLSL or the fxc choice changes.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/native/media_assembly_gpu.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/native/media_assembly_gpu.shaders.cmake")
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/native/media_assembly_gpu.hlsl" ehud_media_assembly_program_hash)
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/native/media_assembly_gpu.shaders.cmake" ehud_media_assembly_script_hash)
  string(SUBSTRING "${ehud_media_assembly_program_hash}" 0 16 ehud_media_assembly_program_hash)
  string(SUBSTRING "${ehud_media_assembly_script_hash}" 0 16 ehud_media_assembly_script_hash)
  add_library(hud_media_assembly_gpu native/media_assembly_gpu.cpp "${ehud_media_assembly_shaders}")
  set_property(SOURCE native/media_assembly_gpu.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    "EHUD_MEDIA_ASSEMBLY_PROGRAMS=${ehud_media_assembly_program_kind}_${ehud_media_assembly_program_hash}_${ehud_media_assembly_script_hash}")
  target_include_directories(hud_media_assembly_gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE "${ehud_media_assembly_shader_dir}")
  target_compile_definitions(hud_media_assembly_gpu PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_media_assembly_gpu PUBLIC hud_media_assembly_processor PRIVATE d3d11 dxgi d3dcompiler)
  if(BUILD_TESTING)
    add_executable(media_assembly_gpu_tests tests/media_assembly_gpu_tests.cpp)
    target_compile_definitions(media_assembly_gpu_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(media_assembly_gpu_tests PRIVATE hud_media_assembly_gpu d3d11 dxgi)
    add_test(NAME media_assembly_gpu_contracts COMMAND media_assembly_gpu_tests ${EHUD_MEDIA_ASSEMBLY_RESOURCE_DIR})
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
    target_link_libraries(media_assembly_codec_tests PRIVATE hud_media_assembly_codec hud_data windowscodecs mfplat mfreadwrite mfuuid ole32 d3d11)
    add_test(NAME media_assembly_codec_contracts COMMAND media_assembly_codec_tests ${EHUD_MEDIA_ASSEMBLY_RESOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/media-assembly-icc-source.json ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/media-assembly-video-colour-source.json
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/media-assembly-jpeg-source.json)
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
    add_test(NAME media_assembly_preview_contracts COMMAND media_assembly_preview_tests ${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl ${EHUD_MEDIA_ASSEMBLY_RESOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/notes-owned-silent.mp4)
  endif()
endif()

# --- Module area (app/module_owner.hpp, "Adding a module area") --------------
# The ModuleOwner adapter and factory over MediaAssemblyPreview, linked into
# EndfieldHUD.exe and the hidden coverage harness by app.cmake's generated
# area table. Runtime input: resources/media-assembly, the shared
# Resources/MediaAssembly folder staged verbatim (provenance.json and its 60
# listed files, pinned again in resources.json). Hidden coverage without a
# staged package uses that shared folder, plus the pinned owned silent movie
# (read-only copy).
if(WIN32)
  add_library(hud_media_assembly_area STATIC app/areas/media_assembly_module.cpp)
  target_include_directories(hud_media_assembly_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_media_assembly_area PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_media_assembly_area PUBLIC hud_module_registry hud_media_assembly_preview hud_utility hud_data
    PRIVATE windowscodecs uiautomationcore oleaut32 ole32 user32)
  if(MSVC)
    target_compile_options(hud_media_assembly_area PRIVATE /W4 /permissive- /EHsc)
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "mediaAssembly|hud_media_assembly_area|app/areas/media_assembly_module.hpp|endfield::areas::mediaAssemblyModule")
  if(COMMAND ehud_app_resource)
    ehud_app_resource(mediaAssembly.assets directory "${EHUD_MEDIA_ASSEMBLY_RESOURCE_DIR}" "media-assembly")
  else()
    set_property(GLOBAL APPEND PROPERTY EHUD_APP_RESOURCE_SPECS
      "mediaAssembly.assets|directory|${EHUD_MEDIA_ASSEMBLY_RESOURCE_DIR}|media-assembly")
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREA_FIXTURES
    "mediaAssembly.sourceAssets|${EHUD_MEDIA_ASSEMBLY_RESOURCE_DIR}"
    "mediaAssembly.movie|${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/notes-owned-silent.mp4")
endif()
