// NowPlayingSession owner integration: source lyric precedence (embedded,
// NetEase catalog, LRCLIB), catalog cover supplement, visibility-bound
// requests, Event Log playbackAction and the playing-app volume bridge.
// Injected provider, transport and audio routes only: no GSMTC session,
// network request, audio session or user data is touched.
#include "tools/now_playing_session.hpp"
#include "modules/event_log.hpp"
#include <iostream>
#include <map>
#include <stdexcept>

namespace n=endfield::native;namespace m=endfield::modules;namespace t=endfield::tools;using Q=endfield::app::UtilityExecutor;
namespace {
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}
struct Provider:n::NowPlayingProvider{Changed changed;Ready startReady,commandReady;Read readReady;unsigned reads{},commands{};
    void start(Changed c,Ready r)override{changed=std::move(c);startReady=std::move(r);}void read(Read r)override{++reads;readReady=std::move(r);}
    void perform(std::uint64_t,std::uint64_t,m::NowPlayingCommand,Ready r)override{++commands;commandReady=std::move(r);}void stop()noexcept override{}};
struct Task final:n::NowPlayingWeb::Task{unsigned cancels{};void cancel()noexcept override{++cancels;}};
struct Transport{
    std::vector<n::NowPlayingWeb::Request>requests;std::vector<n::NowPlayingWeb::Done>done;std::vector<std::shared_ptr<Task>>tasks;
    n::NowPlayingWeb::Transport bind(){return [this](const n::NowPlayingWeb::Request&r,n::NowPlayingWeb::Done d)->std::shared_ptr<n::NowPlayingWeb::Task>{requests.push_back(r);done.push_back(std::move(d));tasks.push_back(std::make_shared<Task>());return tasks.back();};}
};
struct Bridge{
    std::vector<m::NowPlayingAudioRoute>routes;std::uint64_t revision{1};std::vector<std::pair<std::string,double>>gains;std::vector<std::string>stops;std::vector<bool>votes;
    std::map<std::string,std::string>identity;unsigned identityCalls{};bool refuse{};
    std::shared_ptr<const m::NowPlayingAppVolume>bind(){auto v=std::make_shared<m::NowPlayingAppVolume>();
        v->setActive=[this](bool a){votes.push_back(a);};v->revision=[this]{return revision;};v->routes=[this]{return routes;};
        v->processAppUserModelID=[this](const m::NowPlayingAudioRoute&r)->std::optional<std::string>{++identityCalls;const auto f=identity.find(r.id);if(f==identity.end())return std::nullopt;return f->second;};
        v->setGain=[this](const std::string&id,double g){if(refuse)return false;gains.emplace_back(id,g);return true;};v->stop=[this](const std::string&id){stops.push_back(id);return true;};return v;}
};
m::NowPlayingBody body(std::string_view text){return std::make_shared<const std::vector<std::uint8_t>>(text.begin(),text.end());}
std::shared_ptr<const std::vector<std::uint8_t>>pixels(std::uint8_t color){return std::make_shared<const std::vector<std::uint8_t>>(1,color);}
m::NowPlayingTrack song(std::optional<std::string>lyrics={}){m::NowPlayingTrack s;s.title="Synthetic Song";s.artist="Example Artist";s.album="Fixture Album";s.duration=233.4;s.position=10;s.sampledAt=10;s.timedLyrics=std::move(lyrics);return s;}
n::NowPlayingReadResult media(std::string application,m::NowPlayingTrack track=song(),std::shared_ptr<const std::vector<std::uint8_t>>art={},std::uint64_t token=1){
    n::NowPlayingReadResult r;r.value.sessions={{token,application,application}};r.value.session=r.value.sessions.front();r.value.track=std::move(track);r.value.artwork=std::move(art);
    r.value.capabilities={true,true,true,true,true,true};r.value.metadataRevision=3;return r;}
