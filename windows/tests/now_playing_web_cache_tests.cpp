// The Now Playing disk cache of accepted downloads and its use by
// NowPlayingWeb, in a private temporary directory (removed afterwards) with an
// injected in-memory transport. No network request; no user data.
#include "native/now_playing_web_cache.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>

namespace n=endfield::native;namespace m=endfield::modules;namespace fs=std::filesystem;
using Q=endfield::app::UtilityExecutor;using Kind=n::NowPlayingWeb::Kind;
namespace {
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}
m::NowPlayingBody body(std::string_view text){return std::make_shared<const std::vector<std::uint8_t>>(text.begin(),text.end());}
std::string text(const m::NowPlayingBody&b){return b?std::string(b->begin(),b->end()):std::string("<null>");}
const n::NowPlayingWeb::Request search{"https://music.163.com/api/cloudsearch/pc?s=Synthetic%20Song%20Example%20Artist&type=1&limit=10&offset=0",Kind::lyrics};
const n::NowPlayingWeb::Request lyric{"https://music.163.com/api/song/lyric?id=1901371647&lv=-1&kv=-1&tv=-1",Kind::lyrics};
const n::NowPlayingWeb::Request cover{"https://p1.music.126.net/AbC==/1099.jpg?param=500y500",Kind::artwork};
n::NowPlayingWeb::Request numbered(int k){return {"https://lrclib.net/api/get?track_name=Song"+std::to_string(k)+"&artist_name=A",Kind::lyrics};}
struct Directory{
    fs::path path;
    Directory(){
        std::random_device random;
        path=fs::temp_directory_path()/("EndfieldHUD-NowPlayingCache-"+std::to_string(random())+"-"+std::to_string(random()));
        fs::create_directories(path);
    }
    ~Directory(){std::error_code ignored;fs::remove_all(path,ignored);}
    fs::path cache()const{return path/"NowPlaying";}
};
void settle(Q&q){for(int k=0;k<4;++k){q.waitIdle();q.drain();}}
std::size_t files(const fs::path&dir){std::size_t count{};std::error_code error;for(fs::directory_iterator it(dir,error),end;!error&&it!=end;it.increment(error))++count;return count;}

