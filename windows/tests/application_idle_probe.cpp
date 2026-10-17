// What keeps waking a process that holds the HUD's graphics while nothing is
// drawn (the closed HUD, WINDOWS-MIGRATION.md section 9: closed idle). A
// measurement tool, not a ctest: per phase it idles the given seconds on a
// main thread that only waits, then prints the process's wake-ups (context
// switches) per second and the busiest threads by start module and offset:
//   process      before any graphics device
//   device       the HUD's hardware Renderer as Application creates it in
//                production (D3D11 with PREVENT_INTERNAL_THREADING_
//                OPTIMIZATIONS and VIDEO_SUPPORT for the shared Notes media
//                device) after one drawn frame
//   released     after Renderer::releaseFrameTargets (the concealed HUD's
//                release, IDXGIDevice3::Trim included)
//   reset        after Renderer::reset (the device released)
//   no video     the same device without VIDEO_SUPPORT (comparison)
//   threaded     a device without either flag, after one frame (comparison)
// Offscreen by default (any session); --composition binds the HUD's
// composition swap chain and DirectComposition target to a never-shown
// window instead (an interactive desktop only), and adds:
//   presented    after one presented frame to the hidden window
//   swapless     after Renderer::releasePresentation (swap chain, its frame
//                latency object and the composition target released)
// --activity N instead measures the closed HUD's Activity system sample (the
// host's 5 s history, ActivitySamplingPlan): the context switches and time
// of each of its reads on the calling thread, N samples one second apart, and
// of the hand-off to the shared utility worker and back.
// Usage: application_idle_probe <native/hud.hlsl> [--seconds N] [--warp] [--composition] [--activity N]
#include "native/renderer.hpp"
#include "app/process_diagnostics.hpp"
#include "app/utility_executor.hpp"
#include "native/activity_provider.hpp"
#include "native/activity_disk.hpp"
#include "native/activity_gpu.hpp"
#include <functional>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <objbase.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <timeapi.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <atomic>

