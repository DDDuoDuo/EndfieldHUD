#include "native/now_playing_service.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace n=endfield::native;namespace m=endfield::modules;using Executor=endfield::app::UtilityExecutor;
namespace {
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
struct Provider:n::NowPlayingProvider {
    Changed changed;Ready started,command;Read reading;unsigned starts{},reads{},commands{},stops{};bool failRead{};std::function<void()>onStop;
    std::uint64_t target{},metadata{};m::NowPlayingCommand request;
    void start(Changed c,Ready ready)override{++starts;changed=std::move(c);started=std::move(ready);}
    void read(Read value)override{++reads;if(failRead)throw std::runtime_error("Synthetic read failure");reading=std::move(value);}
    void perform(std::uint64_t session,std::uint64_t revision,m::NowPlayingCommand value,Ready done)override{++commands;target=session;metadata=revision;request=value;command=std::move(done);}
    void stop()noexcept override{++stops;if(onStop)onStop();}
};
n::NowPlayingReadResult media(std::uint64_t id=1,std::string title="中文 노래"){
    n::NowPlayingReadResult result;auto&v=result.value;v.sessions={{1,"same.application","First"},{2,"same.application","Second"}};v.session=v.sessions.at(static_cast<std::size_t>(id-1));m::NowPlayingTrack track;track.title=std::move(title);track.duration=240;track.position=10;track.isPlaying=true;track.sampledAt=1;v.track=m::NowPlayingTrack::bounded(std::move(track));v.capabilities={true,true,true,true,true,true,false,false,false};v.metadataRevision=5;return result;
}
void flush(Executor&q,n::NativeNowPlayingService&s){q.waitIdle();q.drain();s.drain();q.waitIdle();q.drain();}
void lifecycle(){std::atomic<unsigned>wakes{};Executor q([]{});auto p=std::make_shared<Provider>();unsigned factories{};n::NativeNowPlayingService service(q,[&]{++factories;return p;},[&]{++wakes;},[]{return 0.;});
    check(factories==0&&!q.stats().started&&!service.snapshot().active,"Construction creates no media provider or worker work");service.refresh();check(factories==0,"Hidden refresh cannot create a subscription");service.setActive(true);flush(q,service);check(factories==1&&p->starts==1&&p->reads==0,"Activation starts exactly one asynchronous manager request");
    p->started({});flush(q,service);check(p->reads==1&&service.snapshot().busy,"Manager ready starts one event-driven metadata request");p->reading(media());flush(q,service);check(service.snapshot().fresh&&service.snapshot().media.track->title=="中文 노래"&&!service.snapshot().busy,"Only current activation metadata becomes fresh");
    for(unsigned k=0;k<1000;++k)service.drain();check(p->reads==1,"Idle owner drains do not poll a media provider");const auto before=wakes.load();for(unsigned k=0;k<1000;++k)p->changed();check(wakes==before+1,"A provider event burst posts one bounded owner wake");flush(q,service);check(p->reads==2,"One coalesced event batch requests one snapshot");
    const auto stale=p->reading;p->changed();stale(media(1,"Obsolete"));flush(q,service);check(p->reads==3&&service.snapshot().media.track->title=="中文 노래","A superseded metadata read cannot replace retained current media");p->reading(media(2,"Current"));flush(q,service);check(service.snapshot().media.session->token==2&&service.snapshot().media.sessions[0].appUserModelID==service.snapshot().media.sessions[1].appUserModelID,"Same-AUMID sessions retain distinct native identities");
    check(service.perform({m::NowPlayingCommandKind::next}),"Explicit supported command accepted");flush(q,service);check(p->commands==1&&p->target==2,"Explicit command targets the selected native session");service.perform({m::NowPlayingCommandKind::seek,20});service.perform({m::NowPlayingCommandKind::seek,30});p->command({});flush(q,service);check(p->commands==2&&p->request.kind==m::NowPlayingCommandKind::seek&&p->request.seconds==30,"Busy commands retain only the latest source request");p->command({});flush(q,service);check(p->reads==4,"Completed commands cause one metadata refresh instead of a poller");p->reading(media(2,"Current"));flush(q,service);
    service.setActive(false);const auto hiddenWakes=wakes.load(),reads=p->reads;check(p->stops==1&&!service.snapshot().active&&!service.snapshot().media.track,"Hide cancels provider and clears transient displayed media");p->changed();p->reading(media());p->command({});p->started({});flush(q,service);check(wakes==hiddenWakes&&p->reads==reads&&!service.snapshot().media.track,"Every obsolete callback is ignored after hide");check(!service.perform({m::NowPlayingCommandKind::playPause}),"Hidden media controls send no OS action");
}
void failureAndCapacity(){Executor q([]{},1);auto p=std::make_shared<Provider>();n::NativeNowPlayingService s(q,[&]{return p;},[]{},[]{return 0.;});const auto blocker=q.makeRoute();q.submit(blocker,[]{},[](std::exception_ptr){});q.waitIdle();s.setActive(true);check(p->starts==0&&!s.snapshot().busy,"Full shared queue retains activation without starting another worker");q.drain();s.drain();flush(q,s);check(p->starts==1,"Owner capacity drain retries activation once");p->started({n::NowPlayingServiceFailure::accessDenied,-5});flush(q,s);check(s.snapshot().failure==n::NowPlayingServiceFailure::accessDenied&&p->stops==1,"Manager failure is explicit and retires the subscription");for(unsigned k=0;k<20;++k)s.drain();check(p->starts==1,"Failed startup never retries itself in an idle loop");
    auto next=std::make_shared<Provider>();n::NativeNowPlayingService good(q,[&]{return next;},[]{},[]{return 0.;});good.setActive(true);flush(q,good);next->started({});flush(q,good);next->reading(media());flush(q,good);next->failRead=true;good.refresh();flush(q,good);check(good.snapshot().failure==n::NowPlayingServiceFailure::unavailable&&good.snapshot().media.track,"Synchronous provider failure retains pixels but never publishes empty success");good.setActive(false);
}
void teardown(){Executor q([]{});auto p=std::make_shared<Provider>();auto service=std::make_unique<n::NativeNowPlayingService>(q,[&]{return p;},[]{},[]{return 0.;});service->setActive(true);q.waitIdle();q.drain();const auto ready=p->started;service.reset();ready({});check(p->stops==1,"Manager completion after owner destruction cannot resurrect it");q.waitIdle();q.drain();
    std::unique_ptr<n::NativeNowPlayingService>duringFactory;duringFactory=std::make_unique<n::NativeNowPlayingService>(q,[&]{duringFactory.reset();return p;},[]{},[]{return 0.;});auto*raw=duringFactory.get();raw->setActive(true);check(!duringFactory&&p->stops==2,"Destruction inside the provider factory suppresses pending native startup");
    unsigned factories{};auto retired=std::make_shared<Provider>();auto duringStop=std::make_unique<n::NativeNowPlayingService>(q,[&]{++factories;return retired;},[]{},[]{return 0.;});raw=duringStop.get();duringStop->setActive(true);q.waitIdle();q.drain();retired->onStop=[raw]{raw->setActive(true);};duringStop.reset();retired->onStop={};check(factories==1&&retired->stops==1,"Terminal destruction rejects synchronous provider-stop reactivation");
}
void publicationAndCommands(){Executor q([]{});auto p=std::make_shared<Provider>();n::NativeNowPlayingService service(q,[&]{return p;},[]{},[]{return 0.;});service.setActive(true);flush(q,service);p->started({});flush(q,service);p->reading(media());flush(q,service);
    service.refresh();flush(q,service);auto duplicate=media();duplicate.value.sessions[1].token=1;p->reading(std::move(duplicate));flush(q,service);check(service.snapshot().failure==n::NowPlayingServiceFailure::unavailable&&service.snapshot().media.track->title=="中文 노래","Malformed session publication retains the complete previous snapshot");check(!service.perform({m::NowPlayingCommandKind::next}),"Failed metadata cannot authorize a transport command");
    service.refresh();flush(q,service);p->reading(media());flush(q,service);check(service.snapshot().failure==n::NowPlayingServiceFailure::none,"Explicit successful refresh recovers from a rejected snapshot");
    p->changed();flush(q,service);check(service.perform({m::NowPlayingCommandKind::seek,20}),"A seek may wait behind an in-flight read");p->reading(media(1,"Different track"));flush(q,service);check(p->commands==0&&service.snapshot().media.track->title=="Different track","Queued seek is discarded when the newly read track identity changes");
    p->changed();flush(q,service);check(service.perform({m::NowPlayingCommandKind::next}),"A transport request may wait for a busy read");p->reading(media(2,"Other session"));flush(q,service);check(p->commands==0,"Queued transport never transfers to another current native session");
    service.refresh();flush(q,service);auto unsupported=media(2);unsupported.value.capabilities={};p->reading(std::move(unsupported));flush(q,service);check(!service.perform({m::NowPlayingCommandKind::playPause})&&!service.perform({m::NowPlayingCommandKind::seek,20}),"Missing native capabilities remain unsupported instead of sending fallback controls");
}
void timeout(){Executor q([]{});auto p=std::make_shared<Provider>();double clock=10;n::NativeNowPlayingService service(q,[&]{return p;},[]{},[&]{return clock;});service.setActive(true);flush(q,service);check(service.nextWakeTime()==13,"Async activation exports one bounded host deadline");clock=12.999;service.drain();check(p->stops==0,"Deadline never becomes premature polling");const auto late=p->started;clock=13;service.drain();check(service.snapshot().failure==n::NowPlayingServiceFailure::timedOut&&!service.nextWakeTime()&&p->stops==1,"Host deadline cancels a hung provider without waiting");late({});flush(q,service);check(p->reads==0,"Timed-out manager cannot publish a late snapshot");service.refresh();flush(q,service);check(p->starts==2,"Only explicit refresh retries timed-out activation");}
void timeline(){n::NowPlayingTimeline t;t.start=10000000;t.end=110000000;t.position=30000000;t.minimumSeek=20000000;t.maximumSeek=100000000;t.rate=2;t.sampledAt=100;t.playing=true;check(t.elapsed(102)==6&&t.elapsed(200)==10,"Windows rate and start timestamp map to bounded relative progress");check(t.seekTicks(0)==20000000&&t.seekTicks(50)==100000000,"Native seek obeys actual advertised seek range");t.playing=false;check(t.elapsed(10000)==2,"Paused native timeline never advances locally");t.rate=NAN;t.playing=true;check(t.elapsed(10000)==2,"Unknown playback rate is not fabricated");t.minimumSeek=120000000;check(!t.seekTicks(5),"Inconsistent native seek range disables seeking");t.start=INT64_MIN;t.end=INT64_MAX;t.minimumSeek=INT64_MIN;t.maximumSeek=INT64_MAX;check(t.seekTicks(1e20)==INT64_MAX&&t.seekTicks(-1e20)==INT64_MIN,"Extreme native ticks clamp before signed conversion");check(!t.seekTicks(NAN),"Nonfinite user seek cannot enter tick arithmetic");}
}
// R9: an explicit AppUserModelID's player executable is resolved by its own
// utility job after the read, then republished (source d09780d's bundle match).
n::NowPlayingReadResult players(std::vector<m::NowPlayingSession>sessions){
    n::NowPlayingReadResult result=media();result.value.sessions=std::move(sessions);result.value.session=result.value.sessions.front();return result;
}
void launchTargets(){
    Executor q([]{});auto p=std::make_shared<Provider>();std::vector<std::string>asked;bool fail{};
    ehud::data::ShelfFileIdentity identity;identity.objectID[0]=7;identity.volumeSerial=0x5eed;
    const auto resolve=[&](const std::string&id)->std::optional<n::NowPlayingLaunchTarget>{asked.push_back(id);if(fail||id=="Invented.Player")return std::nullopt;return n::NowPlayingLaunchTarget{"C:\\EndfieldHUD-Synthetic\\"+id+".exe",identity};};
    n::NativeNowPlayingService service(q,[&]{return p;},[]{},[]{return 0.;},3,resolve);
    // A read, then each resolution job and its republication.
    const auto settle=[&](n::NativeNowPlayingService&s){for(int k=0;k<4;++k)flush(q,s);};
    service.setActive(true);flush(q,service);p->started({});flush(q,service);
    using O=m::NowPlayingSessionOrigin;
    const std::vector<m::NowPlayingSession>listed{{1,"Chrome","Chrome"},{2,"Spotify.exe","Spotify"},{3,"Contoso.Player_abc!App","Contoso"},{4,"C:\\Apps\\Player.exe","Player",O::playerWindow},{5,"MSEdge","Edge"},{6,"Window.Title","Window",O::playerWindow}};
    p->reading(players(listed));settle(service);
    const auto&first=service.snapshot();
    check(asked==std::vector<std::string>{"Chrome","MSEdge"},"Only explicit media-session identities are resolved, one job at a time, after the read");
    check(first.media.session->executablePath=="C:\\EndfieldHUD-Synthetic\\Chrome.exe"&&first.media.session->executableIdentity==identity&&first.media.sessions[0]==*first.media.session,"The selected session is republished with its launch target");
    check(first.media.sessions[4].executablePath=="C:\\EndfieldHUD-Synthetic\\MSEdge.exe"&&first.media.sessions[1].executablePath.empty()&&first.media.sessions[2].executablePath.empty()&&first.media.sessions[3].executablePath.empty()&&first.media.sessions[5].executablePath.empty(),"Win32, packaged and window-title identities are never resolved");
    p->changed();flush(q,service);p->reading(players(listed));settle(service);
    check(asked.size()==2&&service.snapshot().media.session->executablePath=="C:\\EndfieldHUD-Synthetic\\Chrome.exe","A later read reuses the resolved target without another lookup");
    p->changed();flush(q,service);p->reading(players({{7,"Invented.Player","Unknown"}}));settle(service);
    check(asked.size()==3&&service.snapshot().media.session->executablePath.empty()&&!service.snapshot().media.session->executableIdentity,"An identity with no Start shortcut resolves to nothing");
    p->changed();flush(q,service);p->reading(players({{7,"Invented.Player","Unknown"}}));settle(service);
    check(asked.size()==3,"A failure is kept until the next activation");
    service.setActive(false);service.setActive(true);flush(q,service);p->started({});flush(q,service);p->reading(players({{8,"Invented.Player","Unknown"},{9,"Chrome","Chrome"}}));settle(service);
    check(asked.size()==4&&asked.back()=="Invented.Player"&&service.snapshot().media.sessions[1].executablePath=="C:\\EndfieldHUD-Synthetic\\Chrome.exe","A new activation retries a failure and keeps the resolved targets");
    fail=true;for(int k=0;k<20;++k){p->changed();flush(q,service);p->reading(players({{static_cast<std::uint64_t>(10+k),"Player"+std::to_string(k),"Many"}}));settle(service);}
    check(asked.size()==24,"Each new explicit identity is asked once");
    fail=false;p->changed();flush(q,service);p->reading(players({{40,"Chrome","Chrome"}}));settle(service);
    check(asked.size()==25&&service.snapshot().media.session->executablePath=="C:\\EndfieldHUD-Synthetic\\Chrome.exe","The cache holds at most a bounded set of identities");
    // A provider may carry a target itself (replays); it is validated and kept.
    p->changed();flush(q,service);auto own=players({{41,"Firefox.Hash","Firefox"}});own.value.sessions[0].executablePath="C:\\Replay\\firefox.exe";own.value.session=own.value.sessions[0];p->reading(own);settle(service);
    check(asked.size()==25&&service.snapshot().media.session->executablePath=="C:\\Replay\\firefox.exe","A session that already names its executable is not resolved");
    p->changed();flush(q,service);auto bad=own;bad.value.sessions[0].executablePath.clear();bad.value.sessions[0].executableIdentity=identity;bad.value.session=bad.value.sessions[0];p->reading(bad);settle(service);
    check(service.snapshot().failure==n::NowPlayingServiceFailure::unavailable,"An identity without its path is rejected");
    n::NativeNowPlayingService plain(q,[&]{return p;},[]{},[]{return 0.;});plain.setActive(true);flush(q,plain);p->started({});flush(q,plain);p->reading(players(listed));settle(plain);
    check(asked.size()==25&&plain.snapshot().media.session->executablePath.empty(),"Without a resolver nothing is resolved");
}
#ifdef _WIN32
// Hidden native check (read-only, this session's Start menu): "MSEdge"
// resolves to an existing msedge.exe; an invented AppUserModelID and the
// identities that are never resolved give nothing.
void appsFolder(){
    const auto edge=n::nowPlayingAppsFolderExecutable("MSEdge");
    if(edge){const auto&p=edge->path;const std::string tail="\\msedge.exe";
        check(p.size()>tail.size()&&std::equal(tail.rbegin(),tail.rend(),p.rbegin(),[](char a,char b){return a==(b>='A'&&b<='Z'?static_cast<char>(b-'A'+'a'):b);})&&(edge->identity.objectID!=decltype(edge->identity.objectID){}),"MSEdge resolves to an existing msedge.exe with its file identity");
        std::cout<<"AppsFolder launch target: verified (MSEdge)\n";}
    else std::cout<<"AppsFolder launch target: unverified (MSEdge has no Start shortcut in this session)\n";
    check(!n::nowPlayingAppsFolderExecutable("EndfieldHUD.Invented.Player.7f3a")&&!n::nowPlayingAppsFolderExecutable("msedge.exe")&&!n::nowPlayingAppsFolderExecutable("Microsoft.WindowsCalculator_8wekyb3d8bbwe!App")&&!n::nowPlayingAppsFolderExecutable(""),"An invented, Win32 or packaged identity resolves to nothing");
}
#endif
int main(){try{lifecycle();failureAndCapacity();teardown();publicationAndCommands();timeout();timeline();launchTargets();
#ifdef _WIN32
    appsFolder();
#endif
std::cout<<"Now Playing service: "<<checks<<" checks passed (injected provider only)\n";return 0;}catch(const std::exception&e){std::cerr<<"Now Playing service failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
