#include "audio_session_worker.hpp"
#include "native/file_shelf_files.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <appmodel.h>
#include <shlwapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>
#include <cstddef>
#include <cwchar>
#if defined(_MSC_VER)
#pragma comment(lib,"shlwapi.lib")
#pragma comment(lib,"version.lib")
#endif

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
constexpr auto capacity=AudioSessionRoutes::maximumSessions;
// Event context of EndfieldHUD's own session writes. Their echo is skipped:
// the routes already hold the confirmed readback of every write they make.
constexpr GUID ownWrites{0x6d1d2b0f,0x5d5a,0x4f0c,{0x9b,0x3e,0x2f,0x6c,0x8a,0x4e,0x1a,0x71}};
std::string utf8Optional(std::wstring_view text)noexcept{
    try{if(text.empty()||text.size()>4096||text.find(L'\0')!=text.npos)return {};const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);if(length<=0||length>4096)return {};
        std::string out(static_cast<std::size_t>(length),'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),length,nullptr,nullptr)!=length)return {};return out;}catch(...){return {};}
}
// Indirect "@..." session names (packaged apps, system resources) resolve
// through the documented SHLoadIndirectString; failure keeps the fallback.
std::wstring indirect(std::wstring_view display){
    if(display.empty()||display.front()!=L'@'||display.size()>4096)return {};
    const std::wstring source(display);std::vector<wchar_t>buffer(1024);
    if(FAILED(SHLoadIndirectString(source.c_str(),buffer.data(),static_cast<UINT>(buffer.size()),nullptr)))return {};
    const std::wstring_view out(buffer.data());return out.empty()||out.front()==L'@'?std::wstring{}:std::wstring(out);
}
// The executable's own localized product label (VS_VERSIONINFO FileDescription),
// as the Windows mixer shows it; the source likewise prefers a bundle display
// name over the executable name. Read once per process identity.
std::wstring fileDescription(std::wstring_view path){
    try{
        if(path.empty()||path.size()>=32768)return {};const std::wstring file(path);DWORD ignored{};
        const auto size=GetFileVersionInfoSizeExW(FILE_VER_GET_NEUTRAL|FILE_VER_GET_LOCALISED,file.c_str(),&ignored);if(!size||size>1024*1024)return {};
        std::vector<std::byte>data(size);if(!GetFileVersionInfoExW(FILE_VER_GET_NEUTRAL|FILE_VER_GET_LOCALISED,file.c_str(),0,size,data.data()))return {};
        struct Translation{WORD language,codePage;};Translation*translations{};UINT bytes{};
        if(!VerQueryValueW(data.data(),L"\\VarFileInfo\\Translation",reinterpret_cast<void**>(&translations),&bytes)||!translations||bytes<sizeof(Translation))return {};
        for(UINT n=0;n<bytes/sizeof(Translation)&&n<16;++n){
            wchar_t key[64]{};swprintf_s(key,L"\\StringFileInfo\\%04x%04x\\FileDescription",translations[n].language,translations[n].codePage);
            wchar_t*value{};UINT length{};
            if(VerQueryValueW(data.data(),key,reinterpret_cast<void**>(&value),&length)&&value&&length>1){
                std::wstring_view text(value,wcsnlen(value,length));
                while(!text.empty()&&(text.back()==L' '||text.back()==L'\t'))text.remove_suffix(1);
                while(!text.empty()&&(text.front()==L' '||text.front()==L'\t'))text.remove_prefix(1);
                if(!text.empty()&&text.size()<=4096)return std::wstring(text);
            }
        }
    }catch(...){}
    return {};
}
std::string applicationIdentity(HANDLE process)noexcept{
    wchar_t buffer[APPLICATION_USER_MODEL_ID_MAX_LENGTH]{};UINT32 length=APPLICATION_USER_MODEL_ID_MAX_LENGTH;
    if(GetApplicationUserModelId(process,&length,buffer)!=ERROR_SUCCESS)return {};
    return utf8Optional(std::wstring_view(buffer,wcsnlen(buffer,APPLICATION_USER_MODEL_ID_MAX_LENGTH)));
}
struct Handle{HANDLE value{};~Handle(){if(value)CloseHandle(value);}Handle()=default;Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;};
struct TaskString{LPWSTR value{};~TaskString(){CoTaskMemFree(value);}};
std::string utf8(std::wstring_view text){if(text.empty()||text.size()>4096||text.find(L'\0')!=text.npos)throw std::invalid_argument("Invalid audio session identity");const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);if(length<=0||length>4096)throw std::invalid_argument("Invalid audio session identity encoding");std::string out(static_cast<std::size_t>(length),'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),length,nullptr,nullptr)!=length)throw std::runtime_error("Audio identity conversion failed");return out;}
std::optional<AudioApplicationExecutable>executable(std::wstring_view path)noexcept{
    try{if(path.empty()||path.size()>=32768||path.find(L'\0')!=path.npos)return {};const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr);if(count<=0||count>32768)return {};
        std::string token(static_cast<std::size_t>(count),'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),token.data(),count,nullptr,nullptr)!=count)return {};
        NativeFileShelfFiles files;auto lease=files.acquire(token);const auto&metadata=lease.metadata();if(metadata.kind!=ehud::data::ShelfFileKind::regular||metadata.isDirectory)return {};
        return AudioApplicationExecutable{metadata.windowsPath,metadata.identity};
    }catch(...){return {};}// Presentation failure must not disable audio control.
}
// Callback bodies never lock, wait, unregister, invoke owner UI or release a
// final WASAPI reference. The closed-bit/writer-count gate lets the MTA teardown
// wait for already-entered callbacks before draining their retained pointers.
struct EventRoute {
    AudioSessionBackend::Wake wake;
    std::atomic<unsigned>gate{};
    std::atomic<bool>overflow{};
    std::array<std::atomic<IAudioSessionControl*>,capacity>created{};
    explicit EventRoute(AudioSessionBackend::Wake w):wake(std::move(w)){}
    bool enter()noexcept{const auto previous=gate.fetch_add(2,std::memory_order_acq_rel);if(previous&1){leave();return false;}return true;}
    void leave()noexcept{gate.fetch_sub(2,std::memory_order_release);gate.notify_all();}
    void notify()noexcept{if(!enter())return;try{wake();}catch(...){}leave();}
    void add(IAudioSessionControl*session)noexcept{
        if(!session||!enter())return;bool retained{};
        for(auto&slot:created){IAudioSessionControl*empty{};if(slot.compare_exchange_strong(empty,reinterpret_cast<IAudioSessionControl*>(static_cast<std::uintptr_t>(1)),std::memory_order_acq_rel)){
                session->AddRef();slot.store(session,std::memory_order_release);retained=true;break;}}
        if(!retained)overflow.store(true,std::memory_order_release);try{wake();}catch(...){}leave();
    }
    void close()noexcept{auto value=gate.fetch_or(1,std::memory_order_acq_rel)|1;while(value!=1){gate.wait(value,std::memory_order_acquire);value=gate.load(std::memory_order_acquire);}}
    // Only called on the MTA worker, after close has excluded incoming writers.
    void discard()noexcept{for(auto&slot:created){auto*p=slot.exchange(nullptr,std::memory_order_acq_rel);if(p&&p!=reinterpret_cast<IAudioSessionControl*>(static_cast<std::uintptr_t>(1)))p->Release();}}
    ~EventRoute(){// NativeBackend drains before dropping its ownership.
        for(const auto&slot:created)if(slot.load())std::terminate();
    }
};
class NewSessions final:public IAudioSessionNotification {
    std::atomic<ULONG>references{1};std::shared_ptr<EventRoute>route;
public:
    explicit NewSessions(std::shared_ptr<EventRoute>r):route(std::move(r)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IAudioSessionNotification)){*out=static_cast<IAudioSessionNotification*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl*session)override{route->add(session);return S_OK;}
};
class SessionEvents final:public IAudioSessionEvents {
    std::atomic<ULONG>references{1};std::shared_ptr<EventRoute>route;
    HRESULT changed()noexcept{route->notify();return S_OK;}
public:
    explicit SessionEvents(std::shared_ptr<EventRoute>r):route(std::move(r)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IAudioSessionEvents)){*out=static_cast<IAudioSessionEvents*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR,LPCGUID)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR,LPCGUID)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float,BOOL,LPCGUID context)override{if(context&&IsEqualGUID(*context,ownWrites))return S_OK;return changed();}
    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD,float[],DWORD,LPCGUID)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID,LPCGUID)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason)override{return changed();}
};
// Process termination wake: a kernel wait on the retained process handle (no
// polling). The thread-pool callback only raises the worker's bit; teardown
// unregisters it with INVALID_HANDLE_VALUE before the route is released.
VOID CALLBACK processExited(PVOID context,BOOLEAN)noexcept{static_cast<EventRoute*>(context)->notify();}
class NativeBackend final:public AudioSessionBackend {
    struct Entry{ComPtr<IAudioSessionControl2>control;ComPtr<ISimpleAudioVolume>volume;ComPtr<SessionEvents>events;bool registered{};AudioSessionRecord record;std::unique_ptr<Handle>process;std::wstring processName;HANDLE exitWait{};bool exitReported{};};
    static void unwatch(Entry&e)noexcept{if(e.exitWait){UnregisterWaitEx(e.exitWait,INVALID_HANDLE_VALUE);e.exitWait=nullptr;}}
    // WAIT_FAILED (no SYNCHRONIZE access) is never treated as termination.
    static bool ended(const Entry&e)noexcept{return e.process&&e.process->value&&WaitForSingleObject(e.process->value,0)==WAIT_OBJECT_0;}
    HRESULT comStatus;bool initialized{};DWORD thread=GetCurrentThreadId();std::wstring endpoint;
    ComPtr<IMMDeviceEnumerator>enumerator;ComPtr<IAudioSessionManager2>manager;ComPtr<NewSessions>notification;bool managerRegistered{};
    std::shared_ptr<EventRoute>route;
    std::map<std::string,Entry,std::less<>>entries;
    HRESULT add(IAudioSessionControl*base){
        ComPtr<IAudioSessionControl2>control;auto hr=base->QueryInterface(IID_PPV_ARGS(&control));if(FAILED(hr))return hr;TaskString nativeID;hr=control->GetSessionInstanceIdentifier(&nativeID.value);if(FAILED(hr)||!nativeID.value)return FAILED(hr)?hr:E_UNEXPECTED;
        const auto id=utf8(nativeID.value);auto found=entries.find(id);if(found==entries.end()){
            if(entries.size()>=capacity)return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);Entry next;next.control=control;next.record.id=id;DWORD pid{};
            // S_NO_SINGLE_PROCESS is a success HRESULT but does NOT prove that
            // this session belongs to one application. Do not guess from PID.
            const auto processStatus=control->GetProcessId(&pid);
            if(processStatus==S_OK&&pid&&pid!=GetCurrentProcessId()&&control->IsSystemSoundsSession()==S_FALSE){
                auto process=std::make_unique<Handle>();
                // SYNCHRONIZE lets termination be observed; a process whose DACL
                // refuses it is still listed, only without the exit wake.
                process->value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!process->value)process->value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);FILETIME created{},exited{},kernel{},user{};
                if(process->value&&GetProcessTimes(process->value,&created,&exited,&kernel,&user)){
                    const auto stamp=(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;next.record.pid=pid;next.record.processKey=std::to_string(pid)+":"+std::to_string(stamp);next.process=std::move(process);
                    // Share one immutable discovery result across this exact
                    // process identity's sessions. No metadata I/O on read,
                    // volume events, hover, drag or native callback threads.
                    const auto prior=std::find_if(entries.begin(),entries.end(),[&](const auto&entry){return entry.second.record.processKey==next.record.processKey;});
                    if(prior!=entries.end()){next.record.executable=prior->second.record.executable;next.record.name=prior->second.processName;next.record.appUserModelID=prior->second.record.appUserModelID;}
                    else{
                        std::array<wchar_t,32768>path{};DWORD length=static_cast<DWORD>(path.size());
                        if(QueryFullProcessImageNameW(next.process->value,0,path.data(),&length)&&length&&length<path.size()){
                            const std::wstring_view token(path.data(),length);
                            next.record.name=fileDescription(token);
                            if(next.record.name.empty())next.record.name=std::filesystem::path(token).stem().wstring();
                            next.record.executable=executable(token);
                        }
                        next.record.appUserModelID=applicationIdentity(next.process->value);
                    }
                    if(next.record.name.empty()||next.record.name.size()>4096)next.record.name=L"PID "+std::to_wstring(pid);
                    next.processName=next.record.name;
                    TaskString label;if(SUCCEEDED(control->GetDisplayName(&label.value))&&label.value&&*label.value&&wcsnlen(label.value,4097)<=4096){const std::wstring_view display(label.value);
                        const auto resolved=display.front()==L'@'?indirect(display):std::wstring(display);
                        if(!resolved.empty()&&resolved.size()<=4096)next.record.name=resolved;}
                    TaskString persistent;if(SUCCEEDED(control->GetSessionIdentifier(&persistent.value))&&persistent.value)next.record.persistentID=utf8Optional(persistent.value);
                }
            }
            next.record.controllable=!next.record.processKey.empty()&&SUCCEEDED(control.As(&next.volume));found=entries.emplace(id,std::move(next)).first;
        }
        auto&e=found->second;if(route&&!e.registered){e.events.Attach(new SessionEvents(route));hr=e.control->RegisterAudioSessionNotification(e.events.Get());e.registered=SUCCEEDED(hr);if(FAILED(hr)){e.events.Reset();return hr;}}
        // A paused player's session raises no event when its process quits.
        if(route&&!e.exitWait&&e.process&&e.process->value&&!e.exitReported&&!RegisterWaitForSingleObject(&e.exitWait,e.process->value,processExited,route.get(),INFINITE,WT_EXECUTEONLYONCE|WT_EXECUTEINWAITTHREAD))e.exitWait=nullptr;
        return S_OK;
    }
    HRESULT collect(){if(!route)return S_OK;if(route->overflow.load(std::memory_order_acquire))return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);HRESULT first=S_OK;
        for(auto&slot:route->created){auto*p=slot.load(std::memory_order_acquire);if(!p||p==reinterpret_cast<IAudioSessionControl*>(static_cast<std::uintptr_t>(1)))continue;p=slot.exchange(nullptr,std::memory_order_acq_rel);ComPtr<IAudioSessionControl>lease;lease.Attach(p);const auto hr=add(lease.Get());if(FAILED(hr)&&SUCCEEDED(first))first=hr;}
        return first;
    }
    bool ownThread()const noexcept{return thread==GetCurrentThreadId();}
