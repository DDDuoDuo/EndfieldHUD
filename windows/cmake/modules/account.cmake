# Account Linking (China skland.com / Global skport.com).
# Source authority: Sources/Hypergryph*.swift, HUDAccount*.swift (unchanged).
# Oracle: windows/tools/hypergryph_account_reference.sh -> tests/fixtures/hypergryph-account-source.json
# Tests never contact a network host, open a login page, read real tokens/IDs
# or touch real profile/account data; they use synthetic fixtures, fake
# transports, in-memory/temporary vaults and temporary directories.

add_library(hud_hypergryph_account
  modules/hypergryph_account_crypto.cpp
  modules/hypergryph_account_unicode.cpp
  modules/hypergryph_account_strings.cpp
  modules/hypergryph_account_model.cpp
  modules/hypergryph_account_api.cpp
  modules/hypergryph_account_cache.cpp
  modules/hypergryph_account_controller.cpp
  modules/hypergryph_account_identity_sync.cpp
  modules/hypergryph_account_login.cpp
  modules/hypergryph_account_sanity_gauge.cpp
  modules/hypergryph_account_canvas.cpp
  modules/hypergryph_account_profile_relay.cpp)
target_include_directories(hud_hypergryph_account PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hud_hypergryph_account PUBLIC hud_data hud_localization hud_scene)

if(BUILD_TESTING)
  add_executable(hypergryph_account_api_tests tests/hypergryph_account_api_tests.cpp)
  target_link_libraries(hypergryph_account_api_tests PRIVATE hud_hypergryph_account)
  add_test(NAME hypergryph_account_api_contracts COMMAND hypergryph_account_api_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hypergryph-account-source.json)
  add_executable(hypergryph_account_login_tests tests/hypergryph_account_login_tests.cpp)
  target_link_libraries(hypergryph_account_login_tests PRIVATE hud_hypergryph_account)
  add_test(NAME hypergryph_account_login_contracts COMMAND hypergryph_account_login_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hypergryph-account-source.json)
  add_executable(hypergryph_account_sanity_gauge_tests tests/hypergryph_account_sanity_gauge_tests.cpp)
  target_link_libraries(hypergryph_account_sanity_gauge_tests PRIVATE hud_hypergryph_account)
  add_test(NAME hypergryph_account_sanity_gauge_contracts COMMAND hypergryph_account_sanity_gauge_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hypergryph-account-source.json ${CMAKE_CURRENT_SOURCE_DIR}/resources/account)
  add_executable(hypergryph_account_canvas_tests tests/hypergryph_account_canvas_tests.cpp)
  target_link_libraries(hypergryph_account_canvas_tests PRIVATE hud_hypergryph_account)
  add_test(NAME hypergryph_account_canvas_contracts COMMAND hypergryph_account_canvas_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hypergryph-account-source.json)
  # Also the Mac import -> restart contract: the import tool's codec
  # (hud_mac_import_codec, cmake/modules/import.cmake) produces the cache a
  # fresh controller reads.
  add_executable(hypergryph_account_controller_tests tests/hypergryph_account_controller_tests.cpp)
  target_link_libraries(hypergryph_account_controller_tests PRIVATE hud_hypergryph_account hud_mac_import_codec)
  add_test(NAME hypergryph_account_controller_contracts COMMAND hypergryph_account_controller_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hypergryph-account-source.json)
  # Profile sync across module areas: pending update/lock replay on attach.
  add_executable(hypergryph_account_profile_relay_tests tests/hypergryph_account_profile_relay_tests.cpp)
  target_link_libraries(hypergryph_account_profile_relay_tests PRIVATE hud_hypergryph_account)
  add_test(NAME hypergryph_account_profile_relay_contracts COMMAND hypergryph_account_profile_relay_tests)
endif()

