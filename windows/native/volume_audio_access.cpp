#include "native/volume_audio_access.hpp"

namespace endfield::native {
namespace {
constexpr std::int32_t rejected=static_cast<std::int32_t>(0x8000000Au); // E_PENDING: queue closed/full
std::int32_t accepted(bool value)noexcept{return value?0:rejected;}
}
VolumeProviderAccess volumeProviderAccess(AudioService&service){
    VolumeProviderAccess a;
    a.readSnapshot=[&service]() -> const AudioEndpointSnapshot&{return service.snapshot();};
    a.requestActive=[&service](bool active){return service.vote(AudioVoter::volume,active);};
    a.setVolume=[&service](std::wstring_view endpoint,float value){return accepted(service.setVolume(std::wstring(endpoint),value));};
    a.setBalance=[&service](std::wstring_view endpoint,float value){return accepted(service.setBalance(std::wstring(endpoint),value));};
    a.setMute=[&service](std::wstring_view endpoint,bool value){return accepted(service.setMute(std::wstring(endpoint),value));};
    a.setAppGain=[&service](std::string_view id,float value){return accepted(service.setApplicationGain(std::string(id),value));};
    a.stopApp=[&service](std::string_view id){return accepted(service.stopApplication(std::string(id)));};
    a.stopAllApps=[&service]{return accepted(service.stopApplications());};
    return a;
}
} // namespace endfield::native
