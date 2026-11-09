#include "modules/now_playing_volume.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace endfield::modules {
namespace {
char lower(char c)noexcept{return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;}
// Windows file names and AUMIDs compare case-insensitively; the identities
// involved here are ASCII. Non-ASCII bytes must match exactly (never guessed).
bool caseless(std::string_view a,std::string_view b)noexcept{
    if(a.size()!=b.size())return false;
    for(std::size_t n=0;n<a.size();++n)if(lower(a[n])!=lower(b[n]))return false;
    return true;
}
std::string_view fileName(std::string_view path)noexcept{const auto slash=path.find_last_of("\\/");return slash==std::string_view::npos?path:path.substr(slash+1);}
bool hasDirectory(std::string_view value)noexcept{return value.find_first_of("\\/")!=std::string_view::npos;}
}
namespace {
// One file: the same file identity when both sides carry one, else (replays
// and fixtures, which carry none) the same path.
bool sameFile(std::string_view a,const std::optional<ehud::data::ShelfFileIdentity>&ia,std::string_view b,const std::optional<ehud::data::ShelfFileIdentity>&ib)noexcept{
    if(a.empty()||b.empty()||ia.has_value()!=ib.has_value())return false;
    return ia?ia->matches(*ib):caseless(a,b);
}
bool sameExecutable(std::string_view id,std::string_view path)noexcept{
    return !path.empty()&&(hasDirectory(id)?caseless(path,id):caseless(fileName(path),id));
}
}
bool nowPlayingAudioRouteMatches(const NowPlayingAudioSource&source,const NowPlayingAudioRoute&route,const NowPlayingProcessIdentity&processAppUserModelID){
    const std::string_view id(source.appUserModelID);if(id.empty())return false;
    if(nowPlayingWin32Identity(id))return sameExecutable(id,route.executablePath)||sameExecutable(id,route.applicationPath);
    if(route.appUserModelID){if(!route.appUserModelID->empty()&&caseless(*route.appUserModelID,id))return true;}
    else if(processAppUserModelID){if(const auto identity=processAppUserModelID(route);identity&&caseless(*identity,id))return true;}
    // A packaged player's helper without an AppUserModelID of its own joins
    // it through the package family ("Family!App").
    if(const auto bang=id.find('!');bang!=std::string_view::npos&&bang&&route.appUserModelID&&route.appUserModelID->empty()&&!route.packageFamily.empty()&&caseless(route.packageFamily,id.substr(0,bang)))return true;
    // An explicit AppUserModelID ("Chrome", "MSEdge") resolved to the
    // executable its Start shortcut launches.
    return sameFile(source.executablePath,source.executableIdentity,route.executablePath,route.executableIdentity)
        ||sameFile(source.executablePath,source.executableIdentity,route.applicationPath,route.applicationIdentity);
}
NowPlayingAudioMatch nowPlayingAudioRouteFor(const NowPlayingAudioSource&source,std::span<const NowPlayingAudioRoute>routes,
    const NowPlayingProcessIdentity&processAppUserModelID){
    // Ranks (a6f2d27): a working route (Windows has no preparing state), a
    // process playing now, a failed route (a failed restore or an external
    // mixer change: an exited helper's route is removed), then any match.
    enum Rank:std::size_t{live,playing,ended,matched,ranks};
    std::array<std::size_t,ranks>count{};std::array<std::size_t,ranks>first{};NowPlayingAudioMatch out;
    for(std::size_t n=0;n<routes.size();++n){
        const auto&route=routes[n];if(!nowPlayingAudioRouteMatches(source,route,processAppUserModelID))continue;++out.matches;
        const auto add=[&](Rank rank){if(!count[rank]++)first[rank]=n;};
        if(route.state==NowPlayingAudioRouteState::active)add(live);
        if(route.rendering.value_or(false))add(playing);
        if(route.state==NowPlayingAudioRouteState::failed)add(ended);
        add(matched);
    }
    for(std::size_t rank=0;rank<ranks;++rank)if(count[rank]){if(count[rank]==1)out.route=first[rank];break;}
    return out;
}
NowPlayingVolumeFace nowPlayingVolumeFace(const NowPlayingAudioRoute*route)noexcept{
    if(!route)return {};
    const bool session=route->state!=NowPlayingAudioRouteState::direct;
    const bool available=session||route->available;
    std::optional<double>value;
    if(session&&route->gain&&std::isfinite(*route->gain))value=std::clamp(*route->gain,0.,1.);
    else if(available)value=1;
    return {available,value};
}
NowPlayingVolumeCommand nowPlayingVolumeCommand(const NowPlayingAudioRoute&route,double value)noexcept{
    if(!std::isfinite(value))return NowPlayingVolumeCommand::rejected;
    value=std::clamp(value,0.,1.);
    switch(route.state){
    case NowPlayingAudioRouteState::failed:return value==1?NowPlayingVolumeCommand::stop:NowPlayingVolumeCommand::rejected;
    case NowPlayingAudioRouteState::active:return NowPlayingVolumeCommand::setGain;
    case NowPlayingAudioRouteState::direct:
        if(value==1)return NowPlayingVolumeCommand::unchanged;
        return route.available?NowPlayingVolumeCommand::setGain:NowPlayingVolumeCommand::rejected;
    }
    return NowPlayingVolumeCommand::rejected;
}
}
