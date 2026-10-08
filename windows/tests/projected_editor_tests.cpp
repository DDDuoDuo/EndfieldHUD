#include "native/projected_editor.hpp"
#ifdef _WIN32
#include <ocidl.h>
#include <objbase.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using namespace endfield::native;
using namespace endfield::core;
using namespace endfield::core::text;
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
unsigned checks{};
void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
void ok(HRESULT hr,const char*why){check(SUCCEEDED(hr),why);}
template<class F>void rejects(F f,const char*why){bool bad{};try{f();}catch(const std::exception&){bad=true;}check(bad,why);}
// Reused synthetic COM protocol fixtures; no real TSF activation/profile/input.
struct Sink final:ITextStoreACPSink {
    std::atomic<ULONG>refs{1};unsigned text{},selection{},layout{},locks{};std::vector<DWORD>granted;
    TS_TEXTCHANGE last{};std::function<HRESULT(DWORD)>onLock;std::function<void()>onText;std::function<void(REFIID)>onQuery;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;auto callback=onQuery;if(callback)callback(id);if(id!=__uuidof(IUnknown)&&id!=__uuidof(ITextStoreACPSink))return E_NOINTERFACE;*out=static_cast<ITextStoreACPSink*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto r=--refs;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE OnTextChange(DWORD,const TS_TEXTCHANGE*c)override{++text;last=*c;if(onText)onText();return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSelectionChange()override{++selection;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLayoutChange(TsLayoutCode code,TsViewCookie cookie)override{++layout;return code==TS_LC_CHANGE&&cookie==ProjectedTextInput::viewCookie?S_OK:E_FAIL;}
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
    HWND hwnd{};ATOM atom{};
    Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldProjectedEditorOwnedTest";atom=RegisterClassW(&c);check(atom!=0,"Owned editor test class creates");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Hidden synthetic editor",WS_POPUP,17,23,512,256,nullptr,nullptr,c.hInstance,nullptr);check(hwnd!=nullptr,"Owned hidden editor window creates");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}
};
void locked(ITextStoreACP*store,Sink*sink,std::function<void()>f){sink->onLock=[&](DWORD){f();return S_OK;};HRESULT session{};ok(store->RequestLock(TS_LF_READWRITE,&session),"Fake TSF requests write lock");ok(session,"Fake TSF lock callback succeeds");sink->onLock={};}
struct PaintedMetrics {
    DWRITE_LINE_SPACING_METHOD method{};FLOAT spacing{},baseline{};
    std::vector<DWRITE_LINE_METRICS> lines;Rect caret;
};
PaintedMetrics readPaintedMetrics(const LayerTextLayout& layout){
    // Test-only inspection of the already retained COM object's identity. The
    // strong painted handle and this fixture's UI apartment keep it alive; no
    // second layout, production diagnostic API or font measurement is created.
    const auto painted=layout.painted();auto* native=reinterpret_cast<IDWriteTextLayout*>(painted->layoutIdentity());
    PaintedMetrics result;
    ok(native->GetLineSpacing(&result.method,&result.spacing,&result.baseline),"Read spacing from the actual painted layout");
    UINT32 count{};const auto countResult=native->GetLineMetrics(nullptr,0,&count);
    check((SUCCEEDED(countResult)||countResult==HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))&&count>0&&count<=painted->text().size()+1,"Painted line count is bounded by its text");
    result.lines.resize(count);ok(native->GetLineMetrics(result.lines.data(),count,&count),"Read the actual painted line metrics");
    check(count==result.lines.size(),"Painted line count remains stable during inspection");
    FLOAT hitX{},hitY{};DWRITE_HIT_TEST_METRICS hit{};ok(native->HitTestTextPosition(0,FALSE,&hitX,&hitY,&hit),"Read actual painted caret hit region");
    result.caret={hitX,hitY,1,std::max(1.,double(hit.height))};return result;
}
void run(Renderer&renderer,HWND hwnd){
    LayerRasterizer raster;LayerScene scene(raster);Buffer doc(u"Ae\u0301😀B",1024);ProjectedEditorStyle style;style.width=300;style.height=110;style.fontSize=20;style.textColor={.8,.9,.7,1};style.caretColor=style.textColor;LayerRasterOptions options;options.pixelsPerPoint=1;
    auto editor=std::make_unique<NativeProjectedEditor>(hwnd,doc,scene,style,options,PlainEditorFixtureCapacity{64},WM_APP+121,91);
    check(scene.draws().size()==4&&scene.report().unsupported.empty(),"Editor contributes four supported retained leaves to caller composition");
    check(editor->layout().painted()==scene.paintedTextLayout("projected-editor-glyphs")&&editor->layout().painted()->text()==doc.text(),"Editor/TSF layout is exactly the painted leaf handle");
    check(!scene.paintedTextLayout("projected-editor-selection")&&!scene.paintedTextLayout("missing"),"Readonly scene handle rejects nontext and missing surfaces");
    ComPtr<FakeManager> manager;manager.Attach(new FakeManager);ok(editor->connect(*manager.Get(),7),"Editor borrows caller's fake already-activated manager");ok(editor->focus(true),"Caller explicitly assigns focus");
    ComPtr<Sink> sink;sink.Attach(new Sink);auto* store=editor->textStore();ok(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_TEXT_CHANGE|TS_AS_SEL_CHANGE|TS_AS_LAYOUT_CHANGE),"Fake sink observes isolated document changes");
    ProjectedEditorPose pose;pose.localToScreen=Matrix4::translation(30,40);pose.screenToClip=layerViewportProjection(512,256);pose.pixelWidth=512;pose.pixelHeight=256;pose.ownerFocused=true;
    editor->setPose(pose);LayerComposition composition;const std::array scenes{&scene};composition.setScenes(renderer,scenes);renderer.setCamera(pose.screenToClip);composition.present(renderer);renderer.draw(false);
    const auto before=raster.stats();const auto gpu=renderer.stats();const auto handle=editor->layout().painted()->layoutIdentity();allocations=0;counting=true;
    try{for(unsigned n=0;n<120;++n){pose.localToScreen.values[3]=n*.000001;pose.localToScreen.values[7]=-n*.0000004;editor->setPose(pose);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"120 editor pointer/tilt frames allocate no CPU storage");check(raster.stats().rasterizations==before.rasterizations&&raster.stats().textLayoutsCreated==before.textLayoutsCreated&&renderer.stats().textureUploads==gpu.textureUploads,"Tilt changes no raster, DWrite layout or texture upload");
    auto sync=[&]{editor->syncContent();editor->setPose(pose);composition.upload(renderer);composition.present(renderer);};
    const auto first=editor->layout().selectionRectangles({0,1}).front();const auto hit=editor->placement().projection.project({first.x+first.width*.1,first.y+first.height*.5});check(hit.has_value(),"Synthetic glyph projects through shared plane");
    const auto click=editor->pointerDown(*hit);check(click.handled&&doc.selection().range==Range{0,0},"Pointer selects the exact painted cluster");editor->pointerUp();sync();
    editor->command(ProjectedEditorCommand::right);check(doc.selection().range==Range{1,1},"Right moves to next original DWrite cluster");sync();editor->command(ProjectedEditorCommand::right);check(doc.selection().range==Range{3,3},"Combining accent is not split by keyboard navigation");sync();
    editor->command(ProjectedEditorCommand::backspace);check(doc.text()==u"A😀B","Backspace removes one painted combining cluster");sync();check(editor->layout().painted()->layoutIdentity()!=handle&&editor->layout().painted()->text()==doc.text(),"Text edit replaces only its actual painted layout");
    editor->command(ProjectedEditorCommand::documentEnd);sync();editor->character(0xd83d);check(doc.text()==u"A😀B","Lone high surrogate is held without corrupting caller text");editor->character(0xde00);sync();check(doc.text()==u"A😀B😀","WM_CHAR surrogate pair reaches caller document together");
    editor->character(0xdc00);check(doc.text()==u"A😀B😀","Unpaired low surrogate is discarded");
    const auto unchanged=doc.text();LONG start{},end{};check(store->QueryInsert(0,0,65,&start,&end)==E_INVALIDARG,"TSF capacity guard rejects whole excess insertion without truncating");check(doc.text()==unchanged&&doc.maximumUnits()==1024,"Explicit field capacity never changes caller storage limit");
    editor->command(ProjectedEditorCommand::selectAll);sync();const auto layoutCount=raster.stats().textLayoutsCreated;editor->command(ProjectedEditorCommand::left);sync();check(raster.stats().textLayoutsCreated==layoutCount,"Selection/caret updates do not create another DWrite layout");
    // Real TSF protocol with fake context: composition provisional text precedes
    // OnStartComposition in the same write transaction, matching actual IMEs.
    ComPtr<ITfContextOwnerCompositionSink> owner;ok(store->QueryInterface(IID_PPV_ARGS(&owner)),"Fake composition owner interface exists");ComPtr<FakeRange> range;range.Attach(new FakeRange(0,1));ComPtr<FakeView> view;view.Attach(new FakeView(range.Get()));
    const auto saved=std::u16string(doc.text());locked(store,sink.Get(),[&]{TS_TEXTCHANGE change{};LONG a{},b{};ok(store->InsertTextAtSelection(0,L"预",1,&a,&b,&change),"Fake IME inserts provisional text");BOOL accepted{};ok(owner->OnStartComposition(view.Get(),&accepted),"Fake IME announces composition");check(accepted!=FALSE,"Provisional composition accepted");});sync();
    manager->documents[0]->context->terminate=[&](ITfCompositionView*v){return owner->OnEndComposition(v);};
    check(editor->filterKeyMessage(WM_KEYDOWN,VK_ESCAPE,0)&&manager->keyCalls==1&&doc.composition().has_value(),"IME consumes Escape before host finish/cancel routing");
    const auto cancel=editor->command(ProjectedEditorCommand::finish);check(cancel.handled&&cancel.changed&&!cancel.finishRequested&&!doc.composition()&&doc.text()==saved,"Unconsumed Escape rolls back marked text only");sync();
    const auto finish=editor->command(ProjectedEditorCommand::finish);check(finish.finishRequested&&doc.text()==saved,"Unmarked finish returns caller request without saving or closing");
    const auto previousLayout=editor->layout().painted();const auto oldCaret=editor->layout().bounds({0,0})->bounds;const auto unchangedText=std::u16string(doc.text());
    auto spaced=style;spaced.lineHeight=40;spaced.baseline=25;check(editor->setStyle(spaced),"Caller can supply exact measured Notes line metrics");sync();
    check(editor->layout().painted()!=previousLayout&&editor->layout().painted()==scene.paintedTextLayout("projected-editor-glyphs")&&editor->layout().textRevision()==doc.revision(),"Style change binds newly painted leaf without a second layout");
    const auto paintedMetrics=readPaintedMetrics(editor->layout());const auto spacedCaret=editor->layout().bounds({0,0})->bounds;
    check(paintedMetrics.method==DWRITE_LINE_SPACING_METHOD_UNIFORM&&paintedMetrics.spacing==40&&paintedMetrics.baseline==25&&paintedMetrics.lines.size()==1&&paintedMetrics.lines[0].height==40&&paintedMetrics.lines[0].baseline==25,"Caller line height and baseline reach the actual painted DirectWrite layout");
    check(spacedCaret==paintedMetrics.caret&&spacedCaret!=oldCaret&&doc.text()==unchangedText,"Caret follows the actual glyph hit region at its new baseline without changing caller text");
    auto invalidStyle=spaced;invalidStyle.baseline=41;rejects([&]{editor->setStyle(invalidStyle);},"Baseline outside line height rejects before content mutation");
    invalidStyle=spaced;invalidStyle.lineHeight=0;rejects([&]{editor->setStyle(invalidStyle);},"Partial default line metrics reject instead of silently mixing spacing");
    check(editor->layout().painted()==scene.paintedTextLayout("projected-editor-glyphs")&&editor->layout().bounds({0,0})->bounds==spacedCaret,"Rejected metric changes retain current painted layout and caret geometry");
    const auto roundHandle=editor->layout().painted();const auto roundRaster=raster.stats();const auto roundGPU=renderer.stats();
    auto roundedStyle=spaced;roundedStyle.cornerRadius=3;check(editor->setStyle(roundedStyle)&&!editor->syncContent(),"Source corner radius updates only projection and GPU clipping");editor->setPose(pose);composition.present(renderer);
    check(editor->placement().cornerRadius==3&&editor->layout().painted()==roundHandle,"Rounded editor retains its exact rectangular painted layout");
    for(const auto&draw:scene.draws())check(draw.masks.size()==1&&draw.masks[0].cornerRadius==3,"Glyph/caret/selection/composition share source-radius ancestor mask");
    const auto roundCorner=editor->placement().projection.project({.1,.1});check(roundCorner&&!editor->pointerDown(*roundCorner).handled,"Rounded viewport corner cannot select visually clipped text");
    allocations=0;counting=true;try{for(unsigned n=0;n<120;++n){pose.localToScreen.values[3]=n*.000001;pose.localToScreen.values[7]=-n*.0000004;editor->setPose(pose);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==roundRaster.rasterizations&&raster.stats().textLayoutsCreated==roundRaster.textLayoutsCreated&&renderer.stats().textureUploads==roundGPU.textureUploads,"Rounded tilt frames neither allocate nor rerasterize or upload textures");
    invalidStyle=roundedStyle;invalidStyle.cornerRadius=56;rejects([&]{editor->setStyle(invalidStyle);},"Radius exceeding half viewport rejects before clip mutation");check(editor->placement().cornerRadius==3&&editor->layout().painted()==roundHandle,"Invalid radius retains shared painted handle and clip");
    // TSF bounds and pointer use precisely the same painted range and projection.
    RECT actual{};BOOL clipped{};locked(store,sink.Get(),[&]{ok(store->GetTextExt(ProjectedTextInput::viewCookie,0,0,&actual,&clipped),"Fake TSF queries shared painted caret");});const auto expected=projectedRange(doc,editor->layout(),{0,0},editor->placement());POINT origin{};check(ClientToScreen(hwnd,&origin)!=FALSE&&expected.has_value(),"Synthetic owner screen mapping exists");check(actual.left==LONG(std::floor(expected->clientBounds.x))+origin.x&&actual.top==LONG(std::floor(expected->clientBounds.y))+origin.y&&(clipped!=FALSE)==expected->clipped,"TSF candidate anchor and clipping agree with rounded glyph/caret plane exactly");
    check(editor->takeChanges(999)==0,"Stale generation cannot drain this field changes");check(editor->takeChanges(91)!=0,"Owner drains coalesced current generation changes");
    pose.visible=false;editor->setPose(pose);composition.present(renderer);for(const auto&d:scene.draws())check(d.opacity==0,"Hidden owner conceals all editor adornments/glyphs");
    ok(store->UnadviseSink(sink.Get()),"Detach fake observer");ok(editor->stop(),"Stop borrowed editor context without shared-manager deactivation");check(doc.text()==saved&&!doc.composition(),"Stop leaves committed caller draft intact");composition.detach(renderer);editor.reset();check(renderer.stats().textures==0&&renderer.stats().meshes==0,"Caller composition removes resources before borrowed scene teardown");
    check(!IsWindowVisible(hwnd),"Fixture never displays a window or changes real IME profile");
    LayerScene rejected(raster);Buffer longDoc(std::u16string(65,u'x'),1024);rejects([&]{NativeProjectedEditor bad(hwnd,longDoc,rejected,style,options,{64},WM_APP+122,92);},"Oversized initial field rejects explicitly with document intact");check(longDoc.text().size()==65&&rejected.contentRevision()==0,"Rejected construction does not truncate data or replace borrowed scene");
}
void scrolling(Renderer&renderer,HWND hwnd){
    LayerRasterizer raster;LayerScene scene(raster);std::u16string value;for(unsigned k=0;k<240;++k)value+=u"中文输入与选择 日本語 한국어 😀\n";Buffer doc(value,65536);
    ProjectedEditorStyle style;style.width=180;style.height=80;style.fontSize=12;style.lineHeight=18;style.baseline=13;style.cornerRadius=3;
    LayerRasterOptions options;options.pixelsPerPoint=1;NativeProjectedEditor editor(hwnd,doc,scene,style,options,{65536},WM_APP+124,94);
    ComPtr<FakeManager>manager;manager.Attach(new FakeManager);ok(editor.connect(*manager.Get(),7),"Scrollable editor borrows isolated manager");ok(editor.focus(true),"Focus isolated scrollable field");
    ComPtr<Sink>sink;sink.Attach(new Sink);auto*store=editor.textStore();ok(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_TEXT_CHANGE|TS_AS_SEL_CHANGE|TS_AS_LAYOUT_CHANGE),"Observe scrolled geometry with fake TSF sink");
    check(validPlacement(editor.placement())&&!editor.placement().visible,"Pre-pose editor has a valid concealed TSF plane");
    const auto unposedRevision=scene.resourceRevision();const auto unposedRaster=raster.stats().rasterizations;
    locked(store,sink.Get(),[&]{check(!editor.scrollBy(25)&&!editor.syncContent(),"TSF lock declines scroll and sync before first projected pose");});
    check(editor.scrollOffset()==0&&scene.resourceRevision()==unposedRevision&&raster.stats().rasterizations==unposedRaster,"Pre-pose locked scroll cannot mutate viewport pixels or offset");
    check(editor.setScrollOffset(23.125)&&editor.placement().scroll.y==23.125&&!editor.placement().visible,"Unlocked initial restore updates only concealed document geometry");
    check(editor.setScrollOffset(0),"Restore original top before first visible pose");
    ProjectedEditorPose pose;pose.localToScreen=Matrix4::translation(30,40);pose.localToScreen.values[3]=.00007;pose.localToScreen.values[7]=-.00002;pose.screenToClip=layerViewportProjection(512,256);pose.pixelWidth=512;pose.pixelHeight=256;pose.ownerFocused=true;editor.setPose(pose);
    LayerComposition composition;const std::array scenes{&scene};composition.setScenes(renderer,scenes);renderer.setCamera(pose.screenToClip);
    auto sync=[&]{editor.syncContent();editor.setPose(pose);composition.upload(renderer);composition.present(renderer);};
    const auto end=static_cast<std::uint32_t>(doc.text().size());check(editor.maximumScrollOffset()>3000&&editor.scrollOffset()==0,"Long Chinese document starts at preserved top offset");
    check(scene.report().pixelBytes<180*80*4+2048,"Empty adornments and caret retain only ink-sized pixels");
    editor.command(ProjectedEditorCommand::documentEnd);check(!editor.setScrollOffset(0),"Initial restore clears pending end reveal even at equal offset");sync();check(editor.scrollOffset()==0,"Session offset restore wins over initial selection-at-end");
    editor.command(ProjectedEditorCommand::documentEnd);sync();check(editor.scrollOffset()>0&&editor.placement().scroll.y==editor.scrollOffset(),"Keyboard navigation reveals caret and updates shared placement");
    const auto visible=projectedRange(doc,editor.layout(),{end,end},editor.placement());check(visible&&visible->clientBounds.height>0,"Revealed final empty line has a projected caret");
    const auto identity=editor.layout().painted()->layoutIdentity(),beforeLayouts=raster.stats().textLayoutsCreated;const auto viewportBytes=scene.report().pixelBytes;
    for(unsigned k=1;k<=12;++k){check(editor.setScrollOffset(double(k)*23.125),"Fractional logical scrolling accepted");composition.upload(renderer);composition.present(renderer);check(editor.layout().painted()->layoutIdentity()==identity&&editor.placement().scroll.y==double(k)*23.125,"Glyph, hit and candidate geometry share exact retained layout and offset");}
    check(raster.stats().textLayoutsCreated==beforeLayouts&&scene.report().pixelBytes<=viewportBytes+2048,"Scroll shapes no text and retains bounded viewport artwork");
    const auto scroll=editor.scrollOffset();const auto resourceRevision=scene.resourceRevision();const auto bytes=raster.stats().resourceBytes;locked(store,sink.Get(),[&]{check(!editor.scrollBy(25),"TSF write lock declines scroll before painting");});check(editor.scrollOffset()==scroll&&scene.resourceRevision()==resourceRevision&&raster.stats().resourceBytes==bytes,"Locked scroll keeps pixels and projection together");
    rejects([&]{editor.scrollBy(std::numeric_limits<double>::infinity());},"Nonfinite wheel offset rejects");
    check(editor.setScrollOffset(0),"Return to first line for projected selection");sync();const auto first=editor.layout().selectionRectangles({0,1}).front();const auto click=editor.placement().projection.project({first.x+first.width*.1,first.y+first.height*.5});check(click&&editor.pointerDown(*click).handled,"Scrolled pointer uses exact document ACP");sync();const auto outside=editor.placement().projection.project({first.x+first.width*.5,style.height+100});check(outside&&editor.pointerDrag(*outside).handled,"Drag outside viewport reaches later document lines");sync();editor.pointerUp();check(editor.scrollOffset()>0&&doc.selection().range.end>1,"Event-driven drag reveal advances beyond viewport-clamped hit");
    editor.command(ProjectedEditorCommand::documentEnd);sync();editor.character(u'文');sync();const auto typing=raster.stats();editor.character(u'字');sync();check(raster.stats().textLayoutsCreated==typing.textLayoutsCreated+1&&raster.stats().rasterizations==typing.rasterizations+1,"Typing with unchanged caret metrics paints one viewport, not four full images");
    const auto caretRaster=raster.stats().rasterizations;editor.command(ProjectedEditorCommand::left);sync();check(raster.stats().rasterizations==caretRaster,"Collapsed caret navigation moves numeric quad without rasterizing");
    const auto selected=doc.selection().range.start;RECT actual{};BOOL clipped{};locked(store,sink.Get(),[&]{ok(store->GetTextExt(ProjectedTextInput::viewCookie,LONG(selected),LONG(selected),&actual,&clipped),"TSF reads actual scrolled caret");});const auto expected=projectedRange(doc,editor.layout(),{selected,selected},editor.placement());POINT origin{};check(ClientToScreen(hwnd,&origin)&&expected&&actual.top==LONG(std::floor(expected->clientBounds.y))+origin.y,"Candidate extent applies fractional scroll and perspective exactly once");
    const auto frozen=raster.stats();const auto gpu=renderer.stats();allocations=0;counting=true;try{for(unsigned k=0;k<120;++k){pose.localToScreen.values[3]=double(k)*.000001;editor.setPose(pose);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==frozen.rasterizations&&renderer.stats().textureUploads==gpu.textureUploads,"Scrolled editing pose frames allocate, reshape and upload nothing");
    ok(store->UnadviseSink(sink.Get()),"Detach scroll fixture sink");ok(editor.stop(),"Stop scroll fixture");composition.detach(renderer);
}

}
int wmain(int argc,wchar_t**argv){try{check(argc==2,"Pass native shader path");const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);ok(initialized,"Fixture COM apartment initializes");{Window window;Renderer renderer;renderer.initialize(window.hwnd,512,256,{Driver::warpForTests,argv[1],RenderTarget::offscreenForTests});run(renderer,window.hwnd);scrolling(renderer,window.hwnd);renderer.reset();}CoUninitialize();std::cout<<checks<<" projected editor integration checks passed\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"Projected editor test failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#endif
