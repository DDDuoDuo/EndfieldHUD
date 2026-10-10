#include "native/calendar_scene.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <new>
#include <set>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#endif
namespace{std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace{using namespace endfield;namespace m=modules;namespace gpu=native;using J=m::CalendarJson;unsigned checks{};
void check(bool b,const char*message){++checks;if(!b)throw std::runtime_error(message);}
m::CalendarCanvasInput input(){m::CalendarCanvasInput i;i.today=i.selected={2026,10,4};i.month={2026,10,1};i.monthHeading="October 2026";return i;}
void identities(const J&j,std::set<std::string>&ids){check(ids.insert(j["id"].string()).second,"Deduplicated Calendar asset nodes retain unique raster identities");for(const auto&c:j["children"].array())identities(c,ids);}
void plans(){for(bool dark:{false,true})for(int month:{2,10,12}){auto i=input();i.appearance.dark=dark;i.month={2026,month,1};const auto art=m::prepareCalendarCanvas(i);const auto p=gpu::prepareCalendarScene(art);check(p.assetCount<=56&&p.draws.size()<=128,"A full source month retains compact spans and shared day artwork");std::set<std::string>ids;identities(p.assets,ids);std::optional<std::size_t>tint,rim;unsigned dates{};for(const auto&d:p.draws){check(d.asset<p.assetCount&&d.local.finite(),"Every Calendar ordered alias has a finite resident asset binding");if(!d.feedback)continue;const auto&f=p.feedback[*d.feedback];const auto&node=p.assets["children"].array()[d.asset]["children"].array()[0];const auto&position=node["position"].array();check(d.local.values[12]+position[0].number()==f.rect.x&&d.local.values[13]+position[1].number()==f.rect.y,"Integer-padded source raster preserves the exact original control origin");if(!f.action.starts_with("day:"))continue;++dates;if(d.rim){if(rim)check(*rim==d.asset,"All day rim ink shares one source path raster");rim=d.asset;}else{if(tint)check(*tint==d.asset,"All day tint ink shares one source path raster");tint=d.asset;}}check(dates==i.month.daysInMonth()*2&&tint!=rim,"All dates retain independent ordered tint and rim aliases");}
 for(bool deleting:{false,true}){m::CalendarMenuInput i;i.editing=true;i.deleting=deleting;const auto p=gpu::prepareCalendarScene(m::prepareCalendarMenu(i));check(p.assetCount<=12&&p.draws.size()<=18,"Source event menu retains compact shadow/body/border and exact control order");const auto&tail=p.assets["children"].array()[p.draws.back().asset];check(!tail["children"].array().empty()&&tail["children"].array().back()["borderWidth"].number()==.7,"Calendar menu face border remains after its children and highlights");}
 auto bad=m::prepareCalendarCanvas(input());bad.root["mask"]=J::Object{{"bounds",J::Array{0,0,400,440}}};bool rejected{};try{(void)gpu::prepareCalendarScene(bad);}catch(...){rejected=true;}check(rejected,"Calendar rejects unsupported local source masks before any scene mutation");}
#ifdef _WIN32
struct Window{HWND hwnd{};Window(){hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"Owned hidden Calendar scene fixture",WS_POPUP,0,0,400,440,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Calendar scene fixture remains hidden and owns its HWND");}~Window(){if(hwnd)DestroyWindow(hwnd);}};
// Oracle: the exact Direct2D bitmap of the whole original source tree, i.e.
// the raster LayerScene uploads for this root (raster-group settings), in the
// renderer's premultiplied encoded BGRA readback layout.
//
// Root cause of the former one-alpha-sample allowance (116a/117): it was not
// the retained split. At local 350,385 (the cut-corner join of the reminders
// rim) the split reproduces this bitmap (64 vs 64); the renderer's own
// composite of the full-tree surface does not (61). On WARP that surface is one
// 402x442 textured quad and that pixel centre lies 0.025 px from its
// (-1,-1)->(401,441) triangle seam, where a neighbouring texel is blended in at
// any screen offset. The source rim raster at its own and at the reference
// device origin, with layer or brush opacity, all give the same coverage.
// referenceComposite() pins this diagnosis; the split itself keeps the original
// 8-bit staging bound of 2 everywhere against the bitmap.
gpu::Readback sourceBitmap(gpu::LayerRasterizer&raster,const m::CalendarArtwork&art,gpu::LayerRasterOptions options,bool dark){
 options.includeRootOpacity=false;options.includeRootMask=true;const std::string id=std::string("calendar-source-bitmap/")+(dark?"dark":"light");const auto image=raster.rasterize(id,1,art.root,options);
 check(options.pixelsPerPoint==1&&image->bounds.x==-options.paddingPoints&&image->bounds.y==-options.paddingPoints&&image->bounds.x==std::floor(image->bounds.x)&&image->complete(),"Calendar source bitmap is complete on the shared integer device grid");
 const auto ox=unsigned(-image->bounds.x),oy=unsigned(-image->bounds.y);gpu::Readback out;out.width=unsigned(art.bounds.width);out.height=unsigned(art.bounds.height);out.rowBytes=out.width*4;out.pixels.assign(std::size_t(out.rowBytes)*out.height,0);
 check(image->width>=out.width+ox&&image->height>=out.height+oy,"Calendar source bitmap covers the compared target");
 for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x){const auto*p=&image->straightRGBA[(std::size_t(y+oy)*image->width+x+ox)*4];auto*q=&out.pixels[std::size_t(y)*out.rowBytes+x*4];for(unsigned c=0;c<3;++c)q[c]=std::uint8_t((unsigned(p[2-c])*p[3]+127)/255);q[3]=p[3];}
 raster.remove(id);return out;
}
unsigned channelDelta(const gpu::Readback&a,const gpu::Readback&b,unsigned x,unsigned y){unsigned worst{};for(unsigned c=0;c<4;++c)worst=std::max(worst,unsigned(std::abs(int(a.pixels[std::size_t(y)*a.rowBytes+x*4+c])-int(b.pixels[std::size_t(y)*b.rowBytes+x*4+c]))));return worst;}
void referenceComposite(const gpu::Readback&source,const gpu::Readback&composite,double padding,bool dark){
 check(composite.width==source.width&&composite.height==source.height,"Reference composite uses the compared target size");unsigned seam{},worst{};const double width=source.width+2*padding,height=source.height+2*padding;
 for(unsigned y=0;y<source.height;++y)for(unsigned x=0;x<source.width;++x){const auto d=channelDelta(source,composite,x,y);if(d<=1)continue;worst=std::max(worst,d);check(std::abs((y+.5+padding)-(x+.5+padding)*height/width)<.05,"Renderer composite of the full source bitmap departs from it only on its quad triangle seam");++seam;}
 std::cout<<"Calendar "<<(dark?"dark":"light")<<" renderer composite of the full source bitmap: "<<seam<<" seam pixel(s) beyond 1 (maximum "<<worst<<"); the split is compared with the bitmap itself\n";
}
std::uint64_t difference(const gpu::Readback&source,const gpu::Readback&actual){
 check(actual.width==source.width&&actual.height==source.height&&actual.pixels.size()>=std::size_t(actual.rowBytes)*actual.height,"Calendar comparison uses the same retained target size");
 // Original bound: separate 8-bit leaf, group and output stages may differ by 2.
 std::uint64_t bad{};for(unsigned y=0;y<source.height;++y)for(unsigned x=0;x<source.width;++x)bad+=channelDelta(source,actual,x,y)>2;return bad;
}
void diagnostic(const gpu::Readback&a,const gpu::Readback&b,bool dark){const auto prefix=std::string("calendar-split-")+(dark?"dark":"light");for(const auto*which:{&a,&b}){const auto name=prefix+(which==&a?"-expected.bgra":"-actual.bgra");std::ofstream file(name,std::ios::binary);file.write(reinterpret_cast<const char*>(which->pixels.data()),static_cast<std::streamsize>(which->pixels.size()));}std::array<unsigned,4>maximum{};unsigned count{},minX=a.width,minY=a.height,maxX{},maxY{};for(unsigned y=0;y<a.height;++y)for(unsigned x=0;x<a.width;++x){bool bad{};for(unsigned c=0;c<4;++c){const auto delta=unsigned(std::abs(int(a.pixels[std::size_t(y)*a.rowBytes+x*4+c])-int(b.pixels[std::size_t(y)*b.rowBytes+x*4+c])));maximum[c]=std::max(maximum[c],delta);bad|=delta>2;}if(bad){++count;minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x);maxY=std::max(maxY,y);}}std::cerr<<"Calendar differing pixels="<<count<<" bounds="<<minX<<','<<minY<<'-'<<maxX<<','<<maxY<<" maxBGRA="<<maximum[0]<<','<<maximum[1]<<','<<maximum[2]<<','<<maximum[3]<<" diagnostic="<<prefix<<"-{expected,actual}.bgra 400x440\n";}
void nativeScene(const std::filesystem::path&shader){Window w;gpu::Renderer renderer;renderer.initialize(w.hwnd,400,440,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(400,440));gpu::LayerRasterizer raster;gpu::LayerRasterOptions options;options.pixelsPerPoint=1;gpu::NativeCalendarScene scene(raster,options);gpu::LayerComposition composition;gpu::LayerScene reference(raster);double time{};std::uint64_t revision{};std::vector<gpu::LayerCompositionEntry>published;published.reserve(2);
 const auto frame=[&](double t,const core::Matrix4&world=core::Matrix4{},float opacity=1){time=t;scene.updatePose({world,opacity,t,{},{}});scene.uploadResources(renderer);scene.uploadAnimations(renderer);const auto entries=scene.entries();bool changed=entries.size()!=published.size();if(!changed)for(std::size_t n=0;n<entries.size();++n)changed|=entries[n].scene!=published[n].scene||entries[n].after.data()!=published[n].after.data()||entries[n].after.size()!=published[n].after.size();if(changed){composition.setEntries(renderer,entries);published.assign(entries.begin(),entries.end());}composition.present(renderer);renderer.draw(false);scene.collectRetired(renderer);};
 for(bool dark:{false,true}){auto i=input();i.appearance.dark=dark;auto art=m::prepareCalendarCanvas(i);const auto source=sourceBitmap(raster,art,options,dark);reference.load(art.root,options);composition.setScenes(renderer,std::array<gpu::LayerScene*,1>{&reference});composition.present(renderer);renderer.draw(false);referenceComposite(source,renderer.readback(),options.paddingPoints,dark);composition.detach(renderer);published.clear();scene.syncContent(std::move(art),++revision,time+=.2);frame(time);const auto actual=renderer.readback();const auto error=difference(source,actual);if(error){std::cerr<<"Calendar canvas split-byte differences beyond 2 from the source bitmap="<<error<<" dark="<<dark<<'\n';diagnostic(source,actual,dark);}check(error==0,"Retained Calendar alias/group output equals the full original source tree bitmap within 8-bit staging");composition.detach(renderer);published.clear();reference.releaseResources(renderer);}
 scene.setFeedback(core::Point{40,120},false,time+=.1);frame(time);frame(time+.15);const auto beforeRaster=raster.stats();const auto beforeGPU=renderer.stats();allocations=0;counting=true;try{for(unsigned n=0;n<120;++n)frame(time+1./60,core::Matrix4::translation(0,0)*core::Matrix4::rotation(0,.000001*n,0),.8f);}catch(...){counting=false;throw;}counting=false;check(allocations==0,"Calendar steady root tilt/fade allocates no owner storage");check(raster.stats().rasterizations==beforeRaster.rasterizations&&raster.stats().textLayoutsCreated==beforeRaster.textLayoutsCreated&&renderer.stats().textureUploads==beforeGPU.textureUploads&&renderer.stats().meshUploads==beforeGPU.meshUploads&&renderer.stats().nativeGroupRenders==beforeGPU.nativeGroupRenders,"Root tilt/fade reuses all Calendar glyphs, rasters, resources and cached group output");
 // Remove and settle local hover first: a newly built month legitimately
 // animates inherited hover ink independently of the root face crossfade.
 scene.setFeedback({},false,time+=.1);frame(time);frame(time+.15);
 auto next=input();next.month={2026,11,1};next.monthHeading="November 2026";scene.syncContent(m::prepareCalendarCanvas(next),++revision,time+=.1,true);frame(time);check(scene.entries().size()==2&&scene.requiresFrames(time),"Original .18 second whole-face transition retains one outgoing and one incoming group");
 // The shared renderer composites a dirty group only on a frame where its
 // published output has positive opacity. At transition start the incoming
 // face is exactly transparent, so its one initial child composite belongs to
 // the first visible crossfade frame. After that, every crossfade frame must
 // change only the two root output constants.
 const auto hidden=renderer.stats().nativeGroupRenders;frame(time+.01);check(renderer.stats().nativeGroupRenders==hidden+1,"Incoming Calendar face composites its source children exactly once, on its first visible crossfade frame");
 const auto outputs=renderer.stats().nativeGroupRenders;frame(time+.08);check(renderer.stats().nativeGroupRenders==outputs,"Whole-face crossfade changes root constants without recompositing source children");frame(time+.10);check(scene.entries().size()==1&&!scene.requiresFrames(time)&&scene.stats().retiredParts==0,"Calendar finite face transition settles and retires outgoing artwork after replacement publication");
 const auto accepted=scene.artwork().root;auto invalid=m::prepareCalendarCanvas(next);invalid.root["mask"]=J::Object{};bool rejected{};try{scene.syncContent(std::move(invalid),++revision,time);}catch(...){rejected=true;}check(rejected&&scene.artwork().root==accepted,"Invalid replacement preserves previously published Calendar artwork");composition.detach(renderer);check(scene.releaseResources(renderer)&&renderer.stats().textures==0&&renderer.stats().meshes==0&&renderer.stats().nativeGroups==0,"Calendar resources retire fully through the shared publisher without sibling ownership");renderer.reset();}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==2,"Pass shared shader to hidden Calendar native fixture");plans();nativeScene(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" Calendar scene checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){try{plans();std::cout<<"PASS "<<checks<<" Calendar retained-plan checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
