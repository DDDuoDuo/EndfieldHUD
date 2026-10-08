#include "native/shelf_icon_provider.hpp"
namespace endfield::native {
bool validShelfIconRequest(const ShelfIconRequest&r)noexcept{
    return !r.imageKey.empty()&&r.imageKey.size()<=LayerImageSource::maximumKeyBytes&&
        ehud::data::Json::validUtf8(r.imageKey)&&r.imageKey.find('\0')==std::string::npos&&r.revision>0&&
        ehud::data::validUUID(r.itemID)&&ehud::data::validWindowsFilePath(r.path)&&
        (!r.identity.volumeUUID||ehud::data::validUUID(*r.identity.volumeUUID));
}
}
#ifdef _WIN32
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <process.h>
#include <shlobj.h>
#include <commoncontrols.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
struct Failure:std::exception{HRESULT result;explicit Failure(HRESULT value):result(value){}const char*what()const noexcept override{return "Native Shelf icon operation failed";}};void checked(HRESULT hr){if(FAILED(hr))throw Failure{hr};}
// Deliberately process-lifetime: a blocked Shell thread can outlive C++ static
// teardown. One atomic gate, not a registry/worker pool or a second service.
std::atomic<bool>&workerGate(){static auto*gate=new std::atomic<bool>(false);return *gate;}
struct Handle{HANDLE value{};~Handle(){if(value)CloseHandle(value);}Handle()=default;Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;};
struct Bitmap{HBITMAP value{};~Bitmap(){if(value)DeleteObject(value);}};
struct Icon{HICON value{};~Icon(){if(value)DestroyIcon(value);}};
std::wstring wide(std::string_view s){const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(size<=0)throw Failure{HRESULT_FROM_WIN32(GetLastError())};std::wstring out(static_cast<std::size_t>(size),L'\0');if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),size)!=size)throw Failure{HRESULT_FROM_WIN32(GetLastError())};return out;}
void validate(const ShelfIconRequest&r){
    need(validShelfIconRequest(r),"Invalid Shelf icon request identity, revision or Windows path");
}
void validateRoute(const ShelfIconRoute&r){
    if(!r.owner){need(r.message==0&&r.generation==0,"Empty Shelf icon route must have no message or generation");return;}
    DWORD process{};const auto thread=GetWindowThreadProcessId(r.owner,&process);
    need(IsWindow(r.owner)&&thread==GetCurrentThreadId()&&process==GetCurrentProcessId()&&r.message>=WM_APP&&r.message<=0xbfff&&r.generation!=0,
        "Shelf icon notice requires a live owner-thread window, private message and generation");
}
ehud::data::ShelfRecord record(const ShelfIconRequest&r){ehud::data::ShelfRecord out;out.id=r.itemID;out.windowsPath=r.path;out.isDirectory=r.directory;out.identity=r.identity;return out;}
ShelfIconPixels rgba(IWICImagingFactory*wic,IWICBitmapSource*source,bool fallback){
    UINT width{},height{};checked(source->GetSize(&width,&height));need(width>0&&height>0&&width<=256&&height<=256,"Shell icon exceeds bounded source dimensions");
    constexpr UINT size=LayerImageSource::shelfRequestedPixels;const double scale=std::min(double(size)/width,double(size)/height);
    const auto targetWidth=std::max(1u,static_cast<unsigned>(std::lround(width*scale))),targetHeight=std::max(1u,static_cast<unsigned>(std::lround(height*scale)));
    ComPtr<IWICBitmapScaler>scaler;checked(wic->CreateBitmapScaler(&scaler));checked(scaler->Initialize(source,targetWidth,targetHeight,WICBitmapInterpolationModeFant));
    ComPtr<IWICFormatConverter>converter;checked(wic->CreateFormatConverter(&converter));checked(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    std::vector<std::uint8_t>tight(std::size_t(targetWidth)*targetHeight*4);checked(converter->CopyPixels(nullptr,targetWidth*4,static_cast<UINT>(tight.size()),tight.data()));
    ShelfIconPixels result;result.width=result.height=size;result.result=S_OK;result.typeFallback=fallback;result.straightRGBA.resize(std::size_t(size)*size*4);
    const unsigned x=(size-targetWidth)/2,y=(size-targetHeight)/2;for(unsigned row=0;row<targetHeight;++row)std::copy_n(tight.data()+std::size_t(row)*targetWidth*4,std::size_t(targetWidth)*4,result.straightRGBA.data()+(std::size_t(y+row)*size+x)*4);return result;
}
ShelfIconPixels typeIcon(IWICImagingFactory*wic,const ShelfIconRequest&r){
    // USEFILEATTRIBUTES forbids access to this synthetic parsing name. Preserve
    // extension association, but never send the unavailable user's full path.
    std::string token="file";const auto slash=r.path.find_last_of("/\\"),dot=r.path.find_last_of('.');if(!r.directory&&dot!=std::string::npos&&(slash==std::string::npos||dot>slash)&&r.path.size()-dot<=128)token+=r.path.substr(dot);
    if(r.directory)token="folder";const auto name=wide(token);SHFILEINFOW info{};
    if(!SHGetFileInfoW(name.c_str(),r.directory?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL,&info,static_cast<UINT>(sizeof(info)),SHGFI_USEFILEATTRIBUTES|SHGFI_SYSICONINDEX))throw Failure{E_FAIL};
    ComPtr<IImageList>list;checked(SHGetImageList(SHIL_JUMBO,IID_PPV_ARGS(&list)));Icon icon;checked(list->GetIcon(info.iIcon,ILD_TRANSPARENT,&icon.value));
    ComPtr<IWICBitmap>bitmap;checked(wic->CreateBitmapFromHICON(icon.value,&bitmap));return rgba(wic,bitmap.Get(),true);
}
ShelfIconPixels shellIcon(IWICImagingFactory*wic,const ShelfIconRequest&r,const ehud::data::ShelfFileAccess*lease){
    if(!lease||lease->metadata().kind==ehud::data::ShelfFileKind::symbolicLink)return typeIcon(wic,r);
    try{const auto path=wide(lease->metadata().windowsPath);ComPtr<IShellItemImageFactory>factory;checked(SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&factory)));
        Bitmap bitmap;checked(factory->GetImage({LayerImageSource::shelfRequestedPixels,LayerImageSource::shelfRequestedPixels},SIIGBF_ICONONLY,&bitmap.value));need(bitmap.value!=nullptr,"Shell returned an empty icon bitmap");
        ComPtr<IWICBitmap>decoded;checked(wic->CreateBitmapFromHBITMAP(bitmap.value,nullptr,WICBitmapUsePremultipliedAlpha,&decoded));return rgba(wic,decoded.Get(),false);
    }catch(const std::bad_alloc&){throw;}catch(...){return typeIcon(wic,r);}
}
struct Job{ShelfIconRequest request;std::uint64_t generation{};};
struct Ready{ShelfIconRequest request;ShelfIconPixels pixels;};
struct State {
    std::mutex mutex;Handle work;NativeShelfIconProvider::Resolver resolver;NativeShelfIconProvider::Extractor extractor;
    std::vector<Job>queue;std::array<std::optional<Ready>,8>ready;std::size_t readyCount{};
    ShelfIconRoute route;std::uint64_t generation{1};bool stopping{},noticePosted{};ShelfIconProviderStats stats;
    State(NativeShelfIconProvider::Resolver r,NativeShelfIconProvider::Extractor e):resolver(std::move(r)),extractor(std::move(e)){queue.reserve(8);work.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!work.value)throw Failure{HRESULT_FROM_WIN32(GetLastError())};}
    void notice(){if(readyCount&&!noticePosted&&route.owner){if(PostMessageW(route.owner,route.message,route.generation,0)){noticePosted=true;++stats.notices;}}}
    void clearReady(){for(std::size_t n=0;n<readyCount;++n)ready[n].reset();stats.discarded+=readyCount;readyCount=0;noticePosted=false;}
};
bool stale(const std::shared_ptr<State>&s,std::uint64_t generation){std::lock_guard lock(s->mutex);return s->stopping||s->generation!=generation;}
unsigned __stdcall run(void*opaque)noexcept{
    std::unique_ptr<std::shared_ptr<State>>argument(static_cast<std::shared_ptr<State>*>(opaque));auto state=std::move(*argument);argument.reset();
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);ComPtr<IWICImagingFactory>wic;HRESULT codec=initialized;
    if(SUCCEEDED(codec)&&!state->extractor)codec=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));
    try{for(;;){
        const auto signaled=MsgWaitForMultipleObjectsEx(1,&state->work.value,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        if(signaled==WAIT_FAILED)break;
        if(signaled==WAIT_OBJECT_0+1){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){std::lock_guard lock(state->mutex);state->stopping=true;SetEvent(state->work.value);break;}TranslateMessage(&message);DispatchMessageW(&message);}continue;}
        std::optional<Job>job;{std::lock_guard lock(state->mutex);++state->stats.wakeups;if(state->stopping)break;if(!state->queue.empty()){job=std::move(state->queue.front());state->queue.erase(state->queue.begin());state->stats.inFlight=true;++state->stats.extractions;}if(state->queue.empty())ResetEvent(state->work.value);}
        if(!job)continue;ShelfIconPixels pixels;
        try{
            if(FAILED(codec))throw Failure{codec};if(stale(state,job->generation))throw Failure{E_ABORT};
            ehud::data::ShelfFileAccess lease;
            if(!job->request.unavailable){try{lease=state->resolver(record(job->request));need(lease.open()&&job->request.identity.matches(lease.metadata().identity),"Shelf icon resolver returned a different identity");}catch(const std::bad_alloc&){throw;}catch(...){lease.close();}}
            if(stale(state,job->generation))throw Failure{E_ABORT};
            pixels=state->extractor?state->extractor(job->request,lease.open()?&lease:nullptr):shellIcon(wic.Get(),job->request,lease.open()?&lease:nullptr);
            if(SUCCEEDED(pixels.result))need(pixels.width==64&&pixels.height==64&&pixels.straightRGBA.size()==64u*64u*4u,"Shelf icon extractor must return bounded64px RGBA");else pixels.straightRGBA.clear();
            // Lease closes here, before any UI notification or cache publish.
        }catch(const Failure&e){pixels={};pixels.result=e.result;}catch(const std::bad_alloc&){pixels={};pixels.result=E_OUTOFMEMORY;}catch(...){pixels={};pixels.result=E_FAIL;}
        {std::lock_guard lock(state->mutex);state->stats.inFlight=false;if(state->stopping||state->generation!=job->generation){++state->stats.discarded;}else if(state->readyCount<state->ready.size()){state->ready[state->readyCount++]=Ready{std::move(job->request),std::move(pixels)};state->notice();}}
        // Keep the STA responsive between requests without adding periodic wakeups.
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){std::lock_guard lock(state->mutex);state->stopping=true;SetEvent(state->work.value);break;}TranslateMessage(&message);DispatchMessageW(&message);}
    }}catch(...){/* A failed worker never crosses into owner callbacks. */}
    {std::lock_guard lock(state->mutex);state->route={};state->queue.clear();state->clearReady();state->stats.inFlight=false;state->stopping=true;}
    // Worker-owned dependency destructors run outside the mutex and before its
    // apartment closes. A stopped facade may retain only diagnostic state.
    wic.Reset();state->resolver={};state->extractor={};
    if(SUCCEEDED(initialized))CoUninitialize();{std::lock_guard lock(state->mutex);state->stats.stopped=true;}state.reset();workerGate().store(false,std::memory_order_release);return 0;
}
}
struct NativeShelfIconProvider::Impl {
    const DWORD owner{GetCurrentThreadId()};LayerImageSource*images;std::shared_ptr<State>state;Handle worker;std::vector<ShelfIconRequest>visible;bool showing{};
    Impl(LayerImageSource&i):images(&i){visible.reserve(8);}
    void onThread()const{need(owner==GetCurrentThreadId(),"Shelf icon owner API requires its creating thread");}
};
NativeShelfIconProvider::NativeShelfIconProvider(LayerImageSource&images,Resolver resolver,ShelfIconRoute route,Extractor extractor):impl_(std::make_unique<Impl>(images)){
    need(bool(resolver),"Shelf icons require an independent worker-safe metadata resolver");validateRoute(route);bool expected{};need(workerGate().compare_exchange_strong(expected,true,std::memory_order_acq_rel),"Previous Shelf icon worker is still active");
    try{auto state=std::make_shared<State>(std::move(resolver),std::move(extractor));state->route=route;auto argument=std::make_unique<std::shared_ptr<State>>(state);
        const auto handle=_beginthreadex(nullptr,0,run,argument.get(),0,nullptr);if(!handle)throw std::runtime_error("Cannot start bounded Shelf icon worker");argument.release();impl_->worker.value=reinterpret_cast<HANDLE>(handle);impl_->state=std::move(state);
    }catch(...){workerGate().store(false,std::memory_order_release);throw;}
}
NativeShelfIconProvider::~NativeShelfIconProvider(){stop();}
bool NativeShelfIconProvider::setVisible(std::span<const ShelfIconRequest>requests,bool retry){
    auto&i=*impl_;i.onThread();{std::lock_guard lock(i.state->mutex);need(!i.state->stopping&&!i.state->stats.stopped,"Shelf icon provider has stopped");}need(requests.size()<=maximumVisible,"Shelf icon visible request bound exceeded");for(std::size_t n=0;n<requests.size();++n){validate(requests[n]);for(std::size_t k=0;k<n;++k)need(requests[k].imageKey!=requests[n].imageKey&&requests[k].itemID!=requests[n].itemID,"Repeated Shelf icon request");}
    if(i.showing&&!retry&&std::equal(requests.begin(),requests.end(),i.visible.begin(),i.visible.end()))return false;
    std::vector<ShelfIconRequest>visible(requests.begin(),requests.end());std::vector<Job>queue;queue.reserve(8);for(const auto&r:requests)if(!i.images->acquire(r.imageKey,r.revision))queue.push_back({r,0});
    {std::lock_guard lock(i.state->mutex);need(!i.state->stopping&&!i.state->stats.stopped,"Shelf icon provider has stopped");need(i.state->generation<std::numeric_limits<std::uint64_t>::max(),"Shelf icon generation exhausted");const auto generation=++i.state->generation;for(auto&job:queue)job.generation=generation;i.state->queue=std::move(queue);i.state->clearReady();++i.state->stats.requests;SetEvent(i.state->work.value);}
    i.visible=std::move(visible);i.showing=true;return true;
}
void NativeShelfIconProvider::hide(){auto&i=*impl_;i.onThread();std::lock_guard lock(i.state->mutex);if(!i.showing||i.state->stopping)return;need(i.state->generation<std::numeric_limits<std::uint64_t>::max(),"Shelf icon generation exhausted");++i.state->generation;i.state->queue.clear();i.state->clearReady();i.visible.clear();i.showing=false;ResetEvent(i.state->work.value);}
void NativeShelfIconProvider::setRoute(ShelfIconRoute route){auto&i=*impl_;i.onThread();validateRoute(route);std::lock_guard lock(i.state->mutex);need(!i.state->stopping&&!i.state->stats.stopped,"Shelf icon provider has stopped");i.state->route=route;i.state->noticePosted=false;i.state->notice();}
std::vector<ShelfIconCompletion>NativeShelfIconProvider::drain(UINT_PTR generation){
    auto&i=*impl_;i.onThread();std::array<std::optional<Ready>,8>ready;std::size_t count{};
    {std::lock_guard lock(i.state->mutex);if(generation!=i.state->route.generation||i.state->stopping||!i.showing||!i.state->readyCount)return {};count=i.state->readyCount;for(std::size_t n=0;n<count;++n){ready[n]=std::move(i.state->ready[n]);i.state->ready[n].reset();}i.state->readyCount=0;i.state->noticePosted=false;}
    std::vector<ShelfIconCompletion>results;results.reserve(count);for(std::size_t n=0;n<count;++n){auto&item=*ready[n];ShelfIconCompletion result;result.imageKey=std::move(item.request.imageKey);result.itemID=std::move(item.request.itemID);result.revision=item.request.revision;result.result=item.pixels.result;result.typeFallback=item.pixels.typeFallback;
        if(SUCCEEDED(result.result))try{result.image=i.images->publish(result.imageKey,result.revision,item.pixels.width,item.pixels.height,item.pixels.straightRGBA);}catch(const std::bad_alloc&){result.result=E_OUTOFMEMORY;}catch(...){result.result=E_INVALIDARG;}
        results.push_back(std::move(result));}return results;
}
void NativeShelfIconProvider::stop()noexcept{if(!impl_||!impl_->state)return;auto&s=*impl_->state;std::lock_guard lock(s.mutex);s.route={};s.stopping=true;s.queue.clear();s.clearReady();SetEvent(s.work.value);}
ShelfIconProviderStats NativeShelfIconProvider::stats()const{const auto&i=*impl_;i.onThread();std::lock_guard lock(i.state->mutex);auto stats=i.state->stats;stats.queued=i.state->queue.size();stats.completed=i.state->readyCount;return stats;}
HANDLE NativeShelfIconProvider::duplicateWorkerHandle()const{const auto&i=*impl_;i.onThread();HANDLE copy{};if(!DuplicateHandle(GetCurrentProcess(),i.worker.value,GetCurrentProcess(),&copy,SYNCHRONIZE,FALSE,0))throw std::runtime_error("Cannot duplicate Shelf worker completion handle");return copy;}
} // namespace endfield::native
#endif
