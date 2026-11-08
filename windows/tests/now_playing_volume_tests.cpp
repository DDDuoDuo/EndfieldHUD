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
// The route picked for a player known only by its media session identity.
std::optional<std::size_t>routeFor(std::string_view id,std::span<const m::NowPlayingAudioRoute>routes,const m::NowPlayingProcessIdentity&lookup={}){
    return m::nowPlayingAudioRouteFor({std::string(id),{},{}},routes,lookup).route;
}
void identity(){
    check(m::nowPlayingWin32Identity("Spotify.exe")&&m::nowPlayingWin32Identity("cloudmusic.EXE")&&m::nowPlayingWin32Identity("C:\\Apps\\QQMusic.exe"),"Executable GSMTC identities");
    check(!m::nowPlayingWin32Identity("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify")&&!m::nowPlayingWin32Identity("Chrome")&&!m::nowPlayingWin32Identity(".exe")&&!m::nowPlayingWin32Identity("C:\\dir\\.exe")&&!m::nowPlayingWin32Identity("App!x.exe"),"Packaged/explicit identities are not executables");
    const std::vector<m::NowPlayingAudioRoute>routes{{"1:1",1,"C:\\Program Files\\Spotify\\Spotify.exe",true,S::direct,{}},{"2:1",2,"C:\\Games\\game.exe",true,S::direct,{}},{"3:1",3,"",true,S::direct,{}},
        {"4:1",4,"C:\\Program Files\\NetEase\\cloudmusic.exe",true,S::direct,{}},{"5:1",5,"C:\\Program Files\\NetEase\\cloudmusic.exe",true,S::direct,{}}};
    check(routeFor("spotify.exe",routes)==0,"Win32 identity matches the executable file name case-insensitively");
    check(routeFor("C:\\program files\\spotify\\SPOTIFY.exe",routes)==0&&!routeFor("D:\\Spotify.exe",routes),"A directory-qualified identity must match the full path");
    check(!routeFor("cloudmusic.exe",routes),"Two matching processes are never guessed (source matches.count == 1)");
    check(!routeFor("Spotify",routes)&&!routeFor("",routes)&&!routeFor("other.exe",routes),"Unknown identities match nothing");
    unsigned calls{};const auto lookup=[&](const m::NowPlayingAudioRoute&r)->std::optional<std::string>{++calls;if(r.id=="2:1")return "Contoso.Player_abc!App";return std::nullopt;};
    check(routeFor("contoso.player_ABC!app",routes,lookup)==1&&calls==routes.size(),"Packaged identity matches the process AUMID case-insensitively");
    calls=0;check(routeFor("Spotify.exe",routes,lookup)==0&&calls==0,"Executable identities never query process identity");
    check(!routeFor("Contoso.Player_abc!App",routes),"Without a process identity resolver packaged players stay unavailable");
    // Identities captured by the shared audio worker are authoritative: the
    // owner thread never opens a process for a route that already carries one.
    auto captured=routes;captured[1].appUserModelID="Contoso.Player_abc!App";captured[2].appUserModelID="";captured[3].appUserModelID="Other.App_x!Main";
    calls=0;check(routeFor("contoso.player_ABC!app",captured,lookup)==1&&calls==2,"Captured identity matches; only uncaptured routes ask the resolver");
    calls=0;check(routeFor("Contoso.Player_abc!App",captured)==1&&calls==0,"Captured identity needs no resolver");
    const auto none=[&](const m::NowPlayingAudioRoute&)->std::optional<std::string>{++calls;return "Contoso.Player_abc!App";};
    auto known=captured;for(auto&r:known)if(!r.appUserModelID)r.appUserModelID="";
    calls=0;check(!routeFor("Contoso.Player_abc!App",std::vector{known[0],known[2],known[3]},none)&&calls==0,"An empty captured identity means the process has none; it is never re-resolved or guessed");
    auto twice=captured;twice[4].appUserModelID="Contoso.Player_abc!App";
    check(!routeFor("Contoso.Player_abc!App",twice),"Two processes with one captured identity stay unavailable");
    check(routeFor("Spotify.exe",captured,none)==0,"Executable identities ignore captured AUMIDs");
}
ehud::data::ShelfFileIdentity file(std::uint8_t object){ehud::data::ShelfFileIdentity value;value.objectID[0]=object;value.volumeSerial=0x5eed;return value;}
m::NowPlayingAudioRoute process(std::uint32_t pid,std::string path,std::uint8_t object,bool rendering,S state=S::direct){
    m::NowPlayingAudioRoute route{std::to_string(pid)+":1",pid,std::move(path),true,state,{}};route.appUserModelID="";route.rendering=rendering;route.executableIdentity=file(object);
    if(state!=S::direct)route.gain=.5;return route;
}
// Source d09780d: a browser's audio plays through a helper; Windows finds the
// browser's executable from the Start shortcut of its explicit AppUserModelID
// (AppsFolder, the live probe: "Chrome" and "MSEdge" give plain exe paths).
void browsers(){
    const std::string chromeExe="C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
    const m::NowPlayingAudioSource chrome{"Chrome",chromeExe,file(1)},unresolved{"Chrome",{},{}};
    // The live Chrome probe: the main chrome.exe holds no session; the audio
    // service utility (chrome.exe again) does.
    auto main=process(100,chromeExe,1,false),utility=process(101,chromeExe,1,true);
    std::vector routes{main,utility,process(102,"C:\\Games\\game.exe",2,true)};
    auto match=m::nowPlayingAudioRouteFor(chrome,routes);
    check(match.route==1&&match.matches==2,"A resolved Chrome picks the rendering utility over the silent main process");
    routes[1].rendering=false;match=m::nowPlayingAudioRouteFor(chrome,routes);check(!match.route&&match.matches==2,"Two silent unrouted Chrome processes are never guessed between");
    routes={utility};check(m::nowPlayingAudioRouteFor(chrome,routes).route==0,"A Chrome playing through its one helper is adjustable");
    check(!m::nowPlayingAudioRouteFor(unresolved,routes).route&&m::nowPlayingAudioRouteFor(unresolved,routes).matches==0,"An unresolved Chrome matches nothing");
    auto other=utility;other.executablePath="D:\\Portable\\Chrome\\chrome.exe";other.executableIdentity=file(9);
    check(!m::nowPlayingAudioRouteFor(chrome,std::vector{other}).route,"A chrome.exe at another place is another browser");
    auto linked=utility;linked.executablePath="C:\\PROGRA~1\\Google\\Chrome\\Application\\chrome.exe";
    check(m::nowPlayingAudioRouteFor(chrome,std::vector{linked}).route==0,"The same file under another spelling of its path matches by identity");
    auto replaced=utility;replaced.executableIdentity=file(3);
    check(!m::nowPlayingAudioRouteFor(chrome,std::vector{replaced}).route,"Another file at the resolved path does not match");
    auto bare=utility;bare.executableIdentity.reset();
    check(!m::nowPlayingAudioRouteFor(chrome,std::vector{bare}).route,"A row without an identity never matches a resolved file");
    check(m::nowPlayingAudioRouteFor({"Chrome",chromeExe,{}},std::vector{bare}).route==0,"Without identities on both sides (replays) the path decides");
    // A helper inside the browser's app (a different executable) matches
    // through the app it plays for.
    auto helper=process(103,"C:\\Program Files\\Google\\Chrome\\Application\\130.0\\helper.exe",4,true);helper.applicationPath=chromeExe;helper.applicationIdentity=file(1);
    check(m::nowPlayingAudioRouteFor(chrome,std::vector{main,helper}).route==1,"A helper inside the browser's folder matches through its app");
    // Firefox (synthetic only: not installed on the probe laptop).
    const std::string firefoxExe="C:\\Program Files\\Mozilla Firefox\\firefox.exe";
    const m::NowPlayingAudioSource firefox{"308046B0AF4A39CB",firefoxExe,file(5)};
    std::vector fox{process(200,firefoxExe,5,false),process(201,firefoxExe,5,false),process(202,firefoxExe,5,true)};
    match=m::nowPlayingAudioRouteFor(firefox,fox);check(match.route==2&&match.matches==3,"A resolved Firefox picks its one rendering process");
    match=m::nowPlayingAudioRouteFor({"firefox.exe",{},{}},fox);check(match.route==2&&match.matches==3,"\"firefox.exe\" with several processes, one rendering, matches that one");
    // The live Hypergryph probe: the launcher's media session is the bare
    // "Games.exe"; its audio plays from QtWebEngineProcess.exe in its folder.
    auto web=process(301,"D:\\Hypergryph Launcher\\web\\QtWebEngineProcess.exe",6,true);web.applicationPath="D:\\Hypergryph Launcher\\Games.exe";web.applicationIdentity=file(7);
    check(routeFor("Games.exe",std::vector{process(300,web.applicationPath,7,false),web})==1,"A bare executable identity matches a helper through its app and prefers the one playing");
    check(!routeFor("Games.exe",std::vector{process(302,"C:\\x\\QtWebEngineProcess.exe",8,true)}),"A helper of another app is never matched");
}
// Source a6f2d27: a live route, then the process playing, then a failed
// route, then the only match.
void ranking(){
    const std::string exe="C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";const m::NowPlayingAudioSource chrome{"Chrome",exe,file(1)};
    auto stale=process(100,exe,1,false,S::failed),playing=process(101,exe,1,true);
    check(m::nowPlayingAudioRouteFor(chrome,std::vector{stale,playing}).route==1,"A failed route left on a replaced helper loses to the helper now playing");
    auto silent=process(102,exe,1,false);
    check(m::nowPlayingAudioRouteFor(chrome,std::vector{silent,stale}).route==1,"A silent helper with a failed route beats one never routed");
    auto live=process(103,exe,1,false,S::active);
    check(m::nowPlayingAudioRouteFor(chrome,std::vector{playing,live,stale}).route==1,"A working route beats the process playing");
    check(!m::nowPlayingAudioRouteFor(chrome,std::vector{live,process(104,exe,1,false,S::active)}).route,"Two working routes are never guessed between");
    check(!m::nowPlayingAudioRouteFor(chrome,std::vector{playing,process(105,exe,1,true)}).route,"Two playing processes are never guessed between");
    check(!m::nowPlayingAudioRouteFor(chrome,std::vector{stale,process(106,exe,1,false,S::failed)}).route,"Two failed routes are never guessed between");
    auto unknown=silent;unknown.rendering.reset();
    check(m::nowPlayingAudioRouteFor(chrome,std::vector{unknown,playing}).route==1,"A process whose play state is not captured is not playing");
}
// R-D4: a packaged player's helper joins it through the package family only
// when the helper has no AppUserModelID of its own.
void packageFamily(){
    auto helper=process(1,"C:\\Program Files\\WindowsApps\\Contoso.Player_1.0_x64__abc\\Helper.exe",1,true);helper.packageFamily="Contoso.Player_abc";
    auto main=helper;main.id="2:1";main.pid=2;main.appUserModelID="Contoso.Player_abc!App";main.rendering=false;
    auto match=m::nowPlayingAudioRouteFor({"contoso.player_ABC!App",{},{}},std::vector{main,helper});
    check(match.route==1&&match.matches==2,"A helper without its own AppUserModelID joins its package's player; the playing one wins");
    auto own=helper;own.appUserModelID="Contoso.Player_abc!Other";
    match=m::nowPlayingAudioRouteFor({"Contoso.Player_abc!App",{},{}},std::vector{main,own});check(match.route==0&&match.matches==1,"A helper with another AppUserModelID of the package does not join");
    auto stranger=helper;stranger.packageFamily="Other.Player_x";
    check(!m::nowPlayingAudioRouteFor({"Contoso.Player_abc!App",{},{}},std::vector{stranger}).route,"Another package's helper does not join");
    auto uncaptured=helper;uncaptured.appUserModelID.reset();
    check(!m::nowPlayingAudioRouteFor({"Contoso.Player_abc!App",{},{}},std::vector{uncaptured}).route,"A helper whose identity was not captured does not join by family");
    check(!m::nowPlayingAudioRouteFor({"!App",{},{}},std::vector{helper}).route,"An identity without a family joins nothing");
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
    check(out[0].executableIdentity==a.executable->identity&&out[0].applicationPath.empty()&&!out[0].applicationIdentity&&out[0].packageFamily.empty()&&!out[1].executableIdentity,"A process that is its own app carries its executable's identity only");
    n::AudioApplicationRoute helper=a;helper.application=n::AudioApplicationExecutable{"C:\\x\\Launcher.exe",{}};helper.application->identity.objectID[0]=4;helper.packageFamily="Contoso.Player_abc";
    const auto converted=n::nowPlayingAudioRoutes(std::span(&helper,1));
    check(converted[0].applicationPath=="C:\\x\\Launcher.exe"&&converted[0].applicationIdentity==helper.application->identity&&converted[0].packageFamily=="Contoso.Player_abc","A helper's app and package family are copied");
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
    try{identity();browsers();ranking();packageFamily();faces();conversion();std::cout<<"Now Playing volume bridge: "<<checks<<" checks passed (synthetic routes)\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Now Playing volume bridge failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
