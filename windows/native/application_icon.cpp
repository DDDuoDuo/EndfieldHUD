#include "native/application_icon.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <system_error>
#include <utility>
#include <vector>
namespace endfield::native {namespace {
using Microsoft::WRL::ComPtr;
void need(bool v,const char*why){if(!v)throw std::invalid_argument(why);}void checked(HRESULT r,const char*why){if(FAILED(r))throw std::system_error(static_cast<int>(r),std::system_category(),why);}
struct Bitmap{HBITMAP handle{};~Bitmap(){if(handle)DeleteObject(handle);}};
}
ApplicationIcon::~ApplicationIcon(){if(handle_)DestroyIcon(static_cast<HICON>(handle_));}
ApplicationIcon::ApplicationIcon(ApplicationIcon&&other)noexcept:handle_(std::exchange(other.handle_,nullptr)){}
ApplicationIcon&ApplicationIcon::operator=(ApplicationIcon&&other)noexcept{if(this!=&other){if(handle_)DestroyIcon(static_cast<HICON>(handle_));handle_=std::exchange(other.handle_,nullptr);}return *this;}
ApplicationIcon ApplicationIcon::fromPixels(unsigned w,unsigned h,std::span<const std::uint8_t>pixels){
    need(w&&h&&w<=256&&h<=256&&pixels.size()==static_cast<std::size_t>(w)*h*4,"Invalid bounded icon pixels");
    for(std::size_t i=0;i<pixels.size();i+=4)need(pixels[i]<=pixels[i+3]&&pixels[i+1]<=pixels[i+3]&&pixels[i+2]<=pixels[i+3],"Icon pixels must be premultiplied");
    BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=static_cast<LONG>(w);bitmap.bmiHeader.biHeight=-static_cast<LONG>(h);bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
    void*bits{};Bitmap color{CreateDIBSection(nullptr,&bitmap,DIB_RGB_COLORS,&bits,nullptr,0)};need(color.handle&&bits,"Cannot create icon bitmap");std::memcpy(bits,pixels.data(),pixels.size());
    std::vector<std::uint8_t>maskBytes(((w+15)/16)*2*h,0);Bitmap mask{CreateBitmap(static_cast<int>(w),static_cast<int>(h),1,1,maskBytes.data())};need(mask.handle!=nullptr,"Cannot create icon alpha mask");
    ICONINFO info{};info.fIcon=TRUE;info.hbmColor=color.handle;info.hbmMask=mask.handle;const auto icon=CreateIconIndirect(&info);need(icon!=nullptr,"Cannot create application-local icon");return ApplicationIcon(icon);
}
ApplicationIcon ApplicationIcon::fromPNG(const std::filesystem::path&path,std::string_view hash,unsigned size,IconInk ink){
    need(size>=16&&size<=256&&hash.size()==64&&std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),"Invalid icon size or source pin");
    need(ink==IconInk::original||ink==IconInk::blackTemplate||ink==IconInk::whiteTemplate,"Invalid icon template treatment");
    const auto bytes=ehud::data::detail::readFile(path,1024*1024);need(bytes&&!bytes->empty(),"Missing original icon PNG");
    const auto span=std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size());need(core::packet::sha256(span)==hash,"Icon differs from authoritative rendered asset");
    ComPtr<IWICImagingFactory>factory;checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create bounded icon decoder");
    ComPtr<IWICStream>stream;checked(factory->CreateStream(&stream),"Create icon stream");checked(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(bytes->data())),static_cast<DWORD>(bytes->size())),"Read owned icon bytes");
    ComPtr<IWICBitmapDecoder>decoder;checked(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"Decode icon PNG");
    GUID container{};checked(decoder->GetContainerFormat(&container),"Read icon container");need(container==GUID_ContainerFormatPng,"Icon must use the pinned PNG format");UINT count{};checked(decoder->GetFrameCount(&count),"Read icon frames");need(count==1,"Icon cannot contain animation");
    ComPtr<IWICBitmapFrameDecode>frame;checked(decoder->GetFrame(0,&frame),"Decode original icon frame");UINT width{},height{};checked(frame->GetSize(&width,&height),"Read original icon size");need(width==height&&width>=16&&width<=256,"Source-rendered icon canvas must remain a small square");
    ComPtr<IWICBitmapScaler>scaler;ComPtr<IWICBitmapSource>source;checked(frame.As(&source),"Read icon source");if(width!=size){checked(factory->CreateBitmapScaler(&scaler),"Create bounded icon scaler");checked(scaler->Initialize(source.Get(),size,size,WICBitmapInterpolationModeFant),"Scale complete source icon canvas");checked(scaler.As(&source),"Read scaled icon");}
    ComPtr<IWICFormatConverter>converter;checked(factory->CreateFormatConverter(&converter),"Create icon pixel converter");checked(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Convert icon to native premultiplied pixels");
    std::vector<std::uint8_t>pixels(static_cast<std::size_t>(size)*size*4);checked(converter->CopyPixels(nullptr,size*4,static_cast<UINT>(pixels.size()),pixels.data()),"Read bounded icon pixels");
    if(ink!=IconInk::original)for(std::size_t n=0;n<pixels.size();n+=4){const auto value=ink==IconInk::whiteTemplate?pixels[n+3]:std::uint8_t{};pixels[n]=pixels[n+1]=pixels[n+2]=value;}
    return fromPixels(size,size,pixels);
}
}
#endif
