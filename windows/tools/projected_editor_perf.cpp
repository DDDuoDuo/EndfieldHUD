#include "native/projected_editor.hpp"
#include "native/notes_workspace.hpp"
#ifdef _WIN32
#include <objbase.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Explicit build-only diagnostic. Own hidden HWND, synthetic Document, fake
// TSF sink, WARP and offscreen target only. Never activates a text manager,
// reads input/clipboard/stores, presents, changes focus, or starts a timer.
namespace {
using namespace endfield;
using namespace endfield::native;
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
using Json=ehud::data::Json;
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void checked(HRESULT hr,const char* why){need(SUCCEEDED(hr),why);}
double us(Clock::time_point begin,Clock::time_point end){return std::chrono::duration<double,std::micro>(end-begin).count();}
struct Apartment {
    Apartment(){checked(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Initialize isolated COM apartment");}
    ~Apartment(){CoUninitialize();}
};
struct Window {
    HWND value{};
    Window(){value=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"EndfieldHUD hidden synthetic editor performance",WS_POPUP,0,0,1024,768,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);need(value!=nullptr,"Create owned hidden test window");need(!IsWindowVisible(value),"Performance fixture must stay hidden");}
    ~Window(){if(value)DestroyWindow(value);}
};
struct Sink final:ITextStoreACPSink {
    std::atomic<ULONG> refs{1};
    std::uint64_t text{},selection{},layout{},locks{};
    std::function<HRESULT(DWORD)> onLock;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IUnknown)&&id!=__uuidof(ITextStoreACPSink))return E_NOINTERFACE;
        *out=static_cast<ITextStoreACPSink*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto count=--refs;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE OnTextChange(DWORD,const TS_TEXTCHANGE*)override{++text;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnSelectionChange()override{++selection;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLayoutChange(TsLayoutCode,TsViewCookie)override{++layout;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnAttrsChange(LONG,LONG,ULONG,const TS_ATTRID*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLockGranted(DWORD flags)override{++locks;try{return onLock?onLock(flags):S_OK;}catch(...){return E_FAIL;}}
    HRESULT STDMETHODCALLTYPE OnStartEditTransaction()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnEndEditTransaction()override{return S_OK;}
};
enum class Work {hostCharacter,hostSelection,tsfReplacement,unchangedSync};
const char* name(Work work){switch(work){case Work::hostCharacter:return "host-character";case Work::hostSelection:return "host-selection";case Work::tsfReplacement:return "fake-tsf-replacement";case Work::unchangedSync:return "unchanged-sync";}return "invalid";}
Json summary(std::vector<double> values){
    need(!values.empty(),"Empty performance sample");std::sort(values.begin(),values.end());double total{};for(const auto value:values)total+=value;
    return Json::Object{{"unit","microseconds"},{"mean",total/static_cast<double>(values.size())},{"median",values[values.size()/2]},{"p95",values[(values.size()-1)*95/100]},{"max",values.back()}};
}
std::u16string synthetic(std::size_t length){
    static constexpr std::u16string_view pattern=u"Endfield 终末地 日本語 한국어 e\u0301 😀\n";
    std::u16string result;result.reserve(length);
    while(result.size()+pattern.size()<=length)result.append(pattern);
    result.append(length-result.size(),u'A');need(core::text::Buffer::validUTF16(result),"Invalid synthetic UTF-16");return result;
}
void drainOwnMessages(HWND owner,UINT message){MSG value{};while(PeekMessageW(&value,owner,message,message,PM_REMOVE)){};}
std::string utf8(std::u16string_view text){
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const WCHAR*>(text.data()),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    need(count>0,"Encode synthetic Notes fixture");std::string value(static_cast<std::size_t>(count),'\0');
    need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const WCHAR*>(text.data()),static_cast<int>(text.size()),value.data(),count,nullptr,nullptr)==count,"Complete synthetic Notes encoding");return value;
}
Json run(HWND hwnd,const std::filesystem::path& shader,Work work,std::size_t units,double width,double height,unsigned samples){
    constexpr UINT message=WM_APP+203;constexpr UINT_PTR generation=47;
    Renderer renderer;renderer.initialize(hwnd,1024,768,{Driver::warpForTests,shader,RenderTarget::offscreenForTests});
    LayerRasterizer raster;LayerScene scene(raster);core::text::Buffer document(synthetic(units),65536);
    LayerRasterOptions options;options.pixelsPerPoint=2;options.paddingPoints=1;
    auto analysis=raster.plainSystemTextAnalysis("perf.source-system-font",12,options);
    const auto& metrics=analysis->metrics();
    ProjectedEditorStyle style;style.width=width;style.height=height;style.fontSize=12;style.lineHeight=metrics.lineHeight;style.baseline=metrics.ascent;style.fontFamily=metrics.selectedFamily;style.fontFace="";style.cornerRadius=3;
    analysis.reset();
    const auto constructStart=Clock::now();
    NativeProjectedEditor editor(hwnd,document,scene,style,options,PlainEditorFixtureCapacity{65536},message,generation);
    const auto constructed=Clock::now();
    ComPtr<Sink> sink;sink.Attach(new Sink);auto* store=editor.textStore();
    checked(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_TEXT_CHANGE|TS_AS_SEL_CHANGE|TS_AS_LAYOUT_CHANGE),"Advise fake test sink");
    ProjectedEditorPose pose;pose.localToScreen=core::Matrix4::translation(20,20);pose.screenToClip=layerViewportProjection(1024,768);pose.pixelWidth=1024;pose.pixelHeight=768;pose.ownerFocused=true;
    editor.setPose(pose);LayerComposition composition;const std::array entries{LayerCompositionEntry{&scene,{}}};composition.setEntries(renderer,entries);renderer.setCamera(pose.screenToClip);composition.present(renderer);renderer.draw(false);
    // Warm the owned path and remove its coalesced setup notification. All
    // report data is prepared afterward and output only after timed work.
    editor.takeChanges(generation);drainOwnMessages(hwnd,message);
    auto compositionResourceRevision=scene.resourceRevision();
    const auto rasterBefore=raster.stats();const auto gpuBefore=renderer.stats();
    const std::array<std::uint64_t,4> sinkBefore{sink->text,sink->selection,sink->layout,sink->locks};
    std::array<std::vector<double>,5> durations;for(auto& data:durations)data.reserve(samples);
    unsigned syncChanges{};std::uint64_t paintedChanges{};auto lastPaint=editor.paintRevision();
    for(unsigned index=0;index<samples;++index){
        const auto start=Clock::now();
        if(work==Work::hostCharacter){const auto result=editor.character(index%2?'x':0x7EC8,true);need(result.handled&&result.changed,"Synthetic host edit failed");}
        else if(work==Work::hostSelection){const auto result=editor.command(index%2?ProjectedEditorCommand::left:ProjectedEditorCommand::right);need(result.handled&&result.changed,"Synthetic selection failed");}
        else if(work==Work::tsfReplacement){
            // A fake ACP write transaction replaces one ASCII unit at the very
            // start. This exercises TSF's snapshot/queued-notice path without
            // loading a language profile or borrowing a real IME context.
            const WCHAR replacement=index%2?L'Q':L'R';
            sink->onLock=[&](DWORD flags){if((flags&TS_LF_READWRITE)!=TS_LF_READWRITE)return E_FAIL;TS_TEXTCHANGE change{};return store->SetText(0,0,1,&replacement,1,&change);};
            HRESULT result{};checked(store->RequestLock(TS_LF_READWRITE,&result),"Request fake write transaction");checked(result,"Complete fake write transaction");sink->onLock={};
        }
        const auto edited=Clock::now();
        if(editor.syncContent())++syncChanges;editor.setPose(pose);
        const auto painted=Clock::now();
        // Actual preview publication checks both revisions, while a numerical
        // frame simply presents retained entries. No source sibling surfaces
        // are included: publication cost here is an editor-only lower bound.
        if(scene.resourceRevision()!=compositionResourceRevision){composition.setEntries(renderer,entries);compositionResourceRevision=scene.resourceRevision();}
        const auto uploaded=Clock::now();composition.present(renderer);renderer.draw(false);
        const auto drawn=Clock::now();
        // Take once just like the owner; a second queued revision sync must be
        // a no-op. Do not dispatch messages to a real manager or window UI.
        editor.takeChanges(generation);drainOwnMessages(hwnd,message);
        need(!editor.syncContent(),"Queued redundant sync regenerated editor content");
        durations[0].push_back(us(start,edited));durations[1].push_back(us(edited,painted));durations[2].push_back(us(painted,uploaded));durations[3].push_back(us(uploaded,drawn));durations[4].push_back(us(start,drawn));
        const auto nowPaint=editor.paintRevision();paintedChanges+=nowPaint-lastPaint;lastPaint=nowPaint;
    }
    const auto after=raster.stats();const auto gpu=renderer.stats();
    Json result=Json::Object{{"work",name(work)},{"initialUTF16Units",double(units)},{"finalUTF16Units",double(document.text().size())},{"viewport",Json::Array{width,height}},{"pixelsPerPoint",2},{"samples",double(samples)},{"timing",Json::Object{{"edit",summary(std::move(durations[0]))},{"syncAndPose",summary(std::move(durations[1]))},{"publicationAndUpload",summary(std::move(durations[2]))},{"offscreenDrawSubmit",summary(std::move(durations[3]))},{"ownerTotal",summary(std::move(durations[4]))}}},{"counters",Json::Object{{"rasterizations",double(after.rasterizations-rasterBefore.rasterizations)},{"textLayoutsCreated",double(after.textLayoutsCreated-rasterBefore.textLayoutsCreated)},{"textureUploads",double(gpu.textureUploads-gpuBefore.textureUploads)},{"meshUploads",double(gpu.meshUploads-gpuBefore.meshUploads)},{"objectUploads",double(gpu.objectUploads-gpuBefore.objectUploads)},{"drawCalls",double(gpu.drawCalls-gpuBefore.drawCalls)},{"syncChanges",double(syncChanges)},{"paintRevisionIncrements",double(paintedChanges)},{"sinkText",double(sink->text-sinkBefore[0])},{"sinkSelection",double(sink->selection-sinkBefore[1])},{"sinkLayout",double(sink->layout-sinkBefore[2])},{"sinkWriteLocks",double(sink->locks-sinkBefore[3])}}},{"retained",Json::Object{{"rasterBytes",double(after.resourceBytes)},{"rasterEntries",double(after.entries)},{"gpuResourceBytes",double(gpu.resourceBytes)}}}};
    composition.detach(renderer);checked(store->UnadviseSink(sink.Get()),"Unadvise isolated sink");
    const auto stopStart=Clock::now();checked(editor.stop(),"Stop isolated editor");const auto stopped=Clock::now();
    result["setup"]=Json::Object{{"editorConstructionMicroseconds",us(constructStart,constructed)},{"editorStopMicroseconds",us(stopStart,stopped)},{"activatedTSFManager",false},{"stopHasActiveComposition",false}};
    renderer.reset();drainOwnMessages(hwnd,message);return result;
}
Json workspaceRun(HWND hwnd,const std::filesystem::path&shader,std::size_t units,double width,double height,unsigned samples,bool changeText){
    constexpr const char* id="BCF3283C-A7B3-4210-B2F7-73067D0E62D4";
    Renderer renderer;renderer.initialize(hwnd,1024,768,{Driver::warpForTests,shader,RenderTarget::offscreenForTests});
    LayerRasterizer raster;ehud::data::Note note;note.id=id;note.text=utf8(synthetic(units));note.x=40;note.y=40;note.width=width+18;note.height=height+58;
    std::uint64_t saverInvocations{};
    modules::NotesState state({note},{[&](const ehud::data::Note&){++saverInvocations;},[](std::string_view){throw std::runtime_error("Performance fixture must not delete records");}});
    state.setWorkspaceBounds({0,0,1024,768});
    NativeNotesWorkspaceStyle style;style.palette=modules::NotesPalette::source(true,{.98,.83,.12,1});style.editor={{.12,.12,.12,1},style.palette.accent};
    NativeNotesWorkspaceOptions options;options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;
    const auto beginConstruction=Clock::now();NativeNotesWorkspace workspace(hwnd,state,raster,style,options);const auto constructed=Clock::now();
    NativeNotesWorkspacePose pose;pose.screenToClip=layerViewportProjection(1024,768);pose.pixelWidth=1024;pose.pixelHeight=768;pose.opacity=1;pose.time=100;pose.ownerFocused=true;
    workspace.updatePose(pose);LayerComposition composition;composition.setEntries(renderer,workspace.entries());composition.present(renderer);renderer.setCamera(pose.screenToClip);renderer.draw(false);
    const auto rasterBefore=raster.stats();const auto gpuBefore=renderer.stats();const auto measuresBefore=workspace.measurementStats();const auto workspaceBefore=workspace.stats();const auto savesBefore=saverInvocations;
    std::array<std::vector<double>,7> durations;for(auto& values:durations)values.reserve(samples);
    std::uint64_t layoutsEntry{},layoutsExit{},rastersEntry{},rastersExit{},texturesEntry{},texturesExit{};
    for(unsigned index=0;index<samples;++index){
        pose.time=101+index;workspace.updatePose(pose);
        const auto beforeEntry=raster.stats();const auto beforeEntryGPU=renderer.stats();const auto start=Clock::now();
        need(workspace.beginEditing(id),"Synthetic Notes editor entry failed");
        const auto entered=Clock::now();composition.setEntries(renderer,workspace.entries());composition.present(renderer);renderer.draw(false);
        const auto entryPublished=Clock::now();const auto afterEntry=raster.stats();const auto afterEntryGPU=renderer.stats();
        if(changeText){auto* editor=workspace.editor();need(editor!=nullptr,"Synthetic Notes field disappeared");need(editor->character(index%2?'x':0x7EC8,true).changed,"Synthetic Notes edit failed");workspace.syncEditor();composition.setEntries(renderer,workspace.entries());composition.present(renderer);renderer.draw(false);}
        const auto changed=Clock::now();const auto beforeExit=raster.stats();const auto beforeExitGPU=renderer.stats();
        const auto finished=workspace.finishEditing();need(finished.finished&&finished.saved,"Synthetic Notes exit failed");
        const auto exited=Clock::now();composition.setEntries(renderer,workspace.entries());need(workspace.collectRetired(renderer),"Synthetic Notes retired field remained referenced");composition.present(renderer);renderer.draw(false);
        const auto published=Clock::now();const auto afterExit=raster.stats();const auto afterExitGPU=renderer.stats();
        drainOwnMessages(hwnd,options.ownerMessage);
        durations[0].push_back(us(start,entered));durations[1].push_back(us(entered,entryPublished));durations[2].push_back(us(entryPublished,changed));durations[3].push_back(us(changed,exited));durations[4].push_back(us(exited,published));durations[5].push_back(us(start,published));durations[6].push_back(us(start,entryPublished));
        layoutsEntry+=afterEntry.textLayoutsCreated-beforeEntry.textLayoutsCreated;layoutsExit+=afterExit.textLayoutsCreated-beforeExit.textLayoutsCreated;rastersEntry+=afterEntry.rasterizations-beforeEntry.rasterizations;rastersExit+=afterExit.rasterizations-beforeExit.rasterizations;
        texturesEntry+=afterEntryGPU.textureUploads-beforeEntryGPU.textureUploads;texturesExit+=afterExitGPU.textureUploads-beforeExitGPU.textureUploads;
    }
    const auto rasterAfter=raster.stats();const auto gpuAfter=renderer.stats();const auto measuresAfter=workspace.measurementStats();const auto workspaceAfter=workspace.stats();
    Json result=Json::Object{{"work",changeText?"workspace-enter-character-exit":"workspace-enter-exit-unchanged"},{"initialUTF16Units",double(units)},{"viewport",Json::Array{width,height}},{"samples",double(samples)},{"workspaceConstructionMicroseconds",us(beginConstruction,constructed)},{"activatedTSFManager",false},{"actualPersistenceIO",false},{"timing",Json::Object{{"beginEditing",summary(std::move(durations[0]))},{"entryPublicationUploadAndDrawSubmit",summary(std::move(durations[1]))},{"characterSyncUploadAndDrawSubmit",summary(std::move(durations[2]))},{"finishEditing",summary(std::move(durations[3]))},{"exitPublicationRetirementAndDrawSubmit",summary(std::move(durations[4]))},{"ownerCycle",summary(std::move(durations[5]))},{"entryTotal",summary(std::move(durations[6]))}}},{"counters",Json::Object{{"entryTextLayouts",double(layoutsEntry)},{"exitTextLayouts",double(layoutsExit)},{"entryRasters",double(rastersEntry)},{"exitRasters",double(rastersExit)},{"entryTextureUploads",double(texturesEntry)},{"exitTextureUploads",double(texturesExit)},{"totalRasters",double(rasterAfter.rasterizations-rasterBefore.rasterizations)},{"totalTextureUploads",double(gpuAfter.textureUploads-gpuBefore.textureUploads)},{"measurements",double(measuresAfter.measurements-measuresBefore.measurements)},{"analysisLayoutsCreated",double(measuresAfter.analysisLayoutsCreated-measuresBefore.analysisLayoutsCreated)},{"cardContentUpdates",double(workspaceAfter.cardContentUpdates-workspaceBefore.cardContentUpdates)},{"editorContentUpdates",double(workspaceAfter.editorContentUpdates-workspaceBefore.editorContentUpdates)},{"mockSaverInvocations",double(saverInvocations-savesBefore)}}}};
    composition.detach(renderer);need(workspace.releaseResources(renderer),"Synthetic workspace cleanup retained GPU resources");renderer.reset();return result;
}
} // namespace
int wmain(int argc,wchar_t** argv){
    try{
        need(argc==2||argc==3,"Usage: projected_editor_perf <hud.hlsl> [samples:1..1000]");
        const auto requested=argc==3?std::stoul(argv[2]):80ul;need(requested>0&&requested<=1000,"Invalid bounded sample count");const auto samples=static_cast<unsigned>(requested);
        Apartment apartment;Window window;Json::Array reports,workspaces;
        for(const auto viewport:std::array<std::array<double,2>,2>{{{222,87},{602,242}}})for(const auto units:std::array<std::size_t,3>{128,4096,32768})for(const auto work:{Work::hostCharacter,Work::hostSelection,Work::tsfReplacement,Work::unchangedSync}){
            reports.push_back(run(window.value,std::filesystem::path(argv[1]),work,units,viewport[0],viewport[1],samples));
        }
        // Expensive enter/exit cases are intentionally bounded separately.
        // No filesystem-backed saver or activated TSF manager is involved.
        const auto workspaceSamples=std::min(samples,8u);
        for(const auto viewport:std::array<std::array<double,2>,2>{{{222,87},{602,242}}})for(const auto units:std::array<std::size_t,3>{128,4096,32768})for(const bool changed:{false,true})workspaces.push_back(workspaceRun(window.value,std::filesystem::path(argv[1]),units,viewport[0],viewport[1],workspaceSamples,changed));
        const Json output=Json::Object{{"schemaVersion",2},{"fixture","hidden-owned-synthetic-editor"},{"driver","WARP"},{"realIMEActivated",false},{"presented",false},{"sourceSiblingCount",0},{"gpuTiming","CPU submission only; no fence/readback in measured loop"},{"externalTSFCost","Not measured: no real manager/profile/service is activated"},{"results",std::move(reports)},{"workspaceResults",std::move(workspaces)}};
        std::cout<<output.encode()<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Projected editor performance fixture failed: "<<error.what()<<'\n';return 1;}
}
#else
#include <iostream>
int main(){std::cerr<<"Projected editor performance fixture requires Windows native libraries\n";return 2;}
#endif
