#include "native/app_shortcut_scene.hpp"
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <cstdlib>
#include <new>
#include <cmath>
#include <algorithm>
static bool counting{};static std::size_t allocations{};
void*operator new(std::size_t size){if(counting)++allocations;if(auto*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}void operator delete(void*p)noexcept{std::free(p);}void*operator new[](std::size_t size){return ::operator new(size);}void operator delete[](void*p)noexcept{::operator delete(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
namespace {namespace n=endfield::native;namespace m=endfield::modules;namespace c=endfield::core;unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Window{HWND hwnd{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"OwnedShortcutScene";check(RegisterClassW(&c)!=0,"Register synthetic Shortcut fixture");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,c.lpszClassName,L"Isolated",WS_POPUP,0,0,400,334,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Fixture stays hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"OwnedShortcutScene",GetModuleHandleW(nullptr));}};
m::ShortcutFile items(){m::ShortcutFile f;for(unsigned k=0;k<20;++k){m::ShortcutRecord v;char id[37];snprintf(id,sizeof(id),"00000000-0000-4000-8000-%012u",k+1);v.id=id;v.name="Neutral "+std::to_string(k);v.originalName="Original";v.iconPreset=m::shortcutIconOrder[k%14];f.items.push_back(std::move(v));}return f;}
void run(const std::filesystem::path&shader,const std::filesystem::path&root){Window window;n::Renderer renderer;renderer.initialize(window.hwnd,400,334,{n::Driver::warpForTests,shader,n::RenderTarget::offscreenForTests});renderer.setCamera(n::layerViewportProjection(400,334));n::LayerRasterizer raster;n::NativeAppShortcutAssets assets(root);n::LayerRasterOptions options;options.assetRoot=root;options.pixelsPerPoint=1;m::AppShortcutState state(items());state.setActive(true,0);n::NativeAppShortcutScene scene(state,assets,raster,options);n::LayerComposition composition;bool done{};struct Cleanup{n::Renderer&r;n::LayerComposition&c;n::NativeAppShortcutScene&s;bool&done;~Cleanup(){if(done)return;try{c.detach(r);s.releaseResources(r);}catch(...){r.reset();}}}cleanup{renderer,composition,scene,done};
 const auto frame=[&](double t,c::Matrix4 world=c::Matrix4{}){state.advance(t);scene.syncContent(t);scene.updatePose({world,1,t,{},{}});scene.uploadAnimations(renderer);composition.setEntries(renderer,scene.entries());composition.present(renderer);check(scene.collectRetired(renderer),"Retire only detached Shortcut generations");};
 // The production host publishes, collects every owner and presents afterwards (Application::publishNative).
 const auto hostFrame=[&](double t){state.advance(t);scene.syncContent(t);scene.updatePose({c::Matrix4{},1,t,{},{}});scene.uploadAnimations(renderer);composition.setEntries(renderer,scene.entries());check(scene.collectRetired(renderer),"Collecting before the next present retires nothing still published");composition.present(renderer);};
 // The canvas scenes in paint order (an underlay entry carrying the GPU plate meshes precedes each).
 const auto canvases=[&]{std::size_t count{};for(const auto&e:scene.entries())count+=e.after.empty();return count;};
 frame(0);renderer.draw(false);check(scene.stats().surfaces<100,"List virtualizes seven original rows rather than full store");
 // Plain card and button plates (a CALayer background with a corner radius) are GPU meshes under the list, in their
 // own order: six retained cards with their Edit and Remove plates, and + Choose app.
 {const auto entries=scene.entries();check(scene.stats().meshPlates==19&&entries.size()==2&&entries[0].after.size()==19&&entries[1].after.empty()&&canvases()==1,"Card and button plates are GPU meshes under the list canvas");
  for(const auto&d:entries[0].after)check(d.textureID.empty()&&!d.meshID.empty()&&d.linearTint[3]>0&&d.linearTint[3]<1,"A plate mesh draws its translucent source colour without a bitmap");}
 // The same pixels as the source canvas painted leaf by leaf: a reference scene rasters every visible leaf (plates at
 // full size with their borders, captions at their full bounds, feedback hidden) in paint order, and the canvas
 // matches it above `bottom` (the list viewport clips its last card): every caption, glyph and plate interior, every
 // straight plate edge and tile border within 8-bit rounding. Only the pixels of the 3-point rounded corners differ
 // by the antialiasing model: the meshes ramp the coverage across one pixel along the arc's normal (close to the
 // covered area), D2D covers a little less or more of such a tight arc. Measured on WARP at one pixel a point: the
 // list's translucent plates 2 levels; the draft's opaque Save plate 16 (0.95 against 0.89 coverage) and the
 // selected tile's 1.5-point border around its 1.5-point inner arc 23 (each at a single corner pixel).
 struct Worst {unsigned x{},y{};std::array<int,8>pixels{};}; // the canvas's and the reference's RGBA there
 struct Match {int straight{},corners{},rings{};std::size_t pixels{},plates{},bordered{},referenceBytes{};std::array<Worst,3>worst{};};
 const auto match=[&](unsigned bottom){Match out;renderer.draw(false);const auto meshed=renderer.readback();
  const auto plan=m::prepareAppShortcutArtwork(state,assets.artwork(),{},{});ehud::data::Json::Array children;struct Plate{c::Rect r;double border{};};std::vector<Plate>plates;
  for(const auto&leaf:plan){if(leaf.role==m::ShortcutSurfaceRole::tint||leaf.role==m::ShortcutSurfaceRole::rim)continue;auto content=leaf.content;content["position"]=ehud::data::Json::Array{leaf.local.values[12],leaf.local.values[13]};
   if(leaf.id.ends_with("/plate")){const auto&b=leaf.content["bounds"].array();const auto&w=leaf.content["borderWidth"];plates.push_back({{leaf.local.values[12],leaf.local.values[13],b[2].number(),b[3].number()},w.isNumber()&&!leaf.content["borderColor"].isNull()?w.number():0.});}
   children.push_back(std::move(content));}
  {n::LayerScene reference(raster);reference.load(ehud::data::Json::Object{{"bounds",ehud::data::Json::Array{0,0,400,334}},{"children",std::move(children)}},options);check(reference.report().unsupported.empty(),"The reference canvas rasters every leaf");out.referenceBytes=reference.report().pixelBytes;
   const std::array referenceEntries{n::LayerCompositionEntry{&reference,{}}};composition.setEntries(renderer,referenceEntries);composition.present(renderer);renderer.draw(false);const auto rastered=renderer.readback();
   // The canvas returns to the composition before the reference scene ends.
   composition.setEntries(renderer,scene.entries());composition.present(renderer);renderer.draw(false);
   check(rastered.width==meshed.width&&rastered.height==meshed.height&&meshed.width==400&&meshed.height==334,"Both canvases read back at the same size");
   for(unsigned y=0;y<std::min(bottom,meshed.height);++y)for(unsigned x=0;x<meshed.width;++x){int most{};for(unsigned k=0;k<4;++k){const auto at=std::size_t(y)*meshed.rowBytes+x*4+k;most=std::max(most,std::abs(int(meshed.pixels[at])-int(rastered.pixels[at])));}
    int*bucket=&out.straight;const double px=x+.5,py=y+.5; // the pixel centre in points (one pixel a point)
    for(const auto&p:plates){const double x0=p.r.x,y0=p.r.y,x1=p.r.x+p.r.width,y1=p.r.y+p.r.height;if(px<x0-1||px>x1+1||py<y0-1||py>y1+1)continue;
     if((px<x0+3||px>x1-3)&&(py<y0+3||py>y1-3)){bucket=&out.corners;break;}
     if(p.border>0&&std::min({px-x0,x1-px,py-y0,y1-py})<=p.border+1){bucket=&out.rings;break;}}
    if(most>*bucket){auto&w=out.worst[bucket==&out.straight?0:bucket==&out.corners?1:2];w.x=x;w.y=y;for(unsigned k=0;k<4;++k){const auto at=std::size_t(y)*meshed.rowBytes+x*4+k;w.pixels[k]=meshed.pixels[at];w.pixels[4+k]=rastered.pixels[at];}}
    *bucket=std::max(*bucket,most);++out.pixels;}}
  out.plates=plates.size();out.bordered=static_cast<std::size_t>(std::count_if(plates.begin(),plates.end(),[](const Plate&p){return p.border>0;}));return out;};
 const auto report=[](const char*screen,const Match&v){std::cout<<screen<<" vs raster:";const std::array<const char*,3>names{"straight","corners","borders"};const std::array<int,3>levels{v.straight,v.corners,v.rings};
  for(std::size_t n=0;n<3;++n){const auto&w=v.worst[n];std::cout<<" "<<names[n]<<" "<<levels[n];if(levels[n])std::cout<<" (at "<<w.x<<","<<w.y<<": canvas "<<w.pixels[0]<<" "<<w.pixels[1]<<" "<<w.pixels[2]<<" "<<w.pixels[3]<<", reference "<<w.pixels[4]<<" "<<w.pixels[5]<<" "<<w.pixels[6]<<" "<<w.pixels[7]<<")";}
  std::cout<<" levels over "<<v.pixels<<" pixels\n";};
 {const auto list=match(296);check(list.plates==16&&list.bordered==0,"Five visible cards with their Edit and Remove plates, and + Choose app");
  report("list",list);
  check(list.straight<=2,"Mesh plates and narrow captions paint the source canvas (8-bit rounding only)");check(list.corners<=4,"Mesh plate corners stay within a few levels of the rastered antialiasing");
  // Captions rasterize the width their glyphs take: the list keeps far fewer pixels than the same leaves at full size.
  check(scene.stats().textMeasures>0&&scene.stats().pixelBytes*2<list.referenceBytes,"The list rasterizes its captions at their glyphs' width and no plate bitmap");
  std::cout<<"list resident "<<scene.stats().pixelBytes<<" bytes in "<<scene.stats().rasterSurfaces<<" rasters (the same leaves rastered at full size: "<<list.referenceBytes<<")\n";}check(renderer.readback().width==400,"Original Shortcut artwork renders on existing device");for(const auto&e:scene.entries())check(e.scene->report().unsupported.empty(),"No silently unsupported source artwork");
 scene.setFeedback(c::Point{270,2},false,false,.01);frame(.01);check(!scene.requiresFrames(.01),"Source rounded control corner does not light up");scene.setFeedback(c::Point{295,17},true,false,.1);frame(.17);const auto rasters=raster.stats();const auto gpu=renderer.stats();allocations=0;counting=true;try{for(unsigned k=0;k<120;++k)frame(1+double(k)/60,c::Matrix4::rotation(.01,.02*k/120,0));}catch(...){counting=false;throw;}counting=false;check(allocations==0,"Warm Shortcut pose and uploads allocate no memory");check(raster.stats().rasterizations==rasters.rasterizations&&renderer.stats().textureUploads==gpu.textureUploads,"Warm Shortcut frames preserve all text and icons");
 // AppShortcutCanvas.scroll repaints the canvas; the retained cards (the six that can meet the list viewport) are only re-placed.
 {const auto paints=raster.stats().rasterizations,uploads=renderer.stats().textureUploads;const auto before=scene.stats();
  state.scroll({80,110},13.25);frame(3);check(scene.stats().builds==before.builds&&scene.stats().replacements==before.replacements+1&&raster.stats().rasterizations==paints&&renderer.stats().textureUploads==uploads,"A wheel step that brings in the retained neighbour card paints and uploads nothing");
  state.scroll({80,110},52);frame(3.1);const auto recycled=raster.stats().rasterizations-paints;check(scene.stats().builds==before.builds&&recycled>0&&recycled<=3&&renderer.stats().textureUploads-uploads==recycled&&scene.stats().leafRepaints-before.leafRepaints==recycled,"A card entering the window repaints only the recycled card's own leaves");
  state.scroll({80,110},-52);frame(3.2);check(scene.stats().builds==before.builds&&scene.stats().surfaces==before.surfaces,"Scrolling back recycles that card again within the same canvas");}
 // A hover/press tint and rim hold a transparent point until their feedback
 // shows (a CALayer background keeps no bitmap on the Mac): moving onto a
 // card paints only its two feedback leaves, and leaving it gives their
 // bitmaps back once they faded out (.14 s).
 {const auto idle=scene.stats();check(idle.feedbackPaints==2&&idle.feedbackReleases==0,"Only the pressed + Choose app control painted its tint and rim");
  scene.setFeedback(c::Point{100,70},false,false,3.3);frame(3.3);const auto hovered=scene.stats();
  check(hovered.feedbackPaints==idle.feedbackPaints+2&&hovered.pixelBytes>idle.pixelBytes&&hovered.builds==idle.builds&&hovered.leafRepaints==idle.leafRepaints,"Hovering a card paints only its tint and rim");
  scene.setFeedback(c::Point{295,17},true,false,3.35);frame(3.35);frame(3.45);
  check(scene.stats().feedbackReleases==idle.feedbackReleases&&scene.stats().feedbackPaints==hovered.feedbackPaints,"A fading tint keeps its bitmap; the control pressed again still holds its own");
  frame(3.6);const auto left=scene.stats();
  check(left.feedbackReleases==idle.feedbackReleases+2&&left.pixelBytes==idle.pixelBytes&&left.builds==idle.builds,"Faded-out feedback gives its bitmaps back");}
 frame(4);check(state.scrollOffset()==13.25,"Fractional source wheel is preserved");state.beginDraft({"Synthetic candidate"},{},5);frame(5);check(canvases()==2&&scene.entries().size()==4,"Source transition retains outgoing list (and its plates) under incoming draft");const auto incoming=scene.entries().back().scene->draws();const auto heading=std::find_if(incoming.begin(),incoming.end(),[](const auto&v){return v.sourceID.find("apps.heading")!=std::string::npos;});check(heading!=incoming.end(),"Retained source heading is independently addressable");const auto raw=m::shortcutScreenSample(state.transition(),5).incoming;const auto expected=c::Matrix4::translation(200,167)*raw*c::Matrix4::translation(-200,-167)*c::Matrix4::translation(12,7);for(unsigned k=0;k<16;++k)check(std::abs(heading->world.values[k]-expected.values[k])<1e-9,"Source artwork rotates about its default center anchor");const auto before=scene.stats();for(unsigned k=1;k<16;++k)hostFrame(5+double(k)/60);hostFrame(5.26);check(canvases()==1&&scene.stats().meshPlates==18&&scene.entries()[0].after.size()==32&&state.takeNameRequest(),"Arrival retires old list and requests editor exactly once; the draft draws its four button plates and fourteen bordered preset tiles as meshes");check(scene.stats().maskRasters>before.maskRasters&&scene.stats().maskRasters-before.maskRasters<=32,"Finite screen handoff updates at most two bounded source masks per frame");const auto settledRaster=raster.stats().rasterizations,settledUploads=renderer.stats().textureUploads;for(unsigned k=0;k<60;++k)frame(6+double(k)/60);check(raster.stats().rasterizations==settledRaster&&renderer.stats().textureUploads==settledUploads,"Settled Shortcut screens retire masks and do no pixel work");
 // The settled draft: bordered preset tiles (a fill and an inside border ring), button plates and narrow captions.
 // The list's pressed + Choose app still shows its feedback here: the pointer leaves (at once, with reduced motion)
 // so the canvas shows no feedback, as the reference does not.
 scene.setFeedback({},false,true,7);frame(7);scene.setFeedback({},false,false,7);
 {const auto draft=match(334);check(draft.plates==18&&draft.bordered==14,"Four button plates and fourteen bordered preset tiles");
  report("draft",draft);check(draft.straight<=2,"Draft meshes and narrow captions paint the source canvas (8-bit rounding only)");check(draft.rings<=2,"Tile border rings paint the rastered borders (8-bit rounding only)");
  check(draft.corners<=24,"Opaque and bordered plate corners stay within the antialiasing difference of a 3-point arc");
  check(scene.stats().pixelBytes*2<draft.referenceBytes,"The draft keeps no plate or tile bitmap and narrow captions");
  std::cout<<"draft resident "<<scene.stats().pixelBytes<<" bytes in "<<scene.stats().rasterSurfaces<<" rasters (the same leaves rastered at full size: "<<draft.referenceBytes<<")\n";}
 state.selectIcon("camera",7.1);frame(7.1);
 const auto trace=[&]()->const n::DrawObject*{const auto draws=scene.entries().back().scene->draws();const auto found=std::find_if(draws.begin(),draws.end(),[](const auto&v){return v.sourceID.find("apps:icon:camera/register")!=std::string::npos;});return found==draws.end()?nullptr:&*found;};
 check(trace()&&trace()->opacity==0,"New source preset outline starts with no visible ink");const auto presetRasters=raster.stats().rasterizations,presetUploads=renderer.stats().textureUploads;const auto presetBuilds=scene.stats().builds;
 frame(7.19);check(trace()&&trace()->opacity==1&&raster.stats().rasterizations==presetRasters+1&&renderer.stats().textureUploads==presetUploads+1,"Finite source trace repaints only its tiny independent leaf");frame(7.29);
 check(raster.stats().rasterizations==presetRasters+2&&renderer.stats().textureUploads==presetUploads+2&&scene.stats().builds==presetBuilds,"Trace endpoint preserves every sibling caption and icon");check(!scene.requiresFrames(7.4),"Source preview approach and outline end after .18 seconds");
 const auto tracedRaster=raster.stats().rasterizations,tracedUploads=renderer.stats().textureUploads;allocations=0;counting=true;try{for(unsigned k=0;k<20;++k)frame(7.4+double(k)/1000,c::Matrix4::rotation(.01,.01*k/20,0));}catch(...){counting=false;throw;}counting=false;check(allocations==0&&raster.stats().rasterizations==tracedRaster&&renderer.stats().textureUploads==tracedUploads,"Finished preset trace stays retained without allocation or upload");
 state.setDraftName("Updated name");frame(7.5);check(!trace(),"Source content rebuild removes completed trace without resurrecting its old generation");
 state.selectIcon("star",7.6);frame(7.6);state.selectIcon("camera",7.65);frame(7.65);check(trace()&&trace()->opacity==0,"Interrupted preset selection restarts only the newly selected face trace");frame(7.85);check(trace()&&trace()->opacity==1,"Interrupted preset trace reaches its own finite endpoint");state.cancelDraft(8);frame(8);frame(8.13);state.beginDraft({"Newer candidate"},{},8.14);frame(8.14);frame(8.4);check(state.draftName()=="Newer candidate"&&canvases()==1,"Interrupted handoff cannot resurrect an old draft");
 m::ShortcutAppearance light;light.dark=false;light.language=c::Language::korean;scene.setAppearance(light);frame(9);check(state.draftName()=="Newer candidate"&&state.scrollOffset()==13.25,"Appearance change preserves draft and session scroll");
 // A new typography generation (the font language) measures the captions again and rebuilds the canvas before the
 // host could repaint them at widths measured with the earlier fonts.
 {const auto before=scene.stats();check(raster.setDefaultFontLanguage(raster.fontLanguage()==n::LayerFontLanguage::korean?n::LayerFontLanguage::simplifiedChinese:n::LayerFontLanguage::korean),"The font language changes");frame(9.1);
  check(scene.stats().builds==before.builds+1&&scene.stats().textMeasures>before.textMeasures,"A new font generation rebuilds the canvas with fresh caption measurements");}composition.detach(renderer);check(scene.releaseResources(renderer),"Explicit detach releases every Shortcut resource");check(renderer.stats().textures==0&&renderer.stats().meshes==0&&raster.stats().entries<150,"Shortcut resource lifetime remains bounded");frame(10);renderer.draw(false);check(renderer.readback().width==400,"Unchanged Shortcut content uploads again after release");composition.detach(renderer);check(scene.releaseResources(renderer)&&renderer.stats().textures==0&&renderer.stats().meshes==0,"Reuploaded Shortcut resources retire again");
 // Memory (Mac OverlayController releases SystemHUDView on every close): a
 // canvas nobody composes parks - its parts, raster leaves and their CPU
 // pixels leave the shared rasterizer - and the next frame builds the same
 // canvas again; a published canvas refuses and stays resident.
 {const auto held=scene.stats();const auto shared=raster.stats();
  check(held.parts==1&&held.rasterSurfaces>0&&held.pixelBytes>0&&held.surfaces>=held.rasterSurfaces,"The detached canvas still holds its raster artwork");
  check(scene.park(renderer),"A canvas nobody composes parks");const auto parked=scene.stats();const auto released=raster.stats();
  check(parked.parts==0&&parked.rasterSurfaces==0&&parked.pixelBytes==0&&parked.surfaces==0&&parked.retired==0&&parked.parks==held.parks+1&&scene.entries().empty(),"Parking releases every retained part");
  check(released.resourceBytes+held.pixelBytes==shared.resourceBytes&&released.entries+held.rasterSurfaces<=shared.entries,"Parked leaves and their pixels leave the shared rasterizer");
  check(!scene.park(renderer)&&scene.stats().parks==parked.parks,"Nothing more to park");
  frame(11);const auto rebuilt=scene.stats();
  check(rebuilt.parts==1&&rebuilt.builds==held.builds+1&&rebuilt.pixelBytes==held.pixelBytes&&rebuilt.rasterSurfaces==held.rasterSurfaces&&rebuilt.surfaces==held.surfaces,"The next frame builds the same canvas again");
  check(!trace()&&state.draftName()=="Newer candidate","The rebuilt draft replays no finished preset outline");
  check(!scene.park(renderer)&&scene.stats().parts==1&&scene.stats().pixelBytes==held.pixelBytes,"A published canvas refuses to park and stays resident");}
 composition.detach(renderer);check(scene.releaseResources(renderer)&&renderer.stats().textures==0&&renderer.stats().meshes==0,"The rebuilt canvas retires again");done=true;renderer.reset();}
}
int wmain(int argc,wchar_t**argv){auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==3,"Pass native HLSL and original Shortcut resource root");run(argv[1],std::filesystem::absolute(argv[2]));CoUninitialize();std::cout<<"PASS "<<checks<<" native Shortcut scene checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){std::cout<<"Native Shortcut scene requires Windows; no GPU execution on this host\n";return 0;}
#endif
