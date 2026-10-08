#include "audio_session_worker.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace endfield::native {
bool validAudioApplicationExecutable(const AudioApplicationExecutable&e)noexcept{
    return ehud::data::validWindowsFilePath(e.path)&&(!e.identity.volumeUUID||ehud::data::validUUID(*e.identity.volumeUUID));
}
namespace {
constexpr std::int32_t invalid=static_cast<std::int32_t>(0x80070057u),unexpected=static_cast<std::int32_t>(0x8000ffffu),notFound=static_cast<std::int32_t>(0x80070490u),changedState=static_cast<std::int32_t>(0x8000000cu);
bool scalar(float v){return std::isfinite(v)&&v>=0&&v<=1;}
bool same(float a,float b){return std::abs(a-b)<=2e-6f;}
bool identity(std::string_view s){return !s.empty()&&s.size()<=4096&&s.find('\0')==s.npos;}
template<class F>std::int32_t call(F&&f)noexcept{try{return f();}catch(...){return unexpected;}}
}
class AudioSessionRoutes::Impl {
public:
    struct Member{std::string id;float baseline{},expected{};bool touched{};};
    struct Owned{float gain{1};std::int32_t error{};std::vector<Member>members;};
    std::vector<AudioSessionRecord>records;
    std::map<std::string,Owned,std::less<>>owned;
    std::vector<AudioApplicationRoute>apps;
    const AudioSessionRecord*record(std::string_view id)const{const auto i=std::find_if(records.begin(),records.end(),[&](const auto&r){return r.id==id;});return i==records.end()?nullptr:&*i;}
    void rebuild(){
        struct Group{std::size_t index{};bool active{},writable{true},iconConflict{};};std::map<std::string,Group,std::less<>>indices;std::vector<AudioApplicationRoute>next;
        for(const auto&r:records){if(r.processKey.empty()||!r.pid)continue;auto found=indices.find(r.processKey);
            if(found==indices.end()){indices.emplace(r.processKey,Group{next.size(),r.active,r.controllable&&r.volume.has_value()});next.push_back({r.processKey,r.name,r.pid,false,AudioApplicationRouteState::direct,{},r.error,r.executable});}
            else{auto&group=found->second;group.active=group.active||r.active;group.writable=group.writable&&r.controllable&&r.volume.has_value();if(r.error<0)next[group.index].error=r.error;
                auto&app=next[group.index];if(!group.iconConflict&&r.executable){if(app.executable&&*app.executable!=*r.executable){app.executable.reset();group.iconConflict=true;}else app.executable=r.executable;}}
        }
        for(const auto&[_,group]:indices)next[group.index].available=group.active&&group.writable;
        for(auto&a:next)if(const auto found=owned.find(a.id);found!=owned.end()){a.state=found->second.error<0?AudioApplicationRouteState::failed:AudioApplicationRouteState::active;a.gain=found->second.gain;a.error=found->second.error;}
        next.erase(std::remove_if(next.begin(),next.end(),[](const auto&a){return !a.available&&a.state==AudioApplicationRouteState::direct;}),next.end());apps=std::move(next);
    }
    std::int32_t apply(Owned&o,float gain,AudioSessionBackend&backend){
        struct Stage{Member*member;float original{},next{};};std::vector<Stage>stages;stages.reserve(o.members.size());
        for(auto&m:o.members){const auto*r=record(m.id);if(!r||!r->controllable)return changedState;float value{};const auto hr=call([&]{return backend.readVolume(m.id,value);});if(hr<0)return hr;if(!scalar(value))return unexpected;if(!same(value,m.expected))return changedState;stages.push_back({&m,value,m.baseline*gain});}
        std::size_t attempted{};std::int32_t hr{};
        for(auto&s:stages){++attempted;if(s.original==s.next)continue;hr=call([&]{return backend.writeVolume(s.member->id,s.next);});if(hr<0)break;float actual{};hr=call([&]{return backend.readVolume(s.member->id,actual);});if(hr<0)break;if(!scalar(actual)||!same(actual,s.next)){hr=unexpected;break;}s.member->expected=actual;s.member->touched=true;}
        if(hr>=0){o.gain=gain;o.error=0;for(const auto&s:stages)for(auto&r:records)if(r.id==s.member->id)r.volume=s.member->expected;return 0;}
        const auto failure=hr;bool restored=true;
        while(attempted){auto&s=stages[--attempted];if(s.original==s.next)continue;const auto rollback=call([&]{return backend.writeVolume(s.member->id,s.original);});float actual{};const auto readback=call([&]{return backend.readVolume(s.member->id,actual);});
            if(rollback<0||readback<0||!scalar(actual)||!same(actual,s.original))restored=false;
            if(readback>=0&&scalar(actual)&&(same(actual,s.next)||same(actual,s.original))){s.member->expected=actual;s.member->touched=true;}
        }
        return restored?failure:unexpected;
    }
};
AudioSessionRoutes::AudioSessionRoutes():impl_(std::make_unique<Impl>()){}
AudioSessionRoutes::~AudioSessionRoutes()=default;
AudioSessionRoutes::AudioSessionRoutes(AudioSessionRoutes&&)noexcept=default;
AudioSessionRoutes&AudioSessionRoutes::operator=(AudioSessionRoutes&&)noexcept=default;
const std::vector<AudioApplicationRoute>&AudioSessionRoutes::applications()const noexcept{return impl_->apps;}
void AudioSessionRoutes::update(std::span<const AudioSessionRecord>records,AudioSessionBackend&backend){
    if(records.size()>maximumSessions)throw std::invalid_argument("Audio session budget exceeded");std::map<std::string,std::uint32_t,std::less<>>processes;std::map<std::string,bool,std::less<>>ids;std::size_t bytes{};
    for(const auto&r:records){if(!identity(r.id)||(!r.processKey.empty()&&!identity(r.processKey))||r.name.size()>4096||r.name.find(L'\0')!=r.name.npos||(!r.processKey.empty()&&!r.pid)||(!r.processKey.empty()&&!processes.emplace(r.processKey,r.pid).second&&processes.at(r.processKey)!=r.pid)||!ids.emplace(r.id,true).second||(r.volume&&!scalar(*r.volume))||(r.executable&&(r.processKey.empty()||!validAudioApplicationExecutable(*r.executable))))throw std::invalid_argument("Invalid native audio session identity/state");bytes+=r.id.size()+r.processKey.size()+r.name.size()*sizeof(wchar_t);if(r.executable)bytes+=r.executable->path.size()+(r.executable->identity.volumeUUID?r.executable->identity.volumeUUID->size():0);if(bytes>8*1024*1024)throw std::invalid_argument("Audio session metadata budget exceeded");}
    auto&i=*impl_;for(const auto&r:records)if(const auto*previous=i.record(r.id);previous&&(previous->processKey!=r.processKey||previous->pid!=r.pid))throw std::invalid_argument("Native audio session identity was reassigned");i.records.assign(records.begin(),records.end());
    for(auto owned=i.owned.begin();owned!=i.owned.end();){auto&o=owned->second;
        o.members.erase(std::remove_if(o.members.begin(),o.members.end(),[&](const auto&m){return !i.record(m.id);}),o.members.end());
        if(o.members.empty()){owned=i.owned.erase(owned);continue;}
        if(o.error>=0){for(const auto&m:o.members){const auto*r=i.record(m.id);if(!r->controllable||!r->volume||!same(*r->volume,m.expected)){o.error=changedState;break;}}
            if(o.error>=0){bool added=false;for(const auto&r:i.records)if(r.processKey==owned->first&&std::none_of(o.members.begin(),o.members.end(),[&](const auto&m){return m.id==r.id;})){if(!r.controllable||!r.volume){o.error=changedState;break;}o.members.push_back({r.id,*r.volume,*r.volume,false});added=true;}
                if(added&&o.error>=0)o.error=i.apply(o,o.gain,backend);}}
        ++owned;
    }
    i.rebuild();
}
std::int32_t AudioSessionRoutes::setGain(std::string_view id,float gain,AudioSessionBackend&backend){
    if(!identity(id)||!scalar(gain))return invalid;auto&i=*impl_;const auto app=std::find_if(i.apps.begin(),i.apps.end(),[&](const auto&a){return a.id==id;});if(app==i.apps.end())return notFound;
    auto found=i.owned.find(id);if(found==i.owned.end()){
        if(gain==1)return 0;if(!app->available||i.owned.size()>=maximumApplications)return changedState;Impl::Owned next;
        for(const auto&r:i.records)if(r.processKey==id){if(!r.controllable||!r.volume)return changedState;next.members.push_back({r.id,*r.volume,*r.volume,false});}
        if(next.members.empty())return notFound;found=i.owned.emplace(std::string(id),std::move(next)).first;
    }else if(found->second.error<0)return gain==1?stop(id,backend):changedState;
    auto&o=found->second;if(o.gain==gain&&o.error>=0)return 0;const auto hr=i.apply(o,gain,backend);o.gain=gain;o.error=hr;i.rebuild();return hr;
}
std::int32_t AudioSessionRoutes::stop(std::string_view id,AudioSessionBackend&backend){
    auto&i=*impl_;auto found=i.owned.find(id);if(found==i.owned.end())return 0;auto&o=found->second;std::int32_t failure{};
    for(auto m=o.members.begin();m!=o.members.end();){if(!m->touched){m=o.members.erase(m);continue;}float actual{};const auto readback=call([&]{return backend.readVolume(m->id,actual);});
        if(readback<0){if(!failure)failure=readback;++m;continue;}
        // An external owner changed this session: relinquish our claim without
        // writing the remembered original over the user's newer mixer choice.
        if(!scalar(actual)||!same(actual,m->expected)){m=o.members.erase(m);continue;}
        auto hr=call([&]{return backend.writeVolume(m->id,m->baseline);});float confirmed{};if(hr>=0)hr=call([&]{return backend.readVolume(m->id,confirmed);});if(hr>=0&&(!scalar(confirmed)||!same(confirmed,m->baseline)))hr=unexpected;
        if(hr<0){if(!failure)failure=hr;++m;continue;}for(auto&r:i.records)if(r.id==m->id)r.volume=confirmed;m=o.members.erase(m);
    }
    if(o.members.empty())i.owned.erase(found);else o.error=failure?failure:unexpected;i.rebuild();return failure;
}
std::int32_t AudioSessionRoutes::stopAll(AudioSessionBackend&backend){std::vector<std::string>ids;for(const auto&[id,_]:impl_->owned)ids.push_back(id);std::int32_t error{};for(const auto&id:ids){const auto hr=stop(id,backend);if(hr<0&&!error)error=hr;}return error;}

