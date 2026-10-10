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
void singleLineField(Renderer&renderer,HWND hwnd){
    LayerRasterizer raster;LayerScene scene(raster);Buffer doc(u"30:00",65536);
    ProjectedEditorStyle style;style.width=312;style.height=68;style.fontSize=46;style.fontFace=".SFNS-Medium";style.alignment=ProjectedEditorAlignment::center;style.wrapped=false;style.sourceSingleLineField=true;style.cornerRadius=4;
    LayerRasterOptions options;options.pixelsPerPoint=1;NativeProjectedEditor editor(hwnd,doc,scene,style,options,{65536},WM_APP+129,99);
    auto* layout=reinterpret_cast<IDWriteTextLayout*>(editor.layout().painted()->layoutIdentity());
    ComPtr<IDWriteFontCollection> fonts;ok(layout->GetFontCollection(0,fonts.GetAddressOf()),"Read font collection from painted field");UINT32 nameLength{};ok(layout->GetFontFamilyNameLength(0,&nameLength),"Read painted field family length");
    std::wstring family(nameLength+1,L'\0');ok(layout->GetFontFamilyName(0,family.data(),nameLength+1),"Read painted field family");UINT32 index{};BOOL exists{};ok(fonts->FindFamilyName(family.c_str(),&index,&exists),"Resolve actual painted family");check(exists!=FALSE,"Painted family exists in the retained collection");
    ComPtr<IDWriteFontFamily>fontFamily;ok(fonts->GetFontFamily(index,fontFamily.GetAddressOf()),"Read painted family metrics");DWRITE_FONT_WEIGHT weight{};DWRITE_FONT_STYLE slant{};FLOAT em{};
    ok(layout->GetFontWeight(0,&weight),"Read actual painted weight");ok(layout->GetFontStyle(0,&slant),"Read actual painted slant");ok(layout->GetFontSize(0,&em),"Read actual painted em");ComPtr<IDWriteFont>font;ok(fontFamily->GetFirstMatchingFont(weight,DWRITE_FONT_STRETCH_NORMAL,slant,font.GetAddressOf()),"Read selected field font");
    DWRITE_FONT_METRICS fontMetrics{};font->GetMetrics(&fontMetrics);check(fontMetrics.designUnitsPerEm>0,"Selected font metrics have a valid em");
    const Point inset{3,std::max(0.,(style.height-(double(fontMetrics.ascent)+fontMetrics.descent+fontMetrics.lineGap)*em/fontMetrics.designUnitsPerEm)*.5)};
    const auto painted=editor.layout().painted();check(painted->contentInset()==inset&&painted->documentWidth()==style.width&&painted->documentHeight()==style.height,"Source field uses three-point inset, selected-font centering and fixed visible height");
    DWRITE_TEXT_METRICS metrics{};ok(layout->GetMetrics(&metrics),"Inspect same field logical extent");check(layout->GetMaxWidth()==306&&layout->GetTextAlignment()==DWRITE_TEXT_ALIGNMENT_CENTER,"Finite field container preserves source centered alignment");
    const auto actual=readPaintedMetrics(editor.layout());const auto caret=editor.layout().bounds({0,0})->bounds;
    check(std::abs(caret.x-actual.caret.x-inset.x)<.0001&&std::abs(caret.y-actual.caret.y-inset.y)<.0001,"Caret uses the exact painted glyph inset and baseline");
    ComPtr<FakeManager>manager;manager.Attach(new FakeManager);ok(editor.connect(*manager.Get(),7),"Field borrows fake activated manager");ok(editor.focus(true),"Focus isolated field");ComPtr<Sink>sink;sink.Attach(new Sink);auto*store=editor.textStore();ok(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_TEXT_CHANGE|TS_AS_SEL_CHANGE|TS_AS_LAYOUT_CHANGE),"Observe field geometry without real IME activation");
    ProjectedEditorPose pose;pose.localToScreen=Matrix4::translation(25,35);pose.localToScreen.values[3]=.00005;pose.localToScreen.values[7]=-.00002;pose.screenToClip=layerViewportProjection(512,256);pose.pixelWidth=512;pose.pixelHeight=256;pose.ownerFocused=true;editor.setPose(pose);
    LayerComposition composition;const std::array scenes{&scene};composition.setScenes(renderer,scenes);renderer.setCamera(pose.screenToClip);
    auto sync=[&]{editor.syncContent();editor.setPose(pose);composition.upload(renderer);composition.present(renderer);};
    const std::u16string longText=u"中文😀e\u0301 12345678901234567890123456789012345678901234567890";doc.replace({0,5},longText);editor.syncContent();editor.command(ProjectedEditorCommand::documentEnd);sync();
    layout=reinterpret_cast<IDWriteTextLayout*>(editor.layout().painted()->layoutIdentity());ok(layout->GetMetrics(&metrics),"Inspect grown same-layout field extent");const auto expectedWidth=std::max(style.width,std::ceil(double(metrics.widthIncludingTrailingWhitespace))+11);
    check(editor.layout().painted()->documentWidth()==expectedWidth&&layout->GetMaxWidth()==static_cast<float>(expectedWidth-6)&&editor.maximumHorizontalScrollOffset()==expectedWidth-style.width,"Long text grows finite document/container widths with exact source padding");
    check(doc.text()==longText&&editor.scrollOffset()==0&&editor.maximumScrollOffset()==0&&editor.horizontalScrollOffset()>0&&editor.placement().scroll.x==editor.horizontalScrollOffset(),"Long field reveals horizontally without truncation or vertical scrolling");
    check(scene.report().pixelBytes<style.width*style.height*4+4096,"Logical field expansion retains only fixed viewport and ink-sized adornment pixels");
    const auto end=static_cast<std::uint32_t>(doc.text().size());RECT extent{};BOOL clipped{};locked(store,sink.Get(),[&]{ok(store->GetTextExt(ProjectedTextInput::viewCookie,LONG(end),LONG(end),&extent,&clipped),"Read grown field candidate rectangle");});
    const auto projected=projectedRange(doc,editor.layout(),{end,end},editor.placement());POINT origin{};check(ClientToScreen(hwnd,&origin)&&projected&&projected->clientBounds.width>0&&extent.left==LONG(std::floor(projected->clientBounds.x))+origin.x&&extent.top==LONG(std::floor(projected->clientBounds.y))+origin.y,"Field glyph, caret and TSF extent apply inset/horizontal scroll/perspective once");
    const auto identity=editor.layout().painted()->layoutIdentity(),layoutCount=raster.stats().textLayoutsCreated;const auto rasterCount=raster.stats().rasterizations;
    check(editor.setHorizontalScrollOffset(23.125)&&editor.placement().scroll==Point{23.125,0},"Fractional horizontal scroll updates shared projection");composition.upload(renderer);composition.present(renderer);
    check(editor.layout().painted()->layoutIdentity()==identity&&raster.stats().textLayoutsCreated==layoutCount&&raster.stats().rasterizations==rasterCount+1,"Horizontal scrolling repaints one fixed viewport with no reshape");
    const auto revision=scene.resourceRevision();locked(store,sink.Get(),[&]{check(!editor.setHorizontalScrollOffset(30),"TSF lock declines horizontal scroll before mutation");});check(editor.horizontalScrollOffset()==23.125&&scene.resourceRevision()==revision,"Locked horizontal scroll preserves pixels and placement");
    rejects([&]{editor.setHorizontalScrollOffset(std::numeric_limits<double>::infinity());},"Nonfinite field offset rejects");
    editor.command(ProjectedEditorCommand::documentEnd);check(!editor.setHorizontalScrollOffset(23.125),"Equal explicit field restore cancels pending caret reveal");sync();check(editor.horizontalScrollOffset()==23.125,"Manual field restore wins over pending navigation");
    const Point logical{100,inset.y+actual.caret.height*.5};const auto pointer=editor.placement().projection.project(logical);check(pointer&&editor.pointerDown(*pointer).handled,"Field hit testing reaches painted ACP after fractional scroll");sync();
    const Point drag{180,logical.y};const auto dragPoint=editor.placement().projection.project(drag);const auto expectedHit=editor.layout().hit({drag.x+editor.horizontalScrollOffset(),drag.y},true,true);check(dragPoint&&expectedHit&&editor.pointerDrag(*dragPoint).handled&&doc.selection().range.end==*expectedHit,"Field selection drag uses current horizontal offset exactly once");editor.pointerUp();sync();
    editor.command(ProjectedEditorCommand::documentStart);sync();check(editor.horizontalScrollOffset()<23.125,"Document-start navigation reveals the leading source inset");
    const auto frozen=raster.stats();const auto gpu=renderer.stats();allocations=0;counting=true;try{for(unsigned k=0;k<120;++k){pose.localToScreen.values[3]=double(k)*.000001;editor.setPose(pose);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==frozen.rasterizations&&renderer.stats().textureUploads==gpu.textureUploads,"Expanded field tilt retains glyph/layout/texture resources with zero frame allocations");
    auto bad=style;bad.wrapped=true;rejects([&]{editor.setStyle(bad);},"Single-line source field rejects contradictory wrapping");bad=style;bad.lineHeight=50;bad.baseline=40;rejects([&]{editor.setStyle(bad);},"Single-line source field uses selected-font metrics rather than uniform Notes spacing");
    ok(store->UnadviseSink(sink.Get()),"Detach field fake sink");ok(editor.stop(),"Stop field context");composition.detach(renderer);
    // maximumNumberOfLines=1 is a display/layout rule, not data truncation.
    LayerScene breakScene(raster);Buffer breaks(u"first\nsecond",1024);NativeProjectedEditor breakEditor(hwnd,breaks,breakScene,style,options,{1024},WM_APP+130,100);
    check(breakEditor.layout().painted()->text()==breaks.text()&&breakEditor.maximumScrollOffset()==0,"Source one-line display retains all caller hard-break text");
    const auto second=breakEditor.layout().bounds({6,12});check(second&&second->clipped&&second->bounds.height==0,"Second explicit line is excluded from source single-line visible range geometry");
    check(breakEditor.layout().bounds({0,5})->bounds.height>0,"Source first line remains painted and selectable");ok(breakEditor.stop(),"Stop hard-break source field fixture");LayerScene emptyScene(raster);Buffer empty({},1024);NativeProjectedEditor emptyEditor(hwnd,empty,emptyScene,style,options,{1024},WM_APP+131,101);emptyEditor.setPose(pose);const auto blankPoint=emptyEditor.placement().projection.project({style.width-8,8});check(blankPoint&&emptyEditor.pointerDown(*blankPoint).handled&&empty.selection().range==Range{0,0},"Empty field inset accepts insertion without fabricated glyph bounds");emptyEditor.pointerUp();ok(emptyEditor.stop(),"Stop empty source field fixture");check(!IsWindowVisible(hwnd),"Field fixture uses no visible window or actual text service");
}

