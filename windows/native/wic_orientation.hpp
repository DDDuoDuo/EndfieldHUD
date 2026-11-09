#pragma once
// EXIF orientation for WIC image sources: one mapping and one way to turn a
// source upright, shared by the Reader, Clipboard, Now Playing and avatar
// decoders (Mac: ImageIO applies the orientation while it decodes).
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace endfield::native {
// EXIF/TIFF orientation 1...8 as the WIC flip-rotate transform; anything else
// is Rotate0. WIC flips before it rotates, so the mirrored-and-turned values
// 5 and 7 pair FlipHorizontal with Rotate270 and Rotate90. The Now Playing
// image check pins all eight against ImageIO, pixel for pixel.
constexpr WICBitmapTransformOptions wicOrientationTransform(unsigned orientation) noexcept {
    switch (orientation) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;
    }
}
// Turns `source` (the decoded frame, or a scaler or color transform on it,
// already at its final size) upright for `orientation`. A flip-rotator reads
// its source out of row order: straight over a decoder it decodes the image
// again for the rows it turns (a 3000x2000 photo took 14 s). The source is
// decoded once into a bitmap and turned from there. Orientation 1 (or an
// unknown value) hands back `source` itself.
inline HRESULT orientWICSource(IWICImagingFactory* factory, IWICBitmapSource* source, unsigned orientation,
                               IWICBitmapSource** result) noexcept {
    if (!factory || !source || !result) return E_POINTER;
    *result = nullptr;
    const auto transform = wicOrientationTransform(orientation);
    if (transform == WICBitmapTransformRotate0) { source->AddRef(); *result = source; return S_OK; }
    Microsoft::WRL::ComPtr<IWICBitmap> decoded;
    auto hr = factory->CreateBitmapFromSource(source, WICBitmapCacheOnLoad, &decoded);
    if (FAILED(hr)) return hr;
    Microsoft::WRL::ComPtr<IWICBitmapFlipRotator> rotator;
    if (FAILED(hr = factory->CreateBitmapFlipRotator(&rotator))) return hr;
    if (FAILED(hr = rotator->Initialize(decoded.Get(), transform))) return hr;
    *result = rotator.Detach();
    return S_OK;
}
} // namespace endfield::native
#endif