class AudioSessionWorker::Impl {
public:
    enum class Kind{activate,pause,gain,stopApplication,stopApplications};struct Command{Kind kind;std::wstring endpoint;std::string id;float gain{};};
    struct WakeState{std::atomic<unsigned>flags{};std::atomic<bool>alive{true};void wake(unsigned bit)noexcept{if(alive.load(std::memory_order_acquire)){flags.fetch_or(bit,std::memory_order_release);flags.notify_one();}}};
    Factory factory;Notice notice;std::shared_ptr<WakeState>wake=std::make_shared<WakeState>();std::thread thread;std::mutex mutex;std::vector<Command>queue;AudioSessionSnapshot published;std::uint64_t taken{};bool stopped{};
    Impl(Factory f,Notice n):factory(std::move(f)),notice(std::move(n)){if(!factory)throw std::invalid_argument("Inject audio session backend");queue.reserve(512);}
    bool enqueue(Command c){std::lock_guard lock(mutex);if(stopped||!wake->alive.load(std::memory_order_acquire))return false;
        if(c.kind==Kind::gain)for(auto q=queue.rbegin();q!=queue.rend()&&q->kind==Kind::gain;++q)if(q->id==c.id){q->gain=c.gain;wake->wake(1);return true;}
        if(queue.size()>=512)return false;queue.push_back(std::move(c));if(!thread.joinable())thread=std::thread([this]{try{run();}catch(...){wake->alive.store(false,std::memory_order_release);try{AudioSessionSnapshot failure;failure.error=unexpected;publish(std::move(failure));}catch(...){}}});wake->wake(1);return true;
    }
    void publish(AudioSessionSnapshot next){bool changed{};{std::lock_guard lock(mutex);next.revision=published.revision;if(next!=published){next.revision=published.revision+1;published=std::move(next);changed=true;}}
        if(changed&&notice)try{notice();}catch(...){}
    }
    void run(){
        std::unique_ptr<AudioSessionBackend>backend;AudioSessionRoutes routes;AudioSessionSnapshot state;std::vector<AudioSessionRecord>records;std::vector<Command>commands;commands.reserve(512);
        struct Cleanup{std::unique_ptr<AudioSessionBackend>&backend;AudioSessionRoutes&routes;~Cleanup(){if(backend){backend->pause();try{(void)routes.stopAll(*backend);}catch(...){}backend->close();}}}cleanup{backend,routes};
        const auto wakeCallback=[w=wake]{w->wake(2);};
        try{backend=factory();if(!backend)throw std::runtime_error("Missing audio session backend");}catch(...){state.error=unexpected;publish(state);}
        for(;;){const auto flags=wake->flags.exchange(0,std::memory_order_acq_rel);if(flags&4)break;if(!flags){wake->flags.wait(0,std::memory_order_acquire);continue;}
            {std::lock_guard lock(mutex);commands.swap(queue);}
            bool refresh=(flags&2)&&!state.paused;
            for(auto&c:commands)try{
                if(!backend){backend=factory();if(!backend)throw std::runtime_error("Missing audio session backend");}
                if(c.kind==Kind::activate){const bool changed=state.endpoint!=c.endpoint;if(changed){state.error=routes.stopAll(*backend);if(state.error<0){refresh=false;continue;}backend->close();routes=AudioSessionRoutes();records.clear();state.endpoint=std::move(c.endpoint);state.error=call([&]{return backend->open(state.endpoint,wakeCallback);});}
                    else state.error=call([&]{return backend->resume(wakeCallback);});state.paused=false;state.supported=state.error>=0;refresh=true;}
                else if(c.kind==Kind::pause){backend->pause();state.paused=true;refresh=false;}
                else if(c.kind==Kind::gain){if(!state.paused&&state.supported)state.error=routes.setGain(c.id,c.gain,*backend);refresh=true;}
                else if(c.kind==Kind::stopApplication){state.error=routes.stop(c.id,*backend);refresh=!state.paused;}
                else{state.error=routes.stopAll(*backend);refresh=!state.paused;}
            }catch(...){state.error=unexpected;state.supported=false;}
            commands.clear();
            if(refresh&&backend&&!state.paused&&state.supported){const auto hr=call([&]{return backend->read(records);});if(hr>=0)try{routes.update(records,*backend);}catch(...){state.error=unexpected;state.supported=false;}else state.error=hr;}
            state.applications=routes.applications();publish(state);
        }
        wake->alive.store(false,std::memory_order_release);
    }
};
AudioSessionWorker::AudioSessionWorker(Factory factory,Notice notice):impl_(std::make_unique<Impl>(std::move(factory),std::move(notice))){}
AudioSessionWorker::~AudioSessionWorker(){stop();}
bool AudioSessionWorker::activate(std::wstring endpoint){if(endpoint.empty()||endpoint.size()>32767||endpoint.find(L'\0')!=endpoint.npos)return false;return impl_->enqueue({Impl::Kind::activate,std::move(endpoint),{},0});}
bool AudioSessionWorker::pause(){return impl_->enqueue({Impl::Kind::pause,{},{},0});}
bool AudioSessionWorker::setGain(std::string id,float gain){return identity(id)&&scalar(gain)&&impl_->enqueue({Impl::Kind::gain,{},std::move(id),gain});}
bool AudioSessionWorker::stopApplication(std::string id){return identity(id)&&impl_->enqueue({Impl::Kind::stopApplication,{},std::move(id),0});}
bool AudioSessionWorker::stopApplications(){return impl_->enqueue({Impl::Kind::stopApplications,{},{},0});}
bool AudioSessionWorker::takeSnapshot(AudioSessionSnapshot&out){std::lock_guard lock(impl_->mutex);if(impl_->taken==impl_->published.revision)return false;out=impl_->published;impl_->taken=out.revision;return true;}
void AudioSessionWorker::stop()noexcept{auto&i=*impl_;{std::lock_guard lock(i.mutex);if(i.stopped)return;i.stopped=true;i.queue.clear();i.wake->flags.fetch_or(4,std::memory_order_release);i.wake->flags.notify_one();}if(i.thread.joinable())i.thread.join();i.wake->alive=false;}
} // namespace endfield::native
