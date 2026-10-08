#include "native/notes_video_playback.hpp"
#ifdef _WIN32
#include "core/data/data_store.hpp"
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfmediaengine.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <atomic>
#include <climits>
#include <limits>
namespace endfield::native::detail {
namespace {
using Microsoft::WRL::ComPtr;using Owner=NativeNotesVideoPlayback;
void checked(HRESULT hr,const char*what){if(FAILED(hr))throw RendererError(what,hr);}
// Balanced on the creating UI thread, after all owned engines stop. Never call
// MFStartup/Shutdown from a codec callback or static process initializer.
struct Platform {Platform(){checked(MFStartup(MF_VERSION,MFSTARTUP_NOSOCKET),"Initialize local-file Media Foundation");}~Platform(){MFShutdown();}};
struct NativeStatus {std::array<std::atomic<std::uint64_t>,64>events{};std::atomic<unsigned>lastEvent{};};
class Notify final:public IMFMediaEngineNotify {
    std::atomic<ULONG>refs_{1};Owner::Notify callback_;std::shared_ptr<NativeStatus>status_;
public:Notify(Owner::Notify callback,std::shared_ptr<NativeStatus>status):callback_(std::move(callback)),status_(std::move(status)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IMFMediaEngineNotify)){*out=static_cast<IMFMediaEngineNotify*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{const auto left=--refs_;if(!left)delete this;return left;}
    HRESULT STDMETHODCALLTYPE EventNotify(DWORD event,DWORD_PTR a,DWORD b)override{
        status_->lastEvent.store(event,std::memory_order_relaxed);const auto slot=event<32?event:(event>=1000&&event<1032?event-1000+32:64);if(slot<status_->events.size())status_->events[slot].fetch_add(1,std::memory_order_relaxed);
        // This flag is not requested, but fulfilling its contract is harmless
        // if a future OS emits it. Never block the Media Engine's Load thread.
        if(event==MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE){SetEvent(reinterpret_cast<HANDLE>(a));return S_OK;}
        std::uint32_t flags{};HRESULT error=S_OK;
        switch(event){case MF_MEDIA_ENGINE_EVENT_CANPLAY:flags=Owner::ready;break;
            case MF_MEDIA_ENGINE_EVENT_FIRSTFRAMEREADY:flags=Owner::firstFrame;break;
            case MF_MEDIA_ENGINE_EVENT_SEEKED:flags=Owner::seeked;break;
            case MF_MEDIA_ENGINE_EVENT_ENDED:flags=Owner::ended;break;
            case MF_MEDIA_ENGINE_EVENT_ERROR:flags=Owner::failure;error=b?static_cast<HRESULT>(b):E_FAIL;break;
            default:break;}
        // TIMEUPDATE is deliberately not an application invalidation loop.
        // Source progress is sampled once per second by the shared owner clock.
        if(flags)try{callback_(flags,error);}catch(...){return E_FAIL;}return S_OK;
    }
};
std::wstring fileURL(std::string_view path){if(!ehud::data::validWindowsFilePath(path)||path.size()>static_cast<std::size_t>(INT_MAX))throw std::invalid_argument("Video requires explicit Windows file path");const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),nullptr,0);if(!size)throw std::invalid_argument("Invalid video UTF-8 path");std::wstring native(static_cast<std::size_t>(size),L'\0');checked(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),native.data(),size)==size?S_OK:E_INVALIDARG,"Convert explicit video path");
    std::wstring url(65536,L'\0');DWORD length=static_cast<DWORD>(url.size());checked(UrlCreateFromPathW(native.c_str(),url.data(),&length,0),"Convert video path to local URL");url.resize(length);return url;}