if(WIN32)
  # DPAPI vault, owner-only cache file and asynchronous WinHTTP transport.
  add_library(hud_hypergryph_account_native
    native/hypergryph_account_vault.cpp
    native/hypergryph_account_transport.cpp
    native/hypergryph_account_avatar.cpp
    native/hypergryph_account_services.cpp)
  target_include_directories(hud_hypergryph_account_native PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_hypergryph_account_native PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_hypergryph_account_native PUBLIC hud_hypergryph_account hud_utility PRIVATE winhttp crypt32 advapi32 windowscodecs ole32)
  # Account module face and header sanity gauge on the shared renderer.
  add_library(hud_hypergryph_account_scene
    native/hypergryph_account_canvas_scene.cpp
    native/hypergryph_account_sanity_gauge_scene.cpp
    tools/account_preview.cpp
    tools/account_sanity_gauge_preview.cpp)
  target_include_directories(hud_hypergryph_account_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_hypergryph_account_scene PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_hypergryph_account_scene PUBLIC hud_hypergryph_account hud_native hud_motion hud_overlay_host)
  # Development-only flat snapshot renderer (not a test; never shows a window).
  add_executable(account_snapshot tools/account_snapshot.cpp)
  target_compile_definitions(account_snapshot PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(account_snapshot PRIVATE hud_hypergryph_account_scene windowscodecs ole32)
  if(BUILD_TESTING)
    add_executable(hypergryph_account_native_tests tests/hypergryph_account_native_tests.cpp)
    target_compile_definitions(hypergryph_account_native_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(hypergryph_account_native_tests PRIVATE hud_hypergryph_account_scene ole32)
    add_test(NAME hypergryph_account_native_contracts COMMAND hypergryph_account_native_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl ${CMAKE_CURRENT_SOURCE_DIR}/resources/account)
    add_executable(hypergryph_account_platform_tests tests/hypergryph_account_platform_tests.cpp)
    target_compile_definitions(hypergryph_account_platform_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(hypergryph_account_platform_tests PRIVATE hud_hypergryph_account_native windowscodecs ole32)
    add_test(NAME hypergryph_account_platform_contracts COMMAND hypergryph_account_platform_tests)
    # The WebView2 login presenter compiled against a test-only interface stub
    # (tests/hypergryph_account_webview2_stub, NOT the SDK) and driven by
    # in-process fakes: no browser, network, profile or visible window.
    add_executable(hypergryph_account_webview_tests tests/hypergryph_account_webview_tests.cpp native/hypergryph_account_webview.cpp)
    target_include_directories(hypergryph_account_webview_tests BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/hypergryph_account_webview2_stub)
    target_compile_definitions(hypergryph_account_webview_tests PRIVATE EHUD_HAS_WEBVIEW2 _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(hypergryph_account_webview_tests PRIVATE hud_hypergryph_account rpcrt4 ole32 user32 gdi32 shell32)
    add_test(NAME hypergryph_account_webview_contracts COMMAND hypergryph_account_webview_tests)
  endif()
endif()

# Official login over WebView2. The pinned Microsoft.Web.WebView2 package
# files the build needs are vendored, byte for byte, at
# windows/third_party/webview2 (include/WebView2.h, x64/WebView2LoaderStatic.lib;
# flattened from build/native/ because the repository ignores build/
# directories). provenance.json records the version, source URL and the
# SHA-256 of the package and of every file; LICENSE.txt and NOTICE.txt are the
# package's own. Configure verifies those pins. Another SDK may be supplied
# explicitly in its NuGet layout:
#   -DEHUD_WEBVIEW2_SDK_DIR=<Microsoft.Web.WebView2 package root>
# (build/native/include/WebView2.h and build/native/<arch>/WebView2LoaderStatic.lib).
# Only the x64 static loader is vendored; other architectures build without
# the official login (UnavailableLoginPresenter: Connect reports the explicit
# failure). At run time the installed Evergreen WebView2 runtime is required;
# it is not redistributed (no bootstrapper): without it Connect shows the
# localized explanation page linking to Microsoft's official download page.
set(EHUD_WEBVIEW2_SDK_DIR "" CACHE PATH "Optional Microsoft.Web.WebView2 SDK package root (default: the vendored pinned package)")
if(CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64$" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
  set(ehud_webview2_arch arm64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ehud_webview2_arch x64)
else()
  set(ehud_webview2_arch x86)
endif()
set(ehud_webview2_include "")
set(ehud_webview2_loader "")
if(EHUD_WEBVIEW2_SDK_DIR)
  set(ehud_webview2_include "${EHUD_WEBVIEW2_SDK_DIR}/build/native/include")
  set(ehud_webview2_loader "${EHUD_WEBVIEW2_SDK_DIR}/build/native/${ehud_webview2_arch}/WebView2LoaderStatic.lib")
  if(NOT EXISTS "${ehud_webview2_include}/WebView2.h" OR NOT EXISTS "${ehud_webview2_loader}")
    message(FATAL_ERROR "EHUD_WEBVIEW2_SDK_DIR must contain build/native/include/WebView2.h and build/native/${ehud_webview2_arch}/WebView2LoaderStatic.lib: ${EHUD_WEBVIEW2_SDK_DIR}")
  endif()
else()
  set(ehud_webview2_vendored "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2")
  if(EXISTS "${ehud_webview2_vendored}/provenance.json")
    # Every vendored file must match its recorded SHA-256 (no silent drift).
    file(READ "${ehud_webview2_vendored}/provenance.json" ehud_webview2_provenance)
    string(JSON ehud_webview2_version GET "${ehud_webview2_provenance}" version)
    string(JSON ehud_webview2_count LENGTH "${ehud_webview2_provenance}" files)
    math(EXPR ehud_webview2_last "${ehud_webview2_count} - 1")
    foreach(ehud_index RANGE ${ehud_webview2_last})
      string(JSON ehud_webview2_file MEMBER "${ehud_webview2_provenance}" files ${ehud_index})
      string(JSON ehud_webview2_pin GET "${ehud_webview2_provenance}" files "${ehud_webview2_file}" sha256)
      if(NOT EXISTS "${ehud_webview2_vendored}/${ehud_webview2_file}")
        message(FATAL_ERROR "Vendored WebView2 SDK file is missing: ${ehud_webview2_file}")
      endif()
      file(SHA256 "${ehud_webview2_vendored}/${ehud_webview2_file}" ehud_webview2_actual)
      if(NOT ehud_webview2_actual STREQUAL ehud_webview2_pin)
        message(FATAL_ERROR "Vendored WebView2 SDK file does not match its pin: ${ehud_webview2_file}")
      endif()
    endforeach()
    if(EXISTS "${ehud_webview2_vendored}/${ehud_webview2_arch}/WebView2LoaderStatic.lib")
      set(ehud_webview2_include "${ehud_webview2_vendored}/include")
      set(ehud_webview2_loader "${ehud_webview2_vendored}/${ehud_webview2_arch}/WebView2LoaderStatic.lib")
    elseif(WIN32)
      message(STATUS "Vendored WebView2 SDK ${ehud_webview2_version} has no ${ehud_webview2_arch} loader: official login unavailable in this build")
    endif()
  endif()
endif()
if(WIN32 AND ehud_webview2_loader)
  add_library(hud_hypergryph_account_webview native/hypergryph_account_webview.cpp)
  target_include_directories(hud_hypergryph_account_webview PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # The SDK header is third-party: include it as a system directory.
  target_include_directories(hud_hypergryph_account_webview SYSTEM PRIVATE "${ehud_webview2_include}")
  target_compile_definitions(hud_hypergryph_account_webview PUBLIC EHUD_HAS_WEBVIEW2 PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_hypergryph_account_webview PUBLIC hud_hypergryph_account PRIVATE "${ehud_webview2_loader}" rpcrt4 ole32 user32 gdi32 advapi32 version shlwapi shell32)
  message(STATUS "Account Linking official login: WebView2 SDK (${ehud_webview2_arch}) from ${ehud_webview2_loader}")
endif()

if(WIN32)
  # Production owner bundle: services + login + module face + header wallet,
  # wrapped by the Application's ModuleOwner adapter.
  # UI Automation peers (native/account_automation.*) ride with the owner.
  add_library(hud_hypergryph_account_owner tools/account_linking_owner.cpp native/account_automation.cpp)
  target_include_directories(hud_hypergryph_account_owner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_hypergryph_account_owner PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_hypergryph_account_owner PUBLIC hud_hypergryph_account_scene hud_hypergryph_account_native PRIVATE uiautomationcore oleaut32)
  if(TARGET hud_hypergryph_account_webview)
    target_link_libraries(hud_hypergryph_account_owner PRIVATE hud_hypergryph_account_webview)
  endif()
  if(BUILD_TESTING)
    add_executable(hypergryph_account_owner_tests tests/hypergryph_account_owner_tests.cpp)
    target_compile_definitions(hypergryph_account_owner_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(hypergryph_account_owner_tests PRIVATE hud_hypergryph_account_owner ole32)
    add_test(NAME hypergryph_account_owner_contracts COMMAND hypergryph_account_owner_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/native/hud.hlsl ${CMAKE_CURRENT_SOURCE_DIR}/resources/account)
  endif()

  # --- Module area (app/module_owner.hpp, "Adding a module area") ----------
  # EndfieldHUD.exe and hidden coverage construct the owner through
  # endfield::areas::accountModule; hidden coverage uses the synthetic
  # providers in app/areas/account_coverage.* and the fixture below.
  add_library(hud_account_area STATIC app/areas/account_module.cpp app/areas/account_coverage.cpp)
  target_include_directories(hud_account_area PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_account_area PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_account_area PUBLIC hud_hypergryph_account_owner hud_module_registry hud_utility hud_data PRIVATE user32)
  if(MSVC)
    target_compile_options(hud_account_area PRIVATE /W4 /permissive- /EHsc)
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREAS
    "account|hud_account_area|app/areas/account_module.hpp|endfield::areas::accountModule")
  # The WebView2 SDK's static loader is linked into EndfieldHUD.exe, so its
  # licence and third-party notices ship in the package (BSD-style licence:
  # binary redistributions reproduce the notice). Hidden coverage checks the
  # staged copies.
  if(ehud_webview2_loader AND NOT EHUD_WEBVIEW2_SDK_DIR)
    set(ehud_webview2_notices "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2")
  elseif(ehud_webview2_loader)
    set(ehud_webview2_notices "${EHUD_WEBVIEW2_SDK_DIR}")
  else()
    set(ehud_webview2_notices "")
  endif()
  if(ehud_webview2_notices AND EXISTS "${ehud_webview2_notices}/LICENSE.txt" AND EXISTS "${ehud_webview2_notices}/NOTICE.txt")
    target_compile_definitions(hud_account_area PRIVATE EHUD_ACCOUNT_WEBVIEW2_NOTICES)
    foreach(ehud_notice IN ITEMS License:LICENSE.txt Notice:NOTICE.txt)
      string(REPLACE ":" ";" ehud_notice "${ehud_notice}")
      list(GET ehud_notice 0 ehud_notice_id)
      list(GET ehud_notice 1 ehud_notice_file)
      if(COMMAND ehud_app_resource)
        ehud_app_resource(account.webview2${ehud_notice_id} file "${ehud_webview2_notices}/${ehud_notice_file}" "licenses/webview2/${ehud_notice_file}")
      else()
        set_property(GLOBAL APPEND PROPERTY EHUD_APP_RESOURCE_SPECS
          "account.webview2${ehud_notice_id}|file|${ehud_webview2_notices}/${ehud_notice_file}|licenses/webview2/${ehud_notice_file}")
      endif()
    endforeach()
  endif()
  set_property(GLOBAL APPEND PROPERTY EHUD_APP_AREA_FIXTURES
    "account.coverage|${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hypergryph-account-coverage.json")
  # Header wallet sprites and numerals (pinned by GaugeAssets::load), staged
  # as resources/account. This fragment sorts before app.cmake, so it appends
  # the ehud_app_resource() record directly.
  if(COMMAND ehud_app_resource)
    ehud_app_resource(account.gauge directory "${CMAKE_CURRENT_SOURCE_DIR}/resources/account" "account")
  else()
    set_property(GLOBAL APPEND PROPERTY EHUD_APP_RESOURCE_SPECS "account.gauge|directory|${CMAKE_CURRENT_SOURCE_DIR}/resources/account|account")
  endif()
  if(BUILD_TESTING)
    # Synthetic coverage providers and the oracle-derived coverage fixture
    # (tools/hypergryph_account_coverage_fixture.py): no network or browser.
    # The HUD window's UIA root (app/hud_automation.cpp, unchanged host
    # source) is compiled in to drive the area's ModuleOwner hooks under it.
    add_executable(hypergryph_account_area_tests tests/hypergryph_account_area_tests.cpp app/hud_automation.cpp)
    target_compile_definitions(hypergryph_account_area_tests PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
    target_link_libraries(hypergryph_account_area_tests PRIVATE hud_account_area uiautomationcore oleaut32 ole32)
    add_test(NAME hypergryph_account_area_contracts COMMAND hypergryph_account_area_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hypergryph-account-coverage.json ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hypergryph-account-source.json)
  endif()
endif()
