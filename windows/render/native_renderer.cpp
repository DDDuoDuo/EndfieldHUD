#include "native_renderer.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace ehud::render {
HRESULT NativeRenderer::initialize(HWND owner, unsigned width, unsigned height) {
    owner_ = owner;
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device_, &level, &context_);
    if (FAILED(hr)) return hr; // A software fallback is not a hardware parity result.
    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(hr = device_.As(&dxgi))) return hr;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(hr = dxgi->GetAdapter(&adapter))) return hr;
    ComPtr<IDXGIFactory2> dxgiFactory;
    if (FAILED(hr = adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)))) return hr;
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width; description.Height = height;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(hr = dxgiFactory->CreateSwapChainForComposition(device_.Get(), &description, nullptr, &swapchain_))) return hr;
    if (FAILED(hr = DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&compositor_)))) return hr;
    if (FAILED(hr = compositor_->CreateTargetForHwnd(owner, TRUE, &target_))) return hr;
    if (FAILED(hr = compositor_->CreateVisual(&visual_))) return hr;
    if (FAILED(hr = visual_->SetContent(swapchain_.Get()))) return hr;
    if (FAILED(hr = target_->SetRoot(visual_.Get()))) return hr;
    if (FAILED(hr = compositor_->Commit())) return hr;
    D2D1_FACTORY_OPTIONS options{};
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, factory_.GetAddressOf()))) return hr;
    if (FAILED(hr = factory_->CreateDevice(dxgi.Get(), &d2d_))) return hr;
    if (FAILED(hr = d2d_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &painter_))) return hr;
    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(text_.GetAddressOf())))) return hr;
    if (FAILED(hr = text_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14, L"en-US", &textFormat_))) return hr;
    if (FAILED(hr = painter_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush_))) return hr;
    return bindSurface();
}
HRESULT NativeRenderer::bindSurface() {
    ComPtr<IDXGISurface> buffer;
    HRESULT hr = swapchain_->GetBuffer(0, IID_PPV_ARGS(&buffer));
    if (FAILED(hr)) return hr;
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(hr = painter_->CreateBitmapFromDxgiSurface(buffer.Get(), &properties, &surface_))) return hr;
    painter_->SetTarget(surface_.Get());
    return S_OK;
}
HRESULT NativeRenderer::resize(unsigned width, unsigned height) {
    if (!swapchain_ || !width || !height) return S_OK;
    painter_->SetTarget(nullptr); surface_.Reset(); context_->ClearState();
    HRESULT hr = swapchain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    return FAILED(hr) ? hr : bindSurface();
}
endfield::platform::ProjectiveMapping NativeRenderer::editorProjection(const scene::Frame& frame) {
    const auto matrix = frame.camera.viewProjection * frame.worldRoot;
    const auto& m = matrix.values;
    const double cx = m[12] - 300 * m[0] + 170 * m[4];
    const double cy = m[13] - 300 * m[1] + 170 * m[5];
    const double cw = m[15] - 300 * m[3] + 170 * m[7];
    const double width = frame.camera.viewport.x / 2, height = frame.camera.viewport.y / 2;
    return {{width * (m[0] + m[3]), width * (-m[4] - m[7]), width * (cx + cw),
        height * (m[3] - m[1]), height * (m[5] - m[7]), height * (cw - cy), m[3], -m[7], cw}};
}
HRESULT NativeRenderer::draw(const scene::Frame& frame, endfield::platform::ProjectedEditor* editor) {
    if (!painter_) return E_UNEXPECTED;
    if (editor) {
        editor->set_projection(editorProjection(frame));
        if (!editorPlane_) {
            HRESULT hr = painter_->CreateCompatibleRenderTarget(D2D1::SizeF(600, 220), &editorPlane_);
            if (FAILED(hr)) return hr;
        }
        const auto& model = editor->model();
        if (!editorBitmap_ || editorRevision_ != model.revision() || editorAnchor_ != model.anchor() ||
            editorCaret_ != model.caret() || editorFocused_ != editor->focused()) {
            editorPlane_->BeginDraw(); editorPlane_->Clear(D2D1::ColorF(.035f, .055f, .065f, .9f));
            editor->draw(editorPlane_.Get());
            HRESULT hr = editorPlane_->EndDraw(); if (FAILED(hr)) return hr;
            editorBitmap_.Reset(); if (FAILED(hr = editorPlane_->GetBitmap(&editorBitmap_))) return hr;
            editorRevision_ = model.revision(); editorAnchor_ = model.anchor(); editorCaret_ = model.caret();
            editorFocused_ = editor->focused();
        }
    }
    painter_->BeginDraw(); painter_->SetTransform(D2D1::Matrix3x2F::Identity());
    painter_->Clear(D2D1::ColorF(0, 0, 0, 0));
    // Geometry diagnostic only. Source meshes/materials/textures must be ported
    // before this becomes the app renderer; never classify this as visual parity.
    std::size_t count = 0;
    for (const auto& graphic : frame.graphics) {
        if (++count > 8192) break;
        const float alpha = static_cast<float>(std::clamp(graphic.color[3], 0.0, 1.0));
        if (alpha <= 0) continue;
        brush_->SetColor(D2D1::ColorF(static_cast<float>(graphic.color[0]),
            static_cast<float>(graphic.color[1]), static_cast<float>(graphic.color[2]), alpha * .45f));
        for (const auto& quad : graphic.quads) {
            std::array<D2D1_POINT_2F, 4> corners;
            bool valid = true;
            for (unsigned i = 0; i < 4; ++i) {
                auto projected = frame.camera.project(quad[i], graphic.world);
                if (!projected) { valid = false; break; }
                corners[i] = D2D1::Point2F(static_cast<float>(projected->x), static_cast<float>(projected->y));
            }
            if (!valid) continue;
            // Outline diagnostics reuse retained device resources. Avoid a new
            // COM geometry allocation per quad on every pointer presentation.
            for (unsigned i = 0; i < 4; ++i)
                painter_->DrawLine(corners[i], corners[(i + 1) % 4], brush_.Get(), .6f);
        }
    }
    brush_->SetColor(D2D1::ColorF(1, .82f, .2f, 1));
    const wchar_t label[] = L"ENDFIELDHUD | Windows feasibility geometry probe | Not release-ready";
    painter_->DrawText(label, static_cast<UINT32>(std::size(label) - 1), textFormat_.Get(),
        D2D1::RectF(24, 20, static_cast<float>(frame.camera.viewport.x - 24), 50), brush_.Get());
    if (editor && editorBitmap_) {
        const auto& h = editorProjection(frame).values;
        const D2D1_MATRIX_4X4_F perspective{
            static_cast<float>(h[0]), static_cast<float>(h[3]), 0, static_cast<float>(h[6]),
            static_cast<float>(h[1]), static_cast<float>(h[4]), 0, static_cast<float>(h[7]),
            0, 0, 1, 0,
            static_cast<float>(h[2]), static_cast<float>(h[5]), 0, static_cast<float>(h[8])};
        painter_->DrawBitmap(editorBitmap_.Get(), nullptr, 1, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, &perspective);
    }
    HRESULT hr = painter_->EndDraw();
    if (FAILED(hr)) return hr;
    hr = swapchain_->Present(1, 0);
    if (SUCCEEDED(hr)) ++submitted_;
    return hr;
}
void NativeRenderer::reset() {
    if (painter_) painter_->SetTarget(nullptr);
    editorBitmap_.Reset(); editorPlane_.Reset(); editorRevision_ = UINT64_MAX;
    surface_.Reset(); brush_.Reset(); textFormat_.Reset(); text_.Reset(); painter_.Reset();
    d2d_.Reset(); factory_.Reset(); visual_.Reset(); target_.Reset(); compositor_.Reset();
    swapchain_.Reset(); context_.Reset(); device_.Reset(); owner_ = nullptr;
}
}
