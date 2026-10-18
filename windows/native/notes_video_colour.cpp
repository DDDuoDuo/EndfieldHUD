#include "native/notes_video_colour.hpp"
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <mfapi.h>
#include <wrl/client.h>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#endif

namespace endfield::native {
const std::array<std::uint8_t, 256>& notesCoreMedia709Codes() noexcept {
    static const std::array<std::uint8_t, 256> codes = [] {
        std::array<std::uint8_t, 256> out{};
        for (unsigned k = 0; k < 256; ++k) {
            const double v = k / 255.;
            const double linear = std::max(std::pow(v, 1.961), v / 16.);
            const double encoded = linear <= .0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1 / 2.4) - .055;
            out[k] = static_cast<std::uint8_t>(std::clamp(std::lround(encoded * 255.), 0L, 255L));
        }
        return out;
    }();
    return codes;
}
bool notesCoreMedia709(std::uint32_t transfer, std::uint32_t primaries) noexcept {
#ifdef _WIN32
    return (transfer == MFVideoTransFunc_Unknown || transfer == MFVideoTransFunc_709 || transfer == MFVideoTransFunc_709_sym) &&
           (primaries == MFVideoPrimaries_Unknown || primaries == MFVideoPrimaries_BT709);
#else
    // MFVideoTransFunc_Unknown 0, _709 5, _709_sym 18; MFVideoPrimaries_Unknown 0, _BT709 2.
    return (transfer == 0 || transfer == 5 || transfer == 18) && (primaries == 0 || primaries == 2);
#endif
}
void notesApplyCoreMedia709(std::uint8_t* rgba, std::size_t pixels) noexcept {
    const auto& codes = notesCoreMedia709Codes();
    for (std::size_t n = 0; n < pixels; ++n, rgba += 4) {
        rgba[0] = codes[rgba[0]];
        rgba[1] = codes[rgba[1]];
        rgba[2] = codes[rgba[2]];
    }
}

#ifdef _WIN32
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(std::string(what) + " (HRESULT " + std::to_string(static_cast<long>(hr)) + ")");
}
// Full-target triangle; each pixel reads its own decoded texel (no filtering)
// and writes the CoreMedia 709 reading encoded as sRGB, alpha unchanged.
constexpr char shaderSource[] = R"hlsl(
Texture2D<float4> Source : register(t0);
float4 FullScreen(uint id : SV_VertexID) : SV_Position {
    const float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float3 Encode(float3 c) {
    const float3 low = c * 12.92;
    const float3 high = 1.055 * pow(max(c, 1e-7), 1.0 / 2.4) - 0.055;
    return float3(c.r <= 0.0031308 ? low.r : high.r, c.g <= 0.0031308 ? low.g : high.g, c.b <= 0.0031308 ? low.b : high.b);
}
float4 Reading(float4 position : SV_Position) : SV_Target {
    const float4 v = Source.Load(int3(position.xy, 0));
    const float3 linearColour = max(pow(max(v.rgb, 0), 1.961), v.rgb / 16.0);
    return float4(saturate(Encode(linearColour)), v.a);
}
)hlsl";
struct Bytecode { std::vector<std::uint8_t> vertex, pixel; };
std::vector<std::uint8_t> compile(const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(shaderSource, sizeof shaderSource - 1, "notes_video_colour.hlsl", nullptr, nullptr, entry, target,
        D3DCOMPILE_IEEE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        const std::string text = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : std::string{};
        throw std::runtime_error("Notes video colour shader compile failed: " + text);
    }
    const auto* bytes = static_cast<const std::uint8_t*>(code->GetBufferPointer());
    return {bytes, bytes + code->GetBufferSize()};
}
const Bytecode& bytecode() {
    static std::once_flag once;
    static Bytecode value;
    static std::exception_ptr failure;
    std::call_once(once, [] {
        try { value.vertex = compile("FullScreen", "vs_4_0"); value.pixel = compile("Reading", "ps_4_0"); }
        catch (...) { failure = std::current_exception(); }
    });
    if (failure) std::rethrow_exception(failure);
    return value;
}
} // namespace

