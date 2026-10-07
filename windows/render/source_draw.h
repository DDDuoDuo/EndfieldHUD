#pragma once
#include <d3d11.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <filesystem>
#include <memory>
#include "scene/watch_scene.hpp"
namespace ehud::render {
// Native source sprite path. The remaining FX/soft-mask/HDR programs are
// explicitly outside this class's verified scope.
class SourceDraw final {
public:
    SourceDraw();
    ~SourceDraw();
    HRESULT initialize(ID3D11Device*, ID3D11DeviceContext*, ID2D1DeviceContext*, IDWriteFactory*, const std::filesystem::path&);
    HRESULT draw(ID3D11RenderTargetView*, const scene::Frame&);
    std::size_t textureCount() const;
    std::size_t textCount() const;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
}
