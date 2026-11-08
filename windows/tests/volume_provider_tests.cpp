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
    bool otherOwner{},vote{},asynchronousGain{},deferVote{};std::int32_t error{},openError{};
    std::wstring lastEndpoint;
    Graph(){audio.available=true;audio.paused=false;audio.outputs={{L"out-一",L"Speakers",false},{L"out-b",L"Headphones",true}};audio.inputs={{L"in-a",L"Microphone",false}};
        audio.defaultOutputID=L"out-一";audio.defaultInputID=L"in-a";
        audio.volume=.75f;audio.muted=false;audio.balance=0.f;audio.canSetVolume=audio.canSetMute=audio.canSetBalance=true;audio.canSetDefaultOutput=audio.canSetDefaultInput=true;audio.applicationsSupported=true;audio.applicationsPaused=false;
        audio.applications.push_back({"pid:42:creation:1",L"Owned Test App",42,true,gpu::AudioApplicationRouteState::direct,{},{},{},{}});
    }
    void changed(){if(binding)binding->receive(gpu::ServiceChange::audio);}
    // The worker applies the vote (AudioService: paused and controls cleared
    // without it; with it, the endpoints read or their open failure).
    // `level` is the endpoint's level, read again when the controls return.
    std::optional<float>level;
    void apply(){const bool want=vote||otherOwner;audio.paused=!want||openError<0;audio.available=!audio.paused;audio.applicationsPaused=audio.paused;audio.error=openError;
        if(audio.paused){if(audio.volume)level=audio.volume;audio.volume.reset();audio.canSetVolume=false;}else{audio.volume=level;audio.canSetVolume=true;}}
    void settle(){apply();changed();}
    gpu::VolumeProviderAccess access(bool selectors=false){gpu::VolumeProviderAccess a;
        a.readSnapshot=[this]() -> const gpu::AudioEndpointSnapshot&{++reads;return audio;};
        a.requestActive=[this](bool v){++activation;vote=v;if(!deferVote){audio.paused=!(vote||otherOwner);audio.available=!audio.paused;audio.applicationsPaused=audio.paused;changed();}return error;};
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
    check(!binding.snapshot().canSetDefaultOutput&&!binding.snapshot().canSetDefaultInput,"Without setter operations the device menus stay disabled, whatever the service reports");
    check(binding.snapshot().outputs[1].headphones&&binding.snapshot().inputID=="in-a","Explicit headphone and input identity are preserved");
    const auto baselineReads=graph.reads,baselineDeliveries=deliveries;
    for(unsigned i=0;i<1000;++i){binding.receive(gpu::ServiceChange::battery);binding.receive(gpu::ServiceChange::clipboard);callbacks.setActive(true);binding.setVisible(true);}
    check(graph.reads==baselineReads&&graph.activation==1&&deliveries==baselineDeliveries,"Unrelated events and repeated visibility produce no reads, publications, or wake requests");
    check(!binding.receive(gpu::ServiceChange::audio)&&deliveries==baselineDeliveries,"Equal audio snapshot produces no UI publication");
    const auto shown=binding.snapshot();
    graph.otherOwner=true;binding.setVisible(false);check(!graph.vote&&!graph.audio.paused&&graph.activation==2,"Conceal releases this vote without pausing another owner's shared service");
    // AudioDeviceController.stop() keeps its snapshot; only the setters check isRunning.
    check(binding.snapshot()==shown&&deliveries==baselineDeliveries,"Hidden binding keeps the snapshot it showed, even while a sibling keeps the service active");
    const auto hiddenReads=graph.reads;graph.changed();check(graph.reads==hiddenReads,"Hidden binding ignores shared audio events");
    check(!callbacks.setVolume("out-一",.2)&&graph.volume==0,"Stale hidden callbacks cannot mutate native audio");
    binding.setVisible(true);check(graph.activation==3&&binding.snapshot().canSetVolume,"Reopen restores selected module without a polling cycle");
    callbacks.setActive(false);check(graph.activation==4&&!binding.active(),"Leaving Volume releases activation independently of shell visibility");
    binding.close();check(graph.activation==4,"Closing already inactive binding does not repeat a service request");
}
// The writes the Volume canvas makes (modules/volume_canvas.hpp) through the
// binding's callbacks, each with the endpoint its gesture began on.
void commands(){Graph graph;gpu::VolumeProviderBinding binding(graph.access());graph.binding=&binding;auto callbacks=binding.callbacks();gpu::VolumeSnapshot delivered=binding.snapshot();binding.setReceiver([&](const auto&s){delivered=s;});binding.setVisible(true);callbacks.setActive(true);
    const auto&controller=binding;
    check(callbacks.setVolume("out-一",.25)&&graph.volume==1&&delivered.volume==.25,"A volume write reaches the native operation and its synchronous readback");
    check(callbacks.setBalance("out-一",-.5)&&graph.balance==1&&delivered.balance==-.5,"Stereo balance preserves its signed domain");
    check(callbacks.setMute("out-一",true)&&graph.mute==1&&delivered.muted==true,"Mute reaches the provider without an extra owner");
    check(graph.lastEndpoint==L"out-一","Mute and balance writes carry the bound endpoint for asynchronous validation");
    check(callbacks.setAppGain("pid:42:creation:1",.4)&&graph.gain==1&&controller.snapshot().applications[0].state==gpu::VolumeAppState::active,"Per-app attenuation remains connected to existing route model");
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
void selection(){Graph graph;gpu::VolumeProviderBinding binding(graph.access(true));graph.binding=&binding;binding.setVisible(true);binding.callbacks().setActive(true);const auto&controller=binding;
    check(controller.snapshot().canSetDefaultOutput&&controller.snapshot().canSetDefaultInput,"Only supplied actual selectors enable the device menus");
    // The canvas's Output menu stops the routes first (VolumeCanvas.openDeviceMenu).
    binding.callbacks().stopAllApps();check(binding.callbacks().setDefaultOutput("out-b"),"An explicit output routing selection is delegated");
    check(graph.output==1&&graph.stopAll==1&&controller.snapshot().outputID=="out-b","The output switch selects after the routes stopped");
    check(binding.callbacks().setDefaultInput("in-a")&&graph.input==1,"Input selection has its own supported routing operation");
    const auto callbacks=binding.callbacks();check(!callbacks.setDefaultOutput("removed")&&graph.output==1,"A removed device is rejected against latest topology");
    graph.error=-77;check(!callbacks.setDefaultInput("in-a")&&binding.lastFailure()->status==-77,"Native selector failure is preserved instead of claiming success");
    // The service turned switching off (no IPolicyConfig, or a failed switch).
    graph.error=0;graph.audio.canSetDefaultOutput=graph.audio.canSetDefaultInput=false;graph.changed();
    check(!controller.snapshot().canSetDefaultOutput&&!controller.snapshot().canSetDefaultInput,"The service's capability disables both menus even with setters");
    check(!callbacks.setDefaultOutput("out-一")&&!callbacks.setDefaultInput("in-a")&&graph.output==1&&graph.input==2,"A choice reaching a disabled menu is refused before the setter");
    graph.audio.canSetDefaultInput=true;graph.changed();
    check(!controller.snapshot().canSetDefaultOutput&&controller.snapshot().canSetDefaultInput,"Each direction follows its own capability");
}
void lifetime(){Graph graph;gpu::VolumeCallbacks retained;{
    auto binding=std::make_unique<gpu::VolumeProviderBinding>(graph.access());graph.binding=binding.get();retained=binding->callbacks();binding->setVisible(true);retained.setActive(true);
    unsigned called{};binding->setReceiver([&,guard=std::make_shared<unsigned>(17)](const auto&){++called;graph.binding=nullptr;binding.reset();check(*guard==17,"Receiver capture survives owner teardown inside delivery");});
    graph.audio.volume=.1f;graph.binding->receive(gpu::ServiceChange::audio);check(!binding&&called==1&&graph.activation==2,"Binding teardown from notification releases vote and cancels route safely");
    }
    const auto writes=graph.volume;check(!retained.setVolume("out-一",.3)&&!retained.setMute("out-一",true)&&!retained.stopApp("any"),"Copied callbacks expire after binding destruction");retained.setActive(true);retained.stopAllApps();check(graph.volume==writes&&graph.activation==2,"Expired callbacks cannot revive borrowed native service");
    Graph other;gpu::VolumeProviderBinding closed(other.access());other.binding=&closed;auto callbacks=closed.callbacks();closed.setVisible(true);callbacks.setActive(true);closed.close();closed.close();check(other.activation==2&&!callbacks.setDefaultInput("in-a"),"Explicit close is terminal and idempotent before service destruction");
}
void boundaries(){Graph graph;graph.audio.outputs.resize(513);bool rejected{};try{gpu::VolumeProviderBinding binding(graph.access());}catch(const std::invalid_argument&){rejected=true;}check(rejected&&graph.activation==0,"Oversized injected topology is rejected without starting audio");
    graph.audio.outputs.clear();graph.audio.volume=std::numeric_limits<float>::infinity();rejected=false;try{gpu::VolumeProviderBinding binding(graph.access());}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid scalar is rejected before presentation");
    rejected=false;try{gpu::VolumeProviderBinding binding({});}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Missing borrowed access cannot create a partially active provider");
}
}
// Leaving Volume and coming back with the shared worker's asynchronous vote
// (AudioRuntime): until the worker has read again the service snapshot is
// paused with its controls cleared. The binding keeps the content Volume
// showed meanwhile, as the Mac's controller keeps its snapshot across
// stop()/start(), so the page's structure (applicationActivitySupported) never
// flips and VolumeCanvas does not rebuild its page as it leaves or under the
// entrance of the next switch to Volume. Failures still surface.
void revisit(){Graph graph;graph.deferVote=true;graph.apply();gpu::VolumeProviderBinding binding(graph.access());graph.binding=&binding;auto callbacks=binding.callbacks();
    std::vector<gpu::VolumeSnapshot>deliveries;binding.setReceiver([&](const auto&s){deliveries.push_back(s);});
    binding.setVisible(true);callbacks.setActive(true);
    check(!binding.snapshot().applicationActivitySupported&&deliveries.empty(),"The first visit waits for the worker's first read");
    graph.settle();
    const auto shown=binding.snapshot();
    check(shown.applicationActivitySupported&&shown.canSetVolume&&shown.volume==.75&&deliveries.size()==1,"The first read shows the devices and the apps");
    for(const bool conceal:{false,true}){
        deliveries.clear();
        if(conceal)binding.setVisible(false);else callbacks.setActive(false);
        check(deliveries.empty()&&binding.snapshot()==shown,"Releasing the vote delivers nothing to the leaving page");
        graph.settle();
        check(graph.audio.paused&&deliveries.empty()&&binding.snapshot()==shown,"The worker's paused publication changes nothing shown");
        check(!callbacks.setVolume("out-一",.2)&&graph.volume==0,"Writes stay refused while Volume is away");
        if(conceal)binding.setVisible(true);else callbacks.setActive(true);
        check(deliveries.empty()&&binding.snapshot()==shown,"Coming back shows the retained content until the worker reads");
        check(!callbacks.setVolume("out-一",.2)&&graph.volume==0&&binding.snapshot().status==shown.status,"A write before the read is refused quietly");
        graph.settle();
        check(deliveries.empty()&&binding.snapshot()==shown,"An unchanged read publishes nothing");
    }
    // A change made while away is one in-place update with the first read.
    callbacks.setActive(false);graph.settle();graph.level=.25f;deliveries.clear();
    callbacks.setActive(true);check(deliveries.empty(),"Nothing before the read");
    graph.settle();check(deliveries.size()==1&&deliveries[0].volume==.25&&deliveries[0].applicationActivitySupported,"The read updates the level; the structure stays");
    // A refused vote and a worker that cannot open the endpoints still show their failure.
    callbacks.setActive(false);graph.settle();deliveries.clear();
    graph.error=-66;callbacks.setActive(true);
    check(deliveries.size()==1&&deliveries[0].status&&deliveries[0].status->find("-66")!=std::string::npos&&deliveries[0].outputs==shown.outputs,"A refused vote shows its failure over the retained content");
    graph.error=0;callbacks.setActive(false);graph.settle();callbacks.setActive(true);deliveries.clear();
    graph.openError=-77;graph.settle();
    check(graph.audio.paused&&!deliveries.empty()&&!binding.snapshot().canSetVolume&&binding.snapshot().status&&binding.snapshot().status->find("-77")!=std::string::npos,"A worker that could not open the endpoints shows the real paused state and its error");
}
void endpointTraits(){Graph graph;graph.audio.outputs.push_back({L"bt-1",L"Synthetic BT",true});gpu::VolumeProviderBinding binding(graph.access());graph.binding=&binding;binding.setVisible(true);binding.callbacks().setActive(true);
    const auto&out=binding.snapshot().outputs;check(out.size()==3&&out[2].headphones&&!out[0].headphones&&out[1].headphones,"Explicit headphone traits reach the snapshot");
    // Fixed/pass-through output: value stays visible, writes are refused before the provider.
    graph.audio.canSetVolume=false;graph.changed();check(binding.snapshot().volume==.75&&!binding.snapshot().canSetVolume,"Fixed-volume output keeps its readback but disables the slider");
    const auto writes=graph.volume;check(!binding.callbacks().setVolume("out-一",.3)&&graph.volume==writes&&binding.lastFailure()->kind==gpu::VolumeProviderFailureKind::unsupported,"Fixed-volume output never receives a software volume write");
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
}
int main(){try{lifecycle();commands();selection();lifetime();boundaries();revisit();endpointTraits();workerStatus();std::cout<<"PASS "<<checks<<" injected Volume provider checks (no native audio accessed)\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
