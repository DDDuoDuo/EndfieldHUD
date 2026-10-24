#include "native/projection_media_scene.hpp"
#include "core/data/data_store.hpp"
#include "core/source_camera.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#endif
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace n=endfield::native;namespace m=endfield::modules;namespace c=endfield::core;using J=ehud::data::Json;
namespace {
unsigned checks{};
void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
void closeEnough(double a,double b,const char*s){check(std::abs(a-b)<1e-5,s);}
template<class F>void rejects(F f,const char*s){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,s);}
m::ProjectionMediaItem item(std::string_view kind="video"){
    const auto encoded=ehud::data::makeWindowsMediaReference("C:\\owned-synthetic\\never-opened.bin","Synthetic media",640,480,std::string(kind),kind=="video"?std::optional<double>{120}:std::nullopt,kind=="gif"?2:1);
    return {7,m::ProjectionMediaReference::fromWindowsReference(encoded),{10,20,360,260}};
}
const J*find(const J&root,std::string_view id){if(root["id"].isString()&&root["id"].string()==id)return &root;if(root["children"].isArray())for(const auto&child:root["children"].array())if(auto p=find(child,id))return p;return nullptr;}
const J&leaf(const J&root,std::string_view id){const auto p=find(root,id);check(p!=nullptr,"Source layer identity exists");return *p;}
c::Rect frame(const J&v){const auto&b=v["bounds"].array();const auto&p=v["position"].array();return {p[0].number(),p[1].number(),b[2].number(),b[3].number()};}
// The design text services the workspace hands the cards (no measurer: card text never measures).
const m::design::TextServices services{nullptr,{},m::design::TextRegion::simplifiedChinese,2};
J::Array rgba(double r,double g,double b,double a){return {r,g,b,a};}
void sameColor(const J&v,const J::Array&expected,const char*s){const auto&c=v["sRGB"].array();check(c.size()==4,s);for(std::size_t k=0;k<4;++k)closeEnough(c[k].number(),expected[k].number(),s);}
void portable(){
    for(const auto kind:{"image","gif","video"}){const auto i=item(kind);n::ProjectionMediaAppearance a;a.seconds=65.9;a.accent={1,.9,0,1};const auto art=n::projectionMediaArtwork(i,a,services);const auto g=m::projectionMediaGeometry({360,260},i.reference->kind());
        // ProjectionMediaNode (Mac 1.3.0): dark #141414 .9 face, 1 px #3D3D3D ring in both themes.
        check(frame(art.face)==c::Rect{0,0,360,260},"Source face uses complete card bounds");sameColor(art.face["backgroundColor"],rgba(0x14/255.,0x14/255.,0x14/255.,.9),"Face is #141414 at .9");closeEnough(art.face["borderWidth"].number(),1,"Ring is 1 px");sameColor(art.face["borderColor"],rgba(0x3D/255.,0x3D/255.,0x3D/255.,1),"Ring is #3D3D3D");
        check(art.overlay["id"].isNull()&&art.overlay["masksToBounds"].isNull(),"Overlay remains independent leaves without an invented root clip");
        const auto&title=leaf(art.overlay,"projection.media.title");check(title["kind"].string()=="text"&&frame(title)==c::Rect{8,7,320,10.5}&&title["text"]["string"].string()=="Synthetic media","Title line frame {8,7,w-40,10.5} and caption");closeEnough(title["text"]["fontSize"].number(),10.5,"Title is 10.5 px");check(title["text"]["fontWeight"].integer()==700,"Title is bold");closeEnough(title["text"]["tracking"].number(),.04,"Title tracks .04");sameColor(title["text"]["foregroundColor"],rgba(0xEE/255.,0xEE/255.,0xEE/255.,1),"Title is onDarkTag #EEEEEE");check(title["text"]["truncation"].string()=="end","Title truncates at the end");
        const auto&close=leaf(art.overlay,"projection.media.close");check(frame(close)==c::Rect{g.close.x+5.5,g.close.y+5.5,11,11},"Close is the 11 px x icon centred in its 22 px rect");
        const auto&children=art.overlay["children"].array();check(children[0]["id"].string()=="projection.media.title"&&children[1]["id"].string()=="projection.media.close","Title then close");
        check(bool(find(art.overlay,"projection.media.play"))==g.moving&&bool(find(art.overlay,"projection.media.time"))==g.video,"GIF/image/video use original controls");
        if(g.moving){const auto&play=leaf(art.overlay,"projection.media.play");check(frame(play)==c::Rect{g.play.x+5,g.play.y+5,12,12},"Play is the 12 px icon centred in its rect");}
        if(g.video){const auto&time=leaf(art.overlay,"projection.media.time");check(frame(time)==c::Rect{298,242,44,10}&&time["text"]["string"].string()=="1:05","Time {w-62,h-18,44,10} truncates to whole seconds");closeEnough(time["text"]["fontSize"].number(),10,"Time is 10 px");check(time["text"]["tabularFigures"].boolean()&&time["text"]["alignment"].string()=="right","Time is tabular and right aligned");sameColor(time["text"]["foregroundColor"],rgba(0xD9/255.,0xD9/255.,0xD9/255.,1),"Time is #D9D9D9");
            check(frame(leaf(art.overlay,"projection.media.rail"))==c::Rect{37,246,252,2},"Video rail source geometry");sameColor(leaf(art.overlay,"projection.media.rail")["backgroundColor"],rgba(1,1,1,.2),"Rail is white .2");sameColor(leaf(art.overlay,"projection.media.fill")["backgroundColor"],rgba(1,.9,0,1),"Fill is the theme colour");check(frame(leaf(art.overlay,"projection.media.thumb"))==c::Rect{34.5,243,5,8},"Zero-progress thumb source geometry");}
        const auto&grip=leaf(art.overlay,"projection.media.resize");check(frame(grip)==g.resize,"Grip retains only its tiny source hit rectangle");const auto&path=grip["shape"]["path"].array();check(path[0]["points"].array()[0].array()==J::Array{3,11}&&path[1]["points"].array()[0].array()==J::Array{11,3},"Translated tiny grip preserves original card-space ink");
        const auto&corner=leaf(art.overlay,"projection.media.corner");check(frame(corner)==c::Rect{-1,-1,10,10},"Corner mark sits at (-1,-1,10,10)");sameColor(corner["shape"]["fillColor"],rgba(1,.9,0,1),"Corner mark is the theme colour");
        check(children.back()["id"].string()=="projection.media.corner"&&children[children.size()-2]["id"].string()=="projection.media.close.rim","Corner mark paints on top, feedback above captions, progress and grip");
        const auto&rim=leaf(art.overlay,"projection.media.close.rim");check(rim["shape"]["path"].array()[0]["points"].array()[0].array()==J::Array{2,-2},"Framed cut-corner rim extends two points outside control");
        a.playing=true;a.error="Synthetic failure";const auto changed=n::projectionMediaArtwork(i,a,services);check(leaf(changed.overlay,"projection.media.title")["text"]["string"].string()==*a.error,"Error replaces caption only");if(g.moving)check(leaf(changed.overlay,"projection.media.play")!=leaf(art.overlay,"projection.media.play"),"Playing shows the pause icon");
    }
    for(const auto size:{c::Point{110,100},c::Point{360,260},c::Point{8192,2048}}){const auto mesh=n::projectionMediaFaceMesh(size);check(mesh.vertices.size()==20&&mesh.indices.size()==30,"Face storage stays fixed even at maximum width");for(const auto&v:mesh.vertices){check(v.position[0]>=0&&v.position[0]<=size.x&&v.position[1]>=0&&v.position[1]<=size.y,"All mesh vertices stay inside original face");check(v.position[2]==0,"Face painter order never becomes geometry depth");}for(auto index:mesh.indices)check(index<mesh.vertices.size(),"Face indices stay inside fixed vertex array");closeEnough(mesh.vertices[0].linearColor[3],.9,"Background alpha .9 belongs to background quad once");closeEnough(mesh.vertices[4].linearColor[3],1,"Ring is opaque");closeEnough(mesh.vertices[0].linearColor[0],0.006995410,"Face gray is decoded to linear before GPU blending");closeEnough(mesh.vertices[6].position[1],1,"Top ring lies inside original rectangle");closeEnough(mesh.vertices[12].position[1],1,"Side strips exclude top/bottom to avoid double-dark corners");}
    auto bad=item();bad.frame.x=NAN;rejects([&]{n::projectionMediaArtwork(bad,{},services);},"Nonfinite origin rejects before retaining geometry");bad=item();bad.frame.height=0;rejects([&]{n::projectionMediaArtwork(bad,{},services);},"Zero card bounds reject");auto a=n::ProjectionMediaAppearance{};a.seconds=INFINITY;rejects([&]{n::projectionMediaArtwork(item(),a,services);},"Nonfinite clock text rejects");rejects([&]{n::projectionMediaFaceMesh({8193,100});},"Face mesh respects renderer dimension bounds");
}
#ifdef _WIN32
const n::DrawObject&draw(n::NativeProjectionMediaScene&scene,std::string_view id){auto*overlay=scene.entries()[1].scene;const auto index=overlay->surfaceIndex(id);check(index.has_value(),"Native leaf resolves once by source identity");return overlay->prepareDraws()[*index];}
void retained(n::LayerRasterizer&raster){
    for(const auto kind:{"image","gif","video"}){
        n::NativeProjectionMediaScene minimum(raster,{},services);auto minimumItem=item(kind);minimumItem.frame={0,0,110,100};
        check(minimum.sync(minimumItem,{}),"Minimum source card accepts explicit empty-carrier bounds");
        const auto*carrier=minimum.entries()[0].scene;
        check(carrier->report().sourceNodes==1&&carrier->report().unsupported.empty()&&carrier->report().pixelBytes==0&&carrier->draws().empty(),"Empty carrier compiles one valid node without allocating a face raster");
        minimum.update(0);check(minimum.entries()[1].scene->prepareDraws().size()>0,"Minimum card retains its independent visible overlay");
    }
    n::NativeProjectionMediaScene scene(raster,{},services);auto i=item();n::ProjectionMediaAppearance a;a.seconds=30.25;
    check(scene.sync(i,a)&&!scene.sync(i,a),"Equal media content retains artwork");scene.update(0);check(scene.entries()[0].scene->draws().empty()&&scene.entries()[0].after.size()==1,"Fixed face mesh creates no face raster and precedes provider slot");
    closeEnough(draw(scene,"projection.media.fill").world.values[0],30.25/120,"Progress width remains fractional scalar");closeEnough(draw(scene,"projection.media.thumb").world.values[12],10+34.5+252*30.25/120,"Thumb advances in card space");
    closeEnough(draw(scene,"projection.media.close.tint").opacity,0,"Feedback is hidden before first frame");closeEnough(draw(scene,"projection.media.close.rim").opacity,.28,"Framed rim rests at source alpha");
    const auto first=raster.stats();a.seconds=31.25;check(scene.sync(i,a),"Whole second is a caption event");check(raster.stats().rasterizations==first.rasterizations+1&&raster.stats().textLayoutsCreated==first.textLayoutsCreated+2,"Only changed time text is rasterized (the design text node measures, then rasterizes it)");
    auto before=raster.stats();a.playing=true;scene.sync(i,a);check(raster.stats().rasterizations==before.rasterizations+1,"Only changed play icon is rasterized");before=raster.stats();a.error="Synthetic decoder error";scene.sync(i,a);check(raster.stats().rasterizations==before.rasterizations+1,"Only changed error caption is rasterized");
    before=raster.stats();raster.setDefaultFontLanguage(n::LayerFontLanguage::korean);check(scene.sync(i,a),"Shared font event refreshes unchanged media");check(raster.stats().rasterizations==before.rasterizations+2,"Font event refreshes only the two retained text leaves");check(!scene.sync(i,a),"Same font generation is retained");
    scene.setFeedback("play",false,false,1);scene.update(1.07);scene.setFeedback("play",true,false,1.07);scene.update(1.131);closeEnough(draw(scene,"projection.media.play.tint").opacity,1,"Press tint finishes at .06 seconds");closeEnough(draw(scene,"projection.media.play.rim").opacity,1,"Press restarts unchanged-target rim at .06 seconds");
    scene.setFeedback("close",false,false,2);scene.update(2.02);scene.setFeedback("close",false,true,2.02);scene.update(2.02);check(!scene.requiresFrames(2.02),"Same-target reduced-motion event settles all in-flight feedback");closeEnough(draw(scene,"projection.media.close.rim").opacity,1,"Reduced-motion rim reaches target immediately");
    n::DrawObject provider;provider.sourceID="owned.provider";provider.meshID="not-uploaded.synthetic.mesh";provider.world=c::Matrix4::translation(5,25);provider.masks.push_back({{}, {5,25,350,202},0});provider.alphaMask=n::PlaneAlphaMask{{},{5,25,350,202},"not-uploaded.synthetic.alpha"};provider.angularMask=n::AngularMask{{},{100,100},0,3.14,{}};provider.shutter=n::PlaneShutter{{},c::ModuleTransitionStyle::shutterKeyframe(.5,{-1,0})};scene.setDraw(provider);
    i.frame.x=37;i.frame.y=29;scene.sync(i,a);const auto dpi=c::Matrix4::scale(2,2);scene.setPose(dpi);scene.update(3);const auto world=dpi*c::Matrix4::translation(37,29),inverse=c::source::inverseSourceMatrix(world);const auto&borrowed=scene.entries()[0].after[1];check(borrowed.world==world*provider.world,"Borrowed media follows card and DPI placement");check(borrowed.masks[0].worldToLocal==inverse&&borrowed.alphaMask->worldToLocal==inverse&&borrowed.angularMask->worldToLocal==inverse&&borrowed.shutter->worldToLocal==inverse,"Every borrowed mask follows moved/scaled card via right-side inverse");
    // Warm the retained output/layout arrays before counting owner-clock frames.
    scene.entries()[1].scene->prepareDraws();before=raster.stats();allocations=0;counting=true;bool rebuilt{};
    try{for(unsigned k=0;k<120;++k){a.seconds=31.25+double(k)/1000;i.frame.x=37+k%3;rebuilt|=scene.sync(i,a);scene.update(4+double(k)/1000);scene.entries()[1].scene->prepareDraws();}}catch(...){counting=false;throw;}counting=false;
    check(!rebuilt&&allocations==0,"Fractional progress, card moves and borrowed masks allocate no CPU storage after warmup");check(raster.stats().rasterizations==before.rasterizations&&raster.stats().textLayoutsCreated==before.textLayoutsCreated,"Scalar updates neither rasterize nor shape text");
    // Live order: the scene saw a later time inside a callback, then the host's earlier sample.
    scene.setFeedback({},false,true,4.2);scene.setFeedback("close",false,false,4.2);scene.update(4.5);const auto settled=draw(scene,"projection.media.close.tint").opacity;closeEnough(settled,.62,"Hover settles before the late sample");
    scene.update(4.25);closeEnough(draw(scene,"projection.media.close.tint").opacity,settled,"An earlier owner time poses at the latest time seen instead of failing or rewinding feedback");check(!scene.requiresFrames(4.3),"Frame demand at an earlier owner time is the latest time's settled demand");
    scene.setFeedback({},false,false,4.3);check(scene.requiresFrames(4.5)&&!scene.requiresFrames(4.65),"Feedback started at an earlier owner time starts at the latest time seen");
    scene.update(4.65);closeEnough(draw(scene,"projection.media.close.tint").opacity,0,"The clamped release settles .14 seconds after the latest time");rejects([&]{scene.update(std::nan(""));},"A non-finite media owner clock still rejects");
    scene.setDraw({});check(scene.entries()[0].after.size()==1,"Removing provider keeps face and releases its borrowed span");
}
struct Window{HWND hwnd{};ATOM atom{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldProjectionMediaSynthetic";atom=RegisterClassW(&c);if(!atom)throw std::runtime_error("Register hidden fixture");hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOREDIRECTIONBITMAP,c.lpszClassName,L"Hidden owned media fixture",WS_POPUP,0,0,512,384,nullptr,nullptr,c.hInstance,nullptr);if(!hwnd)throw std::runtime_error("Create hidden fixture");}~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}};
void gpu(const wchar_t*shader,n::LayerRasterizer&raster){Window window;n::Renderer renderer;renderer.initialize(window.hwnd,512,384,{n::Driver::warpForTests,shader,n::RenderTarget::offscreenForTests});n::NativeProjectionMediaScene scene(raster,{},services);auto i=item();scene.sync(i,{});scene.update(0);scene.upload(renderer);n::LayerComposition composition;auto entries=scene.entries();composition.setEntries(renderer,entries);renderer.setCamera(n::layerViewportProjection(512,384));composition.present(renderer);renderer.draw(false);const auto frame=renderer.readback();check(std::abs(frame.pixels[std::size_t(100)*frame.rowBytes+100*4+3]/255.-.9)<2./255,"Own hidden target preserves the face alpha (#141414 at .9, 1.3.0) once");
    const auto before=renderer.stats();rejects([&]{scene.releaseResources(renderer);},"Published scene requires composition detachment before resource retirement");check(renderer.stats().meshes==before.meshes&&renderer.stats().textures==before.textures,"Rejected retirement preserves all published resources");i.frame.width=420;i.frame.height=300;scene.sync(i,{});scene.update(1);scene.upload(renderer);entries=scene.entries();composition.setEntries(renderer,entries);composition.present(renderer);renderer.draw(false);check(renderer.stats().meshes==before.meshes&&renderer.stats().textures==before.textures,"Card resize replaces bounded resources without leaking old identity");composition.detach(renderer);check(scene.releaseResources(renderer)&&renderer.stats().meshes==0&&renderer.stats().textures==0&&!IsWindowVisible(window.hwnd),"Detached hidden fixture releases all owned GPU resources");renderer.reset();}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==2,"Pass shader path to owned hidden native fixture");portable();{n::LayerRasterizer raster;retained(raster);gpu(argv[1],raster);}CoUninitialize();std::cout<<"PASS "<<checks<<" Projection media scene checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){try{portable();std::cout<<"PASS "<<checks<<" Projection media descriptor/mesh checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
