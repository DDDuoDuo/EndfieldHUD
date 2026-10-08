#include "renderer.hpp"
#include "source_graphics.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <limits>
#include <map>
#include <utility>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT result, const char *operation) {
    if (FAILED(result)) throw RendererError(operation, static_cast<std::int32_t>(result));
}
void require(bool condition, const char *reason) {
    if (!condition) throw std::invalid_argument(reason);
}
void dimensions(std::uint32_t width, std::uint32_t height) {
    require(width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
            std::uint64_t(width) * height <= Renderer::maximumRenderPixels, "Render dimensions exceed bounds");
}
void identity(const std::string &id) { require(!id.empty() && id.size() <= 512, "Invalid retained source ID"); }
const std::array<double, 256> &linearSRGBBytes() {
    static const auto values = [] {
        std::array<double, 256> result{};
        for (std::size_t i = 0; i < result.size(); ++i) {
            const double encoded = static_cast<double>(i) / 255;
            result[i] = encoded <= .04045 ? encoded / 12.92 : std::pow((encoded + .055) / 1.055, 2.4);
        }
        return result;
    }();
    return values;
}
std::array<float, 16> matrix(const core::Matrix4 &value) {
    std::array<float, 16> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        require(std::isfinite(value.values[i]) && std::abs(value.values[i]) <= std::numeric_limits<float>::max(),
                "Invalid GPU transform");
        result[i] = static_cast<float>(value.values[i]);
    }
    return result;
}
struct alignas(16) MaskUniform {
    std::array<float, 16> worldToLocal;
    std::array<float, 4> bounds;
    std::array<float, 4> corners{};
};
struct alignas(16) ObjectUniform {
    std::array<float, 16> world;
    std::array<float, 4> tint;
    float opacity;
    std::uint32_t maskCount;
    std::uint32_t shutterEnabled{}, shutterStrips{};
    std::array<MaskUniform, 8> masks{};
    std::array<float, 16> shutterWorldToLocal{};
    std::array<std::array<float, 4>, 30> shutterEdges{};
};
static_assert(sizeof(Vertex) == 36 && sizeof(MaskUniform) == 96 && sizeof(ObjectUniform) == 1408);
void shutterUniforms(ObjectUniform& result, const PlaneShutter& shutter) {
    result.shutterEnabled = 1;
    result.shutterWorldToLocal = matrix(shutter.worldToLocal);
    for (std::size_t strip = 0; strip < shutter.strips.size(); ++strip) {
        const auto& polygon = shutter.strips[strip];
        double scale = 1;
        for (const auto vertex : polygon) {
            require(std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                std::abs(vertex.x) <= std::numeric_limits<float>::max() &&
                std::abs(vertex.y) <= std::numeric_limits<float>::max(), "Invalid shutter vertex");
            scale = std::max({scale, std::abs(vertex.x), std::abs(vertex.y)});
        }
        // Translate the area calculation to the first vertex to avoid losing
        // precision when a small source strip has a nonzero local origin.
        double area = 0;
        bool collinear = true;
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
            const auto a = polygon[i], b = polygon[i + 1], origin = polygon[0];
            const auto cross = (a.x-origin.x)*(b.y-origin.y)-(a.y-origin.y)*(b.x-origin.x);
            area += cross; collinear = collinear && cross == 0;
        }
        if (area == 0) {
            require(collinear, "Self-intersecting zero-area shutter polygon");
            continue; // Exact source collapsed endpoint; never a full plane.
        }
        const auto winding = area > 0 ? 1. : -1.;
        const auto tolerance = scale * 8 * std::numeric_limits<float>::epsilon();
        for (std::size_t edge = 0; edge < polygon.size(); ++edge) {
            const auto a = polygon[edge], b = polygon[(edge + 1) % polygon.size()];
            const auto dx = b.x-a.x, dy = b.y-a.y, length = std::hypot(dx, dy);
            if (length == 0) continue; // Repeated full-open bevel vertex.
            const auto nx = -dy / length * winding, ny = dx / length * winding;
            const auto constant = -(nx*a.x+ny*a.y);
            require(std::isfinite(constant) && std::abs(constant) <= std::numeric_limits<float>::max(),
                "Shutter edge exceeds GPU range");
            // Permit harmless roundoff at collinear source path-morph edges,
            // but reject arbitrary concave/crossing paths before any upload.
            for (const auto vertex : polygon)
                require(nx*(vertex.x-a.x)+ny*(vertex.y-a.y) >= -tolerance, "Shutter polygon is not convex");
            result.shutterEdges[strip*polygon.size()+edge] = {
                static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(constant), 0};
        }
        result.shutterStrips |= std::uint32_t{1} << strip;
    }
}
ObjectUniform uniforms(const DrawObject &object) {
    identity(object.sourceID);
    require(object.masks.size() <= 8, "Too many plane masks");
    ObjectUniform result{};
    result.world = matrix(object.world);
    result.tint = object.linearTint;
    for (float color : result.tint)
        require(std::isfinite(color) && color >= 0 && color <= 1, "Plain material tint must be normalized linear RGBA");
    require(std::isfinite(object.opacity) && object.opacity >= 0 && object.opacity <= 1, "Invalid object opacity");
    result.opacity = object.opacity;
    result.maskCount = static_cast<std::uint32_t>(object.masks.size());
    for (std::size_t i = 0; i < object.masks.size(); ++i) {
        const auto &mask = object.masks[i];
        result.masks[i].worldToLocal = matrix(mask.worldToLocal);
        const auto &r = mask.bounds;
        require(std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
                r.width > 0 && r.height > 0, "Invalid plane-mask rectangle");
        require(std::isfinite(mask.cornerRadius) && mask.cornerRadius >= 0 &&
                mask.cornerRadius <= std::min(r.width, r.height) * .5, "Invalid plane-mask corner radius");
        result.masks[i].corners[0] = static_cast<float>(mask.cornerRadius);
        const std::array<double, 4> bounds{r.x, r.y, r.x + r.width, r.y + r.height};
        for (std::size_t j = 0; j < bounds.size(); ++j) {
            require(std::isfinite(bounds[j]) && std::abs(bounds[j]) <= std::numeric_limits<float>::max(), "Plane-mask extent exceeds GPU range");
            result.masks[i].bounds[j] = static_cast<float>(bounds[j]);
        }
    }
    if (object.shutter) shutterUniforms(result, *object.shutter);
    return result;
}
ComPtr<ID3DBlob> compile(const std::filesystem::path &path, const char *entry, const char *target) {
    ComPtr<ID3DBlob> program, errors;
    const HRESULT status = D3DCompileFromFile(path.c_str(), nullptr, nullptr, entry, target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &program, &errors);
    if (FAILED(status)) {
        std::string message = std::string("Compile ") + entry;
        if (errors) message += ": " + std::string(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
        throw RendererError(std::move(message), static_cast<std::int32_t>(status));
    }
    return program;
}
ComPtr<ID3D11Buffer> immutableBuffer(ID3D11Device *device, UINT binding, const void *data, std::size_t bytes) {
    require(bytes > 0 && bytes <= std::numeric_limits<UINT>::max(), "GPU buffer size exceeds bounds");
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(bytes);
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = binding;
    D3D11_SUBRESOURCE_DATA source{};
    source.pSysMem = data;
    ComPtr<ID3D11Buffer> buffer;
    checked(device->CreateBuffer(&description, &source, &buffer), "Create retained buffer");
    return buffer;
}
} // namespace

