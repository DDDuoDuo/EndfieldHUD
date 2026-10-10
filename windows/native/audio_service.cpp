#include "native/audio_service.hpp"
#include "native/audio_route_journal.hpp"
#include "native/system_services.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace endfield::native {
namespace {
using Clock=std::chrono::steady_clock;
constexpr std::int32_t pending=static_cast<std::int32_t>(0x8000000Au);       // E_PENDING
constexpr std::int32_t unexpected=static_cast<std::int32_t>(0x8000FFFFu);    // E_UNEXPECTED
constexpr std::int32_t invalidValue=static_cast<std::int32_t>(0x80070057u);  // E_INVALIDARG
constexpr std::int32_t notFound=static_cast<std::int32_t>(0x80070490u);      // ERROR_NOT_FOUND
constexpr std::int32_t deviceChanged=static_cast<std::int32_t>(0x8007048Fu); // ERROR_DEVICE_NOT_CONNECTED
constexpr std::int32_t unsupported=static_cast<std::int32_t>(0x80070032u);   // ERROR_NOT_SUPPORTED
constexpr std::int32_t overflow=static_cast<std::int32_t>(0x8007006Fu);      // ERROR_BUFFER_OVERFLOW
constexpr unsigned commandBit=1,topologyBit=2,volumeBit=4,sessionsBit=8,stopBit=16;
constexpr unsigned tierTopology=1,tierEndpoints=2,tierApplications=4;
constexpr std::size_t maximumQueue=512;
unsigned demandFor(unsigned votes)noexcept{
    unsigned d{};
    if(votes)d|=tierTopology;
    if(votes&static_cast<unsigned>(AudioVoter::volume))d|=tierEndpoints;
    if(votes&(static_cast<unsigned>(AudioVoter::volume)|static_cast<unsigned>(AudioVoter::nowPlaying)))d|=tierApplications;
    return d;
}
template<class F>std::int32_t call(F&&f)noexcept{try{return f();}catch(const std::bad_alloc&){return static_cast<std::int32_t>(0x8007000Eu);}catch(...){return unexpected;}}
bool unit(float v)noexcept{return std::isfinite(v)&&v>=0&&v<=1;}
bool same(float a,float b)noexcept{return std::abs(a-b)<=audioSessionScalarTolerance;}
bool endpointToken(std::wstring_view v)noexcept{return !v.empty()&&v.size()<=32767&&v.find(L'\0')==v.npos;}
bool applicationToken(std::string_view v)noexcept{return !v.empty()&&v.size()<=4096&&v.find('\0')==v.npos;}

// Native callbacks only raise a bit under a short lock; they never touch the
// worker's backends or the owner. The lock is never held across any call.
struct Signal {
    std::mutex mutex;std::condition_variable cv;unsigned flags{};
    void raise(unsigned bit){{std::lock_guard lock(mutex);flags|=bit;}cv.notify_one();}
};

// Journals the first attenuating write of each session BEFORE it reaches the
// mixer, and recovers a previous run's entries on every session read of the
// same endpoint. Lives and runs only on the worker, wrapping the real backend.
class JournaledSessions final:public AudioSessionBackend {
public:
    JournaledSessions(std::unique_ptr<AudioSessionBackend>inner,AudioRouteJournal*journal,std::function<void()>touched,std::atomic<std::uint64_t>&writes)
        :inner_(std::move(inner)),journal_(journal),touched_(std::move(touched)),writes_(writes){if(!inner_)throw std::invalid_argument("Missing audio session backend");}
    std::int32_t open(std::wstring_view endpoint,Wake wake)override{endpoint_.assign(endpoint);identities_.clear();return inner_->open(endpoint,std::move(wake));}
    std::int32_t resume(Wake wake)override{return inner_->resume(std::move(wake));}
    void pause()noexcept override{inner_->pause();}
    std::int32_t read(std::vector<AudioSessionRecord>&out)override{
        const auto hr=inner_->read(out);if(hr<0)return hr;
        identities_.clear();for(const auto&r:out)identities_[r.id]={r.persistentID,r.processKey};
        if(journal_){
            // Owned sessions that vanished without a restore become recovery
            // entries, so the app's next session in this run is restored too.
            std::vector<std::string>missing;
            for(const auto&e:journal_->live())if(e.endpoint==endpoint_&&!identities_.contains(e.session))missing.push_back(e.session);
            for(const auto&session:missing)if(journal_->demoteSession(session))touched_();
            recover(out);
        }
        return hr;
    }
    std::int32_t readVolume(std::string_view id,float&value)override{return inner_->readVolume(id,value);}
    std::int32_t writeVolume(std::string_view id,float value)override{
        if(!journal_)return inner_->writeVolume(id,value);
        if(const auto*entry=journal_->liveEntry(id)){
            const float original=entry->original;const std::string session(id);
            const auto hr=inner_->writeVolume(id,value);
            if(hr>=0){if(same(value,original))journal_->release(session);else journal_->written(session,value);touched_();}
            return hr;
        }
        float original{};auto hr=inner_->readVolume(id,original);if(hr<0)return hr;
        if(!unit(original))return unexpected;
        if(same(value,original))return inner_->writeVolume(id,value); // No attenuation: nothing to recover.
        const auto identity=identities_.find(id);if(identity==identities_.end())return notFound;
        // Durable record first. Without it, the mixer is not touched at all.
        hr=journal_->record({endpoint_,std::string(id),identity->second.first,identity->second.second,original,value});
        ++writes_;
        if(hr<0)return hr;
        return inner_->writeVolume(id,value);
    }
    void close()noexcept override{inner_->close();identities_.clear();endpoint_.clear();}
    const std::wstring&endpoint()const noexcept{return endpoint_;}
private:
    std::unique_ptr<AudioSessionBackend>inner_;AudioRouteJournal*journal_;std::function<void()>touched_;std::atomic<std::uint64_t>&writes_;
    std::wstring endpoint_;std::map<std::string,std::pair<std::string,std::string>,std::less<>>identities_;
    void recover(std::vector<AudioSessionRecord>&records){
        for(std::size_t n=journal_->recovered().size();n-->0;){
            const auto entry=journal_->recovered()[n];
            if(entry.endpoint!=endpoint_)continue;
            bool matched{},settled{true};
            for(auto&r:records){
                const bool identical=entry.persistent.empty()?r.id==entry.session:r.persistentID==entry.persistent;
                if(!identical||!r.controllable||!r.volume||journal_->liveEntry(r.id))continue;
                matched=true;
                // Anything but the recorded level is a later user/app choice.
                if(!same(*r.volume,entry.written))continue;
                float confirmed{};auto hr=call([&]{return inner_->writeVolume(r.id,entry.original);});
                if(hr>=0)hr=call([&]{return inner_->readVolume(r.id,confirmed);});
                if(hr>=0&&unit(confirmed)&&same(confirmed,entry.original))r.volume=confirmed;else settled=false;
            }
            if(matched&&settled){journal_->settle(n);touched_();}
        }
    }
};

struct Command {
    enum class Kind {volume,mute,balance,gain,stopApplication,stopApplications,suspend,resume,recover} kind;
    std::wstring endpoint;std::string id;float value{};bool flag{};
};
}

