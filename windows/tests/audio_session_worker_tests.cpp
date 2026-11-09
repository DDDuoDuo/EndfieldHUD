#include "native/audio_session_worker.hpp"
#include "native/process_identity.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
namespace {
using namespace endfield::native;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Fake final:AudioSessionBackend {
    std::vector<AudioSessionRecord>records{{"a","42:100",L"Synthetic app",42,true,true,.4f,0},{"b","42:100",L"Synthetic app",42,true,true,.8f,0}};
    std::vector<std::string>calls;std::int32_t failWrite{};unsigned writes{};Wake wake;
    // Processes that run on after their last session expired.
    std::set<std::string,std::less<>>running;
    std::int32_t open(std::wstring_view,Wake w)override{wake=std::move(w);calls.push_back("open");return 0;}
    std::int32_t resume(Wake w)override{wake=std::move(w);calls.push_back("resume");return 0;}
    void pause()noexcept override{wake={};}
    std::int32_t read(std::vector<AudioSessionRecord>&out)override{calls.push_back("read");out=records;return 0;}
    std::int32_t readVolume(std::string_view id,float&v)override{calls.push_back("get:"+std::string(id));for(const auto&r:records)if(r.id==id){if(!r.volume)return -99;v=*r.volume;return 0;}return -98;}
    std::int32_t writeVolume(std::string_view id,float v)override{calls.push_back("set:"+std::string(id));for(auto&r:records)if(r.id==id){r.volume=v;return failWrite&&++writes==static_cast<unsigned>(failWrite)?-77:0;}return -98;}
    bool processRunning(std::string_view key)noexcept override{try{calls.push_back("running:"+std::string(key));}catch(...){}return running.contains(key);}
    void close()noexcept override{wake={};}
};
void transactions(){Fake f;AudioSessionRoutes routes;routes.update(f.records,f);check(routes.applications().size()==1&&routes.applications()[0].state==AudioApplicationRouteState::direct&&!routes.applications()[0].gain,"Direct100% leaves existing native mixer levels untouched");
    check(routes.setGain("42:100",1,f)==0&&f.calls.empty(),"Unity direct gain creates no native route/writes");
    check(routes.setGain("42:100",.5f,f)==0&&f.records[0].volume==.2f&&f.records[1].volume==.4f,"One confirmed process group attenuates every original session relatively");
    check(f.calls.size()>=2&&f.calls[0]=="get:a"&&f.calls[1]=="get:b","Both identity-bound originals read before the first write");
    check(routes.applications()[0].state==AudioApplicationRouteState::active&&routes.applications()[0].gain==.5f,"UI reports owned relative gain rather than guessed native absolute average");
    const auto previous=f.calls.size();check(routes.setGain("42:100",.5f,f)==0&&f.calls.size()==previous,"Unchanged owned gain is no native request");
    f.records.push_back({"c","42:100",L"Synthetic app",42,true,true,.6f,0});routes.update(f.records,f);check(f.records[2].volume==.3f,"New same-process session adopts explicitly owned attenuation");
    f.records[0].active=f.records[1].active=f.records[2].active=false;routes.update(f.records,f);check(routes.applications().size()==1&&routes.applications()[0].state==AudioApplicationRouteState::active,"Owned source route stays visible while app audio is silent");
    check(routes.stop("42:100",f)==0&&f.records[0].volume==.4f&&f.records[1].volume==.8f&&f.records[2].volume==.6f,"Explicit stop restores all remembered independent native mixer originals");
    f.records[0].active=f.records[1].active=f.records[2].active=true;routes.update(f.records,f);routes.setGain("42:100",.5f,f);f.records[0].volume=.9f;routes.update(f.records,f);check(routes.applications()[0].state==AudioApplicationRouteState::failed,"External mixer mutation is detected rather than overwritten");
    check(routes.stopAll(f)==0&&f.records[0].volume==.9f&&f.records[1].volume==.8f,"Cleanup preserves external gain while restoring unchanged owned sessions");
    Fake bad;AudioSessionRoutes rollback;rollback.update(bad.records,bad);bad.failWrite=2;check(rollback.setGain("42:100",.25f,bad)==-77&&bad.records[0].volume==.4f&&bad.records[1].volume==.8f,"Partial native failure rolls attempted sessions back in reverse order");
    check(bad.calls==std::vector<std::string>{"get:a","get:b","set:a","get:a","set:b","set:b","get:b","set:a","get:a"},"Rollback ordering includes the possibly mutating failed native write");
    check(rollback.applications()[0].state==AudioApplicationRouteState::failed&&rollback.applications()[0].gain==.25f,"Failed transaction retains the source requested gain explicitly and has a cleanup path");
    check(rollback.stopAll(bad)==0,"Failed route can safely relinquish restored state");
    auto invalid=bad.records;invalid[1].id=invalid[0].id;bool rejected{};try{rollback.update(invalid,bad);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Duplicate session identities reject before retained metadata mutation");
    check(rollback.setGain("missing",.5f,bad)<0&&rollback.setGain("42:100",2,bad)<0,"Unknown process identity and invalid scalar do not reach native writes");
    invalid=bad.records;invalid[1].pid=43;rejected=false;try{rollback.update(invalid,bad);}catch(...){rejected=true;}check(rejected,"A process key cannot bind two PIDs");
    invalid=bad.records;invalid[0].processKey="99:200";invalid[0].pid=99;rejected=false;try{rollback.update(invalid,bad);}catch(...){rejected=true;}check(rejected&&rollback.applications()[0].id=="42:100","An existing leased session cannot be reassigned to another process before mutation");
}
void pausedPlayer(){Fake f;for(auto&r:f.records)r.active=false;AudioSessionRoutes routes;routes.update(f.records,f);
    check(routes.applications().size()==1&&routes.applications()[0].available&&routes.applications()[0].state==AudioApplicationRouteState::direct,"A paused player (inactive, unexpired sessions) stays listed and adjustable, like the source's remembered output process");
    check(!routes.applications()[0].rendering,"A paused player's route reports no rendering session (Now Playing's window-title play state)");
    f.records[1].active=true;routes.update(f.records,f);check(routes.applications()[0].rendering,"One active session of the process reports the route rendering");
    f.records[1].active=false;routes.update(f.records,f);check(!routes.applications()[0].rendering,"Pausing again clears it");
    check(routes.setGain("42:100",.5f,f)==0&&f.records[0].volume==.2f&&f.records[1].volume==.4f,"Paused-player volume applies relatively to every session immediately");
    f.records[1].controllable=false;f.records[1].volume.reset();routes.update(f.records,f);
    check(routes.applications()[0].state==AudioApplicationRouteState::failed,"A session that becomes uncontrollable fails the owned route instead of guessing");
    check(routes.stop("42:100",f)<0&&f.records[0].volume==.4f,"Cleanup restores every readable session and reports the unreadable one");
    AudioSessionRoutes fresh;Fake g;for(auto&r:g.records)r.active=false;g.records[1].controllable=false;g.records[1].volume.reset();fresh.update(g.records,g);
    check(fresh.applications().empty(),"A process with any uncontrollable session is never offered (no partial control)");
}
void exitedProcess(){Fake f;AudioSessionRoutes routes;routes.update(f.records,f);check(routes.setGain("42:100",.5f,f)==0&&f.records[0].volume==.2f,"Owned attenuation before the player quits");
    for(auto&r:f.records){r.exited=true;r.active=false;}routes.update(f.records,f);
    check(f.records[0].volume==.4f&&f.records[1].volume==.8f,"A terminated owned process is restored on its still-held sessions (Windows persists the level; the source tap ended)");
    check(routes.applications().empty(),"A terminated process is never listed");
    f.records.clear();routes.update(f.records,f);check(routes.applications().empty(),"Released sessions leave nothing owned");
    Fake g;AudioSessionRoutes failing;failing.update(g.records,g);failing.setGain("42:100",.5f,g);g.failWrite=1;g.writes=0;for(auto&r:g.records)r.exited=true;failing.update(g.records,g);
    check(failing.applications().size()==1&&failing.applications()[0].state==AudioApplicationRouteState::failed&&!failing.applications()[0].available,"A failed restore of a terminated process is reported, never offered for new attenuation");
    check(failing.setGain("42:100",.3f,g)<0,"A terminated process cannot take a new gain");
    g.records.clear();failing.update(g.records,g);check(failing.applications().empty(),"Its route ends when the backend releases the sessions");
    Fake h;h.records[1].exited=true;AudioSessionRoutes fresh;fresh.update(h.records,h);check(fresh.applications().empty(),"A process with a terminated session is not offered");
}
// Source 74a7935: a route stays while its app is momentarily silent. On
// Windows an app that closes its stream leaves its session Expired (the
// backend releases it); the route stays listed at its gain while the exact
// process runs, and the app's next session, which Windows opens at the level
// written here, joins from the remembered baseline: attenuated once.
bool sessionWrites(const Fake&f){return std::any_of(f.calls.begin(),f.calls.end(),[](const auto&c){return c.rfind("set:",0)==0;});}
void silentProcess(){
    {
        Fake f;for(auto&r:f.records)r.persistentID="{persist-"+r.id+"}";AudioSessionRoutes routes;routes.update(f.records,f);
        check(routes.setGain("42:100",.5f,f)==0&&f.records[0].volume==.2f&&f.records[1].volume==.4f,"Owned attenuation before the app goes silent");
        f.running.insert("42:100");const auto expired=f.records;f.records.clear();f.calls.clear();routes.update(f.records,f);
        const auto&kept=routes.applications();
        check(kept.size()==1&&kept[0].id=="42:100"&&kept[0].name==L"Synthetic app"&&kept[0].state==AudioApplicationRouteState::active&&kept[0].gain==.5f&&!kept[0].rendering,"Every session expired, the process runs: the route stays listed at its gain");
        check(f.calls==std::vector<std::string>{"running:42:100"},"Only the process is asked about; nothing is read or written");
        auto again=expired[0];again.id="a2";again.active=true;f.records={again};f.calls.clear();routes.update(f.records,f);
        check(f.records[0].volume==.2f&&!sessionWrites(f),"The next session, opened at the level written here, joins as it is: attenuated once, not .2 x .5");
        check(routes.applications().size()==1&&routes.applications()[0].available&&routes.applications()[0].rendering&&routes.applications()[0].gain==.5f,"The row is the live app again");
        check(routes.setGain("42:100",.25f,f)==0&&f.records[0].volume==.1f,"A new gain applies from the original baseline");
        check(routes.stop("42:100",f)==0&&f.records[0].volume==.4f&&routes.applications()[0].state==AudioApplicationRouteState::direct,"Stop restores the original level of that identity");
    }
    {
        Fake f;for(auto&r:f.records)r.persistentID="{persist-"+r.id+"}";AudioSessionRoutes routes;routes.update(f.records,f);routes.setGain("42:100",.5f,f);
        f.running.insert("42:100");const auto expired=f.records;f.records.clear();routes.update(f.records,f);
        check(routes.setGain("42:100",.25f,f)==0&&routes.applications()[0].gain==.25f,"A silent route takes a new gain with nothing to write yet");
        auto kept=expired[0];kept.id="a2";auto changed=expired[1];changed.id="b2";changed.volume=.7f;auto unknown=expired[1];unknown.id="c2";unknown.persistentID="{other}";unknown.volume=.9f;
        f.records={kept,changed,unknown};routes.update(f.records,f);
        check(f.records[0].volume==.1f,"A session at the kept level joins from its baseline: .4 x .25");
        check(std::abs(*f.records[1].volume-.175f)<1e-6f,"One whose level changed meanwhile (a recovery or the user) starts from that level");
        check(std::abs(*f.records[2].volume-.225f)<1e-6f,"Another identity starts from its own level");
        f.records.clear();routes.update(f.records,f);f.running.clear();f.records.clear();routes.update(f.records,f);
        check(routes.applications().empty(),"The route ends with its process");
    }
    {
        Fake f;AudioSessionRoutes routes;routes.update(f.records,f);routes.setGain("42:100",.5f,f);f.records.clear();f.calls.clear();routes.update(f.records,f);
        check(routes.applications().empty(),"A process that ended with its sessions leaves nothing owned");
        Fake g;AudioSessionRoutes failed;failed.update(g.records,g);failed.setGain("42:100",.5f,g);g.records[0].volume=.9f;failed.update(g.records,g);
        g.running.insert("42:100");g.records.clear();g.calls.clear();failed.update(g.records,g);
        check(failed.applications().empty()&&g.calls.empty(),"A failed route is never kept for a silent app");
    }
}
// The native backend's watch of a process whose last session expired, with
// a synthetic value standing in for its handle and exit wake: it lasts while
// the routes ask after each read, is released (unwatched) a read after they
// stop, and the table stays bounded.
void idleProcesses(){
    struct Watch{int id{};bool*released{};};
    AudioIdleProcesses<Watch>idle(2);bool firstReleased{},secondReleased{},thirdReleased{};
    const auto release=[](Watch&w){*w.released=true;};
    check(idle.keep("42:100",{1,&firstReleased})&&idle.contains("42:100"),"A process kept when its last session expires");
    check(!idle.keep("42:100",{9,&thirdReleased})&&idle.ask("42:100")->id==1,"The first watch of a process is kept");
    idle.round(release);check(idle.contains("42:100")&&!firstReleased,"Kept through the read after it expired, even before any ask");
    check(idle.ask("42:100")&&idle.ask("42:100")->id==1,"The routes ask after that read");
    idle.round(release);check(idle.contains("42:100")&&!firstReleased,"Asked since the previous read: kept");
    idle.round(release);check(!idle.contains("42:100")&&firstReleased,"Not asked since the previous read: unwatched and released");
    check(!idle.ask("42:100"),"A released process is not running for the routes");
    check(idle.keep("43:100",{2,&secondReleased})&&idle.keep("44:100",{3,&thirdReleased})&&!idle.keep("45:100",{4,&firstReleased})&&idle.size()==2,"The table is bounded");
    int seen{};idle.each([&](Watch&w){seen+=w.id;});check(seen==5,"Pause and resume reach every kept watch");
    idle.round(release);(void)idle.ask("44:100");idle.round(release);
    check(!idle.contains("43:100")&&secondReleased&&idle.contains("44:100")&&!thirdReleased,"Only the process still asked about stays");
    idle.clear();check(!idle.size(),"Close releases every watch");
}
void appearanceMetadata(){Fake f;AudioSessionRoutes routes;AudioApplicationExecutable e;e.path="C:\\explicit-synthetic\\app.exe";e.identity.objectID[0]=1;e.identity.volumeSerial=9;f.records[0].executable=e;routes.update(f.records,f);
    check(routes.applications()[0].executable==e&&f.calls.empty(),"Captured regular-file appearance metadata flows through existing process grouping without audio writes");
    f.records[1].executable=e;routes.update(f.records,f);check(routes.applications()[0].executable==e,"Matching process sessions share one exact icon source");
    f.records[1].executable->identity.objectID[0]=2;routes.update(f.records,f);check(!routes.applications()[0].executable&&routes.applications()[0].available,"Conflicting process metadata removes icon without disabling valid audio control");
    f.records[1].executable=e;routes.update(f.records,f);const auto previous=routes.applications();auto broken=f.records;broken.back().executable->path="relative.exe";bool rejected{};try{routes.update(broken,f);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&routes.applications()==previous,"Malformed late executable metadata rejects before route mutation");
    broken=f.records;broken[0].processKey.clear();broken[0].pid=0;rejected=false;try{routes.update(broken,f);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Unowned/system session cannot attach guessed executable artwork");
}
// Source ff06692 (a helper is named after the app that contains it) and
// 0585a0e (WebKit's audio service plays for its app), on Windows ancestry.
void helperOwner(){
    using I=AudioProcessImage;const std::string system="C:\\Windows";
    const I explorer{"C:\\Windows\\explorer.exe",10,1};
    // The live Chrome probe: the audio-service utility is chrome.exe again,
    // started by the main chrome.exe, started by Explorer.
    const I chrome{"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",100,2},audio{chrome.path,200,3};
    check(audioHelperOwner(audio,std::vector{chrome,explorer},system)==0,"Chrome's audio utility belongs to the main chrome.exe; Explorer never owns an app");
    check(!audioHelperOwner(chrome,std::vector{explorer},system),"A process launched by Explorer is its own app");
    // The live Hypergryph probe: QtWebEngineProcess.exe under Games.exe.
    const I games{"D:\\Hypergryph Launcher\\Games.exe",100,4},web{"D:\\Hypergryph Launcher\\web\\QtWebEngineProcess.exe",150,5};
    check(audioHelperOwner(web,std::vector{games,explorer},system)==0,"A helper in a subfolder of its launcher's folder belongs to the launcher");
    const I steam{"C:\\Steam\\steam.exe",50,6},cefHost{"C:\\Steam\\bin\\cef\\steamwebhelper.exe",60,7},cef{"C:\\Steam\\bin\\cef\\steamwebhelper.exe",70,8};
    check(audioHelperOwner(cef,std::vector{cefHost,steam,explorer},system)==1,"The outermost in-folder ancestor is the app");
    // A launcher's own folder can hold whole apps (Steam's default library,
    // a launcher's games folder). An app in its own right (an app window or
    // an AppUserModelID) keeps its own name; its helpers belong to it, never
    // to the launcher above it.
    const I steamApp{steam.path,50,6,true},game{"C:\\Steam\\steamapps\\common\\Some Game\\game.exe",80,20,true},gameAudio{"C:\\Steam\\steamapps\\common\\Some Game\\bin\\audio.exe",90,21};
    check(!audioHelperOwner(game,std::vector{steamApp,explorer},system)&&!audioHelperOwner(game,std::vector{steam,explorer},system),"A game in the launcher's library keeps its own name");
    check(audioHelperOwner(gameAudio,std::vector{game,steamApp,explorer},system)==0,"A game's helper belongs to the game, not the launcher above it");
    const I gamesApp{games.path,100,4,true},launched{"D:\\Hypergryph Launcher\\games\\Endfield\\Endfield.exe",160,22,true};
    check(!audioHelperOwner(launched,std::vector{gamesApp,explorer},system),"A game the launcher starts from its games folder keeps its own name");
    check(audioHelperOwner(web,std::vector{gamesApp,explorer},system)==0,"The launcher's windowless web helper still belongs to the launcher");
    check(!audioHelperOwner(I{web.path,150,5,true},std::vector{gamesApp,explorer},system),"A helper with an app window of its own is its own app");
    check(audioHelperOwner(cef,std::vector{I{cefHost.path,60,7,true},steamApp,explorer},system)==0,"The walk stops at the first ancestor that is an app in its own right");
    check(!audioHelperOwner(web,std::vector{I{"D:\\Other\\Games.exe",100,4}},system),"A launcher in another folder does not own the process");
    check(!audioHelperOwner(web,std::vector{I{games.path,151,4}},system)&&!audioHelperOwner(web,std::vector{I{games.path,150,4}},system),"A parent created after (or with) its child is a reused PID and owns nothing");
    check(audioHelperOwner(cef,std::vector{cefHost,I{steam.path,65,6}},system)==0,"The walk stops at a younger ancestor");
    check(!audioHelperOwner(I{"C:\\Tools\\bin\\ffplay.exe",90,9},std::vector{I{"C:\\Windows\\System32\\cmd.exe",80,8},I{"C:\\Tools\\host.exe",70,7}},system),"A command-line child is never renamed after its launcher");
    check(!audioHelperOwner(I{"C:\\Windows\\System32\\rundll32.exe",90,9},std::vector{explorer},system),"Explorer never owns a Windows tool");
    // 0585a0e analogue: the shared WebView2 runtime plays for its host.
    const I host{"C:\\Program Files\\Contoso\\Contoso.exe",100,11},view{"C:\\Program Files (x86)\\Microsoft\\EdgeWebView\\Application\\130.0.1\\msedgewebview2.exe",110,12},viewAudio{view.path,120,13};
    check(audioWebViewRuntime(view.path)&&audioWebViewRuntime("msedgewebview2.EXE")&&!audioWebViewRuntime("C:\\x\\notmsedgewebview2.exe")&&!audioWebViewRuntime(chrome.path),"The WebView2 runtime is recognised by its executable");
    check(audioHelperOwner(viewAudio,std::vector{view,host,explorer},system)==1,"A WebView2 audio process plays for its host app");
    const I launcher{"C:\\Program Files\\Contoso\\Launcher.exe",90,14};
    check(audioHelperOwner(viewAudio,std::vector{view,host,launcher,explorer},system)==2,"The host is then named after its own app");
    check(audioHelperOwner(viewAudio,std::vector{view,I{host.path,100,11,true},launcher,explorer},system)==1,"A host that is an app in its own right is the app");
    check(audioHelperOwner(I{viewAudio.path,120,13,true},std::vector{view,host,explorer},system)==1,"The runtime is never an app in its own right");
    check(!audioHelperOwner(viewAudio,std::vector{view,I{"C:\\Windows\\SystemApps\\Shell\\Widgets.exe",100,15}},system),"A WebView2 host inside the Windows folder is not an app");
    check(!audioHelperOwner(viewAudio,std::vector{view},system)&&!audioHelperOwner(viewAudio,std::vector{I{view.path,130,12}},system),"A runtime with no older host keeps its own name");
    check(!audioHelperOwner(I{"",5,1},std::vector{chrome},system)&&!audioHelperOwner(I{chrome.path,0,1},std::vector{chrome},system),"An unknown path or creation time owns nothing");
    std::vector<I>deep(40,I{"C:\\Deep\\a.exe",0,1});for(std::size_t n=0;n<deep.size();++n)deep[n].created=1000-n;
    check(audioHelperOwner(I{"C:\\Deep\\b.exe",2000,2},deep,system)==audioHelperAncestorLimit-1,"The ancestry read is bounded");
}
// The folder and file-name rules shared with Activity (process_identity).
void folderRule(){
    check(pathInsideFolder("C:\\Windows\\System32\\cmd.exe","c:\\windows")&&pathInsideFolder("C:\\Windows\\x.exe","C:\\Windows\\")&&pathInsideFolder("C:\\Windows/x.exe","C:\\Windows"),"A path inside a folder, caselessly, either separator after it");
    check(!pathInsideFolder("C:\\WindowsApps\\x.exe","C:\\Windows")&&!pathInsideFolder("C:\\Windows","C:\\Windows")&&!pathInsideFolder("C:\\Windows\\","C:\\Windows")&&!pathInsideFolder("C:\\x.exe",""),"A sibling, the folder itself or an empty folder is not inside");
    check(executableInsideFolderOf("C:\\Steam\\bin\\x.exe","c:\\steam\\steam.exe")&&!executableInsideFolderOf("C:\\x.exe","C:\\steam.exe")&&!executableInsideFolderOf("C:\\Steam","C:\\Steam\\steam.exe"),"The executable folder rule, below a drive's root");
    check(executableNamed("C:\\x\\MsEdgeWebView2.exe","msedgewebview2.exe")&&executableNamed("msedgewebview2.exe","msedgewebview2.exe")&&!executableNamed("C:\\x\\notmsedgewebview2.exe","msedgewebview2.exe")&&!executableNamed("C:\\x\\a.exe",""),"A file name compares caselessly and whole");
    static_assert(processIsApplication(true,"")&&processIsApplication(false,"Contoso_x!App")&&!processIsApplication(false,""));
}
// The containing app and package family travel with the process's sessions.
void helperRoutes(){Fake f;AudioSessionRoutes routes;AudioApplicationExecutable app{"C:\\explicit-synthetic\\Launcher.exe",{}};app.identity.objectID[0]=7;
    for(auto&r:f.records){r.application=app;r.packageFamily="Contoso.Player_abc";}
    routes.update(f.records,f);check(routes.applications()[0].application==app&&routes.applications()[0].packageFamily=="Contoso.Player_abc"&&f.calls.empty(),"A helper's app and package family reach its route");
    f.records[1].application->identity.objectID[0]=8;f.records[1].packageFamily="Other.App_x";routes.update(f.records,f);
    check(!routes.applications()[0].application&&routes.applications()[0].packageFamily.empty()&&routes.applications()[0].available,"Sessions that disagree leave no app or family, and audio control stays");
    auto broken=f.records;broken[0].application->path="relative.exe";bool rejected{};try{routes.update(broken,f);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Malformed app metadata is rejected");
    broken=f.records;broken[0].packageFamily=std::string(257,'a');rejected=false;try{routes.update(broken,f);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"An oversized package family is rejected");
    broken=f.records;broken[0].processKey.clear();broken[0].pid=0;broken[0].application.reset();broken[0].executable.reset();rejected=false;try{routes.update(broken,f);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"An unowned session cannot carry a package family");
}
#ifdef _WIN32
// Hidden native check: a copy of this test binary, started without a window,
// reads this process as its parent at the same path, created earlier.
int ancestorChild(){Sleep(30000);return 0;}
void ancestorReader(){
    wchar_t self[32768]{};const DWORD length=GetModuleFileNameW(nullptr,self,32768);check(length>0&&length<32768,"Read the test binary's path");
    std::wstring command=L"\""+std::wstring(self,length)+L"\" --ancestor-child";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=0,"Start a hidden copy of the test binary");
    struct Reap{PROCESS_INFORMATION&p;~Reap(){TerminateProcess(p.hProcess,0);WaitForSingleObject(p.hProcess,5000);CloseHandle(p.hThread);CloseHandle(p.hProcess);}}reap{child};
    std::vector<WindowsProcessEntry>all;check(readWindowsProcesses(all),"One process snapshot");std::map<std::uint32_t,std::uint32_t>parents;for(const auto&e:all)if(e.pid&&e.pid!=e.parent)parents.emplace(e.pid,e.parent);
    std::set<std::uint32_t>windows;check(readWindowsApplicationWindows(windows),"The app windows read in session 0");windows.erase(GetCurrentProcessId());
    const auto ancestors=readWindowsAudioAncestors(child.dwProcessId,parents,windows);
    check(!ancestors.empty()&&ancestors[0].pid==GetCurrentProcessId(),"The child's nearest ancestor is this process");
    const auto created=readWindowsProcessCreation(child.hProcess);const auto path=readWindowsProcessPath(child.hProcess);check(created&&path,"Read the child's creation time and path");
    const auto mine=readWindowsProcessCreation(GetCurrentProcess());check(mine&&ancestors[0].created==*mine&&*mine<=*created,"The ancestor carries this process's creation time");
    std::string utf8(ancestors[0].path);check(audioHelperOwner({utf8,*created,child.dwProcessId},ancestors,"C:\\Windows")==0,"The same executable's child belongs to this process");
    check(!ancestors[0].application,"A windowless unpackaged ancestor is not an app in its own right");
    check(!audioHelperOwner({utf8,*created,child.dwProcessId,true},ancestors,"C:\\Windows"),"A child that is an app in its own right keeps its own name");
    windows.insert(GetCurrentProcessId());const auto windowed=readWindowsAudioAncestors(child.dwProcessId,parents,windows);
    check(!windowed.empty()&&windowed[0].application&&audioHelperOwner({utf8,*created,child.dwProcessId},windowed,"C:\\Windows")==0,"An ancestor owning an app window is an app and owns its helper");
    check(readWindowsAudioAncestors(0,parents,windows).empty()&&readWindowsAudioAncestors(0xfffffff0u,parents,windows).empty(),"An unknown PID has no ancestry");
}
#endif
struct WorkerState{std::mutex mutex;std::vector<AudioSessionRecord>records{{"a","42:100",L"Fixture",42,true,true,.8f,0}};AudioSessionBackend::Wake wake;std::thread::id backendThread;unsigned reads{},opens{},closes{},pauses{},writes{};bool violation{};};
class ThreadFake final:public AudioSessionBackend {
    std::shared_ptr<WorkerState>s;
    void thread(){if(s->backendThread!=std::this_thread::get_id())s->violation=true;}
public:
    explicit ThreadFake(std::shared_ptr<WorkerState>state):s(std::move(state)){std::lock_guard lock(s->mutex);s->backendThread=std::this_thread::get_id();}
    ~ThreadFake(){std::lock_guard lock(s->mutex);thread();}
    std::int32_t open(std::wstring_view,Wake wake)override{std::lock_guard lock(s->mutex);thread();s->wake=std::move(wake);++s->opens;return 0;}
    std::int32_t resume(Wake wake)override{std::lock_guard lock(s->mutex);thread();s->wake=std::move(wake);return 0;}
    void pause()noexcept override{std::lock_guard lock(s->mutex);thread();s->wake={};++s->pauses;}
    std::int32_t read(std::vector<AudioSessionRecord>&out)override{std::lock_guard lock(s->mutex);thread();out=s->records;++s->reads;return 0;}
    std::int32_t readVolume(std::string_view id,float&out)override{std::lock_guard lock(s->mutex);thread();for(auto&r:s->records)if(r.id==id){out=*r.volume;return 0;}return -1;}
    std::int32_t writeVolume(std::string_view id,float value)override{std::lock_guard lock(s->mutex);thread();for(auto&r:s->records)if(r.id==id){r.volume=value;++s->writes;return 0;}return -1;}
    bool processRunning(std::string_view)noexcept override{return false;}
    void close()noexcept override{std::lock_guard lock(s->mutex);thread();s->wake={};++s->closes;}
};
void lifecycle(){auto state=std::make_shared<WorkerState>();std::mutex noticeMutex;std::condition_variable noticed;unsigned notices{};AudioSessionWorker worker([state]{return std::make_unique<ThreadFake>(state);},[&]{std::lock_guard lock(noticeMutex);++notices;noticed.notify_one();});AudioSessionSnapshot snapshot;
    const auto wait=[&](const auto&predicate){std::unique_lock lock(noticeMutex);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);return noticed.wait_until(lock,deadline,[&]{worker.takeSnapshot(snapshot);return predicate(snapshot);});};
    check(worker.activate(L"fixture endpoint")&&wait([](const auto&s){return s.supported&&!s.paused&&s.applications.size()==1;}),"One worker opens injected endpoint and posts first complete typed snapshot");
    {std::lock_guard lock(state->mutex);check(state->backendThread!=std::this_thread::get_id()&&state->opens==1,"Backend construction and all native work stay off the UI thread");}
    check(worker.setGain("42:100",.5f)&&wait([](const auto&s){return s.applications[0].gain==.5f;}),"Explicit app gain is processed by the same retained worker");
    AudioSessionBackend::Wake stale;unsigned reads{};{std::lock_guard lock(state->mutex);stale=state->wake;reads=state->reads;}
    check(worker.pause()&&wait([](const auto&s){return s.paused;}),"Inactive HUD unregisters audio callbacks without resetting user's live attenuation");
    stale();{std::lock_guard lock(state->mutex);check(state->records[0].volume==.4f,"Pause preserves opted-in gain without a hidden mixer write");}
    check(worker.activate(L"fixture endpoint")&&wait([](const auto&s){return !s.paused&&s.applications[0].gain==.5f;}),"Resume performs a fresh event-driven read and preserves explicit route ownership");
    {std::lock_guard lock(state->mutex);check(state->reads>=reads+1,"Resume reads current native state rather than a stale cached scalar");}
    worker.stop();stale();{std::lock_guard lock(state->mutex);check(!state->violation&&state->closes>=1&&state->records[0].volume==.8f,"Terminal stop restores untouched owned original and releases backend on the worker");}
    check(!worker.activate(L"fixture endpoint")&&!worker.setGain("42:100",.5f),"Closed worker rejects stale queued UI commands");
}
}
int main(int argc,char**argv){
#ifdef _WIN32
    if(argc==2&&std::string(argv[1])=="--ancestor-child")return ancestorChild();
#else
    (void)argc;(void)argv;
#endif
    try{transactions();pausedPlayer();exitedProcess();silentProcess();idleProcesses();appearanceMetadata();helperOwner();folderRule();helperRoutes();
#ifdef _WIN32
        ancestorReader();
#endif
        lifecycle();std::cout<<"PASS "<<checks<<" synthetic audio-session worker checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
