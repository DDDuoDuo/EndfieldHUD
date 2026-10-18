#include "native/application_icon.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
namespace {using namespace endfield::native;using ehud::data::Json;unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}template<class F>void rejects(F f,const char*m){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,m);}
struct IconPixels{unsigned width{},height{};std::vector<std::uint8_t>pixels;};
IconPixels inspect(void*handle){ICONINFO info{};check(GetIconInfo(static_cast<HICON>(handle),&info)!=FALSE,"Native icon is a valid HICON");struct Release{ICONINFO&i;~Release(){if(i.hbmColor)DeleteObject(i.hbmColor);if(i.hbmMask)DeleteObject(i.hbmMask);}}release{info};check(info.fIcon==TRUE,"App artwork is an icon, not a system cursor");BITMAP bitmap{};check(GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap)==sizeof(bitmap),"Read owned color bitmap");IconPixels out{static_cast<unsigned>(bitmap.bmWidth),static_cast<unsigned>(bitmap.bmHeight),{}};out.pixels.resize(static_cast<std::size_t>(out.width)*out.height*4);BITMAPINFO data{};data.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);data.bmiHeader.biWidth=out.width;data.bmiHeader.biHeight=-static_cast<LONG>(out.height);data.bmiHeader.biPlanes=1;data.bmiHeader.biBitCount=32;data.bmiHeader.biCompression=BI_RGB;
    auto dc=CreateCompatibleDC(nullptr);check(dc!=nullptr,"Owned bitmap conversion DC");const int lines=GetDIBits(dc,info.hbmColor,0,out.height,out.pixels.data(),&data,DIB_RGB_COLORS);DeleteDC(dc);check(lines==static_cast<int>(out.height),"Read own premultiplied icon pixels");return out;}
void run(const std::filesystem::path&root){const auto data=ehud::data::detail::readFile(root/"manifest.json",512*1024);check(data.has_value(),"Rendered original icon manifest exists");const auto manifest=Json::parse(*data);check(manifest["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Icon roster comes from authoritative Mac build18");
    const auto&icons=manifest["icons"].array();check(icons.size()==66,"Saved preset raw-value roster remains complete");
    const auto&defaultTray=icons.front()["tray"];auto black=ApplicationIcon::fromPNG(root/defaultTray["file"].string(),defaultTray["sha256"].string(),32,IconInk::blackTemplate);auto white=ApplicationIcon::fromPNG(root/defaultTray["file"].string(),defaultTray["sha256"].string(),32,IconInk::whiteTemplate);const auto b=inspect(black.handle()),w=inspect(white.handle());check(b.width==32&&b.height==32&&w.width==32&&w.height==32,"System DPI output retains original square canvas");std::size_t visible{};
    for(std::size_t n=0;n<b.pixels.size();n+=4){check(b.pixels[n+3]==w.pixels[n+3],"Theme treatment preserves exact source silhouette alpha");check(b.pixels[n]==0&&b.pixels[n+1]==0&&b.pixels[n+2]==0&&w.pixels[n]==w.pixels[n+3]&&w.pixels[n+1]==w.pixels[n+3]&&w.pixels[n+2]==w.pixels[n+3],"Template ink has correct native premultiplied RGB");visible+=w.pixels[n+3]!=0;}
    check(visible>0&&visible<32*32,"Endfield original glyph has visible art and transparent surround");
    const auto handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);unsigned offered{};for(const auto&icon:icons){if(icon["offered"].boolean())++offered;for(const auto key:{"tray","app"}){const auto&a=icon[key];auto value=ApplicationIcon::fromPNG(root/a["file"].string(),a["sha256"].string(),32);check(value.handle()!=nullptr,"Every preserved preset decodes to an app-local icon");auto moved=std::move(value);check(!value.handle()&&moved.handle(),"Moving transfers icon ownership");}}
    check(offered==64,"Retired hand-drawn currencies remain compatible but absent from picker");check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==handles,"All transient GDI bitmaps retire after creating each icon");
    rejects([&]{ApplicationIcon::fromPNG(root/defaultTray["file"].string(),std::string(64,'0'),32);},"Changed/unpinned original artwork is rejected before native decode");rejects([&]{ApplicationIcon::fromPixels(257,1,{});},"Pixel source cannot exceed bounded icon dimensions");
}
}
int wmain(int argc,wchar_t**argv){const auto result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(result)&&argc==2,"Pass icon asset directory");run(argv[1]);CoUninitialize();std::cout<<"Original application icons: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){if(SUCCEEDED(result))CoUninitialize();std::cerr<<e.what()<<'\n';return 1;}}
#endif
