#include "native/work_mode_scene.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <utility>
#include <vector>
using namespace endfield;using namespace native;
namespace{std::atomic<std::size_t>allocations{};bool counting{};unsigned checks{};void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}template<class F>void rejects(F f,const char*m){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,m);}
struct Window{HWND hwnd{};ATOM atom{};Window(){WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"EndfieldWorkModeSynthetic";atom=RegisterClassW(&type);check(atom!=0,"Owned class registers");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,type.lpszClassName,L"Synthetic",WS_POPUP,0,0,440,440,nullptr,nullptr,type.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Fixture remains hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}};
void run(const std::filesystem::path&shader){Window window;Renderer renderer;renderer.initialize(window.hwnd,440,440,{Driver::warpForTests,shader,RenderTarget::offscreenForTests});renderer.setCamera(layerViewportProjection(440,440));LayerRasterizer raster;LayerRasterOptions options;options.pixelsPerPoint=1;modules::WorkModeController controller;modules::WorkModePresentation state(controller);state.activate(0);NativeWorkModeScene scene(state,raster,options);LayerComposition composition;
 auto publish=[&](double time){state.refresh(time);scene.syncContent(time);scene.updatePose({{},1,time,{},{}});scene.uploadAnimations(renderer);composition.setEntries(renderer,scene.entries());composition.present(renderer);renderer.draw(false);check(scene.collectRetired(renderer),"Unpublished old source surfaces retire");};
 publish(0);check(scene.entries().size()==7,"Source paint order contains 7 independent parts: the ring base and the ring share the ring carrier");const auto idle=renderer.readback();check(std::any_of(idle.pixels.begin(),idle.pixels.end(),[](auto value){return value!=0;}),"Original Work Mode artwork paints owned target");
 // The ring base and the ring (440 pt circle strokes) are antialiased GPU
 // annulus meshes in source order instead of two canvas-sized rasters.
 {const auto alphaAt=[&](unsigned x,unsigned y){return idle.pixels[std::size_t(y)*idle.rowBytes+x*4+3];};const auto draws=composition.draws();
  const auto base=std::find_if(draws.begin(),draws.end(),[](const auto&d){return d.meshID.ends_with(".ringBase")&&d.textureID.empty();});
  const auto ring=std::find_if(draws.begin(),draws.end(),[](const auto&d){return d.meshID.ends_with(".ring")&&d.textureID.empty()&&d.angularMask.has_value();});
  check(base!=draws.end()&&ring!=draws.end()&&base<ring&&alphaAt(220,5)>0&&alphaAt(435,220)>0&&alphaAt(220,435)>0&&alphaAt(5,220)>0&&alphaAt(220,30)==0,
        "The ring base and the ring are untextured GPU meshes in source order, painted around the whole circle");}
 // Parking (HUDModuleContent drops a screen that is no longer shown): once
 // unpublished, the ring meshes, group targets and every part's raster go;
 // the next sync rebuilds artwork that paints the same frame.
 {composition.detach(renderer);const auto rasters=raster.stats();const auto built=scene.stats();const auto meshes=renderer.stats().meshes;
  check(built.parts>0&&built.pixelBytes>0&&scene.park(renderer)&&scene.stats().parts==0&&scene.stats().pixelBytes==0&&!scene.stats().gpuResident&&scene.entries().empty()&&scene.stats().parks==built.parks+1&&
        renderer.stats().nativeGroups==0&&renderer.stats().meshes+2<=meshes&&raster.stats().entries<rasters.entries&&raster.stats().resourceBytes+built.pixelBytes<=rasters.resourceBytes,
        "An unpublished Work Mode parks its ring meshes, group targets and rasters");
  check(!scene.park(renderer),"Nothing is left to park");
  publish(0);const auto again=renderer.readback();
  check(scene.stats().parts==built.parts&&scene.stats().pixelBytes==built.pixelBytes&&scene.entries().size()==7&&again.pixels==idle.pixels,"The rebuilt Work Mode paints the parked frame exactly");}
 check(state.perform("work:start",0),"Start uses source controller");publish(0);const auto resources=renderer.stats();const auto local=raster.stats();
 for(unsigned n=1;n<=120;++n){const double now=n*.001;state.refresh(now);check(!scene.syncContent(now),"Subsecond ring frames do not regenerate artwork");scene.updatePose({core::Matrix4::rotation(0,.001*n,0),1,now,{},{}});scene.uploadAnimations(renderer);composition.setEntries(renderer,scene.entries());composition.present(renderer);}
 const auto after=renderer.stats();check(after.textureUploads==resources.textureUploads&&after.meshUploads==resources.meshUploads&&after.objectBufferAllocations==resources.objectBufferAllocations&&raster.stats().rasterizations==local.rasterizations,"Continuous ring/layout use only retained numeric state");
 publish(.5);const auto beforeTick=raster.stats();const auto beforeGPU=renderer.stats();publish(1.1);check(raster.stats().rasterizations==beforeTick.rasterizations+1&&renderer.stats().textureUploads==beforeGPU.textureUploads+1,"One-second label changes rasterize/upload exactly one clock leaf");
 // Warm all numeric buffers before measuring allocation-free settled motion.
 scene.updatePose({{},1,1.2,{},{}});scene.uploadAnimations(renderer);composition.setEntries(renderer,scene.entries());composition.present(renderer);allocations=0;counting=true;
 std::array<std::size_t,4>stageAllocations{};try{for(unsigned n=0;n<100;++n){auto before=allocations.load();scene.updatePose({core::Matrix4::rotation(0,.001*n,0),1,1.21+n*.001,{},{}});stageAllocations[0]+=allocations.load()-before;before=allocations.load();scene.uploadAnimations(renderer);stageAllocations[1]+=allocations.load()-before;before=allocations.load();composition.setEntries(renderer,scene.entries());stageAllocations[2]+=allocations.load()-before;before=allocations.load();composition.present(renderer);stageAllocations[3]+=allocations.load()-before;}}catch(...){counting=false;throw;}counting=false;if(allocations!=0)std::cerr<<"Warm allocations pose/upload/entries/present="<<stageAllocations[0]<<'/'<<stageAllocations[1]<<'/'<<stageAllocations[2]<<'/'<<stageAllocations[3]<<'\n';check(allocations==0,"Warm ring/tilt frames allocate no heap objects");
 state.perform("work:pause",1.4);publish(1.4);check(!scene.requiresFrames(2),"Paused settled Work Mode stops compositor demand");state.perform("work:reset",2);publish(2);publish(2.5);scene.setFeedback("work:stopwatch",true,2.5);publish(2.57);check(scene.requiresFrames(2.57)==false,"Stationary pressed feedback settles after source duration");
 // Out-of-order owner times (the live Projection crash class): a pose, feedback
 // or query earlier than a time the scene already saw uses the latest time.
 // Nothing fails, and a settled control never reopens its finished feedback.
 check(scene.setFeedback("work:stopwatch",false,2.6),"Releasing the pressed control starts its feedback");scene.updatePose({{},1,2.58,{},{}});
 check(scene.requiresFrames(2.55),"An earlier pose and query after a later feedback are placed at the latest time");
 check(!scene.setFeedback("work:stopwatch",false,2.59),"An earlier repeated feedback is an unchanged no-op");
 scene.updatePose({{},1,2.8,{},{}});check(!scene.requiresFrames(2.65)&&!scene.requiresFrames(2.8),"Once settled at a later time, an earlier query never reopens the feedback");
 state.perform("work:stopwatch",3);publish(3);state.perform("work:start",3.3);publish(3.3);const auto active=composition.draws();const auto ring=std::find_if(active.begin(),active.end(),[](const auto&d){return d.angularMask.has_value();});check(ring!=active.end()&&ring->angularMask->endPlane.has_value(),"Resident source ring uses exact cubic endpoint plane");check(std::abs(ring->angularMask->sweepAngle-.2900428318115755)<1e-10,"Stopwatch source .045 stroke is not approximated by linear angle");
 state.setFocusStatus("Permission requested by injected platform",true,3.4);publish(3.4);check(renderer.stats().nativeGroups==2,"Permission plate owns a second retained source-opacity group");state.perform("work:focusAccess",3.5);publish(3.5);bool permissionGroup{},caption{};for(const auto&entry:scene.entries()){for(const auto&draw:entry.after)if(draw.sourceID.find("workMode.focusAccess")!=std::string::npos){permissionGroup=true;check(std::abs(draw.opacity-.55f)<1e-6,"Permission feedback multiplies group opacity once");}if(const auto index=entry.scene->surfaceIndex("workMode.focusStatus")){caption=true;entry.scene->prepareDraws();check(entry.scene->draws()[*index].opacity==1,"Separate permission caption never fades with plate");}}check(permissionGroup&&caption,"Original plate and caption retain separate source ancestry");publish(3.8);
 state.setReduceMotion(true,4);publish(4);check(!scene.requiresFrames(4),"Reduced motion does not keep source ring alive");state.deactivate(4);composition.detach(renderer);check(scene.releaseResources(renderer)&&renderer.stats().objects==0&&renderer.stats().nativeGroups==0,"Shared group/ring resources retire after detach");
 // The ring meshes against the exact stroke they stand for and the rasters
 // they replace: the source ring base and the countdown and stopwatch rings
 // (prepareWorkModeArtwork's CGPath cubic-circle strokes) at one and two
 // pixels per point, each alone on the same target. The reference is the
 // box-filtered coverage of the stroke (16x16 samples a pixel; the cubic
 // circle's radius tabulated by angle) times the stroke alpha. The Direct2D
 // raster flattens the curve to within .25 px, the mesh follows it.
 {
  struct Error{int worst{};double mean{};};
  const auto parity=[&](Renderer&target,unsigned pixels,double ppp){
   target.setCamera(layerViewportProjection(pixels,pixels)*core::Matrix4::scale(ppp,ppp));LayerRasterOptions o;o.pixelsPerPoint=ppp;
   for(const bool countdown:{true,false}){
    modules::WorkModeController c;modules::WorkModePresentation s(c);if(!countdown)c.chooseStopwatch(0);s.refresh(0);
    const auto art=modules::prepareWorkModeArtwork(s,{true,ppp,{250./255,212./255,31./255,1}});
    for(const auto*part:{&art.ringBase,&art.ring}){
     LayerRasterizer local;LayerScene reference(local);reference.load(part->layers,o);reference.upload(target);target.draw(false);const auto raster=target.readback();reference.detach(target);
     const auto&shape=part->layers["children"].array()[0]["shape"];const auto&ink=shape["strokeColor"]["sRGB"].array();
     std::vector<Vertex>vertices;std::vector<std::uint32_t>indices;workModeRingMesh(shape,ppp,vertices,indices);
     check(target.setMesh("parity.ring",1,{vertices,indices}),"The ring mesh uploads");DrawObject draw;draw.sourceID="parity.ring/draw";draw.meshID="parity.ring";
     for(unsigned n=0;n<3;++n){const auto v=ink[n].number();draw.linearTint[n]=float(v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4));}draw.linearTint[3]=float(ink[3].number());
     target.setDrawList(std::span<const DrawObject>(&draw,1));target.draw(false);const auto mesh=target.readback();target.clearDrawList();check(target.removeMesh("parity.ring"),"The parity mesh retires");
     check(raster.pixels.size()==mesh.pixels.size()&&raster.width==pixels&&mesh.height==pixels,"Comparable ring readbacks");
     // The cubic circle's radius by angle (each segment is monotonic in angle).
     std::vector<std::pair<double,double>>radius;const auto&path=shape["path"].array();std::array<double,2>from{path[0]["points"].array()[0].array()[0].number(),path[0]["points"].array()[0].array()[1].number()};
     for(std::size_t n=1;n<path.size();++n){const auto&q=path[n]["points"].array();if(q.size()!=3)continue;std::array<std::array<double,2>,4>k{from,{q[0].array()[0].number(),q[0].array()[1].number()},{q[1].array()[0].number(),q[1].array()[1].number()},{q[2].array()[0].number(),q[2].array()[1].number()}};
      for(unsigned j=0;j<=1024;++j){const double t=j/1024.,u=1-t;double xy[2];for(unsigned m=0;m<2;++m)xy[m]=u*u*u*k[0][m]+3*u*u*t*k[1][m]+3*u*t*t*k[2][m]+t*t*t*k[3][m];radius.push_back({std::atan2(xy[1]-220,xy[0]-220),std::hypot(xy[0]-220,xy[1]-220)});}
      from=k[3];}
     std::sort(radius.begin(),radius.end());
     const auto curve=[&](double angle){auto it=std::lower_bound(radius.begin(),radius.end(),std::pair<double,double>{angle,-1.});if(it==radius.begin())return it->second;if(it==radius.end())return radius.back().second;const auto&lo=*(it-1);const auto&hi=*it;return hi.first>lo.first?lo.second+(hi.second-lo.second)*(angle-lo.first)/(hi.first-lo.first):hi.second;};
     const double half=shape["lineWidth"].number()/2,alpha=ink[3].number();Error meshError,rasterError;std::size_t covered{};double meshTotal{},rasterTotal{};
     for(unsigned y=0;y<pixels;++y)for(unsigned x=0;x<pixels;++x){
      const auto at=std::size_t(y)*mesh.rowBytes+x*4+3;const double cx=(x+.5)/ppp-220,cy=(y+.5)/ppp-220,centre=std::hypot(cx,cy);
      double expected{};
      if(std::abs(centre-215)<=half+2/ppp){unsigned inside{};for(unsigned j=0;j<16;++j)for(unsigned i=0;i<16;++i){const double px=(x+(i+.5)/16)/ppp-220,py=(y+(j+.5)/16)/ppp-220;inside+=std::abs(std::hypot(px,py)-curve(std::atan2(py,px)))<=half;}expected=255*alpha*inside/256.;}
      if(expected<=0&&!mesh.pixels[at]&&!raster.pixels[at])continue;++covered;
      const double m=std::abs(mesh.pixels[at]-expected),r=std::abs(raster.pixels[at]-expected);
      meshError.worst=std::max(meshError.worst,int(std::lround(m)));rasterError.worst=std::max(rasterError.worst,int(std::lround(r)));meshTotal+=m;rasterTotal+=r;}
     meshError.mean=meshTotal/double(covered?covered:1);rasterError.mean=rasterTotal/double(covered?covered:1);
     std::cout<<"ring coverage ppp="<<ppp<<(countdown?" countdown ":" stopwatch ")<<(part==&art.ringBase?"base":"ring")<<": pixels="<<covered
              <<" mesh worst="<<meshError.worst<<" mean="<<meshError.mean<<" raster worst="<<rasterError.worst<<" mean="<<rasterError.mean<<'\n';
     check(covered>0&&meshError.worst<=16&&meshError.mean<=rasterError.mean,"The ring mesh follows the exact stroke coverage within 16/255, closer on average than the raster it replaces");
    }
   }
  };
  parity(renderer,440,1);
  Renderer twice;twice.initialize(window.hwnd,880,880,{Driver::warpForTests,shader,RenderTarget::offscreenForTests});parity(twice,880,2);twice.reset();
 }check(!IsWindowVisible(window.hwnd)&&renderer.stats().presents==0,"No visible window or real provider was used");}
}
void*operator new(std::size_t size){if(counting)++allocations;if(void*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}void operator delete(void*p)noexcept{std::free(p);}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
int wmain(int argc,wchar_t**argv){const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 1;int code{};try{check(argc==2,"Pass native shader");run(argv[1]);std::cout<<"Work Mode scene: "<<checks<<" checks passed\n";}catch(const std::exception&e){counting=false;std::cerr<<"Work Mode scene after "<<checks<<": "<<e.what()<<'\n';code=1;}CoUninitialize();return code;}
#else
int main(){return 0;}
#endif
