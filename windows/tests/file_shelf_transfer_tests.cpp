#include "native/file_shelf_transfer.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#include <atomic>
#include <ole2.h>
#include <shldisp.h>
#include <wrl/client.h>
#endif
using namespace ehud::data;
using namespace endfield::native;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){try{f();}catch(const std::exception&){++checks;return;}throw std::runtime_error(message);}
void set32(std::vector<std::uint8_t>& bytes,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)bytes[at+i]=std::uint8_t(value>>(i*8));}
void codec(){
    const std::vector<std::string> paths{"C:\\fixture\\file.txt","C:\\fixture\\folder","\\\\server\\share\\name.txt","C:\\fixture\\\xe4\xb8\xad\xe6\x96\x87-\xf0\x9f\x93\x9d.txt"};
    const auto encoded=encodeShelfDropPaths(paths);check(decodeShelfDropPaths(encoded)==paths,"Unicode file/directory/UNC paths roundtrip exactly without resolving anything");
    check(encoded[0]==20&&encoded[16]==1&&encoded[encoded.size()-1]==0&&encoded[encoded.size()-4]==0,"Native DROPFILES header and complete double terminator");
    auto bytes=encoded;bytes.insert(bytes.end(),{0xa1,0xff,0x70});check(decodeShelfDropPaths(bytes)==paths,"Allocator padding after double terminator is ignored, including odd total allocation size");
    bytes=encoded;set32(bytes,16,0xffffffff);check(decodeShelfDropPaths(bytes)==paths,"fWide obeys nonzero BOOL semantics");
    bytes=encoded;bytes.insert(bytes.begin()+20,8,0x9f);set32(bytes,0,28);check(decodeShelfDropPaths(bytes)==paths,"Valid offset may skip unused header-to-list bytes");
    rejects([]{(void)encodeShelfDropPaths({});},"Empty outgoing selection rejects");
    for(const char* path:{"relative","C:relative","C:\\a\\..\\b","C:\\a:file","\\\\?\\C:\\a","https://example.invalid/a","C:\\a\\NUL"}){
        const std::vector<std::string> invalid{path};rejects([&]{(void)encodeShelfDropPaths(invalid);},"Invalid outgoing token is never advertised");}
    for(const auto offset:{0u,19u,21u,0xffffffffu}){bytes=encoded;set32(bytes,0,offset);rejects([&]{(void)decodeShelfDropPaths(bytes);},"Invalid incoming offset rejects within allocation");}
    bytes=encoded;set32(bytes,16,0);rejects([&]{(void)decodeShelfDropPaths(bytes);},"ANSI data remains explicit unsupported first-slice format");
    bytes=encoded;bytes.resize(19);rejects([&]{(void)decodeShelfDropPaths(bytes);},"Truncated header rejects");
    bytes=encoded;bytes.resize(bytes.size()-2);rejects([&]{(void)decodeShelfDropPaths(bytes);},"Single terminator cannot stand in for complete list termination");
    bytes=encoded;bytes[20]=0;bytes[21]=0;rejects([&]{(void)decodeShelfDropPaths(bytes);},"Empty incoming list rejects");
    for(const std::uint16_t value:{std::uint16_t(0xd800),std::uint16_t(0xdc00)}){bytes=encoded;bytes[20]=std::uint8_t(value);bytes[21]=std::uint8_t(value>>8);rejects([&]{(void)decodeShelfDropPaths(bytes);},"Malformed UTF-16 cannot become a filesystem path");}
    bytes=encoded;bytes[20]='?';rejects([&]{(void)decodeShelfDropPaths(bytes);},"Incoming path validation is as strict as outgoing validation");
    bytes.assign(shelfTransferMaximumBytes+1,0);rejects([&]{(void)decodeShelfDropPaths(bytes);},"Incoming allocation byte bound rejects before parsing");
    std::vector<std::string> many(shelfTransferMaximumPaths,"C:\\a");check(decodeShelfDropPaths(encodeShelfDropPaths(many))==many,"Maximum explicit path count is lossless");
    many.push_back("C:\\a");rejects([&]{(void)encodeShelfDropPaths(many);},"No outgoing selection truncation beyond count bound");
    bytes=encodeShelfDropPaths(std::span<const std::string>(many.data(),shelfTransferMaximumPaths));bytes.resize(bytes.size()-2);
    const auto extra=encodeShelfDropPaths(std::span<const std::string>(many.data(),1));bytes.insert(bytes.end(),extra.begin()+20,extra.end());rejects([&]{(void)decodeShelfDropPaths(bytes);},"Incoming count overflow rejects complete payload");
    // Arbitrary nonfilesystem bytes exercise bounded decoding without I/O.
    std::uint32_t random=0x52912;for(unsigned i=0;i<4000;++i){std::vector<std::uint8_t> noise(24+i%80);for(auto& byte:noise){random=random*1664525u+1013904223u;byte=std::uint8_t(random>>24);}rejects([&]{(void)decodeShelfDropPaths(noise);},"Malformed small payload rejects safely");}
}
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
void ok(HRESULT value,const char* message){check(SUCCEEDED(value),message);}
struct Ole {Ole(){ok(OleInitialize(nullptr),"Fixture owns one OLE STA");}~Ole(){OleUninitialize();}};
constexpr UINT noticeMessage=WM_APP+217;constexpr UINT_PTR generation=29;
struct Window {
    HWND hwnd{};
    Window(){WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"EndfieldHUDShelfTransferFixture";
        if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register hidden fixture class");
        hwnd=CreateWindowExW(0,cls.lpszClassName,L"Synthetic shelf OLE",WS_POPUP,0,0,100,100,nullptr,nullptr,cls.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Fixture owns only a never-shown window");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);}
    ShelfTransferRoute route()const{return {hwnd,noticeMessage,generation};}
    unsigned drain(){MSG message{};unsigned count{};while(PeekMessageW(&message,hwnd,noticeMessage,noticeMessage,PM_REMOVE)){++count;check(message.wParam==generation&&message.lParam==0,"Notices contain only caller generation");}return count;}
};
std::string id(unsigned value){const auto s=std::to_string(value);return "00000000-0000-4000-8000-"+std::string(12-s.size(),'0')+s;}
struct Files {
    struct Shared {unsigned acquired{},released{},active{};std::map<std::string,ShelfFileMetadata,std::less<>> values;std::function<void()> onResolve;};
    std::shared_ptr<Shared> shared=std::make_shared<Shared>();
    Files(){for(unsigned i=1;i<=2;++i){ShelfFileMetadata metadata;metadata.windowsPath="C:\\synthetic\\file-"+std::to_string(i)+".txt";metadata.name="synthetic";metadata.typeDescription="File";metadata.identity.objectID[0]=std::uint8_t(i);metadata.identity.volumeSerial=3;shared->values[id(i)]=metadata;}}
    static ShelfFileAccess lease(const std::shared_ptr<Shared>& value,const ShelfFileMetadata& metadata){++value->acquired;++value->active;return {metadata,[value]{++value->released;--value->active;}};}
    ShelfCopyBundle bundle(){ShelfCopyBundle b;for(const auto& [key,value]:shared->values){b.ids.push_back(key);b.accesses.push_back(lease(shared,value));}return b;}
    ShelfDragTransfer::Resolver resolver(){return [value=shared](const ShelfRecord& record){if(value->onResolve)value->onResolve();return lease(value,value->values.at(record.id));};}
};
struct MediumCounter final:IUnknown {
    std::atomic<ULONG>refs{1};HGLOBAL memory;std::shared_ptr<unsigned> releases;std::function<void()> callback;
    MediumCounter(HGLOBAL m,std::shared_ptr<unsigned> count,std::function<void()> f):memory(m),releases(std::move(count)),callback(std::move(f)){}
    ~MediumCounter(){GlobalFree(memory);++*releases;if(callback)callback();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown)return E_NOINTERFACE;*out=static_cast<IUnknown*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
};
struct FakeData final:IDataObject {
    std::atomic<ULONG>refs{1};unsigned queries{},gets{};std::vector<std::uint8_t> bytes;std::shared_ptr<unsigned> releases=std::make_shared<unsigned>();
    std::function<void()> onQuery,onGet,onMediumRelease;HRESULT getResult{S_OK};
    FakeData():bytes(encodeShelfDropPaths(std::vector<std::string>{"C:\\synthetic\\file.txt","C:\\synthetic\\folder"})){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IDataObject)return E_NOINTERFACE;*out=static_cast<IDataObject*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f)override{++queries;if(onQuery)onQuery();return f&&f->cfFormat==CF_HDROP&&f->tymed==TYMED_HGLOBAL?S_OK:DV_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC*,STGMEDIUM* out)override{++gets;if(onGet)onGet();if(FAILED(getResult))return getResult;*out={};
        auto memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,bytes.size());if(!memory)return E_OUTOFMEMORY;auto* target=GlobalLock(memory);if(!target){GlobalFree(memory);return E_OUTOFMEMORY;}std::memcpy(target,bytes.data(),bytes.size());GlobalUnlock(memory);
        out->tymed=TYMED_HGLOBAL;out->hGlobal=memory;out->pUnkForRelease=new MediumCounter(memory,releases,onMediumRelease);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*,STGMEDIUM*)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*,STGMEDIUM*,BOOL)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD,IEnumFORMATETC**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,DWORD,IAdviseSink*,DWORD*)override{return OLE_E_ADVISENOTSUPPORTED;}HRESULT STDMETHODCALLTYPE DUnadvise(DWORD)override{return OLE_E_ADVISENOTSUPPORTED;}HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**)override{return OLE_E_ADVISENOTSUPPORTED;}
};
std::vector<std::string> mediumPaths(const STGMEDIUM& medium){auto* p=static_cast<const std::uint8_t*>(GlobalLock(medium.hGlobal));check(p!=nullptr,"Outgoing native medium locks");auto paths=decodeShelfDropPaths({p,GlobalSize(medium.hGlobal)});GlobalUnlock(medium.hGlobal);return paths;}
void outgoing(){
    Window window;Files files;auto transfer=std::make_unique<ShelfDragTransfer>(files.bundle(),files.resolver(),window.route());
    auto* object=transfer->dataObject();FORMATETC format{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
    ok(object->QueryGetData(&format),"Actual data object advertises CF_HDROP");
    auto invalid=format;invalid.lindex=0;check(object->QueryGetData(&invalid)==DV_E_LINDEX,"Invalid format index rejects");invalid=format;invalid.tymed=TYMED_ISTREAM;check(object->QueryGetData(&invalid)==DV_E_TYMED,"Virtual streams are not fabricated");
    ComPtr<IEnumFORMATETC> formats;ok(object->EnumFormatEtc(DATADIR_GET,&formats),"Formats enumerate through actual COM interface");FORMATETC values[3]{};ULONG fetched{};
    check(formats->Next(3,values,&fetched)==S_FALSE&&fetched==2&&values[0].cfFormat==CF_HDROP,"Only filesystem paths and preferred copy are advertised");
    ComPtr<IEnumFORMATETC> cloned;ok(formats->Clone(&cloned),"Format enumerator clones retained position");check(cloned->Next(1,values,nullptr)==S_FALSE,"Clone preserves exhausted position");
    STGMEDIUM medium{};ok(object->GetData(&format,&medium),"Actual GetData validates complete group and creates leased medium");check(files.shared->active==2,"Temporary revalidation handles close before returning paths");
    check(mediumPaths(medium)==std::vector<std::string>{"C:\\synthetic\\file-1.txt","C:\\synthetic\\file-2.txt"},"Outgoing group/order stays complete");
    auto* source=transfer->dropSource();check(source->QueryContinueDrag(FALSE,MK_LBUTTON|MK_SHIFT|MK_CONTROL)==S_OK,"Modifiers do not change copy-only drag continuation");
    check(source->GiveFeedback(DROPEFFECT_MOVE)==DRAGDROP_S_USEDEFAULTCURSORS,"OLE owns native pointer feedback");
    check(window.drain()==1&&transfer->takeChanges(generation+1).started==false&&transfer->takeChanges(generation).started,"Started notice coalesces and stale generations cannot drain it");
    check(source->QueryContinueDrag(TRUE,MK_LBUTTON)==DRAGDROP_S_CANCEL&&source->QueryContinueDrag(FALSE,0)==DRAGDROP_S_DROP,"OLE Escape cancels and initiating button release requests drop");
    transfer->complete(DRAGDROP_S_DROP,DROPEFFECT_COPY);check(transfer->takeChanges(generation).finished,"Completion is deferred owner state");
    transfer.reset();check(files.shared->active==2,"Returned medium retains complete leases after facade destruction");ReleaseStgMedium(&medium);check(files.shared->active==0&&files.shared->acquired==files.shared->released,"Medium completion balances all access independently");
    window.drain();
}
void asyncAndFailures(){
    Window window;Files files;auto transfer=std::make_unique<ShelfDragTransfer>(files.bundle(),files.resolver(),window.route());ComPtr<IDataObjectAsyncCapability> async;
    ok(transfer->dataObject()->QueryInterface(IID_PPV_ARGS(&async)),"Data object exposes real async extraction contract");BOOL enabled{};ok(async->GetAsyncMode(&enabled),"Async mode query succeeds");check(enabled,"Async negotiated extraction is supported without a source worker");
    ok(async->StartOperation(nullptr),"Receiver starts async extraction");check(async->StartOperation(nullptr)==E_UNEXPECTED,"Repeated async start cannot leak another operation lease");
    transfer->complete(DRAGDROP_S_DROP,DROPEFFECT_COPY);transfer.reset();check(files.shared->active==2,"DoDragDrop completion and HUD destruction do not revoke async leases");
    BOOL active{};ok(async->InOperation(&active),"Async state remains queryable after owner closes");check(active,"Async operation remains active until receiver completion");
    ComPtr<IDataObject> retainedData;ok(async.As(&retainedData),"Async receiver retains the same complete data object after HUD closure");
    FORMATETC retainedFormat{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM retainedMedium{};
    ok(retainedData->GetData(&retainedFormat,&retainedMedium),"Async receiver can extract actual paths after HUD closure");check(mediumPaths(retainedMedium).size()==2,"Post-close extraction still includes the complete leased group");
    ReleaseStgMedium(&retainedMedium);retainedData.Reset();
    ok(async->EndOperation(S_OK,nullptr,DROPEFFECT_COPY),"EndOperation balances retained operation reference");check(async->EndOperation(S_OK,nullptr,DROPEFFECT_COPY)==E_UNEXPECTED,"Repeated completion cannot double release");async.Reset();check(files.shared->active==0,"Final async COM release closes all leases");
    window.drain();
    transfer=std::make_unique<ShelfDragTransfer>(files.bundle(),files.resolver(),window.route());FORMATETC f{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM medium{};
    files.shared->values.at(id(2)).identity.objectID[1]=9;
    check(FAILED(transfer->dataObject()->GetData(&f,&medium))&&medium.tymed==TYMED_NULL&&files.shared->active==2,"Late identity replacement rejects entire data extraction with no partial medium or leaked validation handles");
    files.shared->values.at(id(2)).identity.objectID[1]=0;
    files.shared->onResolve=[&]{STGMEDIUM nested{};check(transfer->dataObject()->GetData(&f,&nested)==RPC_E_CALL_REJECTED,"Resolver cannot recursively extract another path bundle");};
    ok(transfer->dataObject()->GetData(&f,&medium),"Nonrecursive extraction completes after rejected reentry");ReleaseStgMedium(&medium);files.shared->onResolve={};transfer.reset();check(files.shared->active==0,"Failed and reentrant extraction leave balanced leases");
    transfer=std::make_unique<ShelfDragTransfer>(files.bundle(),files.resolver(),window.route());auto* borrowed=transfer->dataObject();
    files.shared->onResolve=[&]{transfer.reset();};ok(borrowed->GetData(&f,&medium),"COM self-retention survives owner destruction during metadata callback");
    check(!transfer&&files.shared->active==2&&mediumPaths(medium).size()==2,"Returned medium alone retains every source lease after callback closes owner");
    files.shared->onResolve={};ReleaseStgMedium(&medium);check(files.shared->active==0,"Last medium release retires callback-destroyed transfer exactly once");
    transfer=std::make_unique<ShelfDragTransfer>(files.bundle(),files.resolver(),window.route());ok(transfer->dataObject()->QueryInterface(IID_PPV_ARGS(&async)),"Fresh transfer exposes separate async result");
    ok(async->StartOperation(nullptr),"Coalesced-result fixture starts operation");ok(async->EndOperation(E_FAIL,nullptr,DROPEFFECT_NONE),"Async extraction can fail before drag loop returns");
    transfer->complete(DRAGDROP_S_DROP,DROPEFFECT_COPY);const auto changes=transfer->takeChanges(generation);
    check(changes.finished&&changes.result==DRAGDROP_S_DROP&&changes.effect==DROPEFFECT_COPY&&changes.asyncFinished&&changes.asyncResult==E_FAIL&&changes.asyncEffect==DROPEFFECT_NONE,"Coalesced notifications preserve both drag and asynchronous extraction outcomes");
    async.Reset();transfer.reset();check(files.shared->active==0,"Separate outcome reporting does not change lease ownership");window.drain();
}
void explicitPaste(){
    ComPtr<FakeData> data;data.Attach(new FakeData);
    const auto paths=readShelfTransferPaths(*data.Get());
    check(paths==std::vector<std::string>{"C:\\synthetic\\file.txt","C:\\synthetic\\folder"}&&*data->releases==1,"Explicit paste borrows only the supplied object and releases storage before returning paths");
    data->bytes={1,2,3};bool rejected{};try{(void)readShelfTransferPaths(*data.Get());}catch(...){rejected=true;}
    check(rejected&&*data->releases==2,"Invalid pasted allocation is rejected with balanced ownership");
}
void incomingTarget(){
    Window window;ComPtr<FakeData> data;data.Attach(new FakeData);unsigned commits{};std::vector<std::string> received;
    ShelfDropTarget target(window.route(),{[](POINTL p){return p.x>=0&&p.x<100&&p.y>=0&&p.y<100;},[&](std::span<const std::string> paths){check(*data->releases==1,"External medium is released before storage transaction");++commits;received.assign(paths.begin(),paths.end());return true;}});
    ok(target.registerTarget(),"Actual hidden HWND registers on caller OLE STA");check(target.registerTarget()==S_FALSE,"Repeated target registration is idempotent");
    auto* native=target.dropTarget();DWORD effect=DROPEFFECT_COPY|DROPEFFECT_MOVE;ok(native->DragEnter(data.Get(),MK_SHIFT,{5,5},&effect),"Incoming COM DragEnter succeeds");
    check(effect==DROPEFFECT_COPY&&data->queries==1&&data->gets==0&&commits==0,"Hover negotiates copy without reading payload or touching persistence");
    for(unsigned i=0;i<100;++i){effect=DROPEFFECT_COPY;ok(native->DragOver(0,{5,5},&effect),"Repeated hover is event-only");}
    check(data->gets==0&&window.drain()==1,"Repeated hover coalesces one notice with no payload extraction");auto changes=target.takeChanges(generation);check(changes.feedbackChanged&&changes.hovering&&!changes.committed,"Owner drains projected hover outside COM");
    effect=DROPEFFECT_MOVE;ok(native->DragOver(0,{5,5},&effect),"Move-only source negotiation is handled");check(effect==DROPEFFECT_NONE,"Move-only source cannot cause original mutation");
    effect=DROPEFFECT_COPY;ok(native->Drop(data.Get(),0,{5,5},&effect),"Incoming reference commit completes");check(effect==DROPEFFECT_COPY&&commits==1&&data->gets==1&&*data->releases==1,"COPY is returned only after successful complete storage transaction");
    changes=target.takeChanges(generation);check(changes.committed&&!changes.hovering&&changes.paths==received&&received.size()==2,"Deferred owner notice retains complete committed input group");
    effect=DROPEFFECT_COPY;ok(native->Drop(data.Get(),0,{105,5},&effect),"Outside projected target returns harmless rejection");check(effect==DROPEFFECT_NONE&&commits==1,"Projected acceptance gates actual import");
    target.setEnabled(false);effect=DROPEFFECT_COPY;ok(native->Drop(data.Get(),0,{5,5},&effect),"Disabled target handles stale callback");check(effect==DROPEFFECT_NONE&&data->gets==1,"Inactive module does not access advertised paths");
    target.stop();check(RevokeDragDrop(window.hwnd)==DRAGDROP_E_NOTREGISTERED,"Stop revokes native registration before HWND destruction");window.drain();
}
void incomingFailureAndReentry(){
    Window window;ComPtr<FakeData> data;data.Attach(new FakeData);unsigned commits{};
    auto make=[&](std::function<bool(std::span<const std::string>)> commit){return std::make_unique<ShelfDropTarget>(window.route(),ShelfDropTarget::Callbacks{[](POINTL){return true;},std::move(commit)});};
    auto target=make([&](std::span<const std::string>){++commits;return false;});DWORD effect=DROPEFFECT_COPY;
    ok(target->dropTarget()->Drop(data.Get(),0,{0,0},&effect),"Failed persistence is contained at COM boundary");check(effect==DROPEFFECT_NONE&&commits==1&&target->takeChanges(generation).error.has_value(),"Failed persistence never advertises successful copy");
    target=make([&](std::span<const std::string>){++commits;return true;});data->bytes.resize(19);effect=DROPEFFECT_COPY;
    ok(target->dropTarget()->Drop(data.Get(),0,{0,0},&effect),"Malformed payload rejected safely");check(effect==DROPEFFECT_NONE&&commits==1&&*data->releases==2,"Malformed payload releases medium without invoking storage");
    data->bytes=encodeShelfDropPaths(std::vector<std::string>{"C:\\synthetic\\file.txt"});
    data->onGet=[&]{target->setEnabled(false);};effect=DROPEFFECT_COPY;ok(target->dropTarget()->Drop(data.Get(),0,{0,0},&effect),"Disable during external GetData returns safely");check(effect==DROPEFFECT_NONE&&commits==1,"Generation change during source COM call cancels stale commit");data->onGet={};
    target=make([&](std::span<const std::string>){++commits;return true;});data->onMediumRelease=[&]{target.reset();};ComPtr<IDropTarget> retained=target->dropTarget();effect=DROPEFFECT_COPY;
    ok(retained->Drop(data.Get(),0,{0,0},&effect),"Owner destruction during external medium release is safe");check(!target&&effect==DROPEFFECT_NONE&&commits==1,"Terminal detach invalidates storage callback before external release returns");retained.Reset();data->onMediumRelease={};
    target=make([&](std::span<const std::string>){++commits;target.reset();return true;});retained=target->dropTarget();effect=DROPEFFECT_COPY;
    ok(retained->Drop(data.Get(),0,{0,0},&effect),"Owner may close after successful storage transaction");check(!target&&effect==DROPEFFECT_COPY&&commits==2,"Completed persistence stays successful when owner closes, without late UI callbacks");retained.Reset();window.drain();
}
#endif
}
int main(){try{codec();
#ifdef _WIN32
    Ole ole;outgoing();asyncAndFailures();explicitPaste();incomingTarget();incomingFailureAndReentry();
#else
    std::cout<<"SKIP native OLE fixture: Windows required; no clipboard/files/window accessed\n";
#endif
    std::cout<<"Shelf transfer: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Shelf transfer failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
