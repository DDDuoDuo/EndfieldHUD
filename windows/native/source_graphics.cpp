#include "source_graphics.hpp"
#include "renderer.hpp"
#include "source_gpu_copy.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <set>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
void need(bool okay, const char* message) { if (!okay) throw std::invalid_argument(message); }
void okay(HRESULT code, const char* message) { if (FAILED(code)) throw RendererError(message, code); }
void idCheck(const std::string& id) { need(!id.empty() && id.size() <= 512, "Invalid original resource identity"); }
constexpr std::size_t maxBytes = 512ull * 1024 * 1024;
D3D11_BLEND blend(unsigned metal) {
    static constexpr D3D11_BLEND values[]{D3D11_BLEND_ZERO,D3D11_BLEND_ONE,D3D11_BLEND_SRC_COLOR,
        D3D11_BLEND_INV_SRC_COLOR,D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_ALPHA,D3D11_BLEND_DEST_COLOR,
        D3D11_BLEND_INV_DEST_COLOR,D3D11_BLEND_DEST_ALPHA,D3D11_BLEND_INV_DEST_ALPHA,D3D11_BLEND_SRC_ALPHA_SAT};
    need(metal < std::size(values), "Unsupported original blend factor"); return values[metal];
}
D3D11_COMPARISON_FUNC comparison(unsigned metal) { need(metal < 8, "Invalid source comparison"); return static_cast<D3D11_COMPARISON_FUNC>(metal + 1); }
D3D11_STENCIL_OP stencil(unsigned metal) { need(metal < 8, "Invalid source stencil operation"); return static_cast<D3D11_STENCIL_OP>(metal + 1); }
D3D11_TEXTURE_ADDRESS_MODE address(unsigned metal) {
    static constexpr D3D11_TEXTURE_ADDRESS_MODE values[]{D3D11_TEXTURE_ADDRESS_CLAMP,D3D11_TEXTURE_ADDRESS_MIRROR_ONCE,
        D3D11_TEXTURE_ADDRESS_WRAP,D3D11_TEXTURE_ADDRESS_MIRROR,D3D11_TEXTURE_ADDRESS_BORDER};
    need(metal < std::size(values), "Unsupported source sampler address mode"); return values[metal];
}
DXGI_FORMAT format(const std::string& name) {
    static const std::map<std::string, DXGI_FORMAT> formats{{"rgba8Unorm",DXGI_FORMAT_R8G8B8A8_UNORM},
        {"rgba8Unorm_srgb",DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},{"bgra8Unorm",DXGI_FORMAT_B8G8R8A8_UNORM},
        {"bgra8Unorm_srgb",DXGI_FORMAT_B8G8R8A8_UNORM_SRGB},{"r8Unorm",DXGI_FORMAT_R8_UNORM},
        {"bc7_rgbaUnorm",DXGI_FORMAT_BC7_UNORM},{"bc7_rgbaUnorm_srgb",DXGI_FORMAT_BC7_UNORM_SRGB}};
    const auto it = formats.find(name); need(it != formats.end(), "Unsupported original pixel format"); return it->second;
}
ComPtr<ID3D11Buffer> buffer(ID3D11Device* device, UINT flags, const void* bytes, std::size_t size, bool dynamic = false) {
    need(size > 0 && size <= maxBytes, "Invalid original GPU buffer size");
    D3D11_BUFFER_DESC d{}; d.ByteWidth = static_cast<UINT>(size); d.BindFlags = flags;
    d.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
    d.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = bytes;
    ComPtr<ID3D11Buffer> result;
    okay(device->CreateBuffer(&d, &initial, &result), "Create original retained buffer"); return result;
}
// CPU time of a scope, added to a diagnostics counter (numbers only).
double sourceSeconds() noexcept {
    static const double frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return double(f.QuadPart); }();
    LARGE_INTEGER v{}; QueryPerformanceCounter(&v); return double(v.QuadPart) / frequency;
}
struct SourceTimed { double& total; double begin{sourceSeconds()}; ~SourceTimed() { total += sourceSeconds() - begin; } };
}
struct SourceGraphics::Impl {
    struct Mesh { ComPtr<ID3D11Buffer> vertices, indices; unsigned stride{}, indexCount{}; std::size_t bytes{},vertexCapacity{},indexCapacity{};std::uint64_t revision{};
        std::vector<std::uint32_t> indexData; gpu_copy::Copy savedVertices, savedIndices; };
    struct Texture { ComPtr<ID3D11ShaderResourceView> view; ComPtr<ID3D11SamplerState> sampler; std::size_t bytes{}; std::uint64_t revision{};
        gpu_copy::Copy saved; D3D11_SAMPLER_DESC samplerDesc{}; };
    // The programs' bytecode and input layout are kept to create the
    // pipeline again on another device (suspend/resume); the fixed states
    // are read back from their objects.
    struct Pipeline { ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps; ComPtr<ID3D11InputLayout> layout;
        ComPtr<ID3D11BlendState> blend; ComPtr<ID3D11DepthStencilState> depth; ComPtr<ID3D11RasterizerState> raster; unsigned vertexBytes{};
        std::vector<std::uint8_t> vertexCode, fragmentCode; std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        D3D11_BLEND_DESC blendDesc{}; D3D11_DEPTH_STENCIL_DESC depthDesc{}; D3D11_RASTERIZER_DESC rasterDesc{}; };
    struct Uniform { ComPtr<ID3D11Buffer> buffer; std::vector<std::uint8_t> bytes; };
    struct Constant { SourceStage stage; unsigned slot; Uniform* value; };
    struct Image { SourceStage stage; unsigned slot, samplerSlot; Texture* value; };
    struct Draw { Mesh* mesh; Pipeline* pipeline; unsigned firstIndex, indexCount, stencilReference;
        std::vector<Constant> constants; std::vector<Image> images; };
    DWORD owner{GetCurrentThreadId()}; ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    std::map<std::string, Mesh> meshes; std::map<std::string, Texture> textures;
    std::map<std::string, Pipeline> pipelines; std::map<std::string, Uniform> uniforms;
    std::vector<Draw> draws;
    ComPtr<ID3D11Texture2D> color, depth;
    ComPtr<ID3D11RenderTargetView> colorView; ComPtr<ID3D11DepthStencilView> depthView;
    unsigned width{}, height{};
    SourceGraphicsStats counters;
    bool suspended{}; std::size_t suspendedBytes{};
    void thread() const { need(owner == GetCurrentThreadId(), "Source GPU calls require their owner thread"); }
    void live() const { thread(); need(!suspended, "The original pass's device is suspended"); }
    void releaseObjects() noexcept {
        for (auto& [id, m] : meshes) { (void)id; m.vertices.Reset(); m.indices.Reset(); }
        for (auto& [id, t] : textures) { (void)id; t.view.Reset(); t.sampler.Reset(); }
        for (auto& [id, p] : pipelines) { (void)id; p.vs.Reset(); p.ps.Reset(); p.layout.Reset(); p.blend.Reset(); p.depth.Reset(); p.raster.Reset(); }
        for (auto& [id, u] : uniforms) { (void)id; u.buffer.Reset(); }
        colorView.Reset(); depthView.Reset(); color.Reset(); depth.Reset(); width = height = 0;
        if (context) { context->ClearState(); context->Flush(); }
    }
    void budget(std::size_t previous, std::size_t incoming) const {
        need(incoming <= maxBytes && counters.payloadBytes - previous <= maxBytes - incoming, "Original GPU resource budget exceeded");
    }
    void targets(unsigned w, unsigned h) {
        need(w && h && std::uint64_t(w) * h <= Renderer::maximumRenderPixels, "Invalid original target extent");
        if (w == width && h == height) return;
        ComPtr<ID3D11Texture2D> nextColor, nextDepth;
        ComPtr<ID3D11RenderTargetView> nextColorView; ComPtr<ID3D11DepthStencilView> nextDepthView;
        D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS; d.BindFlags = D3D11_BIND_RENDER_TARGET;
        okay(device->CreateTexture2D(&d, nullptr, &nextColor), "Create original LDR target");
        D3D11_RENDER_TARGET_VIEW_DESC view{}; view.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        okay(device->CreateRenderTargetView(nextColor.Get(), &view, &nextColorView), "Bind original sRGB attachment");
        d.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT; d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        okay(device->CreateTexture2D(&d, nullptr, &nextDepth), "Create original depth32 stencil8 attachment");
        okay(device->CreateDepthStencilView(nextDepth.Get(), nullptr, &nextDepthView), "Bind original depth/stencil");
        color = std::move(nextColor); depth = std::move(nextDepth);
        colorView = std::move(nextColorView); depthView = std::move(nextDepthView); width = w; height = h;
    }
};
std::size_t SourceGraphics::releaseTargets() noexcept {
    auto& r = *impl_;
    if (r.owner != GetCurrentThreadId() || !r.color) return 0;
    const auto bytes = std::size_t(r.width) * r.height * 12; // B8G8R8A8 color + D32_FLOAT_S8X24 depth
    r.colorView.Reset(); r.depthView.Reset(); r.color.Reset(); r.depth.Reset(); r.width = r.height = 0;
    return bytes;
}
SourceGraphics::SourceGraphics(void* device, void* context) : impl_(std::make_unique<Impl>()) {
    need(device && context, "Original rendering requires the existing native device");
    impl_->device = static_cast<ID3D11Device*>(device); impl_->context = static_cast<ID3D11DeviceContext*>(context);
}
SourceGraphics::~SourceGraphics() = default;
void SourceGraphics::setMesh(const std::string& id, std::uint64_t revision, unsigned stride,
                             std::span<const std::uint8_t> vertices, std::span<const std::uint32_t> indices) {
    auto& r = *impl_; r.live(); idCheck(id); const SourceTimed timed{r.counters.uploadSeconds};
    auto old = r.meshes.find(id); if (old != r.meshes.end() && old->second.revision == revision) return;
    need(old != r.meshes.end() || r.meshes.size() < 4096, "Original mesh count exceeds bounds");
    need(stride >= 8 && stride <= 256 && !vertices.empty() && vertices.size() % stride == 0 &&
         vertices.size() / stride <= 1000000 && !indices.empty() && indices.size() <= 3000000 && indices.size() % 3 == 0,
         "Invalid original mesh layout");
    for (auto index : indices) need(index < vertices.size() / stride, "Original index exceeds mesh");
    if (old != r.meshes.end()) {
        // Draws retain the map entry. Validate against the incoming layout
        // before replacing its buffers so a rejected update leaves it usable.
        for (const auto& draw : r.draws) if (draw.mesh == &old->second) {
            need(stride >= draw.pipeline->vertexBytes && draw.firstIndex <= indices.size() &&
                 draw.indexCount <= indices.size() - draw.firstIndex,
                 "Original mesh replacement would invalidate a retained draw");
        }
    }
    const auto capacity=[](std::size_t previous,std::size_t requested){
        if(requested<=previous)return previous;std::size_t next=std::max<std::size_t>(previous,16);
        while(next<requested){if(next>maxBytes/2)return requested;next*=2;}return next;};
    const auto vertexCapacity=capacity(old==r.meshes.end()?0:old->second.vertexCapacity,vertices.size_bytes());
    const auto indexCapacity=capacity(old==r.meshes.end()?0:old->second.indexCapacity,indices.size_bytes());
    const auto size=vertexCapacity+indexCapacity,previous=old==r.meshes.end()?0:old->second.bytes;r.budget(previous,size);
    const bool newVertex=old==r.meshes.end()||vertexCapacity!=old->second.vertexCapacity;
    const bool newIndex=old==r.meshes.end()||indexCapacity!=old->second.indexCapacity;
    const bool changedIndex=old==r.meshes.end()||old->second.indexData.size()!=indices.size()||
        !std::equal(indices.begin(),indices.end(),old->second.indexData.begin());
    const auto allocate=[&](UINT flags,std::size_t bytes){D3D11_BUFFER_DESC desc{};desc.ByteWidth=static_cast<UINT>(bytes);desc.BindFlags=flags;
        desc.Usage=D3D11_USAGE_DYNAMIC;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;ComPtr<ID3D11Buffer> result;
        okay(r.device->CreateBuffer(&desc,nullptr,&result),"Create retained original mesh capacity");return result;};
    auto vertexBuffer=newVertex?allocate(D3D11_BIND_VERTEX_BUFFER,vertexCapacity):old->second.vertices;
    auto indexBuffer=newIndex?allocate(D3D11_BIND_INDEX_BUFFER,indexCapacity):old->second.indices;
    // Capacity is independent of current count. WRITE_DISCARD permits driver
    // renaming while the prior submitted frame still reads its old data.
    const auto upload=[&](ID3D11Buffer* output,const void* data,std::size_t bytes){D3D11_MAPPED_SUBRESOURCE mapped{};
        okay(r.context->Map(output,0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Update retained original mesh bytes");
        std::memcpy(mapped.pData,data,bytes);r.context->Unmap(output,0);};
    std::vector<std::uint32_t> firstIndices;if(old==r.meshes.end())firstIndices.assign(indices.begin(),indices.end());
    else if(changedIndex)old->second.indexData.reserve(indices.size()); // before GPU writes
    upload(vertexBuffer.Get(),vertices.data(),vertices.size_bytes());if(changedIndex)upload(indexBuffer.Get(),indices.data(),indices.size_bytes());
    if(old==r.meshes.end()){Impl::Mesh mesh;mesh.vertices=std::move(vertexBuffer);mesh.indices=std::move(indexBuffer);mesh.stride=stride;mesh.indexCount=static_cast<unsigned>(indices.size());
        mesh.bytes=size;mesh.vertexCapacity=vertexCapacity;mesh.indexCapacity=indexCapacity;mesh.revision=revision;mesh.indexData=std::move(firstIndices);r.meshes.emplace(id,std::move(mesh));}
    else{auto& mesh=old->second;mesh.vertices=std::move(vertexBuffer);mesh.indices=std::move(indexBuffer);mesh.stride=stride;mesh.indexCount=static_cast<unsigned>(indices.size());
        mesh.bytes=size;mesh.vertexCapacity=vertexCapacity;mesh.indexCapacity=indexCapacity;mesh.revision=revision;if(changedIndex)mesh.indexData.assign(indices.begin(),indices.end());}
    r.counters.payloadBytes+=size-previous;++r.counters.geometryUploads;++r.counters.vertexUploads;if(changedIndex)++r.counters.indexUploads;
    r.counters.meshBufferAllocations+=std::uint64_t(newVertex)+std::uint64_t(newIndex);
}
void SourceGraphics::setTexture(std::string id, std::uint64_t revision, const SourceTexture& input) {
    auto& r = *impl_; r.live(); idCheck(id); const SourceTimed timed{r.counters.uploadSeconds};
    auto old = r.textures.find(id); if (old != r.textures.end() && old->second.revision == revision) return;
    need(old != r.textures.end() || r.textures.size() < 2048, "Original texture count exceeds bounds");
    need(!input.mips.empty() && input.mips.size() <= 15, "Invalid source mip count");
    const auto pixel = format(input.pixelFormat); const bool compressed = pixel == DXGI_FORMAT_BC7_UNORM || pixel == DXGI_FORMAT_BC7_UNORM_SRGB;
    const auto& base = input.mips[0]; need(base.width && base.height && base.width <= 16384 && base.height <= 16384, "Invalid source texture dimensions");
    std::size_t size{}; std::vector<D3D11_SUBRESOURCE_DATA> levels;
    for (unsigned i = 0; i < input.mips.size(); ++i) {
        const auto& mip = input.mips[i];
        const auto rows = compressed ? (mip.height + 3) / 4 : mip.height;
        const auto pitch = compressed ? (mip.width + 3) / 4 * 16 : mip.width * (pixel == DXGI_FORMAT_R8_UNORM ? 1 : 4);
        need(mip.width == std::max(1u, base.width >> i) && mip.height == std::max(1u, base.height >> i) &&
             mip.rowBytes == pitch && mip.bytes.size() == std::size_t(rows) * pitch, "Original mip data mismatch");
        need(mip.bytes.size() <= maxBytes - size, "Original texture too large"); size += mip.bytes.size();
        levels.push_back({mip.bytes.data(), pitch, 0});
    }
    const auto previous = old == r.textures.end() ? 0 : old->second.bytes; r.budget(previous, size);
    D3D11_TEXTURE2D_DESC d{}; d.Width = base.width; d.Height = base.height; d.MipLevels = static_cast<UINT>(levels.size());
    d.ArraySize = d.SampleDesc.Count = 1; d.Format = pixel; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture; okay(r.device->CreateTexture2D(&d, levels.data(), &texture), "Upload exact original mip chain");
    Impl::Texture value; okay(r.device->CreateShaderResourceView(texture.Get(), nullptr, &value.view), "Bind original source texture");
    need(input.minFilter <= 1 && input.magFilter <= 1 && input.mipFilter <= 2 && input.maxAnisotropy >= 1 && input.maxAnisotropy <= 16, "Invalid original sampler filters");
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_ENCODE_BASIC_FILTER(input.minFilter, input.magFilter, input.mipFilter == 2 ? 1 : 0, D3D11_FILTER_REDUCTION_TYPE_STANDARD);
    if (input.maxAnisotropy > 1 && input.minFilter == 1 && input.magFilter == 1) sampler.Filter = D3D11_FILTER_ANISOTROPIC;
    sampler.AddressU = address(input.addressU); sampler.AddressV = address(input.addressV); sampler.AddressW = address(input.addressW);
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER; sampler.MaxAnisotropy = input.maxAnisotropy;
    sampler.MaxLOD = input.mipFilter ? D3D11_FLOAT32_MAX : 0;
    okay(r.device->CreateSamplerState(&sampler, &value.sampler), "Create exact original sampler");
    value.bytes = size; value.revision = revision;
    r.textures.insert_or_assign(std::move(id), std::move(value)); r.counters.payloadBytes += size - previous; ++r.counters.textureUploads;
}
void SourceGraphics::setPipeline(std::string id, const SourcePipeline& input) {
    auto& r = *impl_; r.live(); idCheck(id);
    need(!r.pipelines.contains(id), "An original pipeline identity is immutable");
    need(r.pipelines.size() < 2048 && !input.vertexBytecode.empty() && !input.fragmentBytecode.empty() &&
         input.vertexBytecode.size() <= 4 * 1024 * 1024 && input.fragmentBytecode.size() <= 4 * 1024 * 1024 && input.attributes.size() <= 16,
         "Invalid original shader program dimensions");
    Impl::Pipeline pipeline;
    okay(r.device->CreateVertexShader(input.vertexBytecode.data(), input.vertexBytecode.size(), nullptr, &pipeline.vs), "Load original vertex bytecode");
    okay(r.device->CreatePixelShader(input.fragmentBytecode.data(), input.fragmentBytecode.size(), nullptr, &pipeline.ps), "Load original fragment bytecode");
    std::vector<D3D11_INPUT_ELEMENT_DESC> attributes; std::set<unsigned> locations;
    constexpr DXGI_FORMAT formats[]{DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32_FLOAT,DXGI_FORMAT_R32G32B32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT};
    for (const auto& value : input.attributes) {
        need(value.components >= 1 && value.components <= 4 && value.byteOffset <= 240 && value.location < 16 && locations.insert(value.location).second,
             "Invalid original vertex attribute");
        attributes.push_back({"TEXCOORD", value.location, formats[value.components - 1], 0, value.byteOffset, D3D11_INPUT_PER_VERTEX_DATA, 0});
        pipeline.vertexBytes = std::max(pipeline.vertexBytes, value.byteOffset + value.components * 4);
    }
    if (!attributes.empty()) okay(r.device->CreateInputLayout(attributes.data(), static_cast<UINT>(attributes.size()), input.vertexBytecode.data(),
        input.vertexBytecode.size(), &pipeline.layout), "Bind original vertex attributes");
    need(input.rgbOperation <= 4 && input.alphaOperation <= 4 && input.writeMask <= 15, "Invalid original blend operation or write mask");
    D3D11_BLEND_DESC blendState{}; auto& b = blendState.RenderTarget[0]; b.BlendEnable = input.blendEnabled;
    b.SrcBlend = blend(input.sourceRGB); b.DestBlend = blend(input.destinationRGB);
    b.SrcBlendAlpha = blend(input.sourceAlpha); b.DestBlendAlpha = blend(input.destinationAlpha);
    b.BlendOp = static_cast<D3D11_BLEND_OP>(input.rgbOperation + 1); b.BlendOpAlpha = static_cast<D3D11_BLEND_OP>(input.alphaOperation + 1);
    b.RenderTargetWriteMask = static_cast<UINT8>(((input.writeMask & 8) >> 3) | ((input.writeMask & 4) >> 1) | ((input.writeMask & 2) << 1) | ((input.writeMask & 1) << 3));
    okay(r.device->CreateBlendState(&blendState, &pipeline.blend), "Create original blend state");
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = TRUE; depth.DepthFunc = comparison(input.depthCompare);
    depth.DepthWriteMask = input.depthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.StencilEnable = input.stencilEnabled;
    need(input.front.readMask <= 255 && input.front.writeMask <= 255 && input.front.readMask == input.back.readMask && input.front.writeMask == input.back.writeMask,
         "D3D11 requires identical front/back stencil masks");
    depth.StencilReadMask = static_cast<UINT8>(input.front.readMask); depth.StencilWriteMask = static_cast<UINT8>(input.front.writeMask);
    const auto face = [](const SourceStencilFace& s) { return D3D11_DEPTH_STENCILOP_DESC{stencil(s.fail),stencil(s.depthFail),stencil(s.pass),comparison(s.compare)}; };
    depth.FrontFace = face(input.front); depth.BackFace = face(input.back);
    okay(r.device->CreateDepthStencilState(&depth, &pipeline.depth), "Create original depth/stencil state");
    need(input.cull <= 2, "Unsupported original cull mode");
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = static_cast<D3D11_CULL_MODE>(input.cull + 1);
    raster.DepthClipEnable = TRUE;
    okay(r.device->CreateRasterizerState(&raster, &pipeline.raster), "Create original raster state");
    pipeline.vertexCode.assign(input.vertexBytecode.begin(), input.vertexBytecode.end());
    pipeline.fragmentCode.assign(input.fragmentBytecode.begin(), input.fragmentBytecode.end());
    pipeline.elements = std::move(attributes);
    r.pipelines.emplace(std::move(id), std::move(pipeline));
}
void SourceGraphics::setUniform(const std::string& id, std::span<const std::uint8_t> bytes) {
    auto& r = *impl_; r.live(); idCheck(id); const SourceTimed timed{r.counters.uploadSeconds};
    need(!bytes.empty() && bytes.size() <= 65536 && bytes.size() % 16 == 0, "Original constant buffer must be bounded and 16-byte aligned");
    auto old = r.uniforms.find(id);
    if (old != r.uniforms.end() && std::equal(bytes.begin(), bytes.end(), old->second.bytes.begin(), old->second.bytes.end())) return;
    need(old != r.uniforms.end() || r.uniforms.size() < 8192, "Original uniform count exceeds bounds");
    const auto previous = old == r.uniforms.end() ? 0 : old->second.bytes.size(); r.budget(previous, bytes.size());
    if (old == r.uniforms.end() || previous != bytes.size()) {
        Impl::Uniform value{buffer(r.device.Get(), D3D11_BIND_CONSTANT_BUFFER, bytes.data(), bytes.size(), true), {bytes.begin(), bytes.end()}};
        r.uniforms.insert_or_assign(id, std::move(value)); ++r.counters.uniformAllocations;
    } else {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        okay(r.context->Map(old->second.buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Update changed original constants");
        std::memcpy(mapped.pData, bytes.data(), bytes.size()); r.context->Unmap(old->second.buffer.Get(), 0);
        std::copy(bytes.begin(), bytes.end(), old->second.bytes.begin());
    }
    r.counters.payloadBytes += bytes.size() - previous; ++r.counters.uniformUploads;
}
void SourceGraphics::setDraws(std::span<const SourceDraw> inputs) {
    auto& r = *impl_; r.live(); need(inputs.size() <= 16384, "Too many original draw passes");
    std::vector<Impl::Draw> draws; draws.reserve(inputs.size());
    for (const auto& input : inputs) {
        auto mesh = r.meshes.find(input.mesh); auto pipeline = r.pipelines.find(input.pipeline);
        need(mesh != r.meshes.end() && pipeline != r.pipelines.end(), "Original draw resource is missing");
        need(mesh->second.stride >= pipeline->second.vertexBytes && input.firstIndex <= mesh->second.indexCount && input.indexCount > 0 &&
             input.indexCount <= mesh->second.indexCount - input.firstIndex && input.indexCount % 3 == 0 && input.stencilReference <= 255,
             "Invalid original draw range");
        std::set<std::pair<int,unsigned>> buffers, textures, samplers;
        Impl::Draw draw{&mesh->second,&pipeline->second,input.firstIndex,input.indexCount,input.stencilReference,{}, {}};
        for (const auto& value : input.uniforms) {
            need(value.stage == SourceStage::vertex || value.stage == SourceStage::fragment, "Invalid original uniform stage");
            need(value.slot < 14 && r.uniforms.contains(value.id) && buffers.emplace(int(value.stage),value.slot).second, "Invalid/duplicate original constant binding");
            draw.constants.push_back({value.stage,value.slot,&r.uniforms.at(value.id)});
        }
        for (const auto& value : input.textures) {
            need(value.stage == SourceStage::vertex || value.stage == SourceStage::fragment, "Invalid original texture stage");
            need(value.slot < 128 && value.samplerSlot < 16 && r.textures.contains(value.id) && textures.emplace(int(value.stage),value.slot).second &&
                 samplers.emplace(int(value.stage),value.samplerSlot).second, "Invalid/duplicate original texture binding");
            draw.images.push_back({value.stage,value.slot,value.samplerSlot,&r.textures.at(value.id)});
        }
        draws.push_back(std::move(draw));
    }
    r.draws = std::move(draws);
}
void SourceGraphics::renderTo(void* destination, unsigned width, unsigned height) {
    auto& r = *impl_; r.live(); need(destination, "Missing original output target"); const SourceTimed timed{r.counters.renderSeconds};
    auto* output = static_cast<ID3D11Texture2D*>(destination); D3D11_TEXTURE2D_DESC outputDescription{}; output->GetDesc(&outputDescription);
    need(outputDescription.Width == width && outputDescription.Height == height && outputDescription.Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
         outputDescription.SampleDesc.Count == 1, "Original output target contract mismatch");
    r.targets(width,height); r.context->ClearState();
    auto* target = r.colorView.Get(); r.context->OMSetRenderTargets(1, &target, r.depthView.Get());
    const float transparent[4]{}; r.context->ClearRenderTargetView(target, transparent);
    r.context->ClearDepthStencilView(r.depthView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);
    const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1}; r.context->RSSetViewports(1,&viewport);
    r.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // The ordered passes share most of their state: only what changes from
    // one draw to the next is bound (ClearState above left everything null).
    struct Bound {
        ID3D11Buffer* vertices{}; UINT stride{}; ID3D11Buffer* indices{}; ID3D11InputLayout* layout{};
        ID3D11VertexShader* vs{}; ID3D11PixelShader* ps{}; ID3D11RasterizerState* raster{}; ID3D11BlendState* blend{};
        ID3D11DepthStencilState* depth{}; UINT stencil{~0u};
        std::array<ID3D11Buffer*, 14> constants[2]{}; std::array<ID3D11ShaderResourceView*, 128> views[2]{}; std::array<ID3D11SamplerState*, 16> samplers[2]{};
    } bound;
    for (const auto& draw : r.draws) {
        const auto& p = *draw.pipeline; auto* vertices = draw.mesh->vertices.Get(); const UINT offset{};
        if (vertices != bound.vertices || draw.mesh->stride != bound.stride) { r.context->IASetVertexBuffers(0,1,&vertices,&draw.mesh->stride,&offset); bound.vertices = vertices; bound.stride = draw.mesh->stride; }
        if (draw.mesh->indices.Get() != bound.indices) { r.context->IASetIndexBuffer(draw.mesh->indices.Get(),DXGI_FORMAT_R32_UINT,0); bound.indices = draw.mesh->indices.Get(); }
        if (p.layout.Get() != bound.layout) { r.context->IASetInputLayout(p.layout.Get()); bound.layout = p.layout.Get(); }
        if (p.vs.Get() != bound.vs) { r.context->VSSetShader(p.vs.Get(),nullptr,0); bound.vs = p.vs.Get(); }
        if (p.ps.Get() != bound.ps) { r.context->PSSetShader(p.ps.Get(),nullptr,0); bound.ps = p.ps.Get(); }
        if (p.raster.Get() != bound.raster) { r.context->RSSetState(p.raster.Get()); bound.raster = p.raster.Get(); }
        if (p.blend.Get() != bound.blend) { r.context->OMSetBlendState(p.blend.Get(),nullptr,UINT_MAX); bound.blend = p.blend.Get(); }
        if (p.depth.Get() != bound.depth || draw.stencilReference != bound.stencil) { r.context->OMSetDepthStencilState(p.depth.Get(),draw.stencilReference); bound.depth = p.depth.Get(); bound.stencil = draw.stencilReference; }
        for (const auto& u : draw.constants) {
            auto* value = u.value->buffer.Get(); const auto stage = u.stage == SourceStage::vertex ? 0 : 1;
            if (bound.constants[stage][u.slot] == value) continue;
            bound.constants[stage][u.slot] = value;
            if (stage == 0) r.context->VSSetConstantBuffers(u.slot,1,&value); else r.context->PSSetConstantBuffers(u.slot,1,&value);
        }
        for (const auto& t : draw.images) {
            const auto& texture = *t.value; auto* view = texture.view.Get(); auto* sampler = texture.sampler.Get(); const auto stage = t.stage == SourceStage::vertex ? 0 : 1;
            if (bound.views[stage][t.slot] != view) { bound.views[stage][t.slot] = view; if (stage == 0) r.context->VSSetShaderResources(t.slot,1,&view); else r.context->PSSetShaderResources(t.slot,1,&view); }
            if (bound.samplers[stage][t.samplerSlot] != sampler) { bound.samplers[stage][t.samplerSlot] = sampler; if (stage == 0) r.context->VSSetSamplers(t.samplerSlot,1,&sampler); else r.context->PSSetSamplers(t.samplerSlot,1,&sampler); }
        }
        r.context->DrawIndexed(draw.indexCount,draw.firstIndex,0);
    }
    r.context->OMSetRenderTargets(0,nullptr,nullptr); r.context->CopyResource(output,r.color.Get()); ++r.counters.frames;
}
bool SourceGraphics::active() const noexcept { return !impl_->draws.empty(); }
void SourceGraphics::suspend() {
    auto& r = *impl_; r.thread();
    if (r.suspended) return;
    r.context->ClearState(); (void)releaseTargets();
    // Copies first (every GPU copy issued before the first map); nothing is
    // released until all of them are in system memory.
    std::vector<gpu_copy::Copy*> staged; std::size_t total{};
    try {
        for (auto& [id, m] : r.meshes) { (void)id;
            m.savedVertices = gpu_copy::stage(r.device.Get(), r.context.Get(), m.vertices.Get(), nullptr); staged.push_back(&m.savedVertices);
            m.savedIndices = gpu_copy::stage(r.device.Get(), r.context.Get(), m.indices.Get(), nullptr); staged.push_back(&m.savedIndices); }
        for (auto& [id, t] : r.textures) { (void)id;
            t.saved = gpu_copy::stage(r.device.Get(), r.context.Get(), gpu_copy::resourceOf(t.view.Get()).Get(), t.view.Get()); staged.push_back(&t.saved);
            t.sampler->GetDesc(&t.samplerDesc); }
        for (auto* copy : staged) { gpu_copy::finish(r.context.Get(), *copy); total += copy->bytes(); }
    } catch (...) {
        for (auto& [id, m] : r.meshes) { (void)id; m.savedVertices = {}; m.savedIndices = {}; }
        for (auto& [id, t] : r.textures) { (void)id; t.saved = {}; }
        throw;
    }
    for (auto& [id, p] : r.pipelines) { (void)id; p.blend->GetDesc(&p.blendDesc); p.depth->GetDesc(&p.depthDesc); p.raster->GetDesc(&p.rasterDesc); }
    r.releaseObjects(); r.context.Reset(); r.device.Reset();
    r.suspended = true; r.suspendedBytes = total;
}
void SourceGraphics::resume(void* device, void* context) {
    auto& r = *impl_; r.thread(); need(device && context, "Original rendering requires the existing native device");
    if (!r.suspended) return;
    r.device = static_cast<ID3D11Device*>(device); r.context = static_cast<ID3D11DeviceContext*>(context);
    try {
        for (auto& [id, m] : r.meshes) { (void)id;
            okay(gpu_copy::create(r.device.Get(), m.savedVertices, nullptr).As(&m.vertices), "Create an original mesh again");
            okay(gpu_copy::create(r.device.Get(), m.savedIndices, nullptr).As(&m.indices), "Create an original mesh again"); }
        for (auto& [id, t] : r.textures) { (void)id;
            (void)gpu_copy::create(r.device.Get(), t.saved, &t.view);
            okay(r.device->CreateSamplerState(&t.samplerDesc, &t.sampler), "Create an original sampler again"); }
        for (auto& [id, p] : r.pipelines) { (void)id;
            okay(r.device->CreateVertexShader(p.vertexCode.data(), p.vertexCode.size(), nullptr, &p.vs), "Load original vertex bytecode again");
            okay(r.device->CreatePixelShader(p.fragmentCode.data(), p.fragmentCode.size(), nullptr, &p.ps), "Load original fragment bytecode again");
            if (!p.elements.empty()) okay(r.device->CreateInputLayout(p.elements.data(), static_cast<UINT>(p.elements.size()), p.vertexCode.data(), p.vertexCode.size(), &p.layout),
                "Bind original vertex attributes again");
            okay(r.device->CreateBlendState(&p.blendDesc, &p.blend), "Create an original blend state again");
            okay(r.device->CreateDepthStencilState(&p.depthDesc, &p.depth), "Create an original depth/stencil state again");
            okay(r.device->CreateRasterizerState(&p.rasterDesc, &p.raster), "Create an original raster state again"); }
        for (auto& [id, u] : r.uniforms) { (void)id; u.buffer = buffer(r.device.Get(), D3D11_BIND_CONSTANT_BUFFER, u.bytes.data(), u.bytes.size(), true); }
    } catch (...) {
        // Still suspended: the copies stay for the next attempt.
        r.releaseObjects(); r.context.Reset(); r.device.Reset(); throw;
    }
    for (auto& [id, m] : r.meshes) { (void)id; m.savedVertices = {}; m.savedIndices = {}; }
    for (auto& [id, t] : r.textures) { (void)id; t.saved = {}; }
    r.suspended = false; r.suspendedBytes = 0;
}
bool SourceGraphics::suspended() const noexcept { return impl_->suspended; }
std::size_t SourceGraphics::suspendedBytes() const noexcept { return impl_->suspendedBytes; }
void SourceGraphics::clear() {
    auto& r = *impl_; r.thread(); if (r.context) r.context->ClearState(); r.draws.clear(); r.suspendedBytes = 0; r.meshes.clear(); r.textures.clear(); r.pipelines.clear(); r.uniforms.clear();
    r.colorView.Reset(); r.depthView.Reset(); r.color.Reset(); r.depth.Reset(); r.width = r.height = 0; r.counters.payloadBytes = 0;
}
SourceGraphicsStats SourceGraphics::stats() const {
    auto result = impl_->counters; result.meshes = impl_->meshes.size(); result.textures = impl_->textures.size();
    result.pipelines = impl_->pipelines.size(); result.uniforms = impl_->uniforms.size(); result.draws = impl_->draws.size(); return result;
}
} // namespace endfield::native
