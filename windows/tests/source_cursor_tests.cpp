#include "native/source_cursor.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>
using endfield::native::SourceCursor;
namespace {
unsigned checks{};
void check(bool value,const char*reason){++checks;if(!value)throw std::runtime_error(reason);}
void inspect(const SourceCursor&cursor,unsigned width,unsigned height,unsigned x,unsigned y){
    ICONINFO info{};check(GetIconInfo(static_cast<HICON>(cursor.handle()),&info)!=FALSE,"Owned cursor is readable");
    BITMAP pixels{};const auto got=GetObjectW(info.hbmColor,sizeof(pixels),&pixels);DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);
    check(!info.fIcon&&info.xHotspot==x&&info.yHotspot==y,"Cursor kind and original hotspot preserved");
    check(got&&pixels.bmWidth==LONG(width)&&pixels.bmHeight==LONG(height)&&pixels.bmBitsPixel==32,"Cursor preserves backing-pixel extent and alpha bitmap");
}
void run(int argc,wchar_t**argv){
    const auto previous=GetCursor();
    const std::array<std::uint8_t,16>pixels{0,0,0,0, 0,0,255,255, 0,128,0,128, 64,0,0,64};
    {
        auto cursor=SourceCursor::fromPixels(2,2,1,0,pixels);inspect(cursor,2,2,1,0);
        const auto handle=cursor.handle();auto moved=std::move(cursor);check(!cursor.handle()&&moved.handle()==handle,"Move retains exactly one native handle owner");
        SourceCursor assigned;assigned=std::move(moved);check(!moved.handle()&&assigned.handle()==handle,"Move assignment transfers cursor ownership");
        bool rejected=false;try{SourceCursor::fromPixels(2,2,2,0,pixels);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Out-of-image hotspot rejected before creating GDI objects");
        auto invalid=pixels;invalid[0]=1;rejected=false;try{SourceCursor::fromPixels(2,2,0,0,invalid);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Nonpremultiplied transparent pixels rejected");
    }
    // Warm up USER32 before checking repeated private-handle ownership.
    const auto gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for(unsigned i=0;i<128;++i){auto cursor=SourceCursor::fromPixels(2,2,0,0,pixels);}
    check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdi&&GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==user,"Repeated create/destroy leaves no native GDI or cursor handles");
    if(argc==2){auto cursor=SourceCursor::fromOriginalPNG(argv[1]);inspect(cursor,58,58,0,0);}else check(argc==1,"Only an explicit original PNG path is accepted");
    check(GetCursor()==previous,"Cursor asset tests never install or hide the user's cursor");
}
}
int wmain(int argc,wchar_t**argv){const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(com),"Caller initializes COM");run(argc,argv);CoUninitialize();std::cout<<checks<<" source cursor checks passed\n";return 0;}catch(const std::exception&e){if(SUCCEEDED(com))CoUninitialize();std::cerr<<e.what()<<'\n';return 1;}}
