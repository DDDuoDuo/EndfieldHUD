#include "native/notes_image_playback.hpp"
#ifdef _WIN32
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
namespace endfield::native {
namespace {using State=modules::NotesMediaState;void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}}
struct NativeNotesImagePlayback::Impl {
    NativeNotesImageDecoder*decoder;DWORD owner=GetCurrentThreadId();std::map<std::string,NotesImagePlaybackRecord,std::less<>>records;
    std::vector<NotesImagePlaybackRecord*>visible,fading;std::vector<NotesImagePlaybackRequest>requests;
    explicit Impl(NativeNotesImageDecoder&d):decoder(&d){visible.reserve(8);requests.reserve(8);fading.reserve(128);}
    void check()const{need(owner==GetCurrentThreadId(),"Media playback belongs to its owner thread");}
    static void clock(double now){need(std::isfinite(now),"Nonfinite media owner clock");}
    NotesImagePlaybackRecord*find(std::string_view key){auto it=records.find(key);return it==records.end()?nullptr:&it->second;}
    static void state(NotesImagePlaybackRecord&r,State next){if(r.state!=next){r.state=next;++r.contentRevision;}}
    static void schedule(NotesImagePlaybackRecord&r,double now){if(r.visible&&r.state==State::playing&&!r.decodePending&&!r.nextFrame&&r.info&&r.frame)r.nextFrame=now+r.info->frameDelays.at(r.frame->index);}
    static void conceal(NotesImagePlaybackRecord&r,double now,bool preserve){r.visible=false;r.decodePending=false;r.nextFrame.reset();r.releaseArtwork.reset();state(r,State::hidden);
        if(preserve&&r.frame)r.releaseArtwork=now+.6;else if(r.frame){r.frame.reset();++r.frameRevision;}}
};
NativeNotesImagePlayback::NativeNotesImagePlayback(NativeNotesImageDecoder&d):impl_(std::make_unique<Impl>(d)){}
NativeNotesImagePlayback::~NativeNotesImagePlayback(){if(impl_){try{impl_->decoder->hide();}catch(...){}}}
bool NativeNotesImagePlayback::setVisible(std::span<const NotesImagePlaybackRequest>incoming,double now,bool preserve){auto&i=*impl_;i.check();Impl::clock(now);
    if(std::equal(incoming.begin(),incoming.end(),i.requests.begin(),i.requests.end()))return false;
    std::vector<NotesImageRequest>decode;decode.reserve(incoming.size());for(const auto&r:incoming){need(r.kind==modules::NotesMediaKind::image||r.kind==modules::NotesMediaKind::gif,"Image playback requires still/GIF reference");if(const auto*old=i.find(r.image.key))need(old->kind==r.kind||old->request!=r.image,"Changing media kind requires a new request revision");decode.push_back(r.image);decode.back().firstFrameOnly=r.kind==modules::NotesMediaKind::image;}
    std::vector<NotesImagePlaybackRequest>requests(incoming.begin(),incoming.end());
    // Complete decoder batch validation happens before any current playback
    // state is hidden/reset. No file is opened synchronously by this method.
    i.decoder->setVisible(decode);
    for(auto*r:i.visible)if(std::none_of(incoming.begin(),incoming.end(),[&](const auto&v){return v.image==r->request&&v.kind==r->kind;})){Impl::conceal(*r,now,preserve);if(r->releaseArtwork&&std::find(i.fading.begin(),i.fading.end(),r)==i.fading.end())i.fading.push_back(r);}
    i.visible.clear();for(const auto&v:incoming){const auto&r=v.image;auto*record=i.find(r.key);if(!record){NotesImagePlaybackRecord fresh;fresh.request=r;fresh.kind=v.kind;auto [it,inserted]=i.records.emplace(r.key,std::move(fresh));(void)inserted;record=&it->second;}
        if(record->request!=r||record->kind!=v.kind){const auto content=record->contentRevision+1,frame=record->frameRevision+1;*record={};record->request=r;record->kind=v.kind;record->contentRevision=content;record->frameRevision=frame;}
        if(!record->visible){record->visible=true;record->error=S_OK;record->releaseArtwork.reset();record->nextFrame.reset();record->decodePending=true;if(!record->loaded){record->wantsPlayback=record->kind==modules::NotesMediaKind::gif;record->loaded=true;}Impl::state(*record,State::loading);}
        i.visible.push_back(record);
    }i.requests=std::move(requests);return true;
}
void NativeNotesImagePlayback::hide(double now,bool preserve){setVisible({},now,preserve);}
bool NativeNotesImagePlayback::accept(UINT_PTR generation,double now){auto&i=*impl_;i.check();Impl::clock(now);bool changed{};
    for(auto&completion:i.decoder->drain(generation)){auto*r=i.find(completion.key);if(!r||!r->visible||r->request.revision!=completion.revision)continue;r->decodePending=false;
        if(FAILED(completion.result)||!completion.info||!completion.frame){r->error=FAILED(completion.result)?completion.result:E_FAIL;r->nextFrame.reset();Impl::state(*r,State::failed);changed=true;continue;}
        const bool initial=r->state==State::loading;r->info=std::move(completion.info);
        // Like the source async frame closure, a paused pending decode is
        // dropped. Resuming before completion may still accept that next frame.
        if(!initial&&r->state!=State::playing)continue;
        r->frame=std::move(completion.frame);++r->frameRevision;changed=true;
        if(r->kind==modules::NotesMediaKind::image)Impl::state(*r,State::ready);
        else{Impl::state(*r,r->wantsPlayback&&r->info->frameCount>1?State::playing:State::paused);Impl::schedule(*r,now);}
    }return changed;
}
bool NativeNotesImagePlayback::sample(double now){auto&i=*impl_;i.check();Impl::clock(now);bool changed{};
    for(auto*r:i.visible)if(r->nextFrame&&now>=*r->nextFrame){r->nextFrame.reset();if(r->state==State::playing&&r->frame&&r->info&&!r->decodePending){r->decodePending=true;i.decoder->requestFrame(r->request.key,(r->frame->index+1)%r->info->frameCount);}}
    for(auto it=i.fading.begin();it!=i.fading.end();){auto&r=**it;if(r.releaseArtwork&&now>=*r.releaseArtwork){r.releaseArtwork.reset();if(!r.visible&&r.frame){r.frame.reset();++r.frameRevision;changed=true;}}if(!r.releaseArtwork)it=i.fading.erase(it);else ++it;}return changed;
}
bool NativeNotesImagePlayback::play(std::string_view key,double now){auto&i=*impl_;i.check();Impl::clock(now);auto*r=i.find(key);if(!r||!r->visible||r->state==State::failed||r->kind==modules::NotesMediaKind::image)return false;const auto previous=r->state;r->wantsPlayback=true;if(r->info&&r->state!=State::loading&&r->info->frameCount>1){Impl::state(*r,State::playing);Impl::schedule(*r,now);}return previous!=r->state;}
bool NativeNotesImagePlayback::pause(std::string_view key){auto&i=*impl_;i.check();auto*r=i.find(key);if(!r)return false;r->wantsPlayback=false;r->nextFrame.reset();const auto previous=r->state;if(r->visible&&r->state==State::playing)Impl::state(*r,State::paused);return previous!=r->state;}
bool NativeNotesImagePlayback::toggle(std::string_view key,double now){impl_->check();auto*r=impl_->find(key);if(!r)return false;return r->state==State::playing||r->wantsPlayback?pause(key):play(key,now);}
std::optional<double>NativeNotesImagePlayback::nextWakeTime()const{const auto&i=*impl_;i.check();std::optional<double>result;auto include=[&](std::optional<double>time){if(time&&(!result||*time<*result))result=time;};for(auto*r:i.visible)include(r->nextFrame);for(auto*r:i.fading)include(r->releaseArtwork);return result;}
const NotesImagePlaybackRecord*NativeNotesImagePlayback::find(std::string_view key)const noexcept{return impl_->find(key);}
bool NativeNotesImagePlayback::retire(std::string_view key){auto&i=*impl_;i.check();auto it=i.records.find(key);if(it==i.records.end())return false;need(!it->second.visible,"Hide a media record before retiring it");std::erase(i.fading,&it->second);i.records.erase(it);return true;}
} // namespace endfield::native
#endif
