#include "native/volume_icon_provider.hpp"
#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>
namespace endfield::native {
namespace {
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
bool applicationID(std::string_view s){return !s.empty()&&s.size()<=4096&&s.find('\0')==s.npos&&ehud::data::Json::validUtf8(s);}
void validate(const VolumeIconApplication&a){need(applicationID(a.applicationID)&&ehud::data::validUUID(a.requestToken)&&(!a.executable||validAudioApplicationExecutable(*a.executable)),"Invalid Volume app icon identity or executable metadata");}
void bump(std::uint64_t&v){need(v<std::numeric_limits<std::uint64_t>::max(),"Volume icon revision exhausted");++v;}
}
VolumeIconPlan::VolumeIconPlan(){retained_.reserve(maximumRetained);visible_.reserve(maximumVisible);requests_.reserve(maximumVisible);bindings_.reserve(maximumVisible);}
bool VolumeIconPlan::setVisible(std::span<const VolumeIconApplication>apps,bool retry){
    need(apps.size()<=maximumVisible,"Volume icon visible budget exceeded");
    for(std::size_t n=0;n<apps.size();++n){validate(apps[n]);for(std::size_t k=0;k<n;++k)need(apps[n].applicationID!=apps[k].applicationID&&apps[n].requestToken!=apps[k].requestToken,"Repeated Volume app icon identity or token");
        for(const auto&old:retained_)need(old.app.applicationID==apps[n].applicationID||old.app.requestToken!=apps[n].requestToken,"Volume icon UUID belongs to another retained process");}
    if(active_&&!retry&&std::equal(apps.begin(),apps.end(),visible_.begin(),visible_.end()))return false;
    // All strings, vectors and revision increments are staged; bad_alloc or a
    // late validation failure cannot replace the prior usable bindings.
    auto next=*this;next.active_=true;next.visible_.assign(apps.begin(),apps.end());next.requests_.clear();next.bindings_.clear();
    for(const auto&a:apps){auto found=std::find_if(next.retained_.begin(),next.retained_.end(),[&](const auto&e){return e.app.applicationID==a.applicationID;});
        if(found==next.retained_.end()){
            if(next.retained_.size()==maximumRetained){const auto oldest=std::min_element(next.retained_.begin(),next.retained_.end(),[&](const auto&x,const auto&y){const bool xv=std::any_of(apps.begin(),apps.end(),[&](const auto&v){return v.applicationID==x.app.applicationID;}),yv=std::any_of(apps.begin(),apps.end(),[&](const auto&v){return v.applicationID==y.app.applicationID;});return xv!=yv?!xv:x.used<y.used;});need(oldest!=next.retained_.end()&&std::none_of(apps.begin(),apps.end(),[&](const auto&v){return v.applicationID==oldest->app.applicationID;}),"Volume retained icon budget exceeded");next.retained_.erase(oldest);}
            next.retained_.push_back({a,{a.applicationID,"volume.app.icon."+a.requestToken,0,{},0,false},0});found=std::prev(next.retained_.end());
        }
        auto&e=*found;const bool changed=e.app!=a||!e.binding.revision||retry;
        if(changed){e.app=a;e.binding.imageKey="volume.app.icon."+a.requestToken;e.binding.image.reset();e.binding.result=0;e.binding.typeFallback=false;need(next.nextRevision_<static_cast<std::uint64_t>(INT64_MAX),"Volume raster image revision exhausted");bump(next.nextRevision_);e.binding.revision=next.nextRevision_;}
        bump(next.serial_);e.used=next.serial_;
        if(a.executable){ShelfIconRequest r;r.imageKey=e.binding.imageKey;r.revision=e.binding.revision;r.itemID=a.requestToken;r.path=a.executable->path;r.identity=a.executable->identity;need(validShelfIconRequest(r),"Invalid prepared Volume Shell icon request");next.requests_.push_back(std::move(r));}
        next.bindings_.push_back(e.binding);
    }
    bump(next.contentRevision_);*this=std::move(next);return true;
}
bool VolumeIconPlan::receive(std::span<const VolumeIconResult>results){
    need(results.size()<=maximumVisible,"Volume icon completion budget exceeded");if(!active_)return false;
    for(std::size_t n=0;n<results.size();++n){const auto&r=results[n];for(std::size_t k=0;k<n;++k)need(r.imageKey!=results[k].imageKey,"Repeated Volume icon completion");
        const auto q=std::find_if(requests_.begin(),requests_.end(),[&](const auto&v){return v.imageKey==r.imageKey&&v.itemID==r.requestToken&&v.revision==r.revision;});if(q==requests_.end())continue;
        if(r.image)need(r.result>=0&&r.image->key()==r.imageKey&&r.image->revision()==r.revision&&r.image->width()==64&&r.image->height()==64,"Volume icon result has mismatched source pixels");}
    need(contentRevision_<std::numeric_limits<std::uint64_t>::max(),"Volume icon revision exhausted");
    bool changed{};for(const auto&r:results){const auto q=std::find_if(requests_.begin(),requests_.end(),[&](const auto&v){return v.imageKey==r.imageKey&&v.itemID==r.requestToken&&v.revision==r.revision;});if(q==requests_.end())continue;
        const auto old=std::find_if(retained_.begin(),retained_.end(),[&](const auto&e){return e.binding.imageKey==r.imageKey&&e.binding.revision==r.revision;});need(old!=retained_.end(),"Volume icon retained binding missing");
        const auto pixels=r.result>=0&&!r.typeFallback?r.image:nullptr;auto&b=old->binding;
        if(b.image!=pixels||b.result!=r.result||b.typeFallback!=r.typeFallback){b.image=pixels;b.result=r.result;b.typeFallback=r.typeFallback;changed=true;}
        for(auto&visible:bindings_)if(visible.applicationID==b.applicationID){visible.image=b.image;visible.result=b.result;visible.typeFallback=b.typeFallback;}
    }
    if(changed)bump(contentRevision_);return changed;
}
bool VolumeIconPlan::hide()noexcept{if(!active_)return false;active_=false;visible_.clear();requests_.clear();bindings_.clear();if(contentRevision_!=std::numeric_limits<std::uint64_t>::max())++contentRevision_;return true;}
const VolumeIconBinding*VolumeIconPlan::binding(std::string_view id)const noexcept{const auto i=std::find_if(bindings_.begin(),bindings_.end(),[&](const auto&b){return b.applicationID==id;});return i==bindings_.end()?nullptr:&*i;}
#ifdef _WIN32
NativeVolumeIconProvider::NativeVolumeIconProvider(NativeShelfIconProvider&p,LayerImageSource&i):provider_(&p),images_(&i),owner_(GetCurrentThreadId()){}
NativeVolumeIconProvider::~NativeVolumeIconProvider(){if(plan_.active())try{hide();}catch(...){std::terminate();}}
void NativeVolumeIconProvider::onThread()const{need(owner_==GetCurrentThreadId(),"Volume icons require the borrowed provider's owner thread");}
bool NativeVolumeIconProvider::setVisible(std::span<const VolumeIconApplication>apps,bool retry){onThread();if(plan_.active()&&!retry&&std::equal(apps.begin(),apps.end(),plan_.applications().begin(),plan_.applications().end()))return false;
    auto next=plan_;if(!next.setVisible(apps,retry))return false;
    // A retained image can outlive shared LRU eviction. Do not expose its
    // descriptor until the cache has the exact revision used by rasterization.
    std::vector<VolumeIconResult>pending;pending.reserve(next.requests().size());for(const auto&r:next.requests())if(!images_->acquire(r.imageKey,r.revision))pending.push_back({r.imageKey,r.itemID,r.revision,{},E_PENDING,false});
    if(!pending.empty())next.receive(pending);
    provider_->setVisible(next.requests());plan_=std::move(next);return true;
}
bool NativeVolumeIconProvider::drain(UINT_PTR generation){onThread();if(!plan_.active())return false;const auto ready=provider_->drain(generation);std::vector<VolumeIconResult>results;results.reserve(ready.size());
    for(const auto&r:ready)results.push_back({r.imageKey,r.itemID,r.revision,r.image,r.result,r.typeFallback});return plan_.receive(results);
}
bool NativeVolumeIconProvider::hide(){onThread();if(!plan_.active())return false;provider_->hide();return plan_.hide();}
#endif
} // namespace endfield::native
