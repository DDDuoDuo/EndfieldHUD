#include "native/projected_text_input.hpp"
#include "native/text_input_diagnostics.hpp"
#ifdef _WIN32
#include <ocidl.h>
#include <olectl.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace endfield::native {
namespace {
using core::text::Range;using core::text::Selection;using core::text::ActiveEnd;using core::text::Change;
constexpr unsigned textChange=unsigned(TextInputChange::text),selectionChange=unsigned(TextInputChange::selection),compositionChange=unsigned(TextInputChange::composition),layoutChange=unsigned(TextInputChange::layout);
template<class F>HRESULT protect(F&&fn)noexcept{try{return fn();}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(const std::invalid_argument&){return E_INVALIDARG;}catch(...){return E_FAIL;}}
bool sameObject(IUnknown*a,IUnknown*b){if(!a||!b)return a==b;ComPtr<IUnknown>x,y;return SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x.Get()==y.Get();}
TS_TEXTCHANGE nativeChange(Change c){return {LONG(c.start),LONG(c.oldEnd),LONG(c.newEnd)};}
std::u16string_view utf16(const WCHAR*p,ULONG length){static_assert(sizeof(WCHAR)==sizeof(char16_t));return {reinterpret_cast<const char16_t*>(p),length};}
bool validSelectionEnd(TsActiveSelEnd end){return end==TS_AE_NONE||end==TS_AE_START||end==TS_AE_END;}
}