class AudioService::Impl {
public:
    AudioServiceOptions options;Notice notice;std::shared_ptr<Signal>signal=std::make_shared<Signal>();
    std::thread::id owner=std::this_thread::get_id();
    // Guarded by signal->mutex.
    std::vector<Command>queue;unsigned demand{};bool stopped{},started{};
    AudioEndpointSnapshot published;std::uint64_t publishedRevision{};
    std::thread thread;
    // Owner-only.
    AudioEndpointSnapshot current;std::uint64_t currentRevision{};unsigned votes{};
    // Statistics (any thread).
    std::atomic<std::uint64_t>wakes{},topologyReads{},volumeReads{},sessionReads{},publications{},journalWrites{};

    Impl(AudioServiceOptions o,Notice n):options(std::move(o)),notice(std::move(n)){
        if(!options.endpoints||!options.sessions)throw std::invalid_argument("Audio service requires injected endpoint and session backends");
        if(!std::isfinite(options.journalFlushSeconds)||options.journalFlushSeconds<0||options.journalFlushSeconds>10)throw std::invalid_argument("Invalid journal flush deadline");
        if(!std::isfinite(options.topologySettleSeconds)||options.topologySettleSeconds<0||options.topologySettleSeconds>10)throw std::invalid_argument("Invalid topology settle deadline");
        queue.reserve(64);
    }
    bool onOwner()const noexcept{return std::this_thread::get_id()==owner;}
    // Called with signal->mutex held.
    void ensureThread(){if(started||stopped)return;started=true;thread=std::thread([this]{run();});}
    bool enqueue(Command c){
        if(!onOwner())return false;
        {
            std::lock_guard lock(signal->mutex);if(stopped)return false;
            using K=Command::Kind;
            if(c.kind==K::volume||c.kind==K::mute||c.kind==K::balance)
                queue.erase(std::remove_if(queue.begin(),queue.end(),[&](const auto&q){return q.kind==c.kind;}),queue.end());
            else if(c.kind==K::gain)
                queue.erase(std::remove_if(queue.begin(),queue.end(),[&](const auto&q){return q.kind==K::gain&&q.id==c.id;}),queue.end());
            if(queue.size()>=maximumQueue)return false;
            queue.push_back(std::move(c));ensureThread();signal->flags|=commandBit;
        }
        signal->cv.notify_one();return true;
    }
    void publish(const AudioEndpointSnapshot&state){
        bool changed{};
        {std::lock_guard lock(signal->mutex);if(!(published==state)){published=state;++publishedRevision;changed=true;}}
        if(changed){++publications;if(notice)try{notice();}catch(...){}}
    }