void storage(){
    Directory dir;Q q{[]{}};
    n::NowPlayingWebCache cache(q,{dir.cache(),3,4096});
    check(!cache.contains(search)&&!cache.stats().indexed,"Nothing is listed before the index exists");
    cache.prepare();settle(q);
    check(cache.stats().indexed&&cache.stats().entries==0&&!fs::exists(dir.cache()),"Indexing a missing directory lists nothing and creates nothing");
    cache.store(search,body("{\"songs\":1}"));settle(q);
    const auto file=dir.cache()/n::NowPlayingWebCache::fileName(search);
    check(fs::is_regular_file(file)&&cache.contains(search)&&cache.stats().entries==1&&cache.stats().stores==1,"An accepted body is written under a hashed name and listed");
    check(n::NowPlayingWebCache::fileName(search).size()==19&&n::NowPlayingWebCache::fileName(search).find("Synthetic")==std::string::npos,
          "File names carry no track text (16 hex digits and .np)");
    check(!cache.contains({search.url,Kind::artwork}),"The request kind is part of the key");
    m::NowPlayingBody loaded;bool ran{};
    (void)cache.load(search,[&](m::NowPlayingBody b){ran=true;loaded=std::move(b);});
    check(!ran,"A read completes only through the executor drain");
    settle(q);
    check(ran&&text(loaded)=="{\"songs\":1}"&&cache.stats().hits==1,"The stored body is read back byte for byte");
    bool cancelledRan{};
    auto cancel=cache.load(search,[&](m::NowPlayingBody){cancelledRan=true;});cancel();settle(q);
    check(!cancelledRan,"A cancelled read never completes");
    // A damaged entry (another URL in its header) is a miss and is removed.
    {
        std::ofstream out(file,std::ios::binary|std::ios::trunc);
        out<<"EHNPCACHE1\nl\nhttps://music.163.com/api/other\n{}";
    }
    m::NowPlayingBody damaged=body("x");
    (void)cache.load(search,[&](m::NowPlayingBody b){damaged=std::move(b);});settle(q);
    check(!damaged&&!fs::exists(file)&&!cache.contains(search)&&cache.stats().misses==1,"A damaged entry reads as a miss and is removed");
    // Bounds: three entries, least recently used first out.
    for(int k=0;k<3;++k){cache.store(numbered(k),body("[00:01] "+std::to_string(k)));settle(q);}
    (void)cache.load(numbered(0),[](m::NowPlayingBody){});settle(q);
    cache.store(numbered(3),body("[00:01] 3"));settle(q);
    check(cache.stats().entries==3&&cache.contains(numbered(0))&&!cache.contains(numbered(1))&&cache.contains(numbered(2))&&cache.contains(numbered(3))&&
              !fs::exists(dir.cache()/n::NowPlayingWebCache::fileName(numbered(1)))&&files(dir.cache())==3,
          "Beyond the entry bound the least recently used entry goes, file included");
    cache.store(cover,body(std::string(4000,'j')));settle(q);
    check(cache.stats().bytes<=4096&&cache.contains(cover)&&cache.stats().entries==1,"Beyond the byte bound older entries go; the newest stays");
    cache.store(lyric,body(std::string(m::nowPlayingMaximumLyricsResponseBytes+1,'l')));cache.store(lyric,nullptr);cache.store(lyric,body(""));settle(q);
    check(!cache.contains(lyric)&&cache.stats().stores==6,"Oversized, missing and empty bodies are never stored");
}
void relaunch(){
    Directory dir;
    {
        Q q{[]{}};n::NowPlayingWebCache cache(q,{dir.cache()});cache.prepare();settle(q);
        cache.store(search,body("first"));settle(q);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        cache.store(cover,body("\xff\xd8\xff"));settle(q);
    }
    // Leftovers of an interrupted write and foreign files.
    {std::ofstream(dir.cache()/"0123456789abcdef.np.tmp")<<"partial";}
    {std::ofstream(dir.cache()/"notes.txt")<<"foreign";}
    {std::ofstream(dir.cache()/"zzzzzzzzzzzzzzzz.np")<<"foreign";}
    Q q{[]{}};n::NowPlayingWebCache cache(q,{dir.cache(),1,1<<20});
    cache.prepare();settle(q);
    check(cache.stats().indexed&&cache.stats().entries==1&&cache.contains(cover)&&!cache.contains(search)&&
              !fs::exists(dir.cache()/n::NowPlayingWebCache::fileName(search)),"A later launch indexes the entries (by last use) and applies its own bounds");
    check(!fs::exists(dir.cache()/"0123456789abcdef.np.tmp")&&fs::exists(dir.cache()/"notes.txt")&&fs::exists(dir.cache()/"zzzzzzzzzzzzzzzz.np"),
          "Interrupted writes are removed; foreign files are left alone");
    m::NowPlayingBody loaded;(void)cache.load(cover,[&](m::NowPlayingBody b){loaded=std::move(b);});settle(q);
    check(text(loaded)=="\xff\xd8\xff","An entry written by an earlier launch is read back");
}

// Production keeps only answers that carry lyrics, songs or a cover: a
// refusal ({"code":-460}) or an empty answer is never written, and one an
// earlier build stored reads as a miss and is removed, so it is asked again.
void policy(){
    Directory dir;Q q{[]{}};
    const auto keep=[](const n::NowPlayingWeb::Request&r,std::span<const std::uint8_t>b){return r.kind!=Kind::lyrics||m::nowPlayingLyricsBodyCacheable(r.url,b);};
    const auto file=[&](const n::NowPlayingWeb::Request&r){return dir.cache()/n::NowPlayingWebCache::fileName(r);};
    {
        // An earlier build (no policy) stored a refusal and an empty search.
        n::NowPlayingWebCache old(q,{dir.cache()});old.prepare();settle(q);
        old.store(lyric,body(R"({"code":-460,"message":"Cheating"})"));old.store(search,body(R"({"code":200,"result":{"songs":[]}})"));settle(q);
        check(fs::exists(file(lyric))&&fs::exists(file(search)),"Without a policy every accepted body is stored");
    }
    n::NowPlayingWebCache::Options options{dir.cache()};options.keep=keep;
    n::NowPlayingWebCache cache(q,options);cache.prepare();settle(q);
    check(cache.contains(lyric)&&cache.contains(search),"The earlier entries are indexed");
    m::NowPlayingBody first=body("x"),second=body("x");
    (void)cache.load(lyric,[&](m::NowPlayingBody b){first=std::move(b);});(void)cache.load(search,[&](m::NowPlayingBody b){second=std::move(b);});settle(q);
    check(!first&&!second&&!fs::exists(file(lyric))&&!fs::exists(file(search))&&!cache.contains(lyric)&&cache.stats().refused==2,
          "A stored refusal or empty answer reads as a miss and is removed (asked again)");
    cache.store(lyric,body(R"({"code":-460,"message":"Cheating"})"));cache.store(search,body(R"({"code":200,"result":{"songs":[]}})"));settle(q);
    check(!fs::exists(file(lyric))&&!fs::exists(file(search))&&cache.stats().refused==4&&cache.stats().stores==0,"Refusals and empty answers are never written");
    cache.store(lyric,body(R"({"code":200,"lrc":{"lyric":"[00:01]x"}})"));cache.store(cover,body("\xff\xd8\xff"));settle(q);
    check(cache.contains(lyric)&&cache.contains(cover)&&cache.stats().stores==2,"Lyrics and covers are kept");
    m::NowPlayingBody kept;(void)cache.load(lyric,[&](m::NowPlayingBody b){kept=std::move(b);});settle(q);
    check(text(kept)==R"({"code":200,"lrc":{"lyric":"[00:01]x"}})","A kept answer is read back");
}

