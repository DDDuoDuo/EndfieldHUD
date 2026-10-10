// Playing-app volume bridge rules (source NowPlayingCanvas.audioApplication,
// volumeAvailable, currentSession?.gain and setSlider("appVolume")), using
// synthetic routes. The Windows section reads only this test's own process.
#include "native/now_playing_volume.hpp"
#include <cmath>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
namespace n=endfield::native;namespace m=endfield::modules;
namespace {
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}
using S=m::NowPlayingAudioRouteState;
void identity(){
    check(m::nowPlayingWin32Identity("Spotify.exe")&&m::nowPlayingWin32Identity("cloudmusic.EXE")&&m::nowPlayingWin32Identity("C:\\Apps\\QQMusic.exe"),"Executable GSMTC identities");
    check(!m::nowPlayingWin32Identity("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify")&&!m::nowPlayingWin32Identity("Chrome")&&!m::nowPlayingWin32Identity(".exe")&&!m::nowPlayingWin32Identity("C:\\dir\\.exe")&&!m::nowPlayingWin32Identity("App!x.exe"),"Packaged/explicit identities are not executables");
    const std::vector<m::NowPlayingAudioRoute>routes{{"1:1",1,"C:\\Program Files\\Spotify\\Spotify.exe",true,S::direct,{}},{"2:1",2,"C:\\Games\\game.exe",true,S::direct,{}},{"3:1",3,"",true,S::direct,{}},
        {"4:1",4,"C:\\Program Files\\NetEase\\cloudmusic.exe",true,S::direct,{}},{"5:1",5,"C:\\Program Files\\NetEase\\cloudmusic.exe",true,S::direct,{}}};
    check(m::nowPlayingAudioRouteFor("spotify.exe",routes)==0,"Win32 identity matches the executable file name case-insensitively");
    check(m::nowPlayingAudioRouteFor("C:\\program files\\spotify\\SPOTIFY.exe",routes)==0&&!m::nowPlayingAudioRouteFor("D:\\Spotify.exe",routes),"A directory-qualified identity must match the full path");
    check(!m::nowPlayingAudioRouteFor("cloudmusic.exe",routes),"Two matching processes are never guessed (source matches.count == 1)");
    check(!m::nowPlayingAudioRouteFor("Spotify",routes)&&!m::nowPlayingAudioRouteFor("",routes)&&!m::nowPlayingAudioRouteFor("other.exe",routes),"Unknown identities match nothing");
    unsigned calls{};const auto lookup=[&](const m::NowPlayingAudioRoute&r)->std::optional<std::string>{++calls;if(r.id=="2:1")return "Contoso.Player_abc!App";return std::nullopt;};
    check(m::nowPlayingAudioRouteFor("contoso.player_ABC!app",routes,lookup)==1&&calls==routes.size(),"Packaged identity matches the process AUMID case-insensitively");
    calls=0;check(m::nowPlayingAudioRouteFor("Spotify.exe",routes,lookup)==0&&calls==0,"Executable identities never query process identity");
    check(!m::nowPlayingAudioRouteFor("Contoso.Player_abc!App",routes),"Without a process identity resolver packaged players stay unavailable");
    // Identities captured by the shared audio worker are authoritative: the
    // owner thread never opens a process for a route that already carries one.
    auto captured=routes;captured[1].appUserModelID="Contoso.Player_abc!App";captured[2].appUserModelID="";captured[3].appUserModelID="Other.App_x!Main";
    calls=0;check(m::nowPlayingAudioRouteFor("contoso.player_ABC!app",captured,lookup)==1&&calls==2,"Captured identity matches; only uncaptured routes ask the resolver");
    calls=0;check(m::nowPlayingAudioRouteFor("Contoso.Player_abc!App",captured)==1&&calls==0,"Captured identity needs no resolver");
    const auto none=[&](const m::NowPlayingAudioRoute&)->std::optional<std::string>{++calls;return "Contoso.Player_abc!App";};
    auto known=captured;for(auto&r:known)if(!r.appUserModelID)r.appUserModelID="";
    calls=0;check(!m::nowPlayingAudioRouteFor("Contoso.Player_abc!App",std::vector{known[0],known[2],known[3]},none)&&calls==0,"An empty captured identity means the process has none; it is never re-resolved or guessed");
    auto twice=captured;twice[4].appUserModelID="Contoso.Player_abc!App";
    check(!m::nowPlayingAudioRouteFor("Contoso.Player_abc!App",twice),"Two processes with one captured identity stay unavailable");
    check(m::nowPlayingAudioRouteFor("Spotify.exe",captured,none)==0,"Executable identities ignore captured AUMIDs");
}
void faces(){
    m::NowPlayingAudioRoute direct{"1:1",1,"a.exe",true,S::direct,{}},blocked{"1:1",1,"a.exe",false,S::direct,{}},active{"1:1",1,"a.exe",false,S::active,.25},failed{"1:1",1,"a.exe",false,S::failed,.6};
    check(!m::nowPlayingVolumeFace(nullptr).available&&!m::nowPlayingVolumeFace(nullptr).value,"No route: unavailable submenu with an em dash");
    check(m::nowPlayingVolumeFace(&direct).available&&m::nowPlayingVolumeFace(&direct).value==1.,"Startable route shows 100%");
    check(!m::nowPlayingVolumeFace(&blocked).available&&!m::nowPlayingVolumeFace(&blocked).value,"Unsupported process stays unavailable");
    check(m::nowPlayingVolumeFace(&active).available&&m::nowPlayingVolumeFace(&active).value==.25,"Existing route shows its gain");
    check(m::nowPlayingVolumeFace(&failed).available&&m::nowPlayingVolumeFace(&failed).value==.6,"Failed route remains visible so 100% can stop it");
    using C=m::NowPlayingVolumeCommand;
    check(m::nowPlayingVolumeCommand(direct,1)==C::unchanged&&m::nowPlayingVolumeCommand(direct,.5)==C::setGain&&m::nowPlayingVolumeCommand(blocked,.5)==C::rejected,"Direct route: 100% is a no-op, other gains start the route");
    check(m::nowPlayingVolumeCommand(active,1)==C::setGain&&m::nowPlayingVolumeCommand(active,0)==C::setGain,"Active route always writes");
    check(m::nowPlayingVolumeCommand(failed,1)==C::stop&&m::nowPlayingVolumeCommand(failed,.9)==C::rejected,"Failed route stops at 100% only");
    check(m::nowPlayingVolumeCommand(active,std::nan(""))==C::rejected&&m::nowPlayingVolumeCommand(direct,7)==C::unchanged,"Nonfinite values are refused; values are clamped");
}
// Shared rows gain AudioApplicationRoute::appUserModelID from the audio
// worker; until then the conversion reports "not captured".
template<class Route>void capturedIdentity(Route route){
    if constexpr(requires(Route&r){r.appUserModelID=std::string();}){
        route.appUserModelID="Contoso.Player_abc!App";check(n::nowPlayingAudioRoutes(std::span(&route,1))[0].appUserModelID==std::optional<std::string>("Contoso.Player_abc!App"),"Captured process identity is copied");
        route.appUserModelID.clear();check(n::nowPlayingAudioRoutes(std::span(&route,1))[0].appUserModelID==std::optional<std::string>(""),"A process without identity stays known-empty");
    }else check(!n::nowPlayingAudioRoutes(std::span(&route,1))[0].appUserModelID,"Rows without a captured identity are not invented");
}
void conversion(){
    n::AudioApplicationRoute a;a.id="42:9";a.pid=42;a.available=true;a.state=n::AudioApplicationRouteState::active;a.gain=.5f;a.executable=n::AudioApplicationExecutable{"C:\\x\\Spotify.exe",{}};
    n::AudioApplicationRoute b;b.id="43:9";b.pid=43;b.state=n::AudioApplicationRouteState::failed;
    const std::vector<n::AudioApplicationRoute>input{a,b};const auto out=n::nowPlayingAudioRoutes(input);
    check(out.size()==2&&out[0].id=="42:9"&&out[0].pid==42&&out[0].executablePath=="C:\\x\\Spotify.exe"&&out[0].state==S::active&&out[0].gain==.5&&out[0].available,"Shared audio rows convert without loss");
    check(out[1].executablePath.empty()&&out[1].state==S::failed&&!out[1].gain&&!out[1].available,"Missing executable metadata stays empty");
    capturedIdentity(a);
#ifdef _WIN32
    FILETIME created{},exited{},kernel{},user{};check(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=0,"Read own process creation time");
    const auto stamp=(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;const auto pid=static_cast<std::uint32_t>(GetCurrentProcessId());
    m::NowPlayingAudioRoute self{std::to_string(pid)+":"+std::to_string(stamp),pid,"",true,S::direct,{}};
    check(!n::nowPlayingProcessAppUserModelID(self),"An unpackaged process has no package AUMID");
    m::NowPlayingAudioRoute reused=self;reused.id=std::to_string(pid)+":"+std::to_string(stamp+1);check(!n::nowPlayingProcessAppUserModelID(reused),"A reused PID (different creation time) is rejected");
    m::NowPlayingAudioRoute garbage=self;garbage.id="nonsense";check(!n::nowPlayingProcessAppUserModelID(garbage),"Malformed route identity is rejected");
    m::NowPlayingAudioRoute mismatch=self;mismatch.pid=pid+1;check(!n::nowPlayingProcessAppUserModelID(mismatch),"Route PID must match its identity");
#endif
}
}
int main(){
    try{identity();faces();conversion();std::cout<<"Now Playing volume bridge: "<<checks<<" checks passed (synthetic routes)\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Now Playing volume bridge failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