struct Fixture{
    Q q{[]{}};std::shared_ptr<Provider>p=std::make_shared<Provider>();double now=10;Transport transport;Bridge bridge;std::vector<m::NowPlayingPlaybackEvent>events;unsigned decodes{};
    std::shared_ptr<n::NowPlayingWeb>web;n::NativeNowPlayingService service{q,[this]{return p;},[]{},[this]{return now;}};std::unique_ptr<t::NowPlayingSession>owner;
    explicit Fixture(bool network=true,bool audio=false){
        t::NowPlayingSessionOptions o;
        o.decode=[this](std::span<const std::uint8_t>b){++decodes;auto f=std::make_shared<n::NotesImageFrame>();f->width=f->height=1;f->straightRGBA={b[0],0,0,255};return n::NowPlayingImageResult{f};};
        if(network){web=std::make_shared<n::NowPlayingWeb>(transport.bind(),[]{},[this]{return now;});o.web=web;}
        if(audio)o.volume=bridge.bind();o.playback=[this](const m::NowPlayingPlaybackEvent&e){events.push_back(e);};
        owner=std::make_unique<t::NowPlayingSession>(service,q,m::NowPlayingAppearance{},[]{},std::move(o));
    }
    void work(){q.waitIdle();q.drain();}
    void flush(){work();owner->utilityCompleted(now);work();owner->utilityCompleted(now);}
    void ready(n::NowPlayingReadResult r){owner->setActive(true,now);flush();p->startReady({});flush();p->readReady(std::move(r));flush();}
    void read(n::NowPlayingReadResult r){service.refresh();flush();p->readReady(std::move(r));flush();}
    void answer(std::size_t index,m::NowPlayingBody b){transport.done.at(index)(std::move(b));flush();}
    const m::NowPlayingViewInput&input()const{return owner->presentation().input();}
};
const std::string lrclib="https://lrclib.net/api/get?track_name=Synthetic%20Song&artist_name=Example%20Artist&album_name=Fixture%20Album&duration=233";
const std::string search="https://music.163.com/api/cloudsearch/pc?s=Synthetic%20Song%20Example%20Artist&type=1&limit=10&offset=0";
const std::string catalogSearch=R"({"code":200,"result":{"songs":[{"id":1901371647,"name":"Synthetic Song","ar":[{"name":"Example Artist"}],"al":{"name":"Fixture Album","picUrl":"http://p1.music.126.net/AbC==/1099.jpg"},"dt":233400}]}})";