struct ProjectedTextInput::Store final:ITextStoreACP,ITfContextOwnerCompositionSink {
    std::atomic<ULONG> refs{1};const DWORD thread{GetCurrentThreadId()};HWND owner{};UINT message{};UINT_PTR generation{};
    core::text::Document*doc{};core::text::Layout*layout{};core::text::Placement placement{};
    bool alive{true},isFocused{},posted{},pendingUpgrade{},cancelPending{};unsigned pendingChanges{};DWORD lock{},sinkMask{};
    bool deferredDetach{},restoreFocusOnDetach{};
    std::uint64_t adviceRevision{};
    ComPtr<ITextStoreACPSink>sink;ComPtr<IUnknown>sinkIdentity;
    ComPtr<ITfThreadMgr>manager;ComPtr<ITfDocumentMgr>documentManager,parkedManager,previousFocus;ComPtr<ITfContext>context;ComPtr<ITfKeystrokeMgr>keys;
    ComPtr<ITfCompositionView>compositionView;std::optional<Change>cancelledChange;
    struct Keep {Store*s;explicit Keep(Store*p):s(p){s->AddRef();}~Keep(){s->Release();}};
    Store(HWND w,core::text::Document&d,core::text::Layout&l,UINT m,UINT_PTR g):owner(w),message(m),generation(g),doc(&d),layout(&l){}
    HRESULT ready()const noexcept{return GetCurrentThreadId()!=thread?RPC_E_WRONG_THREAD:alive&&doc&&layout?S_OK:E_UNEXPECTED;}
    HRESULT read()const noexcept{const auto r=ready();return FAILED(r)?r:(lock&TS_LF_READ)?S_OK:TS_E_NOLOCK;}
    HRESULT write()const noexcept{const auto r=ready();return FAILED(r)?r:(lock&TS_LF_READWRITE)!=TS_LF_READWRITE?TS_E_NOLOCK:S_OK;}
    bool range(LONG a,LONG b)const noexcept{return a>=0&&b>=a&&std::uint32_t(b)<=doc->text().size();}
    void post(unsigned flags)noexcept{
        if(!alive)return;text_input_diagnostic_detail::postAttempt(flags==layoutChange);pendingChanges|=flags;if(!posted){posted=PostMessageW(owner,message,generation,0)!=FALSE;if(posted)text_input_diagnostic_detail::newPost();}
    }
    void notifyText(Change change)noexcept{
        Keep hold(this);auto target=sink;const auto mask=sinkMask;const auto prior=adviceRevision;
        if(target&&(mask&TS_AS_TEXT_CHANGE)){auto c=nativeChange(change);target->OnTextChange(0,&c);}
        if(alive&&target&&adviceRevision==prior&&sink.Get()==target.Get()&&(mask&TS_AS_SEL_CHANGE))target->OnSelectionChange();
    }
    void notifyLayout()noexcept{Keep hold(this);auto target=sink;if(target&&(sinkMask&TS_AS_LAYOUT_CHANGE)){text_input_diagnostic_detail::Scope diagnostic(text_input_diagnostic_detail::Operation::layout);target->OnLayoutChange(TS_LC_CHANGE,viewCookie);}}
    HRESULT rectangle(core::Rect r,RECT*out)const noexcept{
        if(r==core::Rect{}){*out={};return S_OK;}POINT origin{};if(!ClientToScreen(owner,&origin))return HRESULT_FROM_WIN32(GetLastError());
        const double left=std::floor(r.x)+origin.x,top=std::floor(r.y)+origin.y,right=std::ceil(r.x+r.width)+origin.x,bottom=std::ceil(r.y+r.height)+origin.y;
        for(double v:{left,top,right,bottom})if(!std::isfinite(v)||v<std::numeric_limits<LONG>::min()||v>std::numeric_limits<LONG>::max())return TS_E_NOLAYOUT;
        *out={LONG(left),LONG(top),LONG(right),LONG(bottom)};return S_OK;
    }
    HRESULT extent(ITfRange*input,Range&out)noexcept{
        if(!input)return E_INVALIDARG;ComPtr<ITfRangeACP>acp;auto hr=input->QueryInterface(IID_PPV_ARGS(&acp));if(FAILED(hr))return hr;
        hr=ready();if(FAILED(hr))return hr;
        LONG start{},length{};hr=acp->GetExtent(&start,&length);if(FAILED(hr))return hr;
        hr=ready();if(FAILED(hr))return hr;
        if(start<0||length<0||std::uint64_t(start)+std::uint64_t(length)>doc->text().size())return TS_E_INVALIDPOS;
        out={std::uint32_t(start),std::uint32_t(start+length)};return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(ITextStoreACP))*out=static_cast<ITextStoreACP*>(this);
        else if(iid==__uuidof(ITfContextOwnerCompositionSink))*out=static_cast<ITfContextOwnerCompositionSink*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return refs.fetch_add(1,std::memory_order_relaxed)+1;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=refs.fetch_sub(1,std::memory_order_acq_rel)-1;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE AdviseSink(REFIID iid,IUnknown*input,DWORD mask)override{
        const auto hr=ready();if(FAILED(hr))return hr;if(!input||iid!=__uuidof(ITextStoreACPSink)||mask&~TS_AS_ALL_SINKS)return E_INVALIDARG;
        Keep hold(this);ComPtr<IUnknown>identity;auto r=input->QueryInterface(IID_PPV_ARGS(&identity));if(FAILED(r))return r;r=ready();if(FAILED(r))return r;
        if(sink){if(identity.Get()!=sinkIdentity.Get())return CONNECT_E_ADVISELIMIT;sinkMask=mask;++adviceRevision;return S_OK;}
        const auto prior=adviceRevision;
        ComPtr<ITextStoreACPSink>newSink;r=input->QueryInterface(IID_PPV_ARGS(&newSink));if(FAILED(r))return r;r=ready();if(FAILED(r))return r;
        if(adviceRevision!=prior||sink)return CONNECT_E_ADVISELIMIT;
        sink=std::move(newSink);sinkIdentity=std::move(identity);sinkMask=mask;++adviceRevision;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnadviseSink(IUnknown*input)override{
        if(GetCurrentThreadId()!=thread)return RPC_E_WRONG_THREAD;if(!input)return E_INVALIDARG;
        Keep hold(this);const auto prior=adviceRevision;auto identity=sinkIdentity;if(!sink||!sameObject(input,identity.Get()))return CONNECT_E_NOCONNECTION;
        if(!alive)return E_UNEXPECTED;
        if(adviceRevision!=prior)return CONNECT_E_NOCONNECTION;
        auto oldSink=std::move(sink);auto oldIdentity=std::move(sinkIdentity);sinkMask=0;pendingUpgrade=false;++adviceRevision;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestLock(DWORD flags,HRESULT*session)override{
        text_input_diagnostic_detail::Scope diagnostic(text_input_diagnostic_detail::Operation::lock);
        if(!session)return E_INVALIDARG;*session=E_FAIL;const auto hr=ready();if(FAILED(hr))return hr;
        const auto requested=flags&TS_LF_READWRITE;
        if((flags&~(TS_LF_SYNC|TS_LF_READWRITE))||(requested!=TS_LF_READ&&requested!=TS_LF_READWRITE))return E_INVALIDARG;
        if(!sink)return E_FAIL;Keep hold(this);
        if(lock){if(flags&TS_LF_SYNC){*session=TS_E_SYNCHRONOUS;return S_OK;}
            // The mandatory reentrant read -> asynchronous read/write upgrade.
            // Other nested locks are rejected instead of growing a queue.
            if(lock==TS_LF_READ&&requested==TS_LF_READWRITE){pendingUpgrade=true;*session=TS_S_ASYNC;return S_OK;}
            return E_FAIL;
        }
        auto target=sink;
        if(requested==TS_LF_READWRITE){const auto begin=protect([&]{doc->beginInputTransaction();return S_OK;});if(FAILED(begin))return begin;}
        lock=requested;*session=target->OnLockGranted(flags);lock=0;if(deferredDetach)finishDetach();if(alive&&requested==TS_LF_READWRITE)doc->endInputTransaction();
        if(alive&&pendingUpgrade&&sink){pendingUpgrade=false;target=sink;const auto begin=protect([&]{doc->beginInputTransaction();return S_OK;});
            if(FAILED(begin))return begin;lock=TS_LF_READWRITE;target->OnLockGranted(TS_LF_READWRITE);lock=0;if(deferredDetach)finishDetach();if(alive)doc->endInputTransaction();}
        pendingUpgrade=false;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStatus(TS_STATUS*out)override{const auto hr=ready();if(FAILED(hr))return hr;if(!out)return E_INVALIDARG;*out={DWORD(doc->readOnly()?TS_SD_READONLY:0),TS_SS_NOHIDDENTEXT};return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryInsert(LONG a,LONG b,ULONG length,LONG*outStart,LONG*outEnd)override{
        const auto hr=ready();if(FAILED(hr))return hr;if(!outStart||!outEnd||!range(a,b))return E_INVALIDARG;
        if(doc->readOnly())return TS_E_READONLY;if(length>doc->maximumUnits()-(doc->text().size()-std::uint32_t(b-a)))return E_INVALIDARG;
        // Reports the validated existing replacement range, always inside the
        // document. InsertTextAtSelection returns the actual inserted extent.
        *outStart=a;*outEnd=b;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSelection(ULONG index,ULONG count,TS_SELECTION_ACP*out,ULONG*fetched)override{
        const auto hr=read();if(FAILED(hr))return hr;if(!fetched||(!out&&count)||(index!=0&&index!=TS_DEFAULT_SELECTION))return E_INVALIDARG;
        *fetched=0;if(!count)return S_OK;const auto s=doc->selection();out[0]={LONG(s.range.start),LONG(s.range.end),{s.activeEnd==ActiveEnd::none?TS_AE_NONE:s.activeEnd==ActiveEnd::start?TS_AE_START:TS_AE_END,s.interim?TRUE:FALSE}};*fetched=1;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSelection(ULONG count,const TS_SELECTION_ACP*input)override{
        const auto hr=write();if(FAILED(hr))return hr;if(count!=1||!input||!range(input->acpStart,input->acpEnd)||!validSelectionEnd(input->style.ase)||(input->style.fInterimChar&&input->style.ase!=TS_AE_NONE))return E_INVALIDARG;
        return protect([&]{doc->setSelection({{std::uint32_t(input->acpStart),std::uint32_t(input->acpEnd)},input->style.ase==TS_AE_NONE?ActiveEnd::none:input->style.ase==TS_AE_START?ActiveEnd::start:ActiveEnd::end,input->style.fInterimChar!=FALSE});post(selectionChange);return S_OK;});
    }
    HRESULT STDMETHODCALLTYPE GetText(LONG a,LONG b,WCHAR*plain,ULONG plainCapacity,ULONG*plainCount,TS_RUNINFO*runs,ULONG runCapacity,ULONG*runCount,LONG*next)override{
        const auto hr=read();if(FAILED(hr))return hr;
        if(!plainCount||!runCount||!next||(plainCapacity&&!plain)||(runCapacity&&!runs))return E_INVALIDARG;
        *plainCount=0;*runCount=0;*next=a;if(b==-1)b=LONG(doc->text().size());if(!range(a,b))return TS_E_INVALIDPOS;
        const auto available=ULONG(b-a),count=plainCapacity?std::min(available,plainCapacity):runCapacity?available:0u;
        if(plainCapacity){std::memcpy(plain,doc->text().data()+a,std::size_t(count)*sizeof(WCHAR));*plainCount=count;}
        if(runCapacity&&count){runs[0]={count,TS_RT_PLAIN};*runCount=1;}
        *next=a+LONG(count);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetText(DWORD flags,LONG a,LONG b,const WCHAR*text,ULONG length,TS_TEXTCHANGE*out)override{
        const auto hr=write();if(FAILED(hr))return hr;if(doc->readOnly())return TS_E_READONLY;if((flags&~TS_ST_CORRECTION)||!out||(!text&&length))return E_INVALIDARG;if(!range(a,b))return TS_E_INVALIDPOS;
        if(length>doc->maximumUnits()-(doc->text().size()-std::uint32_t(b-a)))return E_INVALIDARG;
        return protect([&]{const auto change=doc->replace({std::uint32_t(a),std::uint32_t(b)},utf16(text,length));*out=nativeChange(change);post(textChange|selectionChange|layoutChange);return S_OK;});
    }
    HRESULT STDMETHODCALLTYPE GetFormattedText(LONG,LONG,IDataObject**out)override{const auto hr=read();if(FAILED(hr))return hr;if(!out)return E_INVALIDARG;*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetEmbedded(LONG,REFGUID,REFIID,IUnknown**out)override{const auto hr=read();if(FAILED(hr))return hr;if(!out)return E_INVALIDARG;*out=nullptr;return TS_E_NOOBJECT;}
    HRESULT STDMETHODCALLTYPE QueryInsertEmbedded(const GUID*,const FORMATETC*,BOOL*out)override{const auto hr=ready();if(FAILED(hr))return hr;if(!out)return E_INVALIDARG;*out=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE InsertEmbedded(DWORD,LONG,LONG,IDataObject*,TS_TEXTCHANGE*)override{const auto hr=write();return FAILED(hr)?hr:E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE InsertTextAtSelection(DWORD flags,const WCHAR*text,ULONG length,LONG*outStart,LONG*outEnd,TS_TEXTCHANGE*out)override{
        const auto hr=write();if(FAILED(hr))return hr;if(doc->readOnly())return TS_E_READONLY;
        if(flags&~(TS_IAS_NOQUERY|TS_IAS_QUERYONLY)||(flags&(TS_IAS_NOQUERY|TS_IAS_QUERYONLY))==(TS_IAS_NOQUERY|TS_IAS_QUERYONLY)||(!text&&length)||(!(flags&TS_IAS_NOQUERY)&&(!outStart||!outEnd))||(!(flags&TS_IAS_QUERYONLY)&&!out))return E_INVALIDARG;
        const auto s=doc->selection().range;
        if(length>doc->maximumUnits()-(doc->text().size()-(s.end-s.start)))return E_INVALIDARG;
        if(flags&TS_IAS_QUERYONLY){*outStart=LONG(s.start);*outEnd=LONG(s.end);return S_OK;}
        return protect([&]{const auto c=doc->replace(s,utf16(text,length));*out=nativeChange(c);if(!(flags&TS_IAS_NOQUERY)){*outStart=LONG(c.start);*outEnd=LONG(c.newEnd);}post(textChange|selectionChange|layoutChange);return S_OK;});
    }
    HRESULT STDMETHODCALLTYPE InsertEmbeddedAtSelection(DWORD,IDataObject*,LONG*,LONG*,TS_TEXTCHANGE*)override{const auto hr=write();return FAILED(hr)?hr:E_NOTIMPL;}
    HRESULT attrs(ULONG count,const TS_ATTRID*input){const auto hr=read();return FAILED(hr)?hr:count&&!input?E_INVALIDARG:S_OK;}
    HRESULT STDMETHODCALLTYPE RequestSupportedAttrs(DWORD,ULONG count,const TS_ATTRID*input)override{const auto hr=ready();return FAILED(hr)?hr:count&&!input?E_INVALIDARG:S_OK;}
    HRESULT STDMETHODCALLTYPE RequestAttrsAtPosition(LONG at,ULONG count,const TS_ATTRID*input,DWORD)override{const auto hr=attrs(count,input);return FAILED(hr)?hr:range(at,at)?S_OK:TS_E_INVALIDPOS;}
    HRESULT STDMETHODCALLTYPE RequestAttrsTransitioningAtPosition(LONG at,ULONG count,const TS_ATTRID*input,DWORD)override{const auto hr=attrs(count,input);return FAILED(hr)?hr:range(at,at)?S_OK:TS_E_INVALIDPOS;}
    HRESULT STDMETHODCALLTYPE FindNextAttrTransition(LONG a,LONG b,ULONG count,const TS_ATTRID*input,DWORD,LONG*next,BOOL*found,LONG*offset)override{
        const auto hr=attrs(count,input);if(FAILED(hr))return hr;if(!next||!found||!offset||!range(std::min(a,b),std::max(a,b)))return E_INVALIDARG;*next=b;*found=FALSE;*offset=0;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RetrieveRequestedAttrs(ULONG count,TS_ATTRVAL*out,ULONG*fetched)override{const auto hr=read();if(FAILED(hr))return hr;if(!fetched||(count&&!out))return E_INVALIDARG;*fetched=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetEndACP(LONG*out)override{const auto hr=read();if(FAILED(hr))return hr;if(!out)return E_INVALIDARG;*out=LONG(doc->text().size());return S_OK;}
    HRESULT STDMETHODCALLTYPE GetActiveView(TsViewCookie*out)override{const auto hr=ready();if(FAILED(hr))return hr;if(!out)return E_INVALIDARG;*out=viewCookie;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetACPFromPoint(TsViewCookie cookie,const POINT*screen,DWORD flags,LONG*out)override{
        const auto hr=read();if(FAILED(hr))return hr;if(cookie!=viewCookie||!screen||!out||flags&~(GXFPF_ROUND_NEAREST|GXFPF_NEAREST))return E_INVALIDARG;
        if(layout->textRevision()!=doc->revision())return TS_E_NOLAYOUT;POINT p=*screen;if(!ScreenToClient(owner,&p))return HRESULT_FROM_WIN32(GetLastError());
        return protect([&]{const auto at=core::text::projectedHit(*doc,*layout,{double(p.x),double(p.y)},placement,(flags&GXFPF_NEAREST)!=0,(flags&GXFPF_ROUND_NEAREST)!=0);if(!at)return TS_E_INVALIDPOINT;*out=LONG(*at);return S_OK;});
    }
    HRESULT STDMETHODCALLTYPE GetTextExt(TsViewCookie cookie,LONG a,LONG b,RECT*out,BOOL*clipped)override{
        text_input_diagnostic_detail::Scope diagnostic(text_input_diagnostic_detail::Operation::extent);
        const auto hr=read();if(FAILED(hr))return hr;if(cookie!=viewCookie||!out||!clipped)return E_INVALIDARG;*out={};*clipped=FALSE;if(!range(a,b))return TS_E_INVALIDPOS;
        if(IsIconic(owner)||!placement.visible){*clipped=TRUE;return S_OK;}
        // Zero-length caret queries use the host's caret bounds, just as the
        // projected Mac adapter does. Nonempty ranges use the same layout.
        return protect([&]{const auto r=core::text::projectedRange(*doc,*layout,{std::uint32_t(a),std::uint32_t(b)},placement);if(!r)return TS_E_NOLAYOUT;*clipped=r->clipped?TRUE:FALSE;return rectangle(r->clientBounds,out);});
    }
    HRESULT STDMETHODCALLTYPE GetScreenExt(TsViewCookie cookie,RECT*out)override{const auto hr=ready();if(FAILED(hr))return hr;if(cookie!=viewCookie||!out)return E_INVALIDARG;*out={};if(IsIconic(owner)||!placement.visible)return S_OK;const auto r=core::text::projectedViewport(placement);return r?rectangle(*r,out):TS_E_NOLAYOUT;}
    HRESULT STDMETHODCALLTYPE GetWnd(TsViewCookie cookie,HWND*out)override{const auto hr=ready();if(FAILED(hr))return hr;if(cookie!=viewCookie||!out)return E_INVALIDARG;*out=owner;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStartComposition(ITfCompositionView*view,BOOL*accepted)override{
        const auto hr=ready();if(FAILED(hr))return hr;if(!view||!accepted)return E_INVALIDARG;*accepted=FALSE;if(compositionView||doc->readOnly())return S_OK;
        Keep hold(this);ComPtr<ITfRange>r;auto result=view->GetRange(&r);if(FAILED(result))return result;result=ready();if(FAILED(result))return result;Range rangeValue;result=extent(r.Get(),rangeValue);if(FAILED(result))return result;
        return protect([&]{doc->beginComposition(rangeValue);compositionView=view;*accepted=TRUE;post(compositionChange);return S_OK;});
    }
    HRESULT STDMETHODCALLTYPE OnUpdateComposition(ITfCompositionView*view,ITfRange*newRange)override{
        const auto hr=ready();if(FAILED(hr))return hr;Keep hold(this);auto previous=compositionView;if(!view||!sameObject(view,previous.Get()))return E_INVALIDARG;
        const auto current=ready();if(FAILED(current))return current;
        if(!newRange)return S_OK;Range next;auto result=extent(newRange,next);if(FAILED(result))return result;
        return protect([&]{doc->updateComposition(next);post(compositionChange);return S_OK;});
    }
    HRESULT STDMETHODCALLTYPE OnEndComposition(ITfCompositionView*view)override{
        const auto hr=ready();if(FAILED(hr))return hr;Keep hold(this);auto previous=compositionView;if(!view||!sameObject(view,previous.Get()))return E_INVALIDARG;
        const auto current=ready();if(FAILED(current))return current;
        // TerminateComposition may call this while holding its own synchronous
        // write lock. Host rollback must wait until that call/lock returns;
        // do not change text/styles here in response to the TSF callback.
        if(cancelPending){compositionView.Reset();post(compositionChange);return S_OK;}
        return protect([&]{doc->endComposition(false);compositionView.Reset();post(compositionChange);return S_OK;});
    }
    HRESULT cancel()noexcept{
        const auto hr=ready();if(FAILED(hr))return hr;if(lock)return TS_E_NOLOCK;if(cancelPending)return E_UNEXPECTED;if(!doc->composition())return S_FALSE;Keep hold(this);cancelPending=true;cancelledChange.reset();
        auto ownedContext=context;auto ownedView=compositionView;
        if(ownedContext&&ownedView){ComPtr<ITfContextOwnerCompositionServices>service;auto result=ownedContext.As(&service);if(SUCCEEDED(result)&&alive)result=service->TerminateComposition(ownedView.Get());
            if(!alive)return E_UNEXPECTED;if(FAILED(result)){cancelPending=false;return result;}}
        // An isolated fake composition has no context. Termination must have
        // finished synchronously before restoring host text/formatting.
        if(doc&&doc->composition()){auto result=protect([&]{cancelledChange=doc->endComposition(true);compositionView.Reset();return S_OK;});if(FAILED(result)){cancelPending=false;return result;}}
        cancelPending=false;if(cancelledChange){const auto change=*cancelledChange;cancelledChange.reset();notifyText(change);}post(textChange|selectionChange|compositionChange|layoutChange);return S_OK;
    }
    HRESULT connect(ITfThreadMgr&input,TfClientId client)noexcept{
        const auto hr=ready();if(FAILED(hr))return hr;if(lock||manager||client==TF_CLIENTID_NULL)return E_INVALIDARG;
        Keep hold(this);ComPtr<ITfThreadMgr>ownedManager=&input;
        ComPtr<ITfDocumentMgr>newDocument,parked;ComPtr<ITfContext>newContext;ComPtr<ITfKeystrokeMgr>newKeys;TfEditCookie cookie{};
        auto result=input.CreateDocumentMgr(&newDocument);if(FAILED(result))return result;if(!alive)return E_UNEXPECTED;
        result=newDocument->CreateContext(client,0,static_cast<ITextStoreACP*>(this),&newContext,&cookie);if(FAILED(result))return result;if(!alive)return E_UNEXPECTED;
        result=newDocument->Push(newContext.Get());if(FAILED(result))return result;if(!alive){newDocument->Pop(TF_POPF_ALL);return E_UNEXPECTED;}
        result=input.CreateDocumentMgr(&parked);if(FAILED(result)||!alive){newDocument->Pop(TF_POPF_ALL);return FAILED(result)?result:E_UNEXPECTED;}
        result=input.QueryInterface(IID_PPV_ARGS(&newKeys));if(FAILED(result)||!alive){newDocument->Pop(TF_POPF_ALL);return FAILED(result)?result:E_UNEXPECTED;}
        manager=&input;documentManager=std::move(newDocument);parkedManager=std::move(parked);context=std::move(newContext);keys=std::move(newKeys);return S_OK;
    }
    HRESULT focus()noexcept{
        const auto hr=ready();if(FAILED(hr))return hr;if(lock)return TS_E_NOLOCK;if(!manager)return TF_E_DISCONNECTED;if(isFocused)return S_FALSE;
        Keep hold(this);auto ownedManager=manager;auto ownedDocument=documentManager;ComPtr<ITfDocumentMgr>previous;auto result=ownedManager->GetFocus(&previous);if(FAILED(result))return result;if(!alive)return E_UNEXPECTED;
        // Publish our intended focus before SetFocus can synchronously call
        // back into teardown; stop/blur can then restore the previous document.
        previousFocus=std::move(previous);isFocused=true;result=ownedManager->SetFocus(ownedDocument.Get());if(!alive)return E_UNEXPECTED;
        if(FAILED(result)){previousFocus.Reset();isFocused=false;return result;}return S_OK;
    }
    HRESULT blur(bool cancelComposition)noexcept{
        const auto hr=ready();if(FAILED(hr))return hr;if(lock)return TS_E_NOLOCK;Keep hold(this);
        if(cancelComposition){auto result=cancel();if(FAILED(result))return result;}
        if(!alive)return E_UNEXPECTED;if(!isFocused)return S_FALSE;auto ownedManager=manager;auto ownedDocument=documentManager;auto destination=previousFocus?previousFocus:parkedManager;ComPtr<ITfDocumentMgr>current;auto result=ownedManager->GetFocus(&current);if(FAILED(result))return result;if(!alive)return E_UNEXPECTED;
        const auto same=sameObject(current.Get(),ownedDocument.Get());if(!alive)return E_UNEXPECTED;
        if(same){result=ownedManager->SetFocus(destination.Get());if(FAILED(result))return result;if(!alive)return E_UNEXPECTED;}
        previousFocus.Reset();isFocused=false;return S_OK;
    }
    HRESULT stop()noexcept{
        if(GetCurrentThreadId()!=thread)return RPC_E_WRONG_THREAD;if(!alive)return S_FALSE;if(lock)return TS_E_NOLOCK;Keep hold(this);
        auto result=blur(true);if(FAILED(result))return result;detach();return S_OK;
    }
    void detach()noexcept{
        // Terminal teardown also protects retained COM references and stale
        // owner messages if the facade is destroyed during a sink callback.
        if(!alive)return;restoreFocusOnDetach=isFocused;alive=false;isFocused=false;pendingChanges=0;posted=false;pendingUpgrade=false;
        // Terminal destruction may occur reentrantly inside a TSF lock. Stop
        // exposing the store first, then restore the caller-owned composition
        // snapshot without sink notifications before abandoning host pointers.
        if(doc){protect([&]{if(doc->composition())doc->endComposition(true);doc->endInputTransaction();return S_OK;});}
        doc=nullptr;layout=nullptr;owner=nullptr;
        // A facade can be destroyed during OnLockGranted. Invalidate host
        // access immediately, but defer manager focus changes/context Pop until
        // that lock unwinds; changing TSF focus inside its write lock is unsafe.
        if(lock){deferredDetach=true;return;}finishDetach();
    }
    void finishDetach()noexcept{
        deferredDetach=false;auto ownedManager=manager;auto ownedDocument=documentManager;auto destination=previousFocus?previousFocus:parkedManager;
        if(restoreFocusOnDetach&&ownedManager&&ownedDocument&&destination){ComPtr<ITfDocumentMgr>current;const auto result=ownedManager->GetFocus(&current);
            if(SUCCEEDED(result)&&sameObject(current.Get(),ownedDocument.Get()))ownedManager->SetFocus(destination.Get());}
        restoreFocusOnDetach=false;
        if(documentManager)documentManager->Pop(TF_POPF_ALL);compositionView.Reset();sink.Reset();sinkIdentity.Reset();context.Reset();keys.Reset();documentManager.Reset();parkedManager.Reset();previousFocus.Reset();manager.Reset();
    }
};

ProjectedTextInput::ProjectedTextInput(HWND owner,core::text::Document&doc,core::text::Layout&layout,UINT message,UINT_PTR generation){
    if(!IsWindow(owner)||GetWindowThreadProcessId(owner,nullptr)!=GetCurrentThreadId()||message<WM_APP||message>0xbfffu||!generation||doc.text().size()>core::text::Buffer::safetyMaximumUnits||!doc.maximumUnits()||doc.maximumUnits()>core::text::Buffer::safetyMaximumUnits||doc.maximumUnits()<doc.text().size()||!core::text::Buffer::validUTF16(doc.text()))throw std::invalid_argument("Invalid projected text owner/model/message lifetime");
    store_=new Store(owner,doc,layout,message,generation);
}
ProjectedTextInput::~ProjectedTextInput(){if(store_){if(GetCurrentThreadId()!=store_->thread)std::terminate();const auto result=store_->stop();if(FAILED(result))store_->detach();store_->Release();}}
ITextStoreACP*ProjectedTextInput::textStore()const noexcept{return store_;}
HRESULT ProjectedTextInput::connect(ITfThreadMgr&manager,TfClientId client)noexcept{return store_->connect(manager,client);}
HRESULT ProjectedTextInput::focus()noexcept{return store_->focus();}
HRESULT ProjectedTextInput::blur(bool cancel)noexcept{return store_->blur(cancel);}
HRESULT ProjectedTextInput::stop()noexcept{return store_->stop();}
bool ProjectedTextInput::focused()const noexcept{return GetCurrentThreadId()==store_->thread&&store_->isFocused;}
HRESULT ProjectedTextInput::cancelComposition()noexcept{return store_->cancel();}
HRESULT ProjectedTextInput::replaceFromHost(Range range,std::u16string_view text)noexcept{
    auto*s=store_;const auto hr=s->ready();if(FAILED(hr))return hr;if(s->lock||s->doc->composition())return TS_E_NOLOCK;if(s->doc->readOnly())return TS_E_READONLY;
    Store::Keep hold(s);return protect([&]{const auto change=s->doc->replace(range,text);s->post(textChange|selectionChange|layoutChange);s->notifyText(change);return S_OK;});
}
HRESULT ProjectedTextInput::selectFromHost(Selection selection)noexcept{
    auto*s=store_;const auto hr=s->ready();if(FAILED(hr))return hr;if(s->lock||s->doc->composition())return TS_E_NOLOCK;
    Store::Keep hold(s);return protect([&]{s->doc->setSelection(selection);s->post(selectionChange);auto target=s->sink;if(target&&(s->sinkMask&TS_AS_SEL_CHANGE))target->OnSelectionChange();return S_OK;});
}
HRESULT ProjectedTextInput::setPlacement(const core::text::Placement&value)noexcept{
    const auto hr=store_->ready();if(FAILED(hr))return hr;if(!core::text::validPlacement(value))return E_INVALIDARG;if(store_->lock)return TS_E_NOLOCK;if(store_->placement==value){text_input_diagnostic_detail::placement(false);return S_FALSE;}
    text_input_diagnostic_detail::placement(true);
    // Placement is output from the owner's current render. Notify TSF so its
    // candidate geometry follows the text, but do not echo a work message back
    // to that owner: it would invalidate another frame for every moving pose.
    Store::Keep hold(store_);store_->placement=value;if(store_->isFocused)store_->notifyLayout();return S_OK;
}
HRESULT ProjectedTextInput::layoutChanged()noexcept{const auto hr=store_->ready();if(FAILED(hr))return hr;if(store_->lock)return TS_E_NOLOCK;if(store_->layout->textRevision()!=store_->doc->revision())return TS_E_NOLAYOUT;store_->notifyLayout();return S_OK;}
bool ProjectedTextInput::filterKeyMessage(UINT message,WPARAM key,LPARAM data)noexcept{
    auto*s=store_;if(FAILED(s->ready())||!s->isFocused||!s->keys)return false;Store::Keep hold(s);auto keys=s->keys;const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN,up=message==WM_KEYUP||message==WM_SYSKEYUP;if(!down&&!up)return false;
    BOOL tested{},eaten{};auto hr=down?keys->TestKeyDown(key,data,&tested):keys->TestKeyUp(key,data,&tested);if(FAILED(hr)||!tested||!s->alive||!s->isFocused)return false;
    hr=down?keys->KeyDown(key,data,&eaten):keys->KeyUp(key,data,&eaten);return SUCCEEDED(hr)&&eaten;
}
unsigned ProjectedTextInput::takeChanges(UINT_PTR token)noexcept{if(FAILED(store_->ready())||token!=store_->generation)return 0;const auto flags=store_->pendingChanges;store_->pendingChanges=0;store_->posted=false;return flags;}
} // namespace endfield::native
#endif
