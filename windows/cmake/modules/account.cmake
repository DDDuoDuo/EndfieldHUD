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
  modules/hypergryph_account_canvas.cpp)
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
  add_executable(hypergryph_account_controller_tests tests/hypergryph_account_controller_tests.cpp)
  target_link_libraries(hypergryph_account_controller_tests PRIVATE hud_hypergryph_account)
  add_test(NAME hypergryph_account_controller_contracts COMMAND hypergryph_account_controller_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hypergryph-account-source.json)
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
    target_link_libraries(hypergryph_account_webview_tests PRIVATE hud_hypergryph_account rpcrt4 ole32 user32 gdi32)
    add_test(NAME hypergryph_account_webview_contracts COMMAND hypergryph_account_webview_tests)
  endif()
endif()

# Official login over WebView2 is built only when the WebView2 SDK is supplied,
# either explicitly or vendored at windows/third_party/webview2:
#   -DEHUD_WEBVIEW2_SDK_DIR=<Microsoft.Web.WebView2 NuGet package root>
# (build/native/include/WebView2.h and build/native/<arch>/WebView2LoaderStatic.lib).
# The SDK is not part of the Windows SDK and is not vendored in this tree;
# without it the owner links UnavailableLoginPresenter and Connect reports a
# clear failure. At run time the Evergreen WebView2 runtime is also required.
set(EHUD_WEBVIEW2_SDK_DIR "" CACHE PATH "Optional Microsoft.Web.WebView2 SDK package root")
set(ehud_webview2_root "${EHUD_WEBVIEW2_SDK_DIR}")
if(NOT ehud_webview2_root AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/build/native/include/WebView2.h")
  set(ehud_webview2_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2")
endif()
if(WIN32 AND ehud_webview2_root)
  if(CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64$" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
    set(ehud_webview2_arch arm64)
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ehud_webview2_arch x64)
  else()
    set(ehud_webview2_arch x86)
  endif()
  set(ehud_webview2_loader "${ehud_webview2_root}/build/native/${ehud_webview2_arch}/WebView2LoaderStatic.lib")
  if(NOT EXISTS "${ehud_webview2_root}/build/native/include/WebView2.h" OR NOT EXISTS "${ehud_webview2_loader}")
    message(FATAL_ERROR "WebView2 SDK root must contain build/native/include/WebView2.h and build/native/${ehud_webview2_arch}/WebView2LoaderStatic.lib: ${ehud_webview2_root}")
  endif()
  add_library(hud_hypergryph_account_webview native/hypergryph_account_webview.cpp)
  target_include_directories(hud_hypergryph_account_webview PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} "${ehud_webview2_root}/build/native/include")
  target_compile_definitions(hud_hypergryph_account_webview PUBLIC EHUD_HAS_WEBVIEW2 PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_hypergryph_account_webview PUBLIC hud_hypergryph_account PRIVATE "${ehud_webview2_loader}" rpcrt4 ole32 user32 gdi32 advapi32 version)
endif()

if(WIN32)
  # Production owner bundle: services + login + module face + header wallet,
  # wrapped by the Application's ModuleOwner adapter.
  add_library(hud_hypergryph_account_owner tools/account_linking_owner.cpp)
  target_include_directories(hud_hypergryph_account_owner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(hud_hypergryph_account_owner PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
  target_link_libraries(hud_hypergryph_account_owner PUBLIC hud_hypergryph_account_scene hud_hypergryph_account_native)
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
endif()
