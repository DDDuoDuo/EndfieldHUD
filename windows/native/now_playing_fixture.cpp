#include "native/now_playing_fixture.hpp"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace endfield::native {
namespace {
constexpr double fixtureDuration=210;
constexpr std::int64_t ticks=10000000;
void put16(std::vector<std::uint8_t>&b,std::uint16_t v){b.push_back(static_cast<std::uint8_t>(v));b.push_back(static_cast<std::uint8_t>(v>>8));}
void put32(std::vector<std::uint8_t>&b,std::uint32_t v){for(unsigned k=0;k<4;++k)b.push_back(static_cast<std::uint8_t>(v>>(8*k)));}
std::uint8_t channel(double v){return static_cast<std::uint8_t>(std::lround(std::clamp(v,0.,1.)*255));}
struct State {
    std::mutex mutex;bool playing{};double position{42},sampledAt{};unsigned track{1};
    double current(double now)const{return std::min(fixtureDuration,position+(playing?std::max(0.,now-sampledAt):0.));}
};
class FixtureProvider final:public NowPlayingProvider {
public:
    FixtureProvider(std::function<double()>clock,std::shared_ptr<State>state):now_(std::move(clock)),state_(std::move(state)){}
    void start(Changed changed,Ready ready)override{{std::lock_guard lock(mutex_);if(stopped_)return;changed_=std::move(changed);}ready({});}
    void read(Read completion)override{
        NowPlayingReadResult result;const auto time=now_();
        modules::NowPlayingSession session{1,"EndfieldHUD.Fixture","Music"};result.value.sessions={session};result.value.session=session;
        modules::NowPlayingTrack track;{std::lock_guard lock(state_->mutex);
            track.title="EndfieldHUD Fixture "+std::to_string(state_->track);track.position=state_->current(time);track.isPlaying=state_->playing;
            result.value.metadataRevision=state_->track;}
        track.artist="Local verification";track.album="Now Playing";track.duration=fixtureDuration;track.sampledAt=time;
        track.timedLyrics="[00:00]Previous\n[00:30]Current\n[01:00]Next";
        NowPlayingTimeline timeline;timeline.start=0;timeline.end=static_cast<std::int64_t>(fixtureDuration*ticks);timeline.position=static_cast<std::int64_t>(std::llround(*track.position*ticks));
        timeline.minimumSeek=0;timeline.maximumSeek=timeline.end;timeline.rate=1;timeline.sampledAt=time;timeline.playing=track.isPlaying;
        result.value.track=std::move(track);result.value.timeline=timeline;result.value.capabilities={true,true,true,true,true,true};result.value.artwork=nowPlayingFixtureCover();
        {std::lock_guard lock(mutex_);if(stopped_)return;}completion(std::move(result));
    }
    void perform(std::uint64_t session,std::uint64_t,modules::NowPlayingCommand command,Ready ready)override{
        if(session!=1){ready({NowPlayingServiceFailure::stale,0});return;}
        {std::lock_guard lock(state_->mutex);const auto time=now_();state_->position=state_->current(time);state_->sampledAt=time;
            switch(command.kind){
            case modules::NowPlayingCommandKind::playPause:state_->playing=!state_->playing;break;
            case modules::NowPlayingCommandKind::previous:state_->track=std::max(1u,state_->track-1);state_->position=0;break;
            case modules::NowPlayingCommandKind::next:++state_->track;state_->position=0;break;
            case modules::NowPlayingCommandKind::seek:state_->position=std::clamp(command.seconds,0.,fixtureDuration);break;}}
        Changed changed;{std::lock_guard lock(mutex_);if(stopped_)return;changed=changed_;}
        ready({});if(changed)changed();
    }
    void stop()noexcept override{std::lock_guard lock(mutex_);stopped_=true;changed_=nullptr;}
private:
    std::function<double()>now_;std::shared_ptr<State>state_;std::mutex mutex_;Changed changed_;bool stopped_{};
};
}
std::shared_ptr<const std::vector<std::uint8_t>>nowPlayingFixtureCover(){
    static const auto cover=[]{
        // Source fixturePNG geometry (CoreGraphics bottom-left origin): fill
        // (0.09,0.16,0.22), left 24-point gold bar, 5-point light ring in
        // (33,21,47,47). Coverage is supersampled 4x4 for a smooth edge.
        constexpr int size=96;std::vector<std::uint8_t>b;b.reserve(54+size*size*4);
        b.push_back('B');b.push_back('M');put32(b,54+size*size*4);put32(b,0);put32(b,54);
        put32(b,40);put32(b,size);put32(b,size);put16(b,1);put16(b,32);put32(b,0);put32(b,size*size*4);put32(b,2835);put32(b,2835);put32(b,0);put32(b,0);
        for(int row=0;row<size;++row)for(int x=0;x<size;++x){ // BMP rows are bottom-up, matching CoreGraphics y
            double r=.09,g=.16,bl=.22;if(x<24){r=.85;g=.70;bl=.22;}
            int inside{};for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){const double px=x+(sx+.5)/4,py=row+(sy+.5)/4;const double d=std::hypot(px-56.5,py-44.5);if(std::abs(d-23.5)<=2.5)++inside;}
            const double a=inside/16.;r=r*(1-a)+.95*a;g=g*(1-a)+.95*a;bl=bl*(1-a)+.95*a;
            b.push_back(channel(bl));b.push_back(channel(g));b.push_back(channel(r));b.push_back(255);
        }
        return std::make_shared<const std::vector<std::uint8_t>>(std::move(b));
    }();
    return cover;
}
NativeNowPlayingService::Factory nowPlayingFixtureProvider(std::function<double()>now){
    if(!now)throw std::invalid_argument("Now Playing fixture needs the host monotonic clock");
    // One synthetic player for the factory's lifetime: state survives conceal
    // and reopen exactly like the source controller-owned fixture backend.
    auto state=std::make_shared<State>();state->sampledAt=now();
    return [now=std::move(now),state]{return std::make_shared<FixtureProvider>(now,state);};
}
}
