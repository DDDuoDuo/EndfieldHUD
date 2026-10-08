#include "native/notes_image_decoder.hpp"
#ifdef _WIN32
#include "core/data/data_store.hpp"
#include <process.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
namespace endfield::native {
namespace detail {std::unique_ptr<NativeNotesImageDecoder::Sequence>makeNotesWICSequence(const NotesImageRequest&,NotesImageAccess);}
namespace {
using Provider=NativeNotesImageDecoder;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
struct Handle {HANDLE value{};~Handle(){if(value)CloseHandle(value);}Handle()=default;Handle(const Handle&)=delete;};
// Intentionally process-lifetime gate, safe if a codec outlives static teardown.
std::atomic<bool>&workerGate(){static auto*p=new std::atomic<bool>{false};return *p;}
void routeValid(NotesImageRoute r){if(!r.owner){need(!r.message&&!r.generation,"Empty image route must be fully empty");return;}
    DWORD process{};need(IsWindow(r.owner)&&GetWindowThreadProcessId(r.owner,&process)==GetCurrentThreadId()&&process==GetCurrentProcessId(),"Image route must be an owned same-thread window");
    need(r.message>=WM_APP&&r.message<0xC000&&r.generation,"Invalid image completion route");}
void valid(const NotesImageRequest&r){need(!r.key.empty()&&r.key.size()<=512&&r.key.find('\0')==std::string::npos&&ehud::data::Json::validUtf8(r.key)&&r.revision,"Invalid image request identity");need(ehud::data::validWindowsFilePath(r.path),"Image request requires explicit native path");}
void valid(const NotesImageInfo&i){need(i.pixelWidth&&i.pixelHeight&&i.pixelWidth<=65536&&i.pixelHeight<=65536&&std::uint64_t(i.pixelWidth)*i.pixelHeight<=Provider::maximumInputPixels,"Image dimensions exceed source bounds");
    need(i.frameCount&&i.frameCount<=Provider::maximumGIFFrames,"Image frame count exceeds source bounds");
    need(i.kind==modules::NotesMediaKind::image||i.kind==modules::NotesMediaKind::gif,"Image worker cannot decode video");
    need(i.kind!=modules::NotesMediaKind::image||(i.frameCount==1&&i.frameDelays.empty()&&i.duration==0),"Still image has animation metadata");
    if(i.kind==modules::NotesMediaKind::gif){need(i.frameDelays.size()==i.frameCount,"GIF delays do not match frame count");double duration{};for(auto delay:i.frameDelays){need(std::isfinite(delay)&&delay>=.04&&delay<=600,"Invalid GIF delay");duration+=delay;}need(duration==i.duration,"GIF duration does not match source delay sum");}}
struct Budget {std::atomic<std::size_t>bytes{};};
std::shared_ptr<const NotesImageFrame>publish(NotesImageFrame frame,const NotesImageRequest&r,unsigned index,const std::shared_ptr<Budget>&budget){
    const auto bound=modules::NotesMediaLayout::decodeDimension(r.maximumDimension);need(frame.width&&frame.height&&frame.width<=bound&&frame.height<=bound&&frame.index==index&&frame.straightRGBA.size()==std::size_t(frame.width)*frame.height*4,"Invalid bounded decoded frame");
    const auto bytes=frame.straightRGBA.size(),previous=budget->bytes.fetch_add(bytes,std::memory_order_relaxed);
    if(previous>Provider::maximumLiveFrameBytes-bytes){budget->bytes.fetch_sub(bytes,std::memory_order_relaxed);throw std::length_error("Borrowed media frame budget exhausted");}
    std::unique_ptr<NotesImageFrame>p;try{p=std::make_unique<NotesImageFrame>(std::move(frame));}catch(...){budget->bytes.fetch_sub(bytes,std::memory_order_relaxed);throw;}
    return std::shared_ptr<const NotesImageFrame>(p.release(),[budget,bytes](const NotesImageFrame*value){delete value;budget->bytes.fetch_sub(bytes,std::memory_order_relaxed);});
}
struct Job {NotesImageRequest request;unsigned index{};std::uint64_t generation{};bool reopen{};};
struct State {
    std::mutex mutex;Handle event;Provider::Resolver resolver;Provider::Factory factory;NotesImageRoute route;
    std::vector<NotesImageRequest>visible;std::vector<Job>jobs;std::optional<Job>active;std::array<std::optional<NotesImageCompletion>,8>ready;
    std::shared_ptr<Budget>budget=std::make_shared<Budget>();NotesImageDecoderStats stats;std::uint64_t generation{};
    bool stopping{},noticePosted{};
    State(Provider::Resolver r,Provider::Factory f):resolver(std::move(r)),factory(std::move(f)){event.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event.value)throw std::runtime_error("Cannot create image worker event");visible.reserve(8);jobs.reserve(8);}
    void clearReady(){for(auto&r:ready)r.reset();noticePosted=false;stats.completed=0;}
    void notice(){if(!stopping&&route.owner&&!noticePosted&&stats.completed){if(PostMessageW(route.owner,route.message,route.generation,0)){noticePosted=true;++stats.notices;}}}
};
struct Decoder {
    NotesImageRequest request;std::unique_ptr<Provider::Sequence>sequence;std::shared_ptr<const NotesImageInfo>info;
    std::array<std::shared_ptr<const NotesImageFrame>,3>cache;std::array<std::uint64_t,3>ages{};std::uint64_t age{};
    std::shared_ptr<const NotesImageFrame>frame(unsigned index,const std::shared_ptr<Budget>&budget,bool&decoded){
        need(index<info->frameCount,"GIF frame index outside decoded sequence");for(std::size_t n=0;n<cache.size();++n)if(cache[n]&&cache[n]->index==index){ages[n]=++age;return cache[n];}
        auto value=publish(sequence->decode(index),request,index,budget);decoded=true;const auto slot=std::size_t(std::min_element(ages.begin(),ages.end())-ages.begin());cache[slot]=value;ages[slot]=++age;return value;
    }
};
unsigned __stdcall worker(void*raw){std::unique_ptr<std::shared_ptr<State>>argument(static_cast<std::shared_ptr<State>*>(raw));auto state=*argument;argument.reset();const auto apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    try{if(FAILED(apartment))throw std::runtime_error("Image worker COM initialization failed");std::map<std::string,Decoder,std::less<>>decoders;
        for(;;){if(WaitForSingleObject(state->event.value,INFINITE)!=WAIT_OBJECT_0)break;{std::lock_guard lock(state->mutex);++state->stats.wakeups;}
            for(;;){std::optional<Job>job;std::vector<NotesImageRequest>visible;bool stop{};
                {std::lock_guard lock(state->mutex);stop=state->stopping;visible=state->visible;if(!stop&&!state->jobs.empty()){job=std::move(state->jobs.front());state->jobs.erase(state->jobs.begin());state->active=job;state->stats.inFlight=true;}}
                if(stop)break;
                for(auto it=decoders.begin();it!=decoders.end();){const auto match=std::find(visible.begin(),visible.end(),it->second.request);if(match==visible.end())it=decoders.erase(it);else ++it;}
                {std::lock_guard lock(state->mutex);state->stats.decoders=decoders.size();state->stats.cachedFrames=0;for(const auto&[key,d]:decoders){(void)key;for(const auto&f:d.cache)if(f)++state->stats.cachedFrames;}}
                if(!job)break;
                NotesImageCompletion result;result.key=job->request.key;result.revision=job->request.revision;bool decoded{};
                try{if(job->reopen)decoders.erase(job->request.key);auto it=decoders.find(job->request.key);if(it==decoders.end()){
                        Decoder d;d.request=job->request;auto access=state->resolver(job->request);need(!access.path.empty(),"Media access resolver returned no path");
                        d.sequence=state->factory?state->factory(job->request,std::move(access)):detail::makeNotesWICSequence(job->request,std::move(access));need(bool(d.sequence),"Image factory returned no decoder");valid(d.sequence->info());d.info=std::make_shared<const NotesImageInfo>(d.sequence->info());auto key=d.request.key;it=decoders.emplace(std::move(key),std::move(d)).first;
                    }
                    result.info=it->second.info;result.frame=it->second.frame(job->index,state->budget,decoded);result.result=S_OK;
                    // Still images retain only their immutable bounded pixels,
                    // never the original stream/access lease after first load.
                    if(result.info->kind==modules::NotesMediaKind::image)it->second.sequence.reset();
                }catch(const std::bad_alloc&){result.result=E_OUTOFMEMORY;}catch(...){result.result=E_FAIL;}
                {std::lock_guard lock(state->mutex);state->stats.inFlight=false;state->active.reset();if(decoded)++state->stats.decodes;
                    if(state->stopping||job->generation!=state->generation){++state->stats.discarded;}else{
                        auto slot=std::find_if(state->ready.begin(),state->ready.end(),[&](const auto&r){return r&&r->key==result.key;});if(slot==state->ready.end())slot=std::find_if(state->ready.begin(),state->ready.end(),[](const auto&r){return !r;});
                        if(slot!=state->ready.end()){if(!*slot)++state->stats.completed;*slot=std::move(result);state->notice();}
                    }}
            }
            {std::lock_guard lock(state->mutex);if(state->stopping)break;}
        }
    }catch(...){/* No codec exception crosses into a UI callback. */}
    {std::lock_guard lock(state->mutex);state->route={};state->jobs.clear();state->visible.clear();state->active.reset();state->clearReady();state->stats.inFlight=false;state->stats.decoders=0;state->stats.cachedFrames=0;state->stopping=true;}
    state->factory={};state->resolver={};if(SUCCEEDED(apartment))CoUninitialize();{std::lock_guard lock(state->mutex);state->stats.stopped=true;}state.reset();workerGate().store(false,std::memory_order_release);return 0;
}
}
struct NativeNotesImageDecoder::Impl {DWORD owner=GetCurrentThreadId();std::shared_ptr<State>state;Handle worker;void onThread()const{need(owner==GetCurrentThreadId(),"Image decoder facade requires owner thread");}};
NativeNotesImageDecoder::NativeNotesImageDecoder(Resolver resolver,NotesImageRoute route,Factory factory):impl_(std::make_unique<Impl>()){
    need(bool(resolver),"Image decoder requires independent access resolver");routeValid(route);bool expected{};need(workerGate().compare_exchange_strong(expected,true),"Previous image decoder worker is still active");
    try{auto state=std::make_shared<State>(std::move(resolver),std::move(factory));state->route=route;auto argument=std::make_unique<std::shared_ptr<State>>(state);const auto thread=_beginthreadex(nullptr,0,worker,argument.get(),0,nullptr);if(!thread)throw std::runtime_error("Cannot start image worker");argument.release();impl_->worker.value=reinterpret_cast<HANDLE>(thread);impl_->state=std::move(state);}catch(...){workerGate().store(false);throw;}
}
NativeNotesImageDecoder::~NativeNotesImageDecoder(){stop();}
bool NativeNotesImageDecoder::setVisible(std::span<const NotesImageRequest>requests,bool retry){auto&i=*impl_;i.onThread();need(requests.size()<=maximumVisible,"Visible media limit exceeded");for(std::size_t n=0;n<requests.size();++n){valid(requests[n]);for(std::size_t k=0;k<n;++k)need(requests[n].key!=requests[k].key,"Duplicate media identity");}
    std::vector<NotesImageRequest>visible(requests.begin(),requests.end());std::vector<Job>jobs;jobs.reserve(8);
    std::lock_guard lock(i.state->mutex);auto&s=*i.state;need(!s.stopping,"Image decoder stopped");if(!retry&&visible==s.visible)return false;need(s.generation<std::numeric_limits<std::uint64_t>::max(),"Image generation exhausted");
    // Do not reset an unchanged playing GIF to frame0 merely because another
    // card appeared. Preserve a pending requested frame when it is still live.
    for(const auto&r:requests){const bool retained=std::find(s.visible.begin(),s.visible.end(),r)!=s.visible.end();const auto queued=std::find_if(s.jobs.begin(),s.jobs.end(),[&](const auto&j){return j.request==r;});
        if(retry||!retained)jobs.push_back({r,0,s.generation+1,retry});else if(queued!=s.jobs.end()){auto job=*queued;job.generation=s.generation+1;jobs.push_back(std::move(job));}
        else if(s.active&&s.active->request==r){auto job=*s.active;job.generation=s.generation+1;jobs.push_back(std::move(job));}
        else {const auto ready=std::find_if(s.ready.begin(),s.ready.end(),[&](const auto&value){return value&&value->key==r.key&&value->revision==r.revision;});if(ready!=s.ready.end())jobs.push_back({r,(*ready)->frame?(*ready)->frame->index:0,s.generation+1,false});}}
    ++s.generation;s.visible=std::move(visible);s.jobs=std::move(jobs);s.clearReady();++s.stats.requests;SetEvent(s.event.value);return true;
}
bool NativeNotesImageDecoder::requestFrame(std::string_view key,unsigned index){auto&i=*impl_;i.onThread();need(index<maximumGIFFrames,"Image frame exceeds source bound");std::lock_guard lock(i.state->mutex);auto&s=*i.state;need(!s.stopping,"Image decoder stopped");const auto visible=std::find_if(s.visible.begin(),s.visible.end(),[&](const auto&r){return r.key==key;});if(visible==s.visible.end())return false;
    auto job=std::find_if(s.jobs.begin(),s.jobs.end(),[&](const auto&j){return j.request.key==key;});if(job!=s.jobs.end()){if(job->index==index)return false;job->index=index;}else{s.jobs.push_back({*visible,index,s.generation});}++s.stats.requests;SetEvent(s.event.value);return true;
}
void NativeNotesImageDecoder::hide(){(void)setVisible({});}
void NativeNotesImageDecoder::setRoute(NotesImageRoute route){auto&i=*impl_;i.onThread();routeValid(route);std::lock_guard lock(i.state->mutex);need(!i.state->stopping,"Image decoder stopped");i.state->route=route;i.state->noticePosted=false;i.state->notice();}
std::vector<NotesImageCompletion>NativeNotesImageDecoder::drain(UINT_PTR generation){auto&i=*impl_;i.onThread();std::vector<NotesImageCompletion>results;std::lock_guard lock(i.state->mutex);auto&s=*i.state;if(s.stopping||s.route.generation!=generation)return results;results.reserve(s.stats.completed);for(auto&r:s.ready)if(r){results.push_back(std::move(*r));r.reset();}s.stats.completed=0;s.noticePosted=false;return results;}
void NativeNotesImageDecoder::stop()noexcept{if(!impl_||!impl_->state)return;auto&s=*impl_->state;std::lock_guard lock(s.mutex);s.route={};s.stopping=true;s.jobs.clear();s.visible.clear();s.clearReady();SetEvent(s.event.value);}
NotesImageDecoderStats NativeNotesImageDecoder::stats()const{auto&i=*impl_;i.onThread();std::lock_guard lock(i.state->mutex);auto result=i.state->stats;result.queued=i.state->jobs.size();result.liveFrameBytes=i.state->budget->bytes.load();return result;}
HANDLE NativeNotesImageDecoder::duplicateWorkerHandle()const{impl_->onThread();HANDLE copy{};if(!DuplicateHandle(GetCurrentProcess(),impl_->worker.value,GetCurrentProcess(),&copy,SYNCHRONIZE,FALSE,0))throw std::runtime_error("Cannot duplicate image worker handle");return copy;}
} // namespace endfield::native
#endif
