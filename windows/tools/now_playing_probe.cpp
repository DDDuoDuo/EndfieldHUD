// Live acceptance probe for the Windows Now Playing services. It is a console
// tool for the USER to run on their own machine while a player of their choice
// is playing; automated tests never run its live modes (they would read the
// user's real media session and contact public lyric services).
//
//   now_playing_probe                      usage only; touches nothing
//   now_playing_probe --live [--seconds N] GSMTC discovery, metadata, timeline,
//                                          capabilities and thumbnail decode;
//                                          prints every event-driven change
//   now_playing_probe --live --lyrics      also the source lyric precedence
//                                          (embedded, NetEase catalog, LRCLIB)
//   now_playing_probe --live --command playPause|previous|next|seek=SECONDS
//                                          sends exactly one explicit command
//
// Output is JSON lines on stdout. Nothing is persisted; no player is launched.
#ifdef _WIN32
#include "native/now_playing_service.hpp"
#include "native/now_playing_image.hpp"
#include "native/now_playing_web.hpp"
#include "modules/now_playing_lyrics_sources.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>

namespace n=endfield::native;namespace m=endfield::modules;using Json=ehud::data::Json;
namespace {
struct Wake {
    std::mutex mutex;std::condition_variable signal;bool pending{};
    void post(){{std::lock_guard lock(mutex);pending=true;}signal.notify_one();}
    void wait(double seconds){std::unique_lock lock(mutex);signal.wait_for(lock,std::chrono::duration<double>(std::max(0.,seconds)),[&]{return pending;});pending=false;}
};
const auto origin=std::chrono::steady_clock::now();
double monotonic(){return std::chrono::duration<double>(std::chrono::steady_clock::now()-origin).count();}
void emit(Json value){value["t"]=std::round(monotonic()*1000)/1000;std::cout<<value.encode()<<std::endl;}
std::string failureName(n::NowPlayingServiceFailure f){
    switch(f){case n::NowPlayingServiceFailure::none:return "none";case n::NowPlayingServiceFailure::unavailable:return "unavailable";case n::NowPlayingServiceFailure::accessDenied:return "accessDenied";
    case n::NowPlayingServiceFailure::unsupported:return "unsupported";case n::NowPlayingServiceFailure::cancelled:return "cancelled";case n::NowPlayingServiceFailure::stale:return "stale";
    case n::NowPlayingServiceFailure::capacity:return "capacity";case n::NowPlayingServiceFailure::timedOut:return "timedOut";}return "unknown";
}
Json optionalNumber(std::optional<double>v){return v?Json(*v):Json();}
Json describe(const n::NowPlayingServiceSnapshot&s){
    Json::Object out{{"event","snapshot"},{"fresh",s.fresh},{"busy",s.busy},{"failure",failureName(s.failure)},{"nativeError",static_cast<std::int64_t>(s.nativeError)},{"sessions",static_cast<std::int64_t>(s.media.sessions.size())}};
    Json::Array ids;for(const auto&session:s.media.sessions)ids.push_back(session.appUserModelID);out["sessionIDs"]=std::move(ids);
    if(s.media.session){out["selected"]=s.media.session->appUserModelID;out["source"]=std::string(m::nowPlayingSourceKey(m::nowPlayingSourceForAppUserModelID(s.media.session->appUserModelID)));}
    if(const auto&t=s.media.track){out["track"]=Json::Object{{"title",t->title},{"artist",t->artist},{"album",t->album},{"duration",optionalNumber(t->duration)},{"position",optionalNumber(t->position)},
        {"playing",t->isPlaying},{"supportsSeeking",t->supportsSeeking},{"timedLyrics",t->timedLyrics.has_value()}};}
    const auto&c=s.media.capabilities;out["capabilities"]=Json::Object{{"play",c.play},{"pause",c.pause},{"toggle",c.toggle},{"previous",c.previous},{"next",c.next},{"seek",c.seek}};
    if(s.media.timeline)out["playbackRate"]=s.media.timeline->rate;
    out["thumbnailBytes"]=static_cast<std::int64_t>(s.media.artwork?s.media.artwork->size():0);
    return Json(std::move(out));
}
int usage(){
    std::cout<<"now_playing_probe --live [--seconds N] [--lyrics] [--command playPause|previous|next|seek=SECONDS]\n"
               "Reads the current Windows media session (GSMTC) for live acceptance. Run only on your own machine.\n";
    return 0;
}
}
int wmain(int argc,wchar_t**argv){
    bool live{},lyrics{};double seconds=15;std::optional<m::NowPlayingCommand>command;
    for(int k=1;k<argc;++k){
        const std::wstring arg=argv[k];
        if(arg==L"--live")live=true;else if(arg==L"--lyrics")lyrics=true;
        else if(arg==L"--seconds"&&k+1<argc)seconds=std::clamp(std::wcstod(argv[++k],nullptr),1.,600.);
        else if(arg==L"--command"&&k+1<argc){const std::wstring value=argv[++k];
            if(value==L"playPause")command=m::NowPlayingCommand{m::NowPlayingCommandKind::playPause};else if(value==L"previous")command=m::NowPlayingCommand{m::NowPlayingCommandKind::previous};
            else if(value==L"next")command=m::NowPlayingCommand{m::NowPlayingCommandKind::next};
            else if(value.rfind(L"seek=",0)==0)command=m::NowPlayingCommand{m::NowPlayingCommandKind::seek,std::wcstod(value.c_str()+5,nullptr)};
            else return usage();}
        else return usage();
    }
    if(!live)return usage();
    try{
        Wake wake;endfield::app::UtilityExecutor utility([&]{wake.post();});
        n::NativeNowPlayingService service(utility,n::windowsNowPlayingProvider(monotonic),[&]{wake.post();},monotonic);
        std::shared_ptr<n::NowPlayingWeb>web;std::unique_ptr<m::NowPlayingLyricsSources>sources;
        if(lyrics){web=std::make_shared<n::NowPlayingWeb>(n::windowsNowPlayingTransport(),[&]{wake.post();},monotonic);sources=std::make_unique<m::NowPlayingLyricsSources>(web->lyricsFetch(),true);}
        std::uint64_t revision{};std::size_t thumbnail{};bool commandSent{};const double end=monotonic()+seconds;
        std::shared_ptr<const m::NowPlayingLyrics>shownLyrics;std::optional<std::string>shownCover;
        service.setActive(true);emit(Json::Object{{"event","activated"}});
        while(monotonic()<end){
            double until=end;if(const auto due=service.nextWakeTime())until=std::min(until,*due);if(web)if(const auto due=web->nextWakeTime())until=std::min(until,*due);
            wake.wait(until-monotonic());utility.drain();service.drain();if(web)web->drain();
            const auto&s=service.snapshot();
            if(s.revision!=revision){revision=s.revision;emit(describe(s));
                if(s.media.artwork&&s.media.artwork->data()&&std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char*>(s.media.artwork->data()),s.media.artwork->size()))!=thumbnail){
                    thumbnail=std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char*>(s.media.artwork->data()),s.media.artwork->size()));
                    const auto image=n::decodeNowPlayingImage(*s.media.artwork);
                    emit(Json::Object{{"event","thumbnail"},{"decoded",bool(image.image)},{"width",image.image?static_cast<std::int64_t>(image.image->width):0},{"height",image.image?static_cast<std::int64_t>(image.image->height):0},{"nativeError",static_cast<std::int64_t>(image.nativeError)}});
                }
                if(s.commandCompleted)emit(Json::Object{{"event","commandCompleted"},{"token",static_cast<std::int64_t>(s.commandCompleted->request.token)},{"failure",failureName(s.commandCompleted->failure)}});
                if(command&&!commandSent&&s.fresh&&s.media.track){commandSent=true;const bool accepted=service.perform(*command);emit(Json::Object{{"event","commandSent"},{"accepted",accepted}});}
            }
            if(sources){
                std::optional<m::NowPlayingLyricsKey>key;if(s.fresh&&s.media.session&&s.media.track)key=m::NowPlayingLyricsKey::make(s.media.session->appUserModelID,*s.media.track);
                sources->update(key,s.media.track);
                const auto value=sources->lyrics();const auto cover=sources->catalogArtwork();
                if(value!=shownLyrics||cover!=shownCover){shownLyrics=value;shownCover=cover;
                    emit(Json::Object{{"event","lyrics"},{"lines",static_cast<std::int64_t>(value?value->lines().size():0)},{"embedded",bool(s.media.track&&s.media.track->timedLyrics)},
                        {"catalogLoading",sources->catalogLoading()},{"catalogCover",cover?Json(*cover):Json()},{"requests",static_cast<std::int64_t>(web->stats().started)},{"failed",static_cast<std::int64_t>(web->stats().failed)}});}
            }
        }
        if(sources)sources->hide();if(web)web->cancelAll();service.setActive(false);utility.shutdown();emit(Json::Object{{"event","stopped"}});return 0;
    }catch(const std::exception&e){std::cerr<<"now_playing_probe failed: "<<e.what()<<'\n';return 1;}
}
#endif
