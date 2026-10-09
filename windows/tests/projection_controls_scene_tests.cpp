#include "native/projection_controls_scene.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <set>
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
namespace native=endfield::native;namespace modules=endfield::modules;namespace core=endfield::core;using Json=ehud::data::Json;
namespace {
unsigned checks{};void check(bool okay,const char*message){++checks;if(!okay)throw std::runtime_error(message);}
void closeEnough(double a,double b,const char*message){check(std::abs(a-b)<1e-8,message);}
template<class F>void rejects(F call,const char*message){bool rejected{};try{call();}catch(const std::exception&){rejected=true;}check(rejected,message);}
std::size_t surface(const native::ProjectionControlsScenePlan&plan,std::string_view id){for(std::size_t n=0;n<plan.surfaces.size();++n)if(plan.surfaces[n].id==id)return n;throw std::runtime_error("Missing Projection test surface");}
void portable(){
    modules::ProjectionControls controls;rejects([&]{native::prepareProjectionControlsScene(controls);},"Uninitialized descriptors reject before compilation");
    for(const auto language:{core::Language::system,core::Language::english,core::Language::simplifiedChinese,core::Language::traditionalChinese,core::Language::japanese,core::Language::korean})for(const auto kind:{modules::ProjectionControlsKind::toolbar,modules::ProjectionControlsKind::brush,modules::ProjectionControlsKind::appearance,modules::ProjectionControlsKind::clearConfirmation}){
        modules::ProjectionControlsInput input;input.language=language;input.kind=kind;controls.update(input);const auto original=controls.artwork();const auto plan=native::prepareProjectionControlsScene(controls);const auto&leaves=plan.layers["children"].array();
        check(controls.artwork()==original,"Compiling never mutates source descriptors");check(plan.layers["id"].isNull(),"Root cannot collapse independent feedback surfaces into one CPU raster");check(plan.layers["masksToBounds"].isNull(),"Floating controls receive no invented clip");check(leaves.size()==plan.surfaces.size(),"One ordered surface for every leaf");
        const auto root=controls.artwork()["id"].string();check(plan.surfaces.front().id==root+"/back"&&plan.surfaces[1].id==root+"/face"&&plan.surfaces.back().id==root+"/face/native-border","Shadow, face, descendants, final border preserve CALayer paint order");
        closeEnough(plan.surfaces.front().local.values[12],-3,"Detached backing keeps source horizontal overflow");closeEnough(plan.surfaces.front().local.values[13],4,"Detached backing keeps source downward overflow");
        check(leaves[1]["borderWidth"].number()==0&&leaves.back()["borderWidth"].number()==.7&&leaves.back()["backgroundColor"].isNull(),"Only final border paints over children");
        std::set<std::string>identities;std::size_t feedbackCount{},inkCount{};
        for(std::size_t n=0;n<plan.surfaces.size();++n){const auto&s=plan.surfaces[n];check(identities.insert(s.id).second,"Every native source identity is distinct");check(leaves[n]["opacity"].number()==1&&leaves[n]["children"].array().empty(),"Artwork stays opaque and flat; numeric feedback owns visibility");check(s.local.values[14]==0&&leaves[n]["zPosition"].number()==0,"Sibling paint order does not become giant geometric depth");
            if(s.feedback!=native::ProjectionControlSurface::none){++feedbackCount;const auto&f=controls.feedback()[s.feedback];check(s.id==(s.rim?f.rimLayerID:f.tintLayerID)&&s.opacity==1,"Feedback uses exact source target identity and separate numeric alpha");}
            if(s.inkLabel!=native::ProjectionControlSurface::none){++inkCount;const auto&label=controls.inkLabels()[s.inkLabel];check(s.id==label.layerID&&leaves[n]["text"]["string"].string()==label.text,"Glyph measurement resolves exact localized source label");check(leaves[n]["projectionInkCentered"].isNull()&&leaves[n]["text"]["alignment"].string()=="center","Native leaf strips private marker and keeps ink inside initial line box");closeEnough(s.local.values[12],label.rect.x,"Unmeasured ink plan retains source item x");closeEnough(s.local.values[13],label.rect.y,"Unmeasured ink plan retains source item y");}
        }
        check(feedbackCount==controls.feedback().size()*2&&inkCount==controls.inkLabels().size(),"Every feedback channel and ink label resolves once");
        if(kind==modules::ProjectionControlsKind::toolbar){check(inkCount==7,"Toolbar centers all seven non-swatch glyphs");const auto lastPlate=surface(plan,"close/plate"),firstLabel=surface(plan,"projection.toolbar.label.brush");check(lastPlate<surface(plan,"color/highlight/tint")&&lastPlate<firstLabel,"Toolbar retains all plates before replacement labels and highlights");check(firstLabel<surface(plan,"brush/highlight/tint"),"Source toolbar feedback paints above its glyph");closeEnough(leaves[0]["cornerRadius"].number(),10,"Toolbar backing uses source radius ten");closeEnough(leaves[surface(plan,"brush/plate")]["cornerRadius"].number(),5,"Toolbar items use source radius five");}
        else {check(inkCount==0,"Ordinary adjustment and confirmation labels keep source line-box placement");check(surface(plan,"close/highlight/rim")<surface(plan,"close/label"),"Framed menu feedback paints below labels");}
        controls.setFeedback("close",true,false);const auto feedbackPlan=native::prepareProjectionControlsScene(controls);check(feedbackPlan.layers==plan.layers,"Feedback events do not change or rebuild raster content");
    }
    controls.update({});const auto toolbar=native::prepareProjectionControlsScene(controls);auto longLeaf=toolbar.layers["children"].array()[surface(toolbar,"projection.toolbar.label.clear")];longLeaf["text"]["string"]=std::string(60,'W');const auto originalLong=longLeaf;
    const auto fullContent=native::prepareProjectionGlyphContent(longLeaf,{600,14,{}});check(longLeaf==originalLong,"Full ink preparation preserves the authored source item descriptor");
    check(fullContent["bounds"].array()[2].number()>600&&fullContent["bounds"].array()[3].number()>14,"Long string probe expands past item width and full line height before rasterization");
    check(fullContent["text"]["string"]==longLeaf["text"]["string"]&&fullContent["text"]["fontSize"]==longLeaf["text"]["fontSize"]&&fullContent["text"]["font"]["familyName"]==longLeaf["text"]["font"]["familyName"]&&fullContent["text"]["font"]["symbolicTraits"]==longLeaf["text"]["font"]["symbolicTraits"],"Probe padding never changes string, glyph size, family or weight");
    check(fullContent["mask"].isNull()&&fullContent["masksToBounds"].isNull(),"Source item clipping is not applied before full ink measurement");
    closeEnough(fullContent["text"]["font"]["ascender"].number()-fullContent["text"]["font"]["descender"].number(),fullContent["bounds"].array()[3].number(),"Padded line metrics stay inside the full probe height");
    rejects([&]{native::prepareProjectionGlyphContent(longLeaf,{9000,14,{}});},"Oversized full glyph descriptors respect the shared raster bound");rejects([&]{native::prepareProjectionGlyphContent(longLeaf,{600,std::numeric_limits<double>::infinity(),{}});},"Nonfinite full glyph metrics reject");
    native::LayerRasterImage wide;wide.bounds={0,0,640,60};wide.width=640;wide.height=60;wide.straightRGBA.resize(640*60*4);for(unsigned y=20;y<30;++y)for(unsigned x=20;x<620;++x)wide.straightRGBA[(y*640+x)*4+3]=255;
    const auto wideOffset=native::projectionGlyphInkOffset(wide,{0,0,54,30});closeEnough(wideOffset->x,-293,"Full long ink centers before the 54-point item clips either end");closeEnough(wideOffset->y,-10,"Full long ink keeps the source vertical center");check(20+wideOffset->x<0&&620+wideOffset->x>54,"Long-string coverage extends beyond both fixed item edges, requiring a separate static clip");
    native::LayerRasterImage image;image.bounds={-1,-2,4,3};image.width=8;image.height=6;image.straightRGBA.resize(8*6*4);
    for(unsigned y=1;y<=3;++y)for(unsigned x=2;x<=4;++x)image.straightRGBA[(y*8+x)*4+3]=255;
    const auto centered=native::projectionGlyphInkOffset(image,{0,0,30,20});check(centered.has_value(),"Actual painted ink is located");closeEnough(centered->x,14.25,"Horizontal centering includes source raster padding and density");closeEnough(centered->y,10.75,"Vertical centering uses painted ink rather than the taller line box");
    const auto moved=native::projectionGlyphInkOffset(image,{11,9,30,20});closeEnough(moved->x,centered->x+11,"Nonzero target origin is supported");closeEnough(moved->y,centered->y+9,"Nonzero target y is supported");
    image.straightRGBA[(5*8+7)*4+3]=1;const auto fringe=native::projectionGlyphInkOffset(image,{0,0,30,20});check(fringe->x<centered->x&&fringe->y<centered->y,"Even faint antialias ink contributes to measured glyph bounds");
    std::fill(image.straightRGBA.begin(),image.straightRGBA.end(),0);image.straightRGBA[0]=255;check(!native::projectionGlyphInkOffset(image,{0,0,30,20}),"Transparent RGB does not count as ink");
    rejects([&]{native::projectionGlyphInkOffset(image,{0,0,0,20});},"Zero target width rejects");rejects([&]{native::projectionGlyphInkOffset(image,{0,0,std::numeric_limits<double>::infinity(),20});},"Nonfinite target rejects");
    image.straightRGBA.pop_back();rejects([&]{native::projectionGlyphInkOffset(image,{0,0,30,20});},"Truncated pixel storage rejects before alpha scan");image.width=0;rejects([&]{native::projectionGlyphInkOffset(image,{0,0,30,20});},"Zero pixel width rejects before division");
}
#ifdef _WIN32
std::size_t surface(const native::LayerScene&scene,std::string_view id){const auto n=scene.surfaceIndex(id);if(!n)throw std::runtime_error("Missing native Projection control");return *n;}
struct Window {HWND hwnd{};ATOM atom{};Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldProjectionControlsSynthetic";atom=RegisterClassW(&c);if(!atom)throw std::runtime_error("Register hidden Projection fixture");hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOREDIRECTIONBITMAP,c.lpszClassName,L"Hidden Projection controls",WS_POPUP,0,0,512,384,nullptr,nullptr,c.hInstance,nullptr);if(!hwnd)throw std::runtime_error("Create hidden Projection fixture");}~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}};
void longGlyph(native::Renderer&renderer,native::LayerRasterizer&raster,const native::LayerRasterOptions&options){
    modules::ProjectionControls source;source.update({});const auto plan=native::prepareProjectionControlsScene(source);auto leaf=plan.layers["children"].array()[surface(plan,"projection.toolbar.label.clear")];leaf["text"]["string"]="WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
    const auto measurement=raster.measureSourceText("long-projection-measure",leaf["text"],30,options);check(measurement.width>100,"Long synthetic text measures full unwrapped advance beyond a narrow source item");leaf=native::prepareProjectionGlyphContent(leaf,measurement);const auto image=raster.rasterize("long-projection-probe",1,leaf,options);check(image->complete(),"Full long glyph probe uses supported shared source text paint");
    const core::Rect item{20,20,30,30};const auto offset=native::projectionGlyphInkOffset(*image,{0,0,item.width,item.height});check(offset&&offset->x<0,"Full long glyph ink centers independently of original cell clipping");
    unsigned left=image->width,right{};for(unsigned y=0;y<image->height;++y)for(unsigned x=0;x<image->width;++x)if(image->straightRGBA[(std::size_t(y)*image->width+x)*4+3]){left=std::min(left,x);right=std::max(right,x+1);}check((right-left)*image->bounds.width/image->width>item.width*3,"Probe retains actual ink well beyond both item boundaries");raster.remove("long-projection-probe");
    native::LayerScene scene(raster);native::NativeLayerGroup group(scene,"long-projection-glyph",options.pixelsPerPoint);scene.load(Json::Object{{"bounds",Json::Array{0,0,512,384}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{leaf}}},options);
    const native::PlaneMask mask{core::Matrix4{},item,0};const native::LayerPlacement placement{0,core::Matrix4::translation(item.x+offset->x,item.y+offset->y),1,std::span(&mask,1)};scene.setPlacements(std::span(&placement,1));group.uploadResources(renderer);group.setPose({},1);native::LayerComposition composition;const std::array entries{group.entry()};composition.setEntries(renderer,entries);renderer.setCamera(native::layerViewportProjection(512,384));composition.present(renderer);renderer.draw(false);const auto frame=renderer.readback();
    std::size_t inside{},outside{};for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x)if(frame.pixels[std::size_t(y)*frame.rowBytes+x*4+3]){if(x>=20&&x<50&&y>=20&&y<50)++inside;else ++outside;}check(inside>0&&outside==0,"Centered full glyph paints only within the original rectangular cell, with no neighboring-cell spill");
    composition.detach(renderer);check(group.releaseResources(renderer),"Long-glyph fixture retires its static masked group");
}
void gpu(const wchar_t*shader){
    Window window;native::Renderer renderer;renderer.initialize(window.hwnd,512,384,{native::Driver::warpForTests,shader,native::RenderTarget::offscreenForTests});
    native::LayerRasterizer raster;native::LayerRasterOptions options;longGlyph(renderer,raster,options);modules::ProjectionControls source;source.update({});native::NativeProjectionControlsScene adapter(source,raster,options);
    rejects([&]{adapter.updatePose({},1,0);},"Native placement requires synchronized content");check(adapter.syncContent()&&!adapter.syncContent(),"Native content is retained until a source/font event");check(adapter.stats().glyphMeasurements==7&&raster.stats().entries==adapter.scene().draws().size(),"Seven event-only glyph probes leave no temporary raster cache entries");
    const auto plan=native::prepareProjectionControlsScene(source);
    for(std::size_t n=0;n<plan.surfaces.size();++n)if(plan.surfaces[n].inkLabel!=native::ProjectionControlSurface::none){const auto id="test-projection-probe:"+std::to_string(n);const auto&label=source.inkLabels()[plan.surfaces[n].inkLabel];const auto&leaf=plan.layers["children"].array()[n];const auto measurement=raster.measureSourceText(id,leaf["text"],label.rect.width,options);const auto image=raster.rasterize(id,1,native::prepareProjectionGlyphContent(leaf,measurement),options);const auto offset=native::projectionGlyphInkOffset(*image,{0,0,label.rect.width,label.rect.height});const auto&draw=adapter.scene().draws()[n];closeEnough(draw.world.values[12],label.rect.x+offset->x,"Retained native glyph is centered from same painted horizontal ink");closeEnough(draw.world.values[13],label.rect.y+offset->y,"Retained native glyph is centered from same painted vertical ink");check(draw.masks.size()==1&&draw.masks[0].bounds==label.rect&&draw.masks[0].worldToLocal==core::Matrix4{}&&draw.masks[0].cornerRadius==0,"Glyph alone retains exact original rectangular item clip in group coordinates");raster.remove(id);}else check(adapter.scene().draws()[n].masks.empty(),"Plate, rim and glow receive no new clipping or corner mask");
    const auto tint=surface(adapter.scene(),"brush/highlight/tint"),rim=surface(adapter.scene(),"brush/highlight/rim"),label=surface(adapter.scene(),"projection.toolbar.label.brush");
    check(adapter.scene().draws()[tint].opacity==0&&adapter.scene().draws()[rim].opacity==0,"Hidden feedback keeps resident artwork without a visible flash");
    const auto world=core::Matrix4::translation(30,20);adapter.updatePose(world,.5f,0);adapter.upload(renderer);check(adapter.entry().after.size()==1&&adapter.entry().after[0].opacity==.5f&&adapter.entry().after[0].world==world,"One group output applies root pose and partial opacity");check(adapter.scene().draws()[label].opacity==1,"Root fade never multiplies overlapping leaf alpha");
    native::LayerComposition composition;const std::array entries{adapter.entry()};composition.setEntries(renderer,entries);renderer.setCamera(native::layerViewportProjection(512,384));composition.present(renderer);renderer.draw(false);
    const auto beforeRaster=raster.stats();const auto beforeGPU=renderer.stats();adapter.setFeedback("brush",false,false,1);adapter.updatePose(world,.5f,1.07);composition.present(renderer);
    check(adapter.scene().draws()[tint].opacity>0&&adapter.scene().draws()[tint].opacity<.62f&&adapter.requiresFrames(1.07),"Source hover advances independent finite numeric tint");
    adapter.setFeedback("brush",true,false,1.07);adapter.updatePose(world,.5f,1.131);composition.present(renderer);check(adapter.scene().draws()[tint].opacity==1&&adapter.scene().draws()[rim].opacity==1,"Press accelerates both channels with source .06 duration");
    adapter.setFeedback("eraser",false,false,2);adapter.updatePose(world,.5f,2.03);const auto releasing=adapter.scene().draws()[tint].opacity;adapter.setFeedback("eraser",true,false,2.03);adapter.updatePose(world,.5f,2.04);check(adapter.scene().draws()[tint].opacity>0&&adapter.scene().draws()[tint].opacity<releasing,"Unrelated press preserves the old control's release fade");
    adapter.setFeedback({},false,true,2.1);adapter.updatePose(world,.5f,2.1);check(!adapter.requiresFrames(2.1)&&adapter.scene().draws()[tint].opacity==0,"Reduced motion settles feedback immediately");
    allocations=0;counting=true;try{for(unsigned n=0;n<100;++n){adapter.updatePose(core::Matrix4::translation(30+n%3,20+n%2),float(n%9)/10,3+n*.001);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==beforeRaster.rasterizations&&raster.stats().textLayoutsCreated==beforeRaster.textLayoutsCreated&&renderer.stats().textureUploads==beforeGPU.textureUploads,"Feedback/pose/fade frames allocate no CPU storage and rasterize or upload no textures");
    rejects([&]{adapter.updatePose({},1,1);},"Backward owner clock rejects");const auto old=adapter.entry().after[0].world;rejects([&]{adapter.updatePose({},1.1f,4);},"Invalid root opacity rejects");check(adapter.entry().after[0].world==old,"Rejected pose preserves published group output");
    raster.setDefaultFontLanguage(native::LayerFontLanguage::korean);check(adapter.syncContent()&&adapter.stats().glyphMeasurements==14,"A shared font event remeasures source toolbar ink");adapter.updatePose(world,.5f,4);adapter.upload(renderer);composition.setEntries(renderer,entries);composition.present(renderer);
    modules::ProjectionControlsInput menu;menu.kind=modules::ProjectionControlsKind::appearance;source.update(menu);adapter.syncContent();adapter.updatePose(world,.4f,5);adapter.upload(renderer);composition.setEntries(renderer,entries);composition.present(renderer);renderer.draw(false);check(adapter.scene().draws().back().sourceID.ends_with("projection.appearance/face/native-border"),"Content replacement preserves final native menu border");
    check(!adapter.releaseResources(renderer),"A published group cannot release its borrowed composition resources");composition.detach(renderer);check(adapter.releaseResources(renderer)&&renderer.stats().meshes==0&&renderer.stats().textures==0&&!IsWindowVisible(window.hwnd),"Detached native controls release without visible windows or GPU leaks");renderer.reset();
}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==2,"Pass the shader path to isolated Windows Projection controls fixture");portable();gpu(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" Projection native controls checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){try{portable();std::cout<<"PASS "<<checks<<" Projection controls scene compiler checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
