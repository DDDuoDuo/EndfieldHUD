#include "native/notes_video_playback.hpp"
#ifdef _WIN32
#include "core/data/data_store.hpp"
#include "native/notes_image_decoder.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <unordered_set>
#include <mferror.h>
#include <dxgi.h>
#include <d3d11.h>
#include <wrl/client.h>
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
        bool metadataReady{},posterReady{},posterSettled{},posterPending{},framePending{},needsEngine{},posterInstalled{},liveInstalled{},firstFrameSeen{},didPlay{},positionRestored{},seekFrameReady{};
        std::uint64_t liveGeneration{},posterGeneration{},acceptedPosterRevision{};std::vector<std::string>retiredTextures;
        std::optional<double>releasePoster,nextProgress,posterDeadline,seekDeadline;
    };
    Renderer&renderer;DWORD thread{GetCurrentThreadId()};std::shared_ptr<void>device;Factory factory;
    std::shared_ptr<Notices>notice=std::make_shared<Notices>();std::map<std::string,Record,std::less<>>records;
    std::vector<Record*>visible;std::array<Record*,8>engines{};std::size_t pendingCount{},pendingCursor{};std::vector<NotesVideoRequest>requests;std::uint64_t serial{},transfers{},ticks{};double time{};
    Impl(Renderer&r,NotesVideoRoute route,Factory f):renderer(r),device(r.mediaDevice()),factory(f?std::move(f):detail::makeNotesMFVideoFactory()){routeValid(route);notice->route=route;visible.reserve(8);requests.reserve(8);}
    ~Impl(){ {std::lock_guard lock(notice->mutex);notice->stopped=true;notice->route={};}for(auto&[key,r]:records){(void)key;if(r.engine)r.engine->stop();r.engine.reset();try{releaseTarget(r.liveID,r.live,r.liveInstalled);releaseTarget(r.posterID,r.poster,r.posterInstalled);for(const auto&id:r.retiredTextures)renderer.removeTexture(id);}catch(...){}} }
    void check()const{need(GetCurrentThreadId()==thread,"Video owner requires its UI thread");}
    void clock(double t){check();need(std::isfinite(t)&&t>=time,"Video owner clock must be monotonic");time=t;}
    Record*find(std::string_view key){auto it=records.find(key);return it==records.end()?nullptr:&it->second;}
    static void state(Record&r,State value){if(r.state!=value){r.state=value;++r.contentRevision;}}
    static void texture(Record&r,const std::string&id){if(r.textureID!=id){r.textureID=id;++r.frameRevision;}}
    void endEngine(Record&r){if(r.slot<8){std::lock_guard lock(notice->mutex);notice->cells[r.slot]={};engines[r.slot]=nullptr;r.slot=8;}if(r.engine){r.engine->pause();r.engine->stop();r.engine.reset();}r.metadataReady=false;r.posterPending=false;r.posterDeadline.reset();r.seekDeadline.reset();r.seekFrameReady=false;r.framePending=false;r.nextProgress.reset();}
    void dequeue(Record&r){if(r.needsEngine){r.needsEngine=false;--pendingCount;}}
    void enqueue(Record&r){if(!r.engine&&!r.needsEngine&&r.visible&&r.state!=State::failed){r.needsEngine=true;++pendingCount;}}
    void fail(Record&r,HRESULT error){dequeue(r);r.error=FAILED(error)?error:E_FAIL;endEngine(r);texture(r,{});state(r,State::failed);}
    void hide(Record&r,bool preserve){dequeue(r);if(r.engine){const auto actual=r.pendingSeek.value_or(r.engine->currentTime());if(std::isfinite(actual))r.currentTime=std::max(0.,actual);}endEngine(r);r.visible=false;r.pendingSeek.reset();r.releasePoster.reset();if(preserve&&r.posterReady&&r.posterInstalled){texture(r,r.posterID);r.releasePoster=time+.6;}else texture(r,{});state(r,State::hidden);}
    bool releaseTarget(const std::string&id,std::shared_ptr<RendererMediaTexture>&target,bool&installed){if(!installed){target.reset();return true;}if(!renderer.removeTexture(id))return false;target.reset();installed=false;return true;}
    bool collect(Record&r){bool done=true;if((!r.visible||r.state==State::failed)&&r.textureID!=r.liveID)done=releaseTarget(r.liveID,r.live,r.liveInstalled)&&done;if((!r.visible||r.state==State::failed)&&r.textureID!=r.posterID){if(releaseTarget(r.posterID,r.poster,r.posterInstalled)){r.posterReady=false;r.posterSettled=false;}else done=false;}
        for(auto it=r.retiredTextures.begin();it!=r.retiredTextures.end();)if(renderer.removeTexture(*it))it=r.retiredTextures.erase(it);else{done=false;++it;}return done;}
    void park(Record&r){if(r.engine){const auto current=r.engine->currentTime();if(std::isfinite(current))r.currentTime=std::max(0.,current);}endEngine(r);if(r.poster){if(r.posterReady){renderer.retainMediaPoster(*r.poster);r.poster.reset();}else releaseTarget(r.posterID,r.poster,r.posterInstalled);}if(r.live){renderer.retainMediaPoster(*r.live);r.live.reset();}}
    void install(Record&r){
        r.visible=true;r.error=S_OK;r.releasePoster.reset();r.posterPending=false;if(!r.request.workerPoster)r.posterSettled=r.posterReady||FAILED(r.posterError);r.framePending=false;r.metadataReady=false;r.firstFrameSeen=false;r.didPlay=false;r.positionRestored=false;if(!r.posterSettled)state(r,State::loading);
        {std::lock_guard lock(notice->mutex);for(std::size_t n=0;n<notice->cells.size();++n)if(!notice->cells[n].serial){r.slot=n;break;}need(r.slot<8&&serial<std::numeric_limits<std::uint64_t>::max(),"Video engine capacity/generation exhausted");r.serial=++serial;notice->cells[r.slot]={r.serial};engines[r.slot]=&r;}
        try{const auto queue=notice;const auto index=r.slot;const auto generation=r.serial;r.engine=factory(device,[queue,index,generation](std::uint32_t event,HRESULT result){queue->notify(index,generation,event,result);});need(bool(r.engine),"Video factory returned no engine");const auto result=r.engine->open(r.request);if(FAILED(result))fail(r,result);}catch(...){fail(r,E_FAIL);}
    }
    void makeMetadata(Record&r){NotesVideoMetadata metadata;const auto result=r.engine->metadata(metadata);if(FAILED(result)){fail(r,result);return;}
        if(!metadata.width||!metadata.height||metadata.width>65536||metadata.height>65536||!std::isfinite(metadata.duration)||metadata.duration<=0||metadata.duration>31536000){fail(r,MF_E_INVALIDMEDIATYPE);return;}
        const auto bound=modules::NotesMediaLayout::decodeDimension(r.request.maximumDimension);const auto ratio=std::min(1.,double(bound)/std::max(metadata.width,metadata.height));const auto w=std::max(1u,static_cast<unsigned>(std::lround(metadata.width*ratio))),h=std::max(1u,static_cast<unsigned>(std::lround(metadata.height*ratio)));
        if((!r.request.workerPoster||!r.posterReady)&&(r.width!=w||r.height!=h)){need(!r.posterInstalled&&!r.liveInstalled,"Video dimensions changed while retained surfaces exist");r.width=w;r.height=h;++r.contentRevision;}
        if(r.nativeWidth!=metadata.width||r.nativeHeight!=metadata.height){r.nativeWidth=metadata.width;r.nativeHeight=metadata.height;++r.contentRevision;}
        if(r.duration!=metadata.duration){r.duration=metadata.duration;++r.contentRevision;}r.metadataReady=true;
        if(!r.request.workerPoster&&!r.posterInstalled&&!r.posterSettled){r.poster=renderer.createMediaTexture(r.posterID,r.width,r.height);r.posterInstalled=true;}state(r,r.request.workerPoster&&!r.posterSettled?State::loading:State::paused);if(r.request.workerPoster)resumeAfterPoster(r);
    }
    void liveTarget(Record&r){if(r.live&&r.live->valid())return;
        // A parked live frame may still be published. Retire its immutable ID
        // only after owner publication switches to the new native target.
        if(r.liveInstalled){r.retiredTextures.push_back(r.liveID);r.liveInstalled=false;r.liveID=r.request.key+".live."+std::to_string(++r.liveGeneration);}
        r.live=renderer.createMediaTexture(r.liveID,r.width,r.height);r.liveInstalled=true;
    }
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
    void pump(){while(pendingCount){auto*next=static_cast<Record*>(nullptr);for(std::size_t count=0;count<visible.size();++count){const auto index=pendingCursor++%visible.size();if(visible[index]->needsEngine){next=visible[index];break;}}if(!next){pendingCount=0;return;}
        if(std::none_of(engines.begin(),engines.end(),[](auto*r){return r==nullptr;})){auto idle=std::find_if(engines.begin(),engines.end(),[](const auto*r){return r&&r->posterSettled&&!r->framePending&&!r->pendingSeek&&!r->wantsPlayback&&r->state==State::paused;});if(idle==engines.end())return;guarded(**idle,[&]{park(**idle);});}
        dequeue(*next);install(*next);
    }}
    bool issueSeek(Record&r,double target){
        r.pendingSeek=target;r.framePending=true;r.seekFrameReady=false;r.seekDeadline=time+5;
        // Frame-server seeks still need the owner's presentation ticks before
        // SEEKED. Waiting for that callback to request the first tick creates a
        // circular wait on native decoders. Do not Play or advance audio here.
        const auto hr=r.engine->seek(target);if(FAILED(hr)){fail(r,hr);return false;}return true;
    }
    void beginPlayback(Record&r){if(!r.engine||!r.metadataReady||!r.posterSettled)return;if(r.duration&&r.currentTime>=*r.duration-.05){r.currentTime=0;if(!issueSeek(r,0))return;}liveTarget(r);const auto result=r.engine->play();if(FAILED(result)){fail(r,result);return;}r.didPlay=true;r.framePending=true;state(r,State::playing);r.nextProgress=time+1;}
    void resumeAfterPoster(Record&r){if(!r.engine||!r.metadataReady||!r.posterSettled||r.positionRestored)return;r.positionRestored=true;state(r,State::paused);
        if(r.currentTime>0||r.pendingSeek){liveTarget(r);const auto target=r.pendingSeek.value_or(r.currentTime);if(!issueSeek(r,target))return;}
        if(r.wantsPlayback)beginPlayback(r);
    }
};
NativeNotesVideoPlayback::NativeNotesVideoPlayback(Renderer&r,NotesVideoRoute route,Factory f):impl_(std::make_unique<Impl>(r,route,std::move(f))){}
NativeNotesVideoPlayback::~NativeNotesVideoPlayback()=default;
bool NativeNotesVideoPlayback::setVisible(std::span<const NotesVideoRequest>values,double now,bool preserve){auto&i=*impl_;i.clock(now);std::unordered_set<std::string_view>keys;keys.reserve(values.size());for(const auto&value:values){valid(value);need(keys.insert(value.key).second,"Duplicate visible video key");if(const auto*r=i.find(value.key))need(r->request==value,"Retire a changed video reference before reusing its identity");}if(std::equal(values.begin(),values.end(),i.requests.begin(),i.requests.end()))return false;
    std::vector<NotesVideoRequest>next(values.begin(),values.end());i.visible.reserve(values.size());for(auto*r:i.visible)if(!keys.contains(r->request.key))i.hide(*r,preserve);i.visible.clear();
    for(const auto&v:values){auto*r=i.find(v.key);if(!r){Impl::Record fresh;fresh.request=v;fresh.duration=v.duration;fresh.posterID=v.key+".poster";fresh.liveID=v.key+".live";auto[it,inserted]=i.records.emplace(v.key,std::move(fresh));(void)inserted;r=&it->second;}if(!r->visible){r->visible=true;r->releasePoster.reset();r->error=S_OK;if(r->request.workerPoster){r->posterSettled=false;r->posterError=S_OK;}Impl::state(*r,State::loading);if(r->posterReady)i.texture(*r,r->posterID);i.enqueue(*r);}i.visible.push_back(r);}i.requests=std::move(next);i.pump();return true;
}
void NativeNotesVideoPlayback::hide(double now,bool preserve){setVisible({},now,preserve);}
bool NativeNotesVideoPlayback::setPoster(std::string_view key,std::uint64_t revision,std::uint64_t posterRevision,const NotesImageFrame*frame,HRESULT result){
    auto&i=*impl_;i.check();auto*r=i.find(key);if(!r||!r->visible||!r->request.workerPoster||r->request.revision!=revision||posterRevision<=r->acceptedPosterRevision)return false;
    need(posterRevision>0,"Poster completion requires an explicit revision");
    const auto bound=modules::NotesMediaLayout::decodeDimension(r->request.maximumDimension);
    if(SUCCEEDED(result))need(frame&&frame->index==0&&frame->width&&frame->height&&frame->width<=bound&&frame->height<=bound&&frame->straightRGBA.size()==std::size_t(frame->width)*frame->height*4,"Invalid immutable worker poster");
    r->acceptedPosterRevision=posterRevision;r->posterError=result;r->posterSettled=true;
    if(SUCCEEDED(result)){
        std::string nextID;std::shared_ptr<RendererMediaTexture>target;
        try{
            need(!r->liveInstalled||(r->width==frame->width&&r->height==frame->height),"Movie poster dimensions changed while live output remains retained");
            need(r->posterGeneration<std::numeric_limits<std::uint64_t>::max(),"Poster identity generation exhausted");
            nextID=r->posterInstalled?r->request.key+".poster."+std::to_string(++r->posterGeneration):r->posterID;
            if(r->posterInstalled)r->retiredTextures.reserve(r->retiredTextures.size()+1);
            std::vector<std::uint8_t>bgra(frame->straightRGBA.size());for(std::size_t p=0;p<bgra.size();p+=4){bgra[p]=frame->straightRGBA[p+2];bgra[p+1]=frame->straightRGBA[p+1];bgra[p+2]=frame->straightRGBA[p];bgra[p+3]=frame->straightRGBA[p+3];}
            target=i.renderer.createMediaTexture(nextID,frame->width,frame->height);
            Microsoft::WRL::ComPtr<ID3D11Texture2D>texture;const auto hr=static_cast<IDXGISurface*>(target->targetSurface())->QueryInterface(IID_PPV_ARGS(&texture));if(FAILED(hr))throw RendererError("Read owned poster upload surface",hr);
            Microsoft::WRL::ComPtr<ID3D11DeviceContext>context;static_cast<ID3D11Device*>(i.device.get())->GetImmediateContext(&context);
            context->UpdateSubresource(texture.Get(),0,nullptr,bgra.data(),frame->width*4,0);i.renderer.commitMediaTexture(*target);
            // Same media byte budget, but a static poster does not occupy one
            // of the finite decoder/output slots after its one upload.
            i.renderer.retainMediaPoster(*target);target.reset();
            if(r->posterInstalled)r->retiredTextures.push_back(std::move(r->posterID));r->posterID=std::move(nextID);r->poster.reset();r->posterInstalled=true;r->posterReady=true;
            if(r->width!=frame->width||r->height!=frame->height){r->width=frame->width;r->height=frame->height;++r->contentRevision;}
            Impl::texture(*r,r->posterID);++r->frameRevision;
        }catch(const RendererError&e){r->posterError=static_cast<HRESULT>(e.code());}catch(const std::bad_alloc&){r->posterError=E_OUTOFMEMORY;}catch(const std::exception&){r->posterError=E_FAIL;}
        if(FAILED(r->posterError)&&target){i.renderer.removeTexture(nextID);target.reset();}
    }
    if(FAILED(r->posterError)){r->posterReady=false;Impl::texture(*r,{});++r->contentRevision;}
    i.guarded(*r,[&]{i.resumeAfterPoster(*r);});if(i.pendingCount)i.pump();return true;
}
bool NativeNotesVideoPlayback::accept(UINT_PTR generation,double now){auto&i=*impl_;i.clock(now);std::array<Notices::Cell,8>events;
    {std::lock_guard lock(i.notice->mutex);if(i.notice->stopped||generation!=i.notice->route.generation)return false;events=i.notice->cells;for(auto&c:i.notice->cells){c.flags=0;c.error=S_OK;}i.notice->posted=false;}
    bool changed{};for(auto*r:i.engines){if(!r||r->slot>=8)continue;const auto&e=events[r->slot];if(e.serial!=r->serial||!e.flags)continue;changed=true;if(e.flags&failure){i.fail(*r,e.error);continue;}if((e.flags&ready)&&r->engine&&!r->metadataReady)i.guarded(*r,[&]{i.makeMetadata(*r);});if(!r->engine)continue;if((e.flags&firstFrame)&&!r->firstFrameSeen&&!r->request.workerPoster){r->firstFrameSeen=true;r->posterPending=true;r->posterDeadline=now+5;}
        if(e.flags&seeked){r->pendingSeek.reset();const auto current=r->engine->currentTime();if(std::isfinite(current))r->currentTime=std::max(0.,current);r->framePending=true;++r->progressRevision;}
        if((e.flags&ended)&&r->didPlay&&(r->wantsPlayback||r->state==State::playing)){r->wantsPlayback=false;r->engine->pause();r->framePending=false;r->nextProgress.reset();if(r->duration)r->currentTime=*r->duration;i.state(*r,State::paused);++r->progressRevision;}}
    return changed;
}
bool NativeNotesVideoPlayback::sample(double now){auto&i=*impl_;i.clock(now);bool changed{};
    for(auto*r:i.engines){if(!r||!r->engine||!r->metadataReady)continue;
        if(!i.guarded(*r,[&]{
            if(r->posterPending){
                if(!r->posterSettled){
                    // LOADEDDATA is not a decoded-frame fence. In frame-server
                    // mode S_OK from OnVideoStreamTick is the transfer contract;
                    // an early TransferVideoFrame may succeed with clear pixels.
                    // Use only the existing presentation clock while this one
                    // poster is pending. Five seconds bounds a stalled codec;
                    // static poster failure never forbids later explicit play.
                    std::int64_t pts{};++i.ticks;const auto ready=r->engine->tick(pts);
                    if(ready==S_FALSE&&r->posterDeadline&&now<*r->posterDeadline)return;
                    r->posterSettled=true;
                    if(ready==S_OK){if(i.transfer(*r,r->poster,true)){r->posterReady=true;Impl::texture(*r,r->posterID);}else Impl::texture(*r,{});}
                    else{r->posterError=FAILED(ready)?ready:HRESULT_FROM_WIN32(ERROR_TIMEOUT);r->posterReady=false;++r->contentRevision;Impl::texture(*r,{});}
                }
                r->posterPending=false;r->posterDeadline.reset();
                changed=true;if(!r->engine)return;
                if(r->currentTime>0||r->pendingSeek){i.liveTarget(*r);const auto target=r->pendingSeek.value_or(r->currentTime);if(!i.issueSeek(*r,target))return;}
                if(r->wantsPlayback)i.beginPlayback(*r);
            }
            if(r->engine&&(r->state==State::playing||r->framePending)){
                if(r->seekDeadline&&now>=*r->seekDeadline){i.fail(*r,HRESULT_FROM_WIN32(ERROR_TIMEOUT));changed=true;return;}
                std::int64_t pts{};HRESULT hr=S_OK;
                if(!r->seekFrameReady||r->pendingSeek){++i.ticks;hr=r->engine->tick(pts);}
                if(FAILED(hr)){i.fail(*r,hr);changed=true;return;}
                if(hr==S_OK){
                    // A paused ready frame may precede the asynchronous SEEKED
                    // notification. Retain that readiness until it is safe to
                    // expose the completed seek; do not require a second frame.
                    if(r->pendingSeek){r->seekFrameReady=true;return;}
                    i.liveTarget(*r);if(i.transfer(*r,r->live)){Impl::texture(*r,r->liveID);r->framePending=false;r->seekFrameReady=false;r->seekDeadline.reset();}changed=true;
                }
            }
            if(r->engine&&r->nextProgress&&now>=*r->nextProgress){const auto time=r->pendingSeek.value_or(r->engine->currentTime());if(std::isfinite(time))r->currentTime=std::max(0.,time);++r->progressRevision;r->nextProgress=now+1;changed=true;}
        }))changed=true;
    }
    for(auto&[key,r]:i.records){(void)key;if(!r.visible&&r.releasePoster&&now>=*r.releasePoster){r.releasePoster.reset();Impl::texture(r,{});changed=true;}}if(i.pendingCount)i.pump();return changed;
}
bool NativeNotesVideoPlayback::play(std::string_view key,double now){auto&i=*impl_;i.clock(now);auto*r=i.find(key);if(!r||!r->visible||r->state==State::failed)return false;const auto old=r->state;const auto previous=r->wantsPlayback;r->wantsPlayback=true;if(!r->engine){i.enqueue(*r);i.pump();}else i.guarded(*r,[&]{i.beginPlayback(*r);});return old!=r->state||!previous;}
bool NativeNotesVideoPlayback::pause(std::string_view key){auto&i=*impl_;i.check();auto*r=i.find(key);if(!r)return false;const bool changed=r->wantsPlayback||r->state==State::playing;r->wantsPlayback=false;r->framePending=r->pendingSeek.has_value();r->nextProgress.reset();if(r->engine){const auto current=r->pendingSeek.value_or(r->engine->currentTime());if(std::isfinite(current))r->currentTime=std::max(0.,current);r->engine->pause();++r->progressRevision;}if(r->state==State::playing)Impl::state(*r,State::paused);if(i.pendingCount)i.pump();return changed;}
bool NativeNotesVideoPlayback::toggle(std::string_view key,double now){impl_->check();auto*r=impl_->find(key);return r?(r->state==State::playing||r->wantsPlayback?pause(key):play(key,now)):false;}
bool NativeNotesVideoPlayback::seek(std::string_view key,double seconds){auto&i=*impl_;i.check();auto*r=i.find(key);if(!r||!r->duration||!std::isfinite(seconds))return false;r->currentTime=std::clamp(seconds,0.,*r->duration);++r->progressRevision;if(!r->engine&&r->visible){r->pendingSeek=r->currentTime;i.enqueue(*r);i.pump();}if(r->engine&&r->metadataReady&&r->posterSettled)i.guarded(*r,[&]{i.liveTarget(*r);i.issueSeek(*r,r->currentTime);});return true;}
bool NativeNotesVideoPlayback::requiresFrames()const noexcept{for(const auto*r:impl_->engines)if(r&&r->engine&&(r->state==State::playing||r->framePending||(r->posterPending&&r->metadataReady)))return true;return false;}
std::optional<double>NativeNotesVideoPlayback::nextWakeTime()const{const auto&i=*impl_;i.check();std::optional<double>result;for(const auto&[key,r]:i.records){(void)key;for(const auto v:{r.nextProgress,r.releasePoster})if(v&&(!result||*v<*result))result=v;}return result;}
const NotesVideoRecord*NativeNotesVideoPlayback::find(std::string_view key)const noexcept{return impl_->find(key);}
bool NativeNotesVideoPlayback::retire(std::string_view key){auto&i=*impl_;i.check();const auto found=i.records.find(key);if(found==i.records.end())return false;auto&r=found->second;need(!r.visible,"Hide video before retiring it");r.releasePoster.reset();Impl::texture(r,{});if(!i.collect(r))return false;i.records.erase(found);return true;}
bool NativeNotesVideoPlayback::collectRetired(){auto&i=*impl_;i.check();bool done=true;for(auto&[key,r]:i.records){(void)key;if(!i.collect(r))done=false;}return done;}
void NativeNotesVideoPlayback::setRoute(NotesVideoRoute r){auto&i=*impl_;i.check();routeValid(r);std::lock_guard lock(i.notice->mutex);i.notice->route=r;i.notice->posted=false;i.notice->postLocked();}
NotesVideoStats NativeNotesVideoPlayback::stats()const{const auto&i=*impl_;i.check();NotesVideoStats out;out.visible=i.visible.size();for(const auto&[key,r]:i.records){(void)key;if(r.engine)++out.engines;}out.transfers=i.transfers;out.ticks=i.ticks;{std::lock_guard lock(i.notice->mutex);out.events=i.notice->events;out.notices=i.notice->notices;}return out;}
NotesVideoDiagnostics NativeNotesVideoPlayback::diagnostics(std::string_view key)const{auto&i=*impl_;i.check();const auto*r=i.find(key);return r&&r->engine?r->engine->diagnostics():NotesVideoDiagnostics{};}
} // namespace endfield::native
#endif
