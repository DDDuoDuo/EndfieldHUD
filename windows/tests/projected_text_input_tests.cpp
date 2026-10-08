#include "native/projected_text_input.hpp"
#ifdef _WIN32
#include <ocidl.h>
#include <olectl.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace endfield::native;
using namespace endfield::core;
using namespace endfield::core::text;
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks{};
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
void ok(HRESULT value,const char*why){check(SUCCEEDED(value),why);}
struct LayoutFixture final:Layout {
    Document&doc;std::uint64_t current;mutable unsigned calls{};
    explicit LayoutFixture(Document&d):doc(d),current(d.revision()){}
    std::uint64_t textRevision()const noexcept override{return current;}
    std::optional<RangeBounds>bounds(Range r)const override{++calls;return RangeBounds{{double(r.start)*10,5,r.start==r.end?1.0:double(r.end-r.start)*10,18},false};}
    std::optional<std::uint32_t>hit(Point p,bool,bool nearest)const override{return std::uint32_t(std::clamp(nearest?std::round(p.x/10):std::floor(p.x/10),0.0,double(doc.text().size())));}
    void relayout(){current=doc.revision();}
};
struct Sink final:ITextStoreACPSink {
    std::atomic<ULONG>refs{1};unsigned text{},selection{},layout{},locks{};std::vector<DWORD>granted;
    TS_TEXTCHANGE last{};std::function<HRESULT(DWORD)>onLock;std::function<void()>onText,onLayout;std::function<void(REFIID)>onQuery;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;auto callback=onQuery;if(callback)callback(id);if(id!=__uuidof(IUnknown)&&id!=__uuidof(ITextStoreACPSink))return E_NOINTERFACE;*out=static_cast<ITextStoreACPSink*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto r=--refs;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE OnTextChange(DWORD,const TS_TEXTCHANGE*c)override{++text;last=*c;if(onText)onText();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSelectionChange()override{++selection;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLayoutChange(TsLayoutCode code,TsViewCookie cookie)override{++layout;if(onLayout)onLayout();return code==TS_LC_CHANGE&&cookie==ProjectedTextInput::viewCookie?S_OK:E_FAIL;}
    HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnAttrsChange(LONG,LONG,ULONG,const TS_ATTRID*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLockGranted(DWORD flags)override{++locks;granted.push_back(flags);return onLock?onLock(flags):S_OK;}
    HRESULT STDMETHODCALLTYPE OnStartEditTransaction()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnEndEditTransaction()override{return S_OK;}
};
// Only GetExtent/GetRange are consumed by this owner composition adapter. All
// unrelated fake range operations explicitly reject use instead of pretending
// to implement a second text engine or activating the user's real TSF profile.
struct FakeRange final:ITfRangeACP {
    std::atomic<ULONG>refs{1};LONG start{},length{};std::function<void()>onExtent;
    FakeRange(LONG a,LONG n):start(a),length(n){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown)&&id!=__uuidof(ITfRange)&&id!=__uuidof(ITfRangeACP))return E_NOINTERFACE;*out=static_cast<ITfRangeACP*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto r=--refs;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE GetExtent(LONG*a,LONG*n)override{if(!a||!n)return E_POINTER;if(onExtent)onExtent();*a=start;*n=length;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetExtent(LONG a,LONG n)override{start=a;length=n;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetText(TfEditCookie,DWORD,WCHAR*,ULONG,ULONG*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetText(TfEditCookie,DWORD,const WCHAR*,LONG)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetFormattedText(TfEditCookie,IDataObject**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetEmbedded(TfEditCookie,REFGUID,REFIID,IUnknown**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE InsertEmbedded(TfEditCookie,DWORD,IDataObject*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ShiftStart(TfEditCookie,LONG,LONG*,const TF_HALTCOND*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ShiftEnd(TfEditCookie,LONG,LONG*,const TF_HALTCOND*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ShiftStartToRange(TfEditCookie,ITfRange*,TfAnchor)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ShiftEndToRange(TfEditCookie,ITfRange*,TfAnchor)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ShiftStartRegion(TfEditCookie,TfShiftDir,BOOL*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ShiftEndRegion(TfEditCookie,TfShiftDir,BOOL*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE IsEmpty(TfEditCookie,BOOL*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Collapse(TfEditCookie,TfAnchor)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE IsEqualStart(TfEditCookie,ITfRange*,TfAnchor,BOOL*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE IsEqualEnd(TfEditCookie,ITfRange*,TfAnchor,BOOL*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CompareStart(TfEditCookie,ITfRange*,TfAnchor,LONG*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CompareEnd(TfEditCookie,ITfRange*,TfAnchor,LONG*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE AdjustForInsert(TfEditCookie,ULONG,BOOL*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetGravity(TfGravity*,TfGravity*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetGravity(TfEditCookie,TfGravity,TfGravity)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Clone(ITfRange**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetContext(ITfContext**)override{return E_NOTIMPL;}
};
struct FakeView final:ITfCompositionView {
    std::atomic<ULONG>refs{1};ComPtr<FakeRange>range;std::function<void()>onRange;
    explicit FakeView(FakeRange*r):range(r){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown)&&id!=__uuidof(ITfCompositionView))return E_NOINTERFACE;*out=static_cast<ITfCompositionView*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto r=--refs;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE GetOwnerClsid(CLSID*out)override{if(!out)return E_POINTER;*out={};return S_OK;}
    HRESULT STDMETHODCALLTYPE GetRange(ITfRange**out)override{if(!out)return E_POINTER;if(onRange)onRange();*out=range.Get();range->AddRef();return S_OK;}
};
struct FakeContext final:ITfContext,ITfContextOwnerCompositionServices {
    std::atomic<ULONG>refs{1};std::function<HRESULT(ITfCompositionView*)>terminate;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(ITfContext))*out=static_cast<ITfContext*>(this);else if(id==__uuidof(ITfContextOwnerCompositionServices)||id==__uuidof(ITfContextComposition))*out=static_cast<ITfContextOwnerCompositionServices*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto r=--refs;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE RequestEditSession(TfClientId,ITfEditSession*,DWORD,HRESULT*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE InWriteSession(TfClientId,BOOL*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetSelection(TfEditCookie,ULONG,ULONG,TF_SELECTION*,ULONG*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetSelection(TfEditCookie,ULONG,const TF_SELECTION*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetStart(TfEditCookie,ITfRange**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetEnd(TfEditCookie,ITfRange**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetActiveView(ITfContextView**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumViews(IEnumTfContextViews**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetStatus(TF_STATUS*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID,ITfProperty**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetAppProperty(REFGUID,ITfReadOnlyProperty**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TrackProperties(const GUID**,ULONG,const GUID**,ULONG,ITfReadOnlyProperty**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumProperties(IEnumTfProperties**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateRangeBackup(TfEditCookie,ITfRange*,ITfRangeBackup**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE StartComposition(TfEditCookie,ITfRange*,ITfCompositionSink*,ITfComposition**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumCompositions(IEnumITfCompositionView**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE FindComposition(TfEditCookie,ITfRange*,IEnumITfCompositionView**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TakeOwnership(TfEditCookie,ITfCompositionView*,ITfCompositionSink*,ITfComposition**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TerminateComposition(ITfCompositionView*view)override{return terminate?terminate(view):E_NOTIMPL;}
};
struct FakeDocument final:ITfDocumentMgr {
    std::atomic<ULONG>refs{1};ComPtr<FakeContext>context;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown)&&id!=__uuidof(ITfDocumentMgr))return E_NOINTERFACE;*out=static_cast<ITfDocumentMgr*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto r=--refs;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE CreateContext(TfClientId,DWORD,IUnknown*,ITfContext**out,TfEditCookie*cookie)override{if(!out||!cookie)return E_POINTER;context.Attach(new FakeContext);*out=context.Get();context->AddRef();*cookie=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE Push(ITfContext*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE Pop(DWORD)override{context.Reset();return S_OK;}
    HRESULT STDMETHODCALLTYPE GetTop(ITfContext**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetBase(ITfContext**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumContexts(IEnumTfContexts**)override{return E_NOTIMPL;}
};
struct FakeManager final:ITfThreadMgr,ITfKeystrokeMgr {
    std::atomic<ULONG>refs{1};std::vector<ComPtr<FakeDocument>>documents;ComPtr<ITfDocumentMgr>current;std::function<void()>onGetFocus,onSetFocus,onTest;unsigned keyCalls{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(ITfThreadMgr))*out=static_cast<ITfThreadMgr*>(this);else if(id==__uuidof(ITfKeystrokeMgr))*out=static_cast<ITfKeystrokeMgr*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto r=--refs;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE Activate(TfClientId*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Deactivate()override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CreateDocumentMgr(ITfDocumentMgr**out)override{if(!out)return E_POINTER;ComPtr<FakeDocument>doc;doc.Attach(new FakeDocument);*out=doc.Get();doc->AddRef();documents.push_back(std::move(doc));return S_OK;}
    HRESULT STDMETHODCALLTYPE EnumDocumentMgrs(IEnumTfDocumentMgrs**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetFocus(ITfDocumentMgr**out)override{if(!out)return E_POINTER;auto callback=onGetFocus;if(callback)callback();*out=current.Get();if(*out)(*out)->AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE SetFocus(ITfDocumentMgr*value)override{if(!value)return E_INVALIDARG;current=value;auto callback=onSetFocus;if(callback)callback();return S_OK;}
    HRESULT STDMETHODCALLTYPE AssociateFocus(HWND,ITfDocumentMgr*,ITfDocumentMgr**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE IsThreadFocus(BOOL*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetFunctionProvider(REFCLSID,ITfFunctionProvider**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumFunctionProviders(IEnumTfFunctionProviders**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetGlobalCompartment(ITfCompartmentMgr**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE AdviseKeyEventSink(TfClientId,ITfKeyEventSink*,BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE UnadviseKeyEventSink(TfClientId)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetForeground(CLSID*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TestKeyDown(WPARAM,LPARAM,BOOL*out)override{if(!out)return E_POINTER;auto callback=onTest;if(callback)callback();*out=TRUE;return S_OK;}
    HRESULT STDMETHODCALLTYPE TestKeyUp(WPARAM w,LPARAM l,BOOL*out)override{return TestKeyDown(w,l,out);}
    HRESULT STDMETHODCALLTYPE KeyDown(WPARAM,LPARAM,BOOL*out)override{if(!out)return E_POINTER;++keyCalls;*out=TRUE;return S_OK;}
    HRESULT STDMETHODCALLTYPE KeyUp(WPARAM w,LPARAM l,BOOL*out)override{return KeyDown(w,l,out);}
    HRESULT STDMETHODCALLTYPE GetPreservedKey(ITfContext*,const TF_PRESERVEDKEY*,GUID*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE IsPreservedKey(REFGUID,const TF_PRESERVEDKEY*,BOOL*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE PreserveKey(TfClientId,REFGUID,const TF_PRESERVEDKEY*,const WCHAR*,ULONG)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE UnpreserveKey(REFGUID,const TF_PRESERVEDKEY*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPreservedKeyDescription(REFGUID,const WCHAR*,ULONG)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPreservedKeyDescription(REFGUID,BSTR*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SimulatePreservedKey(ITfContext*,REFGUID,BOOL*)override{return E_NOTIMPL;}
};
struct Window {
    HWND handle{CreateWindowExW(0,L"STATIC",L"Owned projected-input contract fixture",WS_POPUP,17,23,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
    Window(){check(handle!=nullptr,"Owned hidden test window creates");}
    ~Window(){if(handle)DestroyWindow(handle);}
};
unsigned messages(HWND window,UINT message,UINT_PTR token){MSG value{};unsigned count{};while(PeekMessageW(&value,window,message,message,PM_REMOVE)){check(value.wParam==token&&value.lParam==0,"Posted message contains generation, no raw pointer");++count;}return count;}
void lock(ITextStoreACP*store,Sink*sink,DWORD flags,std::function<void()>fn){sink->onLock=[&](DWORD){fn();return S_OK;};HRESULT session{};ok(store->RequestLock(flags,&session),"Document lock protocol call succeeds");ok(session,"Lock callback succeeds");sink->onLock={};}
void run(){
    Window window;constexpr UINT message=WM_APP+517;constexpr UINT_PTR token=451;
    Buffer doc(u"终末地 日本語 한국어 😀",100);LayoutFixture layout(doc);ProjectedTextInput input(window.handle,doc,layout,message,token);auto*store=input.textStore();
    ComPtr<Sink>sink; sink.Attach(new Sink);ComPtr<Sink>other;other.Attach(new Sink);
    check(!input.focused()&&input.focus()==TF_E_DISCONNECTED,"No focus service created implicitly");
    check(!input.filterKeyMessage(WM_KEYDOWN,VK_SPACE,0),"Unfocused owner does not consume keys");
    HRESULT session{};check(store->RequestLock(TS_LF_READ,&session)==E_FAIL,"Lock without advised sink rejected");
    ok(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_ALL_SINKS),"Fake sink installed");
    ok(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_ALL_SINKS),"Same canonical sink updates mask");
    check(store->AdviseSink(__uuidof(ITextStoreACPSink),other.Get(),TS_AS_ALL_SINKS)==CONNECT_E_ADVISELIMIT,"Second distinct sink rejected");
    check(store->UnadviseSink(other.Get())==CONNECT_E_NOCONNECTION,"Wrong sink cannot disconnect owner");
    LONG end{};check(store->GetEndACP(&end)==TS_E_NOLOCK,"ACP access requires read lock");
    TS_SELECTION_ACP select{0,1,{TS_AE_END,FALSE}};check(store->SetSelection(1,&select)==TS_E_NOLOCK,"Selection mutation requires write lock");
    check(store->RequestLock(0,&session)==E_INVALIDARG,"Zero lock type rejected");check(store->RequestLock(TS_LF_READ|0x100,&session)==E_INVALIDARG,"Unknown lock flags rejected");
    sink->granted.clear();sink->onLock=[&](DWORD flags){
        if((flags&TS_LF_READWRITE)==TS_LF_READ){HRESULT nested{};ok(store->RequestLock(TS_LF_READWRITE|TS_LF_SYNC,&nested),"Nested sync request returns protocol result");check(nested==TS_E_SYNCHRONOUS,"Nested synchronous upgrade refused");
            ok(store->RequestLock(TS_LF_READWRITE,&nested),"Asynchronous upgrade queued");check(nested==TS_S_ASYNC,"Read-write upgrade reports asynchronous status");
            ok(store->RequestLock(TS_LF_READWRITE,&nested),"Duplicate upgrade coalesced");check(nested==TS_S_ASYNC,"Duplicate upgrade is asynchronous");
            TS_TEXTCHANGE c{};check(store->SetText(0,0,1,L"x",1,&c)==TS_E_NOLOCK,"Read lock cannot mutate");check(input.replaceFromHost({0,1},u"x")==TS_E_NOLOCK,"Host cannot mutate inside TSF lock");}
        else{ok(store->SetSelection(1,&select),"Queued upgrade grants writable selection");}return S_OK;};
    ok(store->RequestLock(TS_LF_READ,&session),"Read lock with reentrant upgrade completes");ok(session,"Read callback completed");
    check(sink->granted==std::vector<DWORD>{TS_LF_READ,TS_LF_READWRITE},"Exactly one write upgrade follows read callback");sink->onLock={};
    check(!sink->selection&&!sink->text,"TSF selection mutation does not echo sink notifications");
    lock(store,sink.Get(),TS_LF_READ,[&]{
        ok(store->GetEndACP(&end),"Read lock exposes end ACP");check(end==14,"ACP counts UTF-16, not code points");
        WCHAR text[4]{WCHAR(0x1111),WCHAR(0x1111),WCHAR(0x1111),WCHAR(0x1111)};ULONG chars{},runs{};LONG next{};TS_RUNINFO run{};
        ok(store->GetText(0,-1,text,3,&chars,&run,1,&runs,&next),"Bounded partial UTF-16 retrieval");check(chars==3&&runs==1&&next==3&&run.uCount==3&&run.type==TS_RT_PLAIN,"Text and run lengths stay bounded");check(text[3]==WCHAR(0x1111),"GetText never writes terminator outside capacity");
        ok(store->GetText(3,-1,nullptr,0,&chars,&run,1,&runs,&next),"Run-only text query works");check(chars==0&&runs==1&&run.uCount==11&&next==14,"Run-only query does not claim copied text");
        ok(store->GetText(14,-1,nullptr,0,&chars,nullptr,0,&runs,&next),"Empty end query works");check(next==14&&!chars&&!runs,"End query returns empty without advancing");
        check(store->GetText(0,999,text,3,&chars,&run,1,&runs,&next)==TS_E_INVALIDPOS,"Invalid end ACP rejected");
        ULONG fetched{};TS_SELECTION_ACP result{};ok(store->GetSelection(TS_DEFAULT_SELECTION,1,&result,&fetched),"Default selection retrieved");check(fetched==1&&result.acpStart==0&&result.acpEnd==1,"Selection query matches host model");
    });
    input.takeChanges(token);messages(window.handle,message,token);
    lock(store,sink.Get(),TS_LF_READWRITE,[&]{
        LONG a{},b{};TS_TEXTCHANGE c{};ok(store->InsertTextAtSelection(TS_IAS_QUERYONLY,L"测试",2,&a,&b,nullptr),"Query-only insertion valid");check(a==0&&b==1&&doc.text().starts_with(u"终"),"Query-only cannot change model");
        ok(store->InsertTextAtSelection(0,L"测试",2,&a,&b,&c),"TSF inserts CJK text");check(c.acpStart==0&&c.acpOldEnd==1&&c.acpNewEnd==2&&a==0&&b==2,"Inserted extent and change retain exact UTF-16 ends");
        check(doc.text().starts_with(u"测试末地"),"Text mutation uses caller-owned model");
        check(store->InsertTextAtSelection(TS_IAS_QUERYONLY|TS_IAS_NOQUERY,L"x",1,&a,&b,&c)==E_INVALIDARG,"Conflicting insertion flags rejected");
        check(store->SetText(0,0,999,L"x",1,&c)==TS_E_INVALIDPOS,"Invalid text mutation range rejected");
        WCHAR broken=WCHAR(0xd800);check(store->SetText(0,0,1,&broken,1,&c)==E_INVALIDARG,"Invalid surrogate insertion rejected");
    });
    check(sink->text==0&&sink->selection==0,"TSF text edits never echo OnTextChange or OnSelectionChange");
    check(messages(window.handle,message,token)==1,"Repeated TSF changes coalesce one host message");check(input.takeChanges(token+1)==0,"Stale generation cannot drain current field");
    const auto flags=input.takeChanges(token);check((flags&unsigned(TextInputChange::text))&&(flags&unsigned(TextInputChange::selection))&&(flags&unsigned(TextInputChange::layout)),"Host receives text/selection/layout work flags");
    check(!input.takeChanges(token),"Repeated drain is idle");
    ok(input.replaceFromHost({0,2},u"明日"),"Host edit succeeds outside lock");check(sink->text==1&&sink->selection==1,"Host edit notifies TSF sink once");
    check(sink->last.acpStart==0&&sink->last.acpOldEnd==2&&sink->last.acpNewEnd==2,"Host notification has exact replacement extent");
    check(input.layoutChanged()==TS_E_NOLAYOUT,"Stale layout cannot announce readiness");layout.relayout();ok(input.layoutChanged(),"Caller announces current layout");check(sink->layout==1,"Layout notification sent once");
    Placement place{{},{30,40,100,50},{0,0},true};ok(input.setPlacement(place),"Logical placement installed");check(input.setPlacement(place)==S_FALSE,"Unchanged placement is a no-op");
    place.projection.values={1,.12,10,.17,1,5,.0004,.0006,1};ok(input.setPlacement(place),"Tilt changes candidate projection");
    lock(store,sink.Get(),TS_LF_READ,[&]{
        RECT rect{};BOOL clipped{};ok(store->GetTextExt(1,1,3,&rect,&clipped),"TSF range uses projected host layout");check(rect.right>rect.left&&rect.bottom>rect.top&&!clipped,"Visible projected range has valid screen rectangle");
        ok(store->GetTextExt(1,2,2,&rect,&clipped),"Collapsed candidate caret query supported");check(rect.right>rect.left,"Caret receives narrow visible bounds");
        POINT origin{};ClientToScreen(window.handle,&origin);const auto p=place.projection.project({50,50});POINT point{LONG(std::round(p->x))+origin.x,LONG(std::round(p->y))+origin.y};LONG hit{};
        ok(store->GetACPFromPoint(1,&point,GXFPF_ROUND_NEAREST,&hit),"TSF hit unprojects through shared plane");check(hit==2,"Screen hit agrees with painted logical caret position");
        check(store->GetTextExt(2,1,3,&rect,&clipped)==E_INVALIDARG,"Unknown view cookie rejected");
    });
    doc.setReadOnly(true);lock(store,sink.Get(),TS_LF_READWRITE,[&]{TS_TEXTCHANGE c{};check(store->SetText(0,0,1,L"x",1,&c)==TS_E_READONLY,"Read-only model rejects TSF text mutation");ok(store->SetSelection(1,&select),"Read-only model still allows text selection");});doc.setReadOnly(false);
    ComPtr<ITfContextOwnerCompositionSink>composition;ok(store->QueryInterface(IID_PPV_ARGS(&composition)),"Owner composition interface exposed");
    ComPtr<FakeRange>range;range.Attach(new FakeRange(0,2));ComPtr<FakeView>view;view.Attach(new FakeView(range.Get()));BOOL accepted{};
    const auto original=std::u16string(doc.text());const auto oldSelection=doc.selection();ok(composition->OnStartComposition(view.Get(),&accepted),"Synthetic composition starts");check(accepted&&doc.composition()==Range{0,2},"Composition owns bounded marked range");
    check(input.replaceFromHost({0,1},u"x")==TS_E_NOLOCK,"Host edit does not race marked text");
    lock(store,sink.Get(),TS_LF_READWRITE,[&]{TS_TEXTCHANGE c{};ok(store->SetText(0,0,2,L"中文输入",4,&c),"IME provisional CJK replacement mutates model");});
    range->start=0;range->length=4;ok(composition->OnUpdateComposition(view.Get(),range.Get()),"Pending new composition range applied");check(doc.composition()==Range{0,4},"Marked range updates independently of old view");
    ok(input.cancelComposition(),"Explicit cancel restores original document");check(doc.text()==original&&doc.selection()==oldSelection&&!doc.composition(),"Cancellation restores text, selection and clears marked range");check(sink->text==2,"Host cancellation reports one rollback notification");
    ok(composition->OnStartComposition(view.Get(),&accepted),"Next independent composition starts");ok(composition->OnEndComposition(view.Get()),"Composition commit ends cleanly");check(!doc.composition(),"Commit clears marked range");
    check(composition->OnEndComposition(view.Get())==E_INVALIDARG,"Duplicate composition end rejected");
    HRESULT wrongThread{};std::thread thread([&]{TS_STATUS status{};wrongThread=store->GetStatus(&status);});thread.join();check(wrongThread==RPC_E_WRONG_THREAD,"Foreign thread cannot touch owner document");
    ok(store->UnadviseSink(sink.Get()),"Explicit sink unadvise releases retention");check(store->UnadviseSink(sink.Get())==CONNECT_E_NOCONNECTION,"Repeated unadvise explicit");ok(input.stop(),"Stopped field detaches host/COM lifetime");check(input.stop()==S_FALSE,"Stop is idempotent");check(store->GetEndACP(&end)==E_UNEXPECTED,"Retained interface cannot access detached document");
    // Reentrant callbacks stop or destroy the facade, while Store keeps its own
    // COM lifetime and validates host pointers again after the external call.
    {
        Window isolated;constexpr UINT ownerMessage=WM_APP+518;constexpr UINT_PTR ownerToken=901;
        Buffer d(u"abc");LayoutFixture l(d);ProjectedTextInput field(isolated.handle,d,l,ownerMessage,ownerToken);auto*ts=field.textStore();
        ComPtr<FakeManager>manager;manager.Attach(new FakeManager);ok(field.connect(*manager.Get(),1),"Host-pose fixture connects borrowed fake manager");ok(field.focus(),"Host-pose fixture focuses its fake text context");
        ComPtr<Sink>s;s.Attach(new Sink);ok(ts->AdviseSink(__uuidof(ITextStoreACPSink),s.Get(),TS_AS_ALL_SINKS),"Host-pose fixture observes required TSF layout callbacks");
        s->onLayout=[&]{lock(ts,s.Get(),TS_LF_READ,[&]{RECT extent{};BOOL clipped{};ok(ts->GetTextExt(ProjectedTextInput::viewCookie,0,1,&extent,&clipped),"TSF can query the current candidate extent synchronously during pose notification");check(extent.right>extent.left&&extent.bottom>extent.top,"Required pose notification exposes valid current text geometry");});};
        Placement current{{},{0,0,100,50},{0,0},true};
        for(unsigned frame=0;frame<8;++frame){current.projection.values[2]=double(frame);ok(field.setPlacement(current),"Owner updates a moving text plane");}
        check(s->layout==8&&s->locks==8,"Every changed focused pose still notifies TSF and permits its geometry read lock");
        check(messages(isolated.handle,ownerMessage,ownerToken)==0&&field.takeChanges(ownerToken)==0,"Host-originated pose changes cannot echo owner work or schedule another frame");
        check(field.setPlacement(current)==S_FALSE&&s->layout==8,"Equal placement sends neither TSF nor owner work");
        ok(field.layoutChanged(),"Explicit ready-layout callback remains available");check(s->layout==9&&messages(isolated.handle,ownerMessage,ownerToken)==0,"Ready-layout notification reaches TSF without an owner echo");
        s->onLayout={};
        ok(field.replaceFromHost({0,1},u"z"),"Genuine host text change remains queued");ok(field.selectFromHost({{1,2},ActiveEnd::end,false}),"Genuine host selection change remains queued");
        check(messages(isolated.handle,ownerMessage,ownerToken)==1,"Text and selection work still coalesce one owner notification");const auto work=field.takeChanges(ownerToken);
        check((work&unsigned(TextInputChange::text))&&(work&unsigned(TextInputChange::selection))&&(work&unsigned(TextInputChange::layout)),"Removing pose echo preserves real text, selection and required layout-work flags");
        l.relayout();const auto selectionCallbacks=s->selection;lock(ts,s.Get(),TS_LF_READWRITE,[&]{TS_SELECTION_ACP selection{0,1,{TS_AE_END,FALSE}};ok(ts->SetSelection(1,&selection),"TSF-originated selection changes caller document");});
        check(messages(isolated.handle,ownerMessage,ownerToken)==1&&field.takeChanges(ownerToken)==unsigned(TextInputChange::selection)&&s->selection==selectionCallbacks,"TSF selection work reaches owner without reflecting it back to TSF");
        ComPtr<ITfContextOwnerCompositionSink>c;ok(ts->QueryInterface(IID_PPV_ARGS(&c)),"Owner composition protocol remains installed");ComPtr<FakeRange>r;r.Attach(new FakeRange(0,1));ComPtr<FakeView>v;v.Attach(new FakeView(r.Get()));BOOL accepted{};
        ok(c->OnStartComposition(v.Get(),&accepted),"Genuine marked composition starts");check(accepted,"Composition is accepted");ok(c->OnEndComposition(v.Get()),"Genuine marked composition ends");
        check(messages(isolated.handle,ownerMessage,ownerToken)==1&&field.takeChanges(ownerToken)==unsigned(TextInputChange::composition),"Composition lifecycle continues to schedule owner work");
        ok(ts->UnadviseSink(s.Get()),"Detach host-pose fixture observer");ok(field.stop(),"Stop host-pose fixture without a visible window");
    }
    for(bool inExtent:{false,true}){
        Buffer d(u"日本語");LayoutFixture l(d);auto field=std::make_unique<ProjectedTextInput>(window.handle,d,l,message,token+1);ComPtr<ITextStoreACP>retained=field->textStore();ComPtr<ITfContextOwnerCompositionSink>c;ok(retained.As(&c),"Reentrant fixture composition interface");
        ComPtr<FakeRange>r;r.Attach(new FakeRange(0,1));ComPtr<FakeView>v;v.Attach(new FakeView(r.Get()));if(inExtent)r->onExtent=[&]{field.reset();};else v->onRange=[&]{field.reset();};
        BOOL allow{};check(c->OnStartComposition(v.Get(),&allow)==E_UNEXPECTED,"Composition callback detects owner destruction");check(!allow&&!d.composition(),"Destroyed owner cannot start stale composition");
    }
    {
        Buffer d(u"한국어");LayoutFixture l(d);auto field=std::make_unique<ProjectedTextInput>(window.handle,d,l,message,token+2);ComPtr<ITextStoreACP>retained=field->textStore();ComPtr<Sink>s;s.Attach(new Sink);ok(retained->AdviseSink(__uuidof(ITextStoreACPSink),s.Get(),TS_AS_ALL_SINKS),"Destroyed-owner lock sink installed");
        s->onLock=[&](DWORD){field.reset();return S_OK;};HRESULT result{};ok(retained->RequestLock(TS_LF_READ,&result),"Owner destruction during lock callback is safe");check(retained->GetEndACP(&end)==E_UNEXPECTED,"Retained stopped store never touches caller data");
    }
    {
        Buffer d(u"safe");LayoutFixture l(d);auto field=std::make_unique<ProjectedTextInput>(window.handle,d,l,message,token+3);ComPtr<Sink>s;s.Attach(new Sink);ok(field->textStore()->AdviseSink(__uuidof(ITextStoreACPSink),s.Get(),TS_AS_ALL_SINKS),"Host notification reentrancy sink installed");s->onText=[&]{field.reset();};auto*raw=field.get();ok(raw->replaceFromHost({0,1},u"S"),"Host callback destruction preserves retained Store lifetime");check(!field&&d.text()==u"Safe"&&s->text==1&&!s->selection,"Destroyed owner receives no later selection notification");
    }
    for(bool destroy:{false,true}){
        Buffer d(u"原来的文本");const Selection originalSelection{{0,3},ActiveEnd::start,false};d.setSelection(originalSelection);LayoutFixture l(d);auto field=std::make_unique<ProjectedTextInput>(window.handle,d,l,message,token+4);ComPtr<ITextStoreACP>retained=field->textStore();ComPtr<Sink>s;s.Attach(new Sink);ok(retained->AdviseSink(__uuidof(ITextStoreACPSink),s.Get(),TS_AS_ALL_SINKS),"Insertion-first composition sink installed");ComPtr<ITfContextOwnerCompositionSink>c;ok(retained.As(&c),"Insertion-first composition interface");
        ComPtr<FakeRange>r;r.Attach(new FakeRange(0,1));ComPtr<FakeView>v;v.Attach(new FakeView(r.Get()));
        lock(retained.Get(),s.Get(),TS_LF_READWRITE,[&]{TS_TEXTCHANGE change{};LONG a{},b{};ok(retained->InsertTextAtSelection(0,L"中",1,&a,&b,&change),"Initial IME insertion precedes composition start");BOOL allow{};ok(c->OnStartComposition(v.Get(),&allow),"Composition promotes pre-insertion transaction snapshot");check(allow,"First provisional composition accepted");ok(retained->SetText(0,0,1,L"中文输入",4,&change),"Further provisional insertion updates composition");if(destroy)field.reset();});
        if(!destroy)ok(field->cancelComposition(),"Insertion-first composition cancels");check(d.text()==u"原来的文本"&&d.selection()==originalSelection&&!d.composition(),"Cancellation or terminal lock destruction restores pre-insertion text and selection");
    }
    {
        Buffer d(u"original");d.setSelection({{0,3},ActiveEnd::start,false});const auto beforeSelection=d.selection();LayoutFixture l(d);ProjectedTextInput field(window.handle,d,l,message,token+5);ComPtr<FakeManager>manager;manager.Attach(new FakeManager);ok(field.connect(*manager.Get(),1),"Shared injected fake TSF manager connects");check(manager->documents.size()==2,"Connected field has one context and empty focus parking document");ok(field.focus(),"Owner explicitly focuses connected field");check(field.focused()&&manager->current.Get()==manager->documents[0].Get(),"Shared manager focus belongs to field");check(field.filterKeyMessage(WM_KEYDOWN,VK_SPACE,0)&&manager->keyCalls==1,"Focused owner lets TSF consume pre-translation key");
        ComPtr<Sink>s;s.Attach(new Sink);ok(field.textStore()->AdviseSink(__uuidof(ITextStoreACPSink),s.Get(),TS_AS_ALL_SINKS),"Connected fake composition sink");ComPtr<ITfContextOwnerCompositionSink>c;ok(field.textStore()->QueryInterface(IID_PPV_ARGS(&c)),"Connected owner composition sink");ComPtr<FakeRange>r;r.Attach(new FakeRange(0,1));ComPtr<FakeView>v;v.Attach(new FakeView(r.Get()));
        lock(field.textStore(),s.Get(),TS_LF_READWRITE,[&]{TS_TEXTCHANGE change{};LONG a{},b{};ok(field.textStore()->InsertTextAtSelection(0,L"中",1,&a,&b,&change),"Connected first provisional character");BOOL accepted{};ok(c->OnStartComposition(v.Get(),&accepted),"Connected composition starts after insertion");check(accepted,"Connected marked range accepted");});
        bool inTerminationLock{};auto context=manager->documents[0]->context;
        context->terminate=[&](ITfCompositionView*view){lock(field.textStore(),s.Get(),TS_LF_READWRITE,[&]{inTerminationLock=true;ok(c->OnEndComposition(view),"TerminateComposition ends marker under its synchronous lock");check(d.text()==u"中ginal"&&d.composition().has_value(),"Host rollback is deferred until termination write lock returns");check(s->text==0,"No host text notification inside termination lock");inTerminationLock=false;});check(d.text()==u"中ginal","Rollback waits until TerminateComposition returns");return S_OK;};
        s->onText=[&]{check(!inTerminationLock,"Rollback notification occurs outside TSF write lock");};ok(field.cancelComposition(),"Connected composition termination restores host snapshot");check(d.text()==u"original"&&d.selection()==beforeSelection&&!d.composition(),"Connected cancel preserves original selection and all text");check(s->text==1,"Connected cancellation reports one rollback");ok(field.blur(),"Connected focus exits explicitly");check(!field.focused()&&manager->current.Get()==manager->documents[1].Get(),"Empty parking document prevents stale editor focus");ok(field.focus(),"Connected field can regain focus");ok(field.stop(),"Connected lifecycle pops context and detaches");check(!manager->documents[0]->context,"Stop releases connected document context");
    }
    for(unsigned where=0;where<4;++where){
        Buffer d(u"testing");LayoutFixture l(d);auto field=std::make_unique<ProjectedTextInput>(window.handle,d,l,message,token+6);ComPtr<FakeManager>manager;manager.Attach(new FakeManager);ok(field->connect(*manager.Get(),1),"Reentrant focus fixture connects");
        if(where==0){manager->onGetFocus=[&]{manager->onGetFocus={};field.reset();};auto*raw=field.get();check(raw->focus()==E_UNEXPECTED,"GetFocus teardown invalidates host before state write");}
        else if(where==1){manager->onSetFocus=[&]{manager->onSetFocus={};field.reset();};auto*raw=field.get();check(raw->focus()==E_UNEXPECTED,"SetFocus callback teardown detected");check(manager->current.Get()==manager->documents[1].Get(),"Reentrant SetFocus destruction restores parking focus");}
        else{ok(field->focus(),"Reentrant key/blur fixture focuses");if(where==2){manager->onTest=[&]{manager->onTest={};field.reset();};auto*raw=field.get();check(!raw->filterKeyMessage(WM_KEYDOWN,VK_SPACE,0)&&!manager->keyCalls,"Destroyed owner skips second key COM call");}
            else{manager->onGetFocus=[&]{manager->onGetFocus={};field.reset();};auto*raw=field.get();check(raw->blur()==E_UNEXPECTED,"Blur focus callback teardown safely returns");}
            check(manager->current.Get()==manager->documents[1].Get(),"Reentrant key/blur destruction restores parked focus");}
        check(!field&&!manager->documents[0]->context,"Reentrant manager callback releases stale field context");
    }
    for(bool duringAdvise:{false,true}){
        Buffer d(u"A");LayoutFixture l(d);ProjectedTextInput field(window.handle,d,l,message,token+7);ComPtr<Sink>a,b;a.Attach(new Sink);b.Attach(new Sink);auto*ts=field.textStore();bool reentered{};
        if(!duringAdvise){ok(ts->AdviseSink(__uuidof(ITextStoreACPSink),a.Get(),TS_AS_ALL_SINKS),"Replacement-advice fixture starts with A");a->onQuery=[&](REFIID id){if(!reentered&&id==__uuidof(IUnknown)){reentered=true;ok(ts->UnadviseSink(a.Get()),"QI callback removes prior A advice");ok(ts->AdviseSink(__uuidof(ITextStoreACPSink),b.Get(),TS_AS_ALL_SINKS),"QI callback installs replacement B advice");}};
            check(ts->UnadviseSink(a.Get())==CONNECT_E_NOCONNECTION,"Outer stale unadvise cannot clear new B advice");}
        else{a->onQuery=[&](REFIID id){if(!reentered&&id==__uuidof(ITextStoreACPSink)){reentered=true;ok(ts->AdviseSink(__uuidof(ITextStoreACPSink),b.Get(),TS_AS_ALL_SINKS),"Second-QI callback installs B advice");}};
            check(ts->AdviseSink(__uuidof(ITextStoreACPSink),a.Get(),TS_AS_ALL_SINKS)==CONNECT_E_ADVISELIMIT,"Outer stale advise cannot replace new B advice");}
        ok(field.replaceFromHost({0,1},u"B"),"Replacement-advice host edit succeeds");check(b->text==1&&!a->text,"Only current advised sink receives host notification");
    }
    {
        Buffer d(u"原文");d.setSelection({{0,2},ActiveEnd::end,false});LayoutFixture l(d);auto field=std::make_unique<ProjectedTextInput>(window.handle,d,l,message,token+9);ComPtr<FakeManager>manager;manager.Attach(new FakeManager);ok(field->connect(*manager.Get(),1),"Lock-destruction connected fixture");ok(field->focus(),"Lock-destruction fixture focused");ComPtr<ITextStoreACP>retained=field->textStore();ComPtr<Sink>s;s.Attach(new Sink);ok(retained->AdviseSink(__uuidof(ITextStoreACPSink),s.Get(),TS_AS_ALL_SINKS),"Lock-destruction sink");ComPtr<ITfContextOwnerCompositionSink>c;ok(retained.As(&c),"Lock-destruction composition interface");ComPtr<FakeRange>r;r.Attach(new FakeRange(0,1));ComPtr<FakeView>v;v.Attach(new FakeView(r.Get()));
        lock(retained.Get(),s.Get(),TS_LF_READWRITE,[&]{TS_TEXTCHANGE change{};LONG a{},b{};ok(retained->InsertTextAtSelection(0,L"中",1,&a,&b,&change),"Lock-destruction provisional insertion");BOOL allow{};ok(c->OnStartComposition(v.Get(),&allow),"Lock-destruction provisional composition");check(allow,"Lock-destruction composition accepted");field.reset();check(d.text()==u"原文"&&!d.composition(),"Terminal destruction restores caller immediately");check(manager->current.Get()==manager->documents[0].Get(),"Focus changes wait until outer TSF lock returns");});
        check(manager->current.Get()==manager->documents[1].Get()&&!manager->documents[0]->context,"Outer lock unwind parks focus and releases context");
    }
    {
        Buffer d(u"x");LayoutFixture l(d);ProjectedTextInput field(window.handle,d,l,message,token+8);ComPtr<Sink>s;s.Attach(new Sink);auto*ts=field.textStore();ok(ts->AdviseSink(__uuidof(ITextStoreACPSink),s.Get(),TS_AS_ALL_SINKS),"Same-sink changed-mask fixture advises");
        s->onText=[&]{ok(ts->AdviseSink(__uuidof(ITextStoreACPSink),s.Get(),TS_AS_TEXT_CHANGE),"Text callback changes same sink subscription");};ok(field.replaceFromHost({0,1},u"y"),"Changed subscription host edit");check(s->text==1&&!s->selection,"Old selection subscription does not fire after re-advice");
    }
}
}
int main(){try{run();std::cout<<"Passed "<<checks<<" native projected TSF contracts (synthetic text, fake sinks, hidden window)\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
