#include "native/now_playing_service.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <thread>

namespace endfield::native {
namespace {
constexpr long double ticksPerSecond=10000000.L;
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
}
std::optional<double>NowPlayingTimeline::elapsed(double now)const noexcept{
    if(end<=start||!std::isfinite(sampledAt))return {};
    const auto duration=std::min(static_cast<double>((static_cast<long double>(end)-start)/ticksPerSecond),modules::nowPlayingMaximumSeconds);
    auto value=static_cast<double>((static_cast<long double>(position)-start)/ticksPerSecond);
    if(playing&&std::isfinite(now)&&std::isfinite(rate)&&rate>0)value+=std::max(0.,now-sampledAt)*rate;
    return std::clamp(value,0.,duration);
}
std::optional<std::int64_t>NowPlayingTimeline::seekTicks(double seconds)const noexcept{
    if(!std::isfinite(seconds)||end<=start||maximumSeek<minimumSeek)return {};
    const auto minimum=std::max(start,minimumSeek),maximum=std::min(end,maximumSeek);if(maximum<minimum)return {};
    const auto requested=static_cast<long double>(start)+static_cast<long double>(seconds)*ticksPerSecond;
    const auto bounded=std::clamp(requested,static_cast<long double>(minimum),static_cast<long double>(maximum));
    const auto rounded=std::round(bounded);if(rounded>=static_cast<long double>(maximum))return maximum;if(rounded<=static_cast<long double>(minimum))return minimum;
    return static_cast<std::int64_t>(rounded);
}
struct NativeNowPlayingService::Impl:std::enable_shared_from_this<Impl> {
    enum class Phase {idle,start,read,command};
    struct Message {std::uint64_t serial{},events{};Phase phase{};NowPlayingReadResult read;NowPlayingCommandResult result;};
    struct Command {modules::NowPlayingCommand value;std::uint64_t session{},metadata{};modules::NowPlayingTrack track;NowPlayingCommandReceipt receipt;};
    app::UtilityExecutor&executor;Factory factory;std::function<void()>notify;std::function<double()>now;double timeout;std::optional<double>deadline;const std::thread::id owner=std::this_thread::get_id();
    std::shared_ptr<NowPlayingProvider>provider;app::UtilityExecutor::Route route{};NowPlayingServiceSnapshot published;
    std::atomic<std::uint64_t>generation{1},events{};std::mutex mutex;std::optional<Message>message;bool notified{},changed{};
    bool alive{true},ready{},inFlight{},pendingRead{};std::optional<Command>pendingCommand;std::optional<NowPlayingCommandReceipt>executingCommand;std::uint64_t serial{},commandToken{};Phase phase{Phase::idle};
    Impl(app::UtilityExecutor&e,Factory f,std::function<void()>n,std::function<double()>clock,double bound):executor(e),factory(std::move(f)),notify(std::move(n)),now(std::move(clock)),timeout(bound){need(bool(factory)&&bool(notify)&&bool(now)&&std::isfinite(timeout)&&timeout>0&&timeout<=30,"Now Playing needs provider, owner clock/wake and bounded timeout");}
    void check()const{if(owner!=std::this_thread::get_id())throw std::logic_error("Now Playing service belongs to its owner thread");}
    void wake(std::uint64_t expected,std::optional<Message>next={}){
        if(generation.load(std::memory_order_acquire)!=expected)return;bool post{};{
            std::lock_guard lock(mutex);if(generation.load(std::memory_order_relaxed)!=expected)return;
            if(next){if(message&&message->serial>next->serial)return;message=std::move(next);}else{changed=true;events.fetch_add(1,std::memory_order_relaxed);}post=!notified;notified=true;
        }if(post)try{notify();}catch(...){std::lock_guard lock(mutex);notified=false;}
    }
    void stop(){
        check();generation.fetch_add(1,std::memory_order_acq_rel);auto old=std::move(provider);if(route){executor.invalidate(route);route=0;}
        ready=inFlight=pendingRead=false;deadline.reset();pendingCommand.reset();executingCommand.reset();phase=Phase::idle;{std::lock_guard lock(mutex);message.reset();changed=notified=false;}
        const auto revision=published.revision+1;published={};published.revision=revision;if(old)old->stop();
    }
    void activate(){
        check();if(!alive)return;const auto expected=generation.load(std::memory_order_acquire);published.active=true;published.busy=true;published.fresh=false;++published.revision;
        try{auto next=factory();need(bool(next),"Now Playing provider factory returned no provider");if(!alive||!published.active||generation.load()!=expected){next->stop();return;}provider=std::move(next);route=executor.makeRoute();pendingRead=true;schedule();}
        catch(...){if(alive&&generation.load()==expected){published.busy=false;published.failure=NowPlayingServiceFailure::unavailable;++published.revision;}}
    }
    void schedule(){
        if(!alive||!published.active||!provider||inFlight)return;
        Phase operation=Phase::idle;std::optional<Command>command;
        if(!ready)operation=Phase::start;
        else if(pendingCommand){command=pendingCommand;operation=Phase::command;}
        else if(pendingRead)operation=Phase::read;
        if(operation==Phase::idle)return;
        const auto expected=generation.load(std::memory_order_acquire),request=++serial,version=events.load();const auto p=provider;const auto weak=weak_from_this();
        auto complete=[weak,expected,request,version,operation](NowPlayingCommandResult result){if(auto self=weak.lock()){Message m;m.serial=request;m.events=version;m.phase=operation;m.result=result;if(operation==Phase::read){m.read.failure=result.failure;m.read.nativeError=result.nativeError;}self->wake(expected,std::move(m));}};
        const auto time=now();need(std::isfinite(time)&&std::isfinite(time+timeout),"Now Playing clock must be finite");
        inFlight=true;phase=operation;published.busy=true;
        const bool accepted=executor.submit(route,[p,weak,expected,request,version,operation,command,complete]{
            if(operation==Phase::start)p->start([weak,expected]{if(auto self=weak.lock())self->wake(expected);},complete);
            else if(operation==Phase::read)p->read([weak,expected,request,version](NowPlayingReadResult result){if(auto self=weak.lock()){Message m;m.serial=request;m.events=version;m.phase=Phase::read;m.read=std::move(result);self->wake(expected,std::move(m));}});
            else p->perform(command->session,command->metadata,command->value,complete);
        },[complete](std::exception_ptr error){if(error)complete({NowPlayingServiceFailure::unavailable,0});});
        if(!accepted){inFlight=false;phase=Phase::idle;published.busy=false;return;}
        deadline=time+timeout;
        if(operation==Phase::read)pendingRead=false;if(operation==Phase::command){executingCommand=command->receipt;pendingCommand.reset();}
    }
    void accept(NowPlayingReadResult result){
        if(result.failure!=NowPlayingServiceFailure::none){published.failure=result.failure;published.nativeError=result.nativeError;return;}
        auto&value=result.value;need(value.sessions.size()<=128,"Now Playing session list exceeds its bounded snapshot");
        for(std::size_t n=0;n<value.sessions.size();++n){const auto&s=value.sessions[n];need(s.token&&s.appUserModelID.size()<=4096&&s.displayName.size()<=4096&&ehud::data::Json::validUtf8(s.appUserModelID)&&ehud::data::Json::validUtf8(s.displayName),"Invalid Now Playing session identity");for(std::size_t prior=0;prior<n;++prior)need(value.sessions[prior].token!=s.token,"Duplicate Now Playing session token");}
        if(value.session){need(value.session->token!=0,"Now Playing session requires native identity");need(std::find(value.sessions.begin(),value.sessions.end(),*value.session)!=value.sessions.end(),"Selected Now Playing session must be in the same snapshot");}
        else need(!value.track&&!value.artwork,"Media needs its selected session");
        if(value.artwork)need(value.artwork->size()<=modules::nowPlayingMaximumArtworkBytes,"Now Playing artwork exceeds source byte limit");
        if(value.track)value.track=modules::NowPlayingTrack::bounded(std::move(*value.track));
        published.media=std::move(value);published.failure=NowPlayingServiceFailure::none;published.nativeError=0;published.fresh=true;++published.mediaReadRevision;
    }
    void completed(const NowPlayingCommandReceipt&request,NowPlayingCommandResult result){
        const auto time=now();need(std::isfinite(time),"Now Playing completion clock must be finite");
        published.commandCompleted=NowPlayingCommandCompletion{request,result.failure,result.nativeError,time,published.mediaReadRevision};
    }
    bool drain(){
        check();const auto expected=generation.load();std::optional<Message>next;bool event{};{std::lock_guard lock(mutex);next=std::move(message);message.reset();event=changed;changed=notified=false;}
        if(!published.active||!alive)return false;
        if(inFlight&&deadline&&!next&&now()>=*deadline){if(executingCommand)completed(*executingCommand,{NowPlayingServiceFailure::timedOut,0});executingCommand.reset();generation.fetch_add(1);auto old=std::move(provider);if(route){executor.invalidate(route);route=0;}inFlight=ready=pendingRead=false;deadline.reset();pendingCommand.reset();phase=Phase::idle;published.busy=false;published.failure=NowPlayingServiceFailure::timedOut;published.nativeError=0;++published.revision;if(old)old->stop();return true;}
        bool updated=event;if(event)pendingRead=true;
        if(next&&inFlight&&next->serial==serial&&next->phase==phase){inFlight=false;deadline.reset();phase=Phase::idle;published.busy=false;updated=true;
            if(next->phase==Phase::read){if(next->events==events.load())try{accept(std::move(next->read));}catch(...){published.failure=NowPlayingServiceFailure::unavailable;published.nativeError=0;}else pendingRead=true;}
            else if(next->phase==Phase::start){if(next->result.failure==NowPlayingServiceFailure::none){ready=true;pendingRead=true;}else{published.failure=next->result.failure;published.nativeError=next->result.nativeError;pendingRead=false;auto old=std::move(provider);if(old)old->stop();}}
            else{if(executingCommand)completed(*executingCommand,next->result);executingCommand.reset();if(next->result.failure!=NowPlayingServiceFailure::none&&next->result.failure!=NowPlayingServiceFailure::stale){published.failure=next->result.failure;published.nativeError=next->result.nativeError;}pendingRead=true;}
        }
        if(!alive||!published.active||generation.load()!=expected)return updated;
        if(pendingCommand&&!published.media.session){completed(pendingCommand->receipt,{NowPlayingServiceFailure::stale,0});pendingCommand.reset();updated=true;}
        if(pendingCommand&&published.media.session){const auto&pending=*pendingCommand;const bool seek=pending.value.kind==modules::NowPlayingCommandKind::seek;
            if(pending.session!=published.media.session->token||(seek&&(!published.media.track||!published.media.track->sameIdentity(pending.track)))){completed(pending.receipt,{NowPlayingServiceFailure::stale,0});pendingCommand.reset();updated=true;}}
        if(updated)++published.revision;schedule();return updated;
    }
};
NativeNowPlayingService::NativeNowPlayingService(app::UtilityExecutor&e,Factory f,std::function<void()>n,std::function<double()>now,double timeout):impl_(std::make_shared<Impl>(e,std::move(f),std::move(n),std::move(now),timeout)){}
NativeNowPlayingService::~NativeNowPlayingService(){auto i=impl_;i->check();i->alive=false;i->stop();}
void NativeNowPlayingService::setActive(bool value){auto i=impl_;i->check();if(i->published.active==value)return;if(value)i->activate();else i->stop();}
void NativeNowPlayingService::refresh(){auto i=impl_;i->check();if(!i->published.active)return;if(!i->provider){i->stop();i->activate();return;}i->pendingRead=true;i->schedule();}
bool NativeNowPlayingService::perform(modules::NowPlayingCommand request){auto i=impl_;i->check();const auto&s=i->published;if(!s.active||!s.fresh||s.failure!=NowPlayingServiceFailure::none||!s.media.session||!s.media.track)return false;const auto command=modules::nowPlayingCommand(*s.media.track,s.media.capabilities,request);if(!command)return false;need(i->commandToken!=UINT64_MAX,"Now Playing command token exhausted");NowPlayingCommandReceipt receipt{++i->commandToken,s.media.session->token,s.media.metadataRevision,*command,i->pendingCommand?std::optional(i->pendingCommand->receipt.token):std::nullopt};i->pendingCommand=Impl::Command{*command,s.media.session->token,s.media.metadataRevision,*s.media.track,receipt};i->published.commandAccepted=receipt;++i->published.revision;i->schedule();return true;}
bool NativeNowPlayingService::drain(){auto i=impl_;return i->drain();}
std::optional<double>NativeNowPlayingService::nextWakeTime()const{impl_->check();return impl_->deadline;}
const NowPlayingServiceSnapshot&NativeNowPlayingService::snapshot()const{impl_->check();return impl_->published;}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <roapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

namespace endfield::native {
namespace {
namespace media=winrt::Windows::Media::Control;
namespace foundation=winrt::Windows::Foundation;
namespace streams=winrt::Windows::Storage::Streams;
using Session=media::GlobalSystemMediaTransportControlsSession;
using Manager=media::GlobalSystemMediaTransportControlsSessionManager;
struct Apartment {
    HRESULT status=RoInitialize(RO_INIT_MULTITHREADED);
    Apartment(){winrt::check_hresult(status);}
    ~Apartment(){if(SUCCEEDED(status))RoUninitialize();}
};
struct MtaUsage {
    CO_MTA_USAGE_COOKIE cookie{};
    MtaUsage(){winrt::check_hresult(CoIncrementMTAUsage(&cookie));}
    ~MtaUsage(){if(cookie)CoDecrementMTAUsage(cookie);}
    MtaUsage(const MtaUsage&)=delete;MtaUsage&operator=(const MtaUsage&)=delete;
};
NowPlayingCommandResult failure(std::int32_t error){return {error==E_ACCESSDENIED?NowPlayingServiceFailure::accessDenied:error==E_ABORT||error==HRESULT_FROM_WIN32(ERROR_CANCELLED)?NowPlayingServiceFailure::cancelled:NowPlayingServiceFailure::unavailable,error};}
std::string boundedText(const winrt::hstring&value){need(value.size()<=4096,"GSMTC metadata exceeds source string bound");auto text=winrt::to_string(value);need(text.size()<=4096,"GSMTC metadata exceeds source UTF8 bound");return text;}
struct ManagerEvents {
    // Retire the apartment lease after the event source, including an event
    // callback that briefly outlives its stopped provider.
    std::shared_ptr<MtaUsage>mta;
    Manager value{nullptr};winrt::event_token current{},sessions{};std::atomic<bool>revoked{};
    void revoke()noexcept{if(revoked.exchange(true))return;if(value){try{if(current.value)value.CurrentSessionChanged(current);}catch(...){}try{if(sessions.value)value.SessionsChanged(sessions);}catch(...){}}}
    ~ManagerEvents(){revoke();}
};
struct SessionEvents {
    std::shared_ptr<MtaUsage>mta;
    Session value{nullptr};winrt::event_token media{},playback{},timeline{};std::uint64_t token{};std::atomic<bool>revoked{};
    void revoke()noexcept{if(revoked.exchange(true))return;if(value){try{if(media.value)value.MediaPropertiesChanged(media);}catch(...){}try{if(playback.value)value.PlaybackInfoChanged(playback);}catch(...){}try{if(timeline.value)value.TimelinePropertiesChanged(timeline);}catch(...){}}}
    ~SessionEvents(){revoke();}
};
struct WindowsMediaState:std::enable_shared_from_this<WindowsMediaState> {
    struct Entry {Session value{nullptr};winrt::com_ptr<::IUnknown>identity;std::uint64_t token{};};
    struct Cache {std::uint64_t token{},media{};modules::NowPlayingTrack track;std::shared_ptr<const std::vector<std::uint8_t>>artwork;};
    // First member dies last. The shared utility worker's scoped apartment may
    // end while a WinRT coroutine is suspended; COM keeps its MTA available
    // until all retained native work and event sources have retired.
    std::shared_ptr<MtaUsage>mta;
    std::function<double()>now;NowPlayingProvider::Changed changed;std::atomic<bool>stopped{};std::atomic<std::uint64_t>mediaRevision{1};
    std::mutex mutex;std::shared_ptr<ManagerEvents>manager;std::shared_ptr<SessionEvents>selected;std::vector<Entry>entries;
    std::array<foundation::IAsyncInfo,3>operations{{nullptr,nullptr,nullptr}};std::optional<Cache>cache;std::optional<NowPlayingTimeline>lastTimeline;
    std::uint64_t nextToken{1},lastMetadata{},artworkRevision{};
    explicit WindowsMediaState(std::function<double()>clock):now(std::move(clock)){}
    bool retainMta(){if(stopped.load())return false;auto lease=std::make_shared<MtaUsage>();{std::lock_guard lock(mutex);if(stopped.load())return false;if(!mta)mta=std::move(lease);}return true;}
    void event(bool metadata){if(stopped.load())return;if(metadata)mediaRevision.fetch_add(1);NowPlayingProvider::Changed callback;{std::lock_guard lock(mutex);if(stopped.load())return;callback=changed;}if(callback)try{callback();}catch(...){}}
    bool operation(unsigned slot,const foundation::IAsyncInfo&value){bool accept{};foundation::IAsyncInfo old{nullptr};{std::lock_guard lock(mutex);accept=!stopped.load();if(accept){old=std::move(operations.at(slot));operations.at(slot)=value;}}if(!accept)try{value.Cancel();}catch(...){}return accept;}
    void finish(unsigned slot){foundation::IAsyncInfo old{nullptr};{std::lock_guard lock(mutex);old=std::move(operations.at(slot));}}
    void stop()noexcept{
        if(stopped.exchange(true))return;std::shared_ptr<ManagerEvents>m;std::shared_ptr<SessionEvents>s;std::array<foundation::IAsyncInfo,3>pending{{nullptr,nullptr,nullptr}};std::vector<Entry>retired;
        {std::lock_guard lock(mutex);m=std::move(manager);s=std::move(selected);pending=std::move(operations);changed={};retired=std::move(entries);cache.reset();lastTimeline.reset();}
        if(s)s->revoke();if(m)m->revoke();for(auto&operation:pending)if(operation)try{operation.Cancel();}catch(...){}
    }
    ~WindowsMediaState(){stop();}
};
modules::NowPlayingCapabilities capabilities(const media::GlobalSystemMediaTransportControlsSessionPlaybackInfo&info){
    const auto c=info.Controls();modules::NowPlayingCapabilities out;if(c){out.play=c.IsPlayEnabled();out.pause=c.IsPauseEnabled();out.toggle=c.IsPlayPauseToggleEnabled();out.previous=c.IsPreviousEnabled();out.next=c.IsNextEnabled();out.seek=c.IsPlaybackPositionEnabled();}return out;
}
winrt::fire_and_forget beginWindows(std::shared_ptr<WindowsMediaState>state,NowPlayingProvider::Changed changed,NowPlayingProvider::Ready ready){
    try{
        {std::lock_guard lock(state->mutex);if(state->stopped.load())co_return;state->changed=std::move(changed);}
        auto request=Manager::RequestAsync();if(!state->operation(0,request.as<foundation::IAsyncInfo>()))co_return;auto value=co_await request;state->finish(0);if(state->stopped.load())co_return;
        auto events=std::make_shared<ManagerEvents>();events->mta=state->mta;events->value=value;const auto weak=std::weak_ptr(state);
        events->current=value.CurrentSessionChanged([weak](auto&&,auto&&){if(auto state=weak.lock())state->event(true);});
        events->sessions=value.SessionsChanged([weak](auto&&,auto&&){if(auto state=weak.lock())state->event(false);});
        {std::lock_guard lock(state->mutex);if(state->stopped.load())co_return;state->manager=std::move(events);}ready({});
    }catch(const winrt::hresult_error&e){state->finish(0);if(!state->stopped.load())ready(failure(static_cast<std::int32_t>(e.code())));}
    catch(...){state->finish(0);if(!state->stopped.load())ready({NowPlayingServiceFailure::unavailable,0});}
}
winrt::fire_and_forget readWindows(std::shared_ptr<WindowsMediaState>state,NowPlayingProvider::Read completion){
    NowPlayingReadResult result;
    try{
        std::shared_ptr<ManagerEvents>manager;{std::lock_guard lock(state->mutex);manager=state->manager;}if(state->stopped.load())co_return;need(bool(manager),"GSMTC manager is not ready");
        const auto current=manager->value.GetCurrentSession();const auto sessions=manager->value.GetSessions();std::vector<WindowsMediaState::Entry>next;next.reserve(std::min<unsigned>(127,sessions.Size())+unsigned(bool(current)));
        const auto add=[&](const Session&session){if(!session)return;const auto identity=session.as<::IUnknown>();if(std::any_of(next.begin(),next.end(),[&](const auto&e){return e.identity.get()==identity.get();}))return;
            std::uint64_t token{};{std::lock_guard lock(state->mutex);if(state->stopped.load())return;const auto old=std::find_if(state->entries.begin(),state->entries.end(),[&](const auto&e){return e.identity.get()==identity.get();});if(old!=state->entries.end())token=old->token;else{need(state->nextToken!=UINT64_MAX,"GSMTC session token exhausted");token=state->nextToken++;}}
            const auto id=boundedText(session.SourceAppUserModelId());next.push_back({session,identity,token});result.value.sessions.push_back({token,id,id});};
        add(current);for(unsigned k=0;k<std::min<unsigned>(128,sessions.Size())&&next.size()<128;++k)add(sessions.GetAt(k));
        std::shared_ptr<SessionEvents>selected,old;std::vector<WindowsMediaState::Entry>retired;{std::lock_guard lock(state->mutex);if(state->stopped.load())co_return;retired=std::move(state->entries);state->entries=std::move(next);selected=state->selected;}retired.clear();
        if(!current){std::lock_guard lock(state->mutex);old=std::move(state->selected);state->cache.reset();state->lastTimeline.reset();}
        else{
            result.value.session=result.value.sessions.front();const auto token=result.value.session->token;
            if(!selected||selected->token!=token){selected=std::make_shared<SessionEvents>();selected->mta=state->mta;selected->value=current;selected->token=token;const auto weak=std::weak_ptr(state);const auto weakSession=std::weak_ptr(selected);
                selected->media=current.MediaPropertiesChanged([weak,weakSession](auto&&,auto&&){if(auto live=weakSession.lock();live&&!live->revoked.load())if(auto state=weak.lock())state->event(true);});
                selected->playback=current.PlaybackInfoChanged([weak,weakSession](auto&&,auto&&){if(auto live=weakSession.lock();live&&!live->revoked.load())if(auto state=weak.lock())state->event(false);});
                selected->timeline=current.TimelinePropertiesChanged([weak,weakSession](auto&&,auto&&){if(auto live=weakSession.lock();live&&!live->revoked.load())if(auto state=weak.lock())state->event(false);});
                {std::lock_guard lock(state->mutex);if(state->stopped.load())co_return;old=std::move(state->selected);state->selected=selected;state->cache.reset();state->lastTimeline.reset();state->mediaRevision.fetch_add(1);}
            }
        }
        if(old)old->revoke();if(!current){completion(std::move(result));co_return;}
        const auto version=state->mediaRevision.load();std::optional<WindowsMediaState::Cache>cache;{std::lock_guard lock(state->mutex);if(state->cache&&state->cache->token==selected->token&&state->cache->media==version)cache=state->cache;}
        if(!cache){
            auto request=current.TryGetMediaPropertiesAsync();if(!state->operation(1,request.as<foundation::IAsyncInfo>()))co_return;const auto properties=co_await request;state->finish(1);if(state->stopped.load())co_return;
            WindowsMediaState::Cache nextCache;nextCache.token=selected->token;nextCache.media=version;nextCache.track.title=boundedText(properties.Title());nextCache.track.artist=boundedText(properties.Artist());nextCache.track.album=boundedText(properties.AlbumTitle());
            // Thumbnail failure is a missing cover, not missing transport data.
            try{if(const auto reference=properties.Thumbnail()){
                auto request=reference.OpenReadAsync();if(!state->operation(1,request.as<foundation::IAsyncInfo>()))co_return;auto stream=co_await request;state->finish(1);if(state->stopped.load())co_return;
                const auto size=stream.Size();if(size&&size<=modules::nowPlayingMaximumArtworkBytes){auto bytes=std::make_shared<std::vector<std::uint8_t>>();bytes->reserve(static_cast<std::size_t>(size));streams::DataReader reader(stream.GetInputStreamAt(0));reader.InputStreamOptions(streams::InputStreamOptions::Partial);
                    while(bytes->size()<size){auto request=reader.LoadAsync(static_cast<unsigned>(std::min<std::uint64_t>(65536,size-bytes->size())));if(!state->operation(1,request.as<foundation::IAsyncInfo>()))co_return;const auto count=co_await request;state->finish(1);if(state->stopped.load())co_return;if(!count)break;need(count<=size-bytes->size(),"GSMTC thumbnail exceeded bounded stream size");const auto offset=bytes->size();bytes->resize(offset+count);reader.ReadBytes(winrt::array_view<std::uint8_t>(bytes->data()+offset,bytes->data()+bytes->size()));}
                    if(bytes->size()==size)nextCache.artwork=std::move(bytes);reader.Close();}stream.Close();
            }}catch(...){state->finish(1);if(state->stopped.load())co_return;nextCache.artwork.reset();}
            {std::lock_guard lock(state->mutex);if(state->stopped.load())co_return;if(state->cache&&state->cache->artwork&&nextCache.artwork&&*state->cache->artwork==*nextCache.artwork)nextCache.artwork=state->cache->artwork;else ++state->artworkRevision;if(nextCache.artwork)nextCache.track.artworkRevision=std::to_string(state->artworkRevision);}
            cache=std::move(nextCache);
        }
        const auto playback=current.GetPlaybackInfo();const auto timeline=current.GetTimelineProperties();const auto sample=state->now();need(std::isfinite(sample),"GSMTC needs the host monotonic clock");NowPlayingTimeline clock;
        clock.start=timeline.StartTime().count();clock.end=timeline.EndTime().count();clock.position=timeline.Position().count();clock.minimumSeek=timeline.MinSeekTime().count();clock.maximumSeek=timeline.MaxSeekTime().count();clock.playing=playback.PlaybackStatus()==media::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;const auto rate=playback.PlaybackRate();clock.rate=rate?rate.Value():std::numeric_limits<double>::quiet_NaN();clock.sampledAt=sample;
        const auto utc=winrt::clock::now().time_since_epoch().count(),updated=timeline.LastUpdatedTime().time_since_epoch().count();const auto age=static_cast<double>((static_cast<long double>(utc)-updated)/ticksPerSecond);if(clock.playing&&updated>0&&age>=0&&age<=modules::nowPlayingMaximumSeconds&&std::isfinite(age))clock.sampledAt-=age;
        result.value.capabilities=capabilities(playback);auto track=cache->track;track.isPlaying=clock.playing;track.sampledAt=sample;if(clock.end>clock.start)track.duration=static_cast<double>((static_cast<long double>(clock.end)-clock.start)/ticksPerSecond);track.position=clock.elapsed(sample);track.supportsSeeking=result.value.capabilities.seek&&clock.seekTicks(0).has_value();track=modules::NowPlayingTrack::bounded(std::move(track));
        result.value.timeline=clock;result.value.metadataRevision=version;result.value.artwork=cache->artwork;if(!track.title.empty())result.value.track=std::move(track);
        {std::lock_guard lock(state->mutex);if(state->stopped.load())co_return;if(state->mediaRevision.load()!=version){result.failure=NowPlayingServiceFailure::stale;}else{state->cache=std::move(cache);state->lastTimeline=clock;state->lastMetadata=version;}}
        completion(std::move(result));
    }catch(const winrt::hresult_error&e){state->finish(1);if(!state->stopped.load()){const auto error=failure(static_cast<std::int32_t>(e.code()));result.failure=error.failure;result.nativeError=error.nativeError;completion(std::move(result));}}
    catch(...){state->finish(1);if(!state->stopped.load()){result.failure=NowPlayingServiceFailure::unavailable;completion(std::move(result));}}
}
winrt::fire_and_forget commandWindows(std::shared_ptr<WindowsMediaState>state,std::uint64_t token,std::uint64_t version,modules::NowPlayingCommand command,NowPlayingProvider::Ready completion){
    try{
        std::shared_ptr<SessionEvents>session;std::shared_ptr<ManagerEvents>manager;std::optional<NowPlayingTimeline>timeline;{std::lock_guard lock(state->mutex);if(state->stopped.load())co_return;session=state->selected;manager=state->manager;timeline=state->lastTimeline;}
        if(!session||session->token!=token||session->revoked.load()){completion({NowPlayingServiceFailure::stale,0});co_return;}
        const auto current=manager?manager->value.GetCurrentSession():Session{nullptr};if(!current||current.as<::IUnknown>().get()!=session->value.as<::IUnknown>().get()){completion({NowPlayingServiceFailure::stale,0});co_return;}
        if(command.kind==modules::NowPlayingCommandKind::seek&&version!=state->mediaRevision.load()){completion({NowPlayingServiceFailure::stale,0});co_return;}
        const auto info=session->value.GetPlaybackInfo();const auto controls=capabilities(info);if(state->stopped.load())co_return;foundation::IAsyncOperation<bool>operation{nullptr};
        switch(command.kind){case modules::NowPlayingCommandKind::playPause:if(controls.toggle)operation=session->value.TryTogglePlayPauseAsync();else if(info.PlaybackStatus()==media::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing&&controls.pause)operation=session->value.TryPauseAsync();else if(info.PlaybackStatus()!=media::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing&&controls.play)operation=session->value.TryPlayAsync();break;
        case modules::NowPlayingCommandKind::previous:if(controls.previous)operation=session->value.TrySkipPreviousAsync();break;
        case modules::NowPlayingCommandKind::next:if(controls.next)operation=session->value.TrySkipNextAsync();break;
        case modules::NowPlayingCommandKind::seek:if(controls.seek&&timeline){const auto fresh=session->value.GetTimelineProperties();timeline->start=fresh.StartTime().count();timeline->end=fresh.EndTime().count();timeline->minimumSeek=fresh.MinSeekTime().count();timeline->maximumSeek=fresh.MaxSeekTime().count();if(const auto ticks=timeline->seekTicks(command.seconds))operation=session->value.TryChangePlaybackPositionAsync(*ticks);}break;}
        if(!operation){completion({NowPlayingServiceFailure::unsupported,0});co_return;}if(!state->operation(2,operation.as<foundation::IAsyncInfo>()))co_return;const auto accepted=co_await operation;state->finish(2);if(!state->stopped.load())completion({accepted?NowPlayingServiceFailure::none:NowPlayingServiceFailure::unavailable,0});
    }catch(const winrt::hresult_error&e){state->finish(2);if(!state->stopped.load())completion(failure(static_cast<std::int32_t>(e.code())));}
    catch(...){state->finish(2);if(!state->stopped.load())completion({NowPlayingServiceFailure::unavailable,0});}
}
class WindowsMediaProvider final:public NowPlayingProvider {
    std::shared_ptr<WindowsMediaState>state_;
public:
    explicit WindowsMediaProvider(std::function<double()>now):state_(std::make_shared<WindowsMediaState>(std::move(now))){}
    ~WindowsMediaProvider(){stop();}
    void start(Changed changed,Ready ready)override{if(!state_->retainMta())return;Apartment apartment;beginWindows(state_,std::move(changed),std::move(ready));}
    void read(Read completion)override{Apartment apartment;readWindows(state_,std::move(completion));}
    void perform(std::uint64_t token,std::uint64_t revision,modules::NowPlayingCommand command,Ready completion)override{Apartment apartment;commandWindows(state_,token,revision,command,std::move(completion));}
    void stop()noexcept override{state_->stop();}
};
}
NativeNowPlayingService::Factory windowsNowPlayingProvider(std::function<double()>now){need(bool(now),"GSMTC needs the host monotonic clock");return [now=std::move(now)]{return std::make_shared<WindowsMediaProvider>(now);};}
}
#endif
