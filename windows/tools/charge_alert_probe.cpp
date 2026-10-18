// User-run, read-only diagnostics for the floating charge alert. The default
// invocation is inert (the registered test checks only that).
//   --screens         list monitor work areas, DPI, identity and the preferred display
//   --layered-target  create the alert's hidden click-through layered window and
//                     bind a composition renderer to it; the window is never shown
//   --raster-timing   time one ChargeIndicatorView content raster (the shared
//                     badge/alert scene) per quarter density step, in memory
//   --residency <hud.hlsl> hardware|warp
//                     what the floating alert keeps resident and what its first
//                     frame costs: process private bytes, working set, threads
//                     and handles while an alert shows, once it is ordered out
//                     and parked, and over repeated alerts, next to a stand-in
//                     HUD device of the same driver; hidden offscreen windows and
//                     a synthetic reading only (a temporary copy of the shader
//                     times runtime compilation)
//   --device-threads <hud.hlsl> default|single|prevent|both
//                     what one more hardware D3D11 device (the alert's second
//                     device next to a stand-in HUD device) costs in threads and
//                     private bytes with the given creation flags: BGRA only,
//                     + SINGLETHREADED, + PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,
//                     or both; its hud.hlsl programs, targets and one clear
#ifdef _WIN32
#include "native/charge_indicator_panel.hpp"
#include "native/charge_indicator_scene.hpp"
#include "native/layer_scene.hpp"
#include <objbase.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>

using namespace endfield;

