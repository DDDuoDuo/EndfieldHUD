#include "native/volume_icon_provider.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include "native/file_shelf_files.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif
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

struct VolumeQueuedIconProvider::Impl:std::enable_shared_from_this<Impl>{
    Submit submit;std::function<void()>changed;Extractor extractor;const DWORD owner{GetCurrentThreadId()};
    LayerImageSource images;VolumeIconPlan plan;
    // Requests answered at their revision (success, no icon or failure): not
    // extracted again until a retry or a new process advances the revision.
    std::map<std::string,std::uint64_t,std::less<>>answered;
    bool inFlight{},waiting{};VolumeQueuedIconStats counts;
    Impl(Submit s,std::function<void()>c,Extractor e):submit(std::move(s)),changed(std::move(c)),extractor(e?std::move(e):Extractor(volumeExecutableIcon)){need(bool(submit),"Volume icons need the shared queue");}
    void onThread()const{need(owner==GetCurrentThreadId(),"Volume icons belong to their creating thread");}
    std::vector<ShelfIconRequest>missing(){std::vector<ShelfIconRequest>out;out.reserve(plan.requests().size());
        for(const auto&r:plan.requests()){const auto found=answered.find(r.imageKey);if(found!=answered.end()&&found->second==r.revision)continue;if(images.acquire(r.imageKey,r.revision))continue;out.push_back(r);}
        return out;}
    void pump(){
        if(inFlight||!plan.active())return;auto wanted=missing();if(wanted.empty()){waiting=false;return;}
        if(wanted.size()>VolumeIconPlan::maximumVisible)wanted.resize(VolumeIconPlan::maximumVisible);
        auto requests=std::make_shared<const std::vector<ShelfIconRequest>>(std::move(wanted));
        auto results=std::make_shared<std::vector<ShelfIconPixels>>(requests->size());
        const std::weak_ptr<Impl>weak=weak_from_this();
        // In flight before the call: a queue that completes at once (tests)
        // re-enters complete() from inside submit.
        inFlight=true;waiting=false;++counts.jobs;bool accepted{};
        try{accepted=submit([requests,results,extract=extractor]{
            for(std::size_t n=0;n<requests->size();++n){
                try{(*results)[n]=extract((*requests)[n]);}catch(const std::bad_alloc&){(*results)[n]={};(*results)[n].result=E_OUTOFMEMORY;}catch(...){(*results)[n]={};(*results)[n].result=E_FAIL;}}
        },[weak,requests,results](bool failed){if(auto self=weak.lock())self->complete(*requests,*results,failed);});}
        catch(...){inFlight=false;--counts.jobs;throw;}
        if(!accepted){inFlight=false;--counts.jobs;waiting=true;++counts.backpressure;}
    }
    void complete(const std::vector<ShelfIconRequest>&requests,std::vector<ShelfIconPixels>&results,bool failed){
        onThread();inFlight=false;std::vector<VolumeIconResult>out;out.reserve(requests.size());
        for(std::size_t n=0;n<requests.size();++n){const auto&r=requests[n];auto&p=results[n];++counts.extractions;answered.insert_or_assign(r.imageKey,r.revision);
            VolumeIconResult v{r.imageKey,r.itemID,r.revision,{},failed&&SUCCEEDED(p.result)?E_FAIL:p.result,p.typeFallback};
            if(!failed&&SUCCEEDED(p.result)&&!p.typeFallback&&p.width==LayerImageSource::shelfRequestedPixels&&p.height==LayerImageSource::shelfRequestedPixels&&p.straightRGBA.size()==std::size_t(p.width)*p.height*4){
                try{v.image=images.publish(r.imageKey,r.revision,p.width,p.height,p.straightRGBA);++counts.published;}catch(const std::bad_alloc&){throw;}catch(...){v.image.reset();v.result=E_INVALIDARG;}}
            if(!v.image){++counts.unavailable;if(SUCCEEDED(v.result))v.result=E_FAIL;}
            p={};out.push_back(std::move(v));}
        // Bounded: answers are kept for the plan's visible requests and the
        // most recent others (a process that left keeps none for long).
        if(answered.size()>4*VolumeIconPlan::maximumRetained)for(auto it=answered.begin();it!=answered.end()&&answered.size()>VolumeIconPlan::maximumRetained;){
            const bool visible=std::any_of(plan.requests().begin(),plan.requests().end(),[&](const auto&r){return r.imageKey==it->first;});it=visible?std::next(it):answered.erase(it);}
        const bool art=plan.receive(out);
        if(art&&changed)changed();
        pump();
    }
};
VolumeQueuedIconProvider::VolumeQueuedIconProvider(Submit submit,std::function<void()>changed,Extractor extractor):impl_(std::make_shared<Impl>(std::move(submit),std::move(changed),std::move(extractor))){}
VolumeQueuedIconProvider::~VolumeQueuedIconProvider()=default;
bool VolumeQueuedIconProvider::setVisible(std::span<const VolumeIconApplication>apps,bool retry){auto&i=*impl_;i.onThread();
    if(i.plan.active()&&!retry&&std::equal(apps.begin(),apps.end(),i.plan.applications().begin(),i.plan.applications().end()))return false;
    auto next=i.plan;if(!next.setVisible(apps,retry))return false;
    // Bind what the cache holds at the exact revision now; a retained binding
    // whose pixels the cache evicted is cleared and asked for again (the
    // raster resolves the descriptor through the cache).
    std::vector<VolumeIconResult>known;known.reserve(next.requests().size());
    for(const auto&r:next.requests()){
        if(auto image=i.images.acquire(r.imageKey,r.revision)){known.push_back({r.imageKey,r.itemID,r.revision,std::move(image),S_OK,false});continue;}
        for(const auto&b:next.bindings())if(b.imageKey==r.imageKey&&b.image){known.push_back({r.imageKey,r.itemID,r.revision,{},E_PENDING,false});i.answered.erase(r.imageKey);break;}
    }
    if(!known.empty())(void)next.receive(known);
    i.plan=std::move(next);i.pump();return true;
}
bool VolumeQueuedIconProvider::hide(){auto&i=*impl_;i.onThread();return i.plan.hide();}
void VolumeQueuedIconProvider::queueCapacityAvailable(){auto&i=*impl_;i.onThread();if(i.waiting)i.pump();}
const VolumeIconPlan&VolumeQueuedIconProvider::plan()const noexcept{return impl_->plan;}
LayerImageSource&VolumeQueuedIconProvider::images()noexcept{return impl_->images;}
VolumeQueuedIconStats VolumeQueuedIconProvider::stats()const noexcept{auto out=impl_->counts;out.inFlight=impl_->inFlight;return out;}

