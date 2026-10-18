#include "native/source_cursor.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
struct Bitmap {HBITMAP value{};~Bitmap(){if(value)DeleteObject(value);}};
void need(bool value,const char*reason){if(!value)throw std::invalid_argument(reason);}
void checked(HRESULT result,const char*reason){if(FAILED(result))throw std::system_error(static_cast<int>(result),std::system_category(),reason);}
void nativeNeed(bool value,const char*reason){if(!value)throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),reason);}
}
SourceCursor::~SourceCursor(){if(handle_)DestroyIcon(static_cast<HICON>(handle_));}
SourceCursor::SourceCursor(SourceCursor&& other)noexcept:handle_(std::exchange(other.handle_,nullptr)){}
SourceCursor& SourceCursor::operator=(SourceCursor&& other)noexcept{
    if(this!=&other){if(handle_)DestroyIcon(static_cast<HICON>(handle_));handle_=std::exchange(other.handle_,nullptr);}return *this;
}
SourceCursor SourceCursor::fromPixels(unsigned width,unsigned height,unsigned x,unsigned y,std::span<const std::uint8_t> bgra){
    need(width&&height&&width<=256&&height<=256&&x<width&&y<height&&bgra.size()==std::size_t(width)*height*4,"Invalid source cursor dimensions or hotspot");
    for(std::size_t i=0;i<bgra.size();i+=4)need(bgra[i]<=bgra[i+3]&&bgra[i+1]<=bgra[i+3]&&bgra[i+2]<=bgra[i+3],"Cursor pixels must be premultiplied");
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=LONG(width);info.bmiHeader.biHeight=-LONG(height);
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels{};Bitmap color{CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0)};
    nativeNeed(color.value&&pixels,"Create source cursor color bitmap");std::memcpy(pixels,bgra.data(),bgra.size());
    // Alpha is authoritative on current Windows; the zero AND mask preserves
    // the original transparent outline instead of adding a monochrome shape.
    const auto maskStride=((width+15u)/16u)*2u;std::vector<std::uint8_t> zeros(std::size_t(maskStride)*height);
    Bitmap mask{CreateBitmap(int(width),int(height),1,1,zeros.data())};nativeNeed(mask.value!=nullptr,"Create source cursor mask");
    ICONINFO cursor{};cursor.fIcon=FALSE;cursor.xHotspot=x;cursor.yHotspot=y;cursor.hbmColor=color.value;cursor.hbmMask=mask.value;
    const auto handle=CreateIconIndirect(&cursor);nativeNeed(handle!=nullptr,"Create original source cursor");return SourceCursor(handle);
}
SourceCursor SourceCursor::fromOriginalPNG(const std::filesystem::path&path){
    auto bytes=ehud::data::detail::readFile(path,1024*1024);need(bytes.has_value(),"Original source cursor PNG is missing");
    const auto raw=std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size());
    need(core::packet::sha256(raw)=="d479751c9298b9ec582dcaddb393e4f4e92c7e66511a6db19ed2e666ac9b8789","Original cursor PNG differs from pinned Mac resource");
    ComPtr<IWICImagingFactory> factory;checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create cursor image decoder");
    ComPtr<IWICStream> stream;checked(factory->CreateStream(&stream),"Create cursor image stream");
    checked(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(bytes->data()),DWORD(bytes->size())),"Read verified cursor image");
    ComPtr<IWICBitmapDecoder> decoder;checked(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"Decode source cursor PNG");
    ComPtr<IWICBitmapFrameDecode> frame;checked(decoder->GetFrame(0,&frame),"Read source cursor pixels");
    UINT width{},height{};checked(frame->GetSize(&width,&height),"Read source cursor dimensions");need(width==58&&height==58,"Pinned cursor extent changed");
    ComPtr<IWICFormatConverter> converted;checked(factory->CreateFormatConverter(&converted),"Create cursor pixel converter");
    checked(converted->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Premultiply original cursor pixels");
    std::vector<std::uint8_t> pixels(std::size_t(width)*height*4);checked(converted->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data()),"Copy source cursor pixels");
    return fromPixels(width,height,0,0,pixels);
}
} // namespace endfield::native