namespace {
struct Usage {
    std::uint64_t privateBytes{}, workingSet{};
    unsigned threads{};
    unsigned long handles{};
};
Usage usage() {
    Usage u;
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof memory;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof memory)) {
        u.privateBytes = memory.PrivateUsage;
        u.workingSet = memory.WorkingSetSize;
    }
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry{};
        entry.dwSize = sizeof entry;
        for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry))
            if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++u.threads;
        CloseHandle(snapshot);
    }
    DWORD handles{};
    if (GetProcessHandleCount(GetCurrentProcess(), &handles)) u.handles = handles;
    return u;
}
void print(const char* stage, const Usage& u, const Usage& from) {
    const auto mb = [](double bytes) { return bytes / (1024.0 * 1024.0); };
    std::cout << stage << ": private " << mb(static_cast<double>(u.privateBytes)) << " MB (" << (u.privateBytes >= from.privateBytes ? "+" : "-")
              << mb(static_cast<double>(u.privateBytes >= from.privateBytes ? u.privateBytes - from.privateBytes : from.privateBytes - u.privateBytes))
              << "), working set " << mb(static_cast<double>(u.workingSet)) << " MB, threads " << u.threads << " (" << static_cast<int>(u.threads) - static_cast<int>(from.threads)
              << "), handles " << u.handles << " (" << static_cast<long>(u.handles) - static_cast<long>(from.handles) << ")\n";
}
template <class F> double milliseconds(F&& work) {
    LARGE_INTEGER frequency{}, start{}, end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    work();
    QueryPerformanceCounter(&end);
    return 1000.0 * static_cast<double>(end.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart);
}
void describe(const native::ChargePanelResidency& r) {
    std::cout << "  residency: window " << r.window << ", device " << r.device << ", embedded programs " << r.embeddedShaders << ", GPU resources "
              << r.gpuBytes << " B, targets " << r.targetBytes << " B, CPU pixels " << r.cpuBytes << " B, creations " << r.creations << ", parks "
              << r.parks << ", last creation " << r.lastCreationMilliseconds << " ms, last first frame " << r.lastFirstFrameMilliseconds
              << " ms (production postpones the presentation by it; hidden panels by firstFrameLagForTests)\n";
}
int residency(const std::filesystem::path& shader, bool hardware) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    int result = 0;
    std::filesystem::path compiled;
    try {
        std::cout << "driver: " << (hardware ? "hardware" : "WARP") << " (offscreen, hidden), build-time programs: " << native::Renderer::shadersEmbedded() << '\n';
        native::LayerRasterizer raster;
        // A stand-in for the HUD's own device: the alert's device is the
        // second one in the process, as in EndfieldHUD.
        WNDCLASSW c{};
        c.lpfnWndProc = DefWindowProcW;
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpszClassName = L"EndfieldChargeResidencyProbe";
        RegisterClassW(&c);
        const HWND hud = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, c.lpszClassName, L"EndfieldHUD probe", WS_POPUP, 0, 0, 640, 400, nullptr, nullptr, c.hInstance, nullptr);
        native::Renderer stand;
        stand.initialize(hud, 640, 400, {hardware ? native::Driver::hardware : native::Driver::warpForTests, shader, native::RenderTarget::offscreenForTests});
        stand.setCamera(native::layerViewportProjection(640, 400));
        stand.draw(false);
        const std::vector<modules::ChargeWorkArea> screens{{11, {0, 0, 2880, 1800}, 1.5}};
        modules::BatteryReading reading;
        reading.present = true;
        reading.percentage = 63;
        reading.capacity = modules::BatteryReading::Capacity{32'760, 52'000, "mWh"};
        const auto setUp = [&](native::ChargeIndicatorPanel& panel) {
            panel.setScreens(screens, 0);
            panel.setAppearance({});
            panel.state().update(reading, false, 0);
        };
        Sleep(300);
        const auto base = usage();
        print("baseline (rasterizer + stand-in HUD device)", base, base);
        native::ChargeIndicatorPanel panel(raster, {shader, true, hardware});
        setUp(panel);
        double time = 1;
        // Plug-in alert: window, device and programs created on demand.
        reading.pluggedIn = reading.charging = true;
        panel.state().update(reading, false, time);
        panel.state().show(false, 3, true, time);
        const double first = milliseconds([&] { panel.advance(time); });
        for (const double end = time + 1.6; time < end; time += 1.0 / 60) panel.advance(time);
        Sleep(300);
        std::cout << "plug-in alert, first frame creating the window and device on demand: " << first << " ms\n";
        print("while the alert shows", usage(), base);
        describe(panel.residency());
        for (const double end = time + 4; time < end; time += 1.0 / 60) panel.advance(time);
        const auto ordered = usage();
        print("ordered out (not yet parked)", ordered, base);
        const double parking = milliseconds([&] { (void)panel.park(); });
        Sleep(500);
        std::cout << "park (window, device, swap chain and textures released): " << parking << " ms\n";
        print("parked: window, device and textures released", usage(), base);
        describe(panel.residency());
        // Ten more alerts, each parked afterwards: what stays per cycle
        // (threads a driver or the system pool keeps idle for a while).
        for (unsigned cycle = 1; cycle <= 10; ++cycle) {
            reading.pluggedIn = reading.charging = cycle % 2 == 0;
            panel.state().update(reading, false, time);
            panel.state().show(false, 3, true, time);
            const double frame = milliseconds([&] { panel.advance(time); });
            for (const double end = time + 5.6; time < end; time += 1.0 / 60) panel.advance(time);
            const double released = milliseconds([&] { (void)panel.park(); });
            Sleep(300);
            std::cout << "cycle " << cycle << ": first frame " << frame << " ms (creation " << panel.residency().lastCreationMilliseconds << " ms), park "
                      << released << " ms; ";
            print("parked", usage(), base);
        }
        // Idle pool and driver threads retire on their own timeouts.
        Sleep(5000);
        print("parked, 5 s after the last cycle", usage(), base);
        // The same alert with a byte-different shader source: runtime
        // compilation of every program (what builds without fxc pay).
        compiled = std::filesystem::temp_directory_path() / (L"endfield-charge-probe-" + std::to_wstring(GetCurrentProcessId()) + L".hlsl");
        {
            std::ifstream in(shader, std::ios::binary);
            std::ofstream out(compiled, std::ios::binary);
            out << in.rdbuf() << "\n// probe copy\n";
        }
        native::ChargeIndicatorPanel runtime(raster, {compiled, true, hardware});
        setUp(runtime);
        runtime.state().show(false, 3, true, time);
        const double runtimeFirst = milliseconds([&] { runtime.advance(time); });
        std::cout << "first frame with runtime-compiled programs: " << runtimeFirst << " ms (embedded " << runtime.residency().embeddedShaders
                  << ", creation " << runtime.residency().lastCreationMilliseconds << " ms)\n";
        runtime.shutdown();
        panel.shutdown();
        stand.reset();
        DestroyWindow(hud);
        UnregisterClassW(c.lpszClassName, c.hInstance);
    } catch (const std::exception& e) {
        std::cout << "residency probe failed: " << e.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    if (!compiled.empty()) std::filesystem::remove(compiled, ignored);
    CoUninitialize();
    return result;
}
// One hardware device with its hud.hlsl programs, the alert-sized BGRA8 and
// RGBA16F targets and one clear, as the renderer creates them.
struct RawDevice {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    std::vector<Microsoft::WRL::ComPtr<ID3D11DeviceChild>> children;
};
struct Programs { std::vector<std::pair<bool, Microsoft::WRL::ComPtr<ID3DBlob>>> blobs; };
Programs compilePrograms(const std::filesystem::path& shader) {
    Programs programs;
    constexpr std::array<std::pair<const char*, const char*>, 7> entries{{{"SceneVS", "vs_5_0"}, {"ScenePS", "ps_5_0"}, {"SceneEncodedInputPS", "ps_5_0"},
        {"SceneEncodedOutputPS", "ps_5_0"}, {"SceneEncodedBothPS", "ps_5_0"}, {"CompositeVS", "vs_5_0"}, {"CompositePS", "ps_5_0"}}};
    for (const auto& [entry, target] : entries) {
        Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
        if (FAILED(D3DCompileFromFile(shader.c_str(), nullptr, nullptr, entry, target, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors)))
            throw std::runtime_error(std::string("compile ") + entry);
        programs.blobs.emplace_back(target[0] == 'v', blob);
    }
    return programs;
}
RawDevice createDevice(UINT flags, const Programs& programs) {
    RawDevice d;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 1, D3D11_SDK_VERSION, &d.device, nullptr, &d.context)))
        throw std::runtime_error("hardware D3D11 device unavailable");
    for (const auto& [vertex, blob] : programs.blobs) {
        Microsoft::WRL::ComPtr<ID3D11DeviceChild> child;
        if (vertex) {
            Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
            if (FAILED(d.device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader))) throw std::runtime_error("vertex shader");
            child = shader;
        } else {
            Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
            if (FAILED(d.device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader))) throw std::runtime_error("pixel shader");
            child = shader;
        }
        d.children.push_back(child);
    }
    for (const auto format : {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT}) {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = 600;
        description.Height = 168;
        description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
        description.Format = format;
        description.BindFlags = D3D11_BIND_RENDER_TARGET;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
        if (FAILED(d.device->CreateTexture2D(&description, nullptr, &texture)) || FAILED(d.device->CreateRenderTargetView(texture.Get(), nullptr, &view)))
            throw std::runtime_error("target");
        const float clear[4]{0, 0, 0, 0};
        d.context->ClearRenderTargetView(view.Get(), clear);
        d.children.push_back(view);
    }
    d.context->Flush();
    return d;
}
int deviceThreads(const std::filesystem::path& shader, std::wstring_view mode) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (mode == L"single" || mode == L"both") flags |= D3D11_CREATE_DEVICE_SINGLETHREADED;
    if (mode == L"prevent" || mode == L"both") flags |= D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    try {
        const auto programs = compilePrograms(shader);
        auto hud = createDevice(D3D11_CREATE_DEVICE_BGRA_SUPPORT, programs); // the HUD renderer's flags
        Sleep(300);
        const auto base = usage();
        print("stand-in HUD device (BGRA only)", base, base);
        double created{};
        std::optional<RawDevice> alert;
        created = milliseconds([&] { alert = createDevice(flags, programs); });
        Sleep(300);
        std::cout << "second device flags 0x" << std::hex << flags << std::dec << ": created with programs, targets and a clear in " << created << " ms\n";
        print("second device", usage(), base);
        alert.reset();
        Sleep(500);
        print("second device released", usage(), base);
    } catch (const std::exception& e) {
        std::cout << "device thread probe failed: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring_view(argv[1]) == L"--device-threads") {
        const std::wstring_view mode = argv[3];
        if (mode == L"default" || mode == L"single" || mode == L"prevent" || mode == L"both") return deviceThreads(argv[2], mode);
    }
    if (argc == 4 && std::wstring_view(argv[1]) == L"--residency" && (std::wstring_view(argv[3]) == L"hardware" || std::wstring_view(argv[3]) == L"warp"))
        return residency(argv[2], std::wstring_view(argv[3]) == L"hardware");
    const std::wstring_view mode = argc == 3 || argc == 2 ? argv[1] : L"";
    if (mode == L"--screens") {
        const auto scan = native::scanChargeScreens();
        for (std::size_t n = 0; n < scan.screens.size(); ++n) {
            const auto& s = scan.screens[n];
            std::wcout << L"screen " << n << L" id=" << s.id << L" work=" << s.bounds.x << L',' << s.bounds.y << L' ' << s.bounds.width
                       << L'x' << s.bounds.height << L" pixelsPerPoint=" << s.pixelsPerPoint << (scan.preferred == n ? L" preferred" : L"")
                       << L" identity=" << scan.identities[n] << L'\n';
        }
        return scan.screens.empty() ? 1 : 0;
    }
    if (mode == L"--layered-target" && argc == 3) {
        WNDCLASSW c{};
        c.lpfnWndProc = DefWindowProcW;
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpszClassName = L"EndfieldChargeAlertProbe";
        RegisterClassW(&c);
        for (const bool layered : {true, false}) {
            HWND window = CreateWindowExW(native::chargeAlertWindowStyle(layered), c.lpszClassName, L"EndfieldHUD probe", WS_POPUP, 0, 0, 300, 84,
                                          nullptr, nullptr, c.hInstance, nullptr);
            if (!window) { std::cout << "layered=" << layered << " create failed " << GetLastError() << '\n'; continue; }
            if (layered) SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
            for (const auto driver : {native::Driver::hardware, native::Driver::warpForTests}) {
                native::Renderer renderer;
                try {
                    renderer.initialize(window, 300, 84, {driver, argv[2], native::RenderTarget::composition});
                    renderer.setCamera(native::layerViewportProjection(300, 84));
                    renderer.draw(true);
                    std::cout << "layered=" << layered << " driver=" << (driver == native::Driver::hardware ? "hardware" : "warp")
                              << " composition target: ok, visible=" << IsWindowVisible(window) << '\n';
                } catch (const std::exception& e) {
                    std::cout << "layered=" << layered << " driver=" << (driver == native::Driver::hardware ? "hardware" : "warp")
                              << " composition target: " << e.what() << '\n';
                }
                renderer.reset();
            }
            DestroyWindow(window);
        }
        UnregisterClassW(c.lpszClassName, c.hInstance);
        return 0;
    }
    if (mode == L"--raster-timing") {
        // The UI-thread work one density change costs (synthetic reading,
        // private rasterizer, no window or device): median of five passes per
        // step, each a change from the previous step.
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
        int result = 0;
        try {
            native::LayerRasterizer raster;
            native::ChargeIndicatorScene scene(raster, "probe.charge");
            native::ChargeIndicatorSceneContent content;
            content.battery.present = content.battery.pluggedIn = content.battery.charging = true;
            content.battery.percentage = 63;
            content.pixelsPerPoint = 2;
            std::cout << "first content raster at 2x (cold rasterizer): " << milliseconds([&] { (void)scene.setContent(content); }) << " ms\n";
            constexpr std::array<double, 6> steps{1, 1.25, 1.5, 1.75, 2, 2.5};
            std::array<std::vector<double>, steps.size()> runs;
            for (unsigned round = 0; round < 5; ++round)
                for (std::size_t n = 0; n < steps.size(); ++n) {
                    content.pixelsPerPoint = steps[n];
                    runs[n].push_back(milliseconds([&] { (void)scene.setContent(content); }));
                }
            for (std::size_t n = 0; n < steps.size(); ++n) {
                std::sort(runs[n].begin(), runs[n].end());
                std::cout << "density change to " << steps[n] << "x: median " << runs[n][2] << " ms (min " << runs[n].front() << ", max "
                          << runs[n].back() << ")\n";
            }
        } catch (const std::exception& e) {
            std::cout << "raster timing failed: " << e.what() << '\n';
            result = 1;
        }
        CoUninitialize();
        return result;
    }
    std::cout << "charge_alert_probe: inert. Pass --screens, --raster-timing, --layered-target <hud.hlsl> (hidden window), "
                 "--residency <hud.hlsl> hardware|warp (hidden offscreen windows), or --device-threads <hud.hlsl> default|single|prevent|both.\n";
    return 0;
}
#else
#include <iostream>
int main() { std::cout << "charge_alert_probe: inert (Windows only)\n"; return 0; }
#endif
