// Source: python3 windows/tools/now_playing_lyrics_reference.py (unchanged Mac
// NowPlayingLyrics/NowPlayingCatalog/NowPlayingController lyric precedence).
// Synthetic metadata and injected in-memory responses only: no network.
#include "modules/now_playing_lyrics_sources.hpp"
#include "core/data/json.hpp"
#include <chrono>
#include <cmath>
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
// NetEase Cloud Music's own queue state file resolves the current track's
// exact song id and length (its player bar carries no time labels and its
// progress slider no accessible value). Sanitized synthetic queue only; no
// song metadata from the probe.
void playingListParser(){
    const auto track=[](std::string title,std::string artist){m::NowPlayingTrack t;t.title=std::move(title);t.artist=std::move(artist);return m::NowPlayingTrack::bounded(std::move(t));};
    const std::string queue=R"({"list":[
      {"id":"99","track":{"id":99,"name":"Other Song","artists":[{"name":"Example Artist"}],"album":{"name":"B","picUrl":"http://p2.music.126.net/Other==/99.jpg"},"duration":100000}},
      {"id":"2001","track":{"id":2001,"name":"Synthetic Song","artists":[{"name":"Example Artist"},{"name":"Guest"}],"album":{"name":"Fixture Album","picUrl":"http://p3.music.126.net/Queue==/2001.jpg"},"duration":233400}}
    ]})";
    const auto b=bytes(queue);
    const auto entry=m::nowPlayingNetEasePlayingListEntry(view(b),track("Synthetic Song","Example Artist"));
    check(entry&&entry->identifier==2001&&entry->duration&&std::abs(*entry->duration-233.4)<1e-6&&entry->artwork==std::optional<std::string>("https://p3.music.126.net/Queue==/2001.jpg?param=600y600"),
          "The queue entry matching the window title yields its song id, length and cover");
    const auto other=m::nowPlayingNetEasePlayingListEntry(view(b),track("Other Song","Example Artist"));
    check(other&&other->identifier==99&&other->duration&&std::abs(*other->duration-100.)<1e-6,"Another queued track resolves to its own id and length");
    check(!m::nowPlayingNetEasePlayingListEntry(view(b),track("Unqueued Song","Example Artist")),"A track not in the queue is no match");
    check(!m::nowPlayingNetEasePlayingListEntry(view(b),track("Synthetic Song","Different Artist")),"A different artist is no match (conservative identity)");
    // A track-id-less entry falls back to the numeric string entry id.
    const auto stringId=bytes(R"({"list":[{"id":"3003","track":{"name":"String Id Song","artists":[{"name":"Example Artist"}],"album":{"name":"Z"},"duration":60000}}]})");
    const auto s=m::nowPlayingNetEasePlayingListEntry(view(stringId),track("String Id Song","Example Artist"));
    check(s&&s->identifier==3003&&s->duration&&std::abs(*s->duration-60.)<1e-6&&!s->artwork,"A numeric string entry id is accepted; a missing cover is simply absent");
    // Two queued entries of the same title and artist but different ids: never guessed.
    const auto tie=bytes(R"({"list":[
      {"id":"11","track":{"id":11,"name":"Tie Song","artists":[{"name":"Artist"}],"duration":200000}},
      {"id":"22","track":{"id":22,"name":"Tie Song","artists":[{"name":"Artist"}],"duration":200000}}]})");
    check(!m::nowPlayingNetEasePlayingListEntry(view(tie),track("Tie Song","Artist")),"An ambiguous queue tie is no match (source tie rule)");
    check(!m::nowPlayingNetEasePlayingListEntry(view(bytes("not json")),track("Synthetic Song","Example Artist")),"A malformed queue file is no match");
    check(!m::nowPlayingNetEasePlayingListEntry(view(bytes(R"({"list":[]})")),track("Synthetic Song","Example Artist")),"An empty queue is no match");
}
// With a queue-file reader, a NetEase track reads its lyrics by the exact id
// and keeps the exact length, skipping the fuzzy title search; a track the
// queue does not carry falls back to the search.
void playingListCatalog(){
    struct Jobs {
        std::vector<std::pair<std::function<void()>,std::function<void()>>>queued;
        m::NowPlayingDecoder bind(){return [this](std::function<void()>work,std::function<void()>done){queued.emplace_back(std::move(work),std::move(done));};}
        void run(std::size_t k){auto job=std::move(queued.at(k));job.first();job.second();}
    };
    struct Net {
        std::vector<std::string>urls;std::vector<std::function<void(m::NowPlayingBody)>>done;
        m::NowPlayingFetch bind(){return [this](const std::string&url,std::function<void(m::NowPlayingBody)>completion){urls.push_back(url);done.push_back(std::move(completion));return std::function<void()>([]{});};}
    };
    const std::string queue=R"({"list":[{"id":"2001","track":{"id":2001,"name":"Synthetic Song","artists":[{"name":"Example Artist"}],"album":{"name":"Fixture Album","picUrl":"http://p3.music.126.net/Queue==/2001.jpg"},"duration":233400}}]})";
    m::NowPlayingTrack a;a.title="Synthetic Song";a.artist="Example Artist";a=m::NowPlayingTrack::bounded(std::move(a));
    m::NowPlayingTrack b;b.title="Unqueued Song";b.artist="Nobody";b=m::NowPlayingTrack::bounded(std::move(b));
    const auto ka=m::NowPlayingLyricsKey::make("cloudmusic.exe",a),kb=m::NowPlayingLyricsKey::make("cloudmusic.exe",b);
    Jobs jobs;Net net;m::NowPlayingCatalog catalog(net.bind(),jobs.bind());
    unsigned reads{};catalog.setPlayingList([&]()->m::NowPlayingBody{++reads;return bytes(queue);});
    catalog.request(ka,a);
    check(reads==1&&net.urls.empty()&&jobs.queued.size()==1,"A NetEase track parses the queue file before any network request");
    jobs.run(0);
    check(net.urls.size()==1&&net.urls[0]=="https://music.163.com/api/song/lyric?id=2001&lv=-1&kv=-1&tv=-1"&&catalog.duration()&&std::abs(*catalog.duration()-233.4)<1e-6,
          "The exact song id reads its own lyrics directly (no cloudsearch) and its length is kept");
    check(catalog.artwork()==std::optional<std::string>("https://p3.music.126.net/Queue==/2001.jpg?param=600y600"),"The queue cover is exposed as the catalog cover");
    net.done.at(0)(bytes(R"({"code":200,"lrc":{"lyric":"[00:01]Exact"}})"));jobs.run(1);
    check(!catalog.isLoading()&&catalog.lyrics()&&catalog.lyrics()->lines().front().text=="Exact","The by-id lyrics finish the entry");
    catalog.request(kb,b);jobs.run(jobs.queued.size()-1);
    check(net.urls.size()==2&&net.urls[1].find("/api/cloudsearch/pc?")!=std::string::npos,"A track the queue does not carry falls back to the title search");
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
// A long NetEase queue (the tester's: 1352 entries, 3.2 MB) is far beyond the
// 1 MiB lyric response bound it used to share, so it never parsed and every
// NetEase track fell back to the fuzzy title search (which misses some songs:
// no lyrics, no length). It is parsed within its own 16 MiB bound, once per
// contents. Synthetic queue only.
std::string longQueue(std::size_t rows,std::size_t target){
    std::string out=R"({"list":[)";
    for(std::size_t k=0;k<rows;++k){
        const bool wanted=k==target;const auto id=std::to_string(wanted?4242:100000+k);
        out+=(k?",":"")+std::string(R"({"id":")")+id+R"(","displayOrder":)"+std::to_string(k)+R"(,"track":{"id":)"+id+R"(,"name":")"+(wanted?std::string("Synthetic Song"):"Queued Song "+std::to_string(k))+
            R"(","artists":[{"name":"Example Artist","id":7}],"album":{"name":"Fixture Album","picUrl":"http://p3.music.126.net/Queue==/)"+id+R"(.jpg","cover":"http://p3.music.126.net/Queue==/)"+id+
            R"(.jpg"},"duration":)"+std::to_string(wanted?233400:180000+k)+R"(,"fee":8,"privilege":{"st":0,"pl":320000,"dl":0,"fl":320000}}})";
    }
    return out+"]}";
}
void longPlayingList(){
    m::NowPlayingTrack wanted;wanted.title="Synthetic Song";wanted.artist="Example Artist";wanted=m::NowPlayingTrack::bounded(std::move(wanted));
    const auto text=longQueue(6000,5990);const auto b=bytes(text);
    check(text.size()>m::nowPlayingMaximumLyricsResponseBytes&&text.size()<m::nowPlayingMaximumPlayingListBytes,"The synthetic queue is well beyond 1 MiB and within the queue file bound");
    const auto started=std::chrono::steady_clock::now();
    const auto queue=m::nowPlayingNetEaseQueue(view(b));
    const auto parsed=std::chrono::steady_clock::now();
    const auto entry=queue?m::nowPlayingNetEaseQueueEntry(*queue,wanted):std::nullopt;
    const auto matched=std::chrono::steady_clock::now();
    check(queue&&queue->rows.size()==6000,"A queue beyond 1 MiB is parsed (all 6000 rows)");
    check(entry&&entry->identifier==4242&&entry->duration&&std::abs(*entry->duration-233.4)<1e-6&&entry->artwork==std::optional<std::string>("https://p3.music.126.net/Queue==/4242.jpg?param=600y600"),
          "The current track near the end of a long queue resolves to its exact id, length and cover");
    check(m::nowPlayingNetEasePlayingListEntry(view(b),wanted)==entry,"The one-step lookup agrees with the parsed queue");
    std::cout<<"NetEase queue ("<<text.size()<<" bytes, 6000 rows): parse "<<std::chrono::duration<double,std::milli>(parsed-started).count()<<" ms, match "
        <<std::chrono::duration<double,std::milli>(matched-parsed).count()<<" ms\n";
    // The catalog parses the file once and matches every later track against it.
    struct Net {
        std::vector<std::string>urls;std::vector<std::function<void(m::NowPlayingBody)>>done;
        m::NowPlayingFetch bind(){return [this](const std::string&url,std::function<void(m::NowPlayingBody)>completion){urls.push_back(url);done.push_back(std::move(completion));return std::function<void()>([]{});};}
    };
    Net net;m::NowPlayingCatalog catalog(net.bind());unsigned reads{};
    catalog.setPlayingList([&]()->m::NowPlayingBody{++reads;return b;});
    catalog.request(m::NowPlayingLyricsKey::make("cloudmusic.exe",wanted),wanted);
    check(net.urls.size()==1&&net.urls[0]=="https://music.163.com/api/song/lyric?id=4242&lv=-1&kv=-1&tv=-1"&&catalog.duration()&&std::abs(*catalog.duration()-233.4)<1e-6,
          "A NetEase track in a long queue reads its own lyrics by id (no cloudsearch) and keeps the exact length");
    m::NowPlayingTrack next;next.title="Queued Song 17";next.artist="Example Artist";next=m::NowPlayingTrack::bounded(std::move(next));
    catalog.request(m::NowPlayingLyricsKey::make("cloudmusic.exe",next),next);
    check(net.urls.size()==2&&net.urls[1]=="https://music.163.com/api/song/lyric?id=100017&lv=-1&kv=-1&tv=-1"&&catalog.queueParses()==1&&reads==2,
          "The next track is matched against the same parsed queue (one parse per file contents)");
    const auto changed=bytes(longQueue(10,3));
    catalog.setPlayingList([&]()->m::NowPlayingBody{return changed;});
    m::NowPlayingTrack third;third.title="Queued Song 4";third.artist="Example Artist";third=m::NowPlayingTrack::bounded(std::move(third));
    catalog.request(m::NowPlayingLyricsKey::make("cloudmusic.exe",third),third);
    check(catalog.queueParses()==2&&net.urls.back()=="https://music.163.com/api/song/lyric?id=100004&lv=-1&kv=-1&tv=-1","A rewritten queue file is parsed again");
    // The list is read entry by entry: other members (with brackets and quotes
    // in their strings) are skipped, a damaged entry is skipped, a byte order
    // mark is accepted, and a broken file is no queue.
    const auto quoted=bytes("\xef\xbb\xbf { \"meta\" : {\"note\":\"a ] b } \\\" [list\", \"x\":[1,{\"y\":\"]\"}]}, \"list\" : [ "
        "{\"id\":\"1\",\"track\":{\"id\":1,\"name\":\"Brace } Song\",\"artists\":[{\"name\":\"Example Artist\"}],\"duration\":60000}} , "
        "{\"id\":2,\"track\":{\"name\":\"Broken\",\"duration\":} } , "
        "{\"id\":\"3\",\"track\":{\"id\":3,\"name\":\"Synthetic Song\",\"artists\":[{\"name\":\"Example Artist\"}],\"duration\":233400}} ] , \"after\": null }");
    const auto parsedQuoted=m::nowPlayingNetEaseQueue(view(quoted));
    check(parsedQuoted&&parsedQuoted->rows.size()==2&&parsedQuoted->rows[0].title=="Brace } Song"&&m::nowPlayingNetEaseQueueEntry(*parsedQuoted,wanted)->identifier==3,
          "Strings with brackets and quotes, a damaged entry and a byte order mark are handled entry by entry");
    for(const auto broken:{R"({"list":[{"track":{}})",R"({"list":[{"track":{}}] "x":1})",R"(["list"])",R"({"other":[]})","",R"({"list":"no"})"}){
        const auto b2=bytes(broken);check(!m::nowPlayingNetEaseQueue(view(b2)),std::string("A file without a well-formed list is no queue: ")+broken);
    }
    const auto empty=bytes(R"({"list":[]})");check(m::nowPlayingNetEaseQueue(view(empty))&&m::nowPlayingNetEaseQueue(view(empty))->rows.empty(),"An empty list is an empty queue");
    // More values than one JSON document may hold (1,000,000): each entry is
    // its own document, so a long queue with rich entries still resolves.
    std::string rich=R"({"list":[)";std::string privilege=R"("privilege":{)";
    for(int k=0;k<80;++k)privilege+=(k?",":"")+std::string("\"f")+std::to_string(k)+"\":"+std::to_string(k);
    privilege+="}";
    for(int k=0;k<12000;++k)rich+=(k?",":"")+std::string(R"({"id":")")+std::to_string(k+1)+R"(","track":{"id":)"+std::to_string(k+1)+R"(,"name":")"+(k==11000?std::string("Synthetic Song"):"Rich "+std::to_string(k))+
        R"(","artists":[{"name":"Example Artist"}],"duration":200000,)"+privilege+"}}";
    rich+="]}";
    const auto richBytes=bytes(rich);
    check(rich.size()<m::nowPlayingMaximumPlayingListBytes,"The rich queue stays within the queue file bound");
    bool whole{};try{(void)Json::parse(rich,m::nowPlayingMaximumPlayingListBytes);whole=true;}catch(...){}
    const auto richQueue=m::nowPlayingNetEaseQueue(view(richBytes));
    check(!whole&&richQueue&&richQueue->rows.size()==12000&&m::nowPlayingNetEaseQueueEntry(*richQueue,wanted)->identifier==11001,
          "A queue beyond the one-document value bound resolves the current track entry by entry");
}
// NetEase stores some lines as [mm:ss:cc]; read as hours they never show
// (60 times too late). Only NetEase catalog lyrics are read that way.
void neteaseStamps(){
    const auto b=bytes(R"({"code":200,"lrc":{"version":3,"lyric":"[00:00.00] 作词 : A\n[00:00.58] 作曲 : A\n[00:01:16]「ばぁ。」\n[00:13:04]例えば\n[01:02:345]Three\n[02:03.40]Four"}})");
    const auto lyrics=m::nowPlayingCatalogDecodeLyrics(view(b));
    check(lyrics&&lyrics->lines().size()==6,"Every NetEase line is kept");
    const double expected[]{0,.58,1.16,13.04,62.345,123.4};
    bool same=lyrics.has_value();for(std::size_t k=0;same&&k<6;++k)same=std::abs(lyrics->lines()[k].time-expected[k])<1e-9;
    check(same&&lyrics->lines()[3].text=="例えば","[mm:ss:cc] reads as minutes, seconds and hundredths (as NetEase shows it)");
    const auto other=m::NowPlayingLyrics::parse("[00:01:16] x");
    check(other&&std::abs(other->lines().front().time-76)<1e-9,"Other lyrics keep the source reading of three fields");
}
void bodies(){
    const auto answered=[](std::string_view t){const auto b=bytes(t);return m::nowPlayingCatalogAnswered(view(b));};
    check(answered(R"({"code":200,"nolyric":true})")&&!answered(R"({"code":-460,"message":"Cheating"})")&&!answered(R"({"code":405,"msg":"rate"})")&&!answered("<html>")&&!answered("{}"),
          "Only a code 200 answer is a catalog answer; refusals and damaged bodies are failures");
    const auto keep=[](std::string_view url,std::string_view t){const auto b=bytes(t);return m::nowPlayingLyricsBodyCacheable(url,view(b));};
    const std::string lyric="https://music.163.com/api/song/lyric?id=1&lv=-1&kv=-1&tv=-1",search="https://music.163.com/api/cloudsearch/pc?s=x&type=1&limit=10&offset=0";
    check(keep(lyric,R"({"code":200,"lrc":{"lyric":"[00:01]x"}})")&&!keep(lyric,R"({"code":200,"nolyric":true})")&&!keep(lyric,R"({"code":-460,"msg":"Cheating"})")&&!keep(lyric,"garbage"),
          "A lyric document is kept on disk only with lyric text");
    check(keep(search,R"({"code":200,"result":{"songs":[{"id":1}]}})")&&!keep(search,R"({"code":200,"result":{"songs":[]}})")&&!keep(search,R"({"code":405})"),
          "A catalog search is kept only with songs");
    check(keep("https://lrclib.net/api/get?track_name=a&artist_name=b",R"({"id":3,"syncedLyrics":"[00:01]x"})")&&keep("https://lrclib.net/api/search?track_name=a&artist_name=b",R"([{"id":3}])")&&
          !keep("https://lrclib.net/api/search?track_name=a&artist_name=b","[]")&&!keep("https://example.com/api/get",R"({"id":1})"),
          "LRCLIB records and non-empty searches are kept; an empty search is asked again next time");
}
// A lookup that got no answer is not "no lyrics": with the owner's clock it is
// retried after 5, 15, 45, 135 s and then every 300 s; an answer (with or
// without lyrics) is final; hiding forgets failures. Without a clock the
// source keeps its negative result.
void retries(){
    check(m::nowPlayingRetryDelay(1)==5&&m::nowPlayingRetryDelay(2)==15&&m::nowPlayingRetryDelay(3)==45&&m::nowPlayingRetryDelay(4)==135&&
          m::nowPlayingRetryDelay(5)==300&&m::nowPlayingRetryDelay(40)==300,"Backoff 5, 15, 45, 135, then 300 s");
    struct Net {
        std::vector<std::string>urls;std::vector<std::function<void(m::NowPlayingBody)>>done;
        m::NowPlayingFetch bind(){return [this](const std::string&url,std::function<void(m::NowPlayingBody)>completion){urls.push_back(url);done.push_back(std::move(completion));return std::function<void()>([]{});};}
    };
    const std::string search=R"({"code":200,"result":{"songs":[{"id":1901371647,"name":"Synthetic Song","ar":[{"name":"Example Artist"}],"al":{"name":"Fixture Album","picUrl":"http://p1.music.126.net/AbC==/1099.jpg"},"dt":233400}]}})";
    m::NowPlayingTrack a;a.title="Synthetic Song";a.artist="Example Artist";a=m::NowPlayingTrack::bounded(std::move(a));
    const auto key=m::NowPlayingLyricsKey::make("cloudmusic.exe",a);
    double now=100;Net net;m::NowPlayingCatalog catalog(net.bind());catalog.setClock([&]{return now;});
    catalog.request(key,a);net.done.at(0)(nullptr);
    check(!catalog.isLoading()&&!catalog.lyrics()&&catalog.retryTime()==105.,"A search with no answer finishes without lyrics and is retried after 5 s");
    now=104;catalog.request(key,a);check(net.urls.size()==1,"Before its wait passed the failure is not asked again");
    now=105;catalog.request(key,a);check(net.urls.size()==2&&catalog.isLoading()&&!catalog.retryTime(),"Once the wait passed the same track asks again");
    net.done.at(1)(bytes(R"({"code":-460,"message":"Cheating"})"));
    check(!catalog.isLoading()&&catalog.retryTime()==120.,"A refusal is a failure too, retried after 15 s");
    now=120;catalog.request(key,a);net.done.at(2)(bytes(search));
    check(net.urls.size()==4&&net.urls[3].find("/api/song/lyric?id=1901371647")!=std::string::npos,"The retried search matches and asks for the song's lyrics");
    net.done.at(3)(nullptr);
    check(!catalog.isLoading()&&catalog.retryTime()==165.&&catalog.artwork()&&catalog.duration()==233.4,"A failed lyric document keeps the match's cover and length; retried after 45 s");
    now=165;catalog.request(key,a);
    check(catalog.isLoading()&&catalog.artwork()&&catalog.duration()==233.4,"While retrying, the known cover and length stay");
    net.done.at(4)(bytes(search));net.done.at(5)(bytes(R"({"code":200,"lrc":{"lyric":"[00:01]Found"}})"));
    check(!catalog.isLoading()&&catalog.lyrics()&&catalog.lyrics()->lines().front().text=="Found"&&!catalog.retryTime(),"The retry's answer finishes the entry with lyrics");
    now=10000;catalog.request(key,a);check(net.urls.size()==6,"A finished answer is never asked again");
    // A definitive "nothing" (an answer without a match) is not retried.
    m::NowPlayingTrack b;b.title="Other Song";b.artist="Example Artist";b=m::NowPlayingTrack::bounded(std::move(b));const auto other=m::NowPlayingLyricsKey::make("cloudmusic.exe",b);
    catalog.request(other,b);net.done.at(6)(bytes(R"({"code":200,"result":{"songs":[],"songCount":0}})"));
    check(!catalog.isLoading()&&!catalog.lyrics()&&!catalog.retryTime(),"An answer without a match is final");
    now=20000;catalog.request(other,b);check(net.urls.size()==7,"and never asked again");
    // Hiding forgets failures: the next opening asks at once.
    m::NowPlayingTrack c;c.title="Third Song";c.artist="Example Artist";c=m::NowPlayingTrack::bounded(std::move(c));const auto third=m::NowPlayingLyricsKey::make("cloudmusic.exe",c);
    catalog.request(third,c);net.done.at(7)(nullptr);check(catalog.retryTime()==20005.,"A third track fails");
    catalog.clear();catalog.forgetFailures();catalog.request(third,c);check(net.urls.size()==9,"After hiding, the failed track asks again at once");
    // Without a clock (the source): the failure is the cached negative.
    Net plain;m::NowPlayingCatalog source(plain.bind());source.request(key,a);plain.done.at(0)(nullptr);source.request(key,a);
    check(!source.retryTime()&&plain.urls.size()==1,"Without a clock a failure stays the source negative");
    // LRCLIB: a search with no answer is retried the same way.
    m::NowPlayingTrack s;s.title="Synthetic Song";s.artist="Example Artist";s=m::NowPlayingTrack::bounded(std::move(s));const auto spotify=m::NowPlayingLyricsKey::make("Spotify.exe",s);
    Net lr;m::NowPlayingLyricsLoader loader(lr.bind());loader.setClock([&]{return now;});now=500;
    loader.request(spotify,s);lr.done.at(0)(nullptr);lr.done.at(1)(nullptr);
    check(lr.urls.size()==2&&!loader.lyrics()&&loader.retryTime()==505.,"LRCLIB: no answer to the search is retried after 5 s");
    now=505;loader.request(spotify,s);check(lr.urls.size()==3&&lr.urls[2].rfind("https://lrclib.net/api/get?",0)==0,"and starts over with the get");
    lr.done.at(2)(bytes(R"({"id":1,"syncedLyrics":"[00:01]Lrclib"})"));check(loader.lyrics()&&!loader.retryTime(),"Its answer ends the retries");
    // Sources: a retried catalog lookup keeps LRCLIB's lyrics on screen meanwhile.
    Net both;m::NowPlayingLyricsSources sources(both.bind());sources.setClock([&]{return now;});now=1000;
    sources.update(key,a);both.done.at(0)(nullptr);
    check(both.urls.size()==2&&both.urls[1].rfind("https://lrclib.net/api/get?",0)==0,"A failed NetEase lookup falls back to LRCLIB at once");
    both.done.at(1)(bytes(R"({"id":2,"syncedLyrics":"[00:01]Fallback"})"));
    check(sources.lyrics()&&sources.lyrics()->lines().front().text=="Fallback"&&sources.retryTime()==1005.,"LRCLIB's lyrics show while the catalog waits for its retry");
    now=1005;sources.update(key,a);
    check(both.urls.size()==3&&sources.catalogLoading()&&sources.lyrics()&&sources.lyrics()->lines().front().text=="Fallback","The catalog retries without hiding the lyrics already shown");
    both.done.at(2)(bytes(search));both.done.at(3)(bytes(R"({"code":200,"lrc":{"lyric":"[00:01]Catalog"}})"));
    check(sources.lyrics()&&sources.lyrics()->lines().front().text=="Catalog"&&!sources.retryTime(),"The catalog's own lyrics then take precedence");
    sources.hide();check(!sources.retryTime(),"Hidden: no retry is scheduled");
}
int main(int argc,char**argv){
    try{
        check(argc==2,"Pass now-playing-lyrics-source.json");std::ifstream in(argv[1],std::ios::binary);check(bool(in),"Read original lyric source fixture");
        const auto j=Json::parse(std::string((std::istreambuf_iterator<char>(in)),{}),8*1024*1024);
        check(j["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Lyric source fixture is pinned to the authority commit");
        matcher(j);urls(j);decoders(j);scenarios(j);windowsSources();deferredDecoding();playingListParser();playingListCatalog();decodeCost();
        longPlayingList();neteaseStamps();bodies();retries();
        std::cout<<"Now Playing lyrics sources: "<<checks<<" checks passed (original Swift oracle, injected responses only)\n";return 0;
    }catch(const std::exception&e){std::cerr<<"Now Playing lyrics sources failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
