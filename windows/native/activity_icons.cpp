#include "native/activity_icons.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <thread>

namespace endfield::native {
namespace {
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
}
LayerImageSourceLimits ActivityIconLoads::limits()noexcept{return {modules::ActivityAppIcons::capacity,modules::ActivityAppIcons::capacity+modules::ActivityAppIcons::batch*16,16*1024*1024};}
struct ActivityIconLoads::Impl:std::enable_shared_from_this<Impl>{
    app::UtilityExecutor&executor;app::UtilityExecutor::Route route;ActivityIconLoader loader;const std::thread::id owner=std::this_thread::get_id();
    LayerImageSource images{ActivityIconLoads::limits()};std::string prefix;std::uint64_t nextKey{};bool inFlight{};ActivityIconStats counts;
    Impl(app::UtilityExecutor&e,ActivityIconLoader l):executor(e),route(e.makeRoute()),loader(std::move(l)){
        need(bool(loader),"Activity icons require an explicit loader");static std::atomic<std::uint64_t>identity{};prefix="activity-icon-"+std::to_string(++identity)+"/";
    }
    void thread()const{if(owner!=std::this_thread::get_id())throw std::logic_error("Activity icon loads belong to their creating thread");}
    bool load(std::vector<modules::ActivityAppIcons::Request>requests,modules::ActivityAppIcons::Done done){
        thread();
        auto keys=std::make_shared<std::vector<std::pair<std::string,unsigned>>>();for(const auto&r:requests)keys->push_back({r.key,r.pixels});
        auto results=std::make_shared<std::vector<std::optional<ActivityIconPixels>>>(keys->size());const std::weak_ptr<Impl>weak=weak_from_this();
        const bool accepted=executor.submit(route,[keys,results,load=loader]{
            for(std::size_t n=0;n<keys->size();++n){const auto&[key,size]=(*keys)[n];try{auto value=load(key,size);if(value&&value->width==size&&value->height==size&&value->straightRGBA.size()==std::size_t(size)*size*4)(*results)[n]=std::move(value);}catch(const std::bad_alloc&){throw;}catch(...){}}
        },[weak,results,done=std::move(done)](std::exception_ptr error){if(auto self=weak.lock())self->complete(*results,done,bool(error));});
        if(!accepted){++counts.backpressure;return false;}
        inFlight=true;++counts.batches;return true;
    }
    void complete(std::vector<std::optional<ActivityIconPixels>>&results,const modules::ActivityAppIcons::Done&done,bool failed){
        thread();inFlight=false;std::vector<std::optional<modules::design::ImageSource>>out(results.size());
        for(std::size_t n=0;n<results.size();++n){
            if(failed||!results[n]){++counts.unavailable;continue;}
            const auto&image=*results[n];const auto key=prefix+std::to_string(++nextKey);
            if(!images.publish(key,1,image.width,image.height,image.straightRGBA)){++counts.unavailable;continue;}
            out[n]=modules::design::ImageSource{modules::design::ImageSource::Kind::memory,key,{},1};++counts.loaded;
        }
        auto published=out;
        if(!done||!done(std::move(out)))for(const auto&image:published)if(image)retire(*image);
    }
    void retire(const modules::design::ImageSource&image){if(images.retire(image.key))++counts.retired;}
};
ActivityIconLoads::ActivityIconLoads(app::UtilityExecutor&executor,ActivityIconLoader loader):impl_(std::make_shared<Impl>(executor,std::move(loader))){}
ActivityIconLoads::~ActivityIconLoads(){impl_->executor.invalidate(impl_->route);}
modules::ActivityAppIcons::Loader ActivityIconLoads::loader(){
    auto*i=impl_.get();
    return {[i](std::vector<modules::ActivityAppIcons::Request>requests,modules::ActivityAppIcons::Done done){return i->load(std::move(requests),std::move(done));},
            [i](const modules::design::ImageSource&image){i->retire(image);}};
}
LayerImageSource&ActivityIconLoads::images()noexcept{return impl_->images;}
ActivityIconStats ActivityIconLoads::stats()const{const auto&i=*impl_;i.thread();auto out=i.counts;out.images=i.images.stats().cachedImages;out.inFlight=i.inFlight;return out;}
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
