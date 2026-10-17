#include "native/audio_service.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <propidl.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <utility>

namespace endfield::native {
// Documented property keys, spelled out locally so this unit needs neither
// initguid.h nor a uuid.lib entry (functiondiscoverykeys_devpkey.h,
// mmdeviceapi.h): PKEY_Device_FriendlyName, PKEY_Device_EnumeratorName,
// PKEY_AudioEndpoint_FormFactor.
namespace {
using Microsoft::WRL::ComPtr;
constexpr PROPERTYKEY friendlyNameKey{{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};
constexpr PROPERTYKEY enumeratorNameKey{{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},24};
constexpr PROPERTYKEY formFactorKey{{0x1da5d803,0xd492,0x4edd,{0x8c,0x23,0xe0,0xc0,0xff,0xee,0x7f,0x0e}},0};
// Event context of EndfieldHUD's own endpoint writes. Their change
// notifications carry it and are skipped: the worker already reads back.
constexpr GUID ownWrites{0x6d1d2b0f,0x5d5a,0x4f0c,{0x9b,0x3e,0x2f,0x6c,0x8a,0x4e,0x1a,0x71}};
bool sameKey(const PROPERTYKEY&a,const PROPERTYKEY&b)noexcept{return a.pid==b.pid&&IsEqualGUID(a.fmtid,b.fmtid);}
struct Prop {PROPVARIANT value;Prop(){PropVariantInit(&value);}~Prop(){PropVariantClear(&value);}Prop(const Prop&)=delete;Prop&operator=(const Prop&)=delete;};
std::wstring text(IPropertyStore*store,const PROPERTYKEY&key){
    Prop p;if(!store||FAILED(store->GetValue(key,&p.value))||p.value.vt!=VT_LPWSTR||!p.value.pwszVal)return {};
    const std::wstring_view v(p.value.pwszVal);return v.size()>32767?std::wstring{}:std::wstring(v);
}
std::optional<std::uint32_t>number(IPropertyStore*store,const PROPERTYKEY&key){
    Prop p;if(!store||FAILED(store->GetValue(key,&p.value))||p.value.vt!=VT_UI4)return std::nullopt;return p.value.ulVal;
}
std::wstring identity(IMMDevice*device){
    LPWSTR value{};if(!device||FAILED(device->GetId(&value))||!value)return {};
    std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)>owned(value,&CoTaskMemFree);return value;
}
// Notification objects own an immutable wake. A late callback after
// unregistration only raises a bit on the shared signal: never UI, never COM.
class DeviceEvents final:public IMMNotificationClient {
    std::atomic<ULONG>references{1};AudioEndpointBackend::Wake wake;
    HRESULT changed()noexcept{try{wake();}catch(...){}return S_OK;}
public:
    explicit DeviceEvents(AudioEndpointBackend::Wake w):wake(std::move(w)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IMMNotificationClient)){*out=static_cast<IMMNotificationClient*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,ERole role,LPCWSTR)override{
        // The source follows the system default output/input; only the
        // console role is observed (communications routing is not a control).
        if((flow==eRender||flow==eCapture)&&role==eConsole)return changed();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY key)override{
        if(sameKey(key,friendlyNameKey)||sameKey(key,formFactorKey)||sameKey(key,enumeratorNameKey))return changed();return S_OK;
    }
};
class VolumeEvents final:public IAudioEndpointVolumeCallback {
    std::atomic<ULONG>references{1};AudioEndpointBackend::Wake wake;
public:
    explicit VolumeEvents(AudioEndpointBackend::Wake w):wake(std::move(w)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IAudioEndpointVolumeCallback)){*out=static_cast<IAudioEndpointVolumeCallback*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data)override{
        if(data&&IsEqualGUID(data->guidEventContext,ownWrites))return S_OK;
        try{wake();}catch(...){}return S_OK;
    }
};
class NativeEndpoints final:public AudioEndpointBackend {
    HRESULT com;bool initialized;DWORD thread=GetCurrentThreadId();
    ComPtr<IMMDeviceEnumerator>enumerator;ComPtr<DeviceEvents>devices;bool registered{};
    ComPtr<IAudioEndpointVolume>volume;ComPtr<VolumeEvents>volumeEvents;bool volumeRegistered{};std::wstring bound;
    bool own()const noexcept{return thread==GetCurrentThreadId();}
    void unbind()noexcept{
        if(volumeRegistered&&volume&&volumeEvents)volume->UnregisterControlChangeNotify(volumeEvents.Get());
        volumeRegistered=false;volumeEvents.Reset();volume.Reset();bound.clear();
    }
public:
    NativeEndpoints():com(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),initialized(SUCCEEDED(com)){}
    ~NativeEndpoints()override{close();if(initialized)CoUninitialize();}
    std::int32_t open(Wake wake)override{
        if(!own())return RPC_E_WRONG_THREAD;if(FAILED(com))return com;if(enumerator)return S_OK;
        try{
            auto hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&enumerator));
            if(SUCCEEDED(hr)){devices.Attach(new DeviceEvents(std::move(wake)));hr=enumerator->RegisterEndpointNotificationCallback(devices.Get());registered=SUCCEEDED(hr);}
            if(FAILED(hr))close();return hr;
        }catch(const std::bad_alloc&){close();return E_OUTOFMEMORY;}
    }
    void close()noexcept override{
        if(!own())std::terminate();
        unbind();
        if(registered&&enumerator&&devices)enumerator->UnregisterEndpointNotificationCallback(devices.Get());
        registered=false;devices.Reset();enumerator.Reset();
    }
    std::int32_t enumerate(AudioFlow flow,std::vector<AudioEndpointRecord>&out)override{
        if(!own())return RPC_E_WRONG_THREAD;if(!enumerator)return E_PENDING;
        try{
            ComPtr<IMMDeviceCollection>list;auto hr=enumerator->EnumAudioEndpoints(flow==AudioFlow::input?eCapture:eRender,DEVICE_STATE_ACTIVE,&list);if(FAILED(hr))return hr;
            UINT count{};hr=list->GetCount(&count);if(FAILED(hr))return hr;if(count>maximumAudioEndpoints)return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
            std::vector<AudioEndpointRecord>next;next.reserve(count);
            for(UINT n=0;n<count;++n){
                ComPtr<IMMDevice>device;hr=list->Item(n,&device);if(FAILED(hr))return hr;
                AudioEndpointRecord r;r.id=identity(device.Get());if(r.id.empty())return E_UNEXPECTED;
                ComPtr<IPropertyStore>store;
                if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&store))){r.name=text(store.Get(),friendlyNameKey);r.enumerator=text(store.Get(),enumeratorNameKey);r.formFactor=number(store.Get(),formFactorKey);}
                next.push_back(std::move(r));
            }
            out=std::move(next);return S_OK;
        }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
    }
    std::int32_t defaultEndpoint(AudioFlow flow,std::wstring&out)override{
        if(!own())return RPC_E_WRONG_THREAD;if(!enumerator)return E_PENDING;
        ComPtr<IMMDevice>device;const auto hr=enumerator->GetDefaultAudioEndpoint(flow==AudioFlow::input?eCapture:eRender,eConsole,&device);
        if(FAILED(hr)){out.clear();return hr;} // E_NOTFOUND when no device is active
        try{out=identity(device.Get());}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
        return out.empty()?E_UNEXPECTED:S_OK;
    }
    std::int32_t bind(std::wstring_view id,Wake wake)override{
        if(!own())return RPC_E_WRONG_THREAD;if(!enumerator)return E_PENDING;
        if(!id.empty()&&volume&&bound==id)return S_OK;
        unbind();if(id.empty())return S_OK;
        try{
            const std::wstring stable(id);ComPtr<IMMDevice>device;auto hr=enumerator->GetDevice(stable.c_str(),&device);
            if(SUCCEEDED(hr))hr=device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(volume.GetAddressOf()));
            if(SUCCEEDED(hr)){volumeEvents.Attach(new VolumeEvents(std::move(wake)));hr=volume->RegisterControlChangeNotify(volumeEvents.Get());volumeRegistered=SUCCEEDED(hr);}
            if(FAILED(hr)){unbind();return hr;}
            bound=stable;return S_OK;
        }catch(const std::bad_alloc&){unbind();return E_OUTOFMEMORY;}
    }
    std::int32_t read(AudioEndpointVolumeState&s)override{
        if(!own())return RPC_E_WRONG_THREAD;if(!volume)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        s={};
        s.status=volume->GetMasterVolumeLevelScalar(&s.scalar);
        BOOL muted{};s.muteStatus=volume->GetMute(&muted);s.muted=muted!=FALSE;
        UINT channels{};s.channelStatus=volume->GetChannelCount(&channels);s.channels=channels;
        if(SUCCEEDED(s.channelStatus)&&channels==2){
            s.channelStatus=volume->GetChannelVolumeLevelScalar(0,&s.left);
            if(SUCCEEDED(s.channelStatus))s.channelStatus=volume->GetChannelVolumeLevelScalar(1,&s.right);
        }
        DWORD support{};if(SUCCEEDED(volume->QueryHardwareSupport(&support)))s.hardwareSupport=support;
        s.rangeStatus=volume->GetVolumeRange(&s.minimumDecibels,&s.maximumDecibels,&s.incrementDecibels);
        UINT step{},count{};s.stepStatus=volume->GetVolumeStepInfo(&step,&count);s.steps=count;
        return s.status;
    }
    std::int32_t setVolume(float value)override{if(!own())return RPC_E_WRONG_THREAD;if(!volume)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);if(!std::isfinite(value)||value<0||value>1)return E_INVALIDARG;return volume->SetMasterVolumeLevelScalar(value,&ownWrites);}
    std::int32_t setMute(bool value)override{if(!own())return RPC_E_WRONG_THREAD;if(!volume)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);return volume->SetMute(value?TRUE:FALSE,&ownWrites);}
    std::int32_t readChannel(unsigned channel,float&value)override{if(!own())return RPC_E_WRONG_THREAD;if(!volume)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);return volume->GetChannelVolumeLevelScalar(channel,&value);}
    std::int32_t writeChannel(unsigned channel,float value)override{if(!own())return RPC_E_WRONG_THREAD;if(!volume)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);if(!std::isfinite(value)||value<0||value>1)return E_INVALIDARG;return volume->SetChannelVolumeLevelScalar(channel,value,&ownWrites);}
};
}
std::function<std::unique_ptr<AudioEndpointBackend>()>nativeAudioEndpointBackendFactory(){return []{return std::make_unique<NativeEndpoints>();};}
AudioNameOrder nativeAudioNameOrder(){
    return [](std::wstring_view a,std::wstring_view b)->int{
        if(a.size()>32767||b.size()>32767)return audioNameCompare(a,b);
        const auto result=CompareStringEx(LOCALE_NAME_USER_DEFAULT,LINGUISTIC_IGNORECASE|SORT_DIGITSASNUMBERS,a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),nullptr,nullptr,0);
        if(result==CSTR_LESS_THAN)return -1;if(result==CSTR_GREATER_THAN)return 1;if(result==CSTR_EQUAL)return 0;
        return audioNameCompare(a,b);
    };
}
AudioServiceOptions nativeAudioServiceOptions(std::optional<std::filesystem::path>dataRoot){
    AudioServiceOptions o;o.endpoints=nativeAudioEndpointBackendFactory();o.sessions=nativeAudioSessionBackendFactory();o.order=nativeAudioNameOrder();
    if(dataRoot)o.journal=*dataRoot/L"Audio"/L"RouteJournal.json";
    return o;
}
} // namespace endfield::native
#endif
