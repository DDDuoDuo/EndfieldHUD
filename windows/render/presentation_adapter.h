#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <string>

namespace ehud::render {

// The source attachment stores encoded linear-premultiplied RGB in an sRGB
// texture. DirectComposition's UNORM surface needs encoded-space premultiplied
// RGB. This one final GPU pass changes that representation after source blending.
// Source attachment clipping and source HDR/composite behavior stay upstream.
// Authored additive RGB at alpha0 is retained as encoded energy. Matching its
// blend with the real desktop/backdrop remains a separate visual acceptance.
class SourcePresentationAdapter final {
public:
    SourcePresentationAdapter()=default;
    SourcePresentationAdapter(const SourcePresentationAdapter&)=delete;
    SourcePresentationAdapter& operator=(const SourcePresentationAdapter&)=delete;
    HRESULT initialize(ID3D11Device* device);
    HRESULT draw(ID3D11RenderTargetView* encodedTarget,
                 ID3D11ShaderResourceView* sourceSrgbView,
                 unsigned width,unsigned height);
    void reset();
    const std::string& initializationError() const { return initializationError_; }

private:
    template<class T> using ComPtr=Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VertexShader> vertex_;
    ComPtr<ID3D11PixelShader> pixel_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11DepthStencilState> depth_;
    ComPtr<ID3D11RasterizerState> rasterizer_;
    std::string initializationError_;
};
} // namespace ehud::render
