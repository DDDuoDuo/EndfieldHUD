#include "native/now_playing_volume.hpp"
#include <charconv>
#include <cmath>
#include <optional>
#include <string>

namespace endfield::native {
namespace {
// The shared audio worker captures each process's AppUserModelID next to its
// executable (AudioApplicationRoute::appUserModelID). Until every build of the
// shared row carries that member, an absent member means "not captured" and
// the owner may fall back to NowPlayingAppVolume::processAppUserModelID.
template<class Route>std::optional<std::string>capturedAppUserModelID(const Route&route){
    if constexpr(requires(const Route&value){std::string(value.appUserModelID);})return std::string(route.appUserModelID);
    else return std::nullopt;
}
}
std::vector<modules::NowPlayingAudioRoute>nowPlayingAudioRoutes(std::span<const AudioApplicationRoute>routes){
    std::vector<modules::NowPlayingAudioRoute>out;out.reserve(routes.size());
    for(const auto&route:routes){
        modules::NowPlayingAudioRoute next;next.id=route.id;next.pid=route.pid;next.available=route.available;
        if(route.executable)next.executablePath=route.executable->path;
        next.state=route.state==AudioApplicationRouteState::active?modules::NowPlayingAudioRouteState::active
            :route.state==AudioApplicationRouteState::failed?modules::NowPlayingAudioRouteState::failed:modules::NowPlayingAudioRouteState::direct;
        if(route.gain&&std::isfinite(*route.gain))next.gain=static_cast<double>(*route.gain);
        next.appUserModelID=capturedAppUserModelID(route);
        out.push_back(std::move(next));
    }
    return out;
}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <appmodel.h>

namespace endfield::native {
std::optional<std::string>nowPlayingProcessAppUserModelID(const modules::NowPlayingAudioRoute&route){
    const std::string_view id(route.id);const auto colon=id.find(':');if(colon==std::string_view::npos||!route.pid)return {};
    std::uint32_t pid{};std::uint64_t stamp{};
    if(std::from_chars(id.data(),id.data()+colon,pid).ptr!=id.data()+colon||pid!=route.pid)return {};
    if(std::from_chars(id.data()+colon+1,id.data()+id.size(),stamp).ptr!=id.data()+id.size())return {};
    const HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return {};
    struct Close{HANDLE value;~Close(){CloseHandle(value);}}close{process};
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(process,&created,&exited,&kernel,&user))return {};
    if(((std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime)!=stamp)return {};
    wchar_t buffer[APPLICATION_USER_MODEL_ID_MAX_LENGTH]{};UINT32 length=APPLICATION_USER_MODEL_ID_MAX_LENGTH;
    if(GetApplicationUserModelId(process,&length,buffer)!=ERROR_SUCCESS||!length)return {};
    const int wide=static_cast<int>(wcsnlen(buffer,APPLICATION_USER_MODEL_ID_MAX_LENGTH));
    const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,buffer,wide,nullptr,0,nullptr,nullptr);if(bytes<=0)return {};
    std::string out(static_cast<std::size_t>(bytes),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,buffer,wide,out.data(),bytes,nullptr,nullptr)!=bytes)return {};
    return out;
}
}
#endif
