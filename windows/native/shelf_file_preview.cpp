#include "native/shelf_file_preview.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace endfield::native {
bool validShelfPreviewReference(std::string_view id,const ehud::data::ShelfFileMetadata&value)noexcept{
    return ehud::data::validUUID(id)&&ehud::data::validWindowsFilePath(value.windowsPath)&&
        !value.name.empty()&&value.name.size()<=4096&&ehud::data::Json::validUtf8(value.name)&&
        value.name.find('\0')==std::string::npos&&!value.isDirectory&&value.kind==ehud::data::ShelfFileKind::regular&&
        (!value.identity.volumeUUID||ehud::data::validUUID(*value.identity.volumeUUID));
}
}
#ifdef _WIN32
#include "native/file_shelf_files.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <objbase.h>
#include <objidl.h>
#include <ocidl.h>
#include <propsys.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <wrl/client.h>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;using namespace ehud::data;
struct Failure {HRESULT result;};
void checked(HRESULT hr){if(FAILED(hr))throw Failure{hr};}
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
template<class F>HRESULT protect(F&&body)noexcept{try{return body();}catch(const Failure&e){return e.result;}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(const std::invalid_argument&){return E_INVALIDARG;}catch(...){return E_FAIL;}}
std::wstring wide(std::string_view value){
    need(!value.empty()&&value.size()<=32768&&Json::validUtf8(value)&&value.find('\0')==std::string_view::npos,"Invalid preview text");
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(count<=0)throw Failure{HRESULT_FROM_WIN32(GetLastError())};std::wstring out(static_cast<std::size_t>(count),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),count)!=count)throw Failure{HRESULT_FROM_WIN32(GetLastError())};return out;
}
std::wstring extendedPath(std::string_view path){
    need(validWindowsFilePath(path),"Preview requires an explicit native path");auto value=wide(path);std::replace(value.begin(),value.end(),L'/',L'\\');
    value=value.starts_with(L"\\\\")?L"\\\\?\\UNC\\"+value.substr(2):L"\\\\?\\"+value;need(value.size()<32767,"Preview path exceeds the native path bound");return value;
}
void validateRoute(const ShelfPreviewRoute&value){
    if(!value.owner){need(!value.message&&!value.generation,"Empty preview route must have no message/generation");return;}
    DWORD process{};const auto thread=GetWindowThreadProcessId(value.owner,&process);
    need(IsWindow(value.owner)&&thread==GetCurrentThreadId()&&process==GetCurrentProcessId()&&value.message>=WM_APP&&value.message<=0xbfff&&value.generation,"Preview requires a live owner-thread HWND/private generation route");
}
struct FileBacking {
    HANDLE handle{INVALID_HANDLE_VALUE};std::uint64_t size{};std::wstring name;std::mutex mutex;
    ~FileBacking(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
// No filename initializer is used: this stream is tied to the checked object,
// even if an ancestor namespace is redirected after the open.
class ReadOnlyFileStream final:public IStream {
    std::atomic<ULONG>refs_{1};std::shared_ptr<FileBacking>file_;std::uint64_t position_{};
public:
    explicit ReadOnlyFileStream(std::shared_ptr<FileBacking>file,std::uint64_t position=0):file_(std::move(file)),position_(position){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_ISequentialStream&&iid!=IID_IStream)return E_NOINTERFACE;*out=static_cast<IStream*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto count=--refs_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE Read(void*buffer,ULONG count,ULONG*read)override{return protect([&]{
        if(read)*read=0;if(count&&!buffer)return E_POINTER;if(count>16u*1024u*1024u)return E_INVALIDARG;if(!count)return S_OK;
        std::lock_guard lock(file_->mutex);LARGE_INTEGER offset{};offset.QuadPart=static_cast<LONGLONG>(position_);
        if(!SetFilePointerEx(file_->handle,offset,nullptr,FILE_BEGIN))return HRESULT_FROM_WIN32(GetLastError());DWORD actual{};
        if(!ReadFile(file_->handle,buffer,count,&actual,nullptr))return HRESULT_FROM_WIN32(GetLastError());position_+=actual;if(read)*read=actual;return actual==count?S_OK:S_FALSE;
    });}
    HRESULT STDMETHODCALLTYPE Write(const void*,ULONG,ULONG*out)override{if(out)*out=0;return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER delta,DWORD origin,ULARGE_INTEGER*out)override{return protect([&]{
        std::lock_guard lock(file_->mutex);const auto maximum=static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max());std::uint64_t base{};
        switch(origin){case STREAM_SEEK_SET:break;case STREAM_SEEK_CUR:base=position_;break;case STREAM_SEEK_END:base=file_->size;break;default:return STG_E_INVALIDFUNCTION;}
        if(delta.QuadPart<0){const auto distance=static_cast<std::uint64_t>(-(delta.QuadPart+1))+1;if(distance>base)return STG_E_INVALIDFUNCTION;position_=base-distance;}
        else {const auto distance=static_cast<std::uint64_t>(delta.QuadPart);if(distance>maximum-base)return STG_E_INVALIDFUNCTION;position_=base+distance;}
        if(out)out->QuadPart=position_;return S_OK;
    });}
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER)override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*target,ULARGE_INTEGER count,ULARGE_INTEGER*read,ULARGE_INTEGER*written)override{return protect([&]{
        if(read)read->QuadPart=0;if(written)written->QuadPart=0;if(!target)return E_POINTER;if(target==this)return STG_E_INVALIDFUNCTION;
        if(count.QuadPart>static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()))return E_INVALIDARG;
        AddRef();struct Keep {ReadOnlyFileStream*self;~Keep(){self->Release();}}keep{this};std::array<std::uint8_t,65536>buffer{};std::uint64_t totalRead{},totalWritten{};
        while(totalRead<count.QuadPart){const auto chunk=static_cast<ULONG>(std::min<std::uint64_t>(buffer.size(),count.QuadPart-totalRead));ULONG actual{};const auto hr=Read(buffer.data(),chunk,&actual);totalRead+=actual;if(read)read->QuadPart=totalRead;if(FAILED(hr))return hr;
            if(actual){ULONG copied{};const auto output=target->Write(buffer.data(),actual,&copied);totalWritten+=copied;if(written)written->QuadPart=totalWritten;if(FAILED(output))return output;if(copied!=actual)return STG_E_MEDIUMFULL;}
            if(actual<chunk)return S_FALSE;
        }return S_OK;
    });}
    HRESULT STDMETHODCALLTYPE Commit(DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE Revert()override{return STG_E_REVERTED;}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE Stat(STATSTG*out,DWORD flags)override{return protect([&]{
        if(!out)return E_POINTER;*out={};if(flags!=STATFLAG_DEFAULT&&flags!=STATFLAG_NONAME)return E_INVALIDARG;
        out->type=STGTY_STREAM;out->cbSize.QuadPart=file_->size;out->grfMode=STGM_READ;
        if(flags!=STATFLAG_NONAME){const auto bytes=(file_->name.size()+1)*sizeof(wchar_t);out->pwcsName=static_cast<LPOLESTR>(CoTaskMemAlloc(bytes));if(!out->pwcsName)return E_OUTOFMEMORY;std::memcpy(out->pwcsName,file_->name.c_str(),bytes);}return S_OK;
    });}
    HRESULT STDMETHODCALLTYPE Clone(IStream**out)override{return protect([&]{if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(file_->mutex);*out=new ReadOnlyFileStream(file_,position_);return S_OK;});}
};
HRESULT createHandler(const ShelfFileMetadata&metadata,IPreviewHandler**out)noexcept{return protect([&]{
    if(!out)return E_POINTER;*out=nullptr;const auto name=wide(metadata.name);const auto separator=name.find_last_of(L'.');if(separator==std::wstring::npos||separator+1==name.size())return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    const auto extension=name.substr(separator);if(extension.size()>255)return E_INVALIDARG;
    // Preview-handler interface IID is also the documented ShellEx category.
    std::array<wchar_t,80>classID{};DWORD units=static_cast<DWORD>(classID.size());
    checked(AssocQueryStringW(ASSOCF_NOTRUNCATE,ASSOCSTR_SHELLEXTENSION,extension.c_str(),L"{8895b1c6-b41f-4c1c-a562-0d564250836f}",classID.data(),&units));
    need(units>1&&units<=classID.size(),"Invalid registered preview CLSID");CLSID clsid{};checked(CLSIDFromString(classID.data(),&clsid));
    // Never load a third-party handler DLL in the HUD process. Registered DLL
    // handlers use the system Preview Host surrogate through local activation.
    return CoCreateInstance(clsid,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(out));
});}
}
HRESULT openShelfPreviewStream(const ehud::data::ShelfFileAccess&access,IStream**out)noexcept{return protect([&]{
    if(!out)return E_POINTER;*out=nullptr;need(access.open(),"Preview lease is closed");const auto&metadata=access.metadata();
    need(!metadata.isDirectory&&metadata.kind==ShelfFileKind::regular&&validWindowsFilePath(metadata.windowsPath),"Preview stream requires an ordinary selected file");
    auto file=std::make_shared<FileBacking>();const auto path=extendedPath(metadata.windowsPath);
    file->handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_OPEN_NO_RECALL,nullptr);
    if(file->handle==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(GetLastError());if(GetFileType(file->handle)!=FILE_TYPE_DISK)return E_INVALIDARG;
    FILE_ATTRIBUTE_TAG_INFO attributes{};if(!GetFileInformationByHandleEx(file->handle,FileAttributeTagInfo,&attributes,sizeof(attributes)))return HRESULT_FROM_WIN32(GetLastError());
    if(attributes.FileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_OFFLINE))return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    FILE_ID_INFO identity{};if(!GetFileInformationByHandleEx(file->handle,FileIdInfo,&identity,sizeof(identity)))return HRESULT_FROM_WIN32(GetLastError());
    ShelfFileIdentity actual;actual.volumeSerial=identity.VolumeSerialNumber;std::memcpy(actual.objectID.data(),identity.FileId.Identifier,actual.objectID.size());
    if(!metadata.identity.matches(actual))return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
    FILE_STANDARD_INFO info{};if(!GetFileInformationByHandleEx(file->handle,FileStandardInfo,&info,sizeof(info)))return HRESULT_FROM_WIN32(GetLastError());
    if(info.DeletePending||info.Directory||info.EndOfFile.QuadPart<0)return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
    file->size=static_cast<std::uint64_t>(info.EndOfFile.QuadPart);file->name=wide(metadata.name);*out=new ReadOnlyFileStream(std::move(file));return S_OK;
});}

