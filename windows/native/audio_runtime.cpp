#include "native/audio_runtime.hpp"
#include "native/volume_audio_access.hpp"
#include "native/volume_strings.hpp"
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <utility>

namespace endfield::native {
namespace {
constexpr std::int32_t wrongThread=static_cast<std::int32_t>(0x8001010Eu); // RPC_E_WRONG_THREAD
// Source-equivalent label hygiene: the topology list already carries trimmed,
// nonempty labels; anything that cannot be represented as UTF-8 is withheld.
std::optional<std::map<std::string,std::string>>labels(const AudioEndpointSnapshot&s){
    if(!s.topologyActive||!s.topologyListed)return std::nullopt;
    std::map<std::string,std::string>out;
    for(const auto&d:s.topology){
        auto id=audioUtf8(d.id);auto name=audioUtf8(d.name);
        // An unrepresentable entry makes the read incomplete, like the source
        // watcher's early return: never a false disconnect.
        if(!id||!name||id->empty()||name->empty()||!out.emplace(std::move(*id),std::move(*name)).second)return std::nullopt;
    }
    return out;
}
core::Language resolved(core::Language language){
    if(language==core::Language::system)throw std::invalid_argument("Resolve the System language before localizing audio");
    return language;
}
}

struct AudioRuntime::State {
    std::thread::id owner=std::this_thread::get_id();
    // At most one owner message is outstanding: the worker posts only when
    // none is pending, and drain() re-arms before adopting the publication.
    std::shared_ptr<std::atomic<bool>>pending=std::make_shared<std::atomic<bool>>(false);
    AudioService service;
    VolumeProviderBinding binding;
    core::Language language;
    std::vector<AudioApplicationRoute>applications;std::uint64_t applicationsRevision{};
    std::uint64_t topologyGeneration{};bool topologyListed{};
    bool stopped{};
    State(AudioServiceOptions options,core::Language l,Notice notice)
        :service(std::move(options),coalesced(pending,std::move(notice))),binding(volumeProviderAccess(service),volumeErrorText(l)),language(l){}
    static AudioService::Notice coalesced(std::shared_ptr<std::atomic<bool>>flag,Notice notice){
        if(!notice)return {};
        // A notice that throws (message not posted) re-arms, so the next
        // publication tries again instead of leaving the owner deaf.
        return [flag=std::move(flag),notice=std::move(notice)]{
            if(flag->exchange(true,std::memory_order_acq_rel))return;
            try{notice();}catch(...){flag->store(false,std::memory_order_release);throw;}
        };
    }
    bool onOwner()const noexcept{return std::this_thread::get_id()==owner;}
};

AudioRuntime::AudioRuntime(AudioServiceOptions options,core::Language language,Notice notice)
    :state_(std::make_unique<State>(std::move(options),resolved(language),std::move(notice))){}
AudioRuntime::~AudioRuntime(){stop();}

AudioRuntimeChanges AudioRuntime::drain(){
    auto&s=*state_;AudioRuntimeChanges changes;
    if(s.stopped||!s.onOwner())return changes;
    s.pending->store(false,std::memory_order_release);
    if(!s.service.drain())return changes;
    changes.snapshot=true;
    const auto&current=s.service.snapshot();
    // The binding republishes only while Volume is visible and selected; its
    // receiver updates the Volume owner synchronously.
    changes.volume=s.binding.receive();
    if(current.applications!=s.applications){s.applications=current.applications;++s.applicationsRevision;changes.applications=true;}
    if(!current.topologyListed||!current.topologyActive)s.topologyListed=false;
    else if(!s.topologyListed||current.topologyGeneration!=s.topologyGeneration){
        s.topologyListed=true;s.topologyGeneration=current.topologyGeneration;changes.devices=true;
    }
    return changes;
}
VolumeProviderBinding&AudioRuntime::volume()noexcept{return state_->binding;}
const AudioEndpointSnapshot&AudioRuntime::snapshot()const noexcept{return state_->service.snapshot();}
std::uint64_t AudioRuntime::revision()const noexcept{return state_->service.revision();}
bool AudioRuntime::setLanguage(core::Language language){
    auto&s=*state_;resolved(language);
    if(s.stopped||!s.onOwner()||s.language==language)return false;
    s.language=language;s.binding.setErrorText(volumeErrorText(language));return true;
}
core::Language AudioRuntime::language()const noexcept{return state_->language;}
std::int32_t AudioRuntime::setDeviceEvents(bool enabled){auto&s=*state_;if(!s.onOwner())return wrongThread;return s.service.vote(AudioVoter::eventLog,enabled);}
std::optional<std::map<std::string,std::string>>AudioRuntime::deviceLabels()const{
    const auto&s=*state_;if(!s.topologyListed)return std::nullopt;return labels(s.service.snapshot());
}
std::int32_t AudioRuntime::setNowPlayingVisible(bool visible){auto&s=*state_;if(!s.onOwner())return wrongThread;return s.service.vote(AudioVoter::nowPlaying,visible);}
std::span<const AudioApplicationRoute>AudioRuntime::applications()const noexcept{return state_->applications;}
std::uint64_t AudioRuntime::applicationsRevision()const noexcept{return state_->applicationsRevision;}
bool AudioRuntime::setApplicationGain(std::string_view route,double gain){
    auto&s=*state_;if(s.stopped||!s.onOwner()||!std::isfinite(gain)||gain<0||gain>1)return false;
    return s.service.setApplicationGain(std::string(route),static_cast<float>(gain));
}
bool AudioRuntime::stopApplication(std::string_view route){auto&s=*state_;if(s.stopped||!s.onOwner())return false;return s.service.stopApplication(std::string(route));}
bool AudioRuntime::powerSuspend(){auto&s=*state_;return !s.stopped&&s.onOwner()&&s.service.powerSuspend();}
bool AudioRuntime::powerResume(){auto&s=*state_;return !s.stopped&&s.onOwner()&&s.service.powerResume();}
bool AudioRuntime::recover(){auto&s=*state_;return !s.stopped&&s.onOwner()&&s.service.recover();}
AudioServiceStats AudioRuntime::stats()const{return state_->service.stats();}
void AudioRuntime::stop()noexcept{
    auto&s=*state_;if(s.stopped)return;s.stopped=true;
    s.binding.close();s.service.stop();
}
} // namespace endfield::native
