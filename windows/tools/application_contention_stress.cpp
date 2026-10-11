// Synthetic "video game" load for the HUD contention trace
// (watch_session_preview --contention-trace): busy CPU threads and a GPU that
// renders long offscreen frames on the default adapter, back to back. It
// opens no window, reads no user data and stops by itself after the given
// time (or when its stop file appears). Test tooling only; never shipped.
//
//   application_contention_stress --seconds 60 [--cpu-threads N] [--cpu-priority normal|above|highest]
//       [--gpu-milliseconds 25] [--gpu-draws 1] [--no-gpu] [--stop-file path] [--report out.json]
// --gpu-draws splits each frame into that many full-screen passes of equal
// share (a game issues many draws; the GPU preempts at draw boundaries).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
void need(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void checked(HRESULT result, const char* message) { if (FAILED(result)) throw std::runtime_error(std::string(message) + " (0x" + std::to_string(static_cast<unsigned long>(result)) + ")"); }
using Clock = std::chrono::steady_clock;
double seconds(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); }

// A full-screen triangle whose pixels run a bounded arithmetic loop (no
// texture reads): pure shader-core load like a game's heavy passes.
constexpr char shader[] = R"(
cbuffer Load : register(b0) { uint iterations; float3 pad; };
float4 VS(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); return float4(p * float2(2, -2) + float2(-1, 1), 0, 1); }
float4 PS(float4 position : SV_Position) : SV_Target {
    float2 v = position.xy * 0.001;
    [loop] for (uint i = 0; i < iterations; ++i) v = float2(sin(v.x * 1.7 + v.y), cos(v.y * 1.3 - v.x)) * 0.999 + 0.0005;
    return float4(v, 0.5, 1);
}
)";

struct Gpu {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11RenderTargetView> target; ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps; ComPtr<ID3D11Buffer> constants;
    std::vector<ComPtr<ID3D11Query>> queries;
    unsigned iterations{64}, draws{1};
    Gpu() {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        checked(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context), "Create stress device");
        D3D11_TEXTURE2D_DESC texture{}; texture.Width = 2560; texture.Height = 1440; texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
        texture.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; texture.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> surface; checked(device->CreateTexture2D(&texture, nullptr, &surface), "Create stress target");
        checked(device->CreateRenderTargetView(surface.Get(), nullptr, &target), "Bind stress target");
        ComPtr<ID3DBlob> code, errors;
        checked(D3DCompile(shader, sizeof(shader) - 1, "stress", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &code, &errors), "Compile stress VS");
        checked(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs), "Create stress VS");
        checked(D3DCompile(shader, sizeof(shader) - 1, "stress", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &code, &errors), "Compile stress PS");
        checked(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps), "Create stress PS");
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = 16; buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        checked(device->CreateBuffer(&buffer, nullptr, &constants), "Create stress constants");
        for (unsigned n = 0; n < 2; ++n) { D3D11_QUERY_DESC q{D3D11_QUERY_EVENT, 0}; ComPtr<ID3D11Query> query; checked(device->CreateQuery(&q, &query), "Create stress query"); queries.push_back(query); }
    }
    void wait(ID3D11Query* query) { BOOL done = FALSE; while (context->GetData(query, &done, sizeof(done), 0) == S_FALSE) SwitchToThread(); }
    // One "game frame": a full-screen pass with the current iteration count.
    void frame(unsigned slot) {
        const unsigned values[4]{std::max(1u, iterations / draws), 0, 0, 0};
        context->UpdateSubresource(constants.Get(), 0, nullptr, values, 0, 0);
        D3D11_VIEWPORT viewport{0, 0, 2560, 1440, 0, 1};
        context->RSSetViewports(1, &viewport); context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); context->IASetInputLayout(nullptr);
        context->VSSetShader(vs.Get(), nullptr, 0); context->PSSetShader(ps.Get(), nullptr, 0); context->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
        for (unsigned n = 0; n < draws; ++n) context->Draw(3, 0);
        context->End(queries[slot].Get()); context->Flush();
    }
};
}

