#include "native/font_resources.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite_2.h>
#include <wrl/client.h>
#include <cwchar>
#include <stdexcept>
#include <utility>
#include <vector>
namespace endfield::native {
core::TextRegion preferredSystemRegion(){
    ULONG count{},size{};std::vector<wchar_t> buffer;std::vector<std::string> names;
    if(GetUserPreferredUILanguages(MUI_LANGUAGE_NAME,&count,nullptr,&size)&&size){buffer.resize(size);
        if(GetUserPreferredUILanguages(MUI_LANGUAGE_NAME,&count,buffer.data(),&size))for(const wchar_t*at=buffer.data();*at;at+=std::wcslen(at)+1){
            const int n=WideCharToMultiByte(CP_UTF8,0,at,-1,nullptr,0,nullptr,nullptr);if(n<=1)continue;
            std::string name(static_cast<std::size_t>(n-1),'\0');WideCharToMultiByte(CP_UTF8,0,at,-1,name.data(),n,nullptr,nullptr);names.push_back(std::move(name));}}
    const std::vector<std::string_view> preferred(names.begin(),names.end());return core::firstRegion(preferred);
}
struct LayerFontResources::Impl {
    Microsoft::WRL::ComPtr<IDWriteFactory>factory;
    Microsoft::WRL::ComPtr<IDWriteFontCollection>system,bundled;
    Microsoft::WRL::ComPtr<IDWriteFontFallback>fallback;
    std::string family,locale;std::uint64_t revision{};
};
LayerFontResources::LayerFontResources(IDWriteFactory*factory,IDWriteFontCollection*system,
    IDWriteFontCollection*bundled,IDWriteFontFallback*fallback,std::string family,std::string locale,std::uint64_t revision){
    if(!factory||!system||!bundled||!fallback||family.empty()||locale.empty()||!revision)
        throw std::invalid_argument("Incomplete retained font resources");
    auto value=std::make_shared<Impl>();value->factory=factory;value->system=system;
    value->bundled=bundled;value->fallback=fallback;value->family=std::move(family);value->locale=std::move(locale);
    value->revision=revision;impl_=std::move(value);
}
LayerFontResources::operator bool()const noexcept{return bool(impl_);}
IDWriteFactory*LayerFontResources::factory()const noexcept{return impl_?impl_->factory.Get():nullptr;}
IDWriteFontCollection*LayerFontResources::systemCollection()const noexcept{return impl_?impl_->system.Get():nullptr;}
IDWriteFontCollection*LayerFontResources::bundledCollection()const noexcept{return impl_?impl_->bundled.Get():nullptr;}
IDWriteFontFallback*LayerFontResources::fontFallback()const noexcept{return impl_?impl_->fallback.Get():nullptr;}
long LayerFontResources::applyFontFallback(IDWriteTextFormat*format)const noexcept{
    if(!impl_||!format)return E_INVALIDARG;Microsoft::WRL::ComPtr<IDWriteTextFormat1>advanced;
    const auto hr=format->QueryInterface(IID_PPV_ARGS(advanced.GetAddressOf()));return FAILED(hr)?hr:advanced->SetFontFallback(impl_->fallback.Get());}
std::string_view LayerFontResources::fallbackFamily()const noexcept{return impl_?impl_->family:std::string_view{};}
std::string_view LayerFontResources::locale()const noexcept{return impl_?impl_->locale:std::string_view{};}
std::uint64_t LayerFontResources::revision()const noexcept{return impl_?impl_->revision:0;}
} // namespace endfield::native
#endif
