#include "native/notes_drawing_scene.hpp"
#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace n=endfield::native;namespace m=endfield::modules;namespace c=endfield::core;
namespace {
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Window{HWND h{};ATOM a{};Window(){WNDCLASSW cl{};cl.lpfnWndProc=DefWindowProcW;cl.hInstance=GetModuleHandleW(nullptr);cl.lpszClassName=L"OwnedNotesDrawingFixture";a=RegisterClassW(&cl);check(a!=0,"Register own drawing fixture");h=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,cl.lpszClassName,L"Hidden drawing fixture",WS_POPUP,0,0,320,260,nullptr,nullptr,cl.hInstance,nullptr);check(h&&!IsWindowVisible(h),"Drawing fixture stays hidden");}~Window(){if(h)DestroyWindow(h);if(a)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(a)),GetModuleHandleW(nullptr));}};
// Owner frame: the ink's changes painted, then the composition published.
n::Readback draw(n::Renderer&renderer,n::LayerComposition&composition,n::NativeNotesDrawingScene&scene){scene.upload(renderer);composition.setEntries(renderer,scene.entries());composition.present(renderer);renderer.setCamera(n::layerViewportProjection(320,260));renderer.draw(false);return renderer.readback();}
bool same(const n::Readback&a,const n::Readback&b){if(a.pixels.size()!=b.pixels.size())return false;unsigned maxDelta{};std::size_t differing{};for(std::size_t k=0;k<a.pixels.size();++k){const auto delta=unsigned(std::abs(int(a.pixels[k])-int(b.pixels[k])));maxDelta=std::max(maxDelta,delta);differing+=delta>2;}return maxDelta<=2&&differing==0;}
bool inked(const n::Readback&image){for(std::size_t k=3;k<image.pixels.size();k+=4)if(image.pixels[k])return true;return false;}
void run(const std::filesystem::path&shader){Window w;n::Renderer renderer;renderer.initialize(w.h,320,260,{n::Driver::warpForTests,shader,n::RenderTarget::offscreenForTests});n::LayerRasterizer raster;n::LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=1;n::NativeNotesDrawingScene scene(raster,options);m::NotesDrawing document;
    check(document.append({{{.2,.5},{.8,.5}},8,{1,0,0,1}}),"Synthetic source-normalized stroke accepted");check(scene.setCompleted(document,1,{300,240})&&!scene.setCompleted(document,1,{300,240}),"Equal document/size keeps the completed ink");
    check(scene.entries().size()==2&&scene.entries()[0].after.empty(),"The ink joins its entry only once its texture is resident");
    scene.updatePose(c::Matrix4::translation(10,10),1);n::LayerComposition composition;auto image=draw(renderer,composition,scene);const auto at=std::size_t(131)*image.rowBytes+160*4;check(image.pixels[at+2]>240&&image.pixels[at+3]>240,"Completed strokes paint in the exact source viewport");
    check(scene.entries()[0].after.size()==1&&scene.entries()[0].scene->draws().empty()&&scene.entries()[1].scene->report().unsupported.empty(),"One ink draw over its own texture after an empty carrier, and the brush ring");
    check(scene.entries()[0].after[0].masks.size()==2&&scene.entries()[0].after[0].masks[0].cornerRadius==3&&scene.entries()[0].after[0].masks[1].bounds==m::NotesDrawing::viewport({300,240}),"The ink clips to the rounded card and the drawing viewport");
    const auto first=scene.stats();check(first.wholeRasters==1&&first.textureCreations==1,"The first paint rasterizes the picture once and creates its texture");
    const auto before=scene.stats();const auto gpuBefore=renderer.stats();m::DrawingStroke live{{{.4,.25}},12,{0,0,1,1}};scene.setLive(&live,1);draw(renderer,composition,scene);
    check(scene.stats().wholeRasters==before.wholeRasters&&scene.stats().regionRasters==before.regionRasters+1&&scene.stats().textureCreations==before.textureCreations&&renderer.stats().textureUploads==gpuBefore.textureUploads&&renderer.stats().textureRegionUploads==gpuBefore.textureRegionUploads+1,"A live dot paints its own region in place, never the completed strokes");
    const auto stableRaster=raster.stats().rasterizations;scene.setBrush(c::Point{100,90},8,{1,1,1,1});composition.setEntries(renderer,scene.entries());const auto stable=scene.stats();const auto gpu=renderer.stats();allocations=0;counting=true;for(unsigned i=0;i<120;++i){scene.setBrush(c::Point{100+double(i)*.1,90},8,{1,1,1,1});scene.updatePose(c::Matrix4::translation(10+double(i)*.01,10),1);(void)scene.upload(renderer);composition.present(renderer);}counting=false;
    check(allocations==0&&scene.stats().wholeRasters==stable.wholeRasters&&scene.stats().regionRasters==stable.regionRasters&&scene.stats().brushRasters==stable.brushRasters,"120 brush/tilt samples allocate and rasterize nothing");check(renderer.stats().textureUploads==gpu.textureUploads&&renderer.stats().textureRegionUploads==gpu.textureRegionUploads&&raster.stats().rasterizations==stableRaster+1,"Brush movement uses the existing small ring texture");
    const auto draws=scene.entries()[1].scene->draws();check(draws.size()==1&&draws[0].masks.size()==1&&draws[0].masks[0].bounds==c::Rect{0,0,300,240}&&draws[0].masks[0].cornerRadius==3,"Brush clips to the source rounded card, not the drawing viewport");
    scene.updatePose(c::Matrix4::translation(10,10),1);scene.setLive(nullptr,2);scene.setBrush({},8,{1,1,1,1});image=draw(renderer,composition,scene);const auto dot=std::size_t(10+29+46)*image.rowBytes+std::size_t(10+5+116)*4;
    check(image.pixels[dot+3]==0&&scene.entries()[1].scene->draws()[0].opacity==0,"Ending a gesture without a commit removes its ink in place and hides the brush");
    const auto resize=scene.stats();scene.setCompleted(document,1,{310,250});draw(renderer,composition,scene);check(scene.stats().wholeRasters==resize.wholeRasters+1&&scene.stats().textureCreations==resize.textureCreations+1,"Same document revision at a resized viewport repaints the normalized strokes whole");
    composition.detach(renderer);check(scene.releaseResources(renderer)&&renderer.stats().resourceBytes==0,"The ink and brush retire only after composition detaches");
    // A drawing card beyond the former local group area (the source has no
    // size limit) keeps one ink picture at a bounded density.
    {n::NativeNotesDrawingScene big(raster,options);check(big.setCompleted(document,2,{1600,1000}),"A 1600x1000 drawing card is accepted");
        const auto viewport=m::NotesDrawing::viewport({1600,1000});big.updatePose(c::Matrix4::scale(.2,.2,1),1);n::LayerComposition large;const auto picture=draw(renderer,large,big);check(big.entries()[0].after.size()==1&&big.stats().wholeRasters==1,"Its strokes stay one ink picture");
        const auto row=static_cast<std::size_t>(std::lround((viewport.y+viewport.height*.5)*.2));bool red{};for(std::size_t x=std::size_t((viewport.x+viewport.width*.3)*.2);x<std::size_t((viewport.x+viewport.width*.7)*.2);++x){const auto p=row*picture.rowBytes+x*4;red=red||(picture.pixels[p+2]>200&&picture.pixels[p]<60);}
        check(red,"The stroke paints across the full-size viewport");m::DrawingStroke wide{{},20,{0,0,1,1}};for(unsigned k=0;k<=16;++k)wide.points.push_back({.1+.05*k,.1+.05*k});check(big.setLive(&wide,3),"A live stroke on the large card");draw(renderer,large,big);check(big.stats().wholeRasters==1&&big.stats().regionRasters>0,"It paints its regions in place");
        large.detach(renderer);check(big.releaseResources(renderer)&&renderer.stats().resourceBytes==0,"The large drawing releases its ink");}
    // Incremental painting (RC: the pen lagged the cursor): every sample since
    // the previous frame paints only the pixels its segments reach, in place;
    // a committed stroke is already painted; an erased or cancelled stroke
    // repaints its own footprint. The picture always equals a whole repaint.
    {std::vector<m::DrawingStroke>strokes{{{{.05,.1},{.48,.48},{.95,.65}},13,{1,.2,.1,.7}},{{{.45,.2},{.50,.6},{.65,.92}},5,{.1,.3,1,.5}}};m::NotesDrawing base;for(const auto&s:strokes)check(base.append(s),"Synthetic strokes accepted");
     // One publisher per renderer: the reference replaces the incremental
     // ink in the composition, then leaves it for the next frame to republish.
     n::LayerComposition own;
     const auto reference=[&](const m::NotesDrawing&completed,const m::DrawingStroke*stroke){n::NativeNotesDrawingScene fresh(raster,options);fresh.setCompleted(completed,1,{300,240});if(stroke)fresh.setLive(stroke,1);fresh.updatePose(c::Matrix4::translation(10,10),1);const auto pixels=draw(renderer,own,fresh);own.detach(renderer);check(fresh.releaseResources(renderer),"The whole-repaint reference retires");check(fresh.stats().wholeRasters<=1&&fresh.stats().regionRasters==0,"The reference paints whole");return pixels;};
     n::NativeNotesDrawingScene ink(raster,options);ink.setCompleted(base,1,{300,240});ink.updatePose(c::Matrix4::translation(10,10),1);draw(renderer,own,ink);
     m::DrawingStroke growing{{{.1,.6}},9,{.2,.8,.3,.8}};growing.points.reserve(4096);ink.setLive(&growing,1);std::uint64_t revision{1};
     const auto picture=std::uint64_t(290)*184;
     for(unsigned frame=1;frame<=40;++frame){const auto before=ink.stats();const auto gpu=renderer.stats();
        // One to three samples per frame (coalesced moves), painted together.
        for(unsigned k=0;k<1+frame%3;++k){const auto t=double(frame*3+k)/123.;growing.points.push_back({.1+.8*t,.6+.25*std::sin(t*9)});ink.setLive(&growing,++revision);}
        draw(renderer,own,ink);const auto after=ink.stats();
        check(after.wholeRasters==before.wholeRasters&&after.textureCreations==before.textureCreations&&renderer.stats().textureUploads==gpu.textureUploads,"A growing stroke never repaints or recreates the whole picture");
        check(after.regionRasters>before.regionRasters&&after.regionRasters-before.regionRasters<=2&&after.regionPixels-before.regionPixels<picture/8,"Each frame paints its samples' newest segments once, in one region per tile");}
     const auto grown=draw(renderer,own,ink);check(same(grown,reference(base,&growing)),"Painting a live stroke frame by frame equals painting it whole");
     // Pen-up: the committed stroke was painted where the live one lies.
     auto committed=base;check(committed.append(growing),"The live stroke commits");const auto beforeCommit=ink.stats();ink.setCompleted(committed,2,{300,240});ink.setLive(nullptr,++revision);const auto commitImage=draw(renderer,own,ink);
     check(ink.stats().plan.adoptions==beforeCommit.plan.adoptions+1&&ink.stats().regionRasters==beforeCommit.regionRasters&&ink.stats().wholeRasters==beforeCommit.wholeRasters,"A committed stroke paints nothing");
     check(same(commitImage,reference(committed,nullptr)),"The committed stroke equals the whole repaint");
     // Erasing the thin crossing stroke: only its footprint, painted from the rest.
     m::NotesDrawing erased;for(std::size_t k=0;k<committed.strokes().size();++k)if(k!=1)erased.append(committed.strokes()[k]);const auto beforeErase=ink.stats();ink.setCompleted(erased,3,{300,240});const auto eraseImage=draw(renderer,own,ink);
     check(ink.stats().wholeRasters==beforeErase.wholeRasters&&ink.stats().regionRasters>beforeErase.regionRasters,"An erased stroke repaints only its footprint");
     check(same(eraseImage,reference(erased,nullptr)),"The erased footprint equals the whole repaint");
     // A cancelled live stroke (the drawing limit) leaves no ink.
     m::DrawingStroke cancelled{{{.7,.15},{.75,.3},{.6,.35}},20,{1,1,0,1}};ink.setLive(&cancelled,++revision);draw(renderer,own,ink);ink.setLive(nullptr,++revision);const auto cancelImage=draw(renderer,own,ink);
     check(same(cancelImage,reference(erased,nullptr))&&inked(cancelImage),"A cancelled live stroke's footprint equals the whole repaint");
     own.detach(renderer);check(ink.releaseResources(renderer)&&renderer.stats().resourceBytes==0,"Incremental ink retires all owned resources");}
    check(!IsWindowVisible(w.h)&&renderer.stats().presents==0,"No desktop capture, visible window or application data");
}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr))return 1;int result{};try{check(argc==2,"Pass native/hud.hlsl");run(argv[1]);std::cout<<"PASS "<<checks<<" retained Notes drawing checks\n";}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#else
int main(){return 0;}
#endif