    // ---- Worker-only state and steps ----
    struct Worker {
        Impl&impl;AudioEndpointSnapshot state;
        std::unique_ptr<AudioEndpointBackend>endpoints;bool endpointsOpen{};std::wstring bound;bool volumeBound{};
        std::set<std::wstring>refused;
        std::unique_ptr<JournaledSessions>sessions;bool resumed{};
        AudioSessionRoutes routes;std::vector<AudioSessionRecord>records;
        std::optional<AudioRouteJournal>journal;std::optional<Clock::time_point>flushAt,topologyAt;
        std::optional<std::vector<AudioTopologyDevice>>pendingTopology;
        unsigned demand{};
        explicit Worker(Impl&i):impl(i){
            if(impl.options.journal){
                journal.emplace(*impl.options.journal);
                // An unreadable journal blocks attenuation (see journal notes);
                // it is surfaced, never silently replaced.
                if(!journal->available())state.applicationError=journal->status();
            }
        }
        AudioRouteJournal*journalPointer(){return journal?&*journal:nullptr;}
        AudioEndpointBackend::Wake wake(unsigned bit){return [s=impl.signal,bit]{s->raise(bit);};}
        void touched(){if(journal&&journal->dirty()&&!flushAt)flushAt=Clock::now()+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(impl.options.journalFlushSeconds));}
        void flush(bool force){
            if(!journal||!journal->dirty())return;
            if(!force&&flushAt&&Clock::now()<*flushAt)return;
            flushAt.reset();++impl.journalWrites;const auto hr=journal->flush();
            if(hr<0)state.applicationError=hr;
        }
        bool ensureSessions(){
            if(sessions)return true;
            try{
                auto inner=impl.options.sessions();if(!inner)throw std::runtime_error("Missing audio session backend");
                sessions=std::make_unique<JournaledSessions>(std::move(inner),journalPointer(),[this]{touched();},impl.journalWrites);
                return true;
            }catch(...){state.applicationsSupported=false;state.applicationError=unexpected;return false;}
        }
        bool owned()const{const auto&apps=routes.applications();return std::any_of(apps.begin(),apps.end(),[](const auto&a){return a.state!=AudioApplicationRouteState::direct;});}
        void openEndpoints(){
            try{endpoints=impl.options.endpoints();if(!endpoints)throw std::runtime_error("Missing endpoint backend");}
            catch(...){state.error=unexpected;return;}
            const auto hr=call([&]{return endpoints->open(wake(topologyBit));});
            if(hr<0){state.error=hr;endpoints->close();endpoints.reset();return;}
            endpointsOpen=true;state.topologyActive=true;refreshTopology(false);
        }
        void closeEndpoints(){
            if(endpoints){endpoints->close();endpoints.reset();}
            endpointsOpen=false;bound.clear();volumeBound=false;state.topologyActive=false;clearControls();state.paused=true;
            topologyAt.reset();pendingTopology.reset(); // A closed listener publishes nothing late.
            // Nothing observed while closed is reported as current.
            state.topologyListed=false;state.topology.clear();
        }
        void clearControls(){state.available=false;state.volume.reset();state.balance.reset();state.muted.reset();state.canSetVolume=state.canSetMute=state.canSetBalance=false;}
        void applyTopology(){
            // The first complete list after the listener opens is a baseline
            // and always advances the generation, even when it is empty.
            if(pendingTopology&&(!state.topologyListed||*pendingTopology!=state.topology)){state.topology=std::move(*pendingTopology);++state.topologyGeneration;state.topologyListed=true;}
            pendingTopology.reset();
        }
        void settleTopology(){if(topologyAt&&Clock::now()>=*topologyAt){topologyAt.reset();applyTopology();}}
        // settle=false for the first read after registration (source start()).
        void refreshTopology(bool settle=true){
            if(!endpointsOpen)return;++impl.topologyReads;
            std::vector<AudioEndpointRecord>outputs,inputs;std::wstring defaultOutput,defaultInput;
            auto outputStatus=call([&]{return endpoints->enumerate(AudioFlow::output,outputs);});
            auto inputStatus=call([&]{return endpoints->enumerate(AudioFlow::input,inputs);});
            if(outputStatus>=0&&outputs.size()>maximumAudioEndpoints)outputStatus=overflow;
            if(inputStatus>=0&&inputs.size()>maximumAudioEndpoints)inputStatus=overflow;
            if(outputStatus>=0&&call([&]{return endpoints->defaultEndpoint(AudioFlow::output,defaultOutput);})<0)defaultOutput.clear();
            if(inputStatus>=0&&call([&]{return endpoints->defaultEndpoint(AudioFlow::input,defaultInput);})<0)defaultInput.clear();
            // Raw labels for the Event Log: a missing label is an incomplete
            // read (source watcher), while the UI shows the opaque identity.
            std::vector<AudioEndpoint>rawOutputs,rawInputs;
            const auto convert=[&](const std::vector<AudioEndpointRecord>&records,std::vector<AudioEndpoint>&raw,std::int32_t&status){
                std::vector<AudioEndpoint>ui;ui.reserve(records.size());std::set<std::wstring>ids;
                for(const auto&r:records){
                    if(!endpointToken(r.id)||r.name.size()>32767||r.name.find(L'\0')!=r.name.npos||!audioUtf8(r.id)||!audioUtf8(r.name)||!ids.insert(r.id).second){status=unexpected;return std::vector<AudioEndpoint>{};}
                    AudioEndpoint e{r.id,r.name,audioEndpointHeadphones(r.formFactor),audioEndpointBluetooth(r.enumerator)};
                    raw.push_back(e);if(e.name.empty())e.name=e.id;ui.push_back(std::move(e));
                }
                sortAudioEndpoints(ui,impl.options.order);return ui;
            };
            auto nextOutputs=outputStatus>=0?convert(outputs,rawOutputs,outputStatus):std::vector<AudioEndpoint>{};
            auto nextInputs=inputStatus>=0?convert(inputs,rawInputs,inputStatus):std::vector<AudioEndpoint>{};
            if(outputStatus<0)nextOutputs.clear();if(inputStatus<0)nextInputs.clear();
            const auto present=[](const std::vector<AudioEndpoint>&list,const std::wstring&id){return !id.empty()&&std::any_of(list.begin(),list.end(),[&](const auto&d){return d.id==id;});};
            state.outputs=std::move(nextOutputs);state.inputs=std::move(nextInputs);
            state.defaultOutputID=present(state.outputs,defaultOutput)?defaultOutput:std::wstring{};
            state.defaultInputID=present(state.inputs,defaultInput)?defaultInput:std::wstring{};
            state.inputError=inputStatus<0?inputStatus:0;
            if(outputStatus<0)state.error=outputStatus;else if(state.error==overflow||state.error==unexpected)state.error=0;
            for(auto it=refused.begin();it!=refused.end();)if(present(state.outputs,*it))++it;else it=refused.erase(it);
            // An incomplete read keeps the previous list: never a false disconnect.
            if(outputStatus>=0&&inputStatus>=0)
                if(auto topology=audioTopology(rawOutputs,rawInputs)){
                    pendingTopology=std::move(*topology);
                    if(settle)topologyAt=Clock::now()+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(impl.options.topologySettleSeconds));
                    else{topologyAt.reset();applyTopology();}
                }
        }
        void readVolume(){
            if(!volumeBound){clearControls();return;}
            ++impl.volumeReads;AudioEndpointVolumeState v;const auto hr=call([&]{return endpoints->read(v);});if(hr<0)v.status=hr;
            const auto c=audioEndpointControls(v,refused.count(bound)!=0);
            state.available=c.available;state.volume=c.volume;state.balance=c.balance;state.muted=c.muted;
            state.canSetVolume=c.canSetVolume;state.canSetMute=c.canSetMute;state.canSetBalance=c.canSetBalance;state.error=c.error;
        }
        void syncVolume(bool want){
            const std::wstring target=want&&endpointsOpen?state.defaultOutputID:std::wstring{};
            if(want&&endpointsOpen)state.paused=false;
            if(target==bound&&volumeBound==!target.empty()){if(!want){state.paused=true;clearControls();}return;}
            if(endpointsOpen){
                const auto hr=call([&]{return endpoints->bind(target,wake(volumeBit));});
                volumeBound=hr>=0&&!target.empty();bound=volumeBound?target:std::wstring{};
                if(hr<0)state.error=hr;
            }else{bound.clear();volumeBound=false;}
            if(!want){state.paused=true;clearControls();return;}
            state.commandError=0;
            if(target.empty()){clearControls();state.error=notFound;return;}
            readVolume();
        }
        void readSessions(){
            if(!sessions||!resumed)return;
            // A terminated process is reported once (its owned attenuation is
            // restored on the still-held sessions), then released by a second
            // read in the same wake so it is never listed.
            for(unsigned pass=0;pass<2;++pass){
                ++impl.sessionReads;
                const auto hr=call([&]{return sessions->read(records);});
                if(hr<0){state.applicationError=hr;break;}
                try{routes.update(records,*sessions);}catch(...){state.applicationError=unexpected;break;}
                if(std::none_of(records.begin(),records.end(),[](const auto&r){return r.exited;}))break;
            }
            touched();
        }
        void switchSessions(const std::wstring&target){
            if(!sessions->endpoint().empty()){
                if(owned()){
                    // Source: "App routing stopped because the output device changed."
                    const auto hr=call([&]{return routes.stopAll(*sessions);});
                    state.routesStoppedByDeviceChange=true;if(hr<0)state.applicationError=hr;
                }
                if(journal)journal->demote(sessions->endpoint());
                touched();
            }
            sessions->close();routes=AudioSessionRoutes();records.clear();resumed=false;
            if(target.empty())return;
            const auto hr=call([&]{return sessions->open(target,wake(sessionsBit));});
            resumed=hr>=0;state.applicationsSupported=resumed;state.applicationError=hr<0?hr:(journal&&!journal->available()?journal->status():0);
            if(resumed)readSessions();
        }
        void syncSessions(bool want){
            if(!want){
                if(sessions&&resumed){sessions->pause();resumed=false;}
                state.applicationsPaused=true;return;
            }
            state.applicationsPaused=false;
            const auto target=state.defaultOutputID;
            if(!ensureSessions())return;
            if(sessions->endpoint()!=target||(target.empty()&&resumed)){switchSessions(target);if(target.empty()){state.applicationsSupported=false;state.applicationError=notFound;}return;}
            if(target.empty()){state.applicationsSupported=false;state.applicationError=notFound;return;}
            if(!resumed){
                const auto hr=call([&]{return sessions->resume(wake(sessionsBit));});
                resumed=hr>=0;state.applicationsSupported=resumed;state.applicationError=hr<0?hr:(journal&&!journal->available()?journal->status():0);
                if(resumed)readSessions();
            }
        }
        void endpointCommand(const Command&c){
            using K=Command::Kind;
            if(!volumeBound||c.endpoint!=bound||c.endpoint!=state.defaultOutputID){state.commandError=deviceChanged;return;}
            std::int32_t hr{};
            if(c.kind==K::volume){
                if(!state.canSetVolume){state.commandError=unsupported;return;}
                hr=call([&]{return endpoints->setVolume(c.value);});
                if(audioEndpointWriteRefused(hr))refused.insert(bound);
            }else if(c.kind==K::mute){
                if(!state.canSetMute){state.commandError=unsupported;return;}
                hr=call([&]{return endpoints->setMute(c.flag);});
            }else{
                if(!state.canSetBalance){state.commandError=unsupported;return;}
                auto*backend=endpoints.get();
                hr=apply_audio_stereo_balance(c.value,{[backend](unsigned n,float&v){return call([&]{return backend->readChannel(n,v);});},[backend](unsigned n,float v){return call([&]{return backend->writeChannel(n,v);});}});
            }
            // Always publish the actual readback, never an optimistic value.
            readVolume();state.commandError=hr<0?hr:0;
        }
        void applicationCommand(const Command&c){
            using K=Command::Kind;
            if(c.kind==K::gain){
                state.routesStoppedByDeviceChange=false;
                if(!sessions||!resumed||!state.applicationsSupported){state.applicationError=pending;return;}
                state.applicationError=call([&]{return routes.setGain(c.id,c.value,*sessions);});
            }else if(c.kind==K::stopApplication){
                state.routesStoppedByDeviceChange=false;
                if(!sessions)return;state.applicationError=call([&]{return routes.stop(c.id,*sessions);});
            }else{
                if(!sessions)return;const auto hr=call([&]{return routes.stopAll(*sessions);});
                if(c.kind==K::stopApplications)state.routesStoppedByDeviceChange=false;
                state.applicationError=hr;
            }
            touched();
        }
        void recover(){
            if(!journal||!journal->available())return;
            std::set<std::wstring>endpointsToVisit;for(const auto&e:journal->recovered())endpointsToVisit.insert(e.endpoint);
            for(const auto&endpoint:endpointsToVisit){
                if(sessions&&resumed&&sessions->endpoint()==endpoint){readSessions();continue;}
                // A short, explicit pass: open, read (recovers), close.
                try{
                    auto inner=impl.options.sessions();if(!inner)continue;
                    JournaledSessions pass(std::move(inner),journalPointer(),[this]{touched();},impl.journalWrites);
                    if(call([&]{return pass.open(endpoint,[]{});})>=0){std::vector<AudioSessionRecord>scratch;(void)call([&]{return pass.read(scratch);});}
                    pass.close();
                }catch(...){}
            }
            touched();
        }
        void step(unsigned flags,unsigned nextDemand,std::vector<Command>&commands){
            using K=Command::Kind;
            // Owned attenuation keeps the device-list listener alive even with
            // no vote, so an output change still stops it (source: routes stop
            // on relevant device changes while Volume or the HUD is hidden).
            demand=nextDemand;const bool listen=demand||owned();
            const bool opening=listen&&!endpointsOpen;
            if(opening)openEndpoints();
            else if(!listen&&endpointsOpen)closeEndpoints();
            bool topology=(flags&topologyBit)&&!opening;
            for(const auto&c:commands)if(c.kind==K::resume)topology=true;
            if(topology&&endpointsOpen)refreshTopology();
            if(sessions&&!(demand&tierApplications)&&!sessions->endpoint().empty()&&sessions->endpoint()!=state.defaultOutputID&&owned()){
                const auto hr=call([&]{return routes.stopAll(*sessions);});
                state.routesStoppedByDeviceChange=true;if(hr<0)state.applicationError=hr;
                touched(); // Failed restores stay owned here and remain retryable.
            }
            syncVolume((demand&tierEndpoints)!=0);
            if((flags&volumeBit)&&volumeBound)readVolume();
            syncSessions((demand&tierApplications)!=0);
            bool sessionsRead=(flags&sessionsBit)!=0;
            for(const auto&c:commands){
                switch(c.kind){
                case K::volume:case K::mute:case K::balance:endpointCommand(c);break;
                case K::gain:case K::stopApplication:case K::stopApplications:applicationCommand(c);sessionsRead=true;break;
                case K::suspend:{const Command all{K::suspend,{},{},0,false};applicationCommand(all);sessionsRead=true;break;}
                case K::resume:break;
                case K::recover:recover();sessionsRead=true;break;
                }
            }
            if(sessionsRead&&resumed)readSessions();
            // The listener kept only for owned routes ends with the last route.
            if(!demand&&endpointsOpen&&!owned())closeEndpoints();
            state.applications=routes.applications();sortAudioApplications(state.applications,impl.options.order);
            if(sessions&&!sessions->endpoint().empty()&&resumed)state.applicationsSupported=true;
            if(!resumed&&!(demand&tierApplications))state.applicationsSupported=false;
        }
        void shutdown()noexcept{
            try{
                if(sessions){sessions->pause();(void)call([&]{return routes.stopAll(*sessions);});sessions->close();sessions.reset();}
                flush(true);
            }catch(...){}
            try{closeEndpoints();}catch(...){}
        }
    };

    void run(){
        Worker w(*this);
        for(;;){
            unsigned flags{},nextDemand{};std::vector<Command>commands;
            {
                std::unique_lock lock(signal->mutex);
                // One-shot deadlines only (journal flush, device-list settle);
                // otherwise sleep until a notification or command.
                while(!signal->flags){
                    auto deadline=w.flushAt;if(w.topologyAt&&(!deadline||*w.topologyAt<*deadline))deadline=w.topologyAt;
                    if(deadline){if(signal->cv.wait_until(lock,*deadline)==std::cv_status::timeout&&!signal->flags)break;}
                    else signal->cv.wait(lock);
                }
                flags=std::exchange(signal->flags,0u);commands.swap(queue);nextDemand=demand;
            }
            if(flags&stopBit)break;
            ++wakes;
            try{w.step(flags,nextDemand,commands);}catch(...){w.state.error=unexpected;}
            // A failed flush is retried by the next journal event or at
            // shutdown, never by a repeating deadline.
            w.flush(false);w.settleTopology();
            publish(w.state);
        }
        w.shutdown();
    }
};

