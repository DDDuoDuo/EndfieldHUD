# HUD design library (Mac Sources/HUDDesign*.swift, HUDProjectedTextEditor,
# HUDModuleContent's directional half): the shared component library every
# redesigned centre module is written on. Design and API:
# build/windows-1.3/LIBRARY-DESIGN.md (git-ignored record) and the headers'
# comments. Stage 1 declares the targets with stub sources; work packages
# L1-L4 fill them and add their contract suites below.
#
# hud_design (portable): tokens and palette, text model, formats, JSON node
# builders, icons, motion, the layer tree, controls, cards, menus, header,
# scroll area, redaction, text field model, overlay model, page / canvas /
# context. No window, timer, device or OS provider; explicit caller clock.
add_library(hud_design
  modules/hud_design_tokens.cpp modules/hud_design_text.cpp modules/hud_design_format.cpp
  modules/hud_design_nodes.cpp modules/hud_design_icons.cpp modules/hud_design_motion.cpp
  modules/hud_design_layer.cpp modules/hud_design_control.cpp modules/hud_design_controls.cpp
  modules/hud_design_badges.cpp modules/hud_design_scroll.cpp modules/hud_design_cards.cpp
  modules/hud_design_menu.cpp modules/hud_design_header.cpp modules/hud_design_redaction.cpp
  modules/hud_design_text_field.cpp modules/hud_design_overlay.cpp modules/hud_design_page.cpp)
target_include_directories(hud_design PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_design PUBLIC hud_motion hud_localization hud_data hud_scene)
if(MSVC)
  target_compile_options(hud_design PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(hud_design PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32 AND TARGET hud_native AND TARGET hud_module_registry)
  # hud_design_native: the retained design scene, DirectWrite measurement,
  # the UIA tree, the projected text field host, the reusable owner core and
  # the shared centre overlay owner.
  add_library(hud_design_native
    native/hud_design_text_metrics.cpp native/hud_design_scene.cpp native/hud_design_automation.cpp
    native/hud_design_text_field.cpp tools/hud_design_owner.cpp tools/hud_design_overlay_owner.cpp)
  target_include_directories(hud_design_native PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_design_native PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_design_native PUBLIC hud_design hud_native hud_module_registry hud_overlay_host
    PRIVATE icu uiautomationcore oleaut32 ole32 user32)
  if(MSVC)
    target_compile_options(hud_design_native PRIVATE /W4 /permissive- /EHsc)
  endif()
endif()

# --- L4 native (LIBRARY-DESIGN.md section 15): the design scene, the
# DirectWrite measurer, the UIA tree (also Settings' elements, Q2), the
# projected text field host, the owner core and the overlay owner. Uppercase
# shares Profile's ICU rule (native/profile_text.cpp); UIA screen rects share
# NativeScreenReaderPeers' client-to-screen helper. Suites run hidden: WARP,
# an invisible window, fake TSF sinks, a fake edit menu, no input injection.
if(WIN32 AND TARGET hud_design_native)
  target_link_libraries(hud_design_native PUBLIC hud_profile hud_local_automation)
  if(BUILD_TESTING)
    foreach(suite scene text_metrics automation text_field owner)
      add_executable(hud_design_${suite}_tests tests/hud_design_${suite}_tests.cpp)
      target_compile_definitions(hud_design_${suite}_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
      target_link_libraries(hud_design_${suite}_tests PRIVATE hud_design_native uiautomationcore oleaut32 ole32 user32)
      if(MSVC)
        target_compile_options(hud_design_${suite}_tests PRIVATE /W4 /permissive- /EHsc)
      endif()
    endforeach()
    add_test(NAME hud_design_scene_contracts COMMAND hud_design_scene_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl")
    set(EHUD_DESIGN_ORACLE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hud-design-source.json")
    add_test(NAME hud_design_text_metrics_contracts COMMAND hud_design_text_metrics_tests "${EHUD_DESIGN_ORACLE}")
    add_test(NAME hud_design_automation_contracts COMMAND hud_design_automation_tests)
    add_test(NAME hud_design_text_field_contracts COMMAND hud_design_text_field_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl")
    add_test(NAME hud_design_owner_contracts COMMAND hud_design_owner_tests "${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl")
    set_tests_properties(hud_design_scene_contracts hud_design_text_field_contracts hud_design_owner_contracts PROPERTIES TIMEOUT 300)
  endif()
endif()

# --- L2 components (LIBRARY-DESIGN.md section 15): controls, badges, scroll
# area, cards, menus and pop-overs, header, redaction, text field model. The
# suite checks every component in its Mac states against values derived from
# the Swift and, once tools/hud_design_reference.sh has produced it, node by
# node against the Mac oracle with its recorded text widths.
if(BUILD_TESTING)
  add_executable(hud_design_components_tests tests/hud_design_components_tests.cpp)
  target_link_libraries(hud_design_components_tests PRIVATE hud_design)
  add_test(NAME hud_design_components_contracts COMMAND hud_design_components_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hud-design-source.json")
endif()

# --- L1 core (LIBRARY-DESIGN.md section 15): tokens, text model, formats,
# node builders, icons, motion and the layer tree. Portable contract suites
# (section 14 items 1-3): vectors derived from the Swift formulas and
# CoreGraphics; the Mac oracle (tests/fixtures/hud-design-source.json, package
# L3) is compared when present. The icon suite reads the Mac icon table text.
if(BUILD_TESTING)
  foreach(suite tokens motion layer)
    add_executable(hud_design_${suite}_tests tests/hud_design_${suite}_tests.cpp)
    target_link_libraries(hud_design_${suite}_tests PRIVATE hud_design)
  endforeach()
  set(EHUD_DESIGN_L1_ORACLE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hud-design-source.json")
  add_test(NAME hud_design_tokens_contracts COMMAND hud_design_tokens_tests "${EHUD_DESIGN_L1_ORACLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Sources/HUDDesignIcons.swift")
  add_test(NAME hud_design_motion_contracts COMMAND hud_design_motion_tests "${EHUD_DESIGN_L1_ORACLE}")
  add_test(NAME hud_design_layer_contracts COMMAND hud_design_layer_tests "${EHUD_DESIGN_L1_ORACLE}")
endif()

# --- L3 page + overlay (LIBRARY-DESIGN.md section 15): the canvas, page and
# context and the shared centre overlay (confirm bar, toast, dim). The suite
# replays the Mac oracle's overlay decision traces, confirm bar geometry and
# fake-canvas script (tests/fixtures/hud-design-source.json, produced by
# tools/hud_design_reference.sh from the unchanged Mac Sources).
if(BUILD_TESTING)
  add_executable(hud_design_page_tests tests/hud_design_page_tests.cpp)
  target_link_libraries(hud_design_page_tests PRIVATE hud_design)
  add_test(NAME hud_design_page_contracts COMMAND hud_design_page_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hud-design-source.json")
endif()
