#ifdef _WIN32
#include "tools/notes_format_menu.hpp"
#include <ole2.h>
#include <iostream>
#include <cmath>
#include <numbers>
namespace core=endfield::core;namespace gpu=endfield::native;namespace tools=endfield::tools;namespace app=endfield::app;namespace rich=core::notes;
namespace {
unsigned checks{};void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
struct Fixture{HWND hwnd{};Fixture(){check(SUCCEEDED(OleInitialize(nullptr)),"Owned test apartment");WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"NotesFormatFixture";check(RegisterClassW(&c)!=0,"Register hidden menu fixture");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Temporary menu fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Owned window remains hidden");}~Fixture(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"NotesFormatFixture",GetModuleHandleW(nullptr));OleUninitialize();}};
void run(HWND hwnd,gpu::Renderer&r,const std::filesystem::path&assets){
    gpu::LayerRasterizer raster;gpu::LayerComposition composition;rich::RichDocument document(u"Selected text\nOther line",{},65536);document.setSelection({{0,8},core::text::ActiveEnd::end,false});
    tools::NotesFormatMenu menu(raster,assets,[&](const rich::FormatChange&change){return document.applyFormat(change);});bool released{};
    struct Cleanup{gpu::Renderer&r;gpu::LayerComposition&c;tools::NotesFormatMenu&m;bool&released;~Cleanup(){if(released)return;try{c.detach(r);m.release(r);}catch(...){r.reset();}}}cleanup{r,composition,menu,released};
    double time{};core::Matrix4 world;const auto camera=gpu::layerViewportProjection(1280,800);
    auto frame=[&](double delta=.02){time+=delta;menu.update(world,camera,1280,800,{0,0,1280,800},{200,200,220,130},1,time);menu.upload(r);composition.setEntries(r,menu.entries());menu.collected(r);composition.present(r);r.setCamera(camera);};
    auto point=[&](core::Point local){return *core::Projection::viewport(camera*world,1280,800).project({200+local.x,334+local.y});};
    auto click=[&](core::Point local){const auto p=point(local);check(menu.pointer({app::PointerKind::down,app::PointerButton::left,p.x,p.y,0},1,time),"Menu press stays on its projected plane");frame();check(menu.pointer({app::PointerKind::up,app::PointerButton::left,p.x,p.y,0},1,time),"Menu release is consumed");frame();};
    check(menu.open("formatSize",document.selectionStyle(),time),"Open original size menu");frame(.2);check(menu.acceptsInput()&&menu.contains(point({235,180})),"Entire menu plate prevents clicks falling through");
    click({24,35});check(document.runs().size()==1&&document.runs()[0].style.fontSize==14&&document.runs()[0].length==8,"Selected size affects only selected UTF16 text");
    const auto paints=raster.stats();const auto uploads=r.stats();for(unsigned n=0;n<120;++n){world=core::Matrix4::translation(double(n%4),double(n%3));frame();}
    check(raster.stats().rasterizations==paints.rasterizations&&raster.stats().textLayoutsCreated==paints.textLayoutsCreated,"Tilt does not remake retained menu text or pixels");
    check(r.stats().textureUploads==uploads.textureUploads&&r.stats().meshUploads==uploads.meshUploads,"Tilt uploads no menu textures or meshes");world={};frame();
    check(menu.open("formatSpecial",document.selectionStyle(),time),"Open original trait menu");frame(.2);click({20,20});check(document.runs()[0].style.bold,"Bold toggles through original square control");click({20,20});check(!document.runs()[0].style.bold,"Second click removes bold without changing text");
    check(menu.open("formatColor",document.selectionStyle(),time),"Original prepared color wheel opens");frame(.2);click({160,91});check(document.runs()[0].style.color.has_value()&&document.runs()[0].style.color->red>.9&&document.runs()[0].style.color->green<.3,"Wheel selects warm red from source right-hand hue position");
    // Original NSColor(calibratedHue:, saturation:.7, brightness:1)
    // converted by AppKit to sRGB. Generic RGB is not plain sRGB HSV.
    struct ColorProbe{double hue,r,g,b;};
    for(const auto v:{ColorProbe{0,1,.3983038663864136,.3702169358730316},
        ColorProbe{.08,1,.6964329481124878,.369477242231369},
        ColorProbe{1./6,.9994924664497375,.9872058033943176,.3682177662849426},
        ColorProbe{.25,.69842129945755,.9825108647346497,.3697151839733124},
        ColorProbe{.5,.33565735816955566,.9924101233482361,1},
        ColorProbe{.75,.7155163884162903,.4236825108528137,1}}){
        click({91+83*.7*std::cos(v.hue*2*std::numbers::pi),91+83*.7*std::sin(v.hue*2*std::numbers::pi)});
        const auto c=document.selectionStyle().color;check(c&&std::abs(c->red-v.r)<1./255&&std::abs(c->green-v.g)<1./255&&std::abs(c->blue-v.b)<1./255,"Color wheel preserves source calibrated color within one byte");
    }
    const auto beforeColor=document.selectionStyle().color;
    check(menu.key({app::KeyKind::down,VK_RIGHT},time),"Color wheel consumes original four-point keyboard nudge");
    check(document.selectionStyle().color!=beforeColor,"Keyboard nudge edits selected text color");frame();
    check(menu.open("formatFont",document.selectionStyle(),time),"Native installed-font menu uses source presentation");frame(.2);const auto q=point({40,100});check(menu.wheel({q.x,q.y,-4,false,0,3},1,time),"Font list scroll stays inside menu");frame();
    check(menu.key({app::KeyKind::down,VK_ESCAPE},time),"Escape dismisses menu");check(!menu.acceptsInput()&&!menu.contains(q),"Closing artwork immediately detaches input");frame(.04);check(!menu.entries().empty(),"Finite closing animation retains old artwork");frame(.2);check(menu.entries().empty()&&!menu.requiresFrames(time),"Settled closed menu has no GPU entry or frame demand");
    check(document.text()==u"Selected text\nOther line","Formatting never replaces document content");composition.detach(r);menu.release(r);released=true;check(!IsWindowVisible(hwnd),"Menus never created a native popup or changed focus");
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==3,"Usage: notes_format_menu_tests shader assets");Fixture f;gpu::Renderer renderer;renderer.initialize(f.hwnd,1280,800,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});run(f.hwnd,renderer,std::filesystem::absolute(argv[2]));std::cout<<"Notes formatting menu: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<"Notes menu failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#endif
