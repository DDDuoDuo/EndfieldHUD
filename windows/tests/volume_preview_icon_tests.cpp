#ifdef _WIN32
#include "tools/volume_preview.hpp"
#include "core/module_presentation.hpp"
#include <objbase.h>
#include <array>
#include <iostream>
#include <stdexcept>

// App-row icons in the Volume owner over an injected icon plan and a private
// LayerImageSource: no Shell worker, file, process or audio access.
namespace {
namespace gpu=endfield::native;namespace core=endfield::core;namespace tools=endfield::tools;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
gpu::VolumeSnapshot snapshot(unsigned apps){
    gpu::VolumeSnapshot s;s.outputs={{"o","Speakers"}};s.outputID="o";s.volume=.5;s.canSetVolume=true;s.muted=false;s.canSetMute=true;s.applicationActivitySupported=true;
    for(unsigned n=0;n<apps;++n){gpu::VolumeApplication a;a.id=std::to_string(100+n)+":7";a.name="App "+std::to_string(n);a.pid=100+n;a.available=true;
        gpu::AudioApplicationExecutable e;e.path="C:\\explicit-synthetic\\app-"+std::to_string(n)+".exe";e.identity.objectID[0]=static_cast<std::uint8_t>(n+1);e.identity.volumeSerial=1;a.executable=e;s.applications.push_back(std::move(a));}
    return s;
}
bool hasIcon(const gpu::VolumeController&c,std::size_t row){
    const auto plan=gpu::prepareVolumeScene(c);const auto id="volume/app-row/"+std::to_string(row)+"/icon";
    for(const auto&n:plan.layers["children"].array())if(n["id"].string()==id)return n["contents"]["memoryImage"].isString();
    return false;
}
void run(){
    gpu::LayerRasterizer raster;gpu::LayerImageSource cache;gpu::VolumeIconPlan plan;unsigned visibleCalls{},hides{};
    tools::VolumePreviewOptions options;options.initial=snapshot(5);options.rasterDensity=1;options.memoryImages=&cache;
    options.icons.setVisible=[&](std::span<const gpu::VolumeIconApplication>apps){++visibleCalls;return plan.setVisible(apps);};
    options.icons.plan=[&]{return &plan;};options.icons.hide=[&]{++hides;plan.hide();};
    tools::VolumePreview preview(raster,std::move(options));preview.resize({1280,800,96,1,1280,800});
    core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::volume;core::ModulePresentation modules(core::Module::volume);double time{};
    check(visibleCalls==0&&preview.visibleIcons().empty(),"An inactive owner requests no icons");
    preview.update({},settings,modules.sample(time).presentation,1,time);
    check(visibleCalls==1&&preview.visibleIcons().size()==3,"Activation requests only the visible app rows");
    check(preview.visibleIcons()[0].applicationID=="100:7"&&preview.visibleIcons()[0].executable&&ehud::data::validUUID(preview.visibleIcons()[0].requestToken),"Each request carries the exact process identity, executable metadata and a fresh UUID token");
    check(!hasIcon(preview.state(),0),"No icon is shown before a successful extraction (source nil icon)");
    // Complete the first two requests: one real icon, one generic type icon.
    std::vector<gpu::VolumeIconResult>results;std::array<std::uint8_t,64*64*4>rgba{};for(std::size_t k=0;k<rgba.size();k+=4){rgba[k]=200;rgba[k+3]=255;}
    for(std::size_t n=0;n<2;++n){const auto&r=plan.requests()[n];results.push_back({r.imageKey,r.itemID,r.revision,cache.publish(r.imageKey,r.revision,64,64,rgba),0,n==1});}
    check(plan.receive(results)&&preview.iconsChanged(),"Completed extraction updates the app rows");
    check(preview.state().snapshot().applications[0].icon&&!preview.state().snapshot().applications[1].icon,"A successful instance icon binds; a type-only fallback keeps the source nil icon");
    check(hasIcon(preview.state(),0)&&!hasIcon(preview.state(),1),"The row artwork references the shared memory image");
    check(!preview.iconsChanged(),"Repeated completion notice changes nothing");
    const auto token=preview.visibleIcons()[0].requestToken;const auto calls=visibleCalls;
    for(unsigned n=0;n<60;++n)preview.update({},settings,modules.sample(time+=1./60).presentation,1,time);
    check(visibleCalls==calls,"Steady frames never re-request icons");
    // A content event that changes the rows (scroll) requests the new set; a
    // process keeps its token while listed.
    auto next=snapshot(5);next.volume=.6;check(preview.receiveSnapshot(next)&&visibleCalls==calls,"A snapshot that keeps the rows reuses the icon request");
    check(preview.state().snapshot().applications[0].icon.has_value(),"Icons survive unrelated snapshots");
    const auto hidden=hides;
    auto empty=snapshot(0);preview.receiveSnapshot(empty);preview.update({},settings,modules.sample(time+=1./60).presentation,1,time);
    check(preview.visibleIcons().empty(),"No app rows: no icon requests");
    preview.receiveSnapshot(snapshot(5));preview.update({},settings,modules.sample(time+=1./60).presentation,1,time);
    check(preview.visibleIcons().size()==3&&preview.visibleIcons()[0].requestToken==token,"A relisted process identity keeps its token, so its icon is not re-extracted");
    check(preview.state().snapshot().applications[0].icon.has_value(),"The retained successful binding is shown again immediately");
    settings.module=core::Module::power;modules.select(core::Module::power,time+=1./60);
    for(unsigned n=0;n<90;++n)preview.update({},settings,modules.sample(time+=1./60).presentation,1,time);
    check(hides>hidden&&!plan.active()&&preview.visibleIcons().empty(),"Leaving Volume hides the shared icon request set");
}
}
int main(){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr),"COM for the owned rasterizer");run();CoUninitialize();std::cout<<"PASS "<<checks<<" Volume app-row icon checks (injected plan; no Shell worker)\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
