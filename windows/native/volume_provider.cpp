#include "native/volume_provider.hpp"
#include "native/audio_default_device.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace endfield::native {
namespace {
using Failure=VolumeProviderFailure;using Kind=VolumeProviderFailureKind;
bool failed(std::int32_t value){return value<0;}
std::string errorText(Failure f){switch(f.kind){
case Kind::unavailable:return "Audio device information is unavailable.";
case Kind::unsupported:return "This device does not support this control.";
case Kind::deviceChanged:return "The audio device changed. Try the control again.";
case Kind::invalidValue:return "The audio control value is invalid.";
case Kind::native:return "Windows could not complete the audio operation ("+std::to_string(f.status)+").";
}return {};}
// Endpoint guards run against the latest service snapshot, not the last UI
// event. Compare UTF-16/32 to UTF-8 without allocating during a slider write.
bool sameText(std::wstring_view wide,std::string_view utf8)noexcept{
    std::size_t at{};
    const auto byte=[&](std::uint32_t value){return at<utf8.size()&&static_cast<unsigned char>(utf8[at++])==value;};
    for(std::size_t n=0;n<wide.size();++n){auto c=static_cast<std::uint32_t>(wide[n]);
        if constexpr(sizeof(wchar_t)==2){if(c>=0xd800&&c<=0xdbff){if(++n==wide.size())return false;const auto low=static_cast<std::uint32_t>(wide[n]);if(low<0xdc00||low>0xdfff)return false;c=0x10000+((c-0xd800)<<10)+low-0xdc00;}}
        if(!c||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;
        if(c<0x80){if(!byte(c))return false;}else if(c<0x800){if(!byte(0xc0|(c>>6))||!byte(0x80|(c&63)))return false;}
        else if(c<0x10000){if(!byte(0xe0|(c>>12))||!byte(0x80|((c>>6)&63))||!byte(0x80|(c&63)))return false;}
        else if(!byte(0xf0|(c>>18))||!byte(0x80|((c>>12)&63))||!byte(0x80|((c>>6)&63))||!byte(0x80|(c&63)))return false;
    }return at==utf8.size();
}
void validate(const AudioEndpointSnapshot&s){
    if(s.outputs.size()>512||s.inputs.size()>512||s.applications.size()>AudioSessionRoutes::maximumApplications)throw std::invalid_argument("Audio provider snapshot exceeds native bounds");
    if((s.volume&&(!std::isfinite(*s.volume)||*s.volume<0||*s.volume>1))||(s.balance&&(!std::isfinite(*s.balance)||*s.balance< -1||*s.balance>1)))throw std::invalid_argument("Invalid native audio scalar");
}
}
VolumeProviderFailure volumeProviderFailure(std::int32_t status)noexcept{
    switch(static_cast<std::uint32_t>(status)){
    case 0x8007048Fu:case 0x88890004u:return {Kind::deviceChanged,status};
    case 0x80070032u:case 0x80004001u:return {Kind::unsupported,status};
    case 0x80070057u:return {Kind::invalidValue,status};
    case 0x8000000Au:return {Kind::unavailable,status};
    default:return {Kind::native,status};
    }
}
struct VolumeProviderBinding::State {
    VolumeProviderAccess access;VolumeProviderErrorText format;VolumeSnapshot snapshot;
    std::shared_ptr<Receiver>receiver;std::optional<Failure>failure;
    bool visible{},selected{},requested{},closed{},busy{};
    State(VolumeProviderAccess a,VolumeProviderErrorText f):access(std::move(a)),format(f?std::move(f):errorText){
        if(!access.readSnapshot||!access.requestActive)throw std::invalid_argument("Volume provider requires borrowed snapshot and activation operations");
        publish(false);
    }
    std::string message(Failure f)const{auto text=format(f);if(text.size()>4096||!ehud::data::Json::validUtf8(text)||text.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid Volume provider error text");return text;}
    bool publish(bool deliver=true){
        if(closed)return false;
        const auto&s=access.readSnapshot();validate(s);auto next=volumeSnapshotFromEndpoints(s);
        const bool live=requested&&!s.paused;
        next.canSetVolume=next.canSetVolume&&live&&bool(access.setVolume);
        next.canSetMute=next.canSetMute&&live&&bool(access.setMute);
        next.canSetBalance=next.canSetBalance&&live&&bool(access.setBalance);
        next.canSetDefaultOutput=live&&bool(access.setDefaultOutput);
        next.canSetDefaultInput=live&&bool(access.setDefaultInput);
        next.applicationActivitySupported=next.applicationActivitySupported&&live&&bool(access.setAppGain);
        for(std::size_t n=0;n<next.applications.size();++n){auto&app=next.applications[n];app.available=app.available&&next.applicationActivitySupported;if(failed(s.applications[n].error))app.error=message({Kind::native,s.applications[n].error});}
        if(failed(s.error))next.status=message(volumeProviderFailure(s.error));
        else if(failed(s.inputError))next.status=message(volumeProviderFailure(s.inputError));
        else if(failed(s.commandError))next.status=message(volumeProviderFailure(s.commandError));
        if(failed(s.applicationError))next.applicationMessage=message(volumeProviderFailure(s.applicationError));
        if(failure)next.status=message(*failure);
        if(snapshot==next)return false;
        snapshot=std::move(next);const auto callback=receiver;if(deliver&&callback)(*callback)(snapshot);return true;
    }
    bool reconcile(){
        const bool next=selected&&visible&&!closed;if(next==requested)return false;
        requested=next;busy=true;
        std::int32_t status{};try{status=access.requestActive(next);}catch(...){busy=false;throw;}
        busy=false;failure=failed(status)?std::optional<Failure>{volumeProviderFailure(status)}:std::nullopt;
        publish();return true;
    }
    bool receive(){if(closed||busy||!requested)return false;failure.reset();return publish();}
    bool reject(Kind kind){failure=Failure{kind,0};publish();return false;}
    bool ready(){return !closed&&!busy&&requested;}
    const AudioEndpointSnapshot*current(std::string_view id){
        if(!ready())return nullptr;const auto&s=access.readSnapshot();
        if(s.paused||!s.available){reject(Kind::unavailable);return nullptr;}
        if(!sameText(s.defaultOutputID,id)){reject(Kind::deviceChanged);return nullptr;}return &s;
    }
    template<class F>bool write(F&&f){
        busy=true;std::int32_t status{};try{status=f();}catch(...){busy=false;throw;}
        busy=false;failure=failed(status)?std::optional<Failure>{volumeProviderFailure(status)}:std::nullopt;
        publish();return !failed(status);
    }
    bool scalar(std::string_view id,double value,bool balance){
        const auto*s=current(id);if(!s)return false;
        if(!std::isfinite(value)||value<(balance?-1:0)||value>1)return reject(Kind::invalidValue);
        const auto&operation=balance?access.setBalance:access.setVolume;
        if(!operation||(balance?(!s->canSetBalance||!s->balance):(!s->canSetVolume||!s->volume)))return reject(Kind::unsupported);
        // The snapshot may change synchronously inside the operation.
        const std::wstring endpoint=s->defaultOutputID;
        return write([&]{return operation(endpoint,static_cast<float>(value));});
    }
    bool mute(std::string_view id,bool value){
        const auto*s=current(id);if(!s)return false;if(!s->muted||!s->canSetMute||!access.setMute)return reject(Kind::unsupported);
        const std::wstring endpoint=s->defaultOutputID;return write([&]{return access.setMute(endpoint,value);});
    }
    bool application(std::string_view id,double gain,bool stop){
        if(!ready())return false;const auto&s=access.readSnapshot();
        if(s.paused||s.applicationsPaused||!s.applicationsSupported)return reject(Kind::unavailable);
        const auto found=std::find_if(s.applications.begin(),s.applications.end(),[&](const auto&a){return a.id==id;});
        if(found==s.applications.end()||(!found->available&&found->state==AudioApplicationRouteState::direct))return reject(Kind::deviceChanged);
        if(stop){if(!access.stopApp)return reject(Kind::unsupported);return write([&]{return access.stopApp(id);});}
        if(!std::isfinite(gain)||gain<0||gain>1)return reject(Kind::invalidValue);
        if(!access.setAppGain)return reject(Kind::unsupported);return write([&]{return access.setAppGain(id,static_cast<float>(gain));});
    }
    bool select(std::string_view id,bool input){
        if(!ready())return false;const auto&s=access.readSnapshot();if(s.paused)return reject(Kind::unavailable);
        const auto&operation=input?access.setDefaultInput:access.setDefaultOutput;if(!operation)return reject(Kind::unsupported);
        const auto&devices=input?s.inputs:s.outputs;const auto found=std::find_if(devices.begin(),devices.end(),[&](const auto&d){return sameText(d.id,id);});
        if(found==devices.end())return reject(Kind::deviceChanged);
        // Snapshot storage may change synchronously inside the native operation.
        const auto stableID=found->id;return write([&]{return operation(stableID);});
    }
    void close()noexcept{
        if(closed)return;closed=true;receiver.reset();const bool release=std::exchange(requested,false);selected=false;visible=false;
        if(release)try{(void)access.requestActive(false);}catch(...){}
        access={};
    }
};
VolumeProviderBinding::VolumeProviderBinding(VolumeProviderAccess access,VolumeProviderErrorText format):state_(std::make_shared<State>(std::move(access),std::move(format))){}
VolumeProviderBinding::~VolumeProviderBinding(){close();}
VolumeCallbacks VolumeProviderBinding::callbacks()const{
    const std::weak_ptr<State>weak=state_;VolumeCallbacks c;
    c.setActive=[weak](bool v){if(const auto s=weak.lock();s&&!s->closed){s->selected=v;s->reconcile();}};
    c.setVolume=[weak](std::string_view id,double v){const auto s=weak.lock();return s&&s->scalar(id,v,false);};
    c.setBalance=[weak](std::string_view id,double v){const auto s=weak.lock();return s&&s->scalar(id,v,true);};
    c.setMute=[weak](std::string_view id,bool v){const auto s=weak.lock();return s&&s->mute(id,v);};
    c.setAppGain=[weak](std::string_view id,double v){const auto s=weak.lock();return s&&s->application(id,v,false);};
    c.stopApp=[weak](std::string_view id){const auto s=weak.lock();return s&&s->application(id,1,true);};
    c.stopAllApps=[weak]{if(const auto s=weak.lock();s&&s->ready()&&s->access.stopAllApps)s->write([&]{return s->access.stopAllApps();});};
    c.setDefaultOutput=[weak](std::string_view id){const auto s=weak.lock();return s&&s->select(id,false);};
    c.setDefaultInput=[weak](std::string_view id){const auto s=weak.lock();return s&&s->select(id,true);};return c;
}
const VolumeSnapshot&VolumeProviderBinding::snapshot()const noexcept{return state_->snapshot;}
void VolumeProviderBinding::setReceiver(Receiver receiver){if(!state_->closed)state_->receiver=receiver?std::make_shared<Receiver>(std::move(receiver)):nullptr;}
bool VolumeProviderBinding::receive(ServiceChange change){const auto s=state_;return (static_cast<unsigned>(change)&static_cast<unsigned>(ServiceChange::audio))&&s->receive();}
bool VolumeProviderBinding::receive(){const auto s=state_;return s->receive();}
bool VolumeProviderBinding::setVisible(bool value){const auto s=state_;if(s->closed||s->visible==value)return false;s->visible=value;s->reconcile();return true;}
bool VolumeProviderBinding::visible()const noexcept{return state_->visible;}
bool VolumeProviderBinding::active()const noexcept{return state_->requested;}
void VolumeProviderBinding::setErrorText(VolumeProviderErrorText value){const auto s=state_;if(!s->closed){s->format=value?std::move(value):errorText;s->publish();}}
std::optional<Failure>VolumeProviderBinding::lastFailure()const noexcept{return state_->failure;}
void VolumeProviderBinding::close()noexcept{const auto s=state_;s->close();}

#ifdef _WIN32
VolumeProviderAccess volumeProviderAccess(SystemServices&service,std::function<std::int32_t(bool)>activation,std::shared_ptr<AudioDefaultDeviceSelector>defaults){
    if(!activation)throw std::invalid_argument("Volume requires an explicit shared activation vote");
    // Converted only when the borrowed SystemServices snapshot changes; an
    // unchanged comparison does not allocate on slider writes.
    struct Cache{AudioSnapshot source;AudioEndpointSnapshot converted;bool valid{};};const auto cache=std::make_shared<Cache>();
    VolumeProviderAccess a;
    a.readSnapshot=[&service,cache]() -> const AudioEndpointSnapshot&{const auto&s=service.audio();if(!cache->valid||!(cache->source==s)){cache->converted=audioEndpointSnapshotFromSystem(s);cache->source=s;cache->valid=true;}return cache->converted;};
    a.requestActive=std::move(activation);
    a.setVolume=[&service](std::wstring_view,float v){return static_cast<std::int32_t>(service.set_master_volume(v));};
    a.setBalance=[&service](std::wstring_view,float v){return static_cast<std::int32_t>(service.set_output_balance(v));};
    a.setMute=[&service](std::wstring_view,bool v){return static_cast<std::int32_t>(service.set_master_mute(v));};
    a.setAppGain=[&service](std::string_view id,float v){return static_cast<std::int32_t>(service.set_application_gain(std::string(id),v));};
    a.stopApp=[&service](std::string_view id){return static_cast<std::int32_t>(service.stop_application(std::string(id)));};
    a.stopAllApps=[&service]{return static_cast<std::int32_t>(service.stop_applications());};
    if(defaults&&defaults->capability().supported){
        const auto select=[&service,defaults](std::wstring_view id,AudioDefaultFlow flow){
            const auto result=defaults->select(flow,id);
            // Regardless of partial failure, publish the real final topology.
            // A successful output-default action also returns the shared volume
            // control target to the actual default. This call alone never routes
            // audio; only the preceding guarded selector performs that action.
            const auto refresh=flow==AudioDefaultFlow::output&&result.succeeded()?service.select_audio_endpoint({}):service.refresh_audio();
            if(result.rollbackStatus<0)return result.rollbackStatus;if(result.status<0)return result.status;return static_cast<std::int32_t>(refresh);
        };
        a.setDefaultOutput=[select](auto id){return select(id,AudioDefaultFlow::output);};
        a.setDefaultInput=[select](auto id){return select(id,AudioDefaultFlow::input);};
    }return a;
}
#endif
} // namespace endfield::native
