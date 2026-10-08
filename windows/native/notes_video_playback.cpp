#include "native/notes_video_playback.hpp"
#ifdef _WIN32
#include "core/data/data_store.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <mferror.h>
#include <dxgi.h>
namespace endfield::native {
namespace detail {NativeNotesVideoPlayback::Factory makeNotesMFVideoFactory();}
namespace {
using State=modules::NotesMediaState;using Owner=NativeNotesVideoPlayback;
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}
void routeValid(NotesVideoRoute r){if(!r.owner){need(!r.message&&!r.generation,"Empty video route must be fully empty");return;}DWORD pid{};need(IsWindow(r.owner)&&GetWindowThreadProcessId(r.owner,&pid)==GetCurrentThreadId()&&pid==GetCurrentProcessId()&&r.message>=WM_APP&&r.message<0xc000&&r.generation,"Video route must be an owned same-thread private message");}
void valid(const NotesVideoRequest&r){need(!r.key.empty()&&r.key.size()<=400&&r.key.find('\0')==std::string::npos&&ehud::data::Json::validUtf8(r.key)&&r.revision&&ehud::data::validWindowsFilePath(r.path),"Invalid explicit video reference");if(r.duration)need(std::isfinite(*r.duration)&&*r.duration>0&&*r.duration<=31536000,"Invalid source video duration");}
struct Notices {
    struct Cell {std::uint64_t serial{};std::uint32_t flags{};HRESULT error{S_OK};};
    mutable std::mutex mutex;std::array<Cell,8>cells;NotesVideoRoute route;bool posted{},stopped{};std::uint64_t notices{},events{};
    void postLocked(){if(!stopped&&!posted&&route.owner&&std::any_of(cells.begin(),cells.end(),[](const auto&c){return c.flags!=0;}))if(PostMessageW(route.owner,route.message,route.generation,0)){posted=true;++notices;}}
    void notify(std::size_t index,std::uint64_t serial,std::uint32_t flags,HRESULT error)noexcept{std::lock_guard lock(mutex);if(stopped||cells[index].serial!=serial)return;auto&c=cells[index];c.flags|=flags;if(FAILED(error))c.error=error;++events;postLocked();}
};
}
struct NativeNotesVideoPlayback::Impl {
    struct Record:NotesVideoRecord {
        std::unique_ptr<Engine>engine;std::shared_ptr<RendererMediaTexture>poster,live;
        std::string posterID,liveID;std::size_t slot{8};std::uint64_t serial{};
        bool metadataReady{},posterReady{},posterSettled{},posterPending{},framePending{};
        std::optional<double>releasePoster,nextProgress;
    };
    Renderer&renderer;DWORD thread{GetCurrentThreadId()};std::shared_ptr<void>device;Factory factory;
    std::shared_ptr<Notices>notice=std::make_shared<Notices>();std::map<std::string,Record,std::less<>>records;
    std::vector<Record*>visible;std::vector<NotesVideoRequest>requests;std::uint64_t serial{},transfers{},ticks{};double time{};
    Impl(Renderer&r,NotesVideoRoute route,Factory f):renderer(r),device(r.mediaDevice()),factory(f?std::move(f):detail::makeNotesMFVideoFactory()){routeValid(route);notice->route=route;visible.reserve(8);requests.reserve(8);}
    ~Impl(){ {std::lock_guard lock(notice->mutex);notice->stopped=true;notice->route={};}for(auto&[key,r]:records){(void)key;if(r.engine)r.engine->stop();r.engine.reset();try{releaseTarget(r.live);releaseTarget(r.poster);}catch(...){}} }
    void check()const{need(GetCurrentThreadId()==thread,"Video owner requires its UI thread");}
    void clock(double t){check();need(std::isfinite(t)&&t>=time,"Video owner clock must be monotonic");time=t;}
    Record*find(std::string_view key){auto it=records.find(key);return it==records.end()?nullptr:&it->second;}
    static void state(Record&r,State value){if(r.state!=value){r.state=value;++r.contentRevision;}}
    static void texture(Record&r,const std::string&id){if(r.textureID!=id){r.textureID=id;++r.frameRevision;}}
    void endEngine(Record&r){if(r.slot<8){std::lock_guard lock(notice->mutex);notice->cells[r.slot]={};r.slot=8;}if(r.engine){r.engine->pause();r.engine->stop();r.engine.reset();}r.metadataReady=false;r.posterPending=false;r.framePending=false;r.nextProgress.reset();}
    void fail(Record&r,HRESULT error){r.error=FAILED(error)?error:E_FAIL;endEngine(r);texture(r,{});state(r,State::failed);}
    void hide(Record&r,bool preserve){if(r.engine){const auto actual=r.pendingSeek.value_or(r.engine->currentTime());if(std::isfinite(actual))r.currentTime=std::max(0.,actual);}endEngine(r);r.visible=false;r.pendingSeek.reset();r.releasePoster.reset();if(preserve&&r.posterReady&&r.poster&&r.poster->valid()){texture(r,r.posterID);r.releasePoster=time+.6;}else texture(r,{});state(r,State::hidden);}
    bool releaseTarget(std::shared_ptr<RendererMediaTexture>&target){if(!target)return true;if(target->valid()&&!renderer.removeTexture(target->sourceID()))return false;target.reset();return true;}
    bool collect(Record&r){bool done=true;if((!r.visible||r.state==State::failed)&&r.textureID!=r.liveID)done=releaseTarget(r.live)&&done;if((!r.visible||r.state==State::failed)&&r.textureID!=r.posterID){if(releaseTarget(r.poster))r.posterReady=false;else done=false;}return done;}
    void install(Record&r){
        r.visible=true;r.error=S_OK;r.posterError=S_OK;r.releasePoster.reset();r.posterPending=false;r.posterSettled=false;r.framePending=false;r.metadataReady=false;state(r,State::loading);
        {std::lock_guard lock(notice->mutex);for(std::size_t n=0;n<notice->cells.size();++n)if(!notice->cells[n].serial){r.slot=n;break;}need(r.slot<8&&serial<std::numeric_limits<std::uint64_t>::max(),"Video engine capacity/generation exhausted");r.serial=++serial;notice->cells[r.slot]={r.serial};}
        try{const auto queue=notice;const auto index=r.slot;const auto generation=r.serial;r.engine=factory(device,[queue,index,generation](std::uint32_t event,HRESULT result){queue->notify(index,generation,event,result);});need(bool(r.engine),"Video factory returned no engine");const auto result=r.engine->open(r.request);if(FAILED(result))fail(r,result);}catch(...){fail(r,E_FAIL);}
    }
    void makeMetadata(Record&r){NotesVideoMetadata metadata;const auto result=r.engine->metadata(metadata);if(FAILED(result)){fail(r,result);return;}
        if(!metadata.width||!metadata.height||metadata.width>65536||metadata.height>65536||!std::isfinite(metadata.duration)||metadata.duration<=0||metadata.duration>31536000){fail(r,MF_E_INVALIDMEDIATYPE);return;}
        const auto bound=modules::NotesMediaLayout::decodeDimension(r.request.maximumDimension);const auto ratio=std::min(1.,double(bound)/std::max(metadata.width,metadata.height));const auto w=std::max(1u,static_cast<unsigned>(std::lround(metadata.width*ratio))),h=std::max(1u,static_cast<unsigned>(std::lround(metadata.height*ratio)));
        if(r.width!=w||r.height!=h){need(!r.poster&&!r.live,"Video dimensions changed while retained surfaces exist");r.width=w;r.height=h;++r.contentRevision;}
        if(r.nativeWidth!=metadata.width||r.nativeHeight!=metadata.height){r.nativeWidth=metadata.width;r.nativeHeight=metadata.height;++r.contentRevision;}
        if(r.duration!=metadata.duration){r.duration=metadata.duration;++r.contentRevision;}r.metadataReady=true;
        if(!r.poster||!r.poster->valid())r.poster=renderer.createMediaTexture(r.posterID,r.width,r.height);state(r,State::paused);
    }
    void liveTarget(Record&r){if(!r.live||!r.live->valid())r.live=renderer.createMediaTexture(r.liveID,r.width,r.height);}
    bool transfer(Record&r,const std::shared_ptr<RendererMediaTexture>&target,bool poster=false){
        if(!target||!target->valid()){fail(r,DXGI_ERROR_DEVICE_REMOVED);return false;}
        const auto result=r.engine->transfer(target->targetSurface(),target->width(),target->height());
        if(FAILED(result)){
            // Original poster extraction is best-effort. A valid movie may
            // still play when its initial static image cannot be produced.
            if(poster){r.posterError=result;r.posterReady=false;++r.contentRevision;}
            else fail(r,result);
            return false;
        }
        renderer.commitMediaTexture(*target);++transfers;++r.frameRevision;return true;
    }
    template<class F>bool guarded(Record&r,F&&operation){
        try{operation();return true;}
        catch(const RendererError&e){fail(r,static_cast<HRESULT>(e.code()));}
        catch(const std::bad_alloc&){fail(r,E_OUTOFMEMORY);}
        catch(const std::exception&){fail(r,E_FAIL);}
        return false;
    }
    void beginPlayback(Record&r){if(!r.engine||!r.metadataReady||!r.posterSettled)return;if(r.duration&&r.currentTime>=*r.duration-.05){r.currentTime=0;r.pendingSeek=0;const auto hr=r.engine->seek(0);if(FAILED(hr)){fail(r,hr);return;}}liveTarget(r);const auto result=r.engine->play();if(FAILED(result)){fail(r,result);return;}r.framePending=true;state(r,State::playing);r.nextProgress=time+1;}
};
NativeNotesVideoPlayback::NativeNotesVideoPlayback(Renderer&r,NotesVideoRoute route,Factory f):impl_(std::make_unique<Impl>(r,route,std::move(f))){}
NativeNotesVideoPlayback::~NativeNotesVideoPlayback()=default;
bool NativeNotesVideoPlayback::setVisible(std::span<const NotesVideoRequest>values,double now,bool preserve){auto&i=*impl_;i.clock(now);need(values.size()<=maximumVisible,"At most eight video engines are visible");std::size_t additions{};for(std::size_t n=0;n<values.size();++n){valid(values[n]);for(std::size_t p=0;p<n;++p)need(values[p].key!=values[n].key,"Duplicate visible video key");if(const auto*r=i.find(values[n].key))need(r->request==values[n],"Retire a changed video reference before reusing its identity");else ++additions;}need(i.records.size()+additions<=maximumRetained,"Retire hidden videos before adding more");if(std::equal(values.begin(),values.end(),i.requests.begin(),i.requests.end()))return false;
    std::vector<NotesVideoRequest>next(values.begin(),values.end());for(auto*r:i.visible)if(std::none_of(values.begin(),values.end(),[&](const auto&v){return v.key==r->request.key;}))i.hide(*r,preserve);i.visible.clear();
    for(const auto&v:values){auto*r=i.find(v.key);if(!r){Impl::Record fresh;fresh.request=v;fresh.duration=v.duration;fresh.posterID=v.key+".poster";fresh.liveID=v.key+".live";auto[it,inserted]=i.records.emplace(v.key,std::move(fresh));(void)inserted;r=&it->second;}if(!r->visible)i.install(*r);i.visible.push_back(r);}i.requests=std::move(next);return true;
}
void NativeNotesVideoPlayback::hide(double now,bool preserve){setVisible({},now,preserve);}
bool NativeNotesVideoPlayback::accept(UINT_PTR generation,double now){auto&i=*impl_;i.clock(now);std::array<Notices::Cell,8>events;
    {std::lock_guard lock(i.notice->mutex);if(i.notice->stopped||generation!=i.notice->route.generation)return false;events=i.notice->cells;for(auto&c:i.notice->cells){c.flags=0;c.error=S_OK;}i.notice->posted=false;}
    bool changed{};for(auto*r:i.visible){if(r->slot>=8)continue;const auto&e=events[r->slot];if(e.serial!=r->serial||!e.flags)continue;changed=true;if(e.flags&failure){i.fail(*r,e.error);continue;}if((e.flags&ready)&&r->engine&&!r->metadataReady)i.guarded(*r,[&]{i.makeMetadata(*r);});if(!r->engine)continue;if(e.flags&firstFrame)r->posterPending=true;
        if(e.flags&seeked){r->pendingSeek.reset();const auto current=r->engine->currentTime();if(std::isfinite(current))r->currentTime=std::max(0.,current);r->framePending=true;++r->progressRevision;}
        if(e.flags&ended){r->wantsPlayback=false;r->engine->pause();r->framePending=false;r->nextProgress.reset();if(r->duration)r->currentTime=*r->duration;i.state(*r,State::paused);++r->progressRevision;}}
    return changed;
}
bool NativeNotesVideoPlayback::sample(double now){auto&i=*impl_;i.clock(now);bool changed{};
    for(auto*r:i.visible){if(!r->engine||!r->metadataReady)continue;
        if(!i.guarded(*r,[&]{
            if(r->posterPending){
                r->posterPending=false;r->posterSettled=true;
                if(i.transfer(*r,r->poster,true)){r->posterReady=true;Impl::texture(*r,r->posterID);}
                else Impl::texture(*r,{});
                changed=true;if(!r->engine)return;
                if(r->currentTime>0||r->pendingSeek){i.liveTarget(*r);const auto target=r->pendingSeek.value_or(r->currentTime);r->pendingSeek=target;const auto hr=r->engine->seek(target);if(FAILED(hr)){i.fail(*r,hr);return;}}
                if(r->wantsPlayback)i.beginPlayback(*r);
            }
            if(r->engine&&(r->state==State::playing||r->framePending)){
                std::int64_t pts{};++i.ticks;const auto hr=r->engine->tick(pts);
                if(FAILED(hr)){i.fail(*r,hr);changed=true;return;}
                if(hr==S_OK){i.liveTarget(*r);if(i.transfer(*r,r->live)){Impl::texture(*r,r->liveID);r->framePending=false;}changed=true;}
            }
            if(r->engine&&r->nextProgress&&now>=*r->nextProgress){const auto time=r->pendingSeek.value_or(r->engine->currentTime());if(std::isfinite(time))r->currentTime=std::max(0.,time);++r->progressRevision;r->nextProgress=now+1;changed=true;}
        }))changed=true;
    }
    for(auto&[key,r]:i.records){(void)key;if(!r.visible&&r.releasePoster&&now>=*r.releasePoster){r.releasePoster.reset();Impl::texture(r,{});changed=true;}}return changed;
}
bool NativeNotesVideoPlayback::play(std::string_view key,double now){auto&i=*impl_;i.clock(now);auto*r=i.find(key);if(!r||!r->visible||r->state==State::failed)return false;const auto old=r->state;const auto previous=r->wantsPlayback;r->wantsPlayback=true;i.guarded(*r,[&]{i.beginPlayback(*r);});return old!=r->state||!previous;}
bool NativeNotesVideoPlayback::pause(std::string_view key){auto&i=*impl_;i.check();auto*r=i.find(key);if(!r)return false;r->wantsPlayback=false;r->framePending=r->pendingSeek.has_value();r->nextProgress.reset();if(r->engine){const auto current=r->pendingSeek.value_or(r->engine->currentTime());if(std::isfinite(current))r->currentTime=std::max(0.,current);r->engine->pause();++r->progressRevision;}if(r->state==State::playing){Impl::state(*r,State::paused);return true;}return false;}
bool NativeNotesVideoPlayback::toggle(std::string_view key,double now){impl_->check();auto*r=impl_->find(key);return r?(r->state==State::playing||r->wantsPlayback?pause(key):play(key,now)):false;}
bool NativeNotesVideoPlayback::seek(std::string_view key,double seconds){auto&i=*impl_;i.check();auto*r=i.find(key);if(!r||!r->duration||!std::isfinite(seconds))return false;r->currentTime=std::clamp(seconds,0.,*r->duration);++r->progressRevision;if(r->engine&&r->metadataReady&&r->posterSettled)i.guarded(*r,[&]{i.liveTarget(*r);r->pendingSeek=r->currentTime;const auto result=r->engine->seek(r->currentTime);if(FAILED(result))i.fail(*r,result);});return true;}
bool NativeNotesVideoPlayback::requiresFrames()const noexcept{for(const auto*r:impl_->visible)if(r->engine&&(r->state==State::playing||r->framePending))return true;return false;}
std::optional<double>NativeNotesVideoPlayback::nextWakeTime()const{const auto&i=*impl_;i.check();std::optional<double>result;for(const auto&[key,r]:i.records){(void)key;for(const auto v:{r.nextProgress,r.releasePoster})if(v&&(!result||*v<*result))result=v;}return result;}
const NotesVideoRecord*NativeNotesVideoPlayback::find(std::string_view key)const noexcept{return impl_->find(key);}
bool NativeNotesVideoPlayback::retire(std::string_view key){auto&i=*impl_;i.check();const auto found=i.records.find(key);if(found==i.records.end())return false;auto&r=found->second;need(!r.visible,"Hide video before retiring it");r.releasePoster.reset();Impl::texture(r,{});if(!i.collect(r))return false;i.records.erase(found);return true;}
bool NativeNotesVideoPlayback::collectRetired(){auto&i=*impl_;i.check();bool done=true;for(auto&[key,r]:i.records){(void)key;if(!i.collect(r))done=false;}return done;}
void NativeNotesVideoPlayback::setRoute(NotesVideoRoute r){auto&i=*impl_;i.check();routeValid(r);std::lock_guard lock(i.notice->mutex);i.notice->route=r;i.notice->posted=false;i.notice->postLocked();}
NotesVideoStats NativeNotesVideoPlayback::stats()const{const auto&i=*impl_;i.check();NotesVideoStats out;out.visible=i.visible.size();for(const auto&[key,r]:i.records){(void)key;if(r.engine)++out.engines;}out.transfers=i.transfers;out.ticks=i.ticks;{std::lock_guard lock(i.notice->mutex);out.events=i.notice->events;out.notices=i.notice->notices;}return out;}
NotesVideoDiagnostics NativeNotesVideoPlayback::diagnostics(std::string_view key)const{auto&i=*impl_;i.check();const auto*r=i.find(key);return r&&r->engine?r->engine->diagnostics():NotesVideoDiagnostics{};}
} // namespace endfield::native
#endif
