#include <algorithm>
#include <cmath>
// The production playing-app volume bridge (native::nowPlayingAppVolume over
// the shared AudioRuntime) end to end with the Now Playing owner state: the
// fixture player's session resolves the ONE per-app route whose worker-
// captured AppUserModelID matches, the volume slider writes relative
// attenuation through the runtime's worker into the injected session backend,
// and the confirmed row returns through the runtime publication (the host's
// audioChanged broadcast). The same adapter over NowPlayingAudioFixture is the
// development-preview/hidden-coverage route. Injected endpoint/session
// backends and a synthetic player only: no audio device, mixer level,
// process, media session or user file is touched.
#include "native/audio_runtime.hpp"
#include "native/now_playing_fixture.hpp"
#include "native/now_playing_volume.hpp"
#include "tools/now_playing_session.hpp"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace {
namespace n=endfield::native;namespace m=endfield::modules;namespace t=endfield::tools;namespace c=endfield::core;
using Q=endfield::app::UtilityExecutor;using namespace std::chrono_literals;
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}
const std::wstring speaker=L"{0.0.0.00000000}.{speaker}";

struct World {
    std::mutex m;
    std::vector<n::AudioSessionRecord>sessions;
    n::AudioSessionBackend::Wake sessionWake;
    unsigned sessionsCreated{},writes{};bool endpointOpen{};std::wstring bound;
};
class Endpoints final:public n::AudioEndpointBackend {
    std::shared_ptr<World>w;
public:
    explicit Endpoints(std::shared_ptr<World>world):w(std::move(world)){}
    std::int32_t open(Wake)override{std::lock_guard l(w->m);w->endpointOpen=true;return 0;}
    void close()noexcept override{std::lock_guard l(w->m);w->endpointOpen=false;w->bound.clear();}
    std::int32_t enumerate(n::AudioFlow flow,std::vector<n::AudioEndpointRecord>&out)override{
        out.clear();if(flow==n::AudioFlow::output)out.push_back({speaker,L"Speakers",L"HDAUDIO",1u});return 0;}
    std::int32_t defaultEndpoint(n::AudioFlow flow,std::wstring&out)override{
        out=flow==n::AudioFlow::output?speaker:std::wstring();return out.empty()?static_cast<std::int32_t>(0x80070490u):0;}
    std::int32_t bind(std::wstring_view id,Wake)override{std::lock_guard l(w->m);w->bound=id;return 0;}
    std::int32_t read(n::AudioEndpointVolumeState&v)override{v={};v.scalar=.5f;v.channels=1;v.minimumDecibels=-65;v.maximumDecibels=0;v.incrementDecibels=.5f;v.steps=100;return 0;}
    std::int32_t setVolume(float)override{return -1;}
    std::int32_t setMute(bool)override{return -1;}
    std::int32_t readChannel(unsigned,float&)override{return -1;}
    std::int32_t writeChannel(unsigned,float)override{return -1;}
};
class Sessions final:public n::AudioSessionBackend {
    std::shared_ptr<World>w;
    n::AudioSessionRecord*find(std::string_view id){for(auto&r:w->sessions)if(r.id==id)return &r;return nullptr;}
public:
    explicit Sessions(std::shared_ptr<World>world):w(std::move(world)){std::lock_guard l(w->m);++w->sessionsCreated;}
    std::int32_t open(std::wstring_view,Wake wake)override{std::lock_guard l(w->m);w->sessionWake=std::move(wake);return 0;}
    std::int32_t resume(Wake wake)override{std::lock_guard l(w->m);w->sessionWake=std::move(wake);return 0;}
    void pause()noexcept override{std::lock_guard l(w->m);w->sessionWake={};}
    std::int32_t read(std::vector<n::AudioSessionRecord>&out)override{std::lock_guard l(w->m);out=w->sessions;return 0;}
    std::int32_t readVolume(std::string_view id,float&v)override{std::lock_guard l(w->m);const auto*r=find(id);if(!r||!r->volume)return -98;v=*r->volume;return 0;}
    std::int32_t writeVolume(std::string_view id,float v)override{std::lock_guard l(w->m);auto*r=find(id);if(!r)return -98;r->volume=v;++w->writes;return 0;}
    void close()noexcept override{std::lock_guard l(w->m);w->sessionWake={};}
};
n::AudioSessionRecord record(std::string id,std::string process,std::uint32_t pid,float volume,std::string application){
    n::AudioSessionRecord r;r.id=std::move(id);r.processKey=std::move(process);r.name=L"Synthetic";r.pid=pid;r.active=r.controllable=true;r.volume=volume;
    r.appUserModelID=std::move(application);return r;
}
float level(World&w,std::string_view id){std::lock_guard l(w.m);for(const auto&r:w.sessions)if(r.id==id)return r.volume.value_or(-1);return -1;}