struct NotesCoreMedia709Pass::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Texture2D> input, output;
    ComPtr<ID3D11ShaderResourceView> inputView;
    ComPtr<ID3D11RenderTargetView> outputView;
    unsigned width{}, height{};
    std::uint64_t passes{};
    void reset() noexcept {
        inputView.Reset(); outputView.Reset(); input.Reset(); output.Reset(); width = height = 0;
    }
    void bind(ID3D11Device* next) {
        if (device.Get() == next) return;
        reset(); vertex.Reset(); pixel.Reset(); context.Reset(); device = next;
        device->GetImmediateContext(&context);
        const auto& code = bytecode();
        checked(device->CreateVertexShader(code.vertex.data(), code.vertex.size(), nullptr, &vertex), "Create Notes video colour vertex shader");
        checked(device->CreatePixelShader(code.pixel.data(), code.pixel.size(), nullptr, &pixel), "Create Notes video colour pixel shader");
    }
    void size(unsigned w, unsigned h) {
        if (input && width == w && height == h) return;
        reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        checked(device->CreateTexture2D(&desc, nullptr, &input), "Create Notes video colour input");
        checked(device->CreateShaderResourceView(input.Get(), nullptr, &inputView), "View Notes video colour input");
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        checked(device->CreateTexture2D(&desc, nullptr, &output), "Create Notes video colour output");
        checked(device->CreateRenderTargetView(output.Get(), nullptr, &outputView), "View Notes video colour output");
        width = w; height = h;
    }
};
NotesCoreMedia709Pass::NotesCoreMedia709Pass() : impl_(std::make_unique<Impl>()) {}
NotesCoreMedia709Pass::~NotesCoreMedia709Pass() = default;
void NotesCoreMedia709Pass::apply(void* surface) {
    auto& i = *impl_;
    if (!surface) throw std::invalid_argument("Notes video colour pass requires a frame surface");
    ComPtr<ID3D11Texture2D> target;
    checked(static_cast<IUnknown*>(surface)->QueryInterface(IID_PPV_ARGS(&target)), "Read the frame server target");
    D3D11_TEXTURE2D_DESC desc{};
    target->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.SampleDesc.Count != 1 || desc.MipLevels != 1 || desc.ArraySize != 1 || !desc.Width || !desc.Height)
        throw std::invalid_argument("Notes video colour pass requires a single BGRA8 frame target");
    ComPtr<ID3D11Device> device;
    target->GetDevice(&device);
    i.bind(device.Get());
    i.size(desc.Width, desc.Height);
    auto* context = i.context.Get();
    context->CopyResource(i.input.Get(), target.Get());
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1};
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(i.vertex.Get(), nullptr, 0);
    context->PSSetShader(i.pixel.Get(), nullptr, 0);
    auto* view = i.inputView.Get();
    context->PSSetShaderResources(0, 1, &view);
    context->RSSetState(nullptr);
    context->RSSetViewports(1, &viewport);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
    context->OMSetDepthStencilState(nullptr, 0);
    auto* rtv = i.outputView.Get();
    context->OMSetRenderTargets(1, &rtv, nullptr);
    context->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    context->PSSetShaderResources(0, 1, &none);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    context->CopyResource(target.Get(), i.output.Get());
    ++i.passes;
}
void NotesCoreMedia709Pass::release() noexcept { impl_->reset(); }
std::size_t NotesCoreMedia709Pass::scratchBytes() const noexcept { return impl_->input ? std::size_t(impl_->width) * impl_->height * 8 : 0; }
std::uint64_t NotesCoreMedia709Pass::passes() const noexcept { return impl_->passes; }
#endif
} // namespace endfield::native