namespace {
struct QueuedApartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);~QueuedApartment(){if(SUCCEEDED(hr))CoUninitialize();}};
struct QueuedIcon{HICON value{};~QueuedIcon(){if(value)DestroyIcon(value);}};
std::wstring widePath(std::string_view s){if(s.empty()||s.size()>32767)return {};const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(n<=0)return {};std::wstring out(static_cast<std::size_t>(n),L'\0');if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n)!=n)return {};return out;}
}
ShelfIconPixels volumeExecutableIcon(const ShelfIconRequest&r){
    using Microsoft::WRL::ComPtr;
    ShelfIconPixels out;out.result=E_FAIL;
    if(!validShelfIconRequest(r)||r.directory||r.unavailable){out.result=E_INVALIDARG;return out;}
    // The executable that still has the process's file identity, held (no
    // delete sharing) while its resource is read.
    ehud::data::ShelfFileAccess lease;
    try{lease=NativeFileShelfFiles{}.acquire(r.path);}catch(const std::bad_alloc&){throw;}catch(...){out.result=HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);return out;}
    if(!lease.open()||lease.metadata().isDirectory||lease.metadata().kind!=ehud::data::ShelfFileKind::regular||!r.identity.matches(lease.metadata().identity)){out.result=HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);return out;}
    const auto path=widePath(lease.metadata().windowsPath);if(path.empty()||path.find(L'\0')!=std::wstring::npos){out.result=E_INVALIDARG;return out;}
    constexpr unsigned size=LayerImageSource::shelfRequestedPixels;
    // No icon resource (ExtractIconEx counts the file's icon groups): Explorer
    // would show the generic application icon; the Mac shows none for an
    // executable outside an application bundle.
    if(ExtractIconExW(path.c_str(),-1,nullptr,nullptr,0)==0){out.typeFallback=true;out.result=HRESULT_FROM_WIN32(ERROR_RESOURCE_TYPE_NOT_FOUND);return out;}
    QueuedIcon icon;const HRESULT extracted=SHDefExtractIconW(path.c_str(),0,0,&icon.value,nullptr,MAKELONG(size,0));
    if(extracted!=S_OK||!icon.value){out.typeFallback=extracted==S_FALSE;out.result=extracted==S_FALSE?HRESULT_FROM_WIN32(ERROR_RESOURCE_TYPE_NOT_FOUND):(FAILED(extracted)?extracted:E_FAIL);return out;}
    const QueuedApartment apartment;if(FAILED(apartment.hr)&&apartment.hr!=RPC_E_CHANGED_MODE){out.result=apartment.hr;return out;}
    ComPtr<IWICImagingFactory>wic;HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));if(FAILED(hr)){out.result=hr;return out;}
    ComPtr<IWICBitmap>bitmap;if(FAILED(hr=wic->CreateBitmapFromHICON(icon.value,&bitmap))){out.result=hr;return out;}
    UINT width{},height{};if(FAILED(hr=bitmap->GetSize(&width,&height))||!width||!height||width>LayerImageSource::maximumDimension||height>LayerImageSource::maximumDimension){out.result=FAILED(hr)?hr:E_INVALIDARG;return out;}
    // Fit (Fant) and centre in the 64 px square, straight RGBA, as the Shelf's icons.
    const double scale=std::min(double(size)/width,double(size)/height);
    const auto w=std::max(1u,static_cast<unsigned>(std::lround(width*scale))),h=std::max(1u,static_cast<unsigned>(std::lround(height*scale)));
    ComPtr<IWICBitmapScaler>scaler;ComPtr<IWICFormatConverter>converter;
    if(FAILED(hr=wic->CreateBitmapScaler(&scaler))||FAILED(hr=scaler->Initialize(bitmap.Get(),w,h,WICBitmapInterpolationModeFant))||FAILED(hr=wic->CreateFormatConverter(&converter))||
       FAILED(hr=converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))){out.result=hr;return out;}
    std::vector<std::uint8_t>tight(std::size_t(w)*h*4);if(FAILED(hr=converter->CopyPixels(nullptr,w*4,static_cast<UINT>(tight.size()),tight.data()))){out.result=hr;return out;}
    if(std::none_of(tight.begin(),tight.end(),[](std::uint8_t v){return v!=0;})){out.typeFallback=true;out.result=HRESULT_FROM_WIN32(ERROR_RESOURCE_TYPE_NOT_FOUND);return out;}
    out.width=out.height=size;out.straightRGBA.assign(std::size_t(size)*size*4,0);const unsigned x=(size-w)/2,y=(size-h)/2;
    for(unsigned row=0;row<h;++row)std::copy_n(tight.data()+std::size_t(row)*w*4,std::size_t(w)*4,out.straightRGBA.data()+(std::size_t(y+row)*size+x)*4);
    out.result=S_OK;return out;
}
#endif
} // namespace endfield::native
