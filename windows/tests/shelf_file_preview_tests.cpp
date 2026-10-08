#include "native/shelf_file_preview.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace n=endfield::native;namespace d=ehud::data;
namespace {
unsigned checks{};void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&&body,const char*message){bool rejected{};try{body();}catch(const std::exception&){rejected=true;}check(rejected,message);}
constexpr auto firstID="aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
d::ShelfFileMetadata metadata(unsigned identity=1){d::ShelfFileMetadata value;value.windowsPath="C:\\explicit-synthetic\\owned.txt";value.name="owned.txt";value.typeDescription="File";value.identity.volumeSerial=1;value.identity.objectID[0]=static_cast<std::uint8_t>(identity);return value;}
void pure(){
    auto value=metadata();check(n::validShelfPreviewReference(firstID,value),"Ordinary selected file remains an explicit reference");
    check(!n::validShelfPreviewReference("not-an-id",value),"Noncanonical item ID rejects");value.windowsPath="relative.txt";check(!n::validShelfPreviewReference(firstID,value),"Implicit current-directory reference rejects");
    value=metadata();value.windowsPath="C:\\owned.txt:alternate";check(!n::validShelfPreviewReference(firstID,value),"Alternate data stream rejects");
    value=metadata();value.kind=d::ShelfFileKind::symbolicLink;check(!n::validShelfPreviewReference(firstID,value),"Selected symbolic link never silently dereferences for preview");
    value=metadata();value.kind=d::ShelfFileKind::directory;value.isDirectory=true;check(!n::validShelfPreviewReference(firstID,value),"Folder is not fabricated as file stream");
    value=metadata();value.name.clear();check(!n::validShelfPreviewReference(firstID,value),"Missing native title rejects");value.name=std::string(4097,'a');check(!n::validShelfPreviewReference(firstID,value),"Title memory bound rejects rather than truncates");
    value=metadata();value.name="bad\xff";check(!n::validShelfPreviewReference(firstID,value),"Malformed title UTF8 rejects");value=metadata();value.identity.volumeUUID="bad";check(!n::validShelfPreviewReference(firstID,value),"Malformed optional volume pin rejects");
}
}
#ifdef _WIN32
#include "native/file_shelf_files.hpp"
#include <atomic>
#include <objbase.h>
#include <objidl.h>
#include <ocidl.h>
#include <propsys.h>
#include <shobjidl.h>
#include <wrl/client.h>
namespace {
using Microsoft::WRL::ComPtr;
constexpr auto secondID="bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
struct COM {COM(){check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Owned fixture thread is STA");}~COM(){CoUninitialize();}};
struct Window {
    HWND value{};static constexpr UINT notice=WM_APP+252;
    Window(){value=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Owned hidden Shelf preview fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(value&&!IsWindowVisible(value),"Synthetic owner remains hidden");}
    ~Window(){if(value)DestroyWindow(value);}
    n::ShelfPreviewRoute route(UINT_PTR generation)const{return {value,notice,generation};}
    MSG next(){MSG message{};check(PeekMessageW(&message,value,notice,notice,PM_REMOVE)!=FALSE,"Queued preview work/events target only owned HWND");return message;}
    void discard(){MSG message{};while(PeekMessageW(&message,value,notice,notice,PM_REMOVE))check(message.wParam!=0,"No raw callback pointer appears in route");}
};
struct LeaseCounts {unsigned acquired{},released{};};
d::ShelfFileAccess lease(LeaseCounts&counts,unsigned identity=1){++counts.acquired;return {metadata(identity),[&counts]{++counts.released;}};}
struct HandlerLog {
    unsigned initialize{},sites{},setWindows{},previews{},rects{},focus{},unloads{};bool inside{},unloadInside{},readOnly{};
    HWND parent{};RECT rect{};HRESULT previewResult{S_OK};std::function<void(std::string_view)>callback;
};
class Handler final:public IPreviewHandler,public IInitializeWithStream,public IObjectWithSite {
    std::atomic<ULONG>refs_{1};std::shared_ptr<HandlerLog>log_;ComPtr<IUnknown>site_;ComPtr<IStream>stream_;
    void external(std::string_view operation){log_->inside=true;if(log_->callback)log_->callback(operation);log_->inside=false;}
public:
    explicit Handler(std::shared_ptr<HandlerLog>log):log_(std::move(log)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid==IID_IUnknown||iid==IID_IPreviewHandler)*out=static_cast<IPreviewHandler*>(this);else if(iid==__uuidof(IInitializeWithStream))*out=static_cast<IInitializeWithStream*>(this);else if(iid==IID_IObjectWithSite)*out=static_cast<IObjectWithSite*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{const auto count=--refs_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown*site)override{++log_->sites;site_=site;external(site?"site":"clear-site");return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid,void**out)override{return site_?site_->QueryInterface(iid,out):E_FAIL;}
    HRESULT STDMETHODCALLTYPE Initialize(IStream*source,DWORD mode)override{++log_->initialize;log_->readOnly=mode==STGM_READ;stream_=source;external("initialize");return S_OK;}
    HRESULT STDMETHODCALLTYPE SetWindow(HWND hwnd,const RECT*rect)override{++log_->setWindows;log_->parent=hwnd;if(!rect)return E_POINTER;log_->rect=*rect;external("window");return S_OK;}
    HRESULT STDMETHODCALLTYPE SetRect(const RECT*rect)override{++log_->rects;if(!rect)return E_POINTER;log_->rect=*rect;external("rect");return S_OK;}
    HRESULT STDMETHODCALLTYPE DoPreview()override{++log_->previews;external("preview");return log_->previewResult;}
    HRESULT STDMETHODCALLTYPE Unload()override{++log_->unloads;log_->unloadInside=log_->unloadInside||log_->inside;stream_.Reset();external("unload");return S_OK;}
    HRESULT STDMETHODCALLTYPE SetFocus()override{++log_->focus;external("focus");return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryFocus(HWND*out)override{if(!out)return E_POINTER;*out=nullptr;return S_FALSE;}
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(MSG*)override{return S_FALSE;}
    HRESULT frameWindow(HWND*out){ComPtr<IOleWindow>window;const auto hr=site_.As(&window);return FAILED(hr)?hr:window->GetWindow(out);}
    HRESULT frameKey(MSG&message){ComPtr<IPreviewHandlerFrame>frame;const auto hr=site_.As(&frame);return FAILED(hr)?hr:frame->TranslateAccelerator(&message);}
    HRESULT frameContext(PREVIEWHANDLERFRAMEINFO*out){ComPtr<IPreviewHandlerFrame>frame;const auto hr=site_.As(&frame);return FAILED(hr)?hr:frame->GetWindowContext(out);}
};
struct Fixture {
    Window owner;LeaseCounts leases;std::shared_ptr<HandlerLog>log=std::make_shared<HandlerLog>();ComPtr<Handler>handler;
    unsigned resolutions{},factories{},streams{};unsigned resolvedIdentity{1};bool storeBusy{};HRESULT factoryResult{S_OK},streamResult{S_OK};std::function<void()>onResolve,onFactory,onStream;
    Fixture(){handler.Attach(new Handler(log));}
    n::ShelfPreviewServices services(){return {
        [this](const d::ShelfRecord&record){check(!storeBusy,"Reference resolution occurs after synchronous store/state callback");check(record.identity.objectID[0]==1,"Resolver receives original identity");++resolutions;if(onResolve)onResolve();return lease(leases,resolvedIdentity);},
        [this](const d::ShelfFileMetadata&value,IPreviewHandler**out){check(!storeBusy&&value.identity.objectID[0]==1,"Handler activation only follows validated original object");++factories;if(onFactory)onFactory();*out=nullptr;if(FAILED(factoryResult))return factoryResult;handler->AddRef();*out=handler.Get();return S_OK;},
        [this](const d::ShelfFileAccess&access,IStream**out){check(access.open()&&leases.acquired-leases.released==2,"Both reference leases span stream initialization");++streams;if(onStream)onStream();*out=nullptr;if(FAILED(streamResult))return streamResult;return CreateStreamOnHGlobal(nullptr,TRUE,out);}
    };}
    std::unique_ptr<n::NativeShelfFilePreview>make(UINT_PTR generation=51){return std::make_unique<n::NativeShelfFilePreview>(owner.route(generation),n::ShelfPreviewOptions{false,600,400},services());}
    void dispatch(n::NativeShelfFilePreview&preview){const auto message=owner.next();check(preview.handleMessage(message.wParam,message.lParam),"Owner handles preview's queued private work");}
    std::vector<n::ShelfPreviewEvent>events(n::NativeShelfFilePreview&preview,UINT_PTR generation=51){dispatch(preview);return preview.drain(generation);}
    void open(n::NativeShelfFilePreview&preview,std::string id=firstID){check(preview.request(std::move(id),lease(leases)),"Validated preview request queues");dispatch(preview);auto result=events(preview);check(result.size()==1&&result[0].kind==n::ShelfPreviewEventKind::opened&&SUCCEEDED(result[0].result),"Successful handler emits exactly one opened event");}
};
void queuedIdentityAndLifetime(){
    Fixture fixture;auto preview=fixture.make();fixture.storeBusy=true;check(preview->request(firstID,lease(fixture.leases)),"Request only retains and queues under busy source callback");check(fixture.resolutions==0&&fixture.factories==0&&fixture.streams==0&&fixture.leases.released==0,"No Shell/filesystem/content operation inside callback");fixture.storeBusy=false;
    const auto message=fixture.owner.next();check(!preview->handleMessage(50,message.lParam)&&fixture.resolutions==0,"Stale route cannot activate native preview");check(preview->handleMessage(message.wParam,message.lParam),"Fresh route starts handler");auto opened=fixture.events(*preview);
    check(opened.size()==1&&opened[0].itemID==firstID&&opened[0].kind==n::ShelfPreviewEventKind::opened,"Original stable item ID accompanies opened event");
    const auto popup=preview->window();check(popup&&IsWindow(popup)&&!IsWindowVisible(popup)&&GetWindow(popup,GW_OWNER)==fixture.owner.value,"One hidden owned native window hosts injected handler");
    check(preview->stats().active&&!preview->stats().visible&&!preview->stats().queued,"Hidden fixture active state distinguishes visibility");
    check(fixture.log->readOnly&&fixture.log->initialize==1&&fixture.log->sites==1&&fixture.log->setWindows==1&&fixture.log->previews==1,"Stream/site/window initialized once before preview");
    check(fixture.log->parent==popup&&fixture.log->rect.right>0&&fixture.log->rect.bottom>0,"Handler receives own client rectangle");
    HWND reported{};check(SUCCEEDED(fixture.handler->frameWindow(&reported))&&reported==popup,"Host IOleWindow exposes only the owned popup");PREVIEWHANDLERFRAMEINFO info{};check(SUCCEEDED(fixture.handler->frameContext(&info))&&!info.haccel&&info.cAccelEntries==0,"No unrelated global accelerator table");
    check(fixture.leases.acquired==2&&fixture.leases.released==0,"Independent original/current leases remain open throughout preview");
    const auto factories=fixture.factories;for(unsigned i=0;i<120;++i){check(preview->stats().active,"Stationary preview retains active handler without polling");MSG empty{};check(!PeekMessageW(&empty,fixture.owner.value,Window::notice,Window::notice,PM_REMOVE),"Idle preview produces no timer/request notices");}check(fixture.factories==factories&&fixture.log->previews==1,"No per-frame handler or preview recreation");
    SendMessageW(popup,WM_SIZE,SIZE_RESTORED,MAKELPARAM(300,200));check(fixture.log->rects==1,"Native resize updates existing handler rectangle once");
    preview->close(false);auto closed=fixture.events(*preview);check(closed.size()==1&&closed[0].kind==n::ShelfPreviewEventKind::closed,"Close is owner notification, not synchronous store callback");
    check(!preview->window()&&!IsWindow(popup)&&fixture.log->unloads==1&&fixture.log->sites==2&&fixture.leases.acquired==fixture.leases.released,"Handler unload/site clear precede window/reference retirement");check(!fixture.log->unloadInside,"Unload never reenters an unfinished handler call");
}
void toggleReplacementAndKeys(){
    Fixture fixture;auto preview=fixture.make();fixture.open(*preview);preview->request(firstID,lease(fixture.leases));fixture.dispatch(*preview);auto toggle=fixture.events(*preview);
    check(toggle.size()==1&&toggle[0].kind==n::ShelfPreviewEventKind::closed&&fixture.log->previews==1&&!preview->stats().active,"Repeated same-file request toggles closed without rereading/activating");
    fixture.open(*preview);preview->request(secondID,lease(fixture.leases));fixture.dispatch(*preview);auto next=fixture.events(*preview);check(next.size()==2&&next[0].kind==n::ShelfPreviewEventKind::closed&&next[1].kind==n::ShelfPreviewEventKind::opened&&next[1].itemID==secondID,"Switching file retires previous preview and opens one current reference");
    std::array<BYTE,256>keys{};check(SetKeyboardState(keys.data())!=FALSE,"Synthetic process-thread keyboard state set");MSG escape{preview->window(),WM_KEYDOWN,VK_ESCAPE,0,0,{}};
    check(!preview->preTranslate(MSG{fixture.owner.value,WM_KEYDOWN,VK_ESCAPE,0,0,{}}),"Owner HUD keystrokes are outside preview host");check(preview->preTranslate(escape),"Owned Escape closes native preview before message translation");fixture.events(*preview);
    fixture.open(*preview);keys[VK_CONTROL]=0x80;SetKeyboardState(keys.data());MSG reveal{preview->window(),WM_KEYDOWN,'R',0,0,{}};check(fixture.handler->frameKey(reveal)==S_OK,"Preview handler may forward Ctrl-R through its host site");auto event=fixture.events(*preview);check(event.size()==1&&event[0].kind==n::ShelfPreviewEventKind::revealRequested&&event[0].itemID==firstID&&preview->stats().active,"Reveal queues owner action only, never opens Explorer/default app from handler");
    keys.fill(0);SetKeyboardState(keys.data());MSG space{preview->window(),WM_KEYDOWN,VK_SPACE,0,0,{}};check(fixture.handler->frameKey(space)==S_OK,"Unmodified Space toggles source preview closed");fixture.events(*preview);check(fixture.leases.acquired==fixture.leases.released,"All switched/toggled file leases balance");
}
void failedAndCanceled(){
    Fixture fixture;auto preview=fixture.make();preview->request(firstID,lease(fixture.leases));const auto request=fixture.owner.next();preview->close(false);preview->handleMessage(request.wParam,request.lParam);check(!preview->stats().active&&fixture.factories==0&&fixture.leases.acquired==fixture.leases.released,"Canceled pending request cannot open a window/handler");
    fixture.resolvedIdentity=2;preview->request(firstID,lease(fixture.leases));fixture.dispatch(*preview);auto result=fixture.events(*preview);check(result.size()==1&&result[0].kind==n::ShelfPreviewEventKind::failed&&FAILED(result[0].result)&&fixture.factories==0&&!preview->window(),"Different object at old path fails before handler creation");fixture.resolvedIdentity=1;
    fixture.factoryResult=REGDB_E_CLASSNOTREG;preview->request(firstID,lease(fixture.leases));fixture.dispatch(*preview);result=fixture.events(*preview);check(result.size()==1&&result[0].result==REGDB_E_CLASSNOTREG&&!preview->window()&&fixture.streams==0,"Unavailable handler leaves no empty popup and performs no fallback app execution");fixture.factoryResult=S_OK;
    fixture.streamResult=STG_E_ACCESSDENIED;preview->request(firstID,lease(fixture.leases));fixture.dispatch(*preview);result=fixture.events(*preview);check(result.size()==1&&result[0].result==STG_E_ACCESSDENIED&&!preview->window()&&fixture.log->previews==0,"Read initialization failure unloads hidden handler/window");fixture.streamResult=S_OK;
    fixture.log->previewResult=E_FAIL;preview->request(firstID,lease(fixture.leases));fixture.dispatch(*preview);result=fixture.events(*preview);check(result.size()==1&&FAILED(result[0].result)&&!preview->window()&&!preview->stats().visible,"DoPreview failure cannot leave an empty visible native panel");check(fixture.leases.acquired==fixture.leases.released,"Every failure balances reference leases");
    fixture.log->previewResult=S_OK;preview->request(firstID,lease(fixture.leases));const auto stale=fixture.owner.next();preview->setRoute(fixture.owner.route(52));check(!preview->handleMessage(stale.wParam,stale.lParam)&&preview->drain(51).empty(),"Old route generations cannot activate or drain current host");
    rejects([&]{preview->setRoute({fixture.owner.value,WM_USER,1});},"Nonprivate owner route rejects");rejects([&]{n::NativeShelfFilePreview bad(fixture.owner.route(60),{false,0,800},fixture.services());},"Invalid native dimensions reject before window creation");check(fixture.leases.acquired==fixture.leases.released,"Route replacement releases pending access immediately");
}
void teardownDuringExternalCalls(){
    for(const auto operation:{"site","initialize","window","preview","rect"}){
        Fixture fixture;auto preview=fixture.make();if(std::string_view(operation)=="rect")fixture.open(*preview);
        fixture.log->callback=[&](std::string_view call){if(call==operation)preview.reset();};
        if(std::string_view(operation)=="rect"){const auto popup=preview->window();SendMessageW(popup,WM_SIZE,SIZE_RESTORED,MAKELPARAM(400,300));check(!IsWindow(popup),"Resize callback facade destruction retires owned HWND");}
        else {preview->request(firstID,lease(fixture.leases));const auto message=fixture.owner.next();auto*borrowed=preview.get();check(borrowed->handleMessage(message.wParam,message.lParam),"Facade may be destroyed by synchronous handler callback");}
        check(!preview&&fixture.leases.acquired==fixture.leases.released&&fixture.log->unloads==1&&!fixture.log->unloadInside,"Terminal cleanup waits for outer handler call then releases all references");fixture.owner.discard();
    }
    Fixture fixture;auto preview=fixture.make();fixture.onResolve=[&]{preview.reset();};preview->request(firstID,lease(fixture.leases));const auto message=fixture.owner.next();auto*borrowed=preview.get();check(borrowed->handleMessage(message.wParam,message.lParam)&&!preview&&fixture.factories==0&&fixture.leases.acquired==fixture.leases.released,"Provider reentry suppresses stale handler/window creation");fixture.owner.discard();
    fixture.onResolve={};preview=fixture.make();fixture.open(*preview);const auto popup=preview->window();DestroyWindow(popup);fixture.events(*preview);check(!preview->stats().active&&!preview->window()&&fixture.leases.acquired==fixture.leases.released,"External owned-popup destruction unloads and balances access");
}
std::string pathText(const std::filesystem::path&path){const auto text=path.u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
void actualOwnedReadOnlyStream(){
    const auto root=std::filesystem::temp_directory_path()/("endfield-preview-stream-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));check(std::filesystem::create_directory(root),"Own new temporary preview fixture directory");
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}}cleanup{root};const auto path=root/"synthetic.txt";
    {std::ofstream output(path,std::ios::binary);output<<"abcdef";check(bool(output),"Only synthetic file content is written");}
    n::NativeFileShelfFiles files;auto access=files.acquire(pathText(path));ComPtr<IStream>stream;check(SUCCEEDED(n::openShelfPreviewStream(access,&stream))&&stream,"Production read stream opens exact temporary reference identity");
    STATSTG stat{};check(SUCCEEDED(stream->Stat(&stat,STATFLAG_DEFAULT))&&stat.cbSize.QuadPart==6&&stat.grfMode==STGM_READ&&stat.type==STGTY_STREAM&&stat.pwcsName,"Read-only stream provides bounded native metadata");CoTaskMemFree(stat.pwcsName);
    char value{};ULONG read{};check(stream->Read(&value,1,&read)==S_OK&&read==1&&value=='a',"Checked handle supplies authorized content only on read");ComPtr<IStream>clone;check(SUCCEEDED(stream->Clone(&clone)),"Stream clone shares object handle and independent logical cursor");
    char buffer[4]{};check(clone->Read(buffer,3,&read)==S_OK&&std::string_view(buffer,3)=="bcd","Clone reads from copied logical position");check(stream->Read(&value,1,&read)==S_OK&&value=='b',"Clone cannot move original logical cursor");
    LARGE_INTEGER offset{};offset.QuadPart=-1;check(stream->Seek(offset,STREAM_SEEK_SET,nullptr)==STG_E_INVALIDFUNCTION,"Negative seek rejects safely");offset.QuadPart=0;check(SUCCEEDED(stream->Seek(offset,STREAM_SEEK_END,nullptr))&&stream->Read(&value,1,&read)==S_FALSE&&read==0,"EOF returns explicit short read without fabricated bytes");
    ULONG written=99;check(stream->Write("x",1,&written)==STG_E_ACCESSDENIED&&written==0,"Handler cannot edit original file");ULARGE_INTEGER size{};size.QuadPart=1;check(stream->SetSize(size)==STG_E_ACCESSDENIED,"Handler cannot truncate original file");check(stream->Read(&value,16u*1024u*1024u+1u,&read)==E_INVALIDARG&&read==0,"Oversized individual read fails before touching caller buffer");
    ComPtr<IStream>target;check(SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&target)),"Own memory stream target created");offset.QuadPart=0;stream->Seek(offset,STREAM_SEEK_SET,nullptr);ULARGE_INTEGER copied{},requested{};requested.QuadPart=6;check(stream->CopyTo(target.Get(),requested,&size,&copied)==S_OK&&size.QuadPart==6&&copied.QuadPart==6,"Read stream copy is bounded and reports exact counts");
    auto forged=access.metadata();forged.identity.objectID[0]^=0xff;d::ShelfFileAccess wrong{std::move(forged),[](){}};ComPtr<IStream>rejected;check(FAILED(n::openShelfPreviewStream(wrong,&rejected))&&!rejected,"Same path with different file identity cannot become preview stream");
    access.close();check(!DeleteFileW(path.c_str()),"Read stream itself retains selected-object delete sharing lease after caller closes");stream.Reset();check(!DeleteFileW(path.c_str()),"Clone retains same selected object lifetime");clone.Reset();check(DeleteFileW(path.c_str())!=FALSE,"Last stream release retires handle and only own fixture can delete");
    check(FAILED(n::openShelfPreviewStream({},&rejected))&&!rejected,"Closed reference never implicitly reads a user file");
}
}
int main(){try{pure();COM com;queuedIdentityAndLifetime();toggleReplacementAndKeys();failedAndCanceled();teardownDuringExternalCalls();actualOwnedReadOnlyStream();std::cout<<"PASS "<<checks<<" Shelf native preview/lease checks\n";return 0;}catch(const std::exception&error){std::cerr<<"FAIL after "<<checks<<": "<<error.what()<<'\n';return 1;}}
#else
int main(){try{pure();std::cout<<"PASS "<<checks<<" portable Shelf preview checks; Windows handler/stream fixtures not executed\n";return 0;}catch(const std::exception&error){std::cerr<<"FAIL after "<<checks<<": "<<error.what()<<'\n';return 1;}}
#endif
