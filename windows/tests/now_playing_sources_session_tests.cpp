// NowPlayingSession owner integration: source lyric precedence (embedded,
// NetEase catalog, LRCLIB), catalog cover supplement, visibility-bound
// requests, Event Log playbackAction and the playing-app volume bridge.
// Injected provider, transport and audio routes only: no GSMTC session,
// network request, audio session or user data is touched.
#include "tools/now_playing_session.hpp"
#include "modules/event_log.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace n=endfield::native;namespace m=endfield::modules;namespace t=endfield::tools;using Q=endfield::app::UtilityExecutor;
namespace {
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}
struct Provider:n::NowPlayingProvider{Changed changed;Ready startReady,commandReady;Read readReady;unsigned reads{},commands{},resyncs{};
    void resynchronize()noexcept override{++resyncs;}
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
    explicit Fixture(bool network=true,bool audio=false,unsigned coverPixels=600,m::NowPlayingBody playlist={}){
        t::NowPlayingSessionOptions o;o.coverPixels=coverPixels;
        o.decode=[this](std::span<const std::uint8_t>b){++decodes;auto f=std::make_shared<n::NotesImageFrame>();f->width=f->height=1;f->straightRGBA={b[0],0,0,255};return n::NowPlayingImageResult{f};};
        if(network){web=std::make_shared<n::NowPlayingWeb>(transport.bind(),[]{},[this]{return now;});o.web=web;}
        if(audio)o.volume=bridge.bind();o.playback=[this](const m::NowPlayingPlaybackEvent&e){events.push_back(e);};
        if(playlist)o.neteasePlayingList=[playlist]()->m::NowPlayingBody{return playlist;};
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
// A window-title player (NetEase without a media session) has no length in
// its title: the matched catalog recording supplies it, so the elapsed and
// total times and the progress show. Recently downloaded covers come back at
// once when the player returns to one of the last tracks.
n::NowPlayingReadResult windowPlayer(std::string title,double position){
    const std::string identity="C:\\EndfieldHUD-Synthetic\\CloudMusic\\cloudmusic.exe";
    n::NowPlayingReadResult r;r.value.sessions={{1,identity,identity,m::NowPlayingSessionOrigin::playerWindow}};r.value.session=r.value.sessions.front();
    m::NowPlayingTrack t;t.title=std::move(title);t.artist="Example Artist";t.position=position;t.sampledAt=10;t.isPlaying=true;t.supportsSeeking=false;r.value.track=t;
    r.value.capabilities.toggle=r.value.capabilities.previous=r.value.capabilities.next=true;r.value.metadataRevision=5;return r;}
void windowPlayerCatalog(){
    Fixture f;f.ready(windowPlayer("Synthetic Song",3));
    check(f.transport.requests.size()==1&&f.transport.requests[0].url==search&&!f.input().track->duration,"A NetEase window title asks the public catalog; no length yet");
    f.answer(0,body(catalogSearch));
    check(f.input().track&&f.input().track->duration==233.4&&f.input().track->position==3.&&f.owner->presentation().durationText()=="3:53",
          "The matched recording's catalog length becomes the window-title track's duration");
    check(f.transport.requests.size()==3&&f.transport.requests[2].kind==n::NowPlayingWeb::Kind::artwork,"Its catalog cover is requested");
    f.answer(2,pixels(5));const auto firstCover=f.owner->memory();
    check(f.owner->artwork()&&f.owner->artwork()->straightRGBA[0]==5&&firstCover.recentCovers==1&&firstCover.recentCoverBytes==1&&firstCover.coverBytes==0,
          "The downloaded cover is kept among the recent covers (no second copy)");
    f.answer(1,body(R"({"code":200,"lrc":{"lyric":"[00:01] Catalog"}})"));
    const std::string other=R"({"code":200,"result":{"songs":[{"id":77,"name":"Other Song","ar":[{"name":"Example Artist"}],"al":{"name":"B","picUrl":"https://p2.music.126.net/Other==/2.jpg"},"dt":100000}]}})";
    f.read(windowPlayer("Other Song",0));const auto searches=f.transport.requests.size();
    check(f.transport.requests.back().url.find("Other%20Song")!=std::string::npos,"The next track asks the catalog");
    f.answer(searches-1,body(other));
    check(f.input().track->duration==100.&&f.transport.requests.back().kind==n::NowPlayingWeb::Kind::artwork,"The next track's length and cover follow");
    f.answer(f.transport.requests.size()-1,pixels(6));f.answer(searches,body(R"({"code":200,"lrc":{"lyric":"[00:02] Other"}})"));
    check(f.owner->artwork()->straightRGBA[0]==6&&f.owner->memory().recentCovers==2,"Two recent covers are kept");
    const auto requests=f.transport.requests.size();const auto decodes=f.decodes;
    f.read(windowPlayer("Synthetic Song",0));
    check(f.transport.requests.size()==requests&&f.owner->artwork()&&f.owner->artwork()->straightRGBA[0]==5&&f.decodes==decodes,
          "Returning to a recent track shows its cover at once: no search, download or decode");
    check(f.input().track->duration==233.4,"Its catalog length is cached with the match");
}
// Windows requests the catalog cover at the size its slot shows (the source
// asks for 600x600 for its 2x Retina slot; Windows decodes at most 512 px).
void sizedCover(){
    check(m::nowPlayingCatalogCoverPixels(1)==400&&m::nowPlayingCatalogCoverPixels(.5)==200&&m::nowPlayingCatalogCoverPixels(.9)==300&&
          m::nowPlayingCatalogCoverPixels(1.5)==500&&m::nowPlayingCatalogCoverPixels(3)==500&&m::nowPlayingCatalogCoverPixels(0)==400&&
          m::nowPlayingCatalogCoverPixels(std::numeric_limits<double>::quiet_NaN())==400,"The 330-point slot picks the smallest standard size covering it, at most 500");
    check(m::nowPlayingCatalogCoverURL("http://p1.music.126.net/AbC==/1099.jpg",500)=="https://p1.music.126.net/AbC==/1099.jpg?param=500y500"&&
          m::nowPlayingCatalogCoverURL("https://p3.music.126.net/x.jpg?param=130y130",20)=="https://p3.music.126.net/x.jpg?param=64y64"&&
          !m::nowPlayingCatalogCoverURL("https://evil.example/x.jpg",500)&&m::nowPlayingArtworkURLAllowed(*m::nowPlayingCatalogCoverURL("http://p2.music.126.net/a.jpg",300)),
          "Sized cover URLs keep the source host rules and stay allow-listed");
    Fixture f(true,false,500);f.ready(media("cloudmusic.exe"));f.answer(0,body(catalogSearch));
    check(f.transport.requests.size()==3&&f.transport.requests[2].url=="https://p1.music.126.net/AbC==/1099.jpg?param=500y500","The download asks for the slot's size");
    f.owner->setCoverPixels(300);f.read(windowPlayer("Synthetic Song",0));
    check(f.transport.requests.size()==4&&f.transport.requests[3].url==search,"Another player's track is looked up again");
    f.answer(3,body(catalogSearch));
    check(f.transport.requests.back().kind==n::NowPlayingWeb::Kind::artwork&&f.transport.requests.back().url=="https://p1.music.126.net/AbC==/1099.jpg?param=300y300",
          "A new display density applies to the next cover download");
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
// NetEase Cloud Music 3.1 read through its own player bar (live: no elapsed
// or total labels): the provider states the played fraction, the playing
// time since and the cover the bar shows. The owner starts that cover at
// once (no wait for the catalog search), converts the fraction with the
// catalog length, keeps the running time through the bar's stepwise drawing
// and asks for a new reading once per displayed second while the time runs.
n::NowPlayingReadResult barPlayer(double fraction,double sampledAt,bool playing=true,std::optional<std::string>cover={},double played=0){
    auto r=windowPlayer("Synthetic Song",0);r.value.track->position.reset();r.value.track->sampledAt=sampledAt;r.value.track->isPlaying=playing;
    r.value.progress=n::NowPlayingMediaSnapshot::Progress{fraction,played};r.value.artworkURL=std::move(cover);r.value.playerControls=true;return r;}
void playerBar(){
    const std::string own="https://p3.music.126.net/Own==/1.jpg?param=600y600";
    Fixture f(true,false,500);f.ready(barPlayer(.5,10,true,own));
    check(f.transport.requests.size()==2&&f.transport.requests[0].url==search&&f.transport.requests[1].kind==n::NowPlayingWeb::Kind::artwork&&
          f.transport.requests[1].url=="https://p3.music.126.net/Own==/1.jpg?param=500y500","The player's own cover is requested at once, at the slot's size, beside the catalog search");
    check(!f.input().track->position&&!f.input().track->duration,"Without a length the fraction has no time yet");
    f.answer(1,pixels(8));check(f.owner->artwork()&&f.owner->artwork()->straightRGBA[0]==8,"The player's cover shows before the catalog answers");
    f.answer(0,body(catalogSearch));
    check(f.transport.requests.size()==3&&f.transport.requests[2].kind==n::NowPlayingWeb::Kind::lyrics&&f.owner->artwork()->straightRGBA[0]==8,
          "The catalog answer adds lyrics; the cover is never downloaded twice");
    check(f.input().track->duration==233.4&&std::abs(*f.input().track->position-116.7)<1e-9&&f.input().track->sampledAt==10,"The played half of the catalog length");
    f.answer(2,body(R"({"code":200,"lrc":{"lyric":"[00:01] Catalog"}})"));
    // The bar is drawn in steps: a reading a little behind keeps the running time.
    f.now=11;f.read(barPlayer(.5+.6/233.4,11));
    check(std::abs(*f.input().track->position-117.7)<1e-9,"A reading 0.4 s behind the running time keeps the running time");
    f.now=12;f.read(barPlayer(.5+2.3/233.4,12));
    check(std::abs(*f.input().track->position-119.)<1e-9,"A reading ahead resynchronizes");
    f.now=13;f.read(barPlayer(60/233.4,13));
    check(std::abs(*f.input().track->position-60)<1e-9,"A seek back inside the player resynchronizes");
    f.now=14;f.read(barPlayer(60/233.4,14,true,{},2.5));
    check(std::abs(*f.input().track->position-62.5)<1e-9,"The playing time since a trusted reading is added");
    // Once per displayed second while the time runs, the player's controls are read anew.
    f.read(barPlayer(.5,f.now));const auto before=f.p->resyncs;unsigned ticks{};
    for(int k=0;k<3;++k){
        const auto due=f.owner->nextWakeTime(f.now);if(!due)break;f.now=*due;f.owner->deadline(f.now);++ticks;f.flush();
        f.p->readReady(barPlayer(.5+(f.now-14)/233.4,f.now));f.flush();
    }
    check(ticks==3&&f.p->resyncs==before+3,"Each displayed second asks the player's controls once");
    f.read(barPlayer(.6,f.now,false));const auto paused=f.p->resyncs;
    check(!f.owner->nextWakeTime(f.now)&&f.p->resyncs==paused,"Paused: no display tick, no reading");
    // A window player without its own controls (an estimate) is never re-read on the clock.
    auto estimate=windowPlayer("Synthetic Song",3);estimate.value.track->sampledAt=f.now;f.read(estimate);const auto plain=f.p->resyncs;
    for(int k=0;k<2;++k){const auto due=f.owner->nextWakeTime(f.now);if(!due)break;f.now=*due;f.owner->deadline(f.now);f.flush();}
    check(f.p->resyncs==plain,"An estimated time never asks for readings");
    f.owner->setActive(false,f.now);
    check(!f.owner->nextWakeTime(f.now),"Hidden: no tick and no reading");
}
// NetEase Cloud Music's bar carries no time labels and its progress slider no
// accessible value, so the HUD reads only the played fraction. When NetEase's
// own queue state file is readable it supplies the track's exact length (so
// the fraction becomes seconds and the bar and lyric clock run) and the exact
// song id (so the lyrics are the track's own, not a fuzzy catalog guess).
// Sanitized synthetic queue only.
void neteaseQueue(){
    const std::string own="https://p3.music.126.net/Own==/1.jpg?param=600y600";
    const std::string queue=R"({"list":[{"id":"2001","track":{"id":2001,"name":"Synthetic Song","artists":[{"name":"Example Artist"}],"album":{"name":"Fixture Album","picUrl":"http://p3.music.126.net/Queue==/2001.jpg"},"duration":233400}}]})";
    Fixture f(true,false,500,body(queue));
    f.ready(barPlayer(.5,10,true,own));
    // The bar's own cover is requested at once; the catalog reads the queue
    // (parsed off-thread) instead of the fuzzy title search.
    check(f.transport.requests.size()==2&&f.transport.requests[0].kind==n::NowPlayingWeb::Kind::artwork&&
          f.transport.requests[1].url=="https://music.163.com/api/song/lyric?id=2001&lv=-1&kv=-1&tv=-1",
          "The queue's exact song id reads the track's own lyrics; no cloudsearch");
    check(std::none_of(f.transport.requests.begin(),f.transport.requests.end(),[](const auto&r){return r.url.find("/api/cloudsearch/pc?")!=std::string::npos;}),"The fuzzy title search is skipped entirely");
    check(f.input().track->duration==233.4&&std::abs(*f.input().track->position-116.7)<1e-9,"The queue length turns the played half into seconds");
    f.answer(1,body(R"({"code":200,"lrc":{"lyric":"[00:01]One\n[01:50]Two\n[03:00]Three"}})"));
    check(f.input().lyrics&&f.input().lyrics->lines().size()==3,"The track's own lyrics reach the rows");
    check(f.owner->presentation().lyricRows()[1]=="Two","At 116.7 s the current lyric line (110 s) is centred: the lyric clock follows the bar");
    // The bar advances with the same clock: a later reading keeps the running time.
    f.now=12;f.read(barPlayer(.5+2.3/233.4,12));
    check(std::abs(*f.input().track->position-119.)<1e-9,"The bar keeps tracking in seconds against the queue length");
    f.now=130;f.owner->deadline(f.now);f.flush();
    check(f.owner->presentation().lyricRows()[1]=="Two"||f.owner->presentation().lyricRows()[1]=="Three","The lyric line advances on the shared clock as the song plays");
    // A track the queue does not carry still falls back to the fuzzy search.
    f.read(windowPlayer("Unqueued Song",0));
    check(std::any_of(f.transport.requests.begin(),f.transport.requests.end(),[](const auto&r){return r.url.find("/api/cloudsearch/pc?")!=std::string::npos;}),"An unqueued track falls back to the catalog title search");
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
    try{lrclibLyrics();netease();windowPlayerCatalog();sizedCover();thumbnailFirst();playerBar();neteaseQueue();embedded();visibility();offline();events();offThread();volume();
        std::cout<<"Now Playing sources session: "<<checks<<" checks passed (injected provider/transport/audio routes)\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Now Playing sources session failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
