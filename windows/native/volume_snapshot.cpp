#include "native/volume_snapshot.hpp"
#include "native/system_services.hpp"
#include "core/data/json.hpp"
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
void string(std::string_view text,bool empty=true){need((empty||!text.empty())&&text.size()<=4096&&Json::validUtf8(text)&&text.find('\0')==std::string_view::npos,"Invalid Volume text/identity");}
std::string utf8(std::wstring_view value){
    std::string result;for(std::size_t i=0;i<value.size();++i){std::uint32_t scalar=static_cast<std::uint32_t>(value[i]);
        if constexpr(sizeof(wchar_t)==2){if(scalar>=0xd800&&scalar<=0xdbff){need(i+1<value.size(),"Invalid audio UTF16 name");const auto low=static_cast<std::uint32_t>(value[++i]);need(low>=0xdc00&&low<=0xdfff,"Invalid audio UTF16 name");scalar=0x10000+((scalar-0xd800)<<10)+(low-0xdc00);}else need(!(scalar>=0xdc00&&scalar<=0xdfff),"Invalid audio UTF16 name");}
        need(scalar<=0x10ffff&&!(scalar>=0xd800&&scalar<=0xdfff)&&scalar!=0,"Invalid audio text scalar");if(scalar<0x80)result.push_back(static_cast<char>(scalar));else if(scalar<0x800){result.push_back(static_cast<char>(0xc0|(scalar>>6)));result.push_back(static_cast<char>(0x80|(scalar&63)));}else if(scalar<0x10000){result.push_back(static_cast<char>(0xe0|(scalar>>12)));result.push_back(static_cast<char>(0x80|((scalar>>6)&63)));result.push_back(static_cast<char>(0x80|(scalar&63)));}else{result.push_back(static_cast<char>(0xf0|(scalar>>18)));result.push_back(static_cast<char>(0x80|((scalar>>12)&63)));result.push_back(static_cast<char>(0x80|((scalar>>6)&63)));result.push_back(static_cast<char>(0x80|(scalar&63)));}}
    return result;
}
}
void validateVolumeSnapshot(const VolumeSnapshot&s){
    string(s.outputID);string(s.inputID);std::size_t bytes{};
    auto tokens=[&](const auto&list){std::set<std::string,std::less<>>ids;for(const auto&v:list){string(v.id,false);string(v.name,false);need(ids.insert(v.id).second,"Repeated Volume object identity");bytes+=v.id.size()+v.name.size();need(bytes<=16*1024*1024,"Volume snapshot exceeds retained text budget");}};
    tokens(s.outputs);tokens(s.inputs);tokens(s.applications);
    for(const auto&app:s.applications){need(app.state>=VolumeAppState::direct&&app.state<=VolumeAppState::failed,"Invalid Volume route state");if(app.gain)need(std::isfinite(*app.gain)&&*app.gain>=0&&*app.gain<=1,"Invalid app gain");if(app.error)string(*app.error);
        if(app.executable){need(validAudioApplicationExecutable(*app.executable),"Invalid Volume executable metadata");bytes+=app.executable->path.size();}
        if(app.icon){string(app.icon->imageKey,false);need(app.icon->imageKey.size()<=512&&app.icon->revision>0&&app.icon->revision<=static_cast<std::uint64_t>(INT64_MAX),"Invalid Volume app icon revision/key");bytes+=app.icon->imageKey.size();}
        need(bytes<=16*1024*1024,"Volume snapshot exceeds retained metadata budget");}
    for(const auto*text:{&s.status,&s.applicationMessage,&s.perAppStatus})if(*text)string(**text);
    if(s.volume)need(std::isfinite(*s.volume)&&*s.volume>=0&&*s.volume<=1,"Invalid output volume");if(s.balance)need(std::isfinite(*s.balance)&&*s.balance>=-1&&*s.balance<=1,"Invalid output balance");
}
VolumeSnapshot volumeSnapshotFromEndpoints(const AudioEndpointSnapshot&audio){VolumeSnapshot s;s.outputID=utf8(audio.defaultOutputID);s.inputID=utf8(audio.defaultInputID);
    for(const auto&d:audio.outputs)s.outputs.push_back({utf8(d.id),utf8(d.name),d.headphones,d.bluetooth});for(const auto&d:audio.inputs)s.inputs.push_back({utf8(d.id),utf8(d.name),d.headphones,d.bluetooth});
    if(audio.available&&!audio.paused){s.volume=audio.volume?std::optional<double>(*audio.volume):std::nullopt;s.muted=audio.muted;s.balance=audio.balance?std::optional<double>(*audio.balance):std::nullopt;s.canSetVolume=audio.canSetVolume&&s.volume.has_value();s.canSetMute=audio.canSetMute&&s.muted.has_value();s.canSetBalance=audio.canSetBalance&&s.balance.has_value();}
    s.applicationActivitySupported=audio.applicationsSupported&&!audio.applicationsPaused&&!audio.paused;s.routingStopped=audio.routesStoppedByDeviceChange;
    for(const auto&a:audio.applications){VolumeApplication app;app.id=a.id;app.name=utf8(a.name);app.pid=a.pid;app.available=a.available&&s.applicationActivitySupported;app.state=a.state==AudioApplicationRouteState::active?VolumeAppState::active:a.state==AudioApplicationRouteState::failed?VolumeAppState::failed:VolumeAppState::direct;if(a.gain)app.gain=*a.gain;app.executable=a.executable;s.applications.push_back(std::move(app));}return s;}
AudioEndpointSnapshot audioEndpointSnapshotFromSystem(const AudioSnapshot&audio){AudioEndpointSnapshot e;e.paused=audio.paused;e.available=audio.available;
    for(const auto&d:audio.devices)e.outputs.push_back({d.id,d.name,d.headphones,false});for(const auto&d:audio.inputs)e.inputs.push_back({d.id,d.name,d.headphones,false});
    e.defaultOutputID=audio.controlled_device_id.empty()?audio.default_device_id:audio.controlled_device_id;e.defaultInputID=audio.default_input_device_id;
    e.volume=audio.volume;e.muted=audio.muted;e.balance=audio.balance;e.canSetVolume=audio.volume.has_value();e.canSetMute=audio.muted.has_value();e.canSetBalance=audio.can_set_balance&&audio.balance.has_value();
    e.error=audio.error;e.inputError=audio.input_error;e.applicationsSupported=audio.application_supported;e.applicationsPaused=audio.applications_paused;e.applications=audio.applications;e.applicationError=audio.application_error;return e;}
} // namespace endfield::native
