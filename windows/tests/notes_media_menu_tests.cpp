#include "tools/notes_preview.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <algorithm>
#include <array>
#include <iostream>
namespace n=endfield::native;namespace t=endfield::tools;namespace m=endfield::modules;namespace c=endfield::core;namespace a=endfield::app;namespace d=ehud::data;
namespace {
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Window {HWND hwnd{};ATOM atom{};Window(){WNDCLASSW w{};w.lpfnWndProc=DefWindowProcW;w.hInstance=GetModuleHandleW(nullptr);w.lpszClassName=L"EndfieldOwnedNotesMediaMenu";atom=RegisterClassW(&w);check(atom!=0,"Register owned menu fixture");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,w.lpszClassName,L"Hidden Notes media menu",WS_POPUP,0,0,1280,800,nullptr,nullptr,w.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Menu fixture stays hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}};
struct Temp {std::filesystem::path path=std::filesystem::absolute(std::filesystem::temp_directory_path())/("endfield-media-menu-"+d::makeUUID());~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
std::string ascii(const wchar_t*p){std::string s;for(;*p;++p){check(*p>0&&*p<128,"Build pins are ASCII");s.push_back(static_cast<char>(*p));}return s;}
void run(const std::filesystem::path&shader,const n::NativeNotesControlsAssets&assets){Window w;n::Renderer renderer;renderer.initialize(w.hwnd,1280,800,{n::Driver::warpForTests,shader,n::RenderTarget::offscreenForTests});const auto camera=n::layerViewportProjection(1280,800);renderer.setCamera(camera);n::LayerRasterizer raster;n::LayerComposition composition;
    {
        std::vector<t::NotesMediaAction>actions;t::NotesMediaMenu menu(raster,[&](auto value){actions.push_back(std::move(value));});double now{};
        auto frame=[&](double at,double shift=0){now=at;menu.update(c::Matrix4::translation(shift,0),{},camera,1280,800,1,at);menu.upload(renderer);composition.setEntries(renderer,menu.entries());menu.collected(renderer);composition.present(renderer);};
        auto click=[&](double x,double y){check(menu.pointer({a::PointerKind::down,a::PointerButton::left,x,y},1,now),"Menu consumes owned pointer action");menu.pointer({a::PointerKind::up,a::PointerButton::left,x,y},1,now);};
        menu.openSource({123,456},0);frame(.14);check(menu.acceptsInput()&&menu.contains({120,235})&&menu.entries().size()==1,"Source chooser uses original module placement and one retained group");
        const auto rasters=raster.stats().rasterizations,textures=renderer.stats().textureUploads;for(unsigned i=0;i<120;++i)frame(.15+double(i)/60,double(i)*.01);check(raster.stats().rasterizations==rasters&&renderer.stats().textureUploads==textures,"120 menu projection updates never reraster or upload pixels");frame(2.2);click(120,235);check(actions.size()==1&&actions[0].kind==t::NotesMediaAction::Kind::chooseLocal&&actions[0].workspacePoint.x==123&&actions[0].workspacePoint.y==456,"Native picker action preserves caller insertion point without opening a picker");check(!menu.acceptsInput()&&menu.entries().size()==1,"Dismissed chooser keeps outgoing artwork but rejects input");frame(2.33);check(menu.entries().empty(),"Source .12-second dismissal detaches artwork before retirement");
        std::vector<m::NotesShelfChoice> choices;for(unsigned i=0;i<8;++i)choices.push_back({std::to_string(i),"Synthetic "+std::to_string(i),"PNG",i!=0,i!=1});menu.openShelf(choices,{22,33},3);frame(3.14);click(60,108);check(actions.size()==1&&menu.acceptsInput(),"Unavailable first filtered Shelf row cannot select a file");click(60,70);frame(3.15);click(60,108);check(actions.size()==1,"Unsupported unfiltered row cannot select a file");
        check(menu.wheel({60,108,-3},1,now),"Shelf consumes bounded fractional wheel input");frame(3.16);check(menu.wheel({60,108,-3},1,now),"Shelf accumulates source 30-point row threshold");frame(3.17);click(60,108);check(actions.size()==2&&actions.back().kind==t::NotesMediaAction::Kind::useShelf&&actions.back().itemID=="2"&&actions.back().workspacePoint.x==22,"Filtered/scroll row mapping returns exact Shelf identity, not a copied path");frame(3.3);
        menu.openSource({},4);frame(4.14);check(menu.key({a::KeyKind::down,VK_ESCAPE},4.14)&&!menu.acceptsInput(),"Escape ends only the media chooser input");frame(4.27);composition.setEntries(renderer,{});menu.release(renderer);
    }
    {
        Temp root;t::NotesPreview notes(w.hwnd,raster,root.path,assets,false);a::ClientMetrics metrics{1280,800,96,1,1280,800};notes.resize(metrics);c::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=c::Module::notes;settings.sourceShell=true;const auto center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;
        auto frame=[&](double time){notes.update(center,settings,1,time,false);notes.upload(renderer);composition.setEntries(renderer,notes.entries());notes.collected(renderer);composition.present(renderer);};frame(0);
        check(notes.showMediaError("Synthetic picker failure",.1)&&notes.mediaError()==std::optional<std::string>("Synthetic picker failure"),"Picker failure becomes existing source status artwork");frame(.1);const auto rasters=raster.stats().rasterizations;check(!notes.showMediaError("Synthetic picker failure",.2)&&raster.stats().rasterizations==rasters,"Repeated identical error retains artwork");
        const std::array<std::string,1> invalid{"relative-owned-fixture.png"};check(!notes.importMedia(invalid,{20,20},.3)&&notes.mediaError().has_value(),"Invalid input fails before starting a file worker and stays recoverable");frame(.3);check(!notes.nextWakeTime()&&IsWindow(w.hwnd)&&!IsWindowVisible(w.hwnd),"Error path neither schedules idle media work nor closes the HUD");unsigned closed{};d::ShelfFileMetadata metadata;metadata.windowsPath="C:\\owned-synthetic\\folder";metadata.isDirectory=true;d::ShelfFileAccess directory(std::move(metadata),[&]{++closed;});check(!notes.importMedia(std::move(directory),{20,20},.35)&&closed==1,"Rejected Shelf reference closes independent lease without a worker or file access");notes.setMediaActive(false,.4);composition.setEntries(renderer,{});notes.release(renderer);
    }
    check(renderer.stats().presents==0&&!IsWindowVisible(w.hwnd),"No visible presentation or user-screen capture occurred");
}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr))return 1;int result{};try{check(argc==5,"Pass shader, explicit Notes asset root, manifest SHA, source commit");n::NativeNotesControlsAssets assets(std::filesystem::absolute(argv[2]),{ascii(argv[3]),ascii(argv[4])});run(argv[1],assets);std::cout<<"PASS "<<checks<<" Notes media menu/owner checks\n";}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#else
int main(){return 0;}
#endif