void lrclibLyrics(){
    Fixture f;f.ready(media("Spotify.exe"));
    check(f.transport.requests.size()==1&&f.transport.requests[0].url==lrclib&&f.transport.requests[0].kind==n::NowPlayingWeb::Kind::lyrics,"Spotify track without timed lyrics asks LRCLIB once (no NetEase catalog)");
    check(!f.input().lyrics&&f.owner->nextWakeTime(f.now)==20,"Rows stay empty while the request is bounded by its 10 s deadline");
    f.answer(0,body(R"({"syncedLyrics":"[00:05] First\n[00:09] Second\n[00:20] Third"})"));
    check(f.input().lyrics&&f.input().lyrics->lines().size()==3&&f.owner->presentation().lyricRows()[1]=="Second","LRCLIB timed lyrics reach the source lyric rows");
    auto playing=media("Spotify.exe");playing.value.track->isPlaying=true;f.read(std::move(playing));
    check(f.transport.requests.size()==1,"Playback/timeline events never repeat a lyric request");
    f.read(media("Spotify.exe",song(),{}, 2));check(f.transport.requests.size()==1&&f.input().lyrics,"A new GSMTC session token for the same player and track reuses the cache");
}
void netease(){
    Fixture f;f.ready(media("cloudmusic.exe"));
    check(f.transport.requests.size()==1&&f.transport.requests[0].url==search,"NetEase player uses the public catalog search first");
    f.answer(0,body(catalogSearch));
    check(f.transport.requests.size()==3&&f.transport.requests[1].url=="https://music.163.com/api/song/lyric?id=1901371647&lv=-1&kv=-1&tv=-1","Matched song reads only its own lyric document");
    check(f.transport.requests[2].kind==n::NowPlayingWeb::Kind::artwork&&f.transport.requests[2].url=="https://p1.music.126.net/AbC==/1099.jpg?param=600y600","Missing GSMTC thumbnail is supplemented by the allow-listed 600x600 catalog cover");
    f.answer(2,pixels(7));check(f.owner->artwork()&&f.owner->artwork()->straightRGBA[0]==7&&f.input().coverAvailable,"Catalog cover is decoded through the existing artwork owner");
    f.answer(1,body(R"({"code":200,"lrc":{"lyric":"[00:01] Catalog\n[00:12] Line"},"tlyric":{"lyric":"[00:01] translated"}})"));
    check(f.input().lyrics&&f.input().lyrics->lines().front().text=="Catalog"&&f.transport.requests.size()==3,"Catalog lyrics win and LRCLIB is never asked");
    f.read(media("cloudmusic.exe"));check(f.transport.requests.size()==3&&f.decodes==1,"Routine refresh reuses catalog, lyrics and the decoded cover");
    f.owner->setActive(false,f.now);f.now=50;f.owner->setActive(true,f.now);check(f.owner->artwork()&&f.owner->artwork()->straightRGBA[0]==7,"Reopen shows the retained cover immediately");
    f.flush();f.p->startReady({});f.flush();f.p->readReady(media("cloudmusic.exe"));f.flush();
    check(f.transport.requests.size()==3&&f.owner->artwork()&&f.input().lyrics,"Reopen reuses bounded caches without another request");
}
void thumbnailFirst(){
    Fixture f;f.ready(media("cloudmusic.exe",song(),pixels(3)));f.answer(0,body(catalogSearch));
    check(f.transport.requests.size()==2&&f.owner->artwork()&&f.owner->artwork()->straightRGBA[0]==3,"A GSMTC thumbnail always wins; the catalog cover is never downloaded");
    Fixture late;late.ready(media("cloudmusic.exe"));late.answer(0,body(catalogSearch));
    check(late.transport.requests.size()==3&&late.transport.requests[2].kind==n::NowPlayingWeb::Kind::artwork,"Cover supplement starts while the player has no thumbnail");
    late.read(media("cloudmusic.exe",song(),pixels(9)));
    check(late.transport.tasks[2]->cancels==1&&late.owner->artwork()&&late.owner->artwork()->straightRGBA[0]==9,"A thumbnail that arrives later retires the in-flight supplement");
    late.answer(2,pixels(4));check(late.owner->artwork()->straightRGBA[0]==9,"The retired supplement can never replace the player's thumbnail");
}
void embedded(){
    Fixture f;f.ready(media("Spotify.exe",song("[00:01] Embedded\n[00:30] Later")));
    check(f.transport.requests.empty()&&f.input().lyrics&&f.input().lyrics->lines().front().text=="Embedded","Embedded timed lyrics take precedence without any request");
    Fixture n2;n2.ready(media("cloudmusic.exe",song("[00:02] Embedded")));n2.answer(0,body(catalogSearch));
    check(n2.input().lyrics&&n2.input().lyrics->lines().front().text=="Embedded"&&n2.transport.requests.size()==3,"NetEase embedded lyrics stay first while the catalog still supplies the cover");
}
void visibility(){
    Fixture f;f.ready(media("Spotify.exe"));check(f.transport.requests.size()==1,"Visible owner starts one request");
    f.owner->setActive(false,f.now);check(f.transport.tasks[0]->cancels==1&&f.web->stats().inFlight==0&&!f.owner->nextWakeTime(f.now),"Hide cancels in-flight downloads and leaves no deadline");
    f.answer(0,body(R"({"syncedLyrics":"[00:01] late"})"));check(f.transport.requests.size()==1,"Late completion after hide is discarded");
    f.owner->setActive(true,f.now);f.flush();f.p->startReady({});f.flush();f.p->readReady(media("Spotify.exe"));f.flush();
    check(f.transport.requests.size()==2&&f.transport.requests[1].url==lrclib,"Reopen restarts the interrupted lookup once");
    f.now=40;check(f.owner->deadline(f.now)&&f.transport.requests.size()==3&&f.transport.requests[2].url.rfind("https://lrclib.net/api/search?",0)==0,"Timeout at the host deadline falls back to the source search request");
    f.answer(2,body("[]"));check(!f.input().lyrics&&f.transport.requests.size()==3,"Ambiguous or empty results are a cached negative, never retried by events");
    f.read(media("Spotify.exe"));check(f.transport.requests.size()==3,"Routine reads keep the negative result");
    f.owner->refreshManually(f.now);f.flush();f.p->readReady(media("Spotify.exe"));f.flush();
    check(f.transport.requests.size()==4&&f.transport.requests[3].url==lrclib,"Only an explicit refresh retires the negative lyric result");
}
void offline(){
    Fixture f(false);f.ready(media("cloudmusic.exe"));check(f.transport.requests.empty()&&!f.input().lyrics,"Without an injected web owner no request or catalog exists (source fixture backend)");
}
void events(){
    Fixture f;f.ready(media("QQMusic.exe"));
    // Each command completion is followed by the service's own read; answer it
    // before the next command so every Ready belongs to the command under test.
    const auto finish=[&](n::NowPlayingCommandResult result){f.work();f.p->commandReady(result);f.flush();f.p->readReady(media("QQMusic.exe"));f.flush();};
    check(f.owner->dispatch({m::NowPlayingIntentKind::playPause,1},f.now),"Play/Pause accepted");finish({});
    check(f.events.size()==1&&f.events[0]==m::NowPlayingPlaybackEvent{"playPause",m::NowPlayingSource::qqMusic},"Successful command records action and mapped source only");
    check(f.owner->dispatch({m::NowPlayingIntentKind::next,1},f.now),"Next accepted");finish({n::NowPlayingServiceFailure::unavailable,0});
    check(f.events.size()==1,"Failed command records nothing");
    check(f.owner->dispatch({m::NowPlayingIntentKind::seek,1,30},f.now),"Seek accepted");finish({});
    check(f.events.size()==2&&f.events[1]==m::NowPlayingPlaybackEvent{"seek",m::NowPlayingSource::qqMusic},"Seek success records seek");
    // The shared Event Log accepts every recorded pair unchanged; nothing else is stored.
    for(const auto action:{"playPause","previous","next","seek"})for(const auto source:{m::NowPlayingSource::music,m::NowPlayingSource::spotify,m::NowPlayingSource::netease,m::NowPlayingSource::qqMusic,m::NowPlayingSource::kugou,m::NowPlayingSource::system}){
        const auto metadata=m::nowPlayingPlaybackMetadata({action,source});
        check(metadata.size()==2&&m::sanitizedEventMetadata(m::EventKind::playbackAction,metadata)==metadata,"playbackAction metadata survives Event Log sanitization unchanged");}
    check(m::nowPlayingPlaybackMetadata(f.events[0])==m::EventMetadata{{"action","playPause"},{"source","qqMusic"}},"Recorded playback metadata is action and source only");
    check(f.owner->dispatch({m::NowPlayingIntentKind::previous,1},f.now),"Previous accepted");f.owner->setActive(false,f.now);f.p->commandReady({});
    f.owner->setActive(true,f.now);f.flush();check(f.events.size()==2,"A command completing after hide is never recorded");
    Fixture g;g.ready(media("Chrome"));g.owner->dispatch({m::NowPlayingIntentKind::previous,1},g.now);g.work();g.p->commandReady({});g.flush();
    check(g.events.size()==1&&g.events[0]==m::NowPlayingPlaybackEvent{"previous",m::NowPlayingSource::system},"Unknown players are recorded as system");
}
// Downloaded JSON is decoded on the shared utility worker; the owner call
// that drains the download only submits it. Hide discards queued decodes.
void offThread(){
    Fixture f;f.ready(media("Spotify.exe"));const auto accepted=f.q.stats().accepted;
    f.transport.done.at(0)(body(R"({"syncedLyrics":"[00:05] First\n[00:09] Second"})"));f.owner->utilityCompleted(f.now);
    check(f.q.stats().accepted==accepted+1&&!f.input().lyrics,"The downloaded answer is decoded on the shared utility worker, not inside the owner call");
    f.flush();check(f.input().lyrics&&f.input().lyrics->lines().size()==2&&f.owner->presentation().lyricRows()[1]=="Second","Decoded lyrics reach the rows after the utility drain");
    Fixture g;g.ready(media("Spotify.exe"));const auto discarded=g.q.stats().discarded;
    g.transport.done.at(0)(body(R"({"syncedLyrics":"[00:05] First"})"));g.owner->utilityCompleted(g.now);g.owner->setActive(false,g.now);g.work();
    check(g.q.stats().discarded==discarded+1,"Hide discards the queued decode");
    g.owner->setActive(true,g.now);g.flush();g.p->startReady({});g.flush();g.p->readReady(media("Spotify.exe"));g.flush();
    check(g.transport.requests.size()==2&&!g.input().lyrics,"A discarded answer was never cached; reopen asks once more");
    g.answer(1,body(R"({"syncedLyrics":"[00:05] Again"})"));check(g.input().lyrics&&g.input().lyrics->lines().front().text=="Again","The repeated lookup decodes normally");
}
void volume(){
    using S=m::NowPlayingAudioRouteState;
    Fixture f(false,true);f.bridge.routes={{"100:1",100,"C:\\Apps\\Spotify\\Spotify.exe",true,S::direct,{}},{"200:2",200,"C:\\Other\\game.exe",true,S::direct,{}}};
    f.ready(media("Spotify.exe"));
    check(f.bridge.votes==std::vector<bool>{true}&&f.owner->volumeRoute()=="100:1","Visible owner votes for per-app audio and resolves the single Spotify.exe route");
    check(f.input().volumeAvailable&&f.input().capabilities.volume&&f.input().volume==1.,"Direct route is adjustable and shows 100%");
    auto&view=f.owner->presentation();view.perform(m::NowPlayingAction::volume,f.now);
    const auto intent=view.setSlider(m::NowPlayingSliderKind::volume,.4,f.now);check(intent&&intent->kind==m::NowPlayingIntentKind::volume,"Open submenu emits a volume intent");
    check(f.owner->dispatch(*intent,f.now)&&f.bridge.gains.size()==1&&f.bridge.gains[0]==std::pair<std::string,double>("100:1",.4),"Volume writes the route gain through the injected bridge");
    check(f.input().volume==.4&&view.volumeText()=="40%","Accepted gain is presented immediately");
    f.bridge.routes[0].state=S::active;f.bridge.routes[0].gain=.4;++f.bridge.revision;check(f.owner->audioChanged(f.now)&&f.input().volume==.4,"Confirmed route gain replaces the optimistic value");
    check(!f.owner->audioChanged(f.now),"Unchanged audio revision does not republish");
    check(f.owner->dispatch({m::NowPlayingIntentKind::volume,1,1},f.now)&&f.bridge.gains.back().second==1,"Active route accepts 100%");
    f.bridge.routes[0].state=S::failed;++f.bridge.revision;f.owner->audioChanged(f.now);
    check(!f.owner->dispatch({m::NowPlayingIntentKind::volume,1,.5},f.now)&&f.owner->dispatch({m::NowPlayingIntentKind::volume,1,1},f.now)&&f.bridge.stops==std::vector<std::string>{"100:1"},"Failed route stops at 100% and refuses other gains");
    f.bridge.routes[0].state=S::direct;f.bridge.routes[0].gain.reset();++f.bridge.revision;f.owner->audioChanged(f.now);const auto writes=f.bridge.gains.size();
    check(f.owner->dispatch({m::NowPlayingIntentKind::volume,1,1},f.now)&&f.bridge.gains.size()==writes,"Direct route at 100% needs no write");
    f.bridge.routes.push_back({"300:3",300,"D:\\Spotify.exe",true,S::direct,{}});++f.bridge.revision;f.owner->audioChanged(f.now);
    check(f.owner->volumeRoute().empty()&&!f.input().volumeAvailable&&!f.input().volume&&!f.owner->dispatch({m::NowPlayingIntentKind::volume,1,.2},f.now),"Several matching processes are never guessed");
    f.owner->setActive(false,f.now);check(f.bridge.votes==std::vector<bool>({true,false}),"Hide withdraws the audio vote");
    Fixture packaged(false,true);packaged.bridge.routes={{"10:1",10,"C:\\Program Files\\WindowsApps\\SpotifyAB\\Spotify.exe",true,S::direct,{}},{"11:1",11,"C:\\x\\helper.exe",true,S::direct,{}}};
    packaged.bridge.identity={{"10:1","SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"}};
    packaged.ready(media("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"));const auto calls=packaged.bridge.identityCalls;
    check(packaged.owner->volumeRoute()=="10:1"&&calls==2,"Packaged AUMID resolves through the process identity once per route");
    packaged.read(media("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"));check(packaged.bridge.identityCalls==calls,"Process identities are cached per route while visible");
    // Routes whose identity the audio worker captured never reach the
    // owner-thread process lookup (no OpenProcess on the UI thread).
    Fixture worker(false,true);worker.bridge.routes={{"10:1",10,"C:\\Program Files\\WindowsApps\\SpotifyAB\\Spotify.exe",true,S::direct,{},"SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"},{"11:1",11,"C:\\x\\helper.exe",true,S::direct,{},""}};
    worker.bridge.identity={{"11:1","SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"}};worker.ready(media("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify"));
    check(worker.owner->volumeRoute()=="10:1"&&worker.bridge.identityCalls==0,"Captured process identities resolve the packaged player without a process query");
    Fixture none(false,true);none.bridge.routes={{"5:1",5,"C:\\x\\chrome.exe",true,S::direct,{}}};none.ready(media("Chrome"));
    check(none.owner->volumeRoute().empty()&&!none.input().volumeAvailable&&none.owner->presentation().actions()[4].enabled,"Unresolved player keeps the source unavailable submenu");
    Fixture refused(false,true);refused.bridge.routes={{"1:1",1,"Spotify.exe",true,S::direct,{}}};refused.bridge.refuse=true;refused.ready(media("Spotify.exe"));
    check(!refused.owner->dispatch({m::NowPlayingIntentKind::volume,1,.3},refused.now)&&refused.input().volume==1.,"A refused write leaves the actual value");
}
}
int main(){
    try{lrclibLyrics();netease();thumbnailFirst();embedded();visibility();offline();events();offThread();volume();
        std::cout<<"Now Playing sources session: "<<checks<<" checks passed (injected provider/transport/audio routes)\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Now Playing sources session failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