// The fixture player with the owner state of tools::NowPlayingPreview; artwork
// decoding is injected (the cover path is covered elsewhere).
struct Owner {
    Q queue{[]{}};double now=5;
    n::NativeNowPlayingService service{queue,n::nowPlayingFixtureProvider([this]{return now;}),[]{},[this]{return now;}};
    std::unique_ptr<t::NowPlayingSession>session;std::vector<m::NowPlayingPlaybackEvent>playback;
    explicit Owner(std::shared_ptr<const m::NowPlayingAppVolume>volume){
        t::NowPlayingSessionOptions options;options.volume=std::move(volume);
        options.decode=[](std::span<const std::uint8_t>){auto f=std::make_shared<n::NotesImageFrame>();f->width=f->height=1;f->straightRGBA={1,2,3,255};return n::NowPlayingImageResult{f};};
        options.playback=[this](const m::NowPlayingPlaybackEvent&e){playback.push_back(e);};
        session=std::make_unique<t::NowPlayingSession>(service,queue,[]{},std::move(options));
    }
    ~Owner(){session.reset();queue.shutdown();}
    void flush(){for(int k=0;k<8;++k){queue.waitIdle();queue.drain();session->utilityCompleted(now);}}
    const m::NowPlayingViewInput&input()const{return session->input();}
    bool volume(double value){
        // The canvas's setAppVolume: only an adjustable route takes a value.
        if(!input().volumeAvailable||!std::isfinite(value))return false;
        return session->dispatch({m::NowPlayingIntentKind::volume,input().session,std::clamp(value,0.,1.)},now);
    }
};

// Real runtime: one sleeping worker over injected backends. The test waits on
// the runtime's notice (the owner message) with a bound; the owner then drains
// and forwards the audio publication like the host's audioChanged broadcast.
struct Runtime {
    std::shared_ptr<World>world=std::make_shared<World>();
    std::mutex mutex;std::condition_variable cv;unsigned notices{};
    std::unique_ptr<n::AudioRuntime>runtime;
    Runtime(){
        n::AudioServiceOptions o;o.endpoints=[w=world]{return std::make_unique<Endpoints>(w);};o.sessions=[w=world]{return std::make_unique<Sessions>(w);};o.topologySettleSeconds=.02;
        runtime=std::make_unique<n::AudioRuntime>(std::move(o),c::Language::english,[this]{{std::lock_guard l(mutex);++notices;}cv.notify_all();});
    }
    template<class Predicate>void until(Owner&owner,Predicate predicate,const char*what){
        const auto deadline=std::chrono::steady_clock::now()+10s;
        for(;;){
            unsigned seen{};{std::lock_guard l(mutex);seen=notices;}
            if(runtime->drain().applications)(void)owner.session->audioChanged(owner.now);
            if(predicate())return;
            std::unique_lock l(mutex);
            if(!cv.wait_until(l,deadline,[&]{return notices!=seen;}))throw std::runtime_error(what);
        }
    }
};
const n::AudioApplicationRoute*row(std::span<const n::AudioApplicationRoute>rows,std::string_view id){for(const auto&r:rows)if(r.id==id)return &r;return nullptr;}

void runtime(){
    Runtime audio;
    {std::lock_guard l(audio.world->m);
        audio.world->sessions={record("{fixture}","4242:100",4242,.8f,std::string(n::nowPlayingFixtureApplication)),
                               record("{other}","5151:7",5151,.6f,"Other.Player_8wekyb3d8bbwe!App")};}
    Owner owner(n::nowPlayingAppVolume(*audio.runtime));
    check(audio.runtime->applications().empty()&&audio.runtime->stats().workerStarted==false,"Nothing is read before Now Playing votes");
    owner.session->setActive(true,owner.now);owner.flush();
    check(owner.input().track&&owner.input().track->title=="EndfieldHUD Fixture 1","The fixture player is presented");
    audio.until(owner,[&]{return owner.input().volumeAvailable;},"The fixture's per-app route resolves through the runtime");
    check(owner.session->volumeRoute()=="4242:100"&&owner.input().volume==1.,"Exactly the session whose captured AppUserModelID matches; startable route shows 100%");
    {std::lock_guard l(audio.world->m);check(audio.world->bound.empty(),"Now Playing never binds the endpoint volume control");}

    check(owner.volume(.5),"The volume slider writes the route gain through the runtime");
    check(owner.input().volume==.5,"The pending gain shows until the runtime confirms it");
    audio.until(owner,[&]{const auto*r=row(audio.runtime->applications(),"4242:100");return r&&r->state==n::AudioApplicationRouteState::active&&r->gain&&*r->gain==.5f;},"The runtime publishes the active route");
    check(std::abs(level(*audio.world,"{fixture}")-.4f)<1e-6f&&std::abs(level(*audio.world,"{other}")-.6f)<1e-6f,"Relative attenuation of the player's session only");
    check(owner.input().volume&&std::abs(*owner.input().volume-.5)<1e-6,"The confirmed route gain is the face value");

    check(owner.volume(1),"An active route returns to 100%");
    audio.until(owner,[&]{const auto*r=row(audio.runtime->applications(),"4242:100");return r&&r->gain&&*r->gain==1.f;},"The runtime confirms 100%");
    check(std::abs(level(*audio.world,"{fixture}")-.8f)<1e-6f&&owner.input().volume==1.&&owner.playback.empty(),"100% restores the original level; volume is no playback action");

    // Hiding withdraws the Now Playing vote; nothing is written afterwards.
    owner.session->setActive(false,owner.now);
    unsigned writes{};{std::lock_guard l(audio.world->m);writes=audio.world->writes;}
    check(!owner.volume(.3),"A hidden owner sends no route command");
    {std::lock_guard l(audio.world->m);check(audio.world->writes==writes,"No write after the vote is withdrawn");}
    audio.runtime->stop();
    check(std::abs(level(*audio.world,"{fixture}")-.8f)<1e-6f&&std::abs(level(*audio.world,"{other}")-.6f)<1e-6f,"Stopping the runtime leaves every original level");
}