void validatePlaneShutter(const PlaneShutter& shutter) {
    ObjectUniform candidate{};
    shutterUniforms(candidate, shutter);
}
void validateDrawObject(const DrawObject& object) {(void)uniforms(object);}

RendererError::RendererError(std::string operation, std::int32_t code)
    : std::runtime_error(std::move(operation) + " (HRESULT " + std::to_string(code) + ")"), code_(code) {}

struct Renderer::Impl {
    struct Mesh {
        ComPtr<ID3D11Buffer> vertices, indices;
        UINT indexCount{};
        std::uint64_t revision{};
        std::size_t bytes{};
    };
    struct Texture {
        ComPtr<ID3D11ShaderResourceView> view;
        TextureFilter filter{};
        std::uint64_t revision{};
        std::size_t bytes{};
    };
    struct Draw {
        std::string sourceID;
        Mesh *mesh{};
        Texture *texture{};
        ObjectUniform values{};
        ComPtr<ID3D11Buffer> constants;
    };
    DWORD ownerThread{GetCurrentThreadId()};
    HWND window{};
    bool offscreen{};
    std::uint32_t width{}, height{};
    RendererStats counters;
    bool cameraDirty{true}, readbackReady{};
    std::array<float, 16> cameraValues = matrix(core::Matrix4{});
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapchain;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;
    ComPtr<ID3D11Texture2D> backBuffer, linearTarget;
    ComPtr<ID3D11RenderTargetView> outputView, linearView;
    ComPtr<ID3D11ShaderResourceView> linearResource;
    ComPtr<ID3D11VertexShader> sceneVS, compositeVS;
    ComPtr<ID3D11PixelShader> scenePS, compositePS;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> cameraBuffer;
    ComPtr<ID3D11BlendState> overBlend, replaceBlend;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> noDepth;
    ComPtr<ID3D11SamplerState> nearestSampler, linearSampler;
    std::map<std::string, Mesh> meshes;
    std::map<std::string, Texture> textures;
    Texture white;
    std::vector<Draw> draws;
    std::vector<ObjectUniform> stagedObjectValues;
    std::unique_ptr<SourceGraphics> source;