public:
    NativeBackend():comStatus(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),initialized(SUCCEEDED(comStatus)){}
    ~NativeBackend(){close();if(initialized)CoUninitialize();}
    std::int32_t open(std::wstring_view id,Wake wake)override{if(!ownThread())return RPC_E_WRONG_THREAD;close();endpoint=id;return resume(std::move(wake));}
    std::int32_t resume(Wake wake)override{
        try{
        if(!ownThread())return RPC_E_WRONG_THREAD;if(FAILED(comStatus))return comStatus;if(route)return S_OK;if(endpoint.empty())return E_INVALIDARG;
        auto hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&enumerator));ComPtr<IMMDevice>device;
        if(SUCCEEDED(hr))hr=enumerator->GetDevice(endpoint.c_str(),&device);
        if(SUCCEEDED(hr))hr=device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(manager.GetAddressOf()));
        if(FAILED(hr)){pause();return hr;}route=std::make_shared<EventRoute>(std::move(wake));notification.Attach(new NewSessions(route));hr=manager->RegisterSessionNotification(notification.Get());managerRegistered=SUCCEEDED(hr);
        ComPtr<IAudioSessionEnumerator>sessions;if(SUCCEEDED(hr))hr=manager->GetSessionEnumerator(&sessions);int count{};if(SUCCEEDED(hr))hr=sessions->GetCount(&count); // Required to enable creation callbacks.
        if(SUCCEEDED(hr)&&(count<0||count>static_cast<int>(capacity)))hr=HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        for(int n=0;SUCCEEDED(hr)&&n<count;++n){ComPtr<IAudioSessionControl>control;hr=sessions->GetSession(n,&control);if(SUCCEEDED(hr))hr=add(control.Get());}
        if(SUCCEEDED(hr))hr=collect();if(FAILED(hr))pause();return hr;
        }catch(const std::bad_alloc&){pause();return E_OUTOFMEMORY;}catch(...){pause();return E_FAIL;}
    }
    void pause()noexcept override{
        if(!ownThread())std::terminate();if(route)route->close();
        if(managerRegistered&&manager&&notification)manager->UnregisterSessionNotification(notification.Get());managerRegistered=false;
        for(auto&[_,e]:entries){if(e.registered&&e.control&&e.events)e.control->UnregisterAudioSessionNotification(e.events.Get());e.registered=false;e.events.Reset();unwatch(e);}
        if(route)route->discard();notification.Reset();route.reset();manager.Reset();enumerator.Reset();
    }
    std::int32_t read(std::vector<AudioSessionRecord>&out)override{
        if(!ownThread())return RPC_E_WRONG_THREAD;auto hr=collect();if(FAILED(hr))return hr;std::vector<AudioSessionRecord>next;next.reserve(entries.size());
        for(auto it=entries.begin();it!=entries.end();){auto&e=it->second;AudioSessionState state{};hr=e.control->GetState(&state);
            // Expired, or its process terminated and that was already reported
            // once (owned attenuation had its chance to be restored): release.
            if((SUCCEEDED(hr)&&state==AudioSessionStateExpired)||e.exitReported){if(e.registered&&e.events)e.control->UnregisterAudioSessionNotification(e.events.Get());unwatch(e);it=entries.erase(it);continue;}
            auto record=e.record;record.active=SUCCEEDED(hr)&&state==AudioSessionStateActive;record.volume.reset();record.error=hr;
            if(ended(e)){record.exited=true;record.active=false;e.exitReported=true;}
            DWORD pid{};const auto processStatus=e.control->GetProcessId(&pid);record.controllable=record.controllable&&processStatus==S_OK&&pid==record.pid;
            if(record.controllable){float value{};const auto volumeStatus=e.volume->GetMasterVolume(&value);if(SUCCEEDED(volumeStatus)&&std::isfinite(value)&&value>=0&&value<=1)record.volume=value;else{record.controllable=false;record.error=FAILED(volumeStatus)?volumeStatus:E_UNEXPECTED;}}
            next.push_back(std::move(record));++it;
        }
        out=std::move(next);return S_OK;
    }
    std::int32_t readVolume(std::string_view id,float&out)override{if(!ownThread())return RPC_E_WRONG_THREAD;const auto e=entries.find(id);if(e==entries.end()||!e->second.volume||!e->second.record.controllable)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);DWORD pid{};if(e->second.control->GetProcessId(&pid)!=S_OK||pid!=e->second.record.pid)return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);return e->second.volume->GetMasterVolume(&out);}
    std::int32_t writeVolume(std::string_view id,float value)override{if(!ownThread())return RPC_E_WRONG_THREAD;if(!std::isfinite(value)||value<0||value>1)return E_INVALIDARG;const auto e=entries.find(id);if(e==entries.end()||!e->second.volume||!e->second.record.controllable)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);DWORD pid{};if(e->second.control->GetProcessId(&pid)!=S_OK||pid!=e->second.record.pid)return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);return e->second.volume->SetMasterVolume(value,&ownWrites);}
    void close()noexcept override{pause();entries.clear();endpoint.clear();}
};
}
AudioSessionWorker::Factory nativeAudioSessionBackendFactory(){return []{return std::make_unique<NativeBackend>();};}
} // namespace endfield::native
#endif
