#include "native/profile_scene.hpp"
#include "native/profile_text.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <new>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#endif
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
#ifdef _WIN32
namespace {
using namespace endfield;namespace m=modules;namespace gpu=native;using J=ehud::data::Json;using M=core::Matrix4;
unsigned checks{};
void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
struct Window{HWND hwnd{};Window(){hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"Owned hidden Personal Profile scene fixture",WS_POPUP,0,0,900,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Profile scene fixture remains hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);}};
struct Fixture {
    m::PersonalProfile seed(){m::PersonalProfile p;p.uid="1000000000";p.awakeningDate=721692800;p.accumulatedWorkSeconds=3600;return p;}
    double live{3600};
    m::ProfileState state{seed(),gpu::nativeProfileTextRules(),gpu::nativeProfileDateRules(u"UTC"),persistence()};
    m::ProfilePersistence persistence(){m::ProfilePersistence io;io.commit=[](const m::PersonalProfile&p){return p;};io.workSeconds=[this]{return live;};return io;}
};
J combined(const m::ProfileArtwork&art){
    J::Array children;for(const auto*part:{&art.fields,&art.toolbar,&art.popover})for(const auto&c:part->layers["children"].array())children.push_back(c);
    return J::Object{{"id","profile.reference"},{"bounds",J::Array{0,0,0,0}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",std::move(children)}};
}
std::uint64_t difference(const gpu::Readback&a,const gpu::Readback&b){
    check(a.width==b.width&&a.height==b.height&&a.pixels.size()==b.pixels.size(),"Comparable readbacks");std::uint64_t bad{};
    unsigned minX=a.width,minY=a.height,maxX{},maxY{};int worst{};
    for(unsigned y=0;y<a.height;++y)for(unsigned x=0;x<a.width;++x)for(unsigned c=0;c<4;++c){
        const auto o=std::size_t(y)*a.rowBytes+x*4+c;const int d=std::abs(int(a.pixels[o])-int(b.pixels[o]));
        if(d>2){++bad;minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x);maxY=std::max(maxY,y);worst=std::max(worst,d);}}
    if(bad)std::cerr<<"Profile scene differs: bytes="<<bad<<" bounds="<<minX<<','<<minY<<'-'<<maxX<<','<<maxY<<" max="<<worst<<'\n';
    return bad;
}
void run(const std::filesystem::path&shader){
    Window w;gpu::Renderer renderer;renderer.initialize(w.hwnd,900,400,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});
    renderer.setCamera(gpu::layerViewportProjection(900,400));gpu::LayerRasterizer raster;gpu::LayerRasterOptions options;options.pixelsPerPoint=1;
    Fixture f;auto&s=f.state;gpu::NativeProfileScene scene(raster,options);gpu::LayerComposition composition;std::vector<gpu::LayerCompositionEntry>published;published.reserve(6);
    std::uint64_t revision{};double time{};const M place=M::translation(250,20);
    const auto frame=[&](double t,const M&world){time=t;scene.updatePose({world,place,1,1,t,{},{}});const bool uploaded=scene.uploadResources(renderer);const auto entries=scene.entries();
        bool changed=uploaded||entries.size()!=published.size();if(!changed)for(std::size_t n=0;n<entries.size();++n)changed|=entries[n].scene!=published[n].scene||entries[n].after.data()!=published[n].after.data()||entries[n].after.size()!=published[n].after.size();
        if(changed){composition.setEntries(renderer,entries);published.assign(entries.begin(),entries.end());}composition.present(renderer);renderer.draw(false);scene.collectRetired(renderer);};
    const auto sync=[&](gpu::NativeProfileChange change={},m::ProfileContents contents={},bool reduce=false){scene.syncContent(m::prepareProfileArtwork(s,{},contents),++revision,time,change,reduce);};
    // 1. Each retained leaf lands exactly where the full source tree draws it.
    for(bool dark:{true,false}){
        const auto art=m::prepareProfileArtwork(s,{dark,1,{250./255,212./255,31./255}});
        gpu::LayerScene reference(raster);reference.load(combined(art),options);check(reference.report().unsupported.empty(),"Profile artwork is fully supported by the shared rasterizer");
        // Source HUDControlHighlightLayer rest state: tint 0, framed rim .28.
        const auto rest=[&](const std::string&source){for(const auto&h:art.highlights){if(source.ends_with(h.tint))return 0.f;if(source.ends_with(h.rim))return h.framed&&h.enabled?.28f:0.f;}return 1.f;};
        std::vector<gpu::LayerPlacement>placements;for(std::size_t n=0;n<reference.draws().size();++n)placements.push_back({n,place*reference.draws()[n].world,rest(reference.draws()[n].sourceID),{}});reference.setPlacements(placements);
        composition.setScenes(renderer,std::array<gpu::LayerScene*,1>{&reference});composition.present(renderer);renderer.draw(false);const auto expected=renderer.readback();composition.detach(renderer);published.clear();
        scene.syncContent(art,++revision,time+=.1,{},true);frame(time,place);const auto actual=renderer.readback();
        check(difference(expected,actual)==0,"Retained Personal Profile surfaces equal the full source tree");composition.detach(renderer);published.clear();reference.releaseResources(renderer);
    }
    sync();frame(time+=.1,place);
    // 2. HUDControlHighlightLayer feedback: 0.14 s ease-out, no raster work.
    const auto rasterBefore=raster.stats().rasterizations;
    scene.setFeedback(core::Point{30,125},false,time+=.05,false);frame(time,place);check(scene.requiresFrames(time),"Menu highlight animates in");
    frame(time+.15,place);check(!scene.requiresFrames(time),"Highlight settles after 0.14 s");check(raster.stats().rasterizations==rasterBefore,"Pointer feedback never rasterizes");
    scene.setFeedback({},false,time+=.05,false);frame(time+.2,place);
    // 3. Steady tilt: numeric placement only.
    const auto gpuBefore=renderer.stats();allocations=0;counting=true;
    try{for(unsigned n=0;n<120;++n)frame(time+1./60,place*M::rotation(0,.000001*n,0));}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"Steady profile tilt allocates nothing");
    check(raster.stats().rasterizations==rasterBefore&&renderer.stats().textureUploads==gpuBefore.textureUploads&&renderer.stats().meshUploads==gpuBefore.meshUploads,"Tilt reuses every raster, texture and mesh");
    // 4. A changed value repaints only its leaf, with the source update animation.
    auto before=s.profile();s.commit(m::ProfileField::permissionLevel,"42");gpu::NativeProfileChange change;change.update=m::profileUpdateAnimation(s,before,s.profile());
    const auto loads=scene.stats().loads,locals=scene.stats().localUpdates;sync(change);frame(time+=.02,place);
    check(scene.stats().loads==loads&&scene.stats().localUpdates==locals+1,"One changed level leaf, no structural reload");check(scene.requiresFrames(time),"animateUpdate runs 0.18 s");
    frame(time+=.2,place);check(!scene.requiresFrames(time),"Update animation settles");
    // 5. Popover open and dismiss: departing copy for 0.14 s, then retired.
    const auto opens=s.popoverOpens();s.perform("profile:backgroundMenu");change={};change.popoverOpened=s.popoverOpens()!=opens;sync(change);frame(time+=.02,place);
    check(scene.entries().size()==4&&scene.requiresFrames(time),"Open popover animates in above the card");frame(time+=.2,place);
    const auto dismissals=s.popoverDismissals();s.dismissPopover();change={};change.popoverDismissed=s.popoverDismissals()!=dismissals;sync(change);frame(time+=.02,place);
    check(scene.entries().size()==5&&scene.requiresFrames(time),"Dismissed popover departs above the card");
    frame(time+=.2,place);check(scene.entries().size()==4&&!scene.requiresFrames(time),"Departing popover retires after its fade");
    // 6. Text visibility: fields fade .18 s linear, highlights go quiet.
    const auto toggles=s.visibilityToggles();s.perform("profile:visibility");change={};change.visibilityToggled=s.visibilityToggles()!=toggles;sync(change);frame(time+=.01,place);
    check(scene.requiresFrames(time),"Hidden text fades");frame(time+=.2,place);check(!scene.requiresFrames(time),"Hidden text settles");
    s.perform("profile:visibility");sync({},{},true);frame(time+=.01,place);
    // 7. Background plane: photo + shade group with source fades; geometry moves are numeric.
    std::vector<std::uint8_t>pixels(64*36*4);for(unsigned y=0;y<36;++y)for(unsigned x=0;x<64;++x){auto*p=&pixels[(y*64+x)*4];p[0]=static_cast<std::uint8_t>(x*4);p[1]=static_cast<std::uint8_t>(y*7);p[2]=200;p[3]=255;}
    scene.setBackdropImage(gpu::NativeProfileBackdropImage{1,64,36,pixels});
    auto p=s.profile();p.backgroundFilename="00000000-0000-4000-8000-000000000002.png";s.refresh(p,false);
    m::ProfileContents contents;contents.background=true;contents.backgroundPixels={64,36};change={};change.update=m::profileUpdateAnimation(s,before,s.profile());sync(change,contents);
    frame(time+=.3,place);check(scene.entries().size()==4&&scene.stats().groupDraws>=1,"Background group registered below the card");
    const auto shot=renderer.readback();const auto at=[&](unsigned x,unsigned y){return &shot.pixels[std::size_t(y)*shot.rowBytes+x*4];};
    check(at(250+200,20+167)[3]>0,"Background photo is visible at its center");check(at(250-100+1,20+167)[3]<20,"Background fades at its left edge");
    const auto backdropRasters=raster.stats().rasterizations;p=s.profile();p.backgroundOffsetX=120;p.backgroundZoom=2;s.refresh(p,false);
    change={};change.backdropMoved=true;sync(change,contents);frame(time+=.01,place);check(scene.requiresFrames(time),"Committed background geometry eases for 0.18 s");frame(time+=.2,place);
    check(raster.stats().rasterizations==backdropRasters,"Background offset/zoom never re-rasterizes");
    // 8. Retirement through the shared publisher.
    composition.detach(renderer);published.clear();
    check(scene.releaseResources(renderer)&&renderer.stats().textures==0&&renderer.stats().meshes==0&&renderer.stats().nativeGroups==0,"All Personal Profile resources retire");
    renderer.reset();
}
}
int wmain(int argc,wchar_t**argv){
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{check(SUCCEEDED(hr)&&argc==2,"Pass the shared shader");run(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" Personal Profile scene checks\n";return 0;}
    catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