namespace gpu = endfield::native;
namespace core = endfield::core;
namespace app = endfield::app;
namespace {
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
struct Window {
    ATOM atom{}; HWND handle{};
    Window() {
        WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"EndfieldIdleProbe";
        atom = RegisterClassW(&type); check(atom != 0, "Probe window class registered");
        handle = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW, type.lpszClassName, L"Idle probe", WS_POPUP, 0, 0, 256, 256, nullptr, nullptr, type.hInstance, nullptr);
        check(handle != nullptr, "Hidden probe window created");
    }
    ~Window() { if (handle) DestroyWindow(handle); if (atom) UnregisterClassW(MAKEINTATOM(atom), GetModuleHandleW(nullptr)); }
};
core::Matrix4 camera() { core::Matrix4 m; m.values = {2. / 256, 0, 0, 0, 0, -2. / 256, 0, 0, 0, 0, 0, 0, -1, 1, 0, 1}; return m; }
constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
constexpr std::array<gpu::Vertex, 4> quad{{{{0, 0, 0}, {0, 0}, {1, 1, 1, 1}}, {{64, 0, 0}, {1, 0}, {1, 1, 1, 1}}, {{64, 64, 0}, {1, 1}, {1, 1, 1, 1}}, {{0, 64, 0}, {0, 1}, {1, 1, 1, 1}}}};
void frame(gpu::Renderer& renderer, bool present) {
    renderer.setCamera(camera());
    renderer.setMesh("quad", 1, {quad, indices});
    const std::array<std::uint8_t, 16> texels{255, 255, 255, 255, 40, 180, 90, 128, 0, 0, 0, 0, 200, 30, 60, 255};
    renderer.setTexture("texels", 1, {2, 2, texels});
    std::array<gpu::DrawObject, 1> list;
    list[0].sourceID = "plain"; list[0].meshID = "quad"; list[0].textureID = "texels"; list[0].world = core::Matrix4::translation(20, 20);
    renderer.setDrawList(list);
    if (present) (void)renderer.waitForPresentation(100);
    renderer.draw(present);
}
// Idles `seconds` with only this thread waiting, then prints the activity.
void idle(const char* phase, double seconds) {
    const auto before = app::captureThreadActivity();
    LARGE_INTEGER frequency{}, start{}, end{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
    Sleep(static_cast<DWORD>(seconds * 1000));
    QueryPerformanceCounter(&end);
    const auto after = app::captureThreadActivity();
    const double elapsed = double(end.QuadPart - start.QuadPart) / double(frequency.QuadPart);
    const auto report = app::threadActivityJSON(before, after, elapsed, 8);
    std::cout << "== " << phase << ": " << report["wakeupsPerSecond"].number() << " wake-ups/s, threads " << report["threads"].number()
              << ", thread CPU " << report["threadCpuPercentOfOneCore"].number() << "% of one core\n";
    for (const auto& row : report["busiestThreads"].array())
        std::cout << "   " << row["module"].string() << "+0x" << std::hex << static_cast<std::uint64_t>(row["offset"].number()) << std::dec
                  << (row["description"].string().empty() ? std::string() : " \"" + row["description"].string() + "\"")
                  << "  " << row["wakeupsPerSecond"].number() << "/s\n";
}
// This thread's context switches so far.
std::uint64_t ownSwitches() {
    const auto self = GetCurrentThreadId();
    for (const auto& thread : app::captureThreadActivity()) if (thread.id == self) return thread.contextSwitches;
    return 0;
}
double seconds() { LARGE_INTEGER f{}, v{}; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&v); return double(v.QuadPart) / double(f.QuadPart); }
void activity(unsigned samples) {
    gpu::WindowsActivityDisk disk;
    const auto uptime = [] { return double(GetTickCount64()) * .001; };
    // The interfaces the system sample counts (up hardware Ethernet/Wi-Fi, PPP).
    std::vector<NET_LUID> counted;
    {
        PMIB_IF_TABLE2 table{};
        if (GetIfTable2(&table) == NO_ERROR && table) {
            for (ULONG n = 0; n < table->NumEntries; ++n) {
                const auto& r = table->Table[n];
                const bool supported = (r.Type == IF_TYPE_ETHERNET_CSMACD || r.Type == IF_TYPE_IEEE80211) ? r.InterfaceAndOperStatusFlags.HardwareInterface != 0 : r.Type == IF_TYPE_PPP;
                if (r.OperStatus == IfOperStatusUp && supported) counted.push_back(r.InterfaceLuid);
            }
            std::cout << "activity network: " << table->NumEntries << " interfaces listed, " << counted.size() << " counted\n";
            FreeMibTable(table);
        }
    }
    struct Reader { const char* name; std::function<void()> read; };
    const std::vector<Reader> readers{
        {"nothing (measurement baseline)", [] {}},
        {"CPU (GetSystemTimes)", [] { FILETIME a{}, b{}, c{}; (void)GetSystemTimes(&a, &b, &c); }},
        {"RAM (GlobalMemoryStatusEx)", [] { MEMORYSTATUSEX m{}; m.dwLength = sizeof(m); (void)GlobalMemoryStatusEx(&m); }},
        {"network (GetIfTable2)", [] { PMIB_IF_TABLE2 table{}; if (GetIfTable2(&table) == NO_ERROR && table) FreeMibTable(table); }},
        {"network (GetIfTable2Ex raw)", [] { PMIB_IF_TABLE2 table{}; if (GetIfTable2Ex(MibIfTableRaw, &table) == NO_ERROR && table) FreeMibTable(table); }},
        {"network (GetIfEntry2 of the counted interfaces)", [&] { for (const auto& luid : counted) { MIB_IF_ROW2 row{}; row.InterfaceLuid = luid; (void)GetIfEntry2(&row); } }},
        {"GPU (PDH engines)", [] { (void)gpu::readWindowsActivityGPU(); }},
        {"disk (PDH PhysicalDisk)", [&] { (void)disk.sample(); }},
        {"whole system sample", [&] { auto value = gpu::readWindowsActivity(uptime(), uptime()); value.disk = disk.sample(); }}};
    for (const auto& reader : readers) {
        reader.read(); // the first call opens queries
        std::uint64_t switches{}; double time{};
        for (unsigned n = 0; n < samples; ++n) {
            Sleep(1000);
            const auto before = ownSwitches(); const auto begin = seconds();
            reader.read();
            const auto end = seconds(); switches += ownSwitches() - before; time += end - begin;
        }
        std::cout << "activity " << reader.name << ": " << double(switches) / samples << " switches, " << time * 1000 / samples << " ms per call\n";
    }
    // The hand-off: one job on the shared utility worker and its completion
    // notice back to this thread (what ActivityProbe does per sample).
    const HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    app::UtilityExecutor executor([done] { SetEvent(done); }, 32, std::chrono::milliseconds::zero(), "probe utility");
    std::atomic<std::uint32_t> worker{};
    const auto route = executor.makeRoute();
    const auto job = [&] { (void)executor.submit(route, [&] { worker = GetCurrentThreadId(); }, [](std::exception_ptr) {}); WaitForSingleObject(done, INFINITE); executor.drain(); };
    job();
    const auto before = app::captureThreadActivity();
    for (unsigned n = 0; n < samples; ++n) { Sleep(200); job(); }
    const auto after = app::captureThreadActivity();
    std::uint64_t mine{}, theirs{};
    for (const auto& t : after) for (const auto& o : before) if (o.id == t.id) {
        if (t.id == GetCurrentThreadId()) mine += t.contextSwitches - o.contextSwitches; else if (t.id == worker) theirs += t.contextSwitches - o.contextSwitches; }
    std::cout << "activity hand-off: " << double(mine) / samples << " switches on the host thread (with its sleep), " << double(theirs) / samples << " on the worker per job\n";
    CloseHandle(done);
}
// What creating the HUD's device costs by itself, per creation flag set.
void deviceCreation() {
    struct Flags { const char* name; UINT flags; };
    const Flags sets[]{{"BGRA + VIDEO + PREVENT_THREADING (the HUD)", D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS},
        {"BGRA + PREVENT_THREADING", D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS},
        {"BGRA", D3D11_CREATE_DEVICE_BGRA_SUPPORT}};
    for (unsigned round = 0; round < 3; ++round) for (const auto& set : sets) {
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
        Microsoft::WRL::ComPtr<ID3D11Device> device; Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        const auto begin = seconds();
        const auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, set.flags, levels, 1, D3D11_SDK_VERSION, &device, nullptr, &context);
        const auto created = seconds();
        context.Reset(); device.Reset();
        std::cout << "device creation " << set.name << ": " << (created - begin) * 1000 << " ms" << (FAILED(result) ? " (failed)" : "") << "\n";
        Sleep(200);
    }
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        check(argc >= 2, "Usage: application_idle_probe <native/hud.hlsl> [--seconds N] [--warp] [--composition]");
        double seconds = 10; bool warp = false, composition = false;
        for (int i = 2; i < argc; ++i) {
            const std::wstring_view arg = argv[i];
            if (arg == L"--seconds" && i + 1 < argc) { seconds = std::wcstod(argv[++i], nullptr); check(seconds >= 1 && seconds <= 300, "1...300 seconds"); }
            else if (arg == L"--warp") warp = true;
            else if (arg == L"--composition") composition = true;
            else if (arg == L"--device-creation") { deviceCreation(); return 0; }
            else if (arg == L"--activity" && i + 1 < argc) { const auto samples = std::wcstoul(argv[++i], nullptr, 10); check(samples >= 1 && samples <= 120, "1...120 samples"); activity(samples); return 0; }
            else check(false, "Unknown option");
        }
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) >= 0, "COM");
        Window window;
        const auto options = [&](bool prevent, bool video) {
            gpu::RendererOptions o{warp ? gpu::Driver::warpForTests : gpu::Driver::hardware, argv[1],
                composition ? gpu::RenderTarget::composition : gpu::RenderTarget::offscreenForTests, video};
            o.preventDriverThreads = prevent; return o;
        };
        idle("process (no graphics device)", seconds);
        // What creating the device again would cost an opening: the device
        // with its programs and targets, and 64 MiB of retained textures.
        const auto clock = [] { LARGE_INTEGER f{}, v{}; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&v); return double(v.QuadPart) / double(f.QuadPart); };
        const auto privateMiB = [] { const auto m = app::captureProcessMemory(); return double(m.privateBytes) / 1048576.; };
        for (unsigned round = 0; round < 3; ++round) {
            const double before = privateMiB(), begin = clock();
            gpu::Renderer renderer;
            renderer.initialize(window.handle, 2560, 1600, options(true, true));
            const double created = clock();
            frame(renderer, false);
            const double drawn = clock();
            std::vector<std::uint8_t> texels(512 * 512 * 4);
            for (std::size_t n = 0; n < texels.size(); ++n) texels[n] = static_cast<std::uint8_t>(n * 31 + round);
            const double uploadBegin = clock();
            for (unsigned n = 0; n < 64; ++n) renderer.setTexture("bulk/" + std::to_string(n), 1, {512, 512, texels});
            renderer.draw(false);
            const double uploaded = clock();
            const double withDevice = privateMiB();
            renderer.reset();
            const double after = privateMiB();
            std::cout << "device round " << round << ": create " << (created - begin) * 1000 << " ms, first frame " << (drawn - created) * 1000
                      << " ms, 64 MiB of textures " << (uploaded - uploadBegin) * 1000 << " ms; private " << before << " -> " << withDevice << " -> " << after << " MiB\n";
        }
        {
            gpu::Renderer renderer;
            renderer.initialize(window.handle, 256, 256, options(true, true));
            std::cout << "device: " << renderer.deviceInfo().name << (composition ? ", composition target" : ", offscreen") << '\n';
            frame(renderer, false);
            idle("device (no internal threading) after a frame", seconds);
            if (composition) {
                frame(renderer, true);
                idle("presented (hidden window)", seconds);
            }
            (void)renderer.releaseFrameTargets();
            idle("released (frame targets, driver trimmed)", seconds);
            // How the driver thread waits: a higher process timer resolution
            // (timeBeginPeriod) and Windows power throttling of the process.
            timeBeginPeriod(1);
            idle("released, process timer resolution 1 ms", seconds);
            timeEndPeriod(1);
            {
                PROCESS_POWER_THROTTLING_STATE state{};
                state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | 0x4 /* IGNORE_TIMER_RESOLUTION */;
                state.StateMask = state.ControlMask;
                const bool throttled = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state)) != FALSE;
                idle(throttled ? "released, process EcoQoS + ignored timer resolution" : "released (power throttling refused)", seconds);
                state.StateMask = 0;
                (void)SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
            }
            if (composition) {
                check(renderer.releasePresentation(), "The presentation releases");
                idle("swapless (swap chain and composition target released)", seconds);
                frame(renderer, true);
                idle("re-presented (swap chain created again, one frame)", seconds);
                (void)renderer.releaseFrameTargets(); (void)renderer.releasePresentation();
                idle("swapless again", seconds);
            }
            renderer.reset();
            idle("reset (device released)", seconds);
        }
        {
            gpu::Renderer renderer;
            renderer.initialize(window.handle, 256, 256, options(true, false));
            frame(renderer, false);
            idle("no video (no internal threading, no VIDEO_SUPPORT) after a frame", seconds);
            renderer.reset();
        }
        {
            gpu::Renderer renderer;
            renderer.initialize(window.handle, 256, 256, options(false, false));
            frame(renderer, false);
            idle("threaded device (default flags) after a frame", seconds);
            renderer.reset();
            idle("threaded device released", seconds);
        }
        CoUninitialize();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Idle probe failed: " << e.what() << '\n';
        return 1;
    }
}
