#include "native/font_resources.hpp"
#ifdef _WIN32
#include <dwrite.h>
#include <wrl/client.h>
#include <stdexcept>
#include <utility>
namespace endfield::native {
struct LayerFontResources::Impl {
    Microsoft::WRL::ComPtr<IDWriteFactory>factory;
    Microsoft::WRL::ComPtr<IDWriteFontCollection>system,bundled;
    std::string family,locale;std::uint64_t revision{};
};
LayerFontResources::LayerFontResources(IDWriteFactory*factory,IDWriteFontCollection*system,
    IDWriteFontCollection*bundled,std::string family,std::string locale,std::uint64_t revision){
    if(!factory||!system||!bundled||family.empty()||locale.empty()||!revision)
        throw std::invalid_argument("Incomplete retained font resources");
    auto value=std::make_shared<Impl>();value->factory=factory;value->system=system;
    value->bundled=bundled;value->family=std::move(family);value->locale=std::move(locale);
    value->revision=revision;impl_=std::move(value);
}
LayerFontResources::operator bool()const noexcept{return bool(impl_);}
IDWriteFactory*LayerFontResources::factory()const noexcept{return impl_?impl_->factory.Get():nullptr;}
IDWriteFontCollection*LayerFontResources::systemCollection()const noexcept{return impl_?impl_->system.Get():nullptr;}
IDWriteFontCollection*LayerFontResources::bundledCollection()const noexcept{return impl_?impl_->bundled.Get():nullptr;}
std::string_view LayerFontResources::fallbackFamily()const noexcept{return impl_?impl_->family:std::string_view{};}
std::string_view LayerFontResources::locale()const noexcept{return impl_?impl_->locale:std::string_view{};}
std::uint64_t LayerFontResources::revision()const noexcept{return impl_?impl_->revision:0;}
} // namespace endfield::native
#endif
