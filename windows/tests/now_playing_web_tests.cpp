// NowPlayingWeb owner contracts with an injected in-memory transport. No test
// performs a network request; the Windows transport is only constructed.
#include "native/now_playing_web.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace n=endfield::native;namespace m=endfield::modules;
namespace {
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}
struct Task final:n::NowPlayingWeb::Task {std::atomic<int>cancels{};void cancel()noexcept override{++cancels;}};
struct Transport {
    std::vector<n::NowPlayingWeb::Request>requests;std::vector<n::NowPlayingWeb::Done>done;std::vector<std::shared_ptr<Task>>tasks;bool refuse{};
    n::NowPlayingWeb::Transport bind(){return [this](const n::NowPlayingWeb::Request&r,n::NowPlayingWeb::Done d)->std::shared_ptr<n::NowPlayingWeb::Task>{
        if(refuse)return nullptr;requests.push_back(r);done.push_back(std::move(d));tasks.push_back(std::make_shared<Task>());return tasks.back();};}
};
m::NowPlayingBody body(std::string_view text){return std::make_shared<const std::vector<std::uint8_t>>(text.begin(),text.end());}
const std::string lrclib="https://lrclib.net/api/get?track_name=A&artist_name=B";

void acceptance(){
    using K=n::NowPlayingWeb::Kind;
    check(n::nowPlayingWebAccepts(K::lyrics,200,"application/json",std::nullopt),"LRCLIB JSON with unknown length is accepted");
    check(n::nowPlayingWebAccepts(K::lyrics,200,"Text/Plain",1024),"text/plain is a source lyric MIME type (case-insensitive)");
    check(!n::nowPlayingWebAccepts(K::lyrics,200,"text/html",10),"Other lyric MIME types are refused");
    check(!n::nowPlayingWebAccepts(K::lyrics,203,"application/json",10)&&!n::nowPlayingWebAccepts(K::lyrics,302,"application/json",10),"Lyrics require exactly HTTP 200; redirects are never followed");
    check(!n::nowPlayingWebAccepts(K::lyrics,200,"application/json",m::nowPlayingMaximumLyricsResponseBytes+1)&&n::nowPlayingWebAccepts(K::lyrics,200,"application/json",m::nowPlayingMaximumLyricsResponseBytes),"Lyric Content-Length bound is 1 MiB");
    check(n::nowPlayingWebAccepts(K::artwork,200,"image/jpeg",std::nullopt)&&n::nowPlayingWebAccepts(K::artwork,206,"image/png",100),"Artwork accepts any 2xx image/*");
    check(!n::nowPlayingWebAccepts(K::artwork,301,"image/jpeg",100)&&!n::nowPlayingWebAccepts(K::artwork,200,"text/html",100)&&!n::nowPlayingWebAccepts(K::artwork,200,"image/",100),"Artwork refuses redirects and non-images");
    check(!n::nowPlayingWebAccepts(K::artwork,200,"image/png",m::nowPlayingMaximumArtworkBytes+1),"Artwork Content-Length bound is 8 MiB");
}
void lifecycle(){
    Transport t;double now=100;unsigned wakes{};
    auto web=std::make_unique<n::NowPlayingWeb>(t.bind(),[&]{++wakes;},[&]{return now;});
    unsigned nulls{};web->fetch({"https://evil.example/api/get",n::NowPlayingWeb::Kind::lyrics},[&](m::NowPlayingBody b){if(!b)++nulls;});
    web->fetch({"https://p1.music.126.net/a.jpg",n::NowPlayingWeb::Kind::lyrics},[&](m::NowPlayingBody b){if(!b)++nulls;});
    web->fetch({lrclib,n::NowPlayingWeb::Kind::artwork},[&](m::NowPlayingBody b){if(!b)++nulls;});
    check(nulls==3&&t.requests.empty()&&web->stats().rejected==3,"Disallowed URLs complete synchronously with no transport request");
    m::NowPlayingBody first,second;unsigned firstCalls{};
    web->fetch({lrclib,n::NowPlayingWeb::Kind::lyrics},[&](m::NowPlayingBody b){++firstCalls;first=std::move(b);});
    web->fetch({"https://p2.music.126.net/x.jpg?param=600y600",n::NowPlayingWeb::Kind::artwork},[&](m::NowPlayingBody b){second=std::move(b);});
    check(t.requests.size()==2&&t.requests[0].url==lrclib&&t.requests[1].kind==n::NowPlayingWeb::Kind::artwork,"Allowed requests reach the transport unchanged");
    check(web->nextWakeTime()==110,"The source 10 s resource bound is folded into the host deadline");
    std::thread worker([&]{t.done[0](body("{\"syncedLyrics\":\"[00:01]a\"}"));t.done[1](body("png"));});worker.join();
    check(wakes==1&&firstCalls==0,"Worker-thread completions post one coalesced owner wake and never run owner code");
    check(web->drain()&&firstCalls==1&&first&&first->size()==std::string_view("{\"syncedLyrics\":\"[00:01]a\"}").size()&&second&&second->size()==3,"Completions run only inside the owner drain");
    check(!web->drain()&&!web->nextWakeTime()&&web->stats().completed==2,"Drained requests leave no deadline");
    bool cancelledRan{};auto cancel=web->fetch({lrclib,n::NowPlayingWeb::Kind::lyrics},[&](m::NowPlayingBody){cancelledRan=true;});
    cancel();check(t.tasks.back()->cancels==1&&!web->nextWakeTime(),"Cancellation cancels the transport task and clears the deadline");
    t.done.back()(body("late"));web->drain();check(!cancelledRan,"A cancelled request never delivers, even when the transport answers late");
    bool timedOut{};m::NowPlayingBody timeoutBody=body("x");web->fetch({lrclib,n::NowPlayingWeb::Kind::lyrics},[&](m::NowPlayingBody b){timedOut=true;timeoutBody=std::move(b);});
    now=109.9;check(!web->drain()&&!timedOut,"No timeout before the bound");now=120;
    check(web->drain()&&timedOut&&!timeoutBody&&t.tasks.back()->cancels==1&&web->stats().timedOut==1,"Expired requests are cancelled and complete with no data");
    std::string large(m::nowPlayingMaximumLyricsResponseBytes+1,'x');bool oversized{};
    web->fetch({lrclib,n::NowPlayingWeb::Kind::lyrics},[&](m::NowPlayingBody b){oversized=!b;});t.done.back()(body(large));web->drain();
    check(oversized,"A transport body beyond the lyric bound is discarded");
    std::vector<int>order;for(int k=0;k<6;++k)web->fetch({lrclib,n::NowPlayingWeb::Kind::lyrics},[&,k](m::NowPlayingBody b){if(!b)order.push_back(k);});
    check(web->stats().inFlight==n::NowPlayingWeb::maximumInFlight&&order==std::vector<int>({4,5}),"In-flight requests are bounded; excess requests fail immediately");
    web->cancelAll();check(web->stats().inFlight==0,"cancelAll retires every request");
    t.refuse=true;bool refused{};web->fetch({lrclib,n::NowPlayingWeb::Kind::lyrics},[&](m::NowPlayingBody b){refused=!b;});check(refused,"A transport that cannot start completes with no data");t.refuse=false;
    // Loader integration: the fetch adapter feeds the original LRCLIB loader.
    m::NowPlayingLyricsLoader loader(web->lyricsFetch());m::NowPlayingTrack track;track.title="A";track.artist="B";
    loader.request(m::NowPlayingLyricsKey::make("Spotify.exe",track),track);check(t.requests.back().url==lrclib,"Loader requests go through the owner");
    t.done.back()(body("{\"syncedLyrics\":\"[00:01] Line\"}"));check(!loader.lyrics(),"No loader state changes off the owner drain");
    web->drain();check(loader.lyrics()&&loader.lyrics()->lines().front().text=="Line","Owner drain completes the loader");
    auto stale=t.done.size();loader.request(m::NowPlayingLyricsKey::make("Spotify.exe",m::NowPlayingTrack{"C","D"}),m::NowPlayingTrack{"C","D"});
    const auto pending=t.done.back();web.reset();pending(body("{}"));check(t.done.size()==stale+1,"Transport completion after owner teardown is ignored safely");
}
}
int main(){
    try{
        acceptance();lifecycle();
#ifdef _WIN32
        // Construction only: obtaining the factory performs no IO or request.
        check(bool(n::windowsNowPlayingTransport()),"Windows.Web.Http transport factory is available");
#endif
        std::cout<<"Now Playing web: "<<checks<<" checks passed (injected transport, no network)\n";return 0;
    }catch(const std::exception&e){std::cerr<<"Now Playing web failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
