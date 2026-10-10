#ifdef _WIN32
#include "tools/media_assembly_preview.hpp"
#include "native/media_assembly_codec.hpp"
#include "native/media_assembly_accessibility.hpp"
#include <uiautomation.h>
#include "native/module_scene.hpp"
#include "native/notes_image_decoder.hpp"
#include "native/notes_image_playback.hpp"
#include "core/shell_packet.hpp"
#include <d3d11.h>
#include <dxgi.h>
#include <fstream>
#include <sstream>
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
namespace n=endfield::native;namespace m=endfield::modules;namespace c=endfield::core;namespace a=endfield::app;namespace t=endfield::tools;namespace fs=std::filesystem;using J=ehud::data::Json;using Microsoft::WRL::ComPtr;
unsigned checks{};void check(bool b,const std::string&w){++checks;if(!b)throw std::runtime_error(w);}
struct Apartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};
struct Window{HWND hwnd{};Window(){WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"OwnedMediaAssemblyPreviewFixture";check(RegisterClassW(&cls)!=0,"Register isolated Media Assembly owner fixture");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,cls.lpszClassName,L"Synthetic media only",WS_POPUP,0,0,1280,800,nullptr,nullptr,cls.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Fixture stays hidden");}~Window(){DestroyWindow(hwnd);UnregisterClassW(L"OwnedMediaAssemblyPreviewFixture",GetModuleHandleW(nullptr));}};
struct Temp {fs::path path;Temp(){path=fs::temp_directory_path()/(L"endfield-owned-media-owner-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));fs::create_directories(path);}~Temp(){std::error_code e;fs::remove_all(path,e);}};
J blank(){return J::Object{{"bounds",J::Array{0,0,440,440}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
void writePNG(const fs::path&path,unsigned w,unsigned h){
    ComPtr<IWICImagingFactory>wic;check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"WIC");
    ComPtr<IWICStream>s;wic->CreateStream(&s);check(SUCCEEDED(s->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"Owned synthetic file");ComPtr<IWICBitmapEncoder>e;wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&e);e->Initialize(s.Get(),WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode>f;ComPtr<IPropertyBag2>p;e->CreateNewFrame(&f,&p);f->Initialize(p.Get());f->SetSize(w,h);WICPixelFormatGUID fmt=GUID_WICPixelFormat32bppBGRA;f->SetPixelFormat(&fmt);
    std::vector<BYTE>px(std::size_t(w)*h*4);for(unsigned i=0;i<w*h;++i){px[i*4]=BYTE(i%251);px[i*4+1]=BYTE((i/w)*255/h);px[i*4+2]=BYTE((i%w)*255/w);px[i*4+3]=255;}f->WritePixels(h,w*4,UINT(px.size()),px.data());f->Commit();e->Commit();s->Commit(STGC_DEFAULT);
}
void run(n::Renderer&r,HWND hwnd,const fs::path&assets){
    Temp temp;const auto photo=temp.path/L"Owned Photo.png";writePNG(photo,320,200);const auto other=temp.path/L"dropped.png";writePNG(other,64,48);
    n::LayerRasterizer raster;a::UtilityExecutor utility([]{});auto engine=std::make_shared<n::NativeMediaAssemblyEngine>(n::NativeMediaAssemblyEngine::Options{assets});
    unsigned changes{},ids{};std::vector<std::string>events;std::vector<n::MediaAssemblyDialogRequest>dialogs;std::optional<std::string>dialogAnswer;
    t::MediaAssemblyPreviewOptions options;options.raster.pixelsPerPoint=1;options.raster.assetRoot=assets;options.window=hwnd;options.changed=[&]{++changes;};options.event=[&](std::string_view e){events.emplace_back(e);};
    options.uuid=[&]{return "owned-"+std::to_string(++ids);};options.dialog=[&](HWND owner,const n::MediaAssemblyDialogRequest&q){check(owner==hwnd,"Dialogs are modal to the owner HWND");dialogs.push_back(q);return dialogAnswer;};
    options.shelfChoices=[&]{return std::vector<m::NotesShelfChoice>{{"shelf-1","dropped.png","PNG",true,true},{"shelf-2","notes.txt","TXT",false,true}};};
    std::shared_ptr<int>lease;options.shelfAccess=[&](std::string_view id)->std::optional<t::MediaAssemblyShelfAccess>{if(id!="shelf-1")return {};lease=std::make_shared<int>(7);return t::MediaAssemblyShelfAccess{n::mediaAssemblyUTF8(other),lease};};
    t::MediaAssemblyPreview owner(raster,utility,engine,std::move(options));owner.resize({1280,800,96,1,1280,800});
    n::LayerComposition composition;c::ModulePresentation modules(c::Module::power);n::LayerScene geometry(raster);geometry.load(blank(),{});n::NativeModuleSurface plane(geometry,c::Module::mediaAssembly);
    c::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},c::Module::power,true};auto center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;const auto camera=n::layerViewportProjection(1280,800);double now{};
    bool released{};struct Guard{n::Renderer&r;n::LayerComposition&c;t::MediaAssemblyPreview&o;bool&done;~Guard(){if(!done)try{c.detach(r);o.release(r);}catch(...){r.reset();}}}guard{r,composition,owner,released};
    auto frame=[&](double step=.01){now+=step;const auto sample=modules.sample(now).presentation;settings.module=sample.requested;owner.update(center,settings,sample,1,now);owner.upload(r);composition.setEntries(r,owner.entries());composition.present(r);owner.collected(r);r.setCamera(camera);};
    auto point=[&](c::Point p){const auto sample=modules.sample(now).presentation;const auto*shown=sample.current.module==c::Module::mediaAssembly?&sample.current:&*sample.incoming;plane.update(center,settings,*shown);return *c::Projection::viewport(camera*plane.pose().contentWorld,1280,800).project(p);};
    auto drain=[&]{for(int k=0;k<200;++k){utility.waitIdle();if(!utility.drain())break;}};
    auto click=[&](c::Point p){const auto q=point(p);const bool down=owner.pointer({a::PointerKind::down,a::PointerButton::left,q.x,q.y},now);owner.pointer({a::PointerKind::up,a::PointerButton::left,q.x,q.y},now);return down;};
    auto dialog=[&]{MSG msg{};bool handled{};while(PeekMessageW(&msg,hwnd,0,0,PM_REMOVE))handled=owner.message({hwnd,msg.message,msg.wParam,msg.lParam},now)||handled;return handled;};
    frame();check(owner.entries().empty()&&utility.stats().accepted==0,"Unselected owner publishes nothing and starts no work");
    modules.select(c::Module::mediaAssembly,now);for(int k=0;k<40;++k)frame(.02);check(!owner.entries().empty(),"Selected Media Assembly publishes retained artwork");
    check(!owner.stats().gpuActive&&utility.stats().accepted==1,"First shown: the GPU shader compile is queued on the utility worker, not the UI thread");
    drain();frame();check(owner.stats().gpuActive&&owner.controller().deferredPreviews(),"GPU previews enabled after the worker compile");
    // Open -> chooser -> File Explorer dialog (posted, modal, injected) -> import.
    check(click({389,21})&&owner.menu()==m::MediaAssemblyMenuKind::source,"Open shows the source chooser under its trigger");frame();
    check(click({5,430})&&!owner.menu(),"A click outside the menu closes it and is consumed");frame(.2);
    owner.perform("open",now);check(owner.menu()==m::MediaAssemblyMenuKind::source,"Chooser again");dialogAnswer=n::mediaAssemblyUTF8(photo);
    check(owner.menuAction("finder",now)&&!owner.menu(),"Choose in File Explorer closes the menu");check(dialog()&&dialogs.size()==1&&dialogs[0].kind==n::MediaAssemblyDialogRequest::Kind::open&&!dialogs[0].extensions.empty(),"Open dialog runs from the posted notice");
    drain();frame();drain();frame();check(owner.controller().session().document()&&events==std::vector<std::string>{"imported"},"Picked media imported once");
    check(owner.stats().previewUploads==1&&owner.controller().previewImage()&&owner.controller().previewImage()->width==320,"One bounded preview texture upload");
    check(owner.stats().gpuActive&&owner.stats().gpuPreviews==1&&owner.stats().gpuTargets==1&&owner.controller().previewImage()->deferred()&&engine->stats().deferredPreviews==1,"The bounded preview is edited on the renderer's media device");
    for(int k=0;k<20;++k)frame();const auto uploads=owner.stats().previewUploads,rasters=raster.stats().rasterizations,textures=r.stats().textureUploads;
    allocations=0;counting=true;try{for(int k=0;k<120;++k){now+=1./60;settings.module=c::Module::mediaAssembly;owner.update(center*c::Matrix4::rotation(.001*k,.002,0),settings,modules.sample(now).presentation,1,now);owner.upload(r);composition.setEntries(r,owner.entries());composition.present(r);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"Warm tilt frames allocate nothing");check(owner.stats().previewUploads==uploads&&raster.stats().rasterizations==rasters&&r.stats().textureUploads==textures,"Warm frames re-upload and repaint nothing");
    {const auto ax=owner.accessibility();check(std::any_of(ax.begin(),ax.end(),[](const auto&e){return e.id=="closeMedia"&&e.name=="Close"&&e.rect.width>0;}),"UI Automation elements are projected into client coordinates");}
    {// UI Automation fragments over the owner: Invoke performs the source action.
        n::NativeMediaAssemblyAccessibility tree({nullptr,[&]{return owner.accessibility();},[&](std::string_view id){return owner.perform(id,now);},[&](std::string_view id,double v){return owner.setAccessibleValue(id,v,now);},[](c::Rect r){return r;}});
        check(tree.refresh()&&tree.size()==owner.accessibility().size(),"One UIA fragment per projected source control");
        auto invoke=[&](std::wstring_view id){for(std::size_t k=0;k<tree.size();++k){ComPtr<IRawElementProviderFragment>f;tree.child(k,&f);ComPtr<IRawElementProviderSimple>simple;f.As(&simple);VARIANT v;simple->GetPropertyValue(UIA_AutomationIdPropertyId,&v);
            const bool match=v.vt==VT_BSTR&&std::wstring_view(v.bstrVal,SysStringLen(v.bstrVal))==id;VariantClear(&v);if(match){ComPtr<IInvokeProvider>p;return SUCCEEDED(f.As(&p))&&SUCCEEDED(p->Invoke());}}return false;};
        check(invoke(L"tools")&&owner.presentation().drawer()==m::MediaAssemblyDrawer::tools,"UIA Invoke opens the tools drawer like a click");
        check(tree.refresh()&&tree.size()==owner.accessibility().size(),"The drawer's controls join the UIA tree");
        check(invoke(L"tools")&&!owner.presentation().drawer(),"UIA Invoke closes it again");tree.refresh();tree.disconnect();frame();}
    // Edits: one bounded preview per change; edited event debounced on the host deadline.
    owner.perform("tools",now);owner.perform("rotate",now);drain();frame();check(owner.controller().session().adjustments().rotationQuarterTurns==1&&owner.controller().previewImage()->width==200,"Rotate re-renders the bounded preview");
    check(owner.stats().gpuPreviews==2&&owner.stats().gpuTargets==2,"A new edited size gets a new media target");
    {const auto targets=owner.stats().gpuTargets;owner.perform("mirror",now);drain();frame();
        check(owner.stats().gpuPreviews==3&&owner.stats().gpuTargets==targets&&engine->stats().previewDecodes==1,"Same-size edits reuse the media target and the cached bounded source");}
    check(owner.nextWakeTime().has_value(),"Edited event is scheduled on the host deadline");const double wake=*owner.nextWakeTime();
    now=wake-.01;check(!owner.deadline(now)&&events.back()!="edited","Edited event waits for the 0.4 s debounce");now=wake;check(owner.deadline(now)&&events.back()=="edited"&&!owner.nextWakeTime(),"Edited recorded once on the shared deadline");
    // Zoom: Ctrl+wheel/pinch and a mouse notch zoom; fractional touchpad deltas pan.
    const auto zoomBefore=owner.presentation().viewport().zoom();const auto mid=point({220,198});
    check(owner.wheel({mid.x,mid.y,1,false,MK_CONTROL,3},now)&&owner.presentation().viewport().zoom()>zoomBefore,"Ctrl+wheel zooms the preview");
    const auto zoomed=owner.presentation().viewport().zoom();check(owner.wheel({mid.x,mid.y,-1,false,0,3},now)&&owner.presentation().viewport().zoom()<zoomed,"A wheel notch zooms like the source non-precise wheel");
    owner.wheel({mid.x,mid.y,2,false,MK_CONTROL,3},now);const auto pan=owner.presentation().viewport().pan();check(owner.wheel({mid.x,mid.y,.25,false,0,3},now)&&owner.presentation().viewport().pan().y!=pan.y,"Precision touchpad deltas pan");
    // Keyboard: '+' zooms, Escape unwinds the tool before the HUD.
    owner.perform("adjust",now);check(owner.key({a::KeyKind::down,VK_ESCAPE},{},now)&&!owner.presentation().tool(),"Escape leaves the active tool first");
    check(!owner.key({a::KeyKind::down,VK_ESCAPE},{},now),"Escape with nothing to unwind reaches the HUD");
    const auto z=owner.presentation().viewport().zoom();check(owner.key({a::KeyKind::down,VK_OEM_PLUS},{},now)&&owner.presentation().viewport().zoom()>z,"Plus zooms in");
    // Stickers: add, nudge with arrows, delete with Backspace.
    owner.perform("stickers",now);owner.perform("sticker:sticker_1",now);frame();check(owner.controller().session().adjustments().stickers.size()==1&&!owner.presentation().selectedSticker().empty(),"Sticker added with an owner identity");
    const auto sx=owner.controller().session().adjustments().stickers[0].x;check(owner.key({a::KeyKind::down,VK_RIGHT},{.shift=true},now)&&std::abs(owner.controller().session().adjustments().stickers[0].x-sx-.05)<1e-9,"Shift+Right nudges 0.05");
    check(owner.key({a::KeyKind::down,VK_BACK},{},now)&&owner.controller().session().adjustments().stickers.empty(),"Backspace deletes the selected sticker");drain();frame();
    // Export -> Save As (posted dialog) -> commit; overwrite with confirmation.
    owner.perform("export",now);check(owner.menu()==m::MediaAssemblyMenuKind::exportMedia,"Export menu");const auto out=temp.path/L"Owned Photo-edited.png";dialogAnswer=n::mediaAssemblyUTF8(out);
    check(owner.menuAction("saveAs",now)&&dialog()&&dialogs.back().kind==n::MediaAssemblyDialogRequest::Kind::save&&dialogs.back().filename=="Owned Photo-edited.png","Save As offers the source folder and suggested name");
    check(owner.controller().session().exporting(),"Export runs on the utility worker");check(owner.nextWakeTime().has_value(),"Export progress uses the host deadline only while exporting");
    drain();frame();check(!owner.controller().session().exporting()&&fs::exists(out)&&events.back()=="exported","Exported atomically");
    owner.perform("export",now);owner.menuAction("overwrite",now);check(owner.menu()==m::MediaAssemblyMenuKind::confirmOverwrite,"Overwrite asks for confirmation");
    check(owner.key({a::KeyKind::down,VK_ESCAPE},{},now)&&!owner.menu(),"Escape closes the menu first");
    owner.perform("export",now);owner.menuAction("overwrite",now);const auto imports=std::count(events.begin(),events.end(),"imported");const auto id=owner.controller().session().document()->id;
    check(owner.menuAction("confirm",now),"Confirm overwrite");drain();frame();drain();frame();
    check(std::count(events.begin(),events.end(),"imported")==imports&&owner.controller().session().document()->id!=id&&events.back()=="exported","Overwrite reloads the new file silently");
    // Temporary File Shelf source.
    owner.perform("open",now);owner.menuAction("shelf",now);check(owner.menu()==m::MediaAssemblyMenuKind::shelf,"Shelf picker");check(owner.menuAction("row:0",now)&&!owner.menu(),"Shelf row imports");
    drain();frame();check(owner.controller().session().document()->name=="dropped.png"&&lease.use_count()==1,"Shelf lease released after open");
    // Drop imports the first file; close fades the poster then releases it.
    const std::array<std::string,2>drops{n::mediaAssemblyUTF8(photo),n::mediaAssemblyUTF8(other)};check(owner.importFiles(drops,now),"Drop imports the first file");drain();frame();drain();frame();check(owner.controller().session().document()->name=="Owned Photo.png","First dropped file");
    const auto posesBefore=owner.sceneStats().poses;owner.perform("closeMedia",now);frame(.01);check(owner.sceneStats().surfaces>0&&!owner.controller().session().document(),"Close clears the source");for(int k=0;k<30;++k)frame(.01);
    check(owner.stats().retainedTextures==0,"Poster texture released after the finite fade (retained "+std::to_string(owner.stats().retainedTextures)+", retiredScene "+std::to_string(owner.sceneStats().retired)+", groups "+std::to_string(r.stats().nativeGroups)+", closing "+std::to_string(owner.sceneClosing())+", poses "+std::to_string(owner.sceneStats().poses-posesBefore)+", frames "+std::to_string(owner.requiresFrames(now))+")");
    // Hide parks everything.
    owner.setOverlayVisible(false,now);frame();check(!owner.controller().session().active(),"Hidden owner parks decoding");owner.setOverlayVisible(true,now);frame();
    composition.detach(r);owner.release(r);released=true;check(r.stats().textures==0&&r.stats().meshes==0,"Release leaves no resident resources");
}
// Compile-time detection of the shared frame server's played-frame filter.
void frameFilterHook(){
    struct Hooked {std::shared_ptr<const std::function<void(void*)>>frameFilter;};struct Plain {int other{};};
    static_assert(t::mediaAssemblyFrameFilterHook<Hooked>&&!t::mediaAssemblyFrameFilterHook<Plain>,"Hook detection");
    Hooked h;int calls{};void*seen{};int marker{};
    check(t::attachMediaAssemblyFrameFilter(h,[&](void*surface){++calls;seen=surface;})&&h.frameFilter,"A request with the hook receives the played-frame filter");
    (*h.frameFilter)(&marker);check(calls==1&&seen==&marker,"The filter receives the borrowed frame-server surface");
    Plain q;check(!t::attachMediaAssemblyFrameFilter(q,[](void*){}),"Requests without the hook are left untouched");
    std::cout<<"shared frame-server filter hook "<<(t::mediaAssemblyFrameFilterHook<n::NotesVideoRequest>?"present":"absent (played frames keep geometry-only edits)")<<'\n';
}
// GPU-edited previews (renderer media device) compose the same stage pixels
// as CPU-edited previews, and an owner without GPU previews keeps the worker
// path. Identical synthetic input, edits and frame times in both runs.
void parity(n::Renderer&r,HWND hwnd,const fs::path&assets){
    Temp temp;const auto photo=temp.path/L"parity.png";writePNG(photo,320,200);
    auto scenario=[&](bool useGpu){
        n::LayerRasterizer raster;a::UtilityExecutor utility([]{});auto engine=std::make_shared<n::NativeMediaAssemblyEngine>(n::NativeMediaAssemblyEngine::Options{assets});unsigned ids{};
        t::MediaAssemblyPreviewOptions options;options.raster.pixelsPerPoint=1;options.raster.assetRoot=assets;options.window=hwnd;options.uuid=[&]{return "parity-"+std::to_string(++ids);};options.gpuPreview=useGpu;
        t::MediaAssemblyPreview owner(raster,utility,engine,std::move(options));owner.resize({1280,800,96,1,1280,800});
        n::LayerComposition composition;c::ModulePresentation modules(c::Module::power);c::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},c::Module::power,true};auto center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;
        const auto camera=n::layerViewportProjection(1280,800);double now{};
        bool released{};struct Guard{n::Renderer&r;n::LayerComposition&c;t::MediaAssemblyPreview&o;bool&done;~Guard(){if(!done)try{c.detach(r);o.release(r);}catch(...){r.reset();}}}guard{r,composition,owner,released};
        auto frame=[&](double step=.02){now+=step;const auto sample=modules.sample(now).presentation;settings.module=sample.requested;owner.update(center,settings,sample,1,now);owner.upload(r);composition.setEntries(r,owner.entries());composition.present(r);owner.collected(r);r.setCamera(camera);};
        auto drain=[&]{for(int k=0;k<200;++k){utility.waitIdle();if(!utility.drain())break;}};
        modules.select(c::Module::mediaAssembly,now);for(int k=0;k<40;++k)frame();drain();frame();
        const std::array<std::string,1>drop{n::mediaAssemblyUTF8(photo)};check(owner.importFiles(drop,now),"Parity import");drain();frame();drain();frame();
        owner.perform("tools",now);owner.perform("adjust",now);
        for(const auto&[id,value]:std::array<std::pair<std::string_view,double>,5>{{{"exposure",.5},{"saturation",1.6},{"temperature",4000},{"highlights",.4},{"shadows",.6}}}){check(owner.setAccessibleValue(id,value,now),"Parity edit "+std::string(id));drain();frame();}
        owner.perform("toolBack",now);owner.perform("filters",now);owner.perform("filter:sp_filter_2",now);drain();frame();owner.perform("toolBack",now);owner.perform("rotate",now);drain();frame();
        for(int k=0;k<40;++k){frame();drain();}
        const auto&a=owner.controller().session().adjustments();check(a.rotationQuarterTurns==1&&a.filter!=m::MediaAssemblyFilter::none&&a.highlights==.4,"Parity edits applied");
        check(owner.stats().gpuActive==useGpu&&(useGpu?owner.stats().gpuPreviews>=7&&owner.controller().previewImage()->deferred():owner.stats().gpuPreviews==0&&!owner.controller().previewImage()->deferred()),"GPU previews only when enabled");
        r.draw(false);auto pixels=r.readback();
        // A hidden (parked) editor releases its bounded preview texture; showing
        // it again produces one fresh preview.
        owner.setOverlayVisible(false,now);for(int k=0;k<5;++k)frame();check(!owner.controller().previewImage()&&owner.stats().retainedTextures==0,"Hidden editor releases its preview texture");
        owner.setOverlayVisible(true,now);for(int k=0;k<10;++k){frame();drain();}check(owner.controller().previewImage()&&owner.stats().retainedTextures==1,"Shown again with one fresh preview");
        composition.detach(r);owner.release(r);released=true;check(r.stats().textures==0,"Parity owner releases every texture");return pixels;
    };
    const auto gpu=scenario(true),cpu=scenario(false);check(gpu.width==cpu.width&&gpu.height==cpu.height&&gpu.pixels.size()==cpu.pixels.size()&&!gpu.pixels.empty(),"Comparable stage readbacks");
    int worst{};std::size_t over{},changed{};for(std::size_t k=0;k<gpu.pixels.size();++k){const int d=std::abs(int(gpu.pixels[k])-int(cpu.pixels[k]));worst=std::max(worst,d);over+=d>2;changed+=d>0;}
    std::cout<<"GPU/CPU preview stage worst="<<worst<<" over2="<<over<<" changed="<<changed<<" of "<<gpu.pixels.size()<<'\n';
    check(worst<=3&&over*1000<=gpu.pixels.size(),"GPU-edited previews compose like CPU-edited previews");
}
// Fake IMFMediaEngine for the shared broker (no codec, audio device or file).
struct Probe {n::NativeNotesVideoPlayback::Notify notify;double position{};bool playing{},fresh{true},stopped{};unsigned opens{},seeks{},frames{};};
class FakeEngine final:public n::NativeNotesVideoPlayback::Engine {
    std::shared_ptr<Probe>p_;
public:explicit FakeEngine(std::shared_ptr<Probe>p):p_(std::move(p)){}~FakeEngine()override{stop();}
    HRESULT open(const n::NotesVideoRequest&)override{++p_->opens;p_->notify(n::NativeNotesVideoPlayback::ready|n::NativeNotesVideoPlayback::firstFrame,S_OK);return S_OK;}
    HRESULT metadata(n::NotesVideoMetadata&out)override{out={4,4,10};return S_OK;}
    HRESULT play()override{p_->playing=true;p_->fresh=true;return S_OK;}HRESULT pause()override{p_->playing=false;return S_OK;}
    HRESULT seek(double t)override{p_->position=t;++p_->seeks;p_->fresh=true;p_->notify(n::NativeNotesVideoPlayback::seeked,S_OK);return S_OK;}
    double currentTime()const override{return p_->position;}
    HRESULT tick(std::int64_t&pts)override{pts=static_cast<std::int64_t>(p_->position*10000000);if(!p_->fresh)return S_FALSE;p_->fresh=false;return S_OK;}
    HRESULT transfer(void*raw,unsigned width,unsigned height)override{ComPtr<ID3D11Texture2D>texture;const auto hr=static_cast<IDXGISurface*>(raw)->QueryInterface(IID_PPV_ARGS(&texture));if(FAILED(hr))return hr;ComPtr<ID3D11Device>device;texture->GetDevice(&device);ComPtr<ID3D11DeviceContext>context;device->GetImmediateContext(&context);
        std::vector<std::uint8_t>pixels(std::size_t(width)*height*4,200);context->UpdateSubresource(texture.Get(),0,nullptr,pixels.data(),width*4,0);++p_->frames;return S_OK;}
    void stop()noexcept override{p_->playing=false;p_->stopped=true;}
};
void playback(n::Renderer&r,HWND hwnd,const fs::path&assets,const fs::path&fixture){
    std::ifstream in(fixture,std::ios::binary);std::ostringstream bytes;bytes<<in.rdbuf();const auto movieBytes=bytes.str();
    check(endfield::core::packet::sha256({reinterpret_cast<const std::uint8_t*>(movieBytes.data()),movieBytes.size()})=="0de10d53a6946351f127b78b738c68658708cd91e932164d110ca6ec2f8579cf","Only the pinned owned silent movie fixture is used");
    Temp temp;const auto movie=temp.path/L"owned-silent.mp4";{std::ofstream out(movie,std::ios::binary);out<<movieBytes;}
    constexpr UINT notice=WM_APP+252;constexpr UINT_PTR generation=9;
    n::NativeNotesImageDecoder decoder([](const n::NotesImageRequest&q){return n::NotesImageAccess{n::mediaAssemblyPath(q.path),q.accessLease};},{hwnd,notice,generation});
    n::NativeNotesImagePlayback images(decoder);std::vector<std::shared_ptr<Probe>>engines;std::shared_ptr<Probe>probe;
    // One probe per native engine: a released engine's late stop() cannot
    // clobber the state of its replacement.
    n::NativeNotesVideoPlayback videos(r,{hwnd,notice,generation},[&](std::shared_ptr<void>device,n::NativeNotesVideoPlayback::Notify notify){check(device!=nullptr,"Engine borrows the renderer device");probe=std::make_shared<Probe>();if(!engines.empty())probe->position=engines.back()->position;probe->notify=std::move(notify);engines.push_back(probe);return std::make_unique<FakeEngine>(probe);});
    n::NativeMediaRequestBroker broker(decoder,images,{hwnd,notice,generation},&videos);const auto client=broker.attachClient();
    n::LayerRasterizer raster;a::UtilityExecutor utility([]{});auto engine=std::make_shared<n::NativeMediaAssemblyEngine>(n::NativeMediaAssemblyEngine::Options{assets});unsigned ids{};
    t::MediaAssemblyPreviewOptions options;options.raster.pixelsPerPoint=1;options.raster.assetRoot=assets;options.window=hwnd;options.uuid=[&]{return "video-"+std::to_string(++ids);};options.broker=&broker;options.client=client;
    t::MediaAssemblyPreview owner(raster,utility,engine,std::move(options));owner.resize({1280,800,96,1,1280,800});
    n::LayerComposition composition;c::ModulePresentation modules(c::Module::power);c::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},c::Module::power,true};auto center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;double now{};
    bool released{};struct Guard{n::Renderer&r;n::LayerComposition&c;t::MediaAssemblyPreview&o;bool&done;~Guard(){if(!done)try{c.detach(r);o.release(r);}catch(...){r.reset();}}}guard{r,composition,owner,released};
    auto pump=[&]{MSG msg{};while(PeekMessageW(&msg,hwnd,notice,notice,PM_REMOVE))broker.accept(msg.wParam,now);broker.sample(now);owner.refreshSharedMedia(now);};
    // The fake engine clock advances with the shared frame clock while playing.
    auto frame=[&](double step=.02){now+=step;if(probe&&probe->playing){probe->position+=step;probe->fresh=true;}pump();const auto sample=modules.sample(now).presentation;settings.module=sample.requested;owner.update(center,settings,sample,1,now);owner.upload(r);composition.setEntries(r,owner.entries());composition.present(r);owner.collected(r);broker.collectRetired();};
    auto drain=[&]{for(int k=0;k<200;++k){utility.waitIdle();if(!utility.drain())break;}};
    modules.select(c::Module::mediaAssembly,now);for(int k=0;k<30;++k)frame();drain();frame();
    const std::array<std::string,1>drop{n::mediaAssemblyUTF8(movie)};check(owner.importFiles(drop,now),"Movie import");drain();frame();drain();frame();
    const auto&session=owner.controller().session();check(session.document()&&session.document()->video&&owner.controller().previewImage(),"Owned movie opened with a bounded poster");
    const double playStart=now;check(owner.perform("play",now)&&session.wantsPlayback(),"Play routed to the shared broker");for(int k=0;k<10;++k)frame();
    check(engines.size()==1&&probe->opens==1&&probe->playing&&owner.stats().playbackStarts==1,"One native engine opened and playing");check(owner.stats().videoShown&&session.view().playing,"Live player texture replaces the poster while playing");
    const auto duration=session.document()->duration;std::cout<<"owned movie duration "<<duration<<'\n';check(duration>.3,"Fixture long enough for a range");
    const double started=now;for(int k=0;k<20;++k)frame();
    check(session.currentTime()>0&&session.currentTime()<=now-playStart+.001&&now-playStart-session.currentTime()<.5,"Position follows the shared frame clock at the source observer granularity (current "+std::to_string(session.currentTime())+", since play "+std::to_string(now-playStart)+")");
    check(owner.nextWakeTime()&&*owner.nextWakeTime()>now&&*owner.nextWakeTime()<=now+duration-session.currentTime()+.001,"One end-of-range host deadline replaces a periodic observer");(void)started;
    // Edit while playing: the player is released and re-prepared after the new poster.
    owner.perform("mirror",now);check(owner.stats().playbackReleases==1&&!owner.stats().videoShown,"An edit releases the player");drain();for(int k=0;k<10;++k){frame();drain();}
    check(owner.stats().playbackStarts==2&&engines.size()==2&&engines[0]->stopped&&probe->playing,"Playback resumes with a new player and the new edit after the bounded preview");
    // End of the trim range stops playback (source forwardPlaybackEndTime).
    // Host loop: only owner deadlines (edited event, end of range) wake it.
    unsigned wakes{};for(;wakes<6&&session.wantsPlayback();++wakes){const auto wake=owner.nextWakeTime();check(wake.has_value(),"Resumed playback schedules its end deadline");if(probe->playing)probe->position+=*wake-now;now=*wake;check(owner.deadline(now),"A scheduled deadline fires");frame(.01);}
    check(wakes<=2,"Playback ends on its single end-of-range deadline");
    check(!session.wantsPlayback()&&!session.view().playing&&!owner.stats().videoShown&&!probe->playing,"Trim end stops playback and shows the poster");
    owner.perform("play",now);for(int k=0;k<6;++k)frame();check(probe->playing&&probe->position<duration*.1,"Replaying near the end restarts at the trim start");
    owner.perform("play",now);for(int k=0;k<4;++k)frame();check(!probe->playing&&!owner.stats().videoShown,"Pause releases the player");
    owner.setOverlayVisible(false,now);frame();composition.detach(r);owner.release(r);released=true;broker.hideVideos(client,now);broker.collectRetired();
}
}
int wmain(int argc,wchar_t**argv){Apartment apartment;try{frameFilterHook();check(SUCCEEDED(apartment.hr)&&argc==4,"Pass HLSL, resources/media-assembly and the pinned silent movie");Window window;n::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{n::Driver::warpForTests,argv[1],n::RenderTarget::offscreenForTests,true});run(renderer,window.hwnd,fs::absolute(argv[2]));renderer.reset();
        {n::Renderer same;same.initialize(window.hwnd,1280,800,{n::Driver::warpForTests,argv[1],n::RenderTarget::offscreenForTests,true});parity(same,window.hwnd,fs::absolute(argv[2]));same.reset();}
        n::Renderer media;media.initialize(window.hwnd,1280,800,{n::Driver::warpForTests,argv[1],n::RenderTarget::offscreenForTests,true});playback(media,window.hwnd,fs::absolute(argv[2]),fs::absolute(argv[3]));media.reset();std::cout<<"Media Assembly owner: "<<checks<<" checks passed (synthetic media only)\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"Media Assembly owner failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
#include <iostream>
int main(){std::cout<<"Media Assembly owner needs Windows\n";return 0;}
#endif
