#include "native/layer_scene.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <iostream>
#include <stdexcept>

using namespace endfield::native;
using ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
Json leaf(std::string id,double r,double g,double b){return Json::Object{
    {"id",std::move(id)},{"kind","layer"},{"class","CALayer"},{"bounds",Json::Array{0,0,16,16}},
    {"position",Json::Array{8,8}},{"anchorPoint",Json::Array{0,0}},
    {"backgroundColor",Json::Object{{"sRGB",Json::Array{r,g,b,1}}}}};}
Json root(Json::Array children){return Json::Object{{"bounds",Json::Array{0,0,32,32}},{"children",std::move(children)}};}
void pixel(const Readback& image,unsigned x,unsigned y,std::array<int,4> expected){
    const auto start=std::size_t(y)*image.rowBytes+x*4;
    for(unsigned i=0;i<4;++i)check(std::abs(int(image.pixels.at(start+i))-expected[i])<=1,"Projected native surface has the expected color and opacity");
}
class Window {
public:
    Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldLayerSceneOwnedFixture";
        atom=RegisterClassW(&c);if(!atom)throw std::runtime_error("Cannot register fixture");
        hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned isolated layer fixture",WS_POPUP,0,0,32,32,nullptr,nullptr,c.hInstance,nullptr);
        if(!hwnd)throw std::runtime_error("Cannot create fixture");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}
    HWND hwnd{};ATOM atom{};
};
void run(const std::filesystem::path& shader){
    Window window;Renderer renderer;renderer.initialize(window.hwnd,32,32,{Driver::warpForTests,shader,RenderTarget::offscreenForTests});
    renderer.setCamera(layerViewportProjection(32,32));LayerRasterizer raster;LayerRasterOptions options;options.pixelsPerPoint=1;options.paddingPoints=1;
    {
        LayerScene scene(raster);auto red=leaf("color",1,0,0);red["zPosition"]=10;scene.load(root({red}),options);scene.upload(renderer);renderer.draw(false);
        pixel(renderer.readback(),16,16,{0,0,255,255});check(scene.report().surfaces==1&&raster.stats().entries==1,"One visible local subtree retains one surface");
        const auto r0=raster.stats();const auto g0=renderer.stats();
        for(unsigned i=0;i<120;++i)scene.present(renderer,endfield::core::Matrix4::translation(i%2,0));
        const auto r1=raster.stats();const auto g1=renderer.stats();
        check(r0.rasterizations==r1.rasterizations&&r0.textLayoutsCreated==r1.textLayoutsCreated&&r0.imageDecodes==r1.imageDecodes,"Pointer placement performs no raster, text layout, or image decode");
        check(g0.textureUploads==g1.textureUploads&&g0.meshUploads==g1.meshUploads&&g0.objectBufferAllocations==g1.objectBufferAllocations,"Pointer placement retains every texture, mesh and object buffer");
        check(scene.surfaceIndex("color")==0&&!scene.surfaceIndex("unknown"),"Native binding IDs resolve once to stable local indices");
        const auto original=scene.draws()[0].world;
        LayerPlacement placement{0,endfield::core::Matrix4::translation(2,0)*original,.5f,{}};
        scene.setPlacements(std::span(&placement,1));scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,128,128});
        auto invalidPlacement=placement;invalidPlacement.surface=100;bool placementFailed=false;
        const std::array update{LayerPlacement{0,original,1,{}},invalidPlacement};
        try{scene.setPlacements(update);}catch(const std::exception&){placementFailed=true;}
        check(placementFailed,"Late invalid placement rejects the entire pose before mutation");scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,128,128});
        const PlaneMask emptyMask{{},{0,0,0,10}};
        auto emptyMasked=placement;emptyMasked.masks=std::span(&emptyMask,1);bool maskFailed=false;
        try{scene.setPlacements(std::span(&emptyMasked,1));}catch(const std::exception&){maskFailed=true;}
        check(maskFailed,"An empty clip rejects the complete placement before mutation");scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,128,128});
        placement.world=original;placement.opacity=1;scene.setPlacements(std::span(&placement,1));
        bool duplicateFailed=false;try{scene.load(root({leaf("color",0,1,0),leaf("color",0,0,1)}),options);}catch(const std::exception&){duplicateFailed=true;}
        check(duplicateFailed,"Duplicate local cache identities are rejected before rasterization");scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,255,255});
        auto broken=leaf("invalid",0,0,1);broken["bounds"]=Json::Array{0,0,-1,10};
        bool failed=false;try{scene.load(root({leaf("color",0,1,0),broken}),options);}catch(const std::exception&){failed=true;}
        check(failed,"A malformed second leaf rejects the complete scene revision");scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,0,255,255});
        scene.load(root({leaf("color",0,0,1)}),options);scene.upload(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{255,0,0,255});
        check(raster.stats().entries==1,"Retry after partial failure does not reuse stale content or leak cached surfaces");
        {
            LayerScene other(raster);other.load(root({leaf("color",0,1,0)}),options);other.upload(renderer);renderer.draw(false);pixel(renderer.readback(),16,16,{0,255,0,255});
            check(raster.stats().entries==2,"Two scene owners may use the same source layer name safely");other.detach(renderer);
        }
        check(raster.stats().entries==1,"Destroying a detached scene releases only its own local surfaces");scene.upload(renderer);
        auto first=leaf("clock",1,0,0),second=leaf("caption",0,0,1);
        first["bounds"]=Json::Array{0,0,8,8};first["position"]=Json::Array{2,2};
        second["bounds"]=Json::Array{0,0,8,8};second["position"]=Json::Array{20,20};
        scene.load(root({first,second}),options);scene.upload(renderer);
        const auto structureRevision=scene.contentRevision();
        const PlaneMask clockMask{{},{0,0,16,16}};
        const LayerPlacement clockPlacement{*scene.surfaceIndex("clock"),endfield::core::Matrix4::translation(3,3),.5f,std::span(&clockMask,1)};
        scene.setPlacements(std::span(&clockPlacement,1));scene.present(renderer);
        const auto unchangedCaption=scene.draws()[1];
        const auto beforeLocalRaster=raster.stats();const auto beforeLocalGPU=renderer.stats();
        first["backgroundColor"]=Json::Object{{"sRGB",Json::Array{0,1,0,1}}};
        check(scene.updateLocalContent("clock",1,first,options),"A changed clock updates its retained local surface");
        scene.upload(renderer);scene.present(renderer);renderer.draw(false);
        pixel(renderer.readback(),6,6,{0,128,0,128});pixel(renderer.readback(),24,24,{255,0,0,255});
        const auto afterLocalRaster=raster.stats();const auto afterLocalGPU=renderer.stats();
        check(afterLocalRaster.rasterizations==beforeLocalRaster.rasterizations+1&&afterLocalRaster.entries==2,"A clock tick rasterizes one surface and keeps bounded local storage");
        check(afterLocalGPU.textureUploads==beforeLocalGPU.textureUploads+1&&afterLocalGPU.meshUploads==beforeLocalGPU.meshUploads&&afterLocalGPU.objectBufferAllocations==beforeLocalGPU.objectBufferAllocations,"Clock content uploads one texture without rebuilding other labels or geometry");
        check(scene.contentRevision()==structureRevision&&scene.draws()[0].world.values==clockPlacement.world.values&&scene.draws()[0].opacity==.5f&&scene.draws()[0].masks.size()==1&&scene.draws()[1].world.values==unchangedCaption.world.values,"Local content preserves structural bindings, placement, opacity, masks and sibling placement");
        check(!scene.updateLocalContent("clock",1,first,options)&&raster.stats().rasterizations==afterLocalRaster.rasterizations,"The same caller content revision performs no rasterization");
        const auto priorNodes=scene.report().sourceNodes;
        auto wrong=first;wrong["id"]="caption";
        bool wrongFailed=false;try{scene.updateLocalContent("clock",2,wrong,options);}catch(const std::exception&){wrongFailed=true;}
        auto projective=leaf("projective-child",1,1,1);projective["zPosition"]=1;wrong=first;wrong["children"]=Json::Array{projective};
        bool projectedFailed=false;try{scene.updateLocalContent("clock",2,wrong,options);}catch(const std::exception&){projectedFailed=true;}
        wrong=first;wrong["bounds"]=Json::Array{0,0,-1,8};
        bool emptyFailed=false;try{scene.updateLocalContent("clock",2,wrong,options);}catch(const std::exception&){emptyFailed=true;}
        check(wrongFailed&&projectedFailed&&emptyFailed&&scene.report().sourceNodes==priorNodes,"Invalid local identity, projection or bounds leaves retained content unchanged");
        scene.upload(renderer);scene.present(renderer);renderer.draw(false);pixel(renderer.readback(),6,6,{0,128,0,128});
        const auto beforeResizeGPU=renderer.stats();
        first["bounds"]=Json::Array{0,0,10,8};
        check(scene.updateLocalContent("clock",2,first,options),"A local size change can replace only its own retained quad");scene.upload(renderer);
        check(renderer.stats().meshUploads==beforeResizeGPU.meshUploads+1&&renderer.stats().textureUploads==beforeResizeGPU.textureUploads+1,"A changed local extent rebuilds exactly one mesh and texture");
        for(unsigned i=3;i<103;++i){first["backgroundColor"]=Json::Object{{"sRGB",Json::Array{0,double(i%2),0,1}}};scene.updateLocalContent("clock",i,first,options);scene.upload(renderer);}
        check(raster.stats().entries==2&&renderer.stats().textures==2&&renderer.stats().meshes==2,"Repeated local clock revisions retain bounded CPU and GPU surfaces");
        for(unsigned i=0;i<100;++i){scene.load(root({leaf("revision-"+std::to_string(i),1,0,0)}),options);scene.upload(renderer);}
        check(raster.stats().entries==1&&renderer.stats().textures==1&&renderer.stats().meshes==1,"Changing module identities releases abandoned CPU and GPU surfaces");
        scene.detach(renderer);check(renderer.stats().objects==0&&renderer.stats().textures==0&&renderer.stats().meshes==0,"Detach releases owned native resources");
    }
    check(raster.stats().entries==0,"Scene destruction releases its final local raster");
    check(!IsWindowVisible(window.hwnd),"The fixture never shows or captures a desktop window");
}
}
int wmain(int argc,wchar_t** argv){const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{
    if(FAILED(initialized)||argc!=2)throw std::runtime_error("Expected COM and one shader fixture path");run(std::filesystem::path(argv[1]));
    CoUninitialize();std::cout<<checks<<" layer scene checks passed\n";return 0;
}catch(const std::exception& error){if(SUCCEEDED(initialized))CoUninitialize();std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