int wmain(int argc, wchar_t** argv) {
    try {
        double duration = 60, gpuTarget = 25; unsigned threads = std::thread::hardware_concurrency(), draws = 1; int priority = THREAD_PRIORITY_NORMAL; bool gpu = true;
        std::filesystem::path stop, report;
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg = argv[i];
            if (arg == L"--seconds" && i + 1 < argc) duration = std::stod(argv[++i]);
            else if (arg == L"--cpu-threads" && i + 1 < argc) threads = static_cast<unsigned>(std::stoul(argv[++i]));
            else if (arg == L"--cpu-priority" && i + 1 < argc) { const std::wstring p = argv[++i]; priority = p == L"above" ? THREAD_PRIORITY_ABOVE_NORMAL : p == L"highest" ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL; }
            else if (arg == L"--gpu-milliseconds" && i + 1 < argc) gpuTarget = std::stod(argv[++i]);
            else if (arg == L"--gpu-draws" && i + 1 < argc) draws = static_cast<unsigned>(std::stoul(argv[++i]));
            else if (arg == L"--no-gpu") gpu = false;
            else if (arg == L"--stop-file" && i + 1 < argc) stop = argv[++i];
            else if (arg == L"--report" && i + 1 < argc) report = argv[++i];
            else throw std::invalid_argument("Unknown argument");
        }
        need(duration > 0 && duration <= 600 && threads <= 256 && gpuTarget > 0 && gpuTarget <= 200 && draws >= 1 && draws <= 4096, "Bounded stress arguments");
        const auto start = Clock::now(), end = start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(duration));
        std::atomic<bool> running{true};
        std::atomic<std::uint64_t> work{};
        std::vector<std::thread> spinners;
        for (unsigned n = 0; n < threads; ++n) spinners.emplace_back([&, n] {
            SetThreadPriority(GetCurrentThread(), priority);
            double x = 1.0 + n; std::uint64_t local{};
            while (running.load(std::memory_order_relaxed)) { for (unsigned k = 0; k < 4096; ++k) x = std::sqrt(x * 1.000001 + 0.5); ++local; }
            work.fetch_add(local + static_cast<std::uint64_t>(x > 1e300), std::memory_order_relaxed);
        });
        std::uint64_t frames{}; double frameSeconds{};
        const auto stopRequested = [&] { return Clock::now() >= end || (!stop.empty() && std::filesystem::exists(stop)); };
        if (gpu) {
            Gpu device; device.draws = draws;
            // Calibrate one frame to about the target time, then keep two in flight.
            for (unsigned attempt = 0; attempt < 12; ++attempt) {
                const auto a = Clock::now(); device.frame(0); device.wait(device.queries[0].Get()); const double ms = seconds(a, Clock::now()) * 1000;
                if (ms > gpuTarget * .8 && ms < gpuTarget * 1.25) break;
                device.iterations = static_cast<unsigned>(std::clamp(device.iterations * gpuTarget / std::max(.1, ms), 16., double(1 << 20)));
            }
            unsigned slot = 0; device.frame(0); device.frame(1);
            while (!stopRequested()) {
                const auto a = Clock::now(); device.wait(device.queries[slot].Get()); device.frame(slot); slot ^= 1u; frameSeconds += seconds(a, Clock::now()); ++frames;
            }
            device.wait(device.queries[0].Get()); device.wait(device.queries[1].Get());
        } else while (!stopRequested()) Sleep(50);
        running = false;
        for (auto& t : spinners) t.join();
        const double elapsed = seconds(start, Clock::now());
        if (!report.empty()) {
            std::ofstream out(report, std::ios::binary);
            out << "{\"seconds\":" << elapsed << ",\"cpuThreads\":" << threads << ",\"cpuPriority\":" << priority << ",\"gpu\":" << (gpu ? "true" : "false")
                << ",\"gpuDrawsPerFrame\":" << draws << ",\"gpuFrames\":" << frames << ",\"gpuFrameMilliseconds\":" << (frames ? frameSeconds * 1000 / frames : 0) << ",\"cpuWorkUnits\":" << work.load() << "}\n";
        }
        std::printf("stress finished after %.1f s: %u CPU threads, %llu GPU frames\n", elapsed, threads, static_cast<unsigned long long>(frames));
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "stress failed: %s\n", e.what());
        return 1;
    }
}