void sourceStyles(HWND hwnd){
    // Inspect only the very same retained object used for painting/hit/TSF.
    // No second shaping engine or fabricated character-width measurement.
    LayerRasterizer raster;LayerScene scene(raster);Buffer doc(u"1234567890123456789012345678901234567890",1024);
    ProjectedEditorStyle style;style.width=80;style.height=180;style.fontSize=20;
    LayerRasterOptions options;options.pixelsPerPoint=1;
    NativeProjectedEditor editor(hwnd,doc,scene,style,options,{1024},WM_APP+125,95);
    auto* native=reinterpret_cast<IDWriteTextLayout*>(editor.layout().painted()->layoutIdentity());
    const auto defaultMetrics=readPaintedMetrics(editor.layout());
    check(native->GetTextAlignment()==DWRITE_TEXT_ALIGNMENT_LEADING&&native->GetWordWrapping()==DWRITE_WORD_WRAPPING_WRAP&&defaultMetrics.lines.size()>1,"Default Notes text retains natural/leading alignment and soft wrapping");
    const auto original=editor.layout().painted();const auto count=raster.stats().textLayoutsCreated;
    auto explicitDefaults=style;explicitDefaults.alignment=ProjectedEditorAlignment::natural;explicitDefaults.wrapped=true;explicitDefaults.naturalParagraphSpacingOne=false;
    check(!editor.setStyle(explicitDefaults)&&!editor.syncContent()&&editor.layout().painted()==original&&raster.stats().textLayoutsCreated==count,"Explicit default fields keep existing Notes painted layout unchanged");
    auto centered=style;centered.alignment=ProjectedEditorAlignment::center;centered.wrapped=false;
    check(editor.setStyle(centered),"Changing alignment/wrapping marks the source field style dirty");
    check(editor.syncContent(),"Dirty source alignment/wrapping rebuilds its one painted layout");
    native=reinterpret_cast<IDWriteTextLayout*>(editor.layout().painted()->layoutIdentity());
    const auto centeredMetrics=readPaintedMetrics(editor.layout());
    check(native->GetTextAlignment()==DWRITE_TEXT_ALIGNMENT_CENTER&&native->GetWordWrapping()==DWRITE_WORD_WRAPPING_NO_WRAP&&centeredMetrics.lines.size()==1&&editor.layout().painted()->text()==doc.text(),"Actual painted Work Mode layout centers and disables soft wrapping without truncating its document");
    const auto centeredHandle=editor.layout().painted();auto invalid=centered;invalid.alignment=static_cast<ProjectedEditorAlignment>(99);
    rejects([&]{editor.setStyle(invalid);},"Unsupported alignment rejects before scene or document mutation");
    check(editor.layout().painted()==centeredHandle&&doc.text().size()==40,"Rejected alignment preserves painted handle and full document");
    // Hard line breaks remain caller-owned even in a no-wrap field.
    Buffer breaks(u"first\nsecond",1024);LayerScene breakScene(raster);NativeProjectedEditor breakEditor(hwnd,breaks,breakScene,centered,options,{1024},WM_APP+126,96);
    check(readPaintedMetrics(breakEditor.layout()).lines.size()==2&&breaks.text()==u"first\nsecond","No-wrap does not silently remove explicit caller newlines");

    namespace notes=endfield::core::notes;
    const std::u16string mixed=u"small\nBIG\n中文😀";notes::TextStyle regular,big;big.fontSize=48;big.bold=true;
    notes::RichText attributed;attributed.runs={{0,6,regular,{}},{6,4,big,{}},{10,static_cast<std::uint32_t>(mixed.size()-10),regular,{}}};
    notes::RichDocument rich(mixed,attributed,1024);rich.setSelection({{1,8},ActiveEnd::end,false});const auto importedPayload=rich.richText();const auto importedSelection=rich.selection();const auto importedRevision=rich.revision();LayerScene richScene(raster);auto richStyle=style;richStyle.width=300;richStyle.height=200;richStyle.fontSize=12;
    NativeProjectedEditor richEditor(hwnd,rich,richScene,richStyle,options,{1024},WM_APP+127,97);
    const auto imported=readPaintedMetrics(richEditor.layout());check(imported.method==DWRITE_LINE_SPACING_METHOD_DEFAULT&&imported.lines.size()==3,"Imported Notes rich layout starts with natural mixed-font metrics");
    std::array<Rect,3> oldCarets{};const std::array<std::uint32_t,3> starts{0,6,10};
    for(std::size_t n=0;n<starts.size();++n){native=reinterpret_cast<IDWriteTextLayout*>(richEditor.layout().painted()->layoutIdentity());FLOAT x{},y{};DWRITE_HIT_TEST_METRICS hit{};ok(native->HitTestTextPosition(starts[n],FALSE,&x,&y,&hit),"Inspect imported painted native caret");oldCarets[n]=richEditor.layout().bounds({starts[n],starts[n]})->bounds;check(std::abs(oldCarets[n].y-double(y))<.0001,"Notes imported text adds no unrequested paragraph spacing");}
    auto archive=richStyle;archive.naturalParagraphSpacingOne=true;
    check(richEditor.setStyle(archive)&&richEditor.syncContent(),"Archive requests natural metrics plus one-point paragraph spacing");
    const auto spaced=readPaintedMetrics(richEditor.layout());
    check(spaced.method==DWRITE_LINE_SPACING_METHOD_DEFAULT&&spaced.lines.size()==imported.lines.size(),"Archive preserves natural mixed-font line heights instead of uniform Notes metrics");
    for(std::size_t n=0;n<starts.size();++n){const auto caret=richEditor.layout().bounds({starts[n],starts[n]})->bounds;check(std::abs(caret.y-oldCarets[n].y-double(n))<.0001&&caret.x==oldCarets[n].x&&caret.height==oldCarets[n].height&&spaced.lines[n].height==imported.lines[n].height,"Archive paint and caret apply exactly one additional point per preceding line");}
    auto conflicted=archive;conflicted.lineHeight=20;conflicted.baseline=15;
    rejects([&]{richEditor.setStyle(conflicted);},"Natural Archive spacing rejects contradictory uniform metrics");
    check(rich.text()==mixed&&rich.richText()==importedPayload&&rich.selection()==importedSelection&&rich.revision()==importedRevision,"Layout-only options preserve the normalized rich payload, selection and document revision");
    // Plain date/category fields use the same paragraph map with zero runs.
    Buffer plain(u"alpha\nbeta",1024);LayerScene plainScene(raster);auto plainStyle=richStyle;plainStyle.naturalParagraphSpacingOne=true;
    NativeProjectedEditor plainEditor(hwnd,plain,plainScene,plainStyle,options,{1024},WM_APP+128,98);
    native=reinterpret_cast<IDWriteTextLayout*>(plainEditor.layout().painted()->layoutIdentity());FLOAT x{},y{};DWRITE_HIT_TEST_METRICS hit{};ok(native->HitTestTextPosition(6,FALSE,&x,&y,&hit),"Inspect plain Archive native caret");
    check(std::abs(plainEditor.layout().bounds({6,6})->bounds.y-double(y)-1)<.0001,"Plain Archive field shares painted one-point spacing and caret mapping");
    auto defaults=richStyle;check(plainEditor.setStyle(defaults)&&plainEditor.syncContent(),"Owner can restore ordinary plain Notes spacing");native=reinterpret_cast<IDWriteTextLayout*>(plainEditor.layout().painted()->layoutIdentity());ok(native->HitTestTextPosition(6,FALSE,&x,&y,&hit),"Inspect restored plain native caret");check(std::abs(plainEditor.layout().bounds({6,6})->bounds.y-double(y))<.0001,"Restoring default mode removes only requested extra spacing");
    ok(plainEditor.stop(),"Stop plain spacing fixture");ok(richEditor.stop(),"Stop rich spacing fixture");ok(breakEditor.stop(),"Stop no-wrap fixture");ok(editor.stop(),"Stop default-style fixture");check(!IsWindowVisible(hwnd),"Source-style fixtures remain hidden and activate no real input service");
}

