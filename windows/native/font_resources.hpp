#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

struct IDWriteFactory;
struct IDWriteFontCollection;
namespace endfield::native {
class LayerRasterizer;
// Immutable configuration-event snapshot of the application's existing font
// resources. COM references are owned, so layouts on the borrowed MTA worker
// may outlive the UI rasterizer. No loader, font installation, HWND or raster
// operation belongs to this value. Do not call the UI rasterizer on that worker.
class LayerFontResources final {
public:
    LayerFontResources()=default;
    explicit operator bool()const noexcept;
    IDWriteFactory*factory()const noexcept;
    IDWriteFontCollection*systemCollection()const noexcept;
    IDWriteFontCollection*bundledCollection()const noexcept;
    std::string_view fallbackFamily()const noexcept;
    std::string_view locale()const noexcept;
    std::uint64_t revision()const noexcept;
private:
    struct Impl;std::shared_ptr<const Impl>impl_;
    LayerFontResources(IDWriteFactory*,IDWriteFontCollection*,IDWriteFontCollection*,
        std::string family,std::string locale,std::uint64_t revision);
    friend class LayerRasterizer;
};
} // namespace endfield::native
