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
#include "source_draw.h"
#include "presentation_adapter.h"
#include "frozen_backdrop.h"
#include <cstdint>

namespace ehud::render {
class NativeRenderer final {
public:
    ~NativeRenderer();
    // Hardware remains the production default. WARP is an explicit isolated
    // diagnostic selection, never an automatic rendering fallback.
    HRESULT initialize(HWND owner, unsigned width, unsigned height,
                       D3D_DRIVER_TYPE driver = D3D_DRIVER_TYPE_HARDWARE);
    HRESULT resize(unsigned width, unsigned height);
    HRESULT loadSourceAssets(const std::filesystem::path& root);
    HRESULT saveDiagnosticFrame(const std::filesystem::path& path);
    HRESULT waitForDiagnosticGpu();
    HRESULT verifyDiagnosticAlpha();
    HRESULT draw(const scene::Frame& frame, endfield::platform::ProjectedEditor* editor = nullptr);
    HRESULT setFrozenBackdrop(endfield::platform::FrozenSnapshot snapshot);
    void enableFrozenBackdrop(bool enabled) { backdropEnabled_=enabled; }
    void clearFrozenBackdrop();
    IDWriteFactory* textFactory() const { return text_.Get(); }
    ID3D11Device* graphicsDevice() const { return device_.Get(); }
    static endfield::platform::ProjectiveMapping editorProjection(const scene::Frame& frame);
    std::uint64_t submittedFrames() const { return submitted_; }
    std::size_t textureCount() const { return source_ ? source_->textureCount() : 0; }
    std::size_t textCount() const { return source_ ? source_->textCount() : 0; }
    std::size_t backdropTextureCount() const { return frozenBackdrop_ ? frozenBackdrop_->texture_count() : 0; }
    std::size_t backdropSnapshotBytes() const { return frozenBackdrop_ ? frozenBackdrop_->snapshot_byte_size() : 0; }
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
    ComPtr<ID3D11RenderTargetView> renderTarget_;
    ComPtr<ID3D11Texture2D> sourceSurface_;
    ComPtr<ID3D11RenderTargetView> sourceTarget_;
    ComPtr<ID3D11ShaderResourceView> sourceView_;
    std::unique_ptr<SourcePresentationAdapter> presentationAdapter_;
    std::unique_ptr<FrozenBackdrop> frozenBackdrop_;
    bool backdropEnabled_{};
    std::shared_ptr<SourceDraw> source_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteFactory> text_;
    ComPtr<IDWriteTextFormat> textFormat_;
    ComPtr<ID2D1BitmapRenderTarget> editorPlane_;
    ComPtr<ID2D1Bitmap> editorBitmap_;
    std::uint64_t editorRevision_{UINT64_MAX};
    std::size_t editorAnchor_{SIZE_MAX}, editorCaret_{SIZE_MAX};
    bool editorFocused_{};
    std::uint64_t editorArtworkRevision_{UINT64_MAX};
    std::uint64_t submitted_{};
};
}
