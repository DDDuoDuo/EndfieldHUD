#include "native/chrome_presentation.hpp"
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

namespace {std::atomic<bool> counting{};std::atomic<std::size_t> allocations{};}
void* operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
using namespace endfield::core;
using namespace endfield::core::source;
using namespace endfield::native;
namespace {
unsigned checks{};
void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
Json read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);check(bool(f),"Explicit source reference opens");std::string s((std::istreambuf_iterator<char>(f)),{});return Json::parse(s,64*1024*1024);}
Matrix4 matrix(const Json&j){Matrix4 m;for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)m.values[c*4+r]=j.array().at(c).array().at(r).number();return m;}
void run(const std::filesystem::path&referencePath,const std::filesystem::path&packetPath){
    const auto reference=read(referencePath/"chrome.json"),packet=read(packetPath/"animation.json");
    auto scene=SceneDefinition::fromJson(packet["scene"]);auto library=Library::fromJson(packet["library"]);WatchAnimation animation(scene,library);SourceCamera camera(packet["runtimeRoot"]);
    DesktopChromeProjectionPlan projection(scene,camera,animation,DesktopChromeBindings::fromJson(reference["bindings"]));
    auto root=Json::Object{{"id","owned.chrome.fixture"},{"bounds",Json::Array{0,0,1280,800}},{"children",Json::Array{}}};
    LayerRasterizer raster;LayerScene layers(raster);LayerRasterOptions options;options.assetRoot=referencePath;
    const auto colored=desktopChromeAppearanceReference(reference,{false,{.2,.5,.7}});
    check(colored["header"]["bounds"]==reference["header"]["bounds"]&&colored["header"]["children"].array()[0]["text"]["string"].string()=="ENDFIELDHUD","Runtime appearance preserves literal source header and geometry");
    const auto&headline=colored["header"]["children"].array()[0]["text"]["foregroundColor"]["sRGB"].array();check(headline[0].number()==.12&&headline[3].number()==1,"Light source header uses its original primary role");
    for(const bool hover:{false,true}){const auto&children=colored["panels"].array()[hover?1:0]["children"].array();
        check(children.size()==4&&children[3]["name"].string()=="hud.clock.viewport"&&children[3]["children"].array()[0]["children"].array().empty(),"The retained panel keeps the plate, frame, marks and an empty clipped page");
        check(children[1]["shape"]["strokeColor"]["sRGB"].array()[3].number()==.70&&children[1]["shape"]["strokeColor"]["sRGB"].array()[1].number()==.5,"The frame takes the accent at .70 in both hover looks");}
    // SHELL-36: the header and footer are HUDStandardTextLayer (HarmonyOS Sans), not the monospaced system font.
    check(colored["header"]["children"].array()[0]["text"]["fontWeight"].integer()==600&&colored["header"]["children"].array()[0]["text"]["font"]["familyName"].string()=="HarmonyOS Sans"&&
          colored["footer"]["children"].array()[0]["text"]["font"]["familyName"].string()=="HarmonyOS Sans","Chrome texts use the standard faces");
    const auto combined=NativeChromePresentation::combinedReferenceRoot(root,reference);layers.load(combined,options);
    check(layers.report().surfaces==4,"Header, footer text, status and the record dot are four local surfaces");
    check(layers.report().unsupported.empty(),"Actual source chrome layer effects are supported; font substitutions are reported separately");
    NativeChromePresentation bridge(projection,layers,reference,options,raster);
    DesktopChromeContent content;content.uppercaseShortcut="CTRL+SPACE";content.localizedClose="CLICK OUTSIDE TO CLOSE";
    check(bridge.setContent(content),"First explicit content sample refreshes status and footer only");
    const auto initial=raster.stats();const auto epoch=layers.contentRevision();
    check(!bridge.setContent(content),"Equal content sample retains local text/artwork");check(raster.stats().rasterizations==initial.rasterizations,"Equal sample performs no raster work");
    content.reading=DesktopClockReading{"13:24:15","MON Oct 7"};check(bridge.setContent(content),"Clock sample updates local status surface");check(raster.stats().rasterizations==initial.rasterizations+1,"Clock change cannot rerasterize header, footer or the HUD");
    content.uppercaseShortcut="ALT+SPACE";check(bridge.setContent(content),"Shortcut updates the exact footer string");check(raster.stats().rasterizations==initial.rasterizations+2,"Shortcut refresh changes one local text surface");
    for(unsigned i=0;i<5;++i){content.style=static_cast<DesktopClockStyle>(i);content.clockHovered=i%2==1;content.workPhase=static_cast<DesktopWorkPhase>(i%3);bridge.setContent(content);check(layers.report().unsupported.empty(),"Each actual source clock instrument stays supported");
        // The measured fit: the time never overflows its column (HUDClockStyleArtwork.fit).
        const auto&art=bridge.clockArtwork();check(art.time.fontSize>=10&&art.time.frame.width>0&&art.workBadge.text.starts_with("WORK MODE")&&art.recordLabel.text=="RECORDING MODE","Each style lays out its time and the status row");}
    // Recording mode: the dot blinks with the 1 Hz sample (no loop), Reduce Motion keeps it lit.
    content.recording=true;content.reading=DesktopClockReading{"14:25:17","TUE Oct 8"};check(bridge.setContent(content),"Recording mode recolours the status row");
    check(bridge.requiresFrames(10)&&bridge.advance(10)&&bridge.recordDotOpacity()==1,"An odd second starts the dot's fade on the next frame");
    (void)bridge.advance(10.175);check(bridge.recordDotOpacity()<1&&bridge.recordDotOpacity()>.28,"The dot eases over .35 s");
    check(!bridge.advance(10.4)&&bridge.recordDotOpacity()==.28&&!bridge.requiresFrames(10.4),"Then the dot rests and nothing wakes until the next sample");
    content.reduceMotion=true;content.reading=DesktopClockReading{"14:25:19","TUE Oct 8"};(void)bridge.setContent(content);
    check(!bridge.requiresFrames(11)&&bridge.recordDotOpacity()==1,"Reduce Motion: the dot stays lit");
    content.recording=false;content.reduceMotion=false;(void)bridge.setContent(content);
    check(layers.contentRevision()==epoch,"Clock changes retain structural surface identities");
    const auto beforeTheme=raster.stats();const auto oldDraws=layers.draws();std::array<std::string,3>sourceIDs{oldDraws[0].sourceID,oldDraws[1].sourceID,oldDraws[2].sourceID};
    const DesktopChromeAppearance light{false,{.2,.5,.7}};check(bridge.setAppearance(light),"Source chrome accepts caller's effective theme accent");check(raster.stats().rasterizations==beforeTheme.rasterizations+3,"Appearance event updates only the three chrome surfaces");
    const auto themed=raster.stats();allocations=0;counting=true;for(unsigned n=0;n<120;++n)bridge.setAppearance(light);counting=false;check(allocations==0&&raster.stats().rasterizations==themed.rasterizations&&raster.stats().textLayoutsCreated==themed.textLayoutsCreated,"Equal appearance causes no layout, raster or allocation");
    for(unsigned n=0;n<3;++n)check(layers.draws()[n].sourceID==sourceIDs[n],"Appearance preserves retained source surface identities");
    content.reading=DesktopClockReading{"14:25:16","TUE Oct 8"};check(bridge.setContent(content)&&raster.stats().rasterizations==themed.rasterizations+1,"Clock ticks after theme event still refresh only status");
    auto bad=light;bad.effectiveAccent[1]=-1;bool rejectedColor{};try{bridge.setAppearance(bad);}catch(const std::exception&){rejectedColor=true;}check(rejectedColor&&layers.contentRevision()==epoch,"Invalid accent cannot replace source chrome structure");

    SourceLayout layout(scene);auto nodes=layout.resolve();SourceWatchFrame frame;frame.resolved=nodes;
    const auto&row=reference["projections"].array().back();
    for(const auto&n:row["nodes"].array()){const auto at=layout.nodeIndex(n["id"].string());check(at.has_value(),"Bound source chrome node exists");nodes[*at].worldMatrix=matrix(n["world"]);if(!n["rect"].isNull())nodes[*at].rect=SourceRect{{n["rect"]["origin"].array()[0].number(),n["rect"]["origin"].array()[1].number()},{n["rect"]["size"].array()[0].number(),n["rect"]["size"].array()[1].number()}};}
    CameraFrame cameraFrame;cameraFrame.view=matrix(row["view"]);cameraFrame.projection=matrix(row["projection"]);cameraFrame.worldRoot=matrix(row["worldRoot"]);
    DesktopChromeSettings settings{{0,0,1920,1080},1.12,{.05,-.04},Module::power,true};
    check(bridge.update(frame,cameraFrame,settings,.75f),"Original source frame attaches all native chrome planes");
    const auto before=raster.stats();allocations=0;counting=true;
    for(unsigned i=0;i<120;++i){cameraFrame.worldRoot.values[12]+=.0001;bridge.update(frame,cameraFrame,settings,.75f);}
    counting=false;check(allocations==0,"Pointer-driven chrome placement allocates nothing");
    check(raster.stats().rasterizations==before.rasterizations&&raster.stats().textLayoutsCreated==before.textLayoutsCreated,"Pointer motion never re-rasterizes clock/header/footer");
    check(!bridge.update(frame,cameraFrame,settings,.75f),"Identical native chrome placement does no scene work");
    layers.load(combined,options);bool rejected=false;try{bridge.update(frame,cameraFrame,settings);}catch(const std::exception&){rejected=true;}check(rejected,"Structural reload cannot silently reuse stale surface indices");
}
}
int wmain(int argc,wchar_t**argv){const auto result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(result)&&argc==3,"Pass source chrome and mounted packet roots");run(argv[1],argv[2]);CoUninitialize();std::cout<<"PASS "<<checks<<" native chrome presentation checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(result))CoUninitialize();std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
