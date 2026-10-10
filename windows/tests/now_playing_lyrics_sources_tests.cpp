// Source: python3 windows/tools/now_playing_lyrics_reference.py (unchanged Mac
// NowPlayingLyrics/NowPlayingCatalog/NowPlayingController lyric precedence).
// Synthetic metadata and injected in-memory responses only: no network.
#include "modules/now_playing_lyrics_sources.hpp"
#include "core/data/json.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace m=endfield::modules;using Json=ehud::data::Json;
namespace {
unsigned checks{};
void check(bool value,const std::string&what){++checks;if(!value)throw std::runtime_error(what);}
std::optional<double>number(const Json&j){if(j.isNumber())return j.number();return {};}
std::optional<std::string>text(const Json&j){if(j.isString())return j.string();return {};}
m::NowPlayingTrack track(const Json&j){
    m::NowPlayingTrack t;t.title=text(j["title"]).value_or("");t.artist=text(j["artist"]).value_or("");t.album=text(j["album"]).value_or("");
    t.duration=number(j["duration"]);t.position=number(j["position"]).value_or(0);t.isPlaying=j["playing"].isBool()?j["playing"].boolean():true;
    t.identifier=text(j["identifier"]);t.timedLyrics=text(j["lyrics"]);return m::NowPlayingTrack::bounded(std::move(t));
}
m::NowPlayingBody bytes(std::string_view value){return std::make_shared<const std::vector<std::uint8_t>>(value.begin(),value.end());}
std::span<const std::uint8_t>view(const m::NowPlayingBody&b){return {b->data(),b->size()};}
bool sameLines(const std::optional<m::NowPlayingLyrics>&actual,const Json&expected){
    if(expected.isNull())return !actual;if(!actual)return false;const auto&rows=expected.array();if(rows.size()!=actual->lines().size())return false;
    for(std::size_t n=0;n<rows.size();++n){const auto&row=rows[n].array();if(row[0].number()!=actual->lines()[n].time||!m::nowPlayingCanonicalEqual(row[1].string(),actual->lines()[n].text))return false;}
    return true;
}
bool sameLines(const std::shared_ptr<const m::NowPlayingLyrics>&actual,const Json&expected){return sameLines(actual?std::optional(*actual):std::nullopt,expected);}
std::optional<std::string>optionalText(const Json&j){return j.isNull()?std::nullopt:std::optional(j.string());}
m::NowPlayingSource sourceNamed(std::string_view name){for(auto s:{m::NowPlayingSource::music,m::NowPlayingSource::spotify,m::NowPlayingSource::netease,m::NowPlayingSource::qqMusic,m::NowPlayingSource::kugou,m::NowPlayingSource::system})if(m::nowPlayingSourceKey(s)==name)return s;throw std::runtime_error("Unknown fixture source");}

void matcher(const Json&j){
    const auto&rows=j["normalized"].array();std::vector<std::string>mine;
    for(const auto&row:rows){mine.push_back(m::NowPlayingTrackMatcher::normalized(row["input"].string()));
        check(m::nowPlayingCanonicalEqual(mine.back(),row["expected"].string()),"Source normalized() folding: "+row["input"].string());}
    for(std::size_t a=0;a<rows.size();++a)for(std::size_t b=a+1;b<rows.size();++b)
        check((mine[a]==mine[b])==m::nowPlayingCanonicalEqual(rows[a]["expected"].string(),rows[b]["expected"].string()),"Folding equivalence classes match source");
    for(const auto&row:j["artists"].array()){const auto actual=m::NowPlayingTrackMatcher::artists(row["input"].string());const auto&expected=row["expected"].array();
        bool same=actual.size()==expected.size();for(const auto&e:expected){bool found{};for(const auto&a:actual)found=found||m::nowPlayingCanonicalEqual(a,e.string());same=same&&found;}
        check(same,"Source artist set: "+row["input"].string());}
    for(const auto&row:j["scores"].array()){std::vector<std::string>aliases,names;for(const auto&a:row["aliases"].array())aliases.push_back(a.string());for(const auto&a:row["artists"].array())names.push_back(a.string());
        const auto score=m::NowPlayingTrackMatcher::score(row["title"].string(),aliases,names,row["album"].string(),number(row["duration"]),track(row["target"]));
        check(row["expected"].isNull()?!score:score&&*score==row["expected"].integer(),"Source match score: "+row["title"].string());}
}
void urls(const Json&j){
    for(const auto&row:j["urls"].array()){const auto t=track(row["track"]);const auto&e=row["expected"];
        check(m::nowPlayingLyricsRequestURL(t)==optionalText(e["lrclibGet"]),"LRCLIB get URL is byte-identical");
        check(m::nowPlayingLyricsSearchURL(t)==optionalText(e["lrclibSearch"]),"LRCLIB search URL is byte-identical");
        check(m::nowPlayingCatalogSearchURL(t)==optionalText(e["neteaseSearch"]),"NetEase cloudsearch URL is byte-identical");
        for(const auto&url:{m::nowPlayingLyricsRequestURL(t),m::nowPlayingLyricsSearchURL(t),m::nowPlayingCatalogSearchURL(t)})if(url&&url->size()<=8192)check(m::nowPlayingLyricsURLAllowed(*url),"Every built lyric URL passes the source allow-list");}
    for(const auto&row:j["lyricsURLs"].array())check(m::nowPlayingCatalogLyricsURL(row["id"].integer())==row["expected"].string(),"NetEase song/lyric URL is byte-identical");
    for(const auto&row:j["covers"].array())check(m::nowPlayingCatalogCoverURL(row["input"].string())==optionalText(row["expected"]),"Source cover URL rule: "+row["input"].string());
    for(const auto&row:j["allowedLyrics"].array())check(m::nowPlayingLyricsURLAllowed(row["url"].string())==row["expected"].boolean(),"Lyric download allow-list: "+row["url"].string());
    for(const auto&row:j["allowedArtwork"].array())check(m::nowPlayingArtworkURLAllowed(row["url"].string())==row["expected"].boolean(),"Artwork allow-list: "+row["url"].string());
}
void decoders(const Json&j){
    for(const auto&row:j["responses"].array()){const auto b=bytes(row["body"].string());check(sameLines(m::nowPlayingDecodeLyricsResponse(view(b)),row["expected"]),"LRCLIB get decode: "+row["body"].string().substr(0,40));}
    for(const auto&row:j["searches"].array()){const auto b=bytes(row["body"].string());check(sameLines(m::nowPlayingDecodeLyricsSearch(view(b),track(row["track"])),row["expected"]),"LRCLIB search ranking: "+row["body"].string().substr(0,60));}
    for(const auto&row:j["matches"].array()){const auto b=bytes(row["body"].string());const auto match=m::nowPlayingCatalogMatch(view(b),track(row["track"]));const auto&e=row["expected"];
        check(e.isNull()?!match:match&&match->identifier==e["identifier"].integer()&&match->score==e["score"].integer()&&match->artwork==optionalText(e["artwork"]),"NetEase match: "+row["body"].string().substr(0,80));}
    for(const auto&row:j["catalogLyrics"].array()){const auto b=bytes(row["body"].string());check(sameLines(m::nowPlayingCatalogDecodeLyrics(view(b)),row["expected"]),"NetEase lyric decode: "+row["body"].string().substr(0,40));}
    // Bounds that a pinned fixture should not carry: >1 MiB responses are rejected unread.
    std::string large="{\"syncedLyrics\":\"[00:01] x\",\"pad\":\""+std::string(m::nowPlayingMaximumLyricsResponseBytes,'a')+"\"}";
    const auto big=bytes(large);check(!m::nowPlayingDecodeLyricsResponse(view(big))&&!m::nowPlayingCatalogDecodeLyrics(view(big)),"Responses beyond NowPlayingLyricsDownload.maximumBytes are rejected");
}
struct Script {
    std::vector<std::string>urls;std::vector<std::function<void(m::NowPlayingBody)>>completions;std::vector<std::size_t>cancelled;bool immediate{};
    m::NowPlayingFetch fetch(){return [this](const std::string&url,std::function<void(m::NowPlayingBody)>completion){
        const auto index=urls.size();urls.push_back(url);completions.push_back(completion);
        if(immediate){completion(nullptr);return std::function<void()>([]{});}
        return std::function<void()>([this,index]{cancelled.push_back(index);});};}
};
void scenarios(const Json&j){
    for(const auto&scenario:j["scenarios"].array()){
        const auto name=scenario["name"].string();Script script;script.immediate=scenario["immediate"].isBool()&&scenario["immediate"].boolean();
        m::NowPlayingLyricsSources sources(script.fetch(),!scenario["catalog"].isBool()||scenario["catalog"].boolean());
        std::vector<std::string>provided;sources.onArtworkURL=[&](const std::string&url,const m::NowPlayingLyricsKey&){provided.push_back(url);};
        std::optional<m::NowPlayingTrack>current;std::optional<m::NowPlayingLyricsKey>key;unsigned step{};
        for(const auto&row:scenario["steps"].array()){
            const auto op=row["op"].string();++step;
            if(op=="show"){current=track(row["track"]);const auto source=row["source"].string();
                key=m::NowPlayingLyricsKey{source,sourceNamed(source),current->identifier,current->title,current->artist,current->album,current->duration};sources.update(key,current);}
            else if(op=="hide"){sources.hide();current.reset();key.reset();}
            else if(op=="invalidate"){sources.invalidateMissing();sources.update(key,current);}
            else if(op=="complete"){const auto index=static_cast<std::size_t>(row["index"].integer());check(index<script.completions.size(),name+": fixture completes an issued request");
                auto completion=script.completions[index];completion(row["body"].isNull()?nullptr:bytes(row["body"].string()));}
            const auto&e=row["expected"];const auto where=name+" step "+std::to_string(step)+" ("+op+")";
            std::vector<std::string>expectedURLs;for(const auto&u:e["urls"].array())expectedURLs.push_back(u.string());
            check(script.urls==expectedURLs,where+": identical request sequence");
            std::vector<std::size_t>expectedCancelled;for(const auto&c:e["cancelled"].array())expectedCancelled.push_back(static_cast<std::size_t>(c.integer()));
            check(script.cancelled==expectedCancelled,where+": identical cancellations");
            check(sameLines(sources.lyrics(),e["lyrics"]),where+": identical lyric precedence");
            check(sources.catalogArtwork()==optionalText(e["catalogArtwork"]),where+": identical catalog cover");
            check(sources.catalogLoading()==e["catalogLoading"].boolean(),where+": identical catalog loading state");
            std::vector<std::string>expectedProvided;for(const auto&u:e["provided"].array())expectedProvided.push_back(u.string());
            check(provided==expectedProvided,where+": identical cover supplements");
        }
    }
}
void windowsSources(){
    using S=m::NowPlayingSource;
    const std::pair<const char*,S>rows[]{{"Spotify.exe",S::spotify},{"spotify.EXE",S::spotify},{"SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify",S::spotify},
        {"cloudmusic.exe",S::netease},{"1F8B0F94.122165AE053F_j2p0p5q0044a6!App",S::netease},{"C:\\Program Files\\NetEase\\CloudMusic\\cloudmusic.exe",S::netease},{"QQMusic.exe",S::qqMusic},{"KuGou.exe",S::kugou},
        {"AppleInc.AppleMusicWin_nzyj5cx40ttqa!App",S::music},{"Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic",S::system},{"Chrome",S::system},
        {"MSEdge",S::system},{"cloudmusic.exe.evil",S::system},{"notcloudmusic.exe",S::system},{"",S::system}};
    for(const auto&[id,source]:rows)check(m::nowPlayingSourceForAppUserModelID(id)==source,std::string("AUMID source mapping: ")+id);
    check(m::nowPlayingSourceKey(S::qqMusic)=="qqMusic"&&m::nowPlayingSourceKey(S::system)=="system","Event Log source keys are the original raw values");
    // Cache bounds and the offline (injected provider) fetch never issue a request.
    m::NowPlayingLyricsLoader loader(m::nowPlayingOfflineFetch());
    for(int n=0;n<20;++n){m::NowPlayingTrack t;t.title="Song "+std::to_string(n);t.artist="Artist";loader.request(m::NowPlayingLyricsKey::make("Spotify.exe",t),t);}
    check(loader.cacheSize()==8&&!loader.lyrics(),"Offline LRCLIB loader keeps eight negative entries and no lyrics");
    m::NowPlayingCatalog catalog(m::nowPlayingOfflineFetch());
    for(int n=0;n<20;++n){m::NowPlayingTrack t;t.title="Song "+std::to_string(n);t.artist="Artist";catalog.request(m::NowPlayingLyricsKey::make("cloudmusic.exe",t),t);}
    check(catalog.cacheSize()==8&&!catalog.isLoading(),"Offline catalog keeps eight finished entries");
}
// Production decodes downloaded JSON on the shared utility worker, so a
// decode can finish after the wanted track changed or the owner cleared.
// Source order is unchanged; stale results are never applied or cached.
void deferredDecoding(){
    struct Jobs {
        std::vector<std::pair<std::function<void()>,std::function<void()>>>queued;
        m::NowPlayingDecoder bind(){return [this](std::function<void()>work,std::function<void()>done){queued.emplace_back(std::move(work),std::move(done));};}
        void run(std::size_t k){auto job=std::move(queued.at(k));job.first();job.second();}
    };
    struct Net {
        std::vector<std::string>urls;std::vector<std::function<void(m::NowPlayingBody)>>done;
        m::NowPlayingFetch bind(){return [this](const std::string&url,std::function<void(m::NowPlayingBody)>completion){urls.push_back(url);done.push_back(std::move(completion));return std::function<void()>([]{});};}
    };
    m::NowPlayingTrack a;a.title="Synthetic Song";a.artist="Example Artist";a.album="Fixture Album";a.duration=233.4;a=m::NowPlayingTrack::bounded(std::move(a));
    auto b=a;b.title="Other Song";
    const auto ka=m::NowPlayingLyricsKey::make("Spotify.exe",a),kb=m::NowPlayingLyricsKey::make("Spotify.exe",b);
    Jobs jobs;Net net;m::NowPlayingLyricsLoader loader(net.bind(),jobs.bind());unsigned changes{};loader.setOnChange([&]{++changes;});
    loader.request(ka,a);net.done.at(0)(bytes(R"({"syncedLyrics":"[00:01]First"})"));
    check(jobs.queued.size()==1&&!loader.lyrics()&&loader.cacheSize()==0,"A downloaded answer waits for its decode");
    loader.request(ka,a);check(net.urls.size()==1&&jobs.queued.size()==1,"The same track is not requested again while its decode is pending");
    jobs.run(0);check(loader.lyrics()&&loader.lyrics()->lines().front().text=="First"&&loader.cacheSize()==1&&changes==1,"Decoded lyrics are applied and cached on completion");
    loader.request(kb,b);net.done.at(1)(bytes(R"({"syncedLyrics":"[00:01]Other"})"));loader.request(ka,a);
    check(loader.lyrics()&&loader.lyrics()->lines().front().text=="First","Returning to a cached track is immediate");
    jobs.run(1);check(loader.lyrics()->lines().front().text=="First"&&loader.cacheSize()==1,"A stale decode is neither applied nor cached");
    loader.request(kb,b);check(net.urls.size()==3,"The discarded track asks again when it is wanted");
    net.done.at(2)(bytes("{}"));jobs.run(2);
    check(net.urls.size()==4&&net.urls[3].find("/api/search?")!=std::string::npos&&!loader.lyrics(),"A get answer without lyrics decodes to the search fallback");
    net.done.at(3)(bytes(R"([{"trackName":"Other Song","artistName":"Example Artist","albumName":"Fixture Album","duration":233,"syncedLyrics":"[00:02]Found"}])"));
    check(!loader.lyrics(),"The search answer also waits for its decode");
    jobs.run(3);check(loader.lyrics()&&loader.lyrics()->lines().front().text=="Found"&&net.urls.size()==4,"Search lyrics apply after decoding; at most two requests per track");
    loader.request(ka,a);loader.clear();check(!loader.lyrics(),"Clear removes the current lyrics");
    const std::string search=R"({"code":200,"result":{"songs":[{"id":1901371647,"name":"Synthetic Song","ar":[{"name":"Example Artist"}],"al":{"name":"Fixture Album","picUrl":"http://p1.music.126.net/AbC==/1099.jpg"},"dt":233400}]}})";
    Jobs catalogJobs;Net catalogNet;m::NowPlayingCatalog catalog(catalogNet.bind(),catalogJobs.bind());
    const auto kn=m::NowPlayingLyricsKey::make("cloudmusic.exe",a),ko=m::NowPlayingLyricsKey::make("cloudmusic.exe",b);
    catalog.request(kn,a);catalogNet.done.at(0)(bytes(search));
    check(catalog.isLoading()&&!catalog.artwork()&&catalogNet.urls.size()==1,"Catalog stays loading while its search decodes");
    catalogJobs.run(0);check(catalog.isLoading()&&catalog.artwork()==std::optional<std::string>("https://p1.music.126.net/AbC==/1099.jpg?param=600y600")&&catalogNet.urls.size()==2,"The decoded match supplies the cover and asks for its lyrics");
    catalogNet.done.at(1)(bytes(R"({"code":200,"lrc":{"lyric":"[00:01]Catalog"}})"));check(catalog.isLoading()&&!catalog.lyrics(),"Catalog lyrics wait for their decode");
    catalogJobs.run(1);check(!catalog.isLoading()&&catalog.lyrics()&&catalog.lyrics()->lines().front().text=="Catalog","Catalog finishes after the lyric decode");
    catalog.request(ko,b);catalogNet.done.at(2)(bytes(search));catalog.clear();catalogJobs.run(2);
    check(catalogNet.urls.size()==3&&catalog.cacheSize()==1&&!catalog.isLoading(),"A decode finishing after clear changes nothing and leaves no unfinished entry");
    catalog.request(kn,a);check(!catalog.isLoading()&&catalog.lyrics()&&catalogNet.urls.size()==3,"The finished entry is still a cache hit");
}
// Worst-case decode cost: a 1 MiB LRCLIB search answer whose 100 rows all
// match the track, so every row is scored and its lyrics parsed. Production
// runs this on the utility worker. Outcomes follow the source tie rule; the
// elapsed time is reported only.
void decodeCost(){
    m::NowPlayingTrack target;target.title="Synthetic Song";target.artist="Example Artist";target.album="Fixture Album";target.duration=233.4;target=m::NowPlayingTrack::bounded(std::move(target));
    const auto document=[](std::size_t preferred){
        std::string out="[";
        for(std::size_t row=0;row<100;++row){
            std::string lrc;for(std::size_t line=0;lrc.size()<9900;++line){char stamp[32];std::snprintf(stamp,sizeof stamp,"[%02zu:%02zu.%02zu]",line/60%60,line%60,row%100);lrc+=stamp;lrc+="Row "+std::to_string(row)+" synthetic lyric line "+std::to_string(line)+"\\n";}
            out+=(row?",":"")+std::string(R"({"trackName":"Synthetic Song","artistName":"Example Artist","albumName":")")+(row==preferred?"Fixture Album":"Other Album")+R"(","duration":233,"syncedLyrics":")"+lrc+"\"}";
        }
        return out+"]";
    };
    const auto tie=document(1000),unique=document(37);
    check(tie.size()<=m::nowPlayingMaximumLyricsResponseBytes&&unique.size()<=m::nowPlayingMaximumLyricsResponseBytes,"Worst-case search fixture stays within the 1 MiB response bound");
    const auto tied=bytes(tie),best=bytes(unique);
    const auto started=std::chrono::steady_clock::now();
    const auto none=m::nowPlayingDecodeLyricsSearch(view(tied),target);
    const auto middle=std::chrono::steady_clock::now();
    const auto chosen=m::nowPlayingDecodeLyricsSearch(view(best),target);
    const auto ended=std::chrono::steady_clock::now();
    check(!none,"Equally strong rows with different lyrics are not guessed (source tie rule)");
    check(chosen&&!chosen->lines().empty()&&chosen->lines().front().text.rfind("Row 37 ",0)==0,"The single album-matching row wins");
    std::cout<<"Worst-case LRCLIB search decode ("<<tie.size()<<" bytes, 100 scored rows): "
        <<std::chrono::duration<double,std::milli>(middle-started).count()<<" ms tie, "<<std::chrono::duration<double,std::milli>(ended-middle).count()<<" ms unique\n";
}
}
int main(int argc,char**argv){
    try{
        check(argc==2,"Pass now-playing-lyrics-source.json");std::ifstream in(argv[1],std::ios::binary);check(bool(in),"Read original lyric source fixture");
        const auto j=Json::parse(std::string((std::istreambuf_iterator<char>(in)),{}),8*1024*1024);
        check(j["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Lyric source fixture is pinned to the authority commit");
        matcher(j);urls(j);decoders(j);scenarios(j);windowsSources();deferredDecoding();decodeCost();
        std::cout<<"Now Playing lyrics sources: "<<checks<<" checks passed (original Swift oracle, injected responses only)\n";return 0;
    }catch(const std::exception&e){std::cerr<<"Now Playing lyrics sources failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