struct Task final:n::NowPlayingWeb::Task{unsigned cancels{};void cancel()noexcept override{++cancels;}};
struct Transport{
    std::vector<n::NowPlayingWeb::Request>requests;std::vector<n::NowPlayingWeb::Done>done;std::vector<std::shared_ptr<Task>>tasks;
    n::NowPlayingWeb::Transport bind(){return [this](const n::NowPlayingWeb::Request&r,n::NowPlayingWeb::Done d)->std::shared_ptr<n::NowPlayingWeb::Task>{requests.push_back(r);done.push_back(std::move(d));tasks.push_back(std::make_shared<Task>());return tasks.back();};}
};
void web(){
    Directory dir;Q q{[]{}};double now=10;Transport t;
    auto cache=std::make_shared<n::NowPlayingWebCache>(q,n::NowPlayingWebCache::Options{dir.cache()});
    n::NowPlayingWeb web(t.bind(),[]{},[&]{return now;},10,cache);
    web.prepare();settle(q);
    std::string first;
    web.fetch(search,[&](m::NowPlayingBody b){first=text(b);});
    check(t.requests.size()==1,"An uncached request goes to the network at once");
    t.done[0](body("{\"songs\":[]}"));web.drain();settle(q);
    check(first=="{\"songs\":[]}"&&cache->contains(search)&&web.stats().cacheStores==1,"The accepted answer is delivered and kept on disk");
    std::string second;bool delivered{};
    web.fetch(search,[&](m::NowPlayingBody b){delivered=true;second=text(b);});
    check(t.requests.size()==1&&!delivered&&web.nextWakeTime()==20,"A cached request reads the disk first, within the same 10 s bound");
    settle(q);web.drain();
    check(delivered&&second=="{\"songs\":[]}"&&t.requests.size()==1&&web.stats().cacheHits==1&&web.stats().cacheStores==1&&!web.nextWakeTime(),
          "It is answered from disk: no network request and no second write");
    // A damaged entry falls back to the network for the same request.
    {std::ofstream out(dir.cache()/n::NowPlayingWebCache::fileName(search),std::ios::binary|std::ios::trunc);out<<"garbage";}
    std::string third;
    web.fetch(search,[&](m::NowPlayingBody b){third=text(b);});settle(q);web.drain();
    check(t.requests.size()==2&&third.empty()&&web.stats().cacheMisses==1,"A cache miss starts the network request");
    t.done[1](body("{\"songs\":[2]}"));web.drain();settle(q);
    check(third=="{\"songs\":[2]}"&&cache->contains(search),"The network answer replaces the damaged entry");
    // Cancellation while the disk is read: nothing runs, nothing is sent.
    bool cancelledRan{};
    auto cancel=web.fetch(search,[&](m::NowPlayingBody){cancelledRan=true;});cancel();settle(q);web.drain();
    check(!cancelledRan&&t.requests.size()==2&&web.stats().inFlight==0,"A request cancelled during its cache read never completes or reaches the network");
    // The deadline also covers the cache read.
    bool expired{};m::NowPlayingBody late=body("x");
    web.fetch(search,[&](m::NowPlayingBody b){expired=true;late=std::move(b);});
    now=25;web.drain();
    check(expired&&!late&&web.stats().timedOut==1,"An overdue cache read completes with no data");
    settle(q);web.drain();
    check(t.requests.size()==2,"A cache answer after the deadline is discarded");
    // Without a cache the web owner behaves as before.
    Transport plain;n::NowPlayingWeb direct(plain.bind(),[]{},[&]{return now;});direct.prepare();
    direct.fetch(search,[](m::NowPlayingBody){});
    check(plain.requests.size()==1&&direct.stats().cacheHits==0,"No cache: every request reaches the transport");
}
}
int main(){
    try{
        storage();relaunch();policy();web();
        std::cout<<"Now Playing web cache: "<<checks<<" checks passed (private temporary directory, injected transport)\n";return 0;
    }catch(const std::exception&e){std::cerr<<"Now Playing web cache failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
