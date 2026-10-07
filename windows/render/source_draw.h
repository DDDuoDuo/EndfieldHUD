#pragma once
#include <d3d11.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <filesystem>
#include <memory>
#include <string>
#include "scene/watch_scene.hpp"
namespace ehud::render {
// Native source sprite and selected UI/mesh FX programs. Complete source
// soft-mask/stencil/HDR parity still requires matched visual acceptance.
class SourceDraw final {
public:
    SourceDraw();
    ~SourceDraw();
    HRESULT initialize(ID3D11Device*, ID3D11DeviceContext*, ID2D1DeviceContext*, IDWriteFactory*, const std::filesystem::path&);
    HRESULT draw(ID3D11RenderTargetView*, const scene::Frame&);
    std::size_t textureCount() const;
    std::size_t textCount() const;
    const std::string& initializationError() const;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
}
