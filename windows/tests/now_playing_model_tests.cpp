#include "modules/now_playing_model.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <tuple>
namespace {std::atomic<std::uint64_t>allocations{};}
void*operator new(std::size_t n){allocations.fetch_add(1,std::memory_order_relaxed);if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{::operator delete(p);}
#ifdef __cpp_sized_deallocation
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace m=endfield::modules;using J=ehud::data::Json;
namespace {
unsigned checks{};void check(bool condition,const char*why){++checks;if(!condition)throw std::runtime_error(why);}
bool closeEnough(double a,double b){return std::abs(a-b)<=2e-12*std::max({1.,std::abs(a),std::abs(b)});}
double number(const J&j){if(j==J("nan"))return NAN;if(j==J("inf"))return INFINITY;if(j==J("-inf"))return -INFINITY;return j.number();}
std::optional<double>optionalNumber(const J&j){return j.isNull()?std::nullopt:std::optional(number(j));}
std::optional<std::string>optionalString(const J&j){return j.isString()?std::optional(j.string()):std::nullopt;}
void same(std::optional<double>a,const J&b){check(bool(a)==!b.isNull()&&(!a||closeEnough(*a,number(b))),"Numeric state matches unchanged original Swift");}
m::NowPlayingTrack track(const J&j){m::NowPlayingTrack t;t.title=j["title"].string();t.artist=j["artist"].string();t.album=j["album"].string();t.duration=optionalNumber(j["duration"]);t.position=optionalNumber(j["position"]);t.isPlaying=j["playing"].isBool()&&j["playing"].boolean();t.sampledAt=j["sampledAt"].isNull()?0:number(j["sampledAt"]);t.identifier=optionalString(j["identifier"]);t.timedLyrics=optionalString(j["lyrics"]);t.artworkRevision=optionalString(j["artworkRevision"]);if(j["supportsSeeking"].isBool())t.supportsSeeking=j["supportsSeeking"].boolean();return m::NowPlayingTrack::bounded(std::move(t));}
void source(const char*path){std::ifstream input(path);check(bool(input),"Read explicit immutable source fixture");const std::string bytes((std::istreambuf_iterator<char>(input)),{});const auto fixture=J::parse(bytes,256*1024);check(fixture["sourceCommit"]==J("ca04f142185c7de40acd8523bdb563195d90a1d1"),"Source authority is pinned");
    for(const auto&row:fixture["tracks"].array()){const auto t=track(row["input"]);const auto&e=row["expected"];check(t.title==e["title"].string()&&t.artist==e["artist"].string()&&t.album==e["album"].string(),"Swift grapheme truncation preserves CJK, combining marks and ZWJ emoji");same(t.duration,e["duration"]);same(t.position,e["position"]);same(t.sampledAt,e["sampledAt"]);check(t.identifier==optionalString(e["identifier"])&&t.artworkRevision==optionalString(e["artworkRevision"]),"Source optional identifiers and revision bounds match");
        for(std::size_t k=0;k<row["times"].array().size();++k){const auto now=number(row["times"].array()[k]);same(t.elapsed(now),e["elapsed"].array()[k]);check(m::nowPlayingTime(now)==e["formatted"].array()[k].string(),"Time formatting uses source floor, upper bound and unavailable label");}}
    for(const auto&row:fixture["lyrics"].array()){const auto l=m::NowPlayingLyrics::parse(row["input"].string());const auto&e=row["expected"];check(bool(l)==!e.isNull(),"LRC acceptance matches original Foundation parser");if(!l)continue;check(l->lines().size()==e["lines"].array().size(),"Source LRC line count and duplicate merging match");for(std::size_t k=0;k<l->lines().size();++k){same(l->lines()[k].time,e["lines"].array()[k]["time"]);check(l->lines()[k].text==e["lines"].array()[k]["text"].string(),"Source Unicode timestamp grammar, trimming and duplicate text match");}
        for(std::size_t k=0;k<row["times"].array().size();++k){const auto now=number(row["times"].array()[k]);const auto window=l->window(now);for(std::size_t q=0;q<3;++q)check(window[q]==e["windows"].array()[k].array()[q].string(),"Source three-row cue window matches");same(l->nextBoundary(now),e["boundaries"].array()[k]);}}
    for(const auto&row:fixture["identities"].array())check(track(row["a"]).sameIdentity(track(row["b"]))==row["expected"].boolean(),"Track identity ignores progress but preserves canonical text and identifiers");
    for(const auto&row:fixture["seek"].array())check(m::nowPlayingAcknowledgesSeek(track(row["track"]),number(row["requested"]),number(row["sampledAt"]))==row["expected"].boolean(),"Seek acknowledgement matches original tolerance and capped advancement");
    for(const auto&row:fixture["deadlines"].array()){const auto lyrics=row["lyrics"].isString()?m::NowPlayingLyrics::parse(row["lyrics"].string()):std::nullopt;same(m::nowPlayingDisplayDeadline(track(row["track"]),number(row["now"]),!row["active"].isBool()||row["active"].boolean(),lyrics?&*lyrics:nullptr,!row["lyricsVisible"].isBool()||row["lyricsVisible"].boolean(),row["seeking"].isBool()&&row["seeking"].boolean()),row["expected"]);}
}
void bounds(){check(!m::NowPlayingLyrics::parse(std::string(m::nowPlayingMaximumLyricsBytes+1,'a')),"Oversize lyrics reject before regex or decoding");check(!m::NowPlayingLyrics::parse(std::string("[00:01]\xff",8)),"Malformed UTF8 never reaches the Unicode parser");
    std::string lrc;for(unsigned k=0;k<4100;++k)lrc+="["+std::to_string(k/60)+":"+std::to_string(k%60)+"]Row\n";const auto many=m::NowPlayingLyrics::parse(lrc);check(many&&many->lines().size()==4096,"Original parsed-line bound remains 4096");
    auto t=m::NowPlayingTrack::bounded({"Song","Artist","Album",100,10,true,1,{},{},{},true});t.timedLyrics=std::string(m::nowPlayingMaximumLyricsBytes+1,'x');t=m::NowPlayingTrack::bounded(std::move(t));check(!t.timedLyrics,"Source metadata drops oversized embedded lyrics");
    for(const auto [w,h,b,expected]:std::array<std::tuple<unsigned,unsigned,std::size_t,bool>,7>{{{512,512,1024,true},{0,10,1,false},{8193,1,1,false},{8192,8192,1,false},{8000,4000,m::nowPlayingMaximumArtworkBytes,true},{1,1,m::nowPlayingMaximumArtworkBytes+1,false},{1,1,0,false}}})check(m::nowPlayingArtworkMetadata(w,h,b)==expected,"Artwork metadata uses original dimensions/pixels/encoded byte limits");
    m::NowPlayingCapabilities cap;check(!m::nowPlayingCommand(t,cap,{m::NowPlayingCommandKind::playPause}),"No advertised capability never sends speculative transport");check(!cap.volume&&!cap.lyrics&&!cap.activateApplication,"Unsupported GSMTC capabilities remain explicit");cap.pause=true;check(bool(m::nowPlayingCommand(t,cap,{m::NowPlayingCommandKind::playPause})),"Playing track can use supported pause when toggle is absent");t.isPlaying=false;check(!m::nowPlayingCommand(t,cap,{m::NowPlayingCommandKind::playPause}),"Pause support cannot be guessed into play support");cap.toggle=true;check(bool(m::nowPlayingCommand(t,cap,{m::NowPlayingCommandKind::playPause})),"Advertised toggle is valid for paused track");cap.seek=true;check(m::nowPlayingCommand(t,cap,{m::NowPlayingCommandKind::seek,1000})->seconds==100,"Seek clamps to source track duration");check(!m::nowPlayingCommand(t,cap,{m::NowPlayingCommandKind::seek,NAN}),"Nonfinite seek never reaches an OS boundary");t.supportsSeeking=false;check(!m::nowPlayingCommand(t,cap,{m::NowPlayingCommandKind::seek,5}),"Native seeking capability does not override source track restriction");
    const auto lyrics=m::NowPlayingLyrics::parse("[00:01] One\n[00:10.5] 二行\n[00:20] Third");t.isPlaying=true;const auto before=allocations.load();double total{};for(unsigned k=0;k<10000;++k){const auto now=double(k)/1000;const auto elapsed=t.elapsed(now);const auto rows=lyrics->window(*elapsed);const auto deadline=m::nowPlayingDisplayDeadline(t,now,true,&*lyrics,true,false);total+=double(rows[1].size())+deadline.value_or(0);}check(allocations.load()==before&&total>0,"Warm progress and lyric deadlines allocate nothing and perform no provider read");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass the immutable original-Swift fixture path");source(argv[1]);bounds();std::cout<<"Now Playing model: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<"Now Playing model failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
