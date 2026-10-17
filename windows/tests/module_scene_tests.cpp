#include "native/module_scene.hpp"
#include "core/module_transform.hpp"
#include "core/source_camera.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#endif
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {std::atomic<bool> counting{};std::atomic<std::size_t> allocations{};unsigned checks{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using namespace endfield::core;
using namespace endfield::core::source;
using namespace endfield::native;
using ehud::data::Json;
namespace {
void check(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
void checkNear(double a,double b,const char* message){check(std::isfinite(a)&&std::abs(a-b)<=1e-9*std::max(1.,std::abs(b)),message);}
void equalMatrix(const Matrix4&a,const Matrix4&b,const char*message){for(unsigned i=0;i<16;++i)checkNear(a.values[i],b.values[i],message);}
template<class F>void rejects(F f,const char*message){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,message);}
Json leaf(const char*id,double x,double y,double alpha=1){return Json::Object{{"id",id},{"kind","layer"},{"class","CALayer"},
    {"bounds",Json::Array{0,0,16,16}},{"position",Json::Array{x,y}},{"anchorPoint",Json::Array{0,0}},
    {"opacity",alpha},{"backgroundColor",Json::Object{{"sRGB",Json::Array{1,0,0,1}}}}};}
Json root(double lateX=25){return Json::Object{{"bounds",Json::Array{0,0,100,100}},{"masksToBounds",true},
    {"children",Json::Array{leaf("a",7,9,.5),leaf("b",lateX,12)}}};}
ModulePresentationSurface stable(Module module){ModulePresentation state(module);return state.sample(0).presentation.current;}
DesktopChromeSettings settings(Module requested=Module::power){DesktopChromeSettings out;out.viewport={0,0,1280,800};out.module=requested;return out;}
Projection screenProjection(const Matrix4& world){const auto&m=world.values;return {{m[0],m[4],m[12],m[1],m[5],m[13],m[3],m[7],m[15]}};}
void geometry(LayerRasterizer&raster,const LayerRasterOptions&options){
    LayerScene layers(raster);layers.load(root(),options);const auto original=layers.draws()[0];
    NativeModuleSurface surface(layers,Module::power);auto s=stable(Module::power);auto config=settings(Module::map);
    check(surface.update({},config,s,.8f),"First module pose always publishes");layers.prepareDraws();
    const auto&pose=surface.pose();
    const auto prefix=Matrix4::translation(500,297)*Matrix4::scale(1.14,1.14);
    equalMatrix(pose.hostWorld,prefix*Matrix4::translation(-220,-220),"Latest requested Map controls the shared host scale/origin");
    equalMatrix(pose.contentWorld,prefix*Matrix4::translation(-200,-168),"Outgoing Power keeps its own20,52content-frame offset");
    equalMatrix(layers.draws()[0].world,pose.contentWorld*original.world,"Retained child local placement is composed exactly once");
    checkNear(layers.draws()[0].opacity,.4f,"Source child opacity and outer canvas fade multiply once");
    check(layers.draws()[0].masks.size()==2,"Existing content clip plus untransformed host clip retained");
    equalMatrix(layers.draws()[0].masks[0].worldToLocal,original.masks[0].worldToLocal*inverseSourceMatrix(pose.contentWorld),"Original local ancestor mask follows content projection");
    equalMatrix(layers.draws()[0].masks.back().worldToLocal,inverseSourceMatrix(pose.hostWorld),"Host viewport clip excludes animated wrapper transform");
    check(layers.draws()[0].masks.back().bounds==Rect{0,0,440,440},"Host clipping uses the authored440square");
    check(!pose.shutter&&!pose.registration,"Stable modules emit neither stale shutters nor seams");
    check(!surface.update({},config,s,.8f)&&surface.stats().unchangedUpdates==1,"Unchanged module input is a retained no-op");
    // Every global requested module must remain independent from this Power
    // surface; original own local frame never follows the pending selection.
    for(unsigned i=0;i<24;++i){config.module=static_cast<Module>(i);surface.update({},config,s);const auto layout=DesktopChromeLayout::make(config,{},{});
        equalMatrix(surface.pose().contentWorld,Matrix4::translation(500,layout.reportCenterY)*Matrix4::scale(layout.reportScale,layout.reportScale)*Matrix4::translation(-200,-168),"All pending selections retain outgoing surface geometry");}
    const auto center=nativeProjectiveTextTransform({Point{40,30},Point{1060,42},Point{1015,680},Point{25,640}},{1000,640});
    surface.update(center,config,s);const auto projection=screenProjection(surface.pose().contentWorld);
    for(const auto local:{Point{0,0},Point{199,167},Point{400,334}}){const auto projected=projection.project(local);check(projected.has_value(),"Source center homography projects local module points");
        const auto back=projection.unproject(*projected);check(back.has_value(),"Source module plane supports inverse input mapping");checkNear(back->x,local.x,"Inverse module plane retains source local X");checkNear(back->y,local.y,"Inverse module plane retains source local Y");}
    const auto previous=surface.pose().contentWorld;
    auto invalid=s;invalid.contentFrame.x=0;rejects([&]{surface.update(center,config,invalid);},"Wrong own-module frame rejects before retained mutation");
    invalid=s;invalid.module=Module::map;rejects([&]{surface.update(center,config,invalid);},"Another module cannot silently reuse this local surface");
    rejects([&]{surface.update(center,config,s,std::numeric_limits<float>::quiet_NaN());},"Invalid opacity rejects before mutation");
    equalMatrix(surface.pose().contentWorld,previous,"Invalid requests leave published module pose unchanged");
    rejects([&]{surface.rebindLocalContent();},"Same-revision projected worlds cannot become local bases");
    layers.load(root(),options);rejects([&]{surface.update(center,config,s);},"Structural reload requires explicit fresh local binding");
    surface.rebindLocalContent();surface.update(center,config,s);layers.prepareDraws();
    equalMatrix(layers.draws()[0].world,surface.pose().contentWorld*original.world,"Explicit reload rebind does not compound the old projection");
}
void transitions(LayerRasterizer&raster,const LayerRasterOptions&options){
    LayerScene layers(raster);layers.load(root(),options);NativeModuleSurface surface(layers,Module::map);
    ModulePresentation state(Module::power);state.select(Module::map,0);auto sample=*state.sample(.125).presentation.incoming;
    auto config=settings(Module::reader);const auto center=Matrix4::translation(30,20)*Matrix4::scale(1.2,1.1);
    surface.update(center,config,sample,.75f);layers.prepareDraws();const auto&pose=surface.pose();
    const auto prefix=center*Matrix4::translation(500,275);
    equalMatrix(pose.wrapperWorld,prefix*sampleModuleTransform(sample.transform)*Matrix4::translation(-220,-220),"Incoming wrapper transform pivots around its authored center");
    equalMatrix(pose.contentWorld,pose.wrapperWorld,"Own Map frame fills440square while pending Reader controls shared host");
    check(pose.shutter&&layers.draws()[0].shutter&&pose.registration,"Incoming source shutter and registration remain explicit");
    equalMatrix(pose.shutter->worldToLocal,inverseSourceMatrix(pose.wrapperWorld),"Shutter uses animated wrapper coordinates");
    equalMatrix(pose.registration->world,pose.wrapperWorld,"Source seams follow wrapper rather than content offset");
    checkNear(pose.registration->local.opacity,sample.registration->opacity,"Registration keeps its separate original opacity track");
    checkNear(pose.registration->parentOpacity,.75,"Registration parent canvas opacity is not baked into its local opacity");
    checkNear(pose.registration->local.lineWidth,.7,"Original registration stroke width retained");
    checkNear(pose.registration->local.colorAlpha,.38,"Original registration color alpha retained");
    const auto before=raster.stats();allocations=0;counting=true;
    try{for(unsigned i=0;i<120;++i){auto current=*state.sample(.125+static_cast<double>(i)*.001).presentation.incoming;
        auto tilted=center;tilted.values[3]=static_cast<double>(i)*1e-7;surface.update(tilted,config,current,.75f);layers.prepareDraws();}}
    catch(...){counting=false;throw;}counting=false;
    const auto after=raster.stats();check(allocations==0,"120 changing projection/transform/shutter/seam updates allocate no storage");
    check(before.rasterizations==after.rasterizations&&before.textLayoutsCreated==after.textLayoutsCreated&&before.resourceBytes==after.resourceBytes,"Module motion never redraws local textures or layout");
    auto settled=state.sample(.3).presentation.current;surface.update(center,config,settled);layers.prepareDraws();
    check(!surface.pose().shutter&&!surface.pose().registration&&!layers.draws()[0].shutter,"Completed transition clears wrapper shutter and registration without content reload");
    const auto previous=layers.draws()[0].world;auto invalid=settled;invalid.shutter=ShutterPath{};
    (*invalid.shutter)[5]={{{0,0},{1,0},{.2,.2},{1,1},{0,1}}};
    rejects([&]{surface.update(center,config,invalid);},"Invalid late shutter polygon fails before child placement mutation");layers.prepareDraws();
    equalMatrix(layers.draws()[0].world,previous,"Rejected mask leaves every retained child world unchanged");
}
void lateRollback(LayerRasterizer&raster,const LayerRasterOptions&options){
    LayerScene layers(raster);layers.load(root(1e38),options);NativeModuleSurface surface(layers,Module::power);
    auto s=stable(Module::power);const auto config=settings();surface.update({},config,s);layers.prepareDraws();
    const std::array previous{layers.draws()[0].world,layers.draws()[1].world};const auto pose=surface.pose().contentWorld;
    rejects([&]{surface.update(Matrix4::scale(100,100),config,s);},"Late child GPU-range overflow rejects complete module pose");layers.prepareDraws();
    equalMatrix(layers.draws()[0].world,previous[0],"Late failure leaves earlier child untouched");equalMatrix(layers.draws()[1].world,previous[1],"Late failure leaves final child untouched");
    equalMatrix(surface.pose().contentWorld,pose,"Late failure preserves the externally observable module pose");
    surface.update(Matrix4::translation(1,0),config,s);layers.prepareDraws();checkNear(layers.draws()[0].world.values[12],previous[0].values[12]+1,"Valid retry recomputes complete staged placement after failure");
}
}
int main(){
#ifdef _WIN32
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com)){std::cerr<<"COM initialization failed\n";return 1;}
#endif
    int result=0;try{LayerRasterizer raster;LayerRasterOptions options;options.pixelsPerPoint=1;geometry(raster,options);transitions(raster,options);lateRollback(raster,options);
        check(raster.stats().entries==0,"Borrowed scene destruction releases only its own local raster cache");std::cout<<"PASS "<<checks<<" module surface checks; no window, GPU, providers or capture\n";
    }catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';result=1;}
#ifdef _WIN32
    CoUninitialize();
#endif
    return result;
}
