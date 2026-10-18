#include "native/watch_presentation.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

namespace {std::atomic<bool> countAllocations{};std::atomic<std::size_t> allocations{};}
void* operator new(std::size_t n) {
    if(countAllocations.load(std::memory_order_relaxed))allocations.fetch_add(1,std::memory_order_relaxed);
    if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#endif

using namespace endfield::core;
using namespace endfield::core::source;
using namespace endfield::native;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,message);}
template<class F>std::size_t measured(F&& action){allocations=0;countAllocations=true;try{action();}catch(...){countAllocations=false;throw;}countAllocations=false;return allocations.load();}

class OwnedWindow {
public:
    OwnedWindow(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldWatchPresentationSynthetic";
        atom_=RegisterClassW(&c);if(!atom_)throw std::runtime_error("Cannot register owned hidden fixture");
        window_=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned label presentation oracle",WS_POPUP,0,0,200,100,nullptr,nullptr,c.hInstance,nullptr);
        if(!window_)throw std::runtime_error("Cannot create owned hidden fixture");}
    ~OwnedWindow(){if(window_)DestroyWindow(window_);if(atom_)UnregisterClassW(MAKEINTATOM(atom_),GetModuleHandleW(nullptr));}
    HWND get()const{return window_;}
private:HWND window_{};ATOM atom_{};
};
SceneDefinition sourceScene(){
    Node root;root.id="root";root.name="root";root.children={"button"};
    Node button;button.id="button";button.name="Button";button.parent="root";button.children={"caption"};
    Node caption;caption.id="caption";caption.name="Caption";caption.parent="button";
    caption.rect=RectTransform{{0,0},{0,0},{0,0},{2,1},{.5,.5}};
    return SceneDefinition("root",{root,button,caption});
}
Json leaf(const char* id,double r,double g,double b){return Json::Object{
    {"id",id},{"class","CALayer"},{"kind","layer"},{"bounds",Json::Array{0,0,2,1}},
    {"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},
    {"backgroundColor",Json::Object{{"sRGB",Json::Array{r,g,b,1}}}}};}
Json layers(bool reordered=false){
    auto caption=leaf("caption.surface",1,0,0),other=leaf("other.surface",0,1,0);
    other["position"]=Json::Array{190,90};
    return Json::Object{{"bounds",Json::Array{0,0,200,100}},
        {"children",reordered?Json::Array{other,caption}:Json::Array{caption,other}}};
}
NativeLabelPlan labels(const SceneDefinition& scene){
    return NativeLabelPlan(scene,{{"caption.surface","caption","button",NativeLabelKind::caption,false,false}});
}
void checkPlacement(const LayerScene& layers,const NativeLabelPlacement& expected){
    const auto index=layers.surfaceIndex(expected.surfaceID);check(index.has_value(),"Current layer source identity resolves");
    const auto& draw=layers.draws()[*index];
    check(draw.world==expected.world&&draw.opacity==expected.opacity,"Published GPU placement and opacity equal the source plan");
    check(draw.masks.size()==expected.masks.size(),"Every exact source clip plane reaches the native surface");
    for(std::size_t i=0;i<draw.masks.size();++i)
        check(draw.masks[i].worldToLocal==expected.masks[i].worldToLocal&&draw.masks[i].bounds==expected.masks[i].bounds,"Native clip homography is neither dropped nor applied twice");
}
void alphaAt(const Readback& image,unsigned x,unsigned y,unsigned expected){
    check(image.pixels.at(std::size_t(y)*image.rowBytes+x*4+3)==expected,"Owned rendered pixel obeys original projected clipping");
}
void retainedLabels(const std::filesystem::path& shader){
    OwnedWindow window;Renderer renderer;
    renderer.initialize(window.get(),200,100,{Driver::warpForTests,shader,RenderTarget::offscreenForTests});
    renderer.setCamera(layerViewportProjection(200,100));
    LayerRasterizer raster;LayerScene local(raster);LayerRasterOptions options;
    options.pixelsPerPoint=4;options.paddingPoints=1;
    local.load(layers(),options);local.upload(renderer);
    const auto scene=sourceScene();auto nodes=SourceLayout(scene).resolve();std::vector<double> alpha(nodes.size(),1);
    CameraFrame camera;camera.worldRoot.values[12]=.125;const Rect viewport{0,0,200,100};
    SourceWatchFrame frame;frame.resolved=nodes;frame.inheritedAlpha=alpha;
    SourceWatchHit hit;hit.graphicID="graphic";hit.buttonID="button";hit.rect={{-1,-.5},{2,1}};
    hit.masks.push_back({{{-.5,-.25},{1,.5}},camera.worldRoot});frame.hits.push_back(std::move(hit));
    const std::array availability{WatchButtonAvailability{"button",true,false}};
    auto plan=labels(scene);
    const std::array sourceMask{NativeClipPlane{frame.hits[0].masks[0].rect,frame.hits[0].masks[0].world}};
    const NativeLabelButtonState sourceButton{true,true,false,sourceMask};
    check(plan.update(nodes,alpha,std::span(&sourceButton,1),camera,viewport),"Source plan is already primed before bridge construction");
    WatchLabelPresentation bridge(plan,local);
    bool published=false;
    const auto firstAllocations=measured([&]{published=bridge.update(frame,camera,viewport,availability);});
    check(published&&firstAllocations==0,"First bridge publication happens without allocation even when source plan is unchanged");
    check(bridge.contentBoundsChanged().size()==1&&bridge.contentBoundsChanged()[0]==0,"First publication reports the retained local content bounds");
    bridge.present(renderer);checkPlacement(local,plan.placements()[0]);renderer.draw(false);
    const auto firstPixels=renderer.readback();alphaAt(firstPixels,112,50,255);alphaAt(firstPixels,25,50,0);
    const auto rasterBefore=raster.stats();const auto gpuBefore=renderer.stats();
    const auto planBefore=plan.stats();
    const auto idleAllocations=measured([&]{for(unsigned i=0;i<120;++i)check(!bridge.update(frame,camera,viewport,availability),"Unchanged bridge update does not republish placements");});
    check(idleAllocations==0&&bridge.contentBoundsChanged().empty(),"Idle bridge performs no allocation or local content revision");
    check(plan.stats().projectionComputations==planBefore.projectionComputations,"Idle bridge preserves projection cache");
    const auto pointerAllocations=measured([&]{for(unsigned i=0;i<120;++i){
        camera.worldRoot.values[12]=.001*i;
        frame.hits[0].masks[0].world=camera.worldRoot;
        check(bridge.update(frame,camera,viewport,availability),"Changed pointer pose publishes fresh projected placement");
        check(bridge.contentBoundsChanged().empty(),"Pointer pose never changes the local caption raster bounds");
    }});
    check(pointerAllocations==0,"Full changed-pointer bridge path has no allocation after construction");
    // Check the complete mathematical bridge + retained GPU object submission,
    // in addition to resource counters: reuse alone would miss CPU list churn.
    const auto presentationAllocations=measured([&]{for(unsigned i=0;i<120;++i){
        camera.worldRoot.values[12]=.002*i;frame.hits[0].masks[0].world=camera.worldRoot;
        bridge.update(frame,camera,viewport,availability);bridge.present(renderer);
    }});
    checkPlacement(local,plan.placements()[0]);
    const auto rasterAfter=raster.stats();const auto gpuAfter=renderer.stats();
    check(rasterAfter.rasterizations==rasterBefore.rasterizations&&rasterAfter.textLayoutsCreated==rasterBefore.textLayoutsCreated&&
        rasterAfter.imageDecodes==rasterBefore.imageDecodes,"Pointer presentation neither rasterizes nor shapes text nor decodes images");
    check(gpuAfter.textureUploads==gpuBefore.textureUploads&&gpuAfter.meshUploads==gpuBefore.meshUploads&&
        gpuAfter.objectBufferAllocations==gpuBefore.objectBufferAllocations,"Pointer presentation retains GPU textures meshes and object buffers");
    check(gpuAfter.objects==gpuBefore.objects&&gpuAfter.textures==gpuBefore.textures&&gpuAfter.meshes==gpuBefore.meshes,
        "Pointer presentation does not grow native resources");
    check(presentationAllocations==0,"Complete pointer update and native GPU submission allocate no memory after warmup");
    renderer.draw(false);const auto priorPixels=renderer.readback();const auto priorUploads=renderer.stats().objectUploads;
    auto invalidDraws=std::vector<DrawObject>(local.draws().begin(),local.draws().end());
    invalidDraws[0].opacity=.125f;invalidDraws.back().masks.push_back({{},Rect{0,0,0,10}});
    rejects([&]{renderer.setDrawList(invalidDraws);},"Late invalid mask rejects the entire retained GPU object update");
    check(renderer.stats().objectUploads==priorUploads,"Validation failure cannot upload earlier valid object changes");
    renderer.draw(false);
    check(renderer.readback().pixels==priorPixels.pixels,"Rejected retained update preserves every prior owned render-target pixel");

    const auto oldIndex=local.surfaceIndex("caption.surface");
    local.load(layers(true),options);local.upload(renderer);
    check(local.surfaceIndex("caption.surface")!=oldIndex,"Explicit content revision reorders surfaces");
    check(bridge.update(frame,camera,viewport,availability),"Content revision rebinds source IDs even when the source pose is unchanged");
    bridge.present(renderer);checkPlacement(local,plan.placements()[0]);
    check(local.draws()[*local.surfaceIndex("other.surface")].world.values[12]==190,
        "Rebinding does not overwrite the other reordered surface");
    check(measured([&]{for(unsigned i=0;i<120;++i)bridge.update(frame,camera,viewport,availability);})==0,
        "Unchanged content-revision path resumes allocation-free updates");
    auto missing=layers();missing["children"]=Json::Array{leaf("other.surface",0,1,0)};
    local.load(missing,options);
    rejects([&]{bridge.update(frame,camera,viewport,availability);},"Missing binding after content reload fails explicitly");
    local.load(layers(),options);local.upload(renderer);
    check(bridge.update(frame,camera,viewport,availability),"Valid later revision recovers after a missing binding");
    bridge.present(renderer);checkPlacement(local,plan.placements()[0]);

    // A fresh plan can return changed=false when every caption starts hidden.
    // The bridge still must hide the previously visible exported local surface.
    nodes[2].activeInHierarchy=false;auto hiddenPlan=labels(scene);
    WatchLabelPresentation initiallyHidden(hiddenPlan,local);
    check(initiallyHidden.update(frame,camera,viewport,availability),"Initially hidden unchanged plan still publishes once");
    initiallyHidden.present(renderer);checkPlacement(local,hiddenPlan.placements()[0]);
    check(local.draws()[*local.surfaceIndex("caption.surface")].opacity==0,"First hidden frame cannot leak exported visible caption");
    local.detach(renderer);
    check(renderer.stats().objects==0&&renderer.stats().textures==0&&renderer.stats().meshes==0,"Owned label resources detach cleanly");
    check(!IsWindowVisible(window.get()),"Synthetic WARP fixture remains hidden and never captures desktop pixels");
}
void accent(){
    const std::array<float,4> raw{.8f,.6f,.1f,.375f};
    const std::optional<std::array<float,3>> replacement=std::array<float,3>{.2f,.1f,.02f};
    const auto mapped=WatchMaterialPresentation::vertexTint(raw,true,replacement);
    check(mapped==std::array<float,4>{.2f*.8f,.1f*.8f,.02f*.8f,.375f},"Vertex tint maps original yellow once while preserving authored intensity and alpha");
    check(raw==std::array<float,4>{.8f,.6f,.1f,.375f},"Raw source tint remains immutable across presentations");
    check(WatchMaterialPresentation::vertexTint(mapped,true,replacement)!=mapped,"Chosen accent fixture detects accidental double mapping");
    for(unsigned i=0;i<120;++i)check(WatchMaterialPresentation::vertexTint(raw,true,replacement)==mapped,"Repeated frames use the original color rather than accumulating theme multiplication");
    check(WatchMaterialPresentation::vertexTint(raw,false,replacement)==raw,"Profile opt-out preserves source artwork");
    check(WatchMaterialPresentation::vertexTint(raw,true,{})==raw,"Absent accent leaves source colors untouched");
    const std::array<float,4> neutral{.75f,.75f,.75f,.4f};
    check(WatchMaterialPresentation::vertexTint(neutral,true,replacement)==neutral,"Neutral artwork is not recolored");
    const std::array<float,4> hdr{3,2,.2f,.5f};
    check(WatchMaterialPresentation::vertexTint(hdr,true,replacement)==std::array<float,4>{.2f*3,.1f*3,.02f*3,.5f},"Source HDR intensity is not clamped by accent mapping");
    const std::array<float,4> edge{1,.25f,.125f,1};
    check(WatchMaterialPresentation::vertexTint(edge,true,replacement)==edge,"Strict blue threshold matches source yellow classification");
    const auto invalid=std::optional(std::array<float,3>{std::numeric_limits<float>::quiet_NaN(),1,0});
    check(WatchMaterialPresentation::vertexTint(raw,true,invalid)==raw,"Nonfinite accent is not propagated");
}
}
int wmain(int argc,wchar_t** argv){
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{
        check(SUCCEEDED(initialized)&&argc==2,"Expected COM and one explicit hud.hlsl path");
        accent();retainedLabels(std::filesystem::path(argv[1]));
        CoUninitialize();std::cout<<"PASS "<<checks<<" source watch presentation checks\n";return 0;
    }catch(const std::exception& error){countAllocations=false;if(SUCCEEDED(initialized))CoUninitialize();
        std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