    ~Impl() {
        if (target) target->SetRoot(nullptr);
        if (visual) visual->SetContent(nullptr);
        if (composition) composition->Commit();
        if (context) { context->ClearState(); context->Flush(); }
    }
    void thread() const { require(GetCurrentThreadId() == ownerThread, "Renderer calls must stay on its creating UI thread"); }
    void budget(std::size_t oldBytes, std::size_t newBytes) const {
        require(newBytes <= Renderer::maximumResourceBytes &&
                counters.resourceBytes - oldBytes <= Renderer::maximumResourceBytes - newBytes,
                "Retained GPU resource budget exceeded");
    }
    Texture texture(TextureData input, std::uint64_t revision) {
        // Filtering must interpolate premultiplied linear values. Sampling
        // straight RGB/alpha and multiplying afterwards creates dark borders
        // against transparent texels. Conversion happens only on replacement.
        std::vector<std::uint16_t> premultiplied(input.straightRGBA.size());
        for (std::size_t pixel = 0; pixel < input.straightRGBA.size(); pixel += 4) {
            const double alpha = input.straightRGBA[pixel + 3] / 255.0;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto encoded = input.straightRGBA[pixel + channel];
                const double value = input.colorSpace == TextureColorSpace::sRGB
                    ? linearSRGBBytes()[encoded] : encoded / 255.0;
                premultiplied[pixel + channel] = static_cast<std::uint16_t>(std::lround(value * alpha * 65535));
            }
            premultiplied[pixel + 3] = static_cast<std::uint16_t>(std::lround(alpha * 65535));
        }
        D3D11_TEXTURE2D_DESC description{};
        description.Width = input.width; description.Height = input.height;
        description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
        description.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA pixels{};
        pixels.pSysMem = premultiplied.data(); pixels.SysMemPitch = input.width * 8;
        ComPtr<ID3D11Texture2D> resource;
        checked(device->CreateTexture2D(&description, &pixels, &resource), "Upload retained texture");
        Texture result;
        checked(device->CreateShaderResourceView(resource.Get(), nullptr, &result.view), "Create retained texture view");
        result.filter = input.filter; result.revision = revision; result.bytes = premultiplied.size() * sizeof(std::uint16_t);
        return result;
    }
    void bindTargets() {
        if (offscreen) {
            D3D11_TEXTURE2D_DESC output{};
            output.Width = width; output.Height = height;
            output.MipLevels = output.ArraySize = output.SampleDesc.Count = 1;
            output.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            output.BindFlags = D3D11_BIND_RENDER_TARGET;
            checked(device->CreateTexture2D(&output, nullptr, &backBuffer), "Create isolated output texture");
        } else checked(swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "Get composition back buffer");
        checked(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &outputView), "Bind encoded-premultiplied output");
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width; description.Height = height;
        description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
        description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        checked(device->CreateTexture2D(&description, nullptr, &linearTarget), "Create linear composition target");
        checked(device->CreateRenderTargetView(linearTarget.Get(), nullptr, &linearView), "Bind linear composition target");
        checked(device->CreateShaderResourceView(linearTarget.Get(), nullptr, &linearResource), "Bind linear composition input");
    }
    void initialize(HWND targetWindow, std::uint32_t w, std::uint32_t h, const RendererOptions &options) {
        window = targetWindow;
        dimensions(w, h);
        require(IsWindow(window) != FALSE && !options.shaderPath.empty(), "A native HUD window and shader path are required");
        require(options.driver == Driver::hardware || options.driver == Driver::warpForTests, "Unknown graphics driver selection");
        require(options.target == RenderTarget::composition || options.target == RenderTarget::offscreenForTests, "Unknown render target selection");
        offscreen = options.target == RenderTarget::offscreenForTests;
        DWORD process{};
        GetWindowThreadProcessId(window, &process);
        require(process == GetCurrentProcessId(), "Renderer target must belong to this application");
        require((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) != 0,
                "Composition window must use WS_EX_NOREDIRECTIONBITMAP");
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
        checked(D3D11CreateDevice(nullptr, options.driver == Driver::hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP,
            nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1, D3D11_SDK_VERSION, &device, nullptr, &context), "Create D3D11 device");
        if (!offscreen) {
        ComPtr<IDXGIDevice1> dxgi;
        checked(device.As(&dxgi), "Get DXGI device");
        checked(dxgi->SetMaximumFrameLatency(1), "Bound presentation latency");
        ComPtr<IDXGIAdapter> adapter;
        checked(dxgi->GetAdapter(&adapter), "Get D3D adapter");
        ComPtr<IDXGIFactory2> factory;
        checked(adapter->GetParent(IID_PPV_ARGS(&factory)), "Get composition factory");
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = w; description.Height = h;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        checked(factory->CreateSwapChainForComposition(device.Get(), &description, nullptr, &swapchain), "Create premultiplied composition swap chain");
        checked(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&composition)), "Create DirectComposition device");
        checked(composition->CreateTargetForHwnd(window, TRUE, &target), "Create owned HUD composition target");
        checked(composition->CreateVisual(&visual), "Create HUD composition visual");
        checked(visual->SetContent(swapchain.Get()), "Attach HUD swap chain");
        checked(target->SetRoot(visual.Get()), "Attach HUD composition root");
        checked(composition->Commit(), "Commit HUD composition root");
        }
        width = w; height = h; bindTargets();

        auto vs = compile(options.shaderPath, "SceneVS", "vs_5_0");
        auto ps = compile(options.shaderPath, "ScenePS", "ps_5_0");
        checked(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &sceneVS), "Create scene vertex shader");
        checked(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &scenePS), "Create scene pixel shader");
        const D3D11_INPUT_ELEMENT_DESC elements[]{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        checked(device->CreateInputLayout(elements, 3, vs->GetBufferPointer(), vs->GetBufferSize(), &layout), "Create retained mesh layout");
        vs = compile(options.shaderPath, "CompositeVS", "vs_5_0");
        ps = compile(options.shaderPath, "CompositePS", "ps_5_0");
        checked(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &compositeVS), "Create presentation vertex shader");
        checked(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &compositePS), "Create presentation pixel shader");
        D3D11_BUFFER_DESC camera{};
        camera.ByteWidth = 64; camera.Usage = D3D11_USAGE_DYNAMIC;
        camera.BindFlags = D3D11_BIND_CONSTANT_BUFFER; camera.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        checked(device->CreateBuffer(&camera, nullptr, &cameraBuffer), "Create retained camera uniform");
        D3D11_BLEND_DESC blend{};
        auto &over = blend.RenderTarget[0];
        over.BlendEnable = TRUE;
        over.SrcBlend = over.SrcBlendAlpha = D3D11_BLEND_ONE;
        over.DestBlend = over.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        over.BlendOp = over.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        over.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        checked(device->CreateBlendState(&blend, &overBlend), "Create linear premultiplied over blending");
        over.BlendEnable = FALSE;
        checked(device->CreateBlendState(&blend, &replaceBlend), "Create output replacement blending");
        D3D11_RASTERIZER_DESC rasterDescription{};
        rasterDescription.FillMode = D3D11_FILL_SOLID; rasterDescription.CullMode = D3D11_CULL_NONE;
        rasterDescription.DepthClipEnable = TRUE;
        checked(device->CreateRasterizerState(&rasterDescription, &raster), "Create planar raster state");
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE; depth.StencilEnable = FALSE;
        depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        depth.FrontFace.StencilFailOp = depth.FrontFace.StencilDepthFailOp = depth.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        depth.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        depth.BackFace = depth.FrontFace;
        checked(device->CreateDepthStencilState(&depth, &noDepth), "Create ordered UI depth state");
        D3D11_SAMPLER_DESC sampler{};
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxAnisotropy = 1; sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        checked(device->CreateSamplerState(&sampler, &nearestSampler), "Create point texture sampler");
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        checked(device->CreateSamplerState(&sampler, &linearSampler), "Create linear texture sampler");
        const std::array<std::uint8_t, 4> opaqueWhite{255, 255, 255, 255};
        white = texture({1, 1, opaqueWhite, TextureColorSpace::linear, TextureFilter::nearest}, 0);
        counters.initialized = true;
    }
};

