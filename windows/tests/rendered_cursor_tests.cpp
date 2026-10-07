#include "platform/rendered_cursor.h"
#include "resources/resource_data.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <roapi.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
using ehud::platform::CursorRegion;
using ehud::platform::RenderedCursor;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void checked(HRESULT hr) { check(SUCCEEDED(hr), "Native cursor fixture API failed"); }
struct IconInfo {
    ICONINFO info{};
    ~IconInfo() { if (info.hbmColor) DeleteObject(info.hbmColor); if (info.hbmMask) DeleteObject(info.hbmMask); }
};
void compare_source_pixels(HCURSOR cursor, const std::filesystem::path& directory) {
    // Read only the isolated cursor bitmap. No GetDC(HWND_DESKTOP), screen
    // capture or visible cursor selection is performed by this fixture.
    Ptr<IWICImagingFactory> factory;
    checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    Ptr<IWICBitmapDecoder> decoder;
    checked(factory->CreateDecoderFromFilename((directory / L"player-default-icon_mouse.png").c_str(),
        nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder));
    Ptr<IWICBitmapFrameDecode> frame; checked(decoder->GetFrame(0, &frame));
    Ptr<IWICBitmapSource> converted;
    checked(WICConvertBitmapSource(GUID_WICPixelFormat32bppPBGRA, frame.Get(), &converted));
    UINT width{}, height{}; checked(converted->GetSize(&width, &height));
    std::vector<BYTE> expected(std::size_t(width) * height * 4);
    checked(converted->CopyPixels(nullptr, width * 4, static_cast<UINT>(expected.size()), expected.data()));

    IconInfo native;
    check(GetIconInfo(cursor, &native.info) != FALSE, "GetIconInfo returns native cursor pixels");
    check(!native.info.fIcon && native.info.xHotspot == 0 && native.info.yHotspot == 0,
        "Cursor uses the exact original PlayerSettings hotspot");
    BITMAP color{}, mask{};
    check(GetObjectW(native.info.hbmColor, sizeof(color), &color) == sizeof(color), "Native color bitmap exists");
    check(GetObjectW(native.info.hbmMask, sizeof(mask), &mask) == sizeof(mask), "Native mask bitmap exists");
    check(color.bmWidth == 58 && color.bmHeight == 58 && mask.bmWidth == 58 && mask.bmHeight == 58,
        "Cursor retains the original 58 physical pixels without DPI resampling");
    BITMAPINFO description{};
    description.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    description.bmiHeader.biWidth = static_cast<LONG>(width);
    description.bmiHeader.biHeight = -static_cast<LONG>(height);
    description.bmiHeader.biPlanes = 1; description.bmiHeader.biBitCount = 32;
    description.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> actual(expected.size());
    HDC dc = CreateCompatibleDC(nullptr);
    check(dc != nullptr, "Isolated memory DC exists");
    const int copied = GetDIBits(dc, native.info.hbmColor, 0, height, actual.data(), &description, DIB_RGB_COLORS);
    DeleteDC(dc);
    check(copied == static_cast<int>(height), "Native cursor readback covers every row");
    check(actual == expected, "Native premultiplied BGRA matches every approved PNG pixel and row orientation");

    struct MaskDescription { BITMAPINFOHEADER header; RGBQUAD colors[2]; } maskDescription{};
    maskDescription.header = description.bmiHeader; maskDescription.header.biBitCount = 1;
    maskDescription.colors[1] = {255,255,255,0};
    const unsigned stride = ((width + 31) / 32) * 4;
    std::vector<BYTE> maskPixels(std::size_t(stride) * height);
    dc = CreateCompatibleDC(nullptr);
    check(dc != nullptr, "Isolated mask DC exists");
    const int maskCopied = GetDIBits(dc, native.info.hbmMask, 0, height, maskPixels.data(),
        reinterpret_cast<BITMAPINFO*>(&maskDescription), DIB_RGB_COLORS);
    DeleteDC(dc);
    check(maskCopied == static_cast<int>(height), "Native AND-mask readback covers every row");
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        const bool transparent = expected[(std::size_t(y) * width + x) * 4 + 3] == 0;
        const bool bit = (maskPixels[std::size_t(y) * stride + x / 8] & (0x80 >> (x % 8))) != 0;
        check(bit == transparent, "Native AND mask preserves exactly the source transparent pixels");
    }
}
void activate(RenderedCursor& cursor) {
    cursor.set_presented(true); cursor.set_focused(true); cursor.set_region(CursorRegion::source);
    check(cursor.refresh(), "Eligible HUD acquires its native source cursor");
}
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        path = std::filesystem::temp_directory_path() /
            (L"EndfieldHUD-cursor-fixture-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        check(std::filesystem::create_directory(path), "Create new isolated cursor fixture directory");
    }
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
void write(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(bool(output), "Write isolated cursor fixture");
}
void rejected_assets(RenderedCursor& cursor, HWND owner, const std::filesystem::path& source) {
    TemporaryDirectory scratch;
    activate(cursor);
    const HCURSOR original = cursor.handle();
    const auto sourceSets = cursor.snapshot().source_sets;
    check(FAILED(cursor.initialize(owner, scratch.path)) && cursor.handle() == original,
        "Missing approved artwork cannot silently replace an existing cursor");
    using namespace winrt::Windows::Data::Json;
    auto manifest = JsonObject::Parse(winrt::to_hstring(ehud::resources::text(source / L"player-default.json")));
    std::filesystem::copy_file(source / L"player-default-icon_mouse.png", scratch.path / L"player-default-icon_mouse.png");
    manifest.SetNamedValue(L"width", JsonValue::CreateNumberValue(58.5));
    write(scratch.path / L"player-default.json", winrt::to_string(manifest.Stringify()));
    check(FAILED(cursor.initialize(owner, scratch.path)) && cursor.handle() == original,
        "Fractional source dimensions are rejected before conversion/allocation");
    manifest.SetNamedValue(L"width", JsonValue::CreateNumberValue(58));
    manifest.SetNamedValue(L"png_sha256", JsonValue::CreateStringValue(L"not-the-original-png"));
    write(scratch.path / L"player-default.json", winrt::to_string(manifest.Stringify()));
    check(FAILED(cursor.initialize(owner, scratch.path)) && cursor.handle() == original,
        "Incorrect approved PNG hash is rejected without changing current ownership");
    manifest.SetNamedValue(L"png_sha256", JsonValue::CreateStringValue(
        L"d479751c9298b9ec582dcaddb393e4f4e92c7e66511a6db19ed2e666ac9b8789"));
    manifest.SetNamedValue(L"height", JsonValue::CreateNumberValue(57));
    write(scratch.path / L"player-default.json", winrt::to_string(manifest.Stringify()));
    check(FAILED(cursor.initialize(owner, scratch.path)) && cursor.handle() == original,
        "Manifest and native bitmap dimensions must agree");
    check(cursor.snapshot().owns_cursor && cursor.snapshot().source_sets == sourceSets,
        "Failed reloads leave the active source lease and cursor-selection count untouched");
    cursor.set_presented(false);
}
void ownership(HWND owner, HWND child, const std::filesystem::path& source) {
    const HCURSOR arrow = LoadCursorW(nullptr, IDC_ARROW), resize = LoadCursorW(nullptr, IDC_SIZEWE), text = LoadCursorW(nullptr, IDC_IBEAM);
    HCURSOR current = arrow;
    std::uint64_t sets{}, reads{}; bool queryWorks = true;
    RenderedCursor cursor({[&](HCURSOR& value) { ++reads; value = current; return queryWorks; },
        [&](HCURSOR value) { const auto previous = current; current = value; ++sets; return previous; }});
    checked(cursor.initialize(owner, source));
    auto state = cursor.snapshot();
    check(state.live_handles == 1 && state.width == 58 && state.height == 58 && state.hotspot_x == 0 && state.hotspot_y == 0,
        "Exactly one approved source HCURSOR is retained");
    check(state.png_sha256 == "d479751c9298b9ec582dcaddb393e4f4e92c7e66511a6db19ed2e666ac9b8789",
        "Fixture verifies the exact approved source PNG hash");
    check(!state.recording_verified && !state.runtime_game_override_verified,
        "Native fixture does not claim external recording/runtime-game acceptance");
    compare_source_pixels(cursor.handle(), source);
    check(!cursor.refresh() && sets == 0, "Hidden source never selects a cursor");
    cursor.set_presented(true); cursor.set_region(CursorRegion::source);
    check(!cursor.refresh() && sets == 0, "Unfocused source never selects a cursor");
    cursor.set_focused(true);
    check(sets == 0, "Lifecycle setters wait for explicit fresh pointer arbitration");
    check(cursor.handle_set_cursor(owner, HTCLIENT) && current == cursor.handle(), "Owner client WM_SETCURSOR selects approved artwork");
    const auto idleSets = sets, idleReads = reads;
    for (int i = 0; i < 1000; ++i) check(cursor.snapshot().owns_cursor, "Idle ownership remains stable");
    check(sets == idleSets && reads == idleReads, "Idle diagnostics do not poll or repeatedly set the pointer");
    check(cursor.refresh() && sets == idleSets, "Redundant event refresh does not repeatedly set an unchanged cursor");
    cursor.set_region(CursorRegion::outside);
    check(current == arrow && !cursor.snapshot().owns_cursor, "Pointer exit balances source ownership and restores preceding shape");

    activate(cursor); current = text;
    const auto nativeSets = sets;
    cursor.set_region(CursorRegion::native);
    check(current == text && sets == nativeSets, "Already-selected native editor cursor survives source yielding");
    check(!cursor.handle_set_cursor(owner, HTCLIENT) && sets == nativeSets, "Native text region takes precedence over owner WM_SETCURSOR");
    activate(cursor); current = resize;
    cursor.begin_native_tracking(); cursor.begin_native_tracking();
    cursor.set_region(CursorRegion::source);
    const auto trackingSets = sets;
    check(!cursor.refresh() && current == resize, "Nested native dialogs/drags veto source cursor refresh");
    cursor.end_native_tracking();
    check(!cursor.refresh() && sets == trackingSets, "Outer native tracking lease still protects cursor after inner completion");
    cursor.end_native_tracking();
    check(current == resize && sets == trackingSets, "Native tracking completion never blindly reacquires stale pointer ownership");
    check(cursor.refresh() && current == cursor.handle(), "Fresh caller arbitration resumes source after native tracking");
    cursor.set_focused(false);
    check(current == resize, "Focus loss restores the cursor that preceded the new source lease");

    current = arrow; activate(cursor);
    const auto nonclientSets = sets;
    check(!cursor.handle_set_cursor(owner, HTLEFT) && sets == nonclientSets,
        "Native resize arbitration does not set an intermediate arrow");
    current = resize; cursor.set_focused(false);
    check(current == resize && sets == nonclientSets, "Focus loss cannot clobber a resize cursor chosen after nonclient arbitration");
    current = arrow; activate(cursor);
    const auto childSets = sets;
    check(!cursor.handle_set_cursor(child, HTCLIENT) && sets == childSets,
        "A child-owned cursor region falls through without an intermediate arrow");
    current = text; cursor.set_presented(false);
    check(current == text && sets == childSets, "Concealment does not overwrite a cursor another owner already selected");

    current = arrow; activate(cursor);
    cursor.set_input_enabled(false);
    check(current == arrow && !cursor.refresh(), "HWND pointer disablement releases source cursor");
    cursor.set_input_enabled(true); queryWorks = false;
    const auto failedQuerySets = sets;
    check(!cursor.refresh() && sets == failedQuerySets, "Failed global cursor query cannot force a cursor shape");
    queryWorks = true; cursor.set_presented(false);
    rejected_assets(cursor, owner, source);
    check(cursor.snapshot().live_handles == 1, "Rejected reloads retain exactly the previous native cursor");

    const auto creations = cursor.snapshot().creations;
    for (int cycle = 0; cycle < 100; ++cycle) {
        current = arrow; activate(cursor); cursor.set_presented(false);
        check(current == arrow && cursor.snapshot().live_handles == 1 && !cursor.snapshot().owns_cursor,
            "100 close/reopen cycles retain one cursor and balanced ownership");
    }
    check(cursor.snapshot().creations == creations, "Reopening does not recreate or accumulate cursor images");
    // Real native creation/destruction exercise, with selection still mocked.
    const DWORD initialGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD initialUser = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int cycle = 0; cycle < 100; ++cycle) {
        cursor.reset(); check(cursor.snapshot().live_handles == 0, "Reset releases the native cursor handle");
        checked(cursor.initialize(owner, source));
    }
    check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= initialGdi + 2 &&
        GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= initialUser + 2,
        "100 native recreate cycles keep GDI/user handle counts bounded after warmup");
    current = arrow; activate(cursor); cursor.reset();
    const auto final = cursor.snapshot();
    check(current == arrow && !final.owns_cursor && final.live_handles == 0 && final.creations == final.destructions,
        "Teardown restores source ownership before destroying its sole handle");
    std::cout << "approved_cursor=58x58 hotspot=0,0 native_pixel_match=true reopen_cycles=100 recreate_cycles=100 "
        << "creations=" << final.creations << " destructions=" << final.destructions
        << " final_live_handles=" << final.live_handles << " actual_cursor_selection=false recording=unverified\n";
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "usage: rendered_cursor_tests <approved Cursor resource directory>\n"; return 2; }
    const HRESULT initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(initialized)) return 1;
    int result{}; HWND owner{}, child{};
    try {
        WNDCLASSW windowClass{}; windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpfnWndProc = DefWindowProcW; windowClass.lpszClassName = L"EndfieldHUD.CursorFixture";
        check(RegisterClassW(&windowClass) != 0, "Register isolated cursor fixture class");
        owner = CreateWindowExW(0, windowClass.lpszClassName, L"Synthetic cursor fixture", WS_POPUP,
            0, 0, 2, 2, nullptr, nullptr, windowClass.hInstance, nullptr);
        check(owner != nullptr, "Create unshown synthetic owner HWND");
        child = CreateWindowExW(0, L"STATIC", L"Synthetic native child", WS_CHILD,
            0, 0, 1, 1, owner, nullptr, windowClass.hInstance, nullptr);
        check(child != nullptr, "Create unshown synthetic child HWND");
        ownership(owner, child, argv[1]);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    catch (...) { std::cerr << "Native cursor fixture exception\n"; result = 1; }
    if (owner) DestroyWindow(owner);
    UnregisterClassW(L"EndfieldHUD.CursorFixture", GetModuleHandleW(nullptr));
    RoUninitialize(); return result;
}
