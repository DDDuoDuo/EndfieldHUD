#include "native/audio_default_device.hpp"
#include <array>
#include <bit>
#include <stdexcept>
#include <utility>

namespace endfield::native {
namespace {
constexpr auto status(std::uint32_t value){return std::bit_cast<std::int32_t>(value);}
constexpr auto invalid=status(0x80070057),unavailable=status(0x80004001),failed=status(0x80004005),readback=status(0x800704D5),busy=status(0x800700AA);
template<class F>std::int32_t invoke(F&&f)noexcept{try{return f();}catch(const std::bad_alloc&){return status(0x8007000E);}catch(...){return failed;}}
bool validID(std::wstring_view id){if(id.empty()||id.size()>32767)return false;for(std::size_t n=0;n<id.size();++n){auto c=static_cast<std::uint32_t>(id[n]);if(!c)return false;if constexpr(sizeof(wchar_t)==2){if(c>=0xd800&&c<=0xdbff){if(++n==id.size())return false;const auto low=static_cast<std::uint32_t>(id[n]);if(low<0xdc00||low>0xdfff)return false;continue;}}if(c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;}return true;}
constexpr std::array roles{AudioDefaultRole::console,AudioDefaultRole::multimedia};
}
AudioDefaultDeviceSelector::AudioDefaultDeviceSelector(AudioDefaultDeviceAccess access):access_(std::move(access)){
    if(!access_.capability||!access_.validateEndpoint||!access_.current||!access_.select)throw std::invalid_argument("Default-device selector requires complete injected/native access");
}
AudioDefaultDeviceCapability AudioDefaultDeviceSelector::capability(){return access_.capability();}
AudioDefaultDeviceResult AudioDefaultDeviceSelector::select(AudioDefaultFlow flow,std::wstring_view requested){
    if(busy_)return {busy};
    if((flow!=AudioDefaultFlow::output&&flow!=AudioDefaultFlow::input)||!validID(requested))return result_={invalid};
    const auto supported=capability();if(!supported.supported)return result_={supported.status<0?supported.status:unavailable};
    struct Guard{bool&flag;Guard(bool&v):flag(v){flag=true;}~Guard(){flag=false;}}guard(busy_);
    AudioDefaultDeviceResult next;const std::wstring target(requested);std::array<std::wstring,2>originals;std::array<bool,2>attempted{};
    next.status=invoke([&]{return access_.validateEndpoint(target,flow);});if(next.status<0)return result_=next;
    for(std::size_t n=0;n<roles.size();++n){next.status=invoke([&]{return access_.current(flow,roles[n],originals[n]);});if(next.status<0)return result_=next;if(!validID(originals[n]))return result_={readback};}
    for(std::size_t n=0;n<roles.size();++n){
        if(originals[n]==target)continue;
        next.status=invoke([&]{return access_.validateEndpoint(target,flow);});if(next.status<0)break;
        // A failure may have changed the endpoint before reporting its error.
        attempted[n]=true;++next.writes;next.status=invoke([&]{return access_.select(target,roles[n]);});
        if(next.status<0)break;
        std::wstring actual;next.status=invoke([&]{return access_.current(flow,roles[n],actual);});if(next.status<0)break;
        if(actual!=target){next.status=readback;next.concurrentChange=true;break;}
    }
    if(next.status>=0){next.changed=next.writes!=0;return result_=next;}
    for(std::size_t offset=roles.size();offset>0;--offset){const auto n=offset-1;if(!attempted[n])continue;
        std::wstring actual;auto error=invoke([&]{return access_.current(flow,roles[n],actual);});
        if(error<0){if(next.rollbackStatus>=0)next.rollbackStatus=error;continue;}
        if(actual==originals[n])continue;
        if(actual!=target){next.concurrentChange=true;continue;}
        error=invoke([&]{return access_.validateEndpoint(originals[n],flow);});
        if(error>=0)error=invoke([&]{return access_.select(originals[n],roles[n]);});
        if(error>=0){error=invoke([&]{return access_.current(flow,roles[n],actual);});if(error>=0&&actual!=originals[n])error=readback;}
        if(error<0){if(next.rollbackStatus>=0)next.rollbackStatus=error;}else ++next.restored;
    }
    return result_=next;
}
} // namespace endfield::native

#if defined(_WIN32)&&!defined(ENDFIELD_AUDIO_UNDOCUMENTED_POLICY_CONFIG)
namespace endfield::native {
// Platform gap (default build): Windows has no documented public API that
// changes the default playback or recording endpoint (MMDevice API and
// Windows.Media.Devices.MediaDevice only READ the defaults). The project rule
// forbids shipping undocumented interfaces, so the native access reports an
// unsupported capability and never creates a COM object, reads an endpoint or
// changes routing. Volume's device chooser therefore stays disabled, exactly
// like the source when kAudioHardwarePropertyDefaultOutputDevice is not
// settable; Windows Settings > System > Sound remains the way to switch.
AudioDefaultDeviceAccess nativeAudioDefaultDeviceAccess(){
    constexpr auto unsupported=status(0x80004001); // E_NOTIMPL
    AudioDefaultDeviceAccess access;
    access.capability=[]{AudioDefaultDeviceCapability c;c.status=unsupported;return c;};
    access.validateEndpoint=[](std::wstring_view,AudioDefaultFlow){return unsupported;};
    access.current=[](AudioDefaultFlow,AudioDefaultRole,std::wstring&){return unsupported;};
    access.select=[](std::wstring_view,AudioDefaultRole){return unsupported;};
    return access;
}
}
#elif defined(_WIN32)
// Explicit opt-in research build only (never set by the project CMake files).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <winternl.h>
#include <wrl/client.h>

namespace endfield::native {
namespace {
// Interop declarations are adapted from EarTrumpet commit
// 154980811087fb7c423d806e2d9816733db6a7a5:
// EarTrumpet/Interop/MMDeviceAPI/{IPolicyConfig,PolicyConfigClient}.cs.
// Full upstream notice: audio_default_device_notice.txt. This is NOT an SDK
// interface or a Microsoft compatibility guarantee. EarTrumpet explicitly
// uses different IIDs before Windows10 RS1, which are intentionally unsupported.
// Only slot 13 (IUnknown + ten preceding methods) is invoked. The unused slots
// are placeholders and must never be called through these declarations.
struct __declspec(uuid("F8679F50-850A-41CF-9C72-430F290290C8")) PolicyConfig : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Unused1()=0;virtual HRESULT STDMETHODCALLTYPE Unused2()=0;
    virtual HRESULT STDMETHODCALLTYPE Unused3()=0;virtual HRESULT STDMETHODCALLTYPE Unused4()=0;
    virtual HRESULT STDMETHODCALLTYPE Unused5()=0;virtual HRESULT STDMETHODCALLTYPE Unused6()=0;
    virtual HRESULT STDMETHODCALLTYPE Unused7()=0;virtual HRESULT STDMETHODCALLTYPE Unused8()=0;
    virtual HRESULT STDMETHODCALLTYPE Unused9()=0;virtual HRESULT STDMETHODCALLTYPE Unused10()=0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(LPCWSTR,ERole)=0;
};
constexpr GUID policyClass{0x870af99c,0x171d,0x4f9e,{0xaf,0x0d,0xe6,0x3d,0xf4,0x0c,0x2b,0xc9}};
ERole role(AudioDefaultRole value){switch(value){case AudioDefaultRole::console:return eConsole;case AudioDefaultRole::multimedia:return eMultimedia;case AudioDefaultRole::communications:return eCommunications;}return ERole_enum_count;}
EDataFlow flow(AudioDefaultFlow value){return value==AudioDefaultFlow::input?eCapture:eRender;}
struct NativeAccess {
    DWORD thread{GetCurrentThreadId()};bool attempted{},uninitialize{};AudioDefaultDeviceCapability info;
    Microsoft::WRL::ComPtr<PolicyConfig>policy;Microsoft::WRL::ComPtr<IMMDeviceEnumerator>enumerator;
    ~NativeAccess(){policy.Reset();enumerator.Reset();if(uninitialize)CoUninitialize();}
    bool owner()const noexcept{return thread==GetCurrentThreadId();}
    AudioDefaultDeviceCapability capability(){
        if(!owner())return {false,0,0,0,static_cast<std::int32_t>(RPC_E_WRONG_THREAD)};
        if(attempted)return info;attempted=true;
        using Version=LONG(WINAPI*)(PRTL_OSVERSIONINFOW);const auto module=GetModuleHandleW(L"ntdll.dll");
        const auto version=module?reinterpret_cast<Version>(GetProcAddress(module,"RtlGetVersion")):nullptr;
        RTL_OSVERSIONINFOW os{};os.dwOSVersionInfoSize=static_cast<ULONG>(sizeof(os));
        if(!version||version(&os)<0){info.status=E_NOTIMPL;return info;}
        info.windowsMajor=os.dwMajorVersion;info.windowsMinor=os.dwMinorVersion;info.windowsBuild=os.dwBuildNumber;
        if(os.dwMajorVersion!=10||os.dwBuildNumber<14393){info.status=E_NOTIMPL;return info;}
        auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);uninitialize=SUCCEEDED(hr);
        if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE){info.status=hr;return info;}
        hr=CoCreateInstance(policyClass,nullptr,CLSCTX_INPROC_SERVER,__uuidof(PolicyConfig),reinterpret_cast<void**>(policy.GetAddressOf()));
        if(SUCCEEDED(hr))hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(enumerator.GetAddressOf()));
        info.status=hr;info.supported=SUCCEEDED(hr);if(!info.supported){policy.Reset();enumerator.Reset();}return info;
    }
    HRESULT validate(std::wstring_view id,AudioDefaultFlow kind){
        const auto ready=capability();if(!ready.supported)return ready.status;
        const std::wstring stable(id);Microsoft::WRL::ComPtr<IMMDevice>device;auto hr=enumerator->GetDevice(stable.c_str(),&device);if(FAILED(hr))return hr;
        DWORD state{};hr=device->GetState(&state);if(FAILED(hr))return hr;if(state!=DEVICE_STATE_ACTIVE)return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
        Microsoft::WRL::ComPtr<IMMEndpoint>endpoint;hr=device.As(&endpoint);if(FAILED(hr))return hr;EDataFlow actual{};hr=endpoint->GetDataFlow(&actual);return FAILED(hr)?hr:actual==flow(kind)?S_OK:E_INVALIDARG;
    }
    HRESULT current(AudioDefaultFlow kind,AudioDefaultRole selected,std::wstring&result){
        const auto ready=capability();if(!ready.supported)return ready.status;
        Microsoft::WRL::ComPtr<IMMDevice>device;auto hr=enumerator->GetDefaultAudioEndpoint(flow(kind),role(selected),&device);if(FAILED(hr))return hr;
        LPWSTR id{};hr=device->GetId(&id);if(FAILED(hr))return hr;
        try{if(id)result=id;else hr=E_UNEXPECTED;}catch(...){CoTaskMemFree(id);throw;}CoTaskMemFree(id);return hr;
    }
    HRESULT select(std::wstring_view id,AudioDefaultRole selected){const auto ready=capability();if(!ready.supported)return ready.status;const std::wstring stable(id);return policy->SetDefaultEndpoint(stable.c_str(),role(selected));}
};
}
AudioDefaultDeviceAccess nativeAudioDefaultDeviceAccess(){const auto native=std::make_shared<NativeAccess>();AudioDefaultDeviceAccess access;
    access.capability=[native]{return native->capability();};
    access.validateEndpoint=[native](auto id,auto selected){return static_cast<std::int32_t>(native->validate(id,selected));};
    access.current=[native](auto selected,auto r,auto&out){return static_cast<std::int32_t>(native->current(selected,r,out));};
    access.select=[native](auto id,auto r){return static_cast<std::int32_t>(native->select(id,r));};return access;
}
}
#endif
