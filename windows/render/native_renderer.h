#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include "scene/watch_scene.hpp"
#include "platform/projected_editor.h"
#include <cstdint>

namespace ehud::render {
class NativeRenderer final {
public:
    HRESULT initialize(HWND owner, unsigned width, unsigned height);
    HRESULT resize(unsigned width, unsigned height);
    HRESULT draw(const scene::Frame& frame, endfield::platform::ProjectedEditor* editor = nullptr);
    IDWriteFactory* textFactory() const { return text_.Get(); }
    ID3D11Device* graphicsDevice() const { return device_.Get(); }
    static endfield::platform::ProjectiveMapping editorProjection(const scene::Frame& frame);
    std::uint64_t submittedFrames() const { return submitted_; }
    void reset();
private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    HRESULT bindSurface();
    HWND owner_{};
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swapchain_;
    ComPtr<IDCompositionDevice> compositor_;
    ComPtr<IDCompositionTarget> target_;
    ComPtr<IDCompositionVisual> visual_;
    ComPtr<ID2D1Factory1> factory_;
    ComPtr<ID2D1Device> d2d_;
    ComPtr<ID2D1DeviceContext> painter_;
    ComPtr<ID2D1Bitmap1> surface_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteFactory> text_;
    ComPtr<IDWriteTextFormat> textFormat_;
    ComPtr<ID2D1BitmapRenderTarget> editorPlane_;
    ComPtr<ID2D1Bitmap> editorBitmap_;
    std::uint64_t editorRevision_{UINT64_MAX};
    std::size_t editorAnchor_{SIZE_MAX}, editorCaret_{SIZE_MAX};
    bool editorFocused_{};
    std::uint64_t submitted_{};
};
}