void hostReplacement(HWND hwnd){
    LayerRasterizer raster;LayerScene scene(raster);Buffer doc(u"abcdefghijklmnop",1024);
    ProjectedEditorStyle style;style.width=240;style.height=80;LayerRasterOptions options;options.pixelsPerPoint=1;
    NativeProjectedEditor editor(hwnd,doc,scene,style,options,{64},WM_APP+129,99);
    ComPtr<Sink>sink;sink.Attach(new Sink);auto*store=editor.textStore();
    ok(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_TEXT_CHANGE|TS_AS_SEL_CHANGE|TS_AS_LAYOUT_CHANGE),"Observe explicit host normalization with fake TSF sink");
    const auto painted=editor.layout().painted();const auto before=raster.stats();
    const auto normalized=editor.replaceTextFromHost({0,16},u"中文🌙");
    check(normalized.handled&&normalized.changed&&!normalized.finishRequested&&doc.text()==u"中文🌙"&&doc.selection().range==Range{4,4},"Explicit host replacement preserves whole UTF16 characters and caller selection");
    check(sink->text==1&&sink->selection==1&&sink->last.acpStart==0&&sink->last.acpOldEnd==16&&sink->last.acpNewEnd==4,"Host replacement sends one exact text delta and selection notification outside locks");
    check(raster.stats().rasterizations==before.rasterizations&&raster.stats().textLayoutsCreated==before.textLayoutsCreated&&editor.layout().painted()==painted,"Host normalization neither shapes nor paints before the caller synchronizes");
    check(editor.syncContent()&&editor.layout().painted()->text()==doc.text()&&raster.stats().textLayoutsCreated==before.textLayoutsCreated+1,"Explicit synchronization paints the normalized document once");
    const auto unchanged=std::u16string(doc.text());const auto revision=doc.revision();const auto notified=sink->text;
    locked(store,sink.Get(),[&]{const auto result=editor.replaceTextFromHost({0,1},u"X");check(result.handled&&!result.changed&&doc.text()==unchanged&&sink->text==notified,"Locked TSF callback declines host normalization without mutation or sink echo");});
    ComPtr<ITfContextOwnerCompositionSink>owner;ok(store->QueryInterface(IID_PPV_ARGS(&owner)),"Explicit host edit fixture has the same TSF composition owner");
    ComPtr<FakeRange>range;range.Attach(new FakeRange(0,1));ComPtr<FakeView>view;view.Attach(new FakeView(range.Get()));
    locked(store,sink.Get(),[&]{BOOL accepted{};ok(owner->OnStartComposition(view.Get(),&accepted),"Start isolated marked text before normalization");check(accepted!=FALSE,"Composition accepted by the existing owner");});
    const auto marked=editor.replaceTextFromHost({0,1},u"X");check(marked.handled&&!marked.changed&&doc.composition()==std::optional(Range{0,1})&&doc.text()==unchanged&&sink->text==notified,"Host limits never truncate or rewrite marked composition");
    locked(store,sink.Get(),[&]{ok(owner->OnEndComposition(view.Get()),"Commit isolated composition before host normalization");});
    check(!doc.composition()&&doc.revision()==revision,"Composition commit without text change leaves document revision intact");
    const std::u16string excess(65,u'x'),invalid(1,char16_t(0xd800));
    check(!editor.replaceTextFromHost({0,4},excess).changed&&!editor.replaceTextFromHost({0,5},u"X").changed&&!editor.replaceTextFromHost({0,1},invalid).changed&&!editor.replaceTextFromHost({3,4},u"X").changed,"Capacity, invalid range, lone surrogate and split surrogate replacement decline atomically");
    doc.setReadOnly(true);check(!editor.replaceTextFromHost({0,1},u"X").changed,"Read-only owner declines explicit host normalization");doc.setReadOnly(false);
    check(doc.text()==unchanged&&doc.revision()==revision&&sink->text==notified&&doc.maximumUnits()==1024,"Rejected normalization never truncates storage or publishes a text notification");
    check(!editor.character(0xd83d).changed,"Hold a partial WM_CHAR pair before explicit normalization");
    check(editor.replaceTextFromHost({0,4},u"X").changed&&doc.text()==u"X","Host edit works after marked text ends");
    check(!editor.character(0xde00).changed&&doc.text()==u"X","Host edit resets buffered surrogate input rather than appending an obsolete pair");
    check(editor.syncContent()&&editor.layout().painted()->text()==u"X","Following host edit retains the same painted-document contract");
    ok(store->UnadviseSink(sink.Get()),"Detach host normalization fixture sink");ok(editor.stop(),"Stop host normalization fixture without real TSF activation");
    check(!IsWindowVisible(hwnd),"Explicit host normalization fixture remains hidden");
}
void hostSelection(HWND hwnd){
    LayerRasterizer raster;LayerScene scene(raster);Buffer doc(u"A🌙中Z",1024);
    ProjectedEditorStyle style;style.width=240;style.height=80;LayerRasterOptions options;options.pixelsPerPoint=1;
    NativeProjectedEditor editor(hwnd,doc,scene,style,options,{64},WM_APP+131,101);
    ComPtr<Sink>sink;sink.Attach(new Sink);auto*store=editor.textStore();
    ok(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_TEXT_CHANGE|TS_AS_SEL_CHANGE|TS_AS_LAYOUT_CHANGE),"Observe host selection through the existing fake text store");
    const auto original=std::u16string(doc.text());const auto revision=doc.revision();const auto painted=editor.layout().painted();const auto before=raster.stats();
    const Selection next{{1,4},ActiveEnd::start,false};const auto selected=editor.setSelectionFromHost(next);
    check(selected.handled&&selected.changed&&!selected.finishRequested&&doc.selection()==next,"Host normalization restores the requested UTF16 active endpoint");
    check(sink->selection==1&&sink->text==0&&doc.text()==original&&doc.revision()==revision,"Selection sends one TSF notification without text mutation");
    check(raster.stats().rasterizations==before.rasterizations&&raster.stats().textLayoutsCreated==before.textLayoutsCreated&&editor.layout().painted()==painted,"Selection restoration does not paint or reshape before owner synchronization");
    locked(store,sink.Get(),[&]{check(!editor.setSelectionFromHost({{0,0},ActiveEnd::end,false}).changed&&doc.selection()==next&&sink->selection==1,"TSF write lock declines host selection without mutation or echo");});
    check(!editor.setSelectionFromHost({{0,6},ActiveEnd::end,false}).changed&&!editor.setSelectionFromHost({{4,1},ActiveEnd::end,false}).changed&&doc.selection()==next&&sink->selection==1,"Out-of-bounds and reversed ACP restore decline atomically");
    ComPtr<ITfContextOwnerCompositionSink>owner;ok(store->QueryInterface(IID_PPV_ARGS(&owner)),"Selection fixture borrows the existing composition sink");
    ComPtr<FakeRange>range;range.Attach(new FakeRange(0,1));ComPtr<FakeView>view;view.Attach(new FakeView(range.Get()));
    locked(store,sink.Get(),[&]{BOOL accepted{};ok(owner->OnStartComposition(view.Get(),&accepted),"Start synthetic marked selection");check(accepted!=FALSE,"Synthetic composition accepted");});
    const auto marked=doc.selection();check(!editor.setSelectionFromHost({{0,0},ActiveEnd::end,false}).changed&&doc.selection()==marked&&sink->selection==1,"Host selection never moves a marked composition");
    locked(store,sink.Get(),[&]{ok(owner->OnEndComposition(view.Get()),"End synthetic marked selection");});
    check(editor.setSelectionFromHost({{4,4},ActiveEnd::end,false}).changed&&doc.selection().range==Range{4,4}&&sink->selection==2,"Unmarked owner selection is restored through TSF");
    check(editor.syncContent()&&editor.layout().painted()==painted&&raster.stats().textLayoutsCreated==before.textLayoutsCreated,"Selection sync reuses the actual painted glyph layout");
    ok(store->UnadviseSink(sink.Get()),"Detach host selection fixture sink");ok(editor.stop(),"Stop host selection fixture");
}
void explicitCompositionCommit(HWND hwnd){
    namespace notes=endfield::core::notes;
    LayerRasterizer raster;LayerScene scene(raster);notes::TextStyle bold;bold.bold=true;
    notes::RichText attributed;attributed.runs={{0,3,bold,{}}};notes::RichDocument doc(u"original",attributed,1024);
    doc.setSelection({{0,3},ActiveEnd::start,false});const auto originalPayload=doc.richText();const auto originalSelection=doc.selection();
    ProjectedEditorStyle style;style.width=240;style.height=80;LayerRasterOptions options;options.pixelsPerPoint=1;
    NativeProjectedEditor editor(hwnd,doc,scene,style,options,{1024},WM_APP+130,100);
    ComPtr<FakeManager>manager;manager.Attach(new FakeManager);ok(editor.connect(*manager.Get(),7),"Explicit commit borrows the same fake TSF manager");
    ComPtr<Sink>sink;sink.Attach(new Sink);auto*store=editor.textStore();ok(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_TEXT_CHANGE|TS_AS_SEL_CHANGE|TS_AS_LAYOUT_CHANGE),"Explicit commit has isolated sink notifications");
    ComPtr<ITfContextOwnerCompositionSink>owner;ok(store->QueryInterface(IID_PPV_ARGS(&owner)),"Explicit commit uses the actual composition owner interface");
    ComPtr<FakeRange>range;range.Attach(new FakeRange(0,1));ComPtr<FakeView>view;view.Attach(new FakeView(range.Get()));
    locked(store,sink.Get(),[&]{TS_TEXTCHANGE delta{};LONG a{},b{};ok(store->InsertTextAtSelection(0,L"中",1,&a,&b,&delta),"IME inserts provisional text before its composition");BOOL accepted{};ok(owner->OnStartComposition(view.Get(),&accepted),"IME starts rich marked text");check(accepted!=FALSE,"Explicit commit accepts original marked range");ok(store->SetText(0,0,1,L"中文😀",4,&delta),"IME updates the same provisional text with complete UTF16");});
    const auto provisional=std::u16string(doc.text());const auto provisionalPayload=doc.richText();const auto provisionalSelection=doc.selection();const auto provisionalRevision=doc.revision();
    check(provisional==u"中文😀ginal"&&doc.composition().has_value()&&sink->text==0,"Provisional rich text stays owned by the IME without text-store sink echo");
    const auto painted=editor.layout().painted();const auto stats=raster.stats();
    locked(store,sink.Get(),[&]{check(editor.commitComposition()==TS_E_NOLOCK&&doc.composition().has_value()&&doc.text()==provisional,"Explicit commit declines inside a TSF document lock without cancelling text");});
    auto context=manager->documents[0]->context;unsigned terminations{};
    context->terminate=[&](ITfCompositionView*owned){++terminations;check(owned==view.Get(),"Commit terminates only this exact retained composition view");return TF_E_NOLOCK;};
    check(editor.commitComposition()==TF_E_NOLOCK&&doc.text()==provisional&&doc.composition().has_value(),"Native lock failure preserves marked text for event-driven retry");
    context->terminate=[&](ITfCompositionView*){++terminations;return S_OK;};
    check(editor.commitComposition()==E_UNEXPECTED&&doc.composition().has_value()&&doc.text()==provisional,"A defective native termination cannot fabricate a host-only commit");
    context->terminate=[&](ITfCompositionView*owned){++terminations;check(editor.commitComposition()==E_UNEXPECTED,"Reentrant composition operation declines rather than recursively terminating");locked(store,sink.Get(),[&]{ok(owner->OnEndComposition(owned),"Native termination calls OnEndComposition under its own synchronous write lock");check(!doc.composition()&&doc.text()==provisional&&sink->text==0,"Commit clears only the marker and never restores provisional text or echoes a host edit");});return S_OK;};
    ok(editor.commitComposition(),"Explicit source save commits current marked text through TSF");
    check(terminations==3&&!doc.composition()&&doc.text()==provisional&&doc.richText()==provisionalPayload&&doc.selection()==provisionalSelection&&doc.revision()==provisionalRevision,"Successful commit preserves full text, formatting, selection and revision");
    check(raster.stats().rasterizations==stats.rasterizations&&raster.stats().textLayoutsCreated==stats.textLayoutsCreated&&editor.layout().painted()==painted,"Commit performs no shaping, rasterization or immediate layout publication");
    check(editor.commitComposition()==S_FALSE&&terminations==3,"Already unmarked document does not call a native service");
    check(editor.syncContent()&&editor.layout().painted()->text()==doc.text(),"Caller paints committed text only when explicitly synchronizing");
    check(editor.undo().changed&&doc.text()==u"original"&&doc.richText()==originalPayload&&doc.selection()==originalSelection,"Committed provisional rich edits remain one caller undo group with original selection");
    ok(store->UnadviseSink(sink.Get()),"Detach explicit commit sink");ok(editor.stop(),"Stop committed editor without undoing retained document");
    // An external COM callback may destroy the facade. The existing Store/Impl
    // keep guards protect the call while terminal teardown rolls back unsaved
    // composition and disconnects host pointers without a stale post or crash.
    LayerScene destroyedScene(raster);Buffer destroyedDoc(u"keep",1024);auto destroyed=std::make_unique<NativeProjectedEditor>(hwnd,destroyedDoc,destroyedScene,style,options,PlainEditorFixtureCapacity{1024},WM_APP+131,101);
    ComPtr<FakeManager>destroyedManager;destroyedManager.Attach(new FakeManager);ok(destroyed->connect(*destroyedManager.Get(),7),"Commit teardown fixture uses its own fake context");
    ComPtr<Sink>destroyedSink;destroyedSink.Attach(new Sink);ComPtr<ITextStoreACP>retained=destroyed->textStore();ok(retained->AdviseSink(__uuidof(ITextStoreACPSink),destroyedSink.Get(),TS_AS_TEXT_CHANGE),"Observe isolated teardown composition");
    ComPtr<ITfContextOwnerCompositionSink>destroyedOwner;ok(retained.As(&destroyedOwner),"Retain teardown composition interface");
    locked(retained.Get(),destroyedSink.Get(),[&]{TS_TEXTCHANGE delta{};ok(retained->SetText(0,0,1,L"中",1,&delta),"Teardown fixture creates provisional text");BOOL accepted{};ok(destroyedOwner->OnStartComposition(view.Get(),&accepted),"Teardown fixture promotes provisional snapshot");check(accepted!=FALSE,"Teardown marked range accepted");});
    auto destroyedContext=destroyedManager->documents[0]->context;destroyedContext->terminate=[&](ITfCompositionView*){destroyed.reset();return S_OK;};auto*raw=destroyed.get();
    check(raw->commitComposition()==E_UNEXPECTED&&!destroyed&&destroyedDoc.text()==u"keep"&&!destroyedDoc.composition(),"Reentrant native teardown does not access dead host state or commit a cancelled facade");
    check(!IsWindowVisible(hwnd),"Explicit commit fixtures never activate real IME or display a window");
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==2,"Pass native shader path");const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);ok(initialized,"Fixture COM apartment initializes");{Window window;Renderer renderer;renderer.initialize(window.hwnd,512,256,{Driver::warpForTests,argv[1],RenderTarget::offscreenForTests});run(renderer,window.hwnd);scrolling(renderer,window.hwnd);sourceStyles(window.hwnd);singleLineField(renderer,window.hwnd);hostReplacement(window.hwnd);hostSelection(window.hwnd);explicitCompositionCommit(window.hwnd);renderer.reset();}CoUninitialize();std::cout<<checks<<" projected editor integration checks passed\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"Projected editor test failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#endif
