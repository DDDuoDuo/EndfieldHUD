#include "native/volume_provider.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
namespace gpu=endfield::native;
std::size_t checks{};void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
struct Graph {
    gpu::AudioEndpointSnapshot audio;gpu::VolumeProviderBinding*binding{};
    unsigned reads{},activation{},volume{},mute{},balance{},gain{},stop{},stopAll{},output{},input{};
    bool otherOwner{},vote{},asynchronousGain{};std::int32_t error{};
    std::wstring lastEndpoint;
    Graph(){audio.available=true;audio.paused=false;audio.outputs={{L"out-一",L"Speakers",false,false},{L"out-b",L"Headphones",true,false}};audio.inputs={{L"in-a",L"Microphone",false,false}};
        audio.defaultOutputID=L"out-一";audio.defaultInputID=L"in-a";
        audio.volume=.75f;audio.muted=false;audio.balance=0;audio.canSetVolume=audio.canSetMute=audio.canSetBalance=true;audio.applicationsSupported=true;audio.applicationsPaused=false;
        audio.applications.push_back({"pid:42:creation:1",L"Owned Test App",42,true,gpu::AudioApplicationRouteState::direct,{},{},{},{}});
    }
    void changed(){if(binding)binding->receive(gpu::ServiceChange::audio);}
    gpu::VolumeProviderAccess access(bool selectors=false){gpu::VolumeProviderAccess a;
        a.readSnapshot=[this]() -> const gpu::AudioEndpointSnapshot&{++reads;return audio;};
        a.requestActive=[this](bool v){++activation;vote=v;audio.paused=!(vote||otherOwner);audio.available=!audio.paused;audio.applicationsPaused=audio.paused;changed();return error;};
        a.setVolume=[this](std::wstring_view endpoint,float v){++volume;lastEndpoint=endpoint;check(endpoint==audio.defaultOutputID,"Volume write carries the endpoint its gesture began on");if(error<0)return error;audio.volume=v;changed();return 0;};
        a.setMute=[this](std::wstring_view endpoint,bool v){++mute;lastEndpoint=endpoint;if(error<0)return error;audio.muted=v;changed();return 0;};
        a.setBalance=[this](std::wstring_view endpoint,float v){++balance;lastEndpoint=endpoint;if(error<0)return error;audio.balance=v;changed();return 0;};
        a.setAppGain=[this](std::string_view id,float v){++gain;check(id==audio.applications[0].id,"Application command preserves exact process-generation identity");if(error<0)return error;if(!asynchronousGain){audio.applications[0].gain=v;audio.applications[0].state=gpu::AudioApplicationRouteState::active;changed();}return 0;};
        a.stopApp=[this](std::string_view){++stop;if(error<0)return error;audio.applications[0].gain.reset();audio.applications[0].state=gpu::AudioApplicationRouteState::direct;changed();return 0;};
        a.stopAllApps=[this]{++stopAll;return error;};
        if(selectors){a.setDefaultOutput=[this](std::wstring_view id){++output;audio.defaultOutputID=id;changed();return error;};a.setDefaultInput=[this](std::wstring_view id){++input;audio.defaultInputID=id;changed();return error;};}return a;
    }
};
void lifecycle(){Graph graph;gpu::VolumeProviderBinding binding(graph.access());graph.binding=&binding;auto callbacks=binding.callbacks();unsigned deliveries{};
    binding.setReceiver([&](const auto&){++deliveries;});
    check(graph.activation==0&&!binding.active()&&!binding.visible(),"Construction neither starts native audio nor selects a module");
    check(!binding.snapshot().canSetVolume&&!binding.snapshot().applicationActivitySupported,"Hidden initial snapshot never advertises writable controls");
    callbacks.setActive(true);check(graph.activation==0&&!binding.active(),"Selecting Volume while concealed cannot register audio listeners");
    binding.setVisible(true);check(graph.activation==1&&binding.active()&&graph.vote,"Visible selected Volume acquires one shared activation vote");
    check(binding.snapshot().canSetVolume&&binding.snapshot().canSetMute&&binding.snapshot().canSetBalance,"Known native master capabilities are enabled");
    check(!binding.snapshot().canSetDefaultOutput&&!binding.snapshot().canSetDefaultInput,"No native default-device interop means no invented routing capability");
    check(binding.snapshot().outputs[1].headphones&&binding.snapshot().inputID=="in-a","Explicit headphone and input identity are preserved");
    const auto baselineReads=graph.reads,baselineDeliveries=deliveries;
    for(unsigned i=0;i<1000;++i){binding.receive(gpu::ServiceChange::battery);binding.receive(gpu::ServiceChange::clipboard);callbacks.setActive(true);binding.setVisible(true);}
    check(graph.reads==baselineReads&&graph.activation==1&&deliveries==baselineDeliveries,"Unrelated events and repeated visibility produce no reads, publications, or wake requests");
    check(!binding.receive(gpu::ServiceChange::audio)&&deliveries==baselineDeliveries,"Equal audio snapshot produces no UI publication");
    graph.otherOwner=true;binding.setVisible(false);check(!graph.vote&&!graph.audio.paused&&graph.activation==2,"Conceal releases this vote without pausing another owner's shared service");
    check(!binding.snapshot().canSetVolume&&!binding.snapshot().applicationActivitySupported,"Hidden retained snapshot disables writes even while sibling keeps service active");
    const auto hiddenReads=graph.reads;graph.changed();check(graph.reads==hiddenReads,"Hidden binding ignores shared audio events");
    check(!callbacks.setVolume("out-一",.2)&&graph.volume==0,"Stale hidden callbacks cannot mutate native audio");
    binding.setVisible(true);check(graph.activation==3&&binding.snapshot().canSetVolume,"Reopen restores selected module without a polling cycle");
    callbacks.setActive(false);check(graph.activation==4&&!binding.active(),"Leaving Volume releases activation independently of shell visibility");
    binding.close();check(graph.activation==4,"Closing already inactive binding does not repeat a service request");
}
void commands(){Graph graph;gpu::VolumeProviderBinding binding(graph.access());graph.binding=&binding;auto callbacks=binding.callbacks();gpu::VolumeController controller(binding.snapshot(),callbacks);binding.setReceiver([&](const auto&s){controller.receiveSnapshot(s);});binding.setVisible(true);controller.setActive(true);
    check(controller.setSlider("volume",.25)&&graph.volume==1&&controller.snapshot().volume==.25,"Actual shared controller slider reaches native operation and synchronous readback");
    check(controller.setSlider("balance",-.5)&&graph.balance==1&&controller.snapshot().balance==-.5,"Stereo balance preserves its signed domain");
    check(controller.perform("audio:mute",0)&&graph.mute==1&&controller.snapshot().muted==true,"Source mute action reaches provider without an extra owner");
    check(graph.lastEndpoint==L"out-一","Mute and balance writes carry the bound endpoint for asynchronous validation");
    check(controller.setSlider("app:pid:42:creation:1",.4)&&graph.gain==1&&controller.snapshot().applications[0].state==gpu::VolumeAppState::active,"Per-app attenuation remains connected to existing route model");
    check(std::abs(*controller.snapshot().applications[0].gain-.4)<1e-6,"Native float readback is used rather than optimistic double UI value");
    graph.audio.applications[0].available=false;graph.changed();
    check(callbacks.setAppGain("pid:42:creation:1",.6),"Silent retained enabled route is still adjustable");
    check(callbacks.stopApp("pid:42:creation:1")&&graph.stop==1,"Restoring an application uses the shared session lease cleanup");
    check(!callbacks.setAppGain("pid:42:creation:1",.5),"Unavailable direct application cannot acquire a new route");
    graph.audio.applications[0].available=true;graph.asynchronousGain=true;graph.changed();
    check(callbacks.setAppGain("pid:42:creation:1",.2)&&!controller.snapshot().applications[0].gain,"Accepted asynchronous command does not fabricate a readback");
    graph.audio.applications[0].state=gpu::AudioApplicationRouteState::failed;graph.audio.applications[0].error=-123;graph.changed();
    check(controller.snapshot().applications[0].error&&controller.snapshot().applications[0].error->find("-123")!=std::string::npos,"Asynchronous per-app native failure is surfaced from delivered state");
    graph.audio.defaultOutputID=L"out-b"; // Deliberately delay the notification.
    const auto writes=graph.volume;check(!callbacks.setVolume("out-一",.9)&&graph.volume==writes,"Endpoint changed before event delivery: old gesture cannot retarget a different output");
    check(binding.lastFailure()->kind==gpu::VolumeProviderFailureKind::deviceChanged&&controller.snapshot().outputID=="out-b","Stale endpoint failure publishes current identity and useful source-style status");
    check(!callbacks.setVolume("out-b",std::numeric_limits<double>::quiet_NaN())&&!callbacks.setBalance("out-b",2)&&graph.balance==1,"Nonfinite/out-of-domain raw callbacks cannot reach native operations");
    graph.error=-55;check(!callbacks.setVolume("out-b",.1)&&controller.snapshot().volume==.25,"Failed write leaves actual readback visible");
    check(binding.lastFailure()->status==-55&&controller.snapshot().status->find("-55")!=std::string::npos,"Native status remains observable without logging names or audio data");
    binding.setErrorText([](auto){return "Owned localized failure";});check(controller.snapshot().status=="Owned localized failure","Owner language changes can update failure text");
    graph.error=0;graph.audio.balance.reset();graph.audio.canSetBalance=false;graph.changed();check(!controller.snapshot().canSetBalance&&!binding.lastFailure(),"Topology event clears transient failure and disables unsupported balance");
    check(!callbacks.setBalance("out-b",0)&&graph.balance==1,"Unsupported device balance never synthesizes channel writes");
}
void selection(){Graph graph;gpu::VolumeProviderBinding binding(graph.access(true));graph.binding=&binding;gpu::VolumeController controller(binding.snapshot(),binding.callbacks());binding.setReceiver([&](const auto&s){controller.receiveSnapshot(s);});binding.setVisible(true);controller.setActive(true);
    check(controller.snapshot().canSetDefaultOutput&&controller.snapshot().canSetDefaultInput,"Only supplied actual selectors enable source chooser actions");
    check(controller.perform("audio:output",1)&&controller.perform("audio:output:out-b",2),"Controller chooser delegates explicit output routing selection");
    check(graph.output==1&&graph.stopAll==1&&controller.snapshot().outputID=="out-b"&&!controller.choosing(),"Original output switch stops attenuation routes before selecting and dismisses only on success");
    check(controller.perform("audio:input",3)&&controller.perform("audio:input:in-a",4)&&graph.input==1,"Input chooser has independent supported routing operation");
    const auto callbacks=binding.callbacks();check(!callbacks.setDefaultOutput("removed")&&graph.output==1,"A removed device is rejected against latest topology");
    graph.error=-77;check(!callbacks.setDefaultInput("in-a")&&binding.lastFailure()->status==-77,"Native selector failure is preserved instead of claiming success");
}
void lifetime(){Graph graph;gpu::VolumeCallbacks retained;{
    auto binding=std::make_unique<gpu::VolumeProviderBinding>(graph.access());graph.binding=binding.get();retained=binding->callbacks();binding->setVisible(true);retained.setActive(true);
    unsigned called{};binding->setReceiver([&,guard=std::make_shared<unsigned>(17)](const auto&){++called;graph.binding=nullptr;binding.reset();check(*guard==17,"Receiver capture survives owner teardown inside delivery");});
    graph.audio.volume=.1;graph.binding->receive(gpu::ServiceChange::audio);check(!binding&&called==1&&graph.activation==2,"Binding teardown from notification releases vote and cancels route safely");
    }
    const auto writes=graph.volume;check(!retained.setVolume("out-一",.3)&&!retained.setMute("out-一",true)&&!retained.stopApp("any"),"Copied callbacks expire after binding destruction");retained.setActive(true);retained.stopAllApps();check(graph.volume==writes&&graph.activation==2,"Expired callbacks cannot revive borrowed native service");
    Graph other;gpu::VolumeProviderBinding closed(other.access());other.binding=&closed;auto callbacks=closed.callbacks();closed.setVisible(true);callbacks.setActive(true);closed.close();closed.close();check(other.activation==2&&!callbacks.setDefaultInput("in-a"),"Explicit close is terminal and idempotent before service destruction");
}
void boundaries(){Graph graph;graph.audio.outputs.resize(513);bool rejected{};try{gpu::VolumeProviderBinding binding(graph.access());}catch(const std::invalid_argument&){rejected=true;}check(rejected&&graph.activation==0,"Oversized injected topology is rejected without starting audio");
    graph.audio.outputs.clear();graph.audio.volume=std::numeric_limits<float>::infinity();rejected=false;try{gpu::VolumeProviderBinding binding(graph.access());}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid scalar is rejected before presentation");
    rejected=false;try{gpu::VolumeProviderBinding binding({});}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Missing borrowed access cannot create a partially active provider");
}
}
void endpointTraits(){Graph graph;graph.audio.outputs.push_back({L"bt-1",L"Synthetic BT",true,true});gpu::VolumeProviderBinding binding(graph.access());graph.binding=&binding;binding.setVisible(true);binding.callbacks().setActive(true);
    const auto&out=binding.snapshot().outputs;check(out.size()==3&&out[2].bluetooth&&out[2].headphones&&!out[0].bluetooth&&out[1].headphones&&!out[1].bluetooth,"Explicit Bluetooth and headphone traits reach the Headphones / Bluetooth page");
    gpu::VolumeController controller(binding.snapshot(),binding.callbacks());controller.setActive(true);controller.perform("audio:headphones",0);
    const auto plan=gpu::prepareVolumeScene(controller);bool bluetoothRow{};for(const auto&n:plan.layers["children"].array())if(n["kind"].string()=="text"&&n["text"]["string"].string()=="Bluetooth")bluetoothRow=true;check(bluetoothRow,"A Bluetooth endpoint is labelled Bluetooth exactly as the source");
    // Fixed/pass-through output: value stays visible, writes are refused before the provider.
    graph.audio.canSetVolume=false;graph.changed();check(binding.snapshot().volume==.75&&!binding.snapshot().canSetVolume,"Fixed-volume output keeps its readback but disables the slider");
    const auto writes=graph.volume;check(!binding.callbacks().setVolume("out-一",.3)&&graph.volume==writes&&binding.lastFailure()->kind==gpu::VolumeProviderFailureKind::unsupported,"Fixed-volume output never receives a software volume write");
    gpu::VolumeController fixed(binding.snapshot(),binding.callbacks());fixed.setActive(true);bool hint{};const auto fixedPlan=gpu::prepareVolumeScene(fixed);for(const auto&n:fixedPlan.layers["children"].array())if(n["id"].string()=="volume/device-controls")hint=true;check(hint,"Source \"Use this device's controls\" hint appears for a fixed output");
}
void workerStatus(){
    using K=gpu::VolumeProviderFailureKind;
    check(gpu::volumeProviderFailure(static_cast<std::int32_t>(0x8007048Fu)).kind==K::deviceChanged&&gpu::volumeProviderFailure(static_cast<std::int32_t>(0x88890004u)).kind==K::deviceChanged,"Disconnected/invalidated endpoint statuses map to the source device-changed message");
    check(gpu::volumeProviderFailure(static_cast<std::int32_t>(0x80070032u)).kind==K::unsupported&&gpu::volumeProviderFailure(static_cast<std::int32_t>(0x80004001u)).kind==K::unsupported,"Refused controls map to unsupported");
    check(gpu::volumeProviderFailure(static_cast<std::int32_t>(0x80070057u)).kind==K::invalidValue&&gpu::volumeProviderFailure(static_cast<std::int32_t>(0x8000000Au)).kind==K::unavailable,"Invalid value and pending map to their source categories");
    check(gpu::volumeProviderFailure(-5).kind==K::native&&gpu::volumeProviderFailure(-5).status==-5,"Other native statuses keep their code");
    Graph graph;gpu::VolumeProviderBinding binding(graph.access());graph.binding=&binding;binding.setVisible(true);binding.callbacks().setActive(true);
    graph.audio.commandError=static_cast<std::int32_t>(0x8007048Fu);graph.changed();check(binding.snapshot().status=="The audio device changed. Try the control again.","An asynchronous worker command failure is surfaced with the source text");
    graph.audio.commandError=0;graph.changed();check(!binding.snapshot().status,"A later successful command clears the asynchronous status");
    graph.audio.routesStoppedByDeviceChange=true;graph.changed();check(binding.snapshot().routingStopped,"Output change notice reaches the presentation snapshot");
    gpu::VolumeController controller(binding.snapshot(),binding.callbacks());controller.setActive(true);std::string status;
    const auto englishPlan=gpu::prepareVolumeScene(controller);for(const auto&n:englishPlan.layers["children"].array())if(n["id"].string()=="volume/per-app-status")status=n["text"]["string"].string();
    check(status=="App routing stopped because the output device changed.","Source per-app status text is shown after an output change");
    auto strings=gpu::VolumeStrings::simplifiedChinese();check(controller.setStrings(strings)&&!controller.setStrings(strings),"Equal Volume strings do not rebuild or reshape artwork");
    const auto revision=controller.contentRevision();check(!controller.setStrings(gpu::VolumeStrings::simplifiedChinese())&&controller.contentRevision()==revision,"Repeated language event keeps the content revision");
    const auto chinesePlan=gpu::prepareVolumeScene(controller);for(const auto&n:chinesePlan.layers["children"].array())if(n["id"].string()=="volume/per-app-status")status=n["text"]["string"].string();
    check(status=="输出设备已更改，应用混音已停止。","Language change updates the routing notice immediately");
}
int main(){try{lifecycle();commands();selection();lifetime();boundaries();endpointTraits();workerStatus();std::cout<<"PASS "<<checks<<" injected Volume provider checks (no native audio accessed)\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
