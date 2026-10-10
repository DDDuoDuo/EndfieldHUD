#include "modules/now_playing_volume.hpp"
#include <algorithm>
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
bool nowPlayingWin32Identity(std::string_view id)noexcept{
    return id.size()>4&&caseless(id.substr(id.size()-4),".exe")&&id.find('!')==std::string_view::npos&&!fileName(id).empty()&&fileName(id).size()>4;
}
std::optional<std::size_t>nowPlayingAudioRouteFor(std::string_view id,std::span<const NowPlayingAudioRoute>routes,
    const std::function<std::optional<std::string>(const NowPlayingAudioRoute&)>&processAppUserModelID){
    if(id.empty())return {};
    std::optional<std::size_t>match;std::size_t count{};
    const bool win32=nowPlayingWin32Identity(id);
    for(std::size_t n=0;n<routes.size();++n){
        const auto&route=routes[n];bool same{};
        if(win32){
            if(!route.executablePath.empty())same=hasDirectory(id)?caseless(route.executablePath,id):caseless(fileName(route.executablePath),id);
        }else if(route.appUserModelID){
            same=!route.appUserModelID->empty()&&caseless(*route.appUserModelID,id);
        }else if(processAppUserModelID){
            if(const auto identity=processAppUserModelID(route))same=caseless(*identity,id);
        }
        if(same){match=n;++count;}
    }
    return count==1?match:std::nullopt;
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