struct NativeShelfFilePreview::State:std::enable_shared_from_this<State> {
    struct Session;
    struct Request {std::string id;ShelfFileAccess access;std::uint64_t token{};};
    DWORD thread{GetCurrentThreadId()};ShelfPreviewRoute route;ShelfPreviewOptions options;ShelfPreviewServices services;
    bool alive{true},workPosted{},eventsPosted{};std::uint64_t token{};std::optional<Request>pending;
    std::shared_ptr<Session>active;std::vector<ShelfPreviewEvent>events;ShelfPreviewStats counts;
    void owner()const{need(thread==GetCurrentThreadId(),"Shelf preview requires its creating thread");}
    bool current(std::uint64_t value)const noexcept{return alive&&value==token&&route.owner&&IsWindow(route.owner);}
    bool post(ShelfPreviewNotice notice)noexcept{if(!alive||!route.owner||!IsWindow(route.owner))return false;const bool ok=PostMessageW(route.owner,route.message,route.generation,static_cast<LPARAM>(notice))!=FALSE;if(ok)++counts.notices;return ok;}
    void event(ShelfPreviewEventKind kind,HRESULT result,std::string_view id)noexcept{
        if(!alive)return;try{if(events.size()==8)events.erase(events.begin());events.push_back({kind,result,std::string(id)});if(!eventsPosted)eventsPosted=post(ShelfPreviewNotice::events);}catch(...){}
    }
    void close(bool restore,bool notify=true)noexcept;
    bool key(const MSG&message)noexcept;
    void work()noexcept;
};
struct NativeShelfFilePreview::State::Session:std::enable_shared_from_this<Session> {
    struct Frame final:IPreviewHandlerFrame,IOleWindow {
        std::atomic<ULONG>refs{1};std::weak_ptr<Session>session;
        explicit Frame(const std::shared_ptr<Session>&value):session(value){}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid==IID_IUnknown||iid==IID_IPreviewHandlerFrame)*out=static_cast<IPreviewHandlerFrame*>(this);else if(iid==IID_IOleWindow)*out=static_cast<IOleWindow*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
        ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
        ULONG STDMETHODCALLTYPE Release()override{const auto count=--refs;if(!count)delete this;return count;}
        HRESULT STDMETHODCALLTYPE GetWindowContext(PREVIEWHANDLERFRAMEINFO*out)override{if(!out)return E_POINTER;*out={};return S_OK;}
        HRESULT STDMETHODCALLTYPE TranslateAccelerator(MSG*message)override{if(!message)return E_POINTER;const auto value=session.lock();if(!value||value->closing)return S_FALSE;const auto state=value->state.lock();return state&&state->active==value&&state->key(*message)?S_OK:S_FALSE;}
        HRESULT STDMETHODCALLTYPE GetWindow(HWND*out)override{if(!out)return E_POINTER;*out=nullptr;const auto value=session.lock();if(!value||value->closing||!value->window)return E_FAIL;*out=value->window;return S_OK;}
        HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL)override{return E_NOTIMPL;}
    };
    std::weak_ptr<State>state;std::string id;std::uint64_t token{};ShelfFileAccess original,currentAccess;
    HWND window{};ComPtr<IPreviewHandler>handler;ComPtr<IObjectWithSite>withSite;ComPtr<IStream>stream;ComPtr<Frame>frame;
    unsigned depth{};bool closing{},cleaned{},restoreFocus{},hadForeground{},ready{};
    explicit Session(const std::shared_ptr<State>&owner):state(owner){}
    ~Session(){close(false);}
    template<class F>HRESULT call(F&&body){++depth;struct Scope{Session*self;~Scope(){--self->depth;if(self->closing&&!self->depth)self->cleanup();}}scope{this};return body();}
    void close(bool restore)noexcept{restoreFocus=restoreFocus||restore;hadForeground=hadForeground||(window&&GetForegroundWindow()==window);closing=true;if(window)ShowWindow(window,SW_HIDE);if(!depth)cleanup();}
    void cleanup()noexcept{
        if(cleaned||depth)return;cleaned=true;auto retainedHandler=std::move(handler);auto retainedSite=std::move(withSite);auto retainedStream=std::move(stream);auto retainedFrame=std::move(frame);
        const auto popup=std::exchange(window,nullptr);
        // Remove all owned fields before COM/native callbacks can reenter.
        if(retainedHandler)retainedHandler->Unload();if(retainedSite)retainedSite->SetSite(nullptr);
        retainedHandler.Reset();retainedSite.Reset();retainedStream.Reset();retainedFrame.Reset();if(popup&&IsWindow(popup))DestroyWindow(popup);
        currentAccess.close();original.close();const auto owner=state.lock();
        if(restoreFocus&&hadForeground&&owner&&owner->alive&&owner->options.visible&&owner->route.owner&&IsWindowVisible(owner->route.owner)){SetForegroundWindow(owner->route.owner);SetFocus(owner->route.owner);}
    }
    bool usable()const noexcept{const auto owner=state.lock();return !closing&&owner&&owner->current(token)&&owner->active.get()==this&&window&&IsWindow(window);}
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam)noexcept{
        auto*raw=reinterpret_cast<Session*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){const auto*create=reinterpret_cast<const CREATESTRUCTW*>(lParam);raw=static_cast<Session*>(create->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(raw));}
        const auto self=raw?raw->weak_from_this().lock():std::shared_ptr<Session>{};
        if(message==WM_NCDESTROY)SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
        if(!self)return DefWindowProcW(hwnd,message,wParam,lParam);
        try{
            if(message==WM_NCDESTROY&&self->window==hwnd){self->window=nullptr;const auto owner=self->state.lock();if(owner&&owner->active==self)owner->close(false);else self->close(false);}
            if(message==WM_CLOSE){const auto owner=self->state.lock();if(owner&&owner->active==self)owner->close(true);else self->close(false);return 0;}
            if(message==WM_SIZE&&self->ready&&self->handler&&!self->closing){RECT area{};if(GetClientRect(hwnd,&area)&&area.right>0&&area.bottom>0){auto handler=self->handler;const auto hr=self->call([&]{return handler->SetRect(&area);});if(FAILED(hr)){const auto owner=self->state.lock();if(owner&&owner->active==self){++owner->counts.failures;owner->event(ShelfPreviewEventKind::failed,hr,self->id);owner->close(false);}}}return 0;}
            if(message==WM_SETFOCUS&&self->ready&&self->handler&&!self->closing){auto handler=self->handler;self->call([&]{return handler->SetFocus();});return 0;}
            if(message==WM_KEYDOWN||message==WM_SYSKEYDOWN){const auto owner=self->state.lock();MSG key{hwnd,message,wParam,lParam,0,{}};if(owner&&owner->active==self&&owner->key(key))return 0;}
        }catch(...){const auto owner=self->state.lock();if(owner&&owner->active==self)owner->close(false);}
        return DefWindowProcW(hwnd,message,wParam,lParam);
    }
    void createWindow(){
        static std::once_flag registered;std::call_once(registered,[]{WNDCLASSEXW type{sizeof(type)};type.lpfnWndProc=procedure;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);type.lpszClassName=L"EndfieldShelfPreviewHost.v1";
            if(!RegisterClassExW(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw Failure{HRESULT_FROM_WIN32(GetLastError())};});
        const auto owner=state.lock();need(owner&&owner->current(token),"Preview request detached");RECT rect{0,0,owner->options.clientWidth,owner->options.clientHeight};
        constexpr DWORD style=WS_OVERLAPPEDWINDOW;constexpr DWORD exStyle=WS_EX_TOOLWINDOW;checked(AdjustWindowRectEx(&rect,style,FALSE,exStyle)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
        MONITORINFO monitor{sizeof(monitor)};if(!GetMonitorInfoW(MonitorFromWindow(owner->route.owner,MONITOR_DEFAULTTONEAREST),&monitor))throw Failure{HRESULT_FROM_WIN32(GetLastError())};
        const auto width=std::min(rect.right-rect.left,monitor.rcWork.right-monitor.rcWork.left),height=std::min(rect.bottom-rect.top,monitor.rcWork.bottom-monitor.rcWork.top);
        RECT parent{};if(!GetWindowRect(owner->route.owner,&parent))parent=monitor.rcWork;
        const auto x=std::clamp((parent.left+parent.right-width)/2,monitor.rcWork.left,monitor.rcWork.right-width),y=std::clamp((parent.top+parent.bottom-height)/2,monitor.rcWork.top,monitor.rcWork.bottom-height);
        const auto title=wide(original.metadata().name);
        // CreateWindow can synchronously call application/native hooks. Delay
        // close cleanup until its returned HWND has been assigned to this owner.
        checked(call([&]{window=CreateWindowExW(exStyle,L"EndfieldShelfPreviewHost.v1",title.c_str(),style,x,y,width,height,owner->route.owner,nullptr,GetModuleHandleW(nullptr),this);return window?S_OK:HRESULT_FROM_WIN32(GetLastError());}));
    }
};
void NativeShelfFilePreview::State::close(bool restore,bool notify)noexcept{
    ++token;auto oldPending=std::move(pending);pending.reset();workPosted=false;auto previous=std::move(active);
    if(previous){++counts.closes;if(notify)event(ShelfPreviewEventKind::closed,S_OK,previous->id);previous->close(restore);}
}
bool NativeShelfFilePreview::State::key(const MSG&message)noexcept{
    if(!alive||!active||active->closing||(message.message!=WM_KEYDOWN&&message.message!=WM_SYSKEYDOWN))return false;
    const bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0,control=(GetKeyState(VK_CONTROL)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0;
    if(!shift&&!control&&!alt&&(message.wParam==VK_ESCAPE||message.wParam==VK_SPACE)){close(true);return true;}
    if(!shift&&control&&!alt&&message.wParam=='R'){event(ShelfPreviewEventKind::revealRequested,S_OK,active->id);return true;}return false;
}
void NativeShelfFilePreview::State::work()noexcept{
    workPosted=false;if(!alive||!pending)return;auto request=std::move(*pending);pending.reset();
    if(!current(request.token))return;
    // Source Quick Look toggles an already-requested item off, otherwise retires
    // the old preview without restoring HUD focus before opening the new item.
    if(active&&active->id==request.id){close(true);return;}
    auto previous=std::move(active);if(previous){++counts.closes;event(ShelfPreviewEventKind::closed,S_OK,previous->id);previous->close(false);if(!current(request.token))return;}
    std::shared_ptr<Session>session;
    const auto result=protect([&]{
        session=std::make_shared<Session>(shared_from_this());session->id=std::move(request.id);session->token=request.token;session->original=std::move(request.access);active=session;
        ShelfRecord record;const auto&metadata=session->original.metadata();record.id=session->id;record.windowsPath=metadata.windowsPath;record.name=metadata.name;record.identity=metadata.identity;record.isDirectory=metadata.isDirectory;
        auto resolved=services.resolve(record);if(!current(session->token)||active!=session)return E_ABORT;
        need(resolved.open()&&metadata.identity.matches(resolved.metadata().identity)&&validShelfPreviewReference(session->id,resolved.metadata()),"Original preview reference is unavailable");session->currentAccess=std::move(resolved);session->createWindow();if(!session->usable())return E_ABORT;
        ComPtr<IPreviewHandler>handler;checked(services.createHandler(session->currentAccess.metadata(),&handler));if(!session->usable())return E_ABORT;need(bool(handler),"Preview provider returned no handler");session->handler=std::move(handler);
        ComPtr<IObjectWithSite>site;checked(session->call([&]{return session->handler.As(&site);}));if(!session->usable())return E_ABORT;session->withSite=std::move(site);session->frame.Attach(new Session::Frame(session));
        {auto target=session->withSite;auto frame=session->frame;checked(session->call([&]{return target->SetSite(static_cast<IPreviewHandlerFrame*>(frame.Get()));}));}if(!session->usable())return E_ABORT;
        ComPtr<IInitializeWithStream>initialize;{auto target=session->handler;checked(session->call([&]{return target.As(&initialize);}));}if(!session->usable())return E_ABORT;
        ComPtr<IStream>stream;checked(services.openStream(session->currentAccess,&stream));if(!session->usable())return E_ABORT;need(bool(stream),"Preview provider returned no stream");session->stream=std::move(stream);
        {auto source=session->stream;checked(session->call([&]{return initialize->Initialize(source.Get(),STGM_READ);}));}if(!session->usable())return E_ABORT;
        RECT area{};if(!GetClientRect(session->window,&area))return HRESULT_FROM_WIN32(GetLastError());
        {auto target=session->handler;const auto hwnd=session->window;checked(session->call([&]{return target->SetWindow(hwnd,&area);}));}if(!session->usable())return E_ABORT;
        {auto target=session->handler;checked(session->call([&]{return target->DoPreview();}));}if(!session->usable())return E_ABORT;session->ready=true;
        if(options.visible){if(!SetWindowPos(session->window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW))return HRESULT_FROM_WIN32(GetLastError());if(!session->usable())return E_ABORT;SetForegroundWindow(session->window);if(!session->usable())return E_ABORT;auto target=session->handler;session->call([&]{return target->SetFocus();});if(!session->usable())return E_ABORT;}
        ++counts.opens;event(ShelfPreviewEventKind::opened,S_OK,session->id);return S_OK;
    });
    if(FAILED(result)&&session&&active==session){active.reset();++counts.failures;event(ShelfPreviewEventKind::failed,result,session->id);session->close(false);}
    else if(FAILED(result)&&!session&&current(request.token)){++counts.failures;event(ShelfPreviewEventKind::failed,result,request.id);}
    // Detached/overwritten requests are cleaned by their retained local session
    // without posting a stale result to a replacement owner route.
    if(session&&active!=session)session->close(false);
}
NativeShelfFilePreview::NativeShelfFilePreview(ShelfPreviewRoute route,ShelfPreviewOptions options,ShelfPreviewServices services){
    validateRoute(route);need(options.clientWidth>=32&&options.clientWidth<=4096&&options.clientHeight>=32&&options.clientHeight<=4096,"Invalid native preview dimensions");
    if(!services.resolve)services.resolve=NativeFileShelfFiles().platform().resolve;if(!services.createHandler)services.createHandler=createHandler;if(!services.openStream)services.openStream=openShelfPreviewStream;
    auto state=std::make_shared<State>();state->route=route;state->options=options;state->services=std::move(services);state->events.reserve(8);state_=std::move(state);
}
NativeShelfFilePreview::~NativeShelfFilePreview(){const auto state=std::move(state_);if(!state)return;if(state->thread!=GetCurrentThreadId())std::terminate();state->alive=false;state->events.clear();state->close(false,false);state->route={};}
bool NativeShelfFilePreview::request(std::string id,ehud::data::ShelfFileAccess access){
    const auto state=state_;state->owner();need(access.open()&&validShelfPreviewReference(id,access.metadata()),"Preview requires a supported validated reference");if(!state->alive||!state->route.owner)return false;
    State::Request request{std::move(id),std::move(access),++state->token};auto oldPending=std::move(state->pending);state->pending.reset();state->pending=std::move(request);++state->counts.requests;
    if(!state->workPosted)state->workPosted=state->post(ShelfPreviewNotice::work);if(!state->workPosted){state->pending.reset();return false;}return true;
}
bool NativeShelfFilePreview::handleMessage(UINT_PTR generation,LPARAM notice){const auto state=state_;state->owner();if(!state->alive||generation!=state->route.generation)return false;
    if(notice==static_cast<LPARAM>(ShelfPreviewNotice::work)){state->work();return true;}if(notice==static_cast<LPARAM>(ShelfPreviewNotice::events))return true;return false;
}
std::vector<ShelfPreviewEvent>NativeShelfFilePreview::drain(UINT_PTR generation){const auto state=state_;state->owner();if(!state->alive||generation!=state->route.generation)return {};std::vector<ShelfPreviewEvent>out;out.swap(state->events);state->eventsPosted=false;return out;}
void NativeShelfFilePreview::close(bool restore){const auto state=state_;state->owner();state->close(restore);}
void NativeShelfFilePreview::setRoute(ShelfPreviewRoute route){const auto state=state_;state->owner();validateRoute(route);state->route={};state->close(false);state->events.clear();state->eventsPosted=false;if(state->alive)state->route=route;}
bool NativeShelfFilePreview::preTranslate(const MSG&message){const auto state=state_;state->owner();const auto session=state->active;if(!session||!session->window||(message.hwnd!=session->window&&!IsChild(session->window,message.hwnd)))return false;return state->key(message);}
ShelfPreviewStats NativeShelfFilePreview::stats()const{const auto state=state_;state->owner();auto result=state->counts;result.queued=state->pending.has_value();result.active=state->active&&!state->active->closing&&state->active->ready;result.visible=result.active&&IsWindowVisible(state->active->window);return result;}
HWND NativeShelfFilePreview::window()const{const auto state=state_;state->owner();return state->active?state->active->window:nullptr;}
} // namespace endfield::native
#endif