class Engine final:public Owner::Engine {
    std::shared_ptr<Platform>platform_;std::shared_ptr<void>device_,lease_;ComPtr<IMFDXGIDeviceManager>manager_;ComPtr<IMFMediaEngine>engine_;ComPtr<IMFMediaEngineEx>extended_;bool stopped_{};
    std::shared_ptr<NativeStatus>status_=std::make_shared<NativeStatus>();
public:Engine(std::shared_ptr<Platform>platform,std::shared_ptr<void>device,Owner::Notify callback):platform_(std::move(platform)),device_(std::move(device)){
        UINT token{};checked(MFCreateDXGIDeviceManager(&token,&manager_),"Create same-device video manager");checked(manager_->ResetDevice(static_cast<ID3D11Device*>(device_.get()),token),"Bind existing HUD graphics device");
        ComPtr<IMFAttributes>attributes;checked(MFCreateAttributes(&attributes,3),"Create Media Engine attributes");ComPtr<IMFMediaEngineNotify>notify;notify.Attach(new Notify(std::move(callback),status_));checked(attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK,notify.Get()),"Set generation-safe video callback");checked(attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER,manager_.Get()),"Set retained HUD DXGI manager");checked(attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT,DXGI_FORMAT_B8G8R8A8_UNORM),"Set encoded BGRA video surface");
        ComPtr<IMFMediaEngineClassFactory>factory;checked(CoCreateInstance(CLSID_MFMediaEngineClassFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create Media Engine factory");checked(factory->CreateInstance(0,attributes.Get(),&engine_),"Create video frame server");checked(engine_.As(&extended_),"Query exact video seek interface");checked(engine_->SetAutoPlay(FALSE),"Disable automatic video playback");checked(engine_->SetLoop(FALSE),"Preserve source paused-at-end behavior");
    }
    ~Engine()override{stop();}
    HRESULT open(const NotesVideoRequest&r)override{if(stopped_)return MF_E_SHUTDOWN;try{lease_=r.accessLease;const auto url=fileURL(r.path);BSTR source=SysAllocStringLen(url.data(),static_cast<UINT>(url.size()));if(!source)return E_OUTOFMEMORY;const auto result=engine_->SetSource(source);SysFreeString(source);return result;}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_INVALIDARG;}}
    HRESULT metadata(NotesVideoMetadata&value)override{if(stopped_)return MF_E_SHUTDOWN;DWORD width{},height{};const auto hr=engine_->GetNativeVideoSize(&width,&height);if(FAILED(hr))return hr;if(!engine_->HasVideo())return MF_E_INVALIDMEDIATYPE;value={width,height,engine_->GetDuration()};return S_OK;}
    HRESULT play()override{return stopped_?MF_E_SHUTDOWN:engine_->Play();}
    HRESULT pause()override{return stopped_?MF_E_SHUTDOWN:engine_->Pause();}
    HRESULT seek(double seconds)override{return stopped_?MF_E_SHUTDOWN:extended_->SetCurrentTimeEx(seconds,MF_MEDIA_ENGINE_SEEK_MODE_NORMAL);}
    double currentTime()const override{return stopped_?0:engine_->GetCurrentTime();}
    HRESULT tick(std::int64_t&pts)override{if(stopped_)return MF_E_SHUTDOWN;LONGLONG value{};const auto result=engine_->OnVideoStreamTick(&value);pts=value;return result;}
    HRESULT transfer(void*surface,unsigned width,unsigned height)override{if(stopped_)return MF_E_SHUTDOWN;if(!surface||width>LONG_MAX||height>LONG_MAX)return E_INVALIDARG;const RECT rect{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};const MFARGB transparent{};return engine_->TransferVideoFrame(static_cast<IDXGISurface*>(surface),nullptr,&rect,&transparent);}
    void stop()noexcept override{if(stopped_)return;stopped_=true;if(engine_){engine_->Pause();engine_->Shutdown();}extended_.Reset();engine_.Reset();manager_.Reset();lease_.reset();}
    NotesVideoDiagnostics diagnostics()const override{NotesVideoDiagnostics out;for(std::size_t i=0;i<out.eventCounts.size();++i)out.eventCounts[i]=status_->events[i].load(std::memory_order_relaxed);out.lastEvent=status_->lastEvent.load(std::memory_order_relaxed);if(engine_){out.readyState=engine_->GetReadyState();out.networkState=engine_->GetNetworkState();out.hasVideo=engine_->HasVideo()!=FALSE;out.paused=engine_->IsPaused()!=FALSE;out.seeking=engine_->IsSeeking()!=FALSE;ComPtr<IMFMediaError>error;if(SUCCEEDED(engine_->GetError(&error))&&error){out.errorCode=error->GetErrorCode();out.error=error->GetExtendedErrorCode();}}return out;}
};
}
NativeNotesVideoPlayback::Factory makeNotesMFVideoFactory(){auto platform=std::make_shared<Platform>();return [platform](std::shared_ptr<void>device,NativeNotesVideoPlayback::Notify callback){return std::make_unique<Engine>(platform,std::move(device),std::move(callback));};}
} // namespace endfield::native::detail
#endif
