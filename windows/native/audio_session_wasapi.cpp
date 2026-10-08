#include "audio_session_worker.hpp"
#include "native/file_shelf_files.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
constexpr auto capacity=AudioSessionRoutes::maximumSessions;
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
    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float,BOOL,LPCGUID)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD,float[],DWORD,LPCGUID)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID,LPCGUID)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason)override{return changed();}
};
class NativeBackend final:public AudioSessionBackend {
    struct Entry{ComPtr<IAudioSessionControl2>control;ComPtr<ISimpleAudioVolume>volume;ComPtr<SessionEvents>events;bool registered{};AudioSessionRecord record;std::unique_ptr<Handle>process;};
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
                auto process=std::make_unique<Handle>();process->value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);FILETIME created{},exited{},kernel{},user{};
                if(process->value&&GetProcessTimes(process->value,&created,&exited,&kernel,&user)){
                    const auto stamp=(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;next.record.pid=pid;next.record.processKey=std::to_string(pid)+":"+std::to_string(stamp);next.process=std::move(process);
                    std::array<wchar_t,32768>path{};DWORD length=static_cast<DWORD>(path.size());
                    if(QueryFullProcessImageNameW(next.process->value,0,path.data(),&length)&&length&&length<path.size()){
                        const std::wstring_view token(path.data(),length);next.record.name=std::filesystem::path(token).stem().wstring();
                        // Share one immutable discovery result across this exact
                        // process identity's sessions. No metadata I/O on read,
                        // volume events, hover, drag or native callback threads.
                        const auto prior=std::find_if(entries.begin(),entries.end(),[&](const auto&entry){return entry.second.record.processKey==next.record.processKey;});
                        if(prior!=entries.end())next.record.executable=prior->second.record.executable;
                        else next.record.executable=executable(token);
                    }
                    if(next.record.name.empty()||next.record.name.size()>4096)next.record.name=L"PID "+std::to_wstring(pid);
                    TaskString label;if(SUCCEEDED(control->GetDisplayName(&label.value))&&label.value){const std::wstring_view display(label.value);if(!display.empty()&&display.size()<=4096&&display.front()!=L'@')next.record.name=display;}
                }
            }
            next.record.controllable=!next.record.processKey.empty()&&SUCCEEDED(control.As(&next.volume));found=entries.emplace(id,std::move(next)).first;
        }
        auto&e=found->second;if(route&&!e.registered){e.events.Attach(new SessionEvents(route));hr=e.control->RegisterAudioSessionNotification(e.events.Get());e.registered=SUCCEEDED(hr);if(FAILED(hr)){e.events.Reset();return hr;}}
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
        for(auto&[_,e]:entries){if(e.registered&&e.control&&e.events)e.control->UnregisterAudioSessionNotification(e.events.Get());e.registered=false;e.events.Reset();}
        if(route)route->discard();notification.Reset();route.reset();manager.Reset();enumerator.Reset();
    }
    std::int32_t read(std::vector<AudioSessionRecord>&out)override{
        if(!ownThread())return RPC_E_WRONG_THREAD;auto hr=collect();if(FAILED(hr))return hr;std::vector<AudioSessionRecord>next;next.reserve(entries.size());
        for(auto it=entries.begin();it!=entries.end();){auto&e=it->second;AudioSessionState state{};hr=e.control->GetState(&state);if(SUCCEEDED(hr)&&state==AudioSessionStateExpired){if(e.registered&&e.events)e.control->UnregisterAudioSessionNotification(e.events.Get());it=entries.erase(it);continue;}
            auto record=e.record;record.active=SUCCEEDED(hr)&&state==AudioSessionStateActive;record.volume.reset();record.error=hr;
            DWORD pid{};const auto processStatus=e.control->GetProcessId(&pid);record.controllable=record.controllable&&processStatus==S_OK&&pid==record.pid;
            if(record.controllable){float value{};const auto volumeStatus=e.volume->GetMasterVolume(&value);if(SUCCEEDED(volumeStatus)&&std::isfinite(value)&&value>=0&&value<=1)record.volume=value;else{record.controllable=false;record.error=FAILED(volumeStatus)?volumeStatus:E_UNEXPECTED;}}
            next.push_back(std::move(record));++it;
        }
        out=std::move(next);return S_OK;
    }
    std::int32_t readVolume(std::string_view id,float&out)override{if(!ownThread())return RPC_E_WRONG_THREAD;const auto e=entries.find(id);if(e==entries.end()||!e->second.volume||!e->second.record.controllable)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);DWORD pid{};if(e->second.control->GetProcessId(&pid)!=S_OK||pid!=e->second.record.pid)return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);return e->second.volume->GetMasterVolume(&out);}
    std::int32_t writeVolume(std::string_view id,float value)override{if(!ownThread())return RPC_E_WRONG_THREAD;if(!std::isfinite(value)||value<0||value>1)return E_INVALIDARG;const auto e=entries.find(id);if(e==entries.end()||!e->second.volume||!e->second.record.controllable)return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);DWORD pid{};if(e->second.control->GetProcessId(&pid)!=S_OK||pid!=e->second.record.pid)return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);return e->second.volume->SetMasterVolume(value,nullptr);}
    void close()noexcept override{pause();entries.clear();endpoint.clear();}
};
}
AudioSessionWorker::Factory nativeAudioSessionBackendFactory(){return []{return std::make_unique<NativeBackend>();};}
} // namespace endfield::native
#endif