void ambiguous(){
    // Source rule: two processes with the fixture's identity are never guessed.
    Runtime audio;
    {std::lock_guard l(audio.world->m);
        audio.world->sessions={record("{a}","4242:100",4242,.8f,std::string(n::nowPlayingFixtureApplication)),
                               record("{b}","4343:100",4343,.7f,std::string(n::nowPlayingFixtureApplication))};}
    Owner owner(n::nowPlayingAppVolume(*audio.runtime));
    owner.session->setActive(true,owner.now);owner.flush();
    audio.until(owner,[&]{return audio.runtime->applications().size()==2&&owner.input().track;},"Both processes are published");
    owner.flush();
    check(!owner.input().volumeAvailable&&!owner.input().volume&&owner.session->volumeRoute().empty(),"Several matching processes keep the source unavailable submenu");
    check(!owner.volume(.5),"The unavailable submenu sends no command");
    owner.session->setActive(false,owner.now);audio.runtime->stop();
}

void fixture(){
    // The development-preview/coverage route: same adapter, synchronous publish.
    unsigned notices{};n::NowPlayingAudioFixture audio([&]{++notices;});
    Owner owner(n::nowPlayingAppVolume(audio));
    check(audio.applications().empty()&&!audio.nowPlayingVisible()&&audio.applicationsRevision()==0,"No rows before the vote");
    owner.session->setActive(true,owner.now);
    check(audio.nowPlayingVisible()&&notices==1&&audio.pending()&&audio.applications().empty(),"The vote is one coalesced notice; rows arrive with the next publication");
    owner.flush();
    check(audio.publish()&&audio.applications().size()==1&&audio.applicationsRevision()==1&&!audio.pending(),"Publishing adopts the synthetic process route");
    check(owner.session->audioChanged(owner.now)&&owner.input().volumeAvailable&&owner.input().volume==1.,"The fixture route resolves by its captured AppUserModelID");
    check(!owner.session->audioChanged(owner.now)&&!audio.publish(),"Unchanged publications are ignored");
    check(!audio.setApplicationGain(n::NowPlayingAudioFixture::route,1.5)&&!audio.setApplicationGain("1:1",.5)&&!audio.setApplicationGain(n::NowPlayingAudioFixture::route,std::nan("")),"Invalid routes and gains are refused before the queue");
    check(owner.volume(.25)&&audio.accepted()==1&&notices==2&&audio.process().gain==.25f&&audio.applications().front().state==n::AudioApplicationRouteState::direct,"A write is accepted asynchronously");
    check(audio.publish()&&owner.session->audioChanged(owner.now)&&owner.input().volume==.25,"The confirmed gain returns through the publication");
    check(owner.volume(1)&&audio.publish()&&owner.session->audioChanged(owner.now)&&owner.input().volume==1.&&audio.process().state==n::AudioApplicationRouteState::active,"An active route takes 100% as a gain");
    check(audio.stopApplication(n::NowPlayingAudioFixture::route)&&audio.publish()&&audio.process().state==n::AudioApplicationRouteState::direct&&!audio.process().gain,"Stop restores the direct route");
    owner.session->setActive(false,owner.now);
    check(!audio.nowPlayingVisible()&&audio.publish()&&audio.applications().empty()&&!audio.setApplicationGain(n::NowPlayingAudioFixture::route,.5),"Withdrawing the vote clears the rows and refuses writes");
}
}
int main(){
    try{runtime();ambiguous();fixture();std::cout<<"Now Playing runtime volume: "<<checks<<" checks passed (injected audio backends, synthetic player)\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Now Playing runtime volume failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
