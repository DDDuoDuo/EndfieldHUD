#pragma once
// Internal to the native renderer (renderer.cpp, source_graphics.cpp): system
// memory copies of D3D11 buffers and 2D textures, for a device that is
// released while nothing is drawn (Renderer::suspendDevice) and created again
// later. A copy keeps the resource's description, its shader resource view's
// description and the bytes of every subresource; creating it again on another
// device gives the same resource and view. Copies are staged in batches: every
// GPU copy of a batch is issued first, so the first map waits for the GPU once.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "native/renderer.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace endfield::native::gpu_copy {
using Microsoft::WRL::ComPtr;
struct Copy {
    bool texture{};
    D3D11_TEXTURE2D_DESC textureDesc{};
    D3D11_BUFFER_DESC bufferDesc{};
    bool hasView{};
    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    std::vector<std::vector<std::uint8_t>> data; // one per subresource
    std::vector<UINT> pitch;                     // row pitch per subresource
    ComPtr<ID3D11Resource> staging;              // between stage() and finish()
    std::size_t bytes() const noexcept { std::size_t n{}; for (const auto& d : data) n += d.size(); return n; }
};
inline void succeed(HRESULT result, const char* operation) {
    if (FAILED(result)) throw RendererError(operation, static_cast<std::int32_t>(result));
}
inline bool blockCompressed(DXGI_FORMAT f) noexcept {
    return (f >= DXGI_FORMAT_BC1_TYPELESS && f <= DXGI_FORMAT_BC5_SNORM) || (f >= DXGI_FORMAT_BC6H_TYPELESS && f <= DXGI_FORMAT_BC7_UNORM_SRGB);
}
inline UINT rows(DXGI_FORMAT f, UINT height) noexcept { return blockCompressed(f) ? (height + 3) / 4 : height; }
// The GPU copy of `resource` into a new staging resource (no wait).
inline Copy stage(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Resource* resource, ID3D11ShaderResourceView* view) {
    Copy copy;
    D3D11_RESOURCE_DIMENSION dimension{};
    resource->GetType(&dimension);
    if (view) { view->GetDesc(&copy.viewDesc); copy.hasView = true; }
    if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        ComPtr<ID3D11Texture2D> texture; succeed(resource->QueryInterface(IID_PPV_ARGS(&texture)), "Read a retained texture");
        texture->GetDesc(&copy.textureDesc); copy.texture = true;
        auto staging = copy.textureDesc;
        staging.Usage = D3D11_USAGE_STAGING; staging.BindFlags = 0; staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ; staging.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> target; succeed(device->CreateTexture2D(&staging, nullptr, &target), "Stage a retained texture copy");
        context->CopyResource(target.Get(), resource); copy.staging = target;
    } else if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
        ComPtr<ID3D11Buffer> buffer; succeed(resource->QueryInterface(IID_PPV_ARGS(&buffer)), "Read a retained buffer");
        buffer->GetDesc(&copy.bufferDesc);
        auto staging = copy.bufferDesc;
        staging.Usage = D3D11_USAGE_STAGING; staging.BindFlags = 0; staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ; staging.MiscFlags = 0; staging.StructureByteStride = 0;
        ComPtr<ID3D11Buffer> target; succeed(device->CreateBuffer(&staging, nullptr, &target), "Stage a retained buffer copy");
        context->CopyResource(target.Get(), resource); copy.staging = target;
    } else throw std::invalid_argument("Only buffers and 2D textures are copied");
    return copy;
}
// Maps the staged copy into system memory and drops the staging resource.
inline void finish(ID3D11DeviceContext* context, Copy& copy) {
    if (!copy.staging) return;
    const UINT subresources = copy.texture ? copy.textureDesc.MipLevels * copy.textureDesc.ArraySize : 1;
    copy.data.resize(subresources); copy.pitch.resize(subresources);
    for (UINT n = 0; n < subresources; ++n) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        succeed(context->Map(copy.staging.Get(), n, D3D11_MAP_READ, 0, &mapped), "Read a staged copy");
        std::size_t size;
        if (copy.texture) {
            const UINT mip = n % copy.textureDesc.MipLevels;
            const UINT height = std::max(1u, copy.textureDesc.Height >> mip);
            size = std::size_t(mapped.RowPitch) * rows(copy.textureDesc.Format, height);
            copy.pitch[n] = mapped.RowPitch;
        } else { size = copy.bufferDesc.ByteWidth; copy.pitch[n] = copy.bufferDesc.ByteWidth; }
        try { copy.data[n].assign(static_cast<const std::uint8_t*>(mapped.pData), static_cast<const std::uint8_t*>(mapped.pData) + size); }
        catch (...) { context->Unmap(copy.staging.Get(), n); throw; }
        context->Unmap(copy.staging.Get(), n);
    }
    copy.staging.Reset();
}
// The resource again on `device`, with its view when the copy had one.
inline ComPtr<ID3D11Resource> create(ID3D11Device* device, const Copy& copy, ComPtr<ID3D11ShaderResourceView>* view) {
    std::vector<D3D11_SUBRESOURCE_DATA> initial(copy.data.size());
    for (std::size_t n = 0; n < copy.data.size(); ++n) initial[n] = {copy.data[n].data(), copy.pitch[n], static_cast<UINT>(copy.data[n].size())};
    ComPtr<ID3D11Resource> result;
    if (copy.texture) {
        ComPtr<ID3D11Texture2D> texture; succeed(device->CreateTexture2D(&copy.textureDesc, initial.data(), &texture), "Create a retained texture again");
        result = texture;
    } else {
        ComPtr<ID3D11Buffer> buffer; succeed(device->CreateBuffer(&copy.bufferDesc, initial.data(), &buffer), "Create a retained buffer again");
        result = buffer;
    }
    if (view && copy.hasView) succeed(device->CreateShaderResourceView(result.Get(), &copy.viewDesc, view->ReleaseAndGetAddressOf()), "Create a retained view again");
    return result;
}
// The resource behind a shader resource view.
inline ComPtr<ID3D11Resource> resourceOf(ID3D11ShaderResourceView* view) {
    ComPtr<ID3D11Resource> resource; view->GetResource(&resource); return resource;
}
} // namespace endfield::native::gpu_copy