AudioService::AudioService(AudioServiceOptions options,Notice notice):impl_(std::make_shared<Impl>(std::move(options),std::move(notice))){}
AudioService::~AudioService(){stop();}
std::int32_t AudioService::vote(AudioVoter voter,bool active){
    auto&i=*impl_;if(!i.onOwner())return static_cast<std::int32_t>(0x8001010Eu); // RPC_E_WRONG_THREAD
    const auto bit=static_cast<unsigned>(voter);if(bit!=1&&bit!=2&&bit!=4)return invalidValue;
    const auto next=active?(i.votes|bit):(i.votes&~bit);if(next==i.votes)return 1; // S_FALSE
    const auto previous=demandFor(i.votes);i.votes=next;const auto wanted=demandFor(next);
    if(wanted==previous)return 0;
    {
        std::lock_guard lock(i.signal->mutex);if(i.stopped)return pending;
        i.demand=wanted;i.ensureThread();i.signal->flags|=commandBit;
    }
    i.signal->cv.notify_one();return 0;
}
unsigned AudioService::votes()const noexcept{return impl_->votes;}
bool AudioService::setVolume(std::wstring endpoint,float value){if(!endpointToken(endpoint)||!unit(value))return false;return impl_->enqueue({Command::Kind::volume,std::move(endpoint),{},value,false});}
bool AudioService::setMute(std::wstring endpoint,bool value){if(!endpointToken(endpoint))return false;return impl_->enqueue({Command::Kind::mute,std::move(endpoint),{},0,value});}
bool AudioService::setBalance(std::wstring endpoint,float value){if(!endpointToken(endpoint)||!std::isfinite(value)||value< -1||value>1)return false;return impl_->enqueue({Command::Kind::balance,std::move(endpoint),{},value,false});}
bool AudioService::setApplicationGain(std::string id,float value){if(!applicationToken(id)||!unit(value))return false;return impl_->enqueue({Command::Kind::gain,{},std::move(id),value,false});}
bool AudioService::stopApplication(std::string id){if(!applicationToken(id))return false;return impl_->enqueue({Command::Kind::stopApplication,{},std::move(id),0,false});}
bool AudioService::stopApplications(){return impl_->enqueue({Command::Kind::stopApplications,{},{},0,false});}
bool AudioService::powerSuspend(){return impl_->enqueue({Command::Kind::suspend,{},{},0,false});}
bool AudioService::powerResume(){return impl_->enqueue({Command::Kind::resume,{},{},0,false});}
bool AudioService::recover(){if(!impl_->options.journal)return false;return impl_->enqueue({Command::Kind::recover,{},{},0,false});}
bool AudioService::drain(){
    auto&i=*impl_;if(!i.onOwner())return false;
    std::lock_guard lock(i.signal->mutex);if(i.currentRevision==i.publishedRevision)return false;
    i.current=i.published;i.currentRevision=i.publishedRevision;return true;
}
const AudioEndpointSnapshot&AudioService::snapshot()const noexcept{return impl_->current;}
std::uint64_t AudioService::revision()const noexcept{return impl_->currentRevision;}
AudioServiceStats AudioService::stats()const{
    const auto&i=*impl_;AudioServiceStats s{i.wakes.load(),i.topologyReads.load(),i.volumeReads.load(),i.sessionReads.load(),i.publications.load(),i.journalWrites.load(),false};
    {std::lock_guard lock(i.signal->mutex);s.workerStarted=i.started;}return s;
}
void AudioService::stop()noexcept{
    auto&i=*impl_;
    {std::lock_guard lock(i.signal->mutex);if(i.stopped)return;i.stopped=true;i.queue.clear();i.signal->flags|=stopBit;}
    i.signal->cv.notify_one();
    if(i.thread.joinable())i.thread.join();
}
} // namespace endfield::native
