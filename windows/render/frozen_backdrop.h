#pragma once

#include "platform/frozen_desktop_snapshot.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

namespace ehud::render {
struct FrozenBackdropStyle {
    bool dark{true},low_power{};
    double darkness{.63},blur_amount{.75};
};

// Pre-opening SDR snapshot fallback for the Mac desktop's native behind-window
// NSVisualEffectView(.hudWindow). Its private blur kernel/material is unknown:
// this explicit Windows prototype uses a Gaussian at quarter resolution, with
// sigma12 physical pixels. Source tint/vignette settings and layer envelopes
// retain their source contracts; matching their raster/composition is unverified.
class FrozenBackdrop final {
public:
    HRESULT initialize(ID3D11Device* device,ID3D11DeviceContext* context);
    HRESULT prepare(endfield::platform::FrozenSnapshot snapshot);
    // Writes a fixed-screen linear-premultiplied underlay to an SRGB target.
    // Without a prepared snapshot, writes source tint/vignette alone. Caller
    // draws source geometry with target preservation, then presentation adapter.
    HRESULT draw(ID3D11RenderTargetView* sourceTarget,unsigned width,unsigned height,
                 double alpha,const FrozenBackdropStyle& style={});
    void clear();
    bool ready() const noexcept {return snapshot_&&blurView_;}
    std::size_t texture_count() const noexcept {return static_cast<bool>(raw_)+static_cast<bool>(temporary_)+static_cast<bool>(blur_);}
    std::size_t snapshot_byte_size() const noexcept {return snapshot_?snapshot_->byte_size():0;}
    std::size_t retained_pixel_bytes() const noexcept;
    const std::string& initializationError() const {return error_;}
private:
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;
    Ptr<ID3D11VertexShader> vertex_;Ptr<ID3D11PixelShader> gaussian_,composite_;
    Ptr<ID3D11SamplerState> sampler_;Ptr<ID3D11BlendState> blend_;
    Ptr<ID3D11RasterizerState> raster_;Ptr<ID3D11DepthStencilState> depth_;
    Ptr<ID3D11Buffer> parameters_,weights_;
    Ptr<ID3D11Texture2D> raw_,temporary_,blur_;
    Ptr<ID3D11ShaderResourceView> rawView_,temporaryView_,blurView_;
    Ptr<ID3D11RenderTargetView> temporaryTarget_,blurTarget_;
    endfield::platform::FrozenSnapshot snapshot_;
    unsigned blurWidth_{},blurHeight_{};
    std::string error_;
    void bind(ID3D11RenderTargetView*,ID3D11PixelShader*,ID3D11ShaderResourceView*,unsigned,unsigned);
    void unbind();
};
} // namespace ehud::render
