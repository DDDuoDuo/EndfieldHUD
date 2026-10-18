#include "native/activity_icons.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <atomic>
#include <set>
#include <stdexcept>
#include <thread>

namespace endfield::native {
namespace {
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
bool validKey(std::string_view key){return !key.empty()&&key.size()<=512&&key.find('\0')==key.npos&&ehud::data::Json::validUtf8(key);}
}
struct ActivityIconProvider::Impl:std::enable_shared_from_this<Impl>{
    struct Entry{std::string memoryKey;std::shared_ptr<const ActivityIconPixels>image;std::uint64_t used{};};
    app::UtilityExecutor&executor;app::UtilityExecutor::Route route;ActivityIconLoader loader;const std::thread::id owner=std::this_thread::get_id();
    LayerImageSource images;std::map<std::string,Entry,std::less<>>cache;std::vector<std::string>wanted;std::string prefix;
    unsigned pixels{};std::uint64_t revision{},clock{},nextKey{};bool inFlight{},retry{};ActivityIconStats counts;
    Impl(app::UtilityExecutor&e,ActivityIconLoader l):executor(e),route(e.makeRoute()),loader(std::move(l)){
        need(bool(loader),"Activity icons require an explicit loader");static std::atomic<std::uint64_t>identity{};prefix="activity-icon-"+std::to_string(++identity)+"/";wanted.reserve(maximumPending);
    }
    void thread()const{if(owner!=std::this_thread::get_id())throw std::logic_error("Activity icon provider belongs to its creating thread");}
    void evict(){
        // Least recently bound first; the bound visible rows are always newest.
        while(cache.size()>maximumCached){auto oldest=cache.begin();for(auto it=cache.begin();it!=cache.end();++it)if(it->second.used<oldest->second.used)oldest=it;if(!oldest->second.memoryKey.empty())images.retire(oldest->second.memoryKey);cache.erase(oldest);}
    }
    void submit(){
        if(inFlight||wanted.empty())return;
        auto keys=std::make_shared<std::vector<std::string>>(wanted);auto results=std::make_shared<std::vector<std::optional<ActivityIconPixels>>>(keys->size());const auto size=pixels;const std::weak_ptr<Impl>weak=weak_from_this();
        const bool accepted=executor.submit(route,[keys,results,size,load=loader]{
            for(std::size_t n=0;n<keys->size();++n){try{auto value=load((*keys)[n],size);if(value&&value->width==size&&value->height==size&&value->straightRGBA.size()==std::size_t(size)*size*4)(*results)[n]=std::move(value);}catch(const std::bad_alloc&){throw;}catch(...){}}
        },[weak,keys,results,size](std::exception_ptr error){if(auto self=weak.lock())self->complete(*keys,*results,size,bool(error));});
        if(!accepted){retry=true;++counts.backpressure;return;}
        retry=false;inFlight=true;wanted.clear();++counts.jobs;
    }
    void complete(const std::vector<std::string>&keys,std::vector<std::optional<ActivityIconPixels>>&results,unsigned size,bool failed){
        thread();inFlight=false;
        if(size==pixels&&!failed)for(std::size_t n=0;n<keys.size();++n){
            Entry entry;entry.used=++clock;
            if(results[n]){entry.memoryKey=prefix+std::to_string(++nextKey);entry.image=std::make_shared<const ActivityIconPixels>(std::move(*results[n]));++counts.loaded;}else ++counts.unavailable;
            cache.insert_or_assign(keys[n],std::move(entry));
        }
        evict();++revision;submit();
    }
};
ActivityIconProvider::ActivityIconProvider(app::UtilityExecutor&executor,ActivityIconLoader loader):impl_(std::make_shared<Impl>(executor,std::move(loader))){}
ActivityIconProvider::~ActivityIconProvider(){impl_->executor.invalidate(impl_->route);}
ActivityIcons ActivityIconProvider::bind(std::span<const std::string_view>visible,unsigned pixels){
    auto&i=*impl_;i.thread();need(visible.size()<=maximumVisible,"Activity binds icons for visible rows only");need(pixels>=16&&pixels<=LayerImageSource::maximumDimension,"Invalid Activity icon size");
    if(pixels!=i.pixels){for(const auto&[_,entry]:i.cache)if(!entry.memoryKey.empty())i.images.retire(entry.memoryKey);i.cache.clear();i.wanted.clear();i.pixels=pixels;}
    ActivityIcons out;
    for(const auto key:visible){
        if(!validKey(key))continue;
        if(const auto found=i.cache.find(key);found!=i.cache.end()){
            found->second.used=++i.clock;if(!found->second.image)continue; // unavailable: no icon, no repeated load
            const auto&image=*found->second.image;
            if(i.images.publish(found->second.memoryKey,1,image.width,image.height,image.straightRGBA))++i.counts.publications;
            out.insert_or_assign(std::string(key),ActivityIcon{found->second.memoryKey,1});continue;
        }
        if(i.wanted.size()<maximumPending&&std::find(i.wanted.begin(),i.wanted.end(),key)==i.wanted.end()){i.wanted.emplace_back(key);++i.counts.requests;}
    }
    i.submit();return out;
}
void ActivityIconProvider::retain(std::span<const modules::ActivityApp>live){
    auto&i=*impl_;i.thread();std::set<std::string_view>keys;for(const auto&app:live)keys.insert(app.identity.iconKey);
    for(auto it=i.cache.begin();it!=i.cache.end();)if(keys.contains(it->first))++it;else{if(!it->second.memoryKey.empty())i.images.retire(it->second.memoryKey);it=i.cache.erase(it);}
    std::erase_if(i.wanted,[&](const auto&key){return !keys.contains(key);});
}
std::uint64_t ActivityIconProvider::revision()const noexcept{return impl_->revision;}
bool ActivityIconProvider::retryPending()const noexcept{return impl_->retry;}
LayerImageSource&ActivityIconProvider::images()noexcept{return impl_->images;}
ActivityIconStats ActivityIconProvider::stats()const{const auto&i=*impl_;i.thread();auto out=i.counts;out.cached=i.cache.size();out.pending=i.wanted.size();out.inFlight=i.inFlight;return out;}
} // namespace endfield::native

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shobjidl_core.h>
#include <knownfolders.h>
#include <commoncontrols.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
struct Apartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};
struct Icon{HICON value{};~Icon(){if(value)DestroyIcon(value);}};
struct Bitmap{HBITMAP value{};~Bitmap(){if(value)DeleteObject(value);}};
std::wstring wide(std::string_view value){if(value.empty()||value.size()>32767)return {};const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);if(n<=0)return {};std::wstring out(std::size_t(n),L'\0');if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),n)!=n)return {};return out;}
// Fit the source inside the square (Fant), centered, straight RGBA.
std::optional<ActivityIconPixels>pixels(IWICImagingFactory*wic,IWICBitmapSource*source,unsigned size){
    UINT width{},height{};if(FAILED(source->GetSize(&width,&height))||!width||!height||width>1024||height>1024)return {};
    const double scale=std::min(double(size)/width,double(size)/height);const auto w=std::max(1u,unsigned(std::lround(width*scale))),h=std::max(1u,unsigned(std::lround(height*scale)));
    ComPtr<IWICBitmapScaler>scaler;ComPtr<IWICFormatConverter>converter;
    if(FAILED(wic->CreateBitmapScaler(&scaler))||FAILED(scaler->Initialize(source,w,h,WICBitmapInterpolationModeFant))||FAILED(wic->CreateFormatConverter(&converter))||
       FAILED(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return {};
    std::vector<std::uint8_t>tight(std::size_t(w)*h*4);if(FAILED(converter->CopyPixels(nullptr,w*4,UINT(tight.size()),tight.data())))return {};
    ActivityIconPixels out;out.width=out.height=size;out.straightRGBA.assign(std::size_t(size)*size*4,0);const unsigned x=(size-w)/2,y=(size-h)/2;
    for(unsigned row=0;row<h;++row)std::copy_n(tight.data()+std::size_t(row)*w*4,std::size_t(w)*4,out.straightRGBA.data()+(std::size_t(y+row)*size+x)*4);
    if(std::none_of(out.straightRGBA.begin(),out.straightRGBA.end(),[](std::uint8_t v){return v!=0;}))return {};
    return out;
}
std::optional<ActivityIconPixels>fromIcon(IWICImagingFactory*wic,HICON icon,unsigned size){ComPtr<IWICBitmap>bitmap;if(!icon||FAILED(wic->CreateBitmapFromHICON(icon,&bitmap)))return {};return pixels(wic,bitmap.Get(),size);}
std::optional<ActivityIconPixels>load(const std::string&key,unsigned size){
    const Apartment apartment;if(FAILED(apartment.hr)&&apartment.hr!=RPC_E_CHANGED_MODE)return {};
    ComPtr<IWICImagingFactory>wic;if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))))return {};
    constexpr std::string_view packaged="appsfolder:";
    if(key.starts_with(packaged)){
        // The packaged app's Start icon from its AppsFolder item (icon only).
        const auto aumid=wide(std::string_view(key).substr(packaged.size()));ComPtr<IShellItemImageFactory>factory;Bitmap bitmap;ComPtr<IWICBitmap>decoded;
        if(!aumid.empty()&&SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder,KF_FLAG_DONT_VERIFY,aumid.c_str(),IID_PPV_ARGS(&factory)))&&
           SUCCEEDED(factory->GetImage({LONG(size),LONG(size)},SIIGBF_ICONONLY,&bitmap.value))&&bitmap.value&&
           SUCCEEDED(wic->CreateBitmapFromHBITMAP(bitmap.value,nullptr,WICBitmapUsePremultipliedAlpha,&decoded)))
            if(auto out=pixels(wic.Get(),decoded.Get(),size))return out;
    }else if(const auto path=wide(key);!path.empty()&&path.find(L'\0')==path.npos){
        // The executable's own icon resource at the requested size; the
        // default extractor reads resources only (no icon handlers).
        Icon icon;if(SHDefExtractIconW(path.c_str(),0,0,&icon.value,nullptr,MAKELONG(size,0))==S_OK)
            if(auto out=fromIcon(wic.Get(),icon.value,size))return out;
    }
    SHSTOCKICONINFO stock{};stock.cbSize=sizeof(stock);ComPtr<IImageList>list;Icon icon;
    if(SUCCEEDED(SHGetStockIconInfo(SIID_APPLICATION,SHGSI_SYSICONINDEX,&stock))&&SUCCEEDED(SHGetImageList(size>32?SHIL_EXTRALARGE:SHIL_LARGE,IID_PPV_ARGS(&list)))&&
       SUCCEEDED(list->GetIcon(stock.iSysImageIndex,ILD_TRANSPARENT,&icon.value)))return fromIcon(wic.Get(),icon.value,size);
    return {};
}
}
ActivityIconLoader windowsActivityIconLoader(){return [](const std::string&key,unsigned size){return load(key,size);};}
} // namespace endfield::native
#endif
