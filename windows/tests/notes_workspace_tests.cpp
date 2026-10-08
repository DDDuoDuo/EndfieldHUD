#include "native/notes_scene.hpp"
#include "native/notes_text_measure.hpp"
#include "native/projected_editor.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace gpu=endfield::native;namespace mod=endfield::modules;namespace core=endfield::core;namespace text=core::text;namespace data=ehud::data;
namespace {
std::size_t checks{};
void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
void ok(HRESULT hr,const char*why){check(SUCCEEDED(hr),why);}
template<class F>void rejects(F f,const char*why){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,why);}
constexpr const char*id="00000000-0000-4000-8000-000000000001";
// Only this fresh injected temporary root is opened. No application default,
// real Notes data, user profile, clipboard or Keychain is consulted.
struct TempRoot final {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("endfield-notes-workspace-"+data::makeUUID());
    TempRoot(){check(std::filesystem::create_directory(path),"Fresh owned fixture root created");}
    ~TempRoot(){std::error_code error;std::filesystem::remove_all(path,error);}
};
struct Window final {
    ATOM atom{};HWND hwnd{};
    Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldNotesWorkspaceFixture";
        atom=RegisterClassW(&c);check(atom!=0,"Owned hidden workspace class registered");
        hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned Notes workspace integration",WS_POPUP,50,60,640,360,nullptr,nullptr,c.hInstance,nullptr);check(hwnd!=nullptr,"Owned hidden workspace window created");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}
};
// Small isolated protocol sink. No actual TSF manager, IME profile, focus or
// language is activated; existing native editor tests cover composition locks.
struct Sink final:ITextStoreACPSink {
    std::atomic<ULONG>refs{1};std::function<HRESULT(DWORD)>onLock;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID i,void**o)override{if(!o)return E_POINTER;*o=nullptr;if(i!=__uuidof(IUnknown)&&i!=__uuidof(ITextStoreACPSink))return E_NOINTERFACE;*o=static_cast<ITextStoreACPSink*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto r=--refs;if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE OnTextChange(DWORD,const TS_TEXTCHANGE*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSelectionChange()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLayoutChange(TsLayoutCode,TsViewCookie)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnAttrsChange(LONG,LONG,ULONG,const TS_ATTRID*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLockGranted(DWORD flags)override{return onLock?onLock(flags):S_OK;}
    HRESULT STDMETHODCALLTYPE OnStartEditTransaction()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnEndEditTransaction()override{return S_OK;}
};
std::string utf8(std::u16string_view value){if(value.empty())return{};const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(value.data()),int(value.size()),nullptr,0,nullptr,nullptr);check(n>0,"Synthetic caller document encodes exact UTF16");std::string result(std::size_t(n),'\0');check(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(value.data()),int(value.size()),result.data(),n,nullptr,nullptr)==n,"Synthetic caller UTF8 output is complete");return result;}
void run(HWND hwnd,gpu::Renderer&renderer){
    TempRoot root;data::NotesStore store(root.path);
    const std::string initial="First line\n终末地 日本語 한국어 😀\nLast line";
    data::Note note{.id=id,.kind=data::NoteKind::text,.text=initial,.x=30,.y=40,.width=210,.height=140,.createdAt=123};store.upsert(note);
    std::size_t saves{};mod::NotesState state(store.notes(),{[&](const auto&value){store.upsert(value);++saves;},[&](auto value){store.remove(value);}});
    state.setWorkspaceBounds({0,0,640,360});state.select(std::string(id));
    check(saves==0,"Selecting the only already-front note does not write unchanged data");
    gpu::LayerRasterizer raster;gpu::NativeNotesTextMeasurer measurer(raster);mod::NotesCardPresentation presentation(id);
    mod::NotesPresentationInput input;input.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});
    auto measured=measurer.measure(id,1,initial,192);input.measured=measured->presentationText(measured);presentation.updateContent(state,input);
    gpu::LayerRasterOptions rasterOptions;rasterOptions.pixelsPerPoint=1;rasterOptions.paddingPoints=1;
    gpu::NativeNotesCardScene card(presentation,raster,rasterOptions,gpu::NativeNotesExternalEditorAppearance{{.12,.12,.12,1},input.palette.accent});
    card.syncContent();card.updatePose({},1,0);gpu::LayerScene editorScene(raster);
    text::Buffer document(u"First line\n终末地 日本語 한국어 😀\nLast line",65536);
    gpu::LayerComposition composition;const std::array settledOrder{&card.scene()};composition.setScenes(renderer,settledOrder);composition.present(renderer);renderer.setCamera(gpu::layerViewportProjection(640,360));renderer.draw(false);
    check(!card.externalEditorSlot()&&card.externalEditorAfterDraws().empty()&&card.scene().draws().size()==6,"Settled source card uses original scene with no editor references");
    state.beginEditing(id);presentation.updateContent(state,input);card.syncContent();card.updatePose({},1,1);const auto slot=*card.externalEditorSlot();
    gpu::ProjectedEditorStyle style;style.width=slot.localRect.width;style.height=slot.localRect.height;style.fontSize=12;
    style.lineHeight=measured->font.lineHeight;style.baseline=measured->font.ascent;style.fontFamily=measured->font.selectedFamily;style.fontFace="";
    style.cornerRadius=slot.cornerRadius;style.textColor=input.palette.primary;style.caretColor=input.palette.primary;
    auto editor=std::make_unique<gpu::NativeProjectedEditor>(hwnd,document,editorScene,style,rasterOptions,gpu::PlainEditorFixtureCapacity{65536},WM_APP+181,1);
    gpu::ProjectedEditorPose pose;pose.localToScreen=core::Matrix4::translation(30+slot.localRect.x,40+slot.localRect.y);pose.screenToClip=gpu::layerViewportProjection(640,360);pose.pixelWidth=640;pose.pixelHeight=360;pose.ownerFocused=true;
    editor->setPose(pose);const std::array editingOrder{gpu::LayerCompositionEntry{&card.scene(),{}},gpu::LayerCompositionEntry{&editorScene,card.externalEditorAfterDraws()}};
    composition.setEntries(renderer,editingOrder);composition.present(renderer);renderer.draw(false);
    check(editor->layout().painted()==editorScene.paintedTextLayout("projected-editor-glyphs")&&editor->layout().painted()->text()==document.text(),"Actual Notes editor binds the exact DWrite object painting caller document");
    check(editor->placement().cornerRadius==3,"Source editor rounded clip is shared with input placement");
    for(const auto&draw:editorScene.draws())check(!draw.masks.empty()&&draw.masks.back().cornerRadius==3,"Selection, glyphs, underline and caret share exact source-radius ancestor clip");
    const auto first=editor->layout().bounds({0,0});
    check(first&&std::isfinite(first->bounds.height)&&first->bounds.height>0,"Editing caret retains finite native hit geometry");
    // Test-only read of the SAME retained painted COM object; no second layout.
    // DWrite hit/caret ink-region height need not equal uniform line spacing.
    auto*paintedLayout=reinterpret_cast<IDWriteTextLayout*>(editor->layout().painted()->layoutIdentity());UINT32 lineCount{};
    const auto probe=paintedLayout->GetLineMetrics(nullptr,0,&lineCount);check((SUCCEEDED(probe)||probe==HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))&&lineCount>=3,"Painted editor exposes complete actual line metrics");
    std::vector<DWRITE_LINE_METRICS>lineMetrics(lineCount);ok(paintedLayout->GetLineMetrics(lineMetrics.data(),lineCount,&lineCount),"Read same-object painted editor line metrics");
    check(lineMetrics.size()==measured->measured.lines.size(),"Uniform editor and settled index preserve the same complete line breaks");
    for(const auto&line:lineMetrics)check(std::abs(line.height-style.lineHeight)<1e-5&&std::abs(line.baseline-style.baseline)<1e-5,"Actual painted line height and baseline match measured Notes font contract");
    // A fallback glyph can have a different ink-top/caret height even though
    // the paragraph's line spacing and baseline remain uniform. Compare each
    // ACP to the exact painted object's hit test instead of subtracting ink
    // tops belonging to different fonts.
    for(const auto acp:{0u,11u,27u}){
        FLOAT x{},y{};DWRITE_HIT_TEST_METRICS hit{};ok(paintedLayout->HitTestTextPosition(acp,FALSE,&x,&y,&hit),"Read exact painted Notes caret hit");
        const auto actual=editor->layout().bounds({acp,acp});const double height=std::max(1.,double(hit.height));
        const double left=std::max(0.,double(x)),top=std::max(0.,double(y));
        const double right=std::min(style.width,double(x)+1),bottom=std::min(style.height,double(y)+height);
        check(actual&&right>left&&bottom>top&&std::abs(actual->bounds.x-left)<1e-5&&std::abs(actual->bounds.y-top)<1e-5&&std::abs(actual->bounds.width-(right-left))<1e-5&&std::abs(actual->bounds.height-(bottom-top))<1e-5,"Workspace caret geometry is the clipped hit region of the same painted layout");
    }
    // Diagnostic only: equivalent isolated settled leaf descriptors currently
    // omit font ascender/descender metadata. This records whether a fallback
    // line's default baseline differs from the uniform paragraph baseline;
    // it does not change artwork or assert undocumented fallback equivalence.
    using Json=data::Json;
    std::size_t diagnosticLine{};
    for(const auto&line:measured->measured.lines){
        const auto value=measured->measured.text.substr(line.begin,line.visibleTextEnd-line.begin);if(value.empty()){++diagnosticLine;continue;}
        const auto probeID="isolated-notes-settled-line-"+std::to_string(diagnosticLine);
        Json leaf=Json::Object{{"id",probeID},{"kind","text"},{"bounds",Json::Array{0,0,measured->measured.width,line.height}},
            {"text",Json::Object{{"string",value},{"fontSize",12},{"font",Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".SFNS-Regular"},{"pointSize",12}}},{"foregroundColor",Json::Object{{"sRGB",Json::Array{1,1,1,1}}}},{"alignment","left"},{"wrapped",false},{"truncation","none"},{"runs",Json::Array{}}}}};
        raster.rasterize(probeID,1,leaf,rasterOptions);const auto probeLayout=raster.textLayout(probeID,1);check(bool(probeLayout),"Isolated settled baseline probe retains its actual painted layout");
        auto*settled=reinterpret_cast<IDWriteTextLayout*>(probeLayout->layoutIdentity());DWRITE_LINE_METRICS metric{};UINT32 count{};ok(settled->GetLineMetrics(&metric,1,&count),"Read isolated settled leaf native line metrics");check(count==1,"Settled baseline probe is one original line");
        std::cout<<"Notes baseline probe line="<<diagnosticLine<<" settledHeight="<<metric.height<<" settledBaseline="<<metric.baseline<<" editedHeight="<<lineMetrics[diagnosticLine].height<<" editedBaseline="<<lineMetrics[diagnosticLine].baseline<<'\n';
        raster.remove(probeID);++diagnosticLine;
    }
    check(card.scene().draws().back().opacity==0&&composition.draws().back().sourceID==card.externalEditorAfterDraws()[0].sourceID,"One final editor border paints after actual selection/glyph/caret, with no lower duplicate frame");
    const auto corner=editor->placement().projection.project({.1,.1});check(corner&&!editor->pointerDown(*corner).handled,"Source rounded corner rejects invisible editor hit");
    const auto word=editor->layout().selectionRectangles({0,1}).front();const auto click=editor->placement().projection.project({word.x+word.width*.2,word.y+word.height*.5});check(click&&editor->pointerDown(*click).handled&&document.selection().range==text::Range{0,0},"Notes workspace glyph selects through the same projected plane");editor->pointerUp();editor->syncContent();editor->setPose(pose);composition.upload(renderer);composition.present(renderer);
    Microsoft::WRL::ComPtr<Sink>sink;sink.Attach(new Sink);auto*textStore=editor->textStore();ok(textStore->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_LAYOUT_CHANGE),"Fake sink owns isolated text extent query");
    sink->onLock=[&](DWORD){RECT result{};BOOL clipped{};ok(textStore->GetTextExt(gpu::ProjectedTextInput::viewCookie,0,0,&result,&clipped),"TSF caret extent queries actual Notes painted layout");
        const auto expected=text::projectedRange(document,editor->layout(),{0,0},editor->placement());check(expected.has_value(),"Projected Notes caret has visible client geometry");POINT origin{};check(ClientToScreen(hwnd,&origin)!=FALSE,"Only owned HWND client origin is queried");
        check(result.left==LONG(std::floor(expected->clientBounds.x))+origin.x&&result.top==LONG(std::floor(expected->clientBounds.y))+origin.y,"Candidate coordinates add owned HWND screen origin exactly once");return S_OK;};
    HRESULT session{};ok(textStore->RequestLock(TS_LF_SYNC|TS_LF_READ,&session),"Fake read lock dispatches without a real IME");ok(session,"Isolated extent lock succeeds");
    const auto before=raster.stats();const auto gpuBefore=renderer.stats();const auto originalLayout=editor->layout().painted()->layoutIdentity();const auto fontFormats=raster.stats().textAnalysisFormatsCreated;
    allocations=0;counting=true;
    try{for(unsigned frame=0;frame<120;++frame){core::Matrix4 workspace;workspace.values[3]=frame*.0000006;workspace.values[7]=-frame*.0000003;workspace.values[12]=frame*.03;
        card.updatePose(workspace,.75f,2+frame/60.);pose.localToScreen=workspace*core::Matrix4::translation(30+slot.localRect.x,40+slot.localRect.y);pose.opacity=.75f;editor->setPose(pose);composition.present(renderer);
    }}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"120 integrated Notes closing/pointer frames allocate no C++ storage");
    check(raster.stats().rasterizations==before.rasterizations&&raster.stats().textLayoutsCreated==before.textLayoutsCreated&&raster.stats().textAnalysisFormatsCreated==fontFormats&&renderer.stats().textureUploads==gpuBefore.textureUploads,"Shared Notes closing pose changes neither font measurement nor raster/layout/texture resources");
    check(editor->layout().painted()->layoutIdentity()==originalLayout,"Closing tilt retains original painted editor object");
    const auto projectedCaret=text::projectedRange(document,editor->layout(),{0,0},editor->placement());check(projectedCaret&&projectedCaret->clientBounds.x!=first->bounds.x+30+slot.localRect.x,"Candidate follows current tilted/moving Notes plane during closing");
    ok(textStore->RequestLock(TS_LF_SYNC|TS_LF_READ,&session),"Tilted fake candidate query uses same shared geometry");ok(session,"Tilted candidate extent query succeeds");
    editor->command(gpu::ProjectedEditorCommand::documentEnd);editor->character('\n');editor->character(0x2605,true);editor->syncContent();editor->setPose(pose);composition.upload(renderer);composition.present(renderer);
    const auto changed=utf8(document.text());check(changed==initial+"\n★","Actual projected editor edits reach caller document without changing existing CJK/emoji bytes");
    ok(textStore->UnadviseSink(sink.Get()),"Fake sink detaches before field lifetime ends");ok(editor->stop(),"Editor stops without activating a real IME");
    check(state.finishEditing(changed)&&!state.editing()&&state.unsaved().empty(),"Owner commits plain draft through original NotesState persistence callback");
    measured=measurer.measure(id,2,changed,192);input.measured=measured->presentationText(measured);presentation.updateContent(state,input);card.syncContent();card.updatePose({},1,5);
    composition.setScenes(renderer,settledOrder);composition.present(renderer);editor.reset();check(!card.externalEditorSlot()&&card.externalEditorAfterDraws().empty()&&renderer.stats().textures==6,"Leaving edit removes live field/frame only after one safe combined publication");
    {data::NotesStore reopened(root.path);check(reopened.notes().size()==1&&reopened.notes()[0].text==changed&&reopened.notes()[0].createdAt==123,"Fresh injected SQLite reopen preserves edited bytes and original Foundation timestamp");}
    check(saves==1,"Only the explicit changed edit writes through injected persistence");
    state.setPresentation(false);presentation.updateContent(state,input);card.syncContent();composition.upload(renderer);core::Matrix4 outgoing;outgoing.values[3]=.0001;outgoing.values[12]=7;
    const auto settledRaster=raster.stats();card.updatePose(outgoing,.6f,6,true);composition.present(renderer);check(std::abs(card.scene().draws()[0].opacity-.6f)<1e-6f&&card.scene().draws()[0].world.values[12]==37,"Caller sampled outgoing unpinned card remains tilted/visible while target is hidden");
    card.updatePose(outgoing,.6f,6.3);composition.present(renderer);check(card.scene().draws()[0].opacity==0,"No override settles target-hidden unpinned note");
    check(raster.stats().rasterizations==settledRaster.rasterizations,"Outgoing visibility override changes no content raster");
    state.setPresentation(true);state.togglePin(id);presentation.updateContent(state,input);card.syncContent();card.updatePose({},1,7);composition.upload(renderer);state.setPresentation(false);presentation.updatePlacement(state);card.updatePose(outgoing,.8f,8);composition.present(renderer);
    check(std::abs(card.scene().draws()[0].opacity-.8f)<1e-6f,"Pinned Notes remains visible on other sections without any override");
    composition.detach(renderer);check(renderer.stats().textures==0&&renderer.stats().meshes==0,"Integrated Notes owner releases its own resources after detaching all references");
    // Explicit current boundary: complete storage/measurement remains valid,
    // but the short plain editor does not silently truncate a longer document.
    text::Buffer longDocument(std::u16string(65537,u'a'));gpu::LayerScene longScene(raster);
    rejects([&]{gpu::NativeProjectedEditor rejected(hwnd,longDocument,longScene,style,rasterOptions,gpu::PlainEditorFixtureCapacity{65536},WM_APP+181,2);},"Long editor fixture capacity rejects instead of flattening/truncating stored text");
    check(longDocument.text().size()==65537&&longScene.contentRevision()==0,"Rejected long field preserves complete owner text and empty scene");
    auto rich=note;rich.id="00000000-0000-4000-8000-000000000002";rich.richText="{\"version\":1,\"runs\":[]}";mod::NotesState richState({rich},{[](const auto&){},[](auto){}});richState.setWorkspaceBounds({0,0,640,360});
    rejects([&]{richState.beginEditing(rich.id);},"Rich Notes still rejects the plain-editor fixture explicitly");check(richState.note(rich.id)->richText==rich.richText,"Rich payload is left intact for the later style-preserving adapter");
}
}
int wmain(int argc,wchar_t**argv){try{check(argc==2,"Pass original native HUD shader path");ok(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Owned fixture COM apartment initializes");
    {Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,640,360,{gpu::Driver::warpForTests,argv[1],gpu::RenderTarget::offscreenForTests});run(window.hwnd,renderer);check(!IsWindowVisible(window.hwnd),"Integrated workspace test never shows its owned window");renderer.reset();}
    CoUninitialize();std::cout<<"Native Notes workspace contracts: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception&e){counting=false;std::cerr<<"Native Notes workspace contract failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#endif
