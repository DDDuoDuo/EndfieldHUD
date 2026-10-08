// Isolated owner-drawn editor feasibility gate. Visible TSF activation is
// explicitly opt-in. Hidden fixtures use a fake sink and synthetic text only.
#include "app/overlay_host.hpp"
#include "native/layer_raster.hpp"
#include "native/layer_text_layout.hpp"
#include "native/projected_text_input.hpp"
#include "native/renderer.hpp"
#include "native/layer_scene.hpp"
#include "core/source_camera.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>
#ifdef _WIN32
#include <ocidl.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
namespace app=endfield::app;
namespace core=endfield::core;
namespace text=core::text;
namespace gpu=endfield::native;
using ehud::data::Json;
namespace {
constexpr UINT changesMessage=WM_APP+83;
constexpr UINT_PTR fieldGeneration=0x20261007;
constexpr core::Rect planeBounds{0,0,860,380},textBounds{18,20,824,340};
void need(bool value,const char*why){if(!value)throw std::runtime_error(why);}
void checked(HRESULT value,const char*why){if(FAILED(value))throw std::runtime_error(std::string(why)+" (HRESULT "+std::to_string(static_cast<std::uint32_t>(value))+")");}
struct COM {COM(){checked(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Initialize owned COM apartment");}~COM(){CoUninitialize();}};
struct Manager {ComPtr<ITfThreadMgr>value;TfClientId client{TF_CLIENTID_NULL};bool active{};
    void start(){checked(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&value)),"Create one shared TSF manager");checked(value->Activate(&client),"Activate explicit live TSF test");active=true;}
    ~Manager(){if(active)value->Deactivate();}
};
struct Options {fs::path shader,output;bool visible{},warp{},tsfSetup{},diagnostics{};};
Options options(int argc,wchar_t**argv){
    need(argc>=3,"Usage: projected_editor_preview hud.hlsl --visible | --fixture new-output-directory [--warp] | --tsf-setup new-output-directory [--warp] [--diagnostics]");Options o;o.shader=fs::absolute(argv[1]);
    for(int i=2;i<argc;++i){const std::wstring_view a=argv[i];if(a==L"--visible"){need(!o.visible,"Duplicate visible mode");o.visible=true;}
        else if(a==L"--fixture"&&i+1<argc){need(o.output.empty(),"Duplicate fixture output");o.output=fs::absolute(argv[++i]);}
        else if(a==L"--tsf-setup"&&i+1<argc){need(o.output.empty(),"Duplicate fixture output");o.output=fs::absolute(argv[++i]);o.tsfSetup=true;}
        else if(a==L"--warp"){need(!o.warp,"Duplicate WARP mode");o.warp=true;}
        else if(a==L"--diagnostics"){need(!o.diagnostics,"Duplicate diagnostics mode");o.diagnostics=true;}else need(false,"Unknown or incomplete editor argument");}
    need(o.visible!=!o.output.empty(),"Choose exactly one explicit visible or hidden fixture mode");need(!o.visible||!o.warp,"WARP is hidden-fixture only");
    need(o.output.empty()||!fs::exists(o.output),"Fixture output must be a new directory");return o;
}
std::string utf8(std::u16string_view input){
    if(input.empty())return {};const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(input.data()),static_cast<int>(input.size()),nullptr,0,nullptr,nullptr);need(count>0,"Synthetic UTF-16 conversion failed");std::string result(count,'\0');need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(input.data()),static_cast<int>(input.size()),result.data(),count,nullptr,nullptr)==count,"Synthetic UTF-16 conversion changed size");return result;
}
bool high(char16_t c){return c>=0xd800&&c<=0xdbff;}bool low(char16_t c){return c>=0xdc00&&c<=0xdfff;}
std::uint32_t previous(std::u16string_view s,std::uint32_t at){if(!at)return 0;--at;if(at&&low(s[at])&&high(s[at-1]))--at;return at;}
std::uint32_t next(std::u16string_view s,std::uint32_t at){if(at>=s.size())return static_cast<std::uint32_t>(s.size());const bool pair=high(s[at])&&at+1<s.size()&&low(s[at+1]);return at+(pair?2u:1u);}
Json descriptor(std::u16string_view value){return Json::Object{{"id","synthetic-editor"},{"class","CATextLayer"},{"kind","text"},{"bounds",Json::Array{textBounds.x,textBounds.y,textBounds.width,textBounds.height}},{"anchorPoint",Json::Array{0,0}},{"position",Json::Array{0,0}},{"opacity",1},{"children",Json::Array{}},{"text",Json::Object{{"string",utf8(value)},{"fontSize",28},{"font",Json::Object{{"familyName","Segoe UI"},{"postScriptName","SegoeUI"},{"pointSize",28}}},{"foregroundColor",Json::Object{{"sRGB",Json::Array{.96,.96,.96,1}}}},{"alignment","left"},{"wrapped",true},{"truncation","none"},{"runs",Json::Array{}}}}};}
void appendQuad(std::vector<gpu::Vertex>&v,std::vector<std::uint32_t>&i,core::Rect r){const auto start=static_cast<std::uint32_t>(v.size());
    for(const auto&p:std::array<core::Point,4>{{{r.x,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height},{r.x,r.y+r.height}}})v.push_back({{float(p.x),float(p.y),0},{0,0},{1,1,1,1}});
    for(const auto index:std::array<std::uint32_t,6>{0,1,2,0,2,3})i.push_back(start+index);
}
struct Sink final:ITextStoreACPSink {
    std::atomic<ULONG>refs{1};std::function<HRESULT(DWORD)>lock;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown)&&id!=__uuidof(ITextStoreACPSink))return E_NOINTERFACE;*out=static_cast<ITextStoreACPSink*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE OnLockGranted(DWORD flags)override{return lock?lock(flags):S_OK;}
    HRESULT STDMETHODCALLTYPE OnTextChange(DWORD,const TS_TEXTCHANGE*)override{return S_OK;}HRESULT STDMETHODCALLTYPE OnSelectionChange()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLayoutChange(TsLayoutCode,TsViewCookie)override{return S_OK;}HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnAttrsChange(LONG,LONG,ULONG,const TS_ATTRID*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStartEditTransaction()override{return S_OK;}HRESULT STDMETHODCALLTYPE OnEndEditTransaction()override{return S_OK;}
};
class Editor final {
    const Options&args_;Manager manager_;gpu::LayerRasterizer raster_;app::OverlayHost host_;gpu::Renderer renderer_;
    text::Buffer document_{u"EndfieldHUD projected editor\n中文：终末地  日本語：こんにちは  한국어：안녕하세요  😀",8192};
    std::unique_ptr<gpu::LayerTextLayout>layout_;std::unique_ptr<gpu::ProjectedTextInput>input_;app::ClientMetrics metrics_;
    text::Placement placement_;core::Matrix4 world_;bool ready_{},dragging_{},windowFocused_{},firstPresentLogged_{};std::uint32_t anchor_{};std::optional<char16_t>highUnit_;
    std::uint64_t paintedRevision_{},adornmentRevision_{1};text::Selection drawnSelection_;std::optional<text::Range>drawnComposition_;std::uint64_t drawnTextRevision_{};
    std::vector<gpu::DrawObject>draws_;std::vector<gpu::Vertex>vertices_;std::vector<std::uint32_t>indices_;
    void diagnostic(const char*stage){if(!args_.diagnostics)return;const auto window=static_cast<HWND>(host_.hwnd());RECT rect{},client{};
        const bool rectKnown=window&&GetWindowRect(window,&rect),clientKnown=window&&GetClientRect(window,&client);const auto host=host_.stats();const auto gpu=renderer_.stats();const auto raster=raster_.stats();
        Json projected;const auto bounds=text::projectedViewport(placement_);if(bounds)projected=Json::Array{bounds->x,bounds->y,bounds->width,bounds->height};
        Json value=Json::Object{{"editorStage",stage},{"ready",ready_},{"ownerWindow",std::int64_t(reinterpret_cast<std::uintptr_t>(window))},{"windowVisible",window&&IsWindowVisible(window)!=FALSE},{"ownerIsForeground",window&&GetForegroundWindow()==window},{"ownerHasKeyboardFocus",window&&GetFocus()==window},{"tsfFocused",input_&&input_->focused()},{"windowRectKnown",rectKnown},{"windowRect",Json::Array{int(rect.left),int(rect.top),int(rect.right),int(rect.bottom)}},{"clientRectKnown",clientKnown},{"clientRect",Json::Array{int(client.left),int(client.top),int(client.right),int(client.bottom)}},{"pixelWidth",int(metrics_.pixelWidth)},{"pixelHeight",int(metrics_.pixelHeight)},{"DIPWidth",metrics_.width},{"DIPHeight",metrics_.height},{"scale",metrics_.scale},{"projectedTextBounds",projected},{"framePending",host.framePending},{"hostFrames",std::int64_t(host.frames)},{"hostTimerArmed",host.timerArmed},{"rendererInitialized",gpu.initialized},{"renderObjects",std::int64_t(gpu.objects)},{"drawCalls",std::int64_t(gpu.drawCalls)},{"presents",std::int64_t(gpu.presents)},{"localRasterizations",std::int64_t(raster.rasterizations)},{"paintedTextLayouts",std::int64_t(raster.textLayoutsCreated)}};
        std::cout<<value.encode()<<std::endl;
    }
    void quad(const char*id,core::Rect r,std::uint64_t revision){vertices_.clear();indices_.clear();appendQuad(vertices_,indices_,r);renderer_.setMesh(id,revision,{vertices_,indices_});}
    void adornment(const char*id,text::Range range,bool underline,std::uint64_t revision){vertices_.clear();indices_.clear();
        for(auto r:layout_->selectionRectangles(range)){r.x+=textBounds.x;r.y+=textBounds.y;if(underline){r.y+=std::max(0.0,r.height-2);r.height=2;}appendQuad(vertices_,indices_,r);}
        if(vertices_.empty())appendQuad(vertices_,indices_,{0,0,1,1});renderer_.setMesh(id,revision,{vertices_,indices_});}
    void syncAdornments(){const auto selection=document_.selection();const auto composition=document_.composition();if(drawnTextRevision_==document_.revision()&&selection==drawnSelection_&&composition==drawnComposition_)return;
        ++adornmentRevision_;drawnTextRevision_=document_.revision();drawnSelection_=selection;drawnComposition_=composition;
        adornment("editor-selection",selection.range,false,adornmentRevision_);draws_[2].opacity=selection.range.start!=selection.range.end?1.f:0.f;
        adornment("editor-composition",composition.value_or(text::Range{}),true,adornmentRevision_);draws_[4].opacity=composition?1.f:0.f;
        const auto at=selection.activeEnd==text::ActiveEnd::start?selection.range.start:selection.range.end;
        adornment("editor-caret",{at,at},false,adornmentRevision_);draws_[5].opacity=windowFocused_&&selection.range.start==selection.range.end?1.f:0.f;
    }
    void syncText(){if(paintedRevision_!=document_.revision()){
        gpu::LayerRasterOptions o;o.pixelsPerPoint=2;o.paddingPoints=0;o.retainEmptyTextLayout=true;
        const auto image=raster_.rasterize("editor",document_.revision(),descriptor(document_.text()),o);need(image->complete(),"Editor leaf has unsupported drawing state");
        const auto painted=raster_.textLayout("editor",document_.revision());need(bool(painted),"Exact painted editor handle missing");
        if(layout_)layout_->bind(painted,document_);else layout_=std::make_unique<gpu::LayerTextLayout>(painted,document_);
        renderer_.setTexture("editor-glyphs",document_.revision(),{image->width,image->height,image->straightRGBA,gpu::TextureColorSpace::sRGB,gpu::TextureFilter::linear});
        // UVs address the complete bitmap; selection/caret coordinates come
        // from the very same DWrite content box and never from this mesh.
        const auto&r=image->bounds;const std::array<gpu::Vertex,4>v{{{{float(r.x),float(r.y),0},{0,0},{1,1,1,1}},{{float(r.x+r.width),float(r.y),0},{1,0},{1,1,1,1}},{{float(r.x+r.width),float(r.y+r.height),0},{1,1},{1,1,1,1}},{{float(r.x),float(r.y+r.height),0},{0,1},{1,1,1,1}}}};constexpr std::array<std::uint32_t,6>i{0,1,2,0,2,3};
        renderer_.setMesh("editor-glyphs",document_.revision(),{v,i});paintedRevision_=document_.revision();if(input_)checked(input_->layoutChanged(),"Notify exact edited text layout");}
        syncAdornments();
    }
    void pose(double x,double y){
        core::Matrix4 perspective;perspective.values[11]=-1./1000;
        world_=core::Matrix4::scale(metrics_.scale,metrics_.scale)*core::Matrix4::translation(metrics_.width/2,metrics_.height/2)*perspective*core::Matrix4::rotation(y,x,0)*core::Matrix4::translation(-planeBounds.width/2,-planeBounds.height/2);
        const auto camera=gpu::layerViewportProjection(metrics_.pixelWidth,metrics_.pixelHeight);renderer_.setCamera(camera);
        placement_={core::Projection::viewport(camera*world_,metrics_.pixelWidth,metrics_.pixelHeight),textBounds,{0,0},true};
        const auto inverse=core::source::inverseSourceMatrix(world_);for(auto&d:draws_){d.world=world_;for(auto&m:d.masks){m.worldToLocal=inverse;m.bounds=textBounds;}}
        if(input_)checked(input_->setPlacement(placement_),"Update shared editor plane");
    }
    void present(bool submit){syncText();renderer_.setDrawList(draws_);renderer_.draw(submit);if(submit&&!firstPresentLogged_){firstPresentLogged_=true;diagnostic("first-present");}}
    void repaint(){if(args_.visible)host_.invalidate();}
    void replace(text::Range range,std::u16string_view value){if(document_.composition())return;const auto result=input_->replaceFromHost(range,value);if(result==E_INVALIDARG)return;checked(result,"Edit synthetic host document");syncText();repaint();}
    void select(std::uint32_t target,bool extend){if(document_.composition())return;const auto current=document_.selection();if(!extend)anchor_=target;
        else anchor_=current.activeEnd==text::ActiveEnd::start?current.range.end:current.range.start;
        checked(input_->selectFromHost({{std::min(anchor_,target),std::max(anchor_,target)},target<anchor_?text::ActiveEnd::start:text::ActiveEnd::end,false}),"Select synthetic host document");syncAdornments();repaint();}
    void focusInput(){if(!args_.visible||!input_)return;const auto hr=windowFocused_?input_->focus():input_->blur();if(hr==TS_E_NOLOCK){PostMessageW(static_cast<HWND>(host_.hwnd()),changesMessage,fieldGeneration,0);return;}checked(hr,"Change owned editor focus");}
    bool key(const app::KeyEvent&e){if(!ready_)return false;
        if(e.kind==app::KeyKind::down){
            if(e.value==VK_ESCAPE){if(document_.composition()){checked(input_->cancelComposition(),"Cancel live composition");syncText();repaint();}else{host_.hide();host_.requestStop();}return true;}
            if(document_.composition())return false;const bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0,control=(GetKeyState(VK_CONTROL)&0x8000)!=0;
            auto r=document_.selection().range;const auto current=document_.selection().activeEnd==text::ActiveEnd::start?r.start:r.end;
            if(control&&e.value=='A'){anchor_=0;checked(input_->selectFromHost({{0,static_cast<std::uint32_t>(document_.text().size())},text::ActiveEnd::end,false}),"Select all synthetic text");syncAdornments();repaint();return true;}
            if(e.value==VK_LEFT||e.value==VK_RIGHT){const auto at=!shift&&r.start!=r.end?(e.value==VK_LEFT?r.start:r.end):(e.value==VK_LEFT?previous(document_.text(),current):next(document_.text(),current));select(at,shift);return true;}
            if(e.value==VK_HOME||e.value==VK_END){select(e.value==VK_HOME?0u:static_cast<std::uint32_t>(document_.text().size()),shift);return true;}
            if(e.value==VK_BACK||e.value==VK_DELETE){if(r.start==r.end){if(e.value==VK_BACK)r.start=previous(document_.text(),r.start);else r.end=next(document_.text(),r.end);}if(r.start!=r.end)replace(r,{});return true;}
            if(e.value==VK_UP||e.value==VK_DOWN){const auto bounds=layout_->bounds({current,current});if(bounds){const auto&r2=bounds->bounds;const auto hit=layout_->hit({r2.x,r2.y+(e.value==VK_UP?-.5:1.5)*r2.height},true,true);if(hit)select(*hit,shift);}return true;}
            return false;
        }
        if(e.kind!=app::KeyKind::character&&e.kind!=app::KeyKind::unicodeCharacter)return false;
        if(document_.composition()||e.alt||(GetKeyState(VK_CONTROL)&0x8000))return true;
        const auto value=e.value;if(value<32&&value!='\r'&&value!='\n')return true;
        std::u16string units;
        if(e.kind==app::KeyKind::unicodeCharacter){if(value>0x10ffff||(value>=0xd800&&value<=0xdfff))return true;if(value>0xffff){const auto n=value-0x10000;units={char16_t(0xd800+(n>>10)),char16_t(0xdc00+(n&1023))};}else units.push_back(char16_t(value));highUnit_.reset();}
        else{const auto unit=char16_t(value);if(high(unit)){highUnit_=unit;return true;}if(low(unit)){if(!highUnit_)return true;units={*highUnit_,unit};highUnit_.reset();}else{highUnit_.reset();units.push_back(unit=='\r'?u'\n':unit);}}
        replace(document_.selection().range,units);return true;
    }
public:
    explicit Editor(const Options&o):args_(o){
        draws_.reserve(6);vertices_.reserve(256);indices_.reserve(384);
        for(const char*id:{"editor-plane","editor-border","editor-selection","editor-glyphs","editor-composition","editor-caret"})draws_.push_back({id,id,id==std::string_view("editor-glyphs")?id:"",{}, {1,1,1,1},1,{}});
        draws_[0].linearTint={.018f,.018f,.023f,.96f};draws_[1].linearTint={.9f,.72f,.01f,1};draws_[2].linearTint={.18f,.28f,.48f,.7f};draws_[4].linearTint={.95f,.75f,.015f,1};
        for(std::size_t i=2;i<draws_.size();++i)draws_[i].masks.push_back({{},textBounds});
        app::OverlayCallbacks cb;cb.frame=[&](double){if(ready_)present(true);};
        cb.resize=[&](const auto&m){metrics_=m;if(ready_&&m.pixelWidth&&m.pixelHeight){renderer_.resize(m.pixelWidth,m.pixelHeight);pose(0,0);repaint();}};
        cb.focus=[&](bool focused){windowFocused_=focused;highUnit_.reset();if(ready_){focusInput();drawnTextRevision_=0;syncText();repaint();}};
        cb.beforeKeyTranslation=[&](const app::NativeMessage&m){if(!ready_||!input_)return false;if(m.message==WM_KEYDOWN&&m.wParam==VK_ESCAPE&&document_.composition()){checked(input_->cancelComposition(),"Cancel live IME composition");syncText();repaint();return true;}return input_->filterKeyMessage(m.message,m.wParam,m.lParam);};
        cb.appMessage=[&](const app::NativeMessage&m)->std::optional<std::intptr_t>{if(m.message!=changesMessage)return {};if(ready_&&m.wParam==fieldGeneration){input_->takeChanges(m.wParam);focusInput();syncText();repaint();}return 0;};
        cb.key=[&](const auto&e){return key(e);};cb.closeRequested=[&]{if(input_)checked(input_->cancelComposition(),"Cancel before closing test");host_.hide();host_.requestStop();};
        cb.pointer=[&](const app::PointerEvent&e){if(!ready_)return false;
            if(e.kind==app::PointerKind::captureLost){dragging_=false;return true;}
            const core::Point physical{e.x*metrics_.scale,e.y*metrics_.scale};
            if(e.kind==app::PointerKind::move){pose(std::clamp((e.x/metrics_.width-.5)*.28,-.14,.14),std::clamp((.5-e.y/metrics_.height)*.22,-.11,.11));if(dragging_){const auto at=text::projectedHit(document_,*layout_,physical,placement_,true,true);if(at)select(*at,true);}repaint();return true;}
            if(e.button==app::PointerButton::left&&e.kind==app::PointerKind::down){const auto at=text::projectedHit(document_,*layout_,physical,placement_,false,true);if(at&&!document_.composition()){SetFocus(static_cast<HWND>(host_.hwnd()));select(*at,(e.modifiers&MK_SHIFT)!=0);dragging_=true;SetCapture(static_cast<HWND>(host_.hwnd()));return true;}}
            if(e.button==app::PointerButton::left&&e.kind==app::PointerKind::up){dragging_=false;if(GetCapture()==static_cast<HWND>(host_.hwnd()))ReleaseCapture();return true;}return false;};
        host_.create({L"EndfieldHUD projected text / IME gate — synthetic text only",100,100,1280,800,nullptr},std::move(cb));metrics_=host_.metrics();
        diagnostic("owned-window-created");
        renderer_.initialize(host_.hwnd(),metrics_.pixelWidth,metrics_.pixelHeight,{args_.warp?gpu::Driver::warpForTests:gpu::Driver::hardware,args_.shader,args_.visible?gpu::RenderTarget::composition:gpu::RenderTarget::offscreenForTests});
        diagnostic("renderer-initialized");
        quad("editor-plane",planeBounds,1);vertices_.clear();indices_.clear();for(const auto&r:std::array<core::Rect,4>{{{0,0,860,1},{0,379,860,1},{0,0,1,380},{859,0,1,380}}})appendQuad(vertices_,indices_,r);renderer_.setMesh("editor-border",1,{vertices_,indices_});
        syncText();input_=std::make_unique<gpu::ProjectedTextInput>(static_cast<HWND>(host_.hwnd()),document_,*layout_,changesMessage,fieldGeneration);pose(0,0);
        if(args_.tsfSetup){placement_.visible=false;checked(input_->setPlacement(placement_),"Hide actual unfocused TSF view");}
        if(args_.visible||args_.tsfSetup){manager_.start();checked(input_->connect(*manager_.value.Get(),manager_.client),"Connect real TSF to painted owner field");}
        ready_=true;
        diagnostic("painted-editor-ready");
    }
    ~Editor(){ready_=false;input_.reset();renderer_.reset();try{host_.destroy();}catch(const std::exception&e){std::cerr<<"Owned editor cleanup: "<<e.what()<<'\n';}}
    void visible(){std::cout<<"Synthetic projected editor only. Click/drag to select. Type English, Chinese, Japanese, Korean, or emoji using your selected Windows input method. Pointer movement tilts the same glyph/caret/selection plane. Escape cancels composition; Escape again closes. Ctrl+A selects all. No clipboard or persistent data.\n";
        core::FrameDemand demand;demand.phase=core::VisibilityPhase::visible;demand.presented=demand.onScreen=true;host_.setFrameDemand(demand);host_.show();windowFocused_=host_.stats().focused;focusInput();drawnTextRevision_=0;syncText();repaint();diagnostic("visible-request-complete");host_.run();}
    void fixture(){
        need(fs::create_directory(args_.output),"Cannot create new explicit fixture directory");ehud::data::detail::validateRoot(args_.output);
        if(args_.tsfSetup){
            // Actual thread-local service lifecycle, without an input focus,
            // profile switch, simulated key, composition or visible window.
            HWND owner{};TsViewCookie view{};checked(input_->textStore()->GetWnd(gpu::ProjectedTextInput::viewCookie,&owner),"Read real connected TSF owner");checked(input_->textStore()->GetActiveView(&view),"Read real connected TSF view");
            need(owner==static_cast<HWND>(host_.hwnd())&&view==gpu::ProjectedTextInput::viewCookie,"Real TSF connection lost owned view identity");
            for(unsigned i=0;i<8;++i)host_.pumpOnce(0);need(!input_->focused()&&!IsWindowVisible(owner)&&!host_.stats().timerArmed&&host_.stats().frames==0,"TSF setup check unexpectedly focused, displayed or scheduled the owner");
            need(document_.revision()==1&&!document_.composition(),"Unfocused TSF setup mutated synthetic text");checked(input_->stop(),"Detach actual TSF context without focus");input_.reset();checked(manager_.value->Deactivate(),"Deactivate actual thread-local TSF service");manager_.active=false;
            Json report=Json::Object{{"scope","Actual thread-local TSF activation, connected owner view and teardown only; real IME typing not verified"},{"visible",false},{"ownerFocused",false},{"userDataRead",false},{"clipboardRead",false},{"keyboardSimulated",false},{"IMEProfileChanged",false},{"desktopCaptured",false},{"actualTSFActivated",true},{"actualTSFConnected",true},{"actualTSFDeactivated",true},{"limitations",Json::Array{"No input focus, key routing, CJK composition or candidate window tested","Run explicit visible mode to verify the installed Windows input methods"}}};
            ehud::data::detail::replaceFile(args_.output/"report.json",std::nullopt,report.encode(),1024*1024);std::cout<<"Actual hidden TSF setup/teardown passed; live IME remains unverified.\n";return;
        }
        ComPtr<Sink>sink;sink.Attach(new Sink);auto*store=input_->textStore();checked(store->AdviseSink(__uuidof(ITextStoreACPSink),sink.Get(),TS_AS_TEXT_CHANGE|TS_AS_SEL_CHANGE|TS_AS_LAYOUT_CHANGE),"Install isolated fake sink");
        auto locked=[&](std::function<HRESULT(DWORD)>operation){sink->lock=std::move(operation);HRESULT session{};checked(store->RequestLock(TS_LF_READ,&session),"Request isolated text lock");checked(session,"Complete isolated text lock");sink->lock={};};
        Json::Array states;const std::array<core::Point,3>poses{{{0,0},{.12,-.09},{-.11,.08}}};unsigned ordinal{};
        for(const auto&p:poses){pose(p.x,p.y);windowFocused_=true;drawnTextRevision_=0;checked(input_->selectFromHost({{34,37},text::ActiveEnd::end,false}),"Select synthetic CJK fixture");present(false);
            const auto caret=text::projectedRange(document_,*layout_,{37,37},placement_);need(caret.has_value(),"Projected caret missing");RECT tsf{};BOOL clipped{};LONG hit=-1;
            const auto glyph=layout_->selectionRectangles({34,35}).front();const auto local=core::Point{textBounds.x+glyph.x+glyph.width*.25,textBounds.y+glyph.y+glyph.height*.5};const auto client=placement_.projection.project(local);need(client.has_value(),"Synthetic glyph projection failed");POINT screen{LONG(std::lround(client->x)),LONG(std::lround(client->y))};need(ClientToScreen(static_cast<HWND>(host_.hwnd()),&screen)!=0,"Map synthetic hit to screen");
            locked([&](DWORD){checked(store->GetTextExt(gpu::ProjectedTextInput::viewCookie,37,37,&tsf,&clipped),"Read projected TSF caret from painted layout");checked(store->GetACPFromPoint(gpu::ProjectedTextInput::viewCookie,&screen,GXFPF_ROUND_NEAREST,&hit),"Hit painted glyph through TSF");return S_OK;});
            POINT origin{0,0};need(ClientToScreen(static_cast<HWND>(host_.hwnd()),&origin)!=0,"Map owned client origin");const auto&r=caret->clientBounds;
            need(tsf.left==LONG(std::floor(r.x))+origin.x&&tsf.top==LONG(std::floor(r.y))+origin.y&&tsf.right==LONG(std::ceil(r.x+r.width))+origin.x&&tsf.bottom==LONG(std::ceil(r.y+r.height))+origin.y,"TSF caret differs from rendered shared projection");need(hit==34,"TSF glyph hit differs from the exact painted cluster");
            const auto image=renderer_.readback();const auto file="pose-"+std::to_string(ordinal++)+".raw-bgra.bin";ehud::data::detail::replaceFile(args_.output/file,std::nullopt,std::string(reinterpret_cast<const char*>(image.pixels.data()),image.pixels.size()),32*1024*1024);
            states.push_back(Json::Object{{"file",file},{"width",int(image.width)},{"height",int(image.height)},{"rowBytes",int(image.rowBytes)},{"tilt",Json::Array{p.x,p.y}},{"tsfHitACP",int(hit)},{"tsfCaretScreen",Json::Array{int(tsf.left),int(tsf.top),int(tsf.right),int(tsf.bottom)}},{"clipped",clipped!=FALSE}});
        }
        const auto initialRaster=raster_.stats();const auto initialGPU=renderer_.stats();for(unsigned i=0;i<120;++i){pose(std::sin(i*.08)*.12,std::cos(i*.09)*.09);present(false);}
        const auto finalRaster=raster_.stats();const auto finalGPU=renderer_.stats();need(finalRaster.rasterizations==initialRaster.rasterizations&&finalRaster.textLayoutsCreated==initialRaster.textLayoutsCreated,"Pointer-only frames recreated painted text");need(finalGPU.textureUploads==initialGPU.textureUploads&&finalGPU.meshUploads==initialGPU.meshUploads&&finalGPU.objectBufferAllocations==initialGPU.objectBufferAllocations,"Pointer-only frames recreated native resources");
        checked(input_->replaceFromHost({0,0},u"测试 😀\n"),"Edit isolated UTF-16 buffer");syncText();pose(.04,-.02);present(false);need(layout_->painted()->text()==document_.text()&&layout_->textRevision()==document_.revision(),"Synthetic edited document lost exact painted text");
        for(unsigned i=0;i<8;++i)host_.pumpOnce(0);need(!IsWindowVisible(static_cast<HWND>(host_.hwnd()))&&!host_.stats().timerArmed&&host_.stats().frames==0,"Hidden fixture displayed or scheduled native frames");
        checked(store->UnadviseSink(sink.Get()),"Detach isolated fake sink");
        Json report=Json::Object{{"scope","Synthetic painted editor and fake-TSF geometry; real CJK IME not verified"},{"visible",false},{"userDataRead",false},{"clipboardRead",false},{"realTSFProfileActivated",false},{"desktopCaptured",false},{"driver",args_.warp?"WARP":"hardware"},{"samePaintedLayout",true},{"pointerSamples",120},{"pointerRasterizations",std::int64_t(finalRaster.rasterizations-initialRaster.rasterizations)},{"pointerTextLayouts",std::int64_t(finalRaster.textLayoutsCreated-initialRaster.textLayoutsCreated)},{"pointerTextureUploads",std::int64_t(finalGPU.textureUploads-initialGPU.textureUploads)},{"pointerMeshUploads",std::int64_t(finalGPU.meshUploads-initialGPU.meshUploads)},{"pointerObjectAllocations",std::int64_t(finalGPU.objectBufferAllocations-initialGPU.objectBufferAllocations)},{"hostTimers",host_.stats().timerArmed},{"states",states},{"limitations",Json::Array{"Plain synthetic Buffer only; rich host styles/undo remain caller-owned","Leaf viewport only, not a long-document scrolling layout","Real Chinese/Japanese/Korean composition and candidate placement require explicit visible testing"}}};
        ehud::data::detail::replaceFile(args_.output/"report.json",std::nullopt,report.encode(),1024*1024);std::cout<<"Wrote hidden painted editor fixture; real IME remains unverified.\n";
    }
};
}
int wmain(int argc,wchar_t**argv){try{const auto args=options(argc,argv);COM com;Editor editor(args);if(args.visible)editor.visible();else editor.fixture();return 0;}catch(const std::exception&e){std::cerr<<"Projected editor gate failed: "<<e.what()<<'\n';return 1;}}
#endif