Renderer::Renderer() = default;
Renderer::~Renderer() = default;
void Renderer::initialize(void *window, std::uint32_t width, std::uint32_t height, const RendererOptions &options) {
    if (impl_) {
        impl_->thread();
        require(impl_->window != static_cast<HWND>(window), "Reset is required before reinitializing the same composition window");
    }
    auto next = std::make_unique<Impl>();
    next->initialize(static_cast<HWND>(window), width, height, options);
    impl_ = std::move(next);
}
void Renderer::resize(std::uint32_t width, std::uint32_t height) {
    require(impl_ != nullptr, "Renderer is not initialized"); impl_->thread(); dimensions(width, height);
    if (impl_->width == width && impl_->height == height) return;
    try {
        auto &r = *impl_;
        r.readbackReady = false;
        r.context->ClearState();
        r.outputView.Reset(); r.backBuffer.Reset(); r.linearView.Reset(); r.linearResource.Reset(); r.linearTarget.Reset();
        if (!r.offscreen) checked(r.swapchain->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0), "Resize composition buffers");
        r.width = width; r.height = height; r.bindTargets();
    } catch (...) { reset(); throw; }
}
bool Renderer::setMesh(std::string sourceID, std::uint64_t revision, MeshData input) {
    require(impl_ != nullptr, "Renderer is not initialized"); auto &r = *impl_; r.thread(); identity(sourceID);
    const auto existing = r.meshes.find(sourceID);
    if (existing != r.meshes.end() && existing->second.revision == revision) return false;
    require(existing != r.meshes.end() || r.meshes.size() < maximumMeshes, "Retained mesh count exceeds bounds");
    require(!input.vertices.empty() && input.vertices.size() <= 1000000 && !input.indices.empty() &&
            input.indices.size() <= 3000000 && input.indices.size() % 3 == 0, "Invalid triangle mesh size");
    for (const auto &vertex : input.vertices) {
        for (float value : vertex.position) require(std::isfinite(value), "Nonfinite mesh position");
        for (float value : vertex.uv) require(std::isfinite(value), "Nonfinite mesh UV");
        for (float value : vertex.linearColor) require(std::isfinite(value) && value >= 0 && value <= 1, "Invalid linear vertex color");
    }
    for (auto index : input.indices) require(index < input.vertices.size(), "Triangle index exceeds vertex count");
    const auto bytes = input.vertices.size_bytes() + input.indices.size_bytes();
    const auto previousBytes = existing == r.meshes.end() ? 0 : existing->second.bytes;
    r.budget(previousBytes, bytes);
    Impl::Mesh mesh;
    mesh.vertices = immutableBuffer(r.device.Get(), D3D11_BIND_VERTEX_BUFFER, input.vertices.data(), input.vertices.size_bytes());
    mesh.indices = immutableBuffer(r.device.Get(), D3D11_BIND_INDEX_BUFFER, input.indices.data(), input.indices.size_bytes());
    mesh.indexCount = static_cast<UINT>(input.indices.size()); mesh.revision = revision; mesh.bytes = bytes;
    r.meshes.insert_or_assign(std::move(sourceID), std::move(mesh));
    r.counters.resourceBytes = r.counters.resourceBytes - previousBytes + bytes; ++r.counters.meshUploads;
    return true;
}
bool Renderer::setTexture(std::string sourceID, std::uint64_t revision, TextureData input) {
    require(impl_ != nullptr, "Renderer is not initialized"); auto &r = *impl_; r.thread(); identity(sourceID);
    const auto existing = r.textures.find(sourceID);
    if (existing != r.textures.end() && existing->second.revision == revision) return false;
    require(existing != r.textures.end() || r.textures.size() < maximumTextures, "Retained texture count exceeds bounds");
    require(input.width > 0 && input.height > 0 && input.width <= 8192 && input.height <= 8192 &&
            std::uint64_t(input.width) * input.height * 4 == input.straightRGBA.size(), "Invalid straight RGBA8 texture dimensions");
    require((input.colorSpace == TextureColorSpace::sRGB || input.colorSpace == TextureColorSpace::linear) &&
            (input.filter == TextureFilter::nearest || input.filter == TextureFilter::linear), "Invalid texture sampling policy");
    const auto previousBytes = existing == r.textures.end() ? 0 : existing->second.bytes;
    const auto nativeBytes = std::size_t(input.width) * input.height * 8;
    r.budget(previousBytes, nativeBytes);
    auto texture = r.texture(input, revision);
    r.textures.insert_or_assign(std::move(sourceID), std::move(texture));
    r.counters.resourceBytes = r.counters.resourceBytes - previousBytes + nativeBytes; ++r.counters.textureUploads;
    return true;
}
void Renderer::setDrawList(std::span<const DrawObject> objects) {
    require(impl_ != nullptr, "Renderer is not initialized"); auto &r = *impl_; r.thread();
    require(objects.size() <= maximumObjects, "Retained draw count exceeds bounds");
    bool retained = objects.size() == r.draws.size();
    for (std::size_t i = 0; retained && i < objects.size(); ++i) {
        const auto &object = objects[i]; const auto &draw = r.draws[i];
        const auto mesh = r.meshes.find(object.meshID);
        const auto texture = r.textures.find(object.textureID);
        retained = object.sourceID == draw.sourceID && mesh != r.meshes.end() && &mesh->second == draw.mesh &&
            (object.textureID.empty() ? draw.texture == &r.white : texture != r.textures.end() && &texture->second == draw.texture);
    }
    if (retained) {
        // Validate the complete pose before touching any active GPU constants.
        // Storage was reserved at the last structural commit. Pointer, fade and
        // clipping updates retain both the draw records and their identities.
        r.stagedObjectValues.resize(objects.size());
        for (std::size_t i = 0; i < objects.size(); ++i) r.stagedObjectValues[i] = uniforms(objects[i]);
        try {
            for (std::size_t i = 0; i < objects.size(); ++i) {
                auto &draw = r.draws[i]; const auto &value = r.stagedObjectValues[i];
                if (std::memcmp(&value, &draw.values, sizeof(ObjectUniform)) == 0) continue;
                D3D11_MAPPED_SUBRESOURCE mapped{};
                checked(r.context->Map(draw.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Update retained object uniform");
                std::memcpy(mapped.pData, &value, sizeof(ObjectUniform));
                r.context->Unmap(draw.constants.Get(), 0); draw.values = value; ++r.counters.objectUploads;
            }
        } catch (...) { reset(); throw; }
        return;
    }
    r.stagedObjectValues.reserve(objects.size());
    std::vector<Impl::Draw> next;
    next.reserve(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto &object = objects[i];
        auto mesh = r.meshes.find(object.meshID);
        require(mesh != r.meshes.end(), "Draw refers to an unregistered mesh");
        auto texture = r.textures.find(object.textureID);
        require(object.textureID.empty() || texture != r.textures.end(), "Draw refers to an unregistered texture");
        Impl::Draw draw;
        draw.sourceID = object.sourceID; draw.mesh = &mesh->second;
        draw.texture = object.textureID.empty() ? &r.white : &texture->second;
        draw.values = uniforms(object);
        if (i < r.draws.size())
            draw.constants = r.draws[i].constants;
        else {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = sizeof(ObjectUniform); description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER; description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = &draw.values;
            checked(r.device->CreateBuffer(&description, &initial, &draw.constants), "Create retained object uniform");
            ++r.counters.objectUploads;
            ++r.counters.objectBufferAllocations;
        }
        next.push_back(std::move(draw));
    }
    // Validate/build the entire list before changing any currently bound
    // constants. Stable slots reuse their buffer during opacity/transform
    // animation; only changed numeric values are uploaded here, never in draw.
    try {
        for (std::size_t i = 0; i < std::min(next.size(), r.draws.size()); ++i)
            if (std::memcmp(&next[i].values, &r.draws[i].values, sizeof(ObjectUniform)) != 0) {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                checked(r.context->Map(next[i].constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Update retained object uniform");
                std::memcpy(mapped.pData, &next[i].values, sizeof(ObjectUniform));
                r.context->Unmap(next[i].constants.Get(), 0);
                ++r.counters.objectUploads;
            }
    } catch (...) {
        // Earlier slots may already have new GPU bytes. Do not leave old CPU
        // comparisons attached to those partially updated buffers on retry.
        reset();
        throw;
    }
    r.draws = std::move(next);
}
void Renderer::setCamera(const core::Matrix4 &viewProjection) {
    require(impl_ != nullptr, "Renderer is not initialized"); auto &r = *impl_; r.thread();
    auto next = matrix(viewProjection);
    if (next != r.cameraValues) { r.cameraValues = next; r.cameraDirty = true; }
}
void Renderer::draw(bool present) {
    require(impl_ != nullptr, "Renderer is not initialized"); auto &r = *impl_; r.thread();
    require(!present || !r.offscreen, "An isolated render target cannot present to the desktop");
    r.readbackReady = false;
    const bool originalBackground = r.source && r.source->active();
    if (originalBackground) {
        r.source->renderTo(r.backBuffer.Get(), r.width, r.height);
        if (r.draws.empty()) {
            if (present) { checked(r.swapchain->Present(1, 0), "Present original HUD materials"); ++r.counters.presents; }
            else r.readbackReady = true;
            return;
        }
    }
    if (r.cameraDirty) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(r.context->Map(r.cameraBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Update camera uniform");
        std::memcpy(mapped.pData, r.cameraValues.data(), sizeof(r.cameraValues));
        r.context->Unmap(r.cameraBuffer.Get(), 0);
        r.cameraDirty = false; ++r.counters.cameraUploads;
    }
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(r.width), static_cast<float>(r.height), 0, 1};
    r.context->RSSetViewports(1, &viewport); r.context->RSSetState(r.raster.Get());
    r.context->OMSetDepthStencilState(r.noDepth.Get(), 0);
    ID3D11ShaderResourceView *emptyResource = nullptr;
    r.context->PSSetShaderResources(0, 1, &emptyResource);
    auto *linear = r.linearView.Get();
    r.context->OMSetRenderTargets(1, &linear, nullptr);
    constexpr float clear[4]{};
    r.context->ClearRenderTargetView(linear, clear);
    r.context->OMSetBlendState(r.overBlend.Get(), nullptr, UINT_MAX);
    r.context->IASetInputLayout(r.layout.Get());
    r.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.context->VSSetShader(r.sceneVS.Get(), nullptr, 0); r.context->PSSetShader(r.scenePS.Get(), nullptr, 0);
    auto *camera = r.cameraBuffer.Get();
    r.context->VSSetConstantBuffers(0, 1, &camera);
    UINT stride = sizeof(Vertex), offset = 0;
    for (const auto &draw : r.draws) {
        if (draw.values.opacity == 0 || draw.values.tint[3] == 0) continue;
        auto *vertices = draw.mesh->vertices.Get();
        r.context->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
        r.context->IASetIndexBuffer(draw.mesh->indices.Get(), DXGI_FORMAT_R32_UINT, 0);
        auto *constants = draw.constants.Get();
        r.context->VSSetConstantBuffers(1, 1, &constants); r.context->PSSetConstantBuffers(1, 1, &constants);
        auto *texture = draw.texture->view.Get();
        auto *sampler = draw.texture->filter == TextureFilter::nearest ? r.nearestSampler.Get() : r.linearSampler.Get();
        r.context->PSSetShaderResources(0, 1, &texture); r.context->PSSetSamplers(0, 1, &sampler);
        r.context->DrawIndexed(draw.mesh->indexCount, 0, 0); ++r.counters.drawCalls;
    }
    r.context->PSSetShaderResources(0, 1, &emptyResource);
    auto *output = r.outputView.Get();
    r.context->OMSetRenderTargets(1, &output, nullptr);
    // Native captions/modules form a separate premultiplied encoded surface,
    // matching the source's Core Animation layer above its Metal attachment.
    // Preserve original shader output; only the native surface is composited.
    r.context->OMSetBlendState(originalBackground ? r.overBlend.Get() : r.replaceBlend.Get(), nullptr, UINT_MAX);
    r.context->IASetInputLayout(nullptr);
    r.context->VSSetShader(r.compositeVS.Get(), nullptr, 0); r.context->PSSetShader(r.compositePS.Get(), nullptr, 0);
    auto *source = r.linearResource.Get();
    r.context->PSSetShaderResources(0, 1, &source);
    r.context->Draw(3, 0);
    r.context->PSSetShaderResources(0, 1, &emptyResource);
    r.context->OMSetRenderTargets(0, nullptr, nullptr);
    if (present) {
        checked(r.swapchain->Present(1, 0), "Present HUD composition");
        ++r.counters.presents;
    } else r.readbackReady = true;
}
Readback Renderer::readback() {
    require(impl_ != nullptr, "Renderer is not initialized"); auto &r = *impl_; r.thread();
    require(r.readbackReady, "Readback requires a completed render-only draw");
    D3D11_TEXTURE2D_DESC description{};
    r.backBuffer->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ; description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    checked(r.device->CreateTexture2D(&description, nullptr, &staging), "Create application-target readback");
    r.context->CopyResource(staging.Get(), r.backBuffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    checked(r.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read application render target");
    Readback result;
    result.width = r.width; result.height = r.height; result.rowBytes = r.width * 4;
    try {
        result.pixels.resize(std::size_t(result.rowBytes) * result.height);
        for (std::size_t row = 0; row < result.height; ++row)
            std::memcpy(result.pixels.data() + row * result.rowBytes,
                        static_cast<const std::uint8_t *>(mapped.pData) + row * mapped.RowPitch, result.rowBytes);
    } catch (...) { r.context->Unmap(staging.Get(), 0); throw; }
    r.context->Unmap(staging.Get(), 0);
    return result;
}
bool Renderer::removeMesh(const std::string &sourceID) {
    if (!impl_) return false;
    auto &r = *impl_; r.thread(); auto item = r.meshes.find(sourceID);
    if (item == r.meshes.end() || std::any_of(r.draws.begin(), r.draws.end(), [&](const auto &draw) { return draw.mesh == &item->second; })) return false;
    r.context->ClearState(); r.counters.resourceBytes -= item->second.bytes; r.meshes.erase(item); return true;
}
bool Renderer::removeTexture(const std::string &sourceID) {
    if (!impl_) return false;
    auto &r = *impl_; r.thread(); auto item = r.textures.find(sourceID);
    if (item == r.textures.end() || std::any_of(r.draws.begin(), r.draws.end(), [&](const auto &draw) { return draw.texture == &item->second; })) return false;
    r.counters.resourceBytes -= item->second.bytes; r.textures.erase(item); return true;
}
void Renderer::clearDrawList() { if (impl_) { impl_->thread(); impl_->draws.clear(); impl_->context->ClearState(); } }
void Renderer::clearResources() {
    if (!impl_) return;
    impl_->thread(); impl_->draws.clear(); impl_->meshes.clear(); impl_->textures.clear();
    impl_->context->ClearState(); impl_->counters.resourceBytes = 0;
    if (impl_->source) impl_->source->clear();
}
void Renderer::reset() noexcept { impl_.reset(); }
RendererStats Renderer::stats() const noexcept {
    if (!impl_) return {};
    auto result = impl_->counters;
    result.meshes = impl_->meshes.size(); result.textures = impl_->textures.size(); result.objects = impl_->draws.size();
    return result;
}
RendererDeviceInfo Renderer::deviceInfo() const {
    require(impl_ != nullptr, "Renderer is not initialized"); impl_->thread();
    ComPtr<IDXGIDevice> device; checked(impl_->device.As(&device), "Read renderer DXGI device");
    ComPtr<IDXGIAdapter> adapter; checked(device->GetAdapter(&adapter), "Read selected renderer adapter");
    DXGI_ADAPTER_DESC description{}; checked(adapter->GetDesc(&description), "Read selected renderer identity");
    description.Description[127] = 0;
    const auto length = static_cast<int>(std::wcslen(description.Description));
    const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, description.Description, length, nullptr, 0, nullptr, nullptr);
    require(bytes > 0, "Selected renderer adapter has no valid name");
    std::string name(static_cast<std::size_t>(bytes), '\0');
    require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, description.Description, length, name.data(), bytes, nullptr, nullptr) == bytes,
        "Cannot encode selected renderer adapter name");
    return {std::move(name), description.VendorId, description.DeviceId,
        static_cast<std::uint64_t>(description.DedicatedVideoMemory), static_cast<std::uint64_t>(description.SharedSystemMemory)};
}
SourceGraphics& Renderer::sourceGraphics() {
    require(impl_ != nullptr, "Renderer is not initialized"); impl_->thread();
    if (!impl_->source) impl_->source = std::make_unique<SourceGraphics>(impl_->device.Get(), impl_->context.Get());
    return *impl_->source;
}

} // namespace endfield::native
