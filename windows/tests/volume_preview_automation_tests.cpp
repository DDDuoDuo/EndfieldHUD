#ifdef _WIN32
#include "tools/volume_preview.hpp"
#include "native/module_scene.hpp"
#include "native/volume_automation.hpp"
#include "native/volume_strings.hpp"
#include "core/module_presentation.hpp"
#include <uiautomation.h>
#include <wrl/client.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Projected accessibility of the Volume owner: the canvas -> client mapping
// agrees with an independently computed module plane and with pointer hit
// testing, and UI Automation drives the same source actions. Hidden owner,
// injected callbacks: no window is shown and no audio provider is opened.
namespace {
namespace gpu=endfield::native;namespace core=endfield::core;namespace tools=endfield::tools;namespace app=endfield::app;
using Json=ehud::data::Json;using Microsoft::WRL::ComPtr;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
std::wstring property(IRawElementProviderFragment*f,PROPERTYID id){
    ComPtr<IRawElementProviderSimple>s;check(SUCCEEDED(f->QueryInterface(IID_PPV_ARGS(&s))),"Simple provider");VARIANT v;VariantInit(&v);s->GetPropertyValue(id,&v);
    std::wstring out=v.vt==VT_BSTR&&v.bstrVal?std::wstring(v.bstrVal,SysStringLen(v.bstrVal)):std::wstring{};VariantClear(&v);return out;
}
gpu::VolumeSnapshot base(){
    gpu::VolumeSnapshot s;s.outputs={{"endpoint-1","Speakers"},{"endpoint-2","Headphones",true}};s.inputs={{"mic","Microphone"}};s.outputID="endpoint-1";s.inputID="mic";
    s.volume=.55;s.muted=false;s.canSetVolume=s.canSetMute=true;s.applicationActivitySupported=true;
    s.applications.push_back({"40:1","Player",40,true});return s;
}
void run(){
    gpu::LayerRasterizer raster;auto data=base();tools::VolumePreview*owner{};std::vector<std::string>calls;
    tools::VolumePreviewOptions options;options.initial=data;options.rasterDensity=1;options.strings=gpu::volumeStrings(core::Language::english);
    options.actions.setActive=[](bool){};
    options.actions.setVolume=[&](std::string_view endpoint,double value){calls.push_back("volume "+std::string(endpoint)+" "+std::to_string(value));data.volume=value;owner->receiveSnapshot(data);return true;};
    options.actions.setMute=[&](std::string_view endpoint,bool value){calls.push_back("mute "+std::string(endpoint)+(value?" on":" off"));data.muted=value;owner->receiveSnapshot(data);return true;};
    tools::VolumePreview preview(raster,std::move(options));owner=&preview;
    preview.resize({1920,1200,144,1.5,1280,800});
    check(!preview.acceptsInput()&&!preview.clientRect({0,0,400,334})&&!preview.perform("audio:mute",0)&&!preview.setAccessibleValue("volume",.3,0)&&calls.empty(),"Before Volume is presented there are no projected controls and no actions");
    core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::volume;core::ModulePresentation modules(core::Module::volume);double time{};
    const auto sample=modules.sample(time).presentation;preview.update({},settings,sample,1,time);
    check(preview.acceptsInput(),"Presented Volume accepts input");

    // Oracle: the module plane computed independently (as the pointer fixture does).
    gpu::LayerScene geometry(raster);gpu::LayerRasterOptions fixtureOptions;geometry.load(Json::Object{{"bounds",Json::Array{0,0,400,334}},{"children",Json::Array{}}},fixtureOptions);
    gpu::NativeModuleSurface surface(geometry,core::Module::volume);surface.update({},settings,sample.current,1);
    const auto plane=core::Projection::viewport(gpu::layerViewportProjection(1920,1200)*core::Matrix4::scale(1.5,1.5)*surface.pose().contentWorld,1920,1200);
    const auto expected=[&](core::Rect r){double x0=INFINITY,y0=INFINITY,x1=-INFINITY,y1=-INFINITY;
        for(const core::Point c:{core::Point{r.x,r.y},core::Point{r.x+r.width,r.y},core::Point{r.x,r.y+r.height},core::Point{r.x+r.width,r.y+r.height}}){const auto q=plane.project(c);check(q.has_value(),"Oracle plane projects");x0=std::min(x0,q->x);y0=std::min(y0,q->y);x1=std::max(x1,q->x);y1=std::max(y1,q->y);}
        return core::Rect{x0/1.5,y0/1.5,(x1-x0)/1.5,(y1-y0)/1.5};};
    const auto sameRect=[](core::Rect a,core::Rect b){return std::abs(a.x-b.x)<1e-6&&std::abs(a.y-b.y)<1e-6&&std::abs(a.width-b.width)<1e-6&&std::abs(a.height-b.height)<1e-6;};
    for(const auto&a:preview.state().actions()){const auto r=preview.clientRect(a.rect);check(r&&sameRect(*r,expected(a.rect)),"Button rects follow the module plane in logical client points (DPI scale removed)");}
    for(const auto&s:preview.state().sliders()){const auto r=preview.clientRect(s.visibleRect.value_or(s.rect));check(r&&sameRect(*r,expected(s.visibleRect.value_or(s.rect))),"Slider rects follow the module plane");}
    const auto canvas=*preview.clientRect(gpu::VolumeController::bounds());
    check(canvas.width>100&&canvas.height>100&&canvas.x>=0&&canvas.y>=0&&canvas.x+canvas.width<=1280&&canvas.y+canvas.height<=800,"The canvas projects inside the logical client area");
    check(!preview.clientRect({0,0,-1,1})&&!preview.clientRect({NAN,0,1,1}),"Invalid canvas rects are refused");

    // The UIA rect agrees with pointer hit testing on the same artwork.
    const auto actions=preview.state().actions();const auto mute=std::find_if(actions.begin(),actions.end(),[](const auto&a){return a.id=="audio:mute";});
    check(mute!=actions.end(),"Mute is a source action");const auto muteCanvas=mute->rect; // the controller rebuilds its storage on every snapshot
    const auto muteRect=*preview.clientRect(muteCanvas);const core::Point centre{muteRect.x+muteRect.width/2,muteRect.y+muteRect.height/2};
    check(preview.covers(centre)&&preview.pointer({app::PointerKind::down,app::PointerButton::left,centre.x,centre.y},++time),"Pointer at the accessible rect's centre lands on the Volume artwork");
    preview.pointer({app::PointerKind::up,app::PointerButton::left,centre.x,centre.y},time);
    check(calls.size()==1&&calls[0]=="mute endpoint-1 on","The accessible Mute rect is the projected Mute control");
    check(preview.perform("audio:mute",++time)&&calls.back()=="mute endpoint-1 off","perform() is the source projected button press");
    check(!preview.perform("audio:output",++time)&&calls.size()==2,"A disabled source action (default-device chooser) is refused");
    check(preview.setAccessibleValue("volume",1.7,++time)&&calls.back()=="volume endpoint-1 1.000000","Accessible slider values clamp to the source range like NSSlider");
    check(!preview.setAccessibleValue("volume",NAN,++time)&&!preview.setAccessibleValue("balance",.2,++time)&&calls.size()==3,"Nonfinite values and unavailable balance are refused");

    // UI Automation over the owner, exactly as the host wires it.
    gpu::NativeVolumeAutomation tree({nullptr,
        [&]{return preview.acceptsInput()?gpu::volumeAccessibility(preview.state(),gpu::volumeAccessibilityStrings(core::Language::english)):gpu::VolumeAccessibility{};},
        [&](core::Rect r){return preview.clientRect(r);},
        [&](std::string_view id){return preview.perform(id,time+=.01);},
        [&](std::string_view id,double value){return preview.setAccessibleValue(id,value,time+=.01);}});
    check(tree.refresh()&&tree.size()==preview.state().actions().size()+preview.state().sliders().size(),"Every projected control is listed");
    ComPtr<IRawElementProviderFragment>hit;check(SUCCEEDED(tree.fromPoint(centre.x,centre.y,&hit))&&hit&&property(hit.Get(),UIA_AutomationIdPropertyId)==L"audio:mute","UIA hit testing agrees with the pointer");
    ComPtr<IRawElementProviderFragment>volume;for(std::size_t i=0;i<tree.size();++i){ComPtr<IRawElementProviderFragment>f;tree.child(i,&f);if(property(f.Get(),UIA_AutomationIdPropertyId)==L"volume")volume=f;}
    ComPtr<IRangeValueProvider>range;check(volume&&SUCCEEDED(volume.As(&range))&&SUCCEEDED(range->SetValue(.4))&&calls.back()=="volume endpoint-1 0.400000","RangeValue drives the owner's slider");
    ComPtr<IRawElementProviderFragment>headphones;for(std::size_t i=0;i<tree.size();++i){ComPtr<IRawElementProviderFragment>f;tree.child(i,&f);if(property(f.Get(),UIA_AutomationIdPropertyId)==L"audio:headphones")headphones=f;}
    ComPtr<IInvokeProvider>invoke;check(headphones&&SUCCEEDED(headphones.As(&invoke))&&SUCCEEDED(invoke->Invoke())&&preview.state().headphones(),"Invoke opens Headphones / Bluetooth");
    check(tree.refresh()&&tree.stats().disconnected==1,"The app slider leaves the tree with its page");

    // Leaving Volume hides every projected control.
    settings.module=core::Module::power;modules.select(core::Module::power,++time);preview.update({},settings,modules.sample(time+.05).presentation,1,time+.05);
    check(!preview.acceptsInput()&&!preview.clientRect(muteCanvas)&&!preview.perform("audio:mute",time+.1),"A departing Volume exposes and performs nothing");
    check(tree.refresh()&&tree.size()==0,"UIA elements are removed when Volume leaves");
}
}
int main(){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr),"Owned test STA");run();CoUninitialize();std::cout<<"PASS "<<checks<<" Volume owner accessibility checks (hidden owner; no audio)\n";return 0;}catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
