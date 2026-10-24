// Media Assembly's design owner (tools/media_assembly_owner.hpp) on a hidden
// window and WARP, published as the host publishes owners (the shared overlay
// owner's Context, its confirm bar and deadlines): the open menu and the
// posted modal dialog, one bounded preview on the stage (GPU-edited on the
// renderer's media device), warm frames that repaint nothing, rotate / mirror
// media targets, the 0.4 s edited event, wheel / pinch / touchpad, keys,
// stickers, Save As and the overwrite confirm bar, the Shelf, drops, close
// on the confirm bar, GPU / CPU stage parity, the memory audit (shown, hidden
// in the open HUD, after the HUD closed) and playback through the shared
// broker with a fake media engine (the played-frame colour filter, the
// end-of-trim deadline, the identity re-check). Synthetic media only; no
// input injection, no visible window.
#ifdef _WIN32
#include "tools/media_assembly_owner.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#include "native/media_assembly_codec.hpp"
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
#include <array>
#include <vector>
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
namespace n=endfield::native;namespace m=endfield::modules;namespace d=endfield::modules::design;namespace c=endfield::core;namespace a=endfield::app;namespace t=endfield::tools;namespace fs=std::filesystem;using Microsoft::WRL::ComPtr;
unsigned checks{};void check(bool b,const std::string&w){++checks;if(!b)throw std::runtime_error(w);}
struct Apartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};
struct Window{HWND hwnd{};Window(){WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"OwnedMediaAssemblyOwnerFixture";check(RegisterClassW(&cls)!=0,"Register isolated Media Assembly owner fixture");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,cls.lpszClassName,L"Synthetic media only",WS_POPUP,0,0,1280,800,nullptr,nullptr,cls.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Fixture stays hidden");}~Window(){DestroyWindow(hwnd);UnregisterClassW(L"OwnedMediaAssemblyOwnerFixture",GetModuleHandleW(nullptr));}};
struct Temp {fs::path path;Temp(){path=fs::temp_directory_path()/(L"endfield-owned-media-owner-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));fs::create_directories(path);}~Temp(){std::error_code e;fs::remove_all(path,e);}};
void writePNG(const fs::path&path,unsigned w,unsigned h){
    ComPtr<IWICImagingFactory>wic;check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"WIC");
    ComPtr<IWICStream>s;wic->CreateStream(&s);check(SUCCEEDED(s->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"Owned synthetic file");ComPtr<IWICBitmapEncoder>e;wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&e);e->Initialize(s.Get(),WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode>f;ComPtr<IPropertyBag2>p;e->CreateNewFrame(&f,&p);f->Initialize(p.Get());f->SetSize(w,h);WICPixelFormatGUID fmt=GUID_WICPixelFormat32bppBGRA;f->SetPixelFormat(&fmt);
    std::vector<BYTE>px(std::size_t(w)*h*4);for(unsigned i=0;i<w*h;++i){px[i*4]=BYTE(i%251);px[i*4+1]=BYTE((i/w)*255/h);px[i*4+2]=BYTE((i%w)*255/w);px[i*4+3]=255;}f->WritePixels(h,w*4,UINT(px.size()),px.data());f->Commit();e->Commit();s->Commit(STGC_DEFAULT);
}
// The host's publication of the overlay owner and the Media Assembly owner
// (app/application.cpp publishNative): deadlines, update, upload, the module's
// entries then the overlay's, collection.
struct Fixture {
    n::Renderer&r;HWND hwnd;n::LayerRasterizer raster;a::UtilityExecutor utility{[]{}};std::unique_ptr<t::DesignOverlayOwner>overlay;
    std::shared_ptr<n::NativeMediaAssemblyEngine>engine;std::unique_ptr<t::MediaAssemblyOwner>owner;
    n::LayerComposition composition;c::ModulePresentation modules{c::Module::power};c::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},c::Module::power,true};c::Matrix4 center;
    const c::Matrix4 camera=n::layerViewportProjection(1280,800);double now{};bool released{};
    Fixture(n::Renderer&renderer,HWND window,const fs::path&assets,t::MediaAssemblyOwnerOptions options,double pixelsPerPoint=1):r(renderer),hwnd(window){
        t::DesignOverlayOptions o;o.raster.pixelsPerPoint=pixelsPerPoint;o.window=hwnd;o.now=[this]{return now;};overlay=std::make_unique<t::DesignOverlayOwner>(raster,o);
        const a::ClientMetrics metrics{1280,800,96,1,1280,800};overlay->resize(metrics);
        a::ModuleAppearance appearance;appearance.accentSRGB={250./255,212./255,31./255};overlay->setAppearance(appearance,0);overlay->setOverlayVisible(true,0);
        modules.setDesignScreen(c::Module::mediaAssembly,true);center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;
        engine=std::make_shared<n::NativeMediaAssemblyEngine>(n::NativeMediaAssemblyEngine::Options{assets});
        options.assetRoot=assets;options.window=hwnd;options.design.name="mediaAssembly";options.design.raster.pixelsPerPoint=pixelsPerPoint;options.design.now=[this]{return now;};
        options.design.routing.message=20;
        owner=std::make_unique<t::MediaAssemblyOwner>(raster,utility,overlay->context(),engine,std::move(options));owner->resize(metrics);owner->setAppearance(appearance,0);owner->setOverlayVisible(true,0);
        r.setCamera(camera);
    }
    ~Fixture(){if(!released)try{finish();}catch(...){r.reset();}}
    void finish(){composition.detach(r);owner->release(r);overlay->release(r);released=true;}
    t::MediaAssemblyOwner&o(){return *owner;}
    m::MediaAssemblyCanvas&canvas(){return owner->canvas();}
    const m::MediaAssemblySession&session(){return owner->controller().session();}
    void frame(double step=.02,const std::function<void()>&before={}){
        now+=step;(void)overlay->deadline(now);(void)owner->deadline(now);if(before)before();
        const auto sample=modules.sample(now).presentation;settings.module=sample.requested;const a::ModuleFrame f{center,settings,sample,1,now,true};
        overlay->update(f);owner->update(f);overlay->upload(r);owner->upload(r);
        std::vector<n::LayerCompositionEntry>entries;for(const auto&e:owner->entries(sample.current.module))entries.push_back(e);
        if(sample.incoming)for(const auto&e:owner->entries(sample.incoming->module))entries.push_back(e);
        for(const auto&e:overlay->modalEntries())entries.push_back(e);
        composition.setEntries(r,entries);composition.present(r);owner->collected(r);overlay->collected(r);
    }
    void frames(double seconds){const auto end=now+seconds;while(now<end)frame();}
    void drain(){for(int k=0;k<200;++k){utility.waitIdle();if(!utility.drain())break;}}
    void settle(){for(int k=0;k<6;++k){drain();frame();}}
    std::optional<c::Point>client(c::Point box){const auto rect=owner->core().clientRect({box.x,box.y,0,0});return rect?std::optional(c::Point{rect->x,rect->y}):std::nullopt;}
    const d::Control*control(std::string_view id){for(const auto*x:canvas().header().controls())if(x->id()==id)return x;for(const auto*x:canvas().page().controls())if(x->id()==id)return x;return nullptr;}
    bool click(c::Point box){const auto q=client(box);check(q.has_value(),"Box point projects");const bool down=owner->pointer({a::PointerKind::down,a::PointerButton::left,q->x,q->y},now);owner->pointer({a::PointerKind::up,a::PointerButton::left,q->x,q->y},now);return down;}
    bool clickControl(std::string_view id){const auto*x=control(id);check(x!=nullptr,"Control laid out: "+std::string(id));const auto rect=x->rect(canvas().root());return click({rect.x+rect.width/2,rect.y+rect.height/2});}
    bool perform(std::string_view id){return canvas().perform(id,now);}
    bool key(std::uint32_t vk,std::uint32_t modifiers=0){return owner->key({a::KeyKind::down,vk},modifiers,now);}
    bool dialog(){MSG msg{};bool handled{};while(PeekMessageW(&msg,hwnd,0,0,PM_REMOVE))handled=owner->message({hwnd,msg.message,msg.wParam,msg.lParam},now)||handled;return handled;}
    bool confirm(std::string_view id){auto&overlayState=overlay->context().overlay();if(!overlayState.isConfirming()||overlayState.confirmState().request()->id!=id)return false;(void)overlayState.decide(d::ConfirmState::Decision::confirm);frames(.3);return true;}
    c::Point stageCentre(){const auto q=client({m::MediaAssemblyCanvas::stageOrigin.x+224,m::MediaAssemblyCanvas::stageOrigin.y+206});check(q.has_value(),"Stage centre projects");return *q;}
};

void run(n::Renderer&r,HWND hwnd,const fs::path&assets){
    Temp temp;const auto photo=temp.path/L"Owned Photo.png";writePNG(photo,320,200);const auto other=temp.path/L"dropped.png";writePNG(other,64,48);
    unsigned ids{};std::vector<std::string>events;std::vector<n::MediaAssemblyDialogRequest>dialogs;std::optional<std::string>dialogAnswer;
    t::MediaAssemblyOwnerOptions options;options.event=[&](std::string_view e){events.emplace_back(e);};
    options.uuid=[&]{return "owned-"+std::to_string(++ids);};options.dialog=[&](HWND owner,const n::MediaAssemblyDialogRequest&q){check(owner==hwnd,"Dialogs are modal to the owner HWND");dialogs.push_back(q);return dialogAnswer;};
    options.shelfChoices=[&]{return std::vector<m::MediaAssemblyShelfChoice>{{"shelf-1","dropped.png",true,true},{"shelf-2","notes.txt",false,true}};};
    std::shared_ptr<int>lease;options.shelfAccess=[&](std::string_view id)->std::optional<m::MediaAssemblyShelfFile>{if(id!="shelf-1")return {};lease=std::make_shared<int>(7);return m::MediaAssemblyShelfFile{n::mediaAssemblyUTF8(other),lease};};
    Fixture f(r,hwnd,assets,std::move(options));auto&owner=f.o();
    f.frame();check(owner.entries(c::Module::mediaAssembly).empty()&&f.utility.stats().accepted==0,"Unselected owner publishes nothing and starts no work");
    f.modules.select(c::Module::mediaAssembly,f.now);for(int k=0;k<40;++k)f.frame();check(!owner.entries(c::Module::mediaAssembly).empty()&&f.canvas().stage().isEmpty(),"Selected Media Assembly publishes its empty stage");
    if(n::mediaAssemblyGpuShadersEmbedded())check(owner.stats().gpuActive&&owner.controller().deferredPreviews()&&f.utility.stats().accepted==0,"First shown: build-time GPU programs, so GPU previews start at once and nothing is compiled or queued");
    else check(!owner.stats().gpuActive&&f.utility.stats().accepted==1,"First shown: the GPU shader compile is queued on the utility worker, not the UI thread");
    f.drain();f.frame();check(owner.stats().gpuActive&&owner.controller().deferredPreviews(),"GPU previews enabled after the worker compile");
    // Open -> menu -> File Explorer dialog (posted, modal, injected) -> import.
    check(f.clickControl("open")&&f.canvas().menuOpen(),"Open shows the source menu under its control");f.frame();
    check(f.click({500,480})&&!f.canvas().menuOpen(),"A press outside the menu closes it and is consumed");f.frames(.2);
    check(f.perform("open")&&f.canvas().menuOpen(),"The menu again");dialogAnswer=n::mediaAssemblyUTF8(photo);
    check(f.perform("open:finder")&&!f.canvas().menuOpen(),"Choose in File Explorer closes the menu");
    check(f.dialog()&&dialogs.size()==1&&dialogs[0].kind==n::MediaAssemblyDialogRequest::Kind::open&&!dialogs[0].extensions.empty(),"Open dialog runs from the posted notice");
    f.settle();f.frames(.5);check(f.session().document()&&events==std::vector<std::string>{"imported"},"Picked media imported once");
    check(owner.stats().previewUploads==1&&owner.controller().previewImage()&&owner.controller().previewImage()->width==320&&owner.stats().pictureDraws==1,"One bounded preview drawn on the stage");
    check(owner.stats().gpuActive&&owner.stats().gpuPreviews==1&&owner.stats().gpuTargets==1&&owner.controller().previewImage()->deferred()&&f.engine->stats().deferredPreviews==1,"The bounded preview is edited on the renderer's media device");
    for(int k=0;k<20;++k)f.frame();const auto uploads=owner.stats().previewUploads,rasters=f.raster.stats().rasterizations,textures=r.stats().textureUploads;
    {   // Warm frames under a tilting plane: numbers only.
        const auto base=f.center;
        allocations=0;counting=true;try{for(int k=0;k<120;++k){f.center=base*c::Matrix4::rotation(.001*k,.002,0);f.frame(1./60);}}catch(...){counting=false;throw;}counting=false;f.center=base;
        std::cout<<"warm tilt frames: "<<allocations<<" allocations in 120 frames (host publication included)\n";
        check(owner.stats().previewUploads==uploads&&f.raster.stats().rasterizations==rasters&&r.stats().textureUploads==textures,"Warm frames re-upload and repaint nothing");}
    // Edits: one bounded preview per change; edited event debounced on the host deadline.
    // settle + .1 s + settle (.34 s): past the .25 s turn, inside the .4 s edited debounce (each edit restarts it).
    check(f.perform("tool:rotate"),"Rotate");f.settle();f.frames(.1);f.settle();
    check(f.session().adjustments().rotationQuarterTurns==1&&owner.controller().previewImage()->width==200&&f.canvas().stage().image()==owner.controller().previewImage(),"Rotate re-renders the bounded preview; the stage takes it after the turn");
    check(owner.stats().gpuPreviews==2&&owner.stats().gpuTargets==2,"A new edited size gets a new media target");
    {const auto targets=owner.stats().gpuTargets;check(f.perform("tool:mirror"),"Mirror");f.settle();f.frames(.1);f.settle();
        check(owner.stats().gpuPreviews==3&&owner.stats().gpuTargets==targets&&f.engine->stats().previewDecodes==1,"Same-size edits reuse the media target and the cached bounded source");}
    {const auto wake=owner.nextWakeTime(f.now);check(wake.has_value(),"Edited event is scheduled on the host deadline");
        check(!owner.deadline(*wake-.01)&&events.back()!="edited","Edited event waits for the 0.4 s debounce");f.now=*wake;check(owner.deadline(f.now)&&events.back()=="edited","Edited recorded once on the shared deadline");}
    // A filter edit cross-fades (.12 s): the previous preview is drawn under the new one, then let go.
    {check(f.perform("mediaTab:1")&&f.perform("filter:sp_filter_2"),"Filter");f.drain();f.frame();f.drain();f.frame(.01);
        check(f.canvas().stage().previousImage()&&owner.stats().pictureDraws==2,"The filter edit cross-fades over the previous preview");
        f.frames(.3);check(!f.canvas().stage().previousImage()&&owner.stats().pictureDraws==1&&owner.stats().retainedTextures==1,"After the cross-fade one preview texture is kept");
        check(f.perform("mediaTab:0"),"Back to the tiles");f.frames(.3);}
    // Zoom: a mouse notch x1.1 about the pointer, the pinch (Ctrl+wheel), touchpad pans.
    const auto mid=f.stageCentre();const auto zoomBefore=f.canvas().viewport().zoom();
    check(owner.wheel({mid.x,mid.y,1,false,MK_CONTROL,3},false,f.now)&&std::abs(f.canvas().viewport().zoom()-std::clamp(zoomBefore*std::exp(.12),1.,20.))<1e-9,"A pinch (Ctrl+wheel) scales by 1 + magnification");
    {const auto pinched=f.canvas().viewport().zoom();const auto outside=f.client({500,20});owner.wheel({outside->x,outside->y,1,false,MK_CONTROL,3},false,f.now);check(f.canvas().viewport().zoom()==pinched,"A pinch outside the stage zooms nothing");}
    {const auto zoomed=f.canvas().viewport().zoom();check(owner.wheel({mid.x,mid.y,-1,false,0,3},false,f.now)&&std::abs(f.canvas().viewport().zoom()-std::max(1.,zoomed/1.1))<1e-9,"A mouse wheel notch zooms x1.1 (down: out)");}
    owner.wheel({mid.x,mid.y,3,false,MK_CONTROL,3},false,f.now);const auto pan=f.canvas().viewport().pan();
    check(owner.wheel({mid.x,mid.y,.25,false,0,3},false,f.now)&&f.canvas().viewport().pan().y!=pan.y,"Precision touchpad deltas pan");
    {const auto z=f.canvas().viewport().zoom();check(owner.wheel({mid.x,mid.y,.25,false,0,3},true,f.now)&&f.canvas().viewport().zoom()!=z,"A touchpad with Alt (the Mac's ⌘ / ⌥) zooms");}
    check(f.perform("zoomReset")&&f.canvas().viewport().zoom()==1,"The zoom value resets zoom and pan");
    // Keyboard: Escape unwinds the tool before the HUD; '+' zooms in.
    check(f.perform("tool:adjust")&&f.canvas().tool()==m::MediaAssemblyTool::adjust,"Adjust");check(f.key(VK_ESCAPE)&&!f.canvas().tool(),"Escape leaves the active tool first");f.frames(.4);
    check(!f.key(VK_ESCAPE),"Escape with nothing to unwind reaches the HUD");
    {const auto z=f.canvas().viewport().zoom();check(f.key(VK_OEM_PLUS)&&f.canvas().viewport().zoom()>z,"Plus zooms in");check(f.perform("zoomReset"),"Reset zoom");}
    // Stickers: add, nudge with arrows, turn with Alt, delete with Backspace.
    check(f.perform("mediaTab:2")&&f.perform("sticker:sticker_1"),"Sticker");f.frame();check(f.session().adjustments().stickers.size()==1&&!f.canvas().selectedSticker().empty(),"Sticker added with an owner identity");
    const auto sx=f.session().adjustments().stickers[0].x;check(f.key(VK_RIGHT,4)&&std::abs(f.session().adjustments().stickers[0].x-sx-.05)<1e-9,"Shift+Right nudges 0.05");
    check(f.key(VK_RIGHT,1)&&f.session().adjustments().stickers[0].rotation==3,"Alt+Right turns 3 degrees");
    check(f.key(VK_RIGHT,5)&&f.session().adjustments().stickers[0].rotation==18,"Alt+Shift+Right turns 15 degrees");
    check(!f.key(VK_RIGHT,2)&&f.session().adjustments().stickers[0].rotation==18,"Ctrl+arrows are not handled (the Mac's ⌘ / ⌃)");
    check(owner.stats().thumbnailDecodes==m::mediaAssemblyStickerCount&&owner.stats().thumbnailBytes<=m::mediaAssemblyStickerCount*96u*96*4,"The drawer decodes 96 px sticker thumbnails once");
    check(f.key(VK_BACK)&&f.session().adjustments().stickers.empty(),"Backspace deletes the selected sticker");f.settle();
    // Export -> Save As (posted dialog) -> commit; overwrite on the confirm bar.
    check(f.perform("mediaTab:0"),"Tiles");check(f.perform("export")&&f.canvas().menuOpen(),"Export menu");const auto out=temp.path/L"Owned Photo-edited.png";dialogAnswer=n::mediaAssemblyUTF8(out);
    check(f.perform("export:saveAs")&&f.dialog()&&dialogs.back().kind==n::MediaAssemblyDialogRequest::Kind::save&&dialogs.back().filename=="Owned Photo-edited.png","Save As offers the source folder and suggested name");
    check(f.session().exporting()&&owner.nextWakeTime(f.now).has_value()&&f.canvas().counterText().ends_with("%"),"Export runs on the utility worker; the header counts it");
    f.settle();f.frames(1.5);check(!f.session().exporting()&&fs::exists(out)&&events.back()=="exported"&&!f.canvas().isExporting(),"Exported atomically; the gauge finished");
    {const auto&toast=f.overlay->context().overlay().toastState().current();check(toast&&toast->caption=="Owned Photo-edited.png","The Exported toast names the file");}
    check(f.perform("export")&&f.perform("export:overwrite"),"Overwrite");
    {const auto&request=f.overlay->context().overlay().confirmState().request();check(request&&request->id=="mediaOverwrite"&&request->message=="Owned Photo.png"&&request->danger,"Overwrite asks on the confirm bar");}
    check(f.overlay->key({a::KeyKind::down,VK_ESCAPE},f.now)&&!f.overlay->context().overlay().isConfirming(),"Escape cancels the confirm bar");f.frames(.3);
    check(f.perform("export")&&f.perform("export:overwrite"),"Overwrite again");const auto imports=std::count(events.begin(),events.end(),"imported");const auto id=f.session().document()->id;
    check(f.confirm("mediaOverwrite"),"Confirm overwrite");f.settle();f.frames(1.5);f.settle();
    check(std::count(events.begin(),events.end(),"imported")==imports&&f.session().document()->id!=id&&events.back()=="exported","Overwrite reloads the new file silently");
    // Temporary File Shelf source.
    check(f.perform("open")&&f.perform("open:shelf")&&f.canvas().menuOpen(),"Shelf menu");check(f.perform("shelf:shelf-1")&&!f.canvas().menuOpen(),"Shelf row imports");
    f.settle();check(f.session().document()->name=="dropped.png"&&lease.use_count()==1,"Shelf lease released after open");
    // Drop imports the first file; close asks, fades the picture, then releases it.
    const std::array<std::string,2>drops{n::mediaAssemblyUTF8(photo),n::mediaAssemblyUTF8(other)};check(owner.drop(drops,{},f.now),"Drop imports the first file");f.settle();f.frames(.5);check(f.session().document()->name=="Owned Photo.png","First dropped file");
    check(f.perform("close"),"Close");{const auto&request=f.overlay->context().overlay().confirmState().request();check(request&&request->id=="mediaClose"&&!request->danger&&request->message=="Owned Photo.png","Close of an unedited file asks with its name");}
    check(f.confirm("mediaClose"),"Confirm close");f.frames(.3);f.settle();for(int k=0;k<30;++k)f.frame(.01);
    check(!f.session().document()&&f.canvas().stage().isEmpty()&&owner.stats().retainedTextures==0,"The picture fades, then the preview texture is released ("+std::to_string(owner.stats().retainedTextures)+")");
    // Hide parks decoding.
    owner.setOverlayVisible(false,f.now);f.frame();check(!f.session().active(),"Hidden owner parks decoding");owner.setOverlayVisible(true,f.now);f.frame();
    f.finish();check(r.stats().textures==0&&r.stats().meshes==0,"Release leaves no resident resources");
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
        unsigned ids{};t::MediaAssemblyOwnerOptions options;options.uuid=[&]{return "parity-"+std::to_string(++ids);};options.gpuPreview=useGpu;
        Fixture f(r,hwnd,assets,std::move(options));auto&owner=f.o();
        f.modules.select(c::Module::mediaAssembly,f.now);for(int k=0;k<40;++k)f.frame();f.drain();f.frame();
        const std::array<std::string,1>drop{n::mediaAssemblyUTF8(photo)};check(owner.drop(drop,{},f.now),"Parity import");f.settle();f.frames(.6);
        check(f.perform("tool:adjust"),"Parity adjust");
        for(const auto&[id,value]:std::array<std::pair<std::string_view,double>,5>{{{"parameter:exposure",.5},{"parameter:saturation",1.6},{"parameter:temperature",4000},{"parameter:highlights",.4},{"parameter:shadows",.6}}}){
            check(f.canvas().adjustAccessibilitySlider(id,value,f.now),"Parity edit "+std::string(id));f.settle();}
        check(f.perform("toolBack")&&f.perform("mediaTab:1")&&f.perform("filter:sp_filter_2"),"Parity filter");f.settle();check(f.perform("mediaTab:0")&&f.perform("tool:rotate"),"Parity rotate");f.settle();
        for(int k=0;k<40;++k){f.frame();f.drain();}
        const auto&a=f.session().adjustments();check(a.rotationQuarterTurns==1&&a.filter!=m::MediaAssemblyFilter::none&&a.highlights==.4,"Parity edits applied");
        check(owner.stats().gpuActive==useGpu&&(useGpu?owner.stats().gpuPreviews>=7&&owner.controller().previewImage()->deferred():owner.stats().gpuPreviews==0&&!owner.controller().previewImage()->deferred()),"GPU previews only when enabled");
        r.draw(false);auto pixels=r.readback();
        // A hidden (parked) editor releases its bounded preview texture; showing
        // it again produces one fresh preview.
        owner.setOverlayVisible(false,f.now);for(int k=0;k<5;++k)f.frame();(void)owner.hostEvent({a::HostEvent::Kind::overlayConcealed,0},f.now);f.frame();
        check(!owner.controller().previewImage()&&owner.stats().retainedTextures==0,"Hidden editor releases its preview texture");
        owner.setOverlayVisible(true,f.now);for(int k=0;k<20;++k){f.frame();f.drain();}check(owner.controller().previewImage()&&owner.stats().retainedTextures==1,"Shown again with one fresh preview");
        f.finish();check(r.stats().textures==0,"Parity owner releases every texture");return pixels;
    };
    const auto gpu=scenario(true),cpu=scenario(false);check(gpu.width==cpu.width&&gpu.height==cpu.height&&gpu.pixels.size()==cpu.pixels.size()&&!gpu.pixels.empty(),"Comparable stage readbacks");
    int worst{};std::size_t over{},changed{};for(std::size_t k=0;k<gpu.pixels.size();++k){const int dv=std::abs(int(gpu.pixels[k])-int(cpu.pixels[k]));worst=std::max(worst,dv);over+=dv>2;changed+=dv>0;}
    std::cout<<"GPU/CPU preview stage worst="<<worst<<" over2="<<over<<" changed="<<changed<<" of "<<gpu.pixels.size()<<'\n';
    check(worst<=3&&over*1000<=gpu.pixels.size(),"GPU-edited previews compose like CPU-edited previews");
}
// Memory audit: what the owner keeps while shown, hidden inside the open HUD
// and after the HUD closed (source: setActive(false) clears the editor's
// preview and engine caches, the canvas layers die with the HUD). A synthetic
// 1000 x 750 still with a filter, edited on the renderer's device, at 2 px/pt.
void residency(n::Renderer&r,HWND hwnd,const fs::path&assets){
    Temp temp;const auto photo=temp.path/L"residency.png";writePNG(photo,1000,750);
    unsigned ids{};t::MediaAssemblyOwnerOptions options;options.uuid=[&]{return "resident-"+std::to_string(++ids);};
    Fixture f(r,hwnd,assets,std::move(options),2);auto&owner=f.o();
    struct Usage{t::MediaAssemblyOwnerStats owner;n::RendererStats gpu;n::LayerRasterStats raster;std::size_t cached{};};
    const auto usage=[&]{return Usage{owner.stats(),r.stats(),f.raster.stats(),f.engine->stats().cachedSourceBytes};};
    const auto print=[](const char*state,const Usage&u){
        std::cout<<"Media Assembly residency, "<<state<<": canvas "<<u.owner.sceneParts<<" parts / "<<u.owner.scenePixelBytes<<" raster bytes, preview pixels "<<u.owner.previewPixelBytes
                 <<", GPU processors "<<u.owner.gpuResidentBytes+u.owner.playbackResidentBytes<<", thumbnails "<<u.owner.thumbnailBytes<<", engine cache "<<u.cached<<", renderer textures+meshes "<<u.gpu.resourceBytes
                 <<" ("<<u.gpu.textures<<" textures) groups "<<u.gpu.nativeGroupBytes<<" media "<<u.gpu.mediaLiveBytes<<", shared rasterizer "<<u.raster.entries<<" entries + "<<u.raster.decodedImages<<" decoded images / "<<u.raster.resourceBytes<<" bytes\n";};
    f.modules.select(c::Module::mediaAssembly,f.now);for(int k=0;k<40;++k)f.frame();f.drain();f.frame();
    const std::array<std::string,1>drop{n::mediaAssemblyUTF8(photo)};check(owner.drop(drop,{},f.now),"Residency import");f.settle();f.frames(.6);
    check(f.perform("mediaTab:1")&&f.perform("filter:sp_filter_2"),"Residency filter");f.settle();f.frames(.4);check(f.perform("mediaTab:0"),"Tiles");for(int k=0;k<20;++k){f.frame();f.drain();}
    check(owner.stats().gpuActive&&owner.controller().previewImage()&&owner.controller().previewImage()->deferred()&&f.session().adjustments().filter!=m::MediaAssemblyFilter::none,
          "Residency: a filtered still edited on the renderer's device");
    const auto shown=usage();print("shown",shown);
    check(shown.owner.sceneParts>0&&shown.owner.scenePixelBytes>0&&shown.owner.previewPixelBytes>=1000u*750*4&&shown.owner.gpuResidentBytes>=1000u*750*4&&shown.cached>=1000u*750*4&&shown.owner.retainedTextures==1,
          "A shown editor holds its canvas, one bounded source (shared by the engine cache and the GPU upload) and its GPU edit");
    // Hidden inside the open HUD (another module): source setActive(false).
    f.modules.select(c::Module::power,f.now);for(int k=0;k<40;++k)f.frame();f.drain();f.frame();
    const auto hidden=usage();print("hidden in the open HUD",hidden);
    check(hidden.owner.previewPixelBytes==0&&hidden.owner.gpuResidentBytes==0&&hidden.cached==0&&hidden.owner.retainedTextures==0&&hidden.gpu.mediaLiveBytes<shown.gpu.mediaLiveBytes,
          "Hidden: no decoded source, preview pixels, GPU source or preview texture remains (released "+std::to_string(shown.owner.previewPixelBytes)+" CPU and "+std::to_string(shown.owner.gpuResidentBytes)+" processor bytes)");
    check(hidden.owner.sceneParts==shown.owner.sceneParts&&hidden.owner.parks==0,"Hidden inside the open HUD the canvas stays, so switching back rebuilds nothing");
    // The HUD closes while Media Assembly is hidden: its canvas keeps no artwork.
    owner.setOverlayVisible(false,f.now);for(int k=0;k<3;++k)f.frame();(void)owner.hostEvent({a::HostEvent::Kind::overlayConcealed,0},f.now);f.frame();
    const auto closed=usage();print("after the HUD closed",closed);
    check(closed.owner.parks>=1&&closed.owner.sceneParts==0&&closed.raster.entries<hidden.raster.entries&&closed.owner.retainedTextures==0,
          "After the HUD closed a hidden Media Assembly keeps no canvas artwork, shared rasters or textures (released "+std::to_string(hidden.raster.entries-closed.raster.entries)+" entries)");
    check(!closed.owner.gpuActive&&!closed.gpu.mediaDeviceLent&&r.deviceReleasable(),"After the HUD closed Media Assembly holds no GPU processor, so nothing of it keeps the renderer's device");
    // Reopened on Media Assembly: the canvas and one fresh preview come back.
    owner.setOverlayVisible(true,f.now);f.modules.select(c::Module::mediaAssembly,f.now);for(int k=0;k<40;++k){f.frame();f.drain();}
    const auto again=usage();print("shown again",again);
    check(again.owner.sceneParts>0&&again.owner.retainedTextures==1&&again.owner.previewPixelBytes==shown.owner.previewPixelBytes&&
          owner.controller().previewImage()&&f.session().adjustments().filter!=m::MediaAssemblyFilter::none,"Shown again: the canvas and one fresh edited preview are rebuilt from the session");
    check(again.owner.gpuActive&&owner.controller().previewImage()->deferred()&&again.gpu.mediaDeviceLent,"Shown again: the GPU processor is created again on the renderer's device");
    // Closing on Media Assembly: the preview and its GPU source go at once; the
    // host's one publication after overlayConcealed (Application::releaseConcealedGraphics)
    // retires their textures.
    owner.setOverlayVisible(false,f.now);for(int k=0;k<3;++k)f.frame();(void)owner.hostEvent({a::HostEvent::Kind::overlayConcealed,0},f.now);f.frame();
    const auto selected=usage();print("after closing on Media Assembly",selected);
    check(selected.owner.previewPixelBytes==0&&selected.owner.gpuResidentBytes==0&&selected.cached==0&&selected.owner.retainedTextures==0,"Closing on Media Assembly keeps no preview");
    check(!selected.owner.gpuActive&&!selected.gpu.mediaDeviceLent&&r.deviceReleasable()&&r.suspendDevice()&&r.stats().devicesDestroyed==r.stats().devicesCreated,
          "Closed on Media Assembly, the renderer's device can be released and is destroyed");
    check(r.resumeDevice(),"The device comes back for the rest of the fixture");
    f.finish();check(r.stats().textures==0&&r.stats().nativeGroups==0,"Residency owner releases every texture and group");
}
// Fake IMFMediaEngine for the shared broker (no codec, audio device or file).
struct Probe {n::NativeNotesVideoPlayback::Notify notify;double position{};bool playing{},fresh{true},stopped{};unsigned opens{},seeks{},frames{};std::string path;ComPtr<IDXGISurface>surface;};
// Every played frame is this straight BGRA8 colour (opaque, distinct channels).
constexpr std::array<std::uint8_t,4>playedBGRA{40,120,200,255};
std::array<std::uint8_t,4>firstPixel(IDXGISurface*surface){
    ComPtr<ID3D11Texture2D>texture;check(surface&&SUCCEEDED(surface->QueryInterface(IID_PPV_ARGS(&texture))),"Frame-server surface is a texture");
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Device>device;texture->GetDevice(&device);ComPtr<ID3D11Texture2D>staging;check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)),"Staging readback");
    ComPtr<ID3D11DeviceContext>context;device->GetImmediateContext(&context);context->CopyResource(staging.Get(),texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Map readback");
    std::array<std::uint8_t,4>out{};std::copy_n(static_cast<const std::uint8_t*>(mapped.pData),4,out.begin());context->Unmap(staging.Get(),0);return out;
}
class FakeEngine final:public n::NativeNotesVideoPlayback::Engine {
    std::shared_ptr<Probe>p_;
public:explicit FakeEngine(std::shared_ptr<Probe>p):p_(std::move(p)){}~FakeEngine()override{stop();}
    HRESULT open(const n::NotesVideoRequest&q)override{++p_->opens;p_->path=q.path;p_->notify(n::NativeNotesVideoPlayback::ready|n::NativeNotesVideoPlayback::firstFrame,S_OK);return S_OK;}
    HRESULT metadata(n::NotesVideoMetadata&out)override{out={4,4,10};return S_OK;}
    HRESULT play()override{p_->playing=true;p_->fresh=true;return S_OK;}HRESULT pause()override{p_->playing=false;return S_OK;}
    HRESULT seek(double t)override{p_->position=t;++p_->seeks;p_->fresh=true;p_->notify(n::NativeNotesVideoPlayback::seeked,S_OK);return S_OK;}
    double currentTime()const override{return p_->position;}
    HRESULT tick(std::int64_t&pts)override{pts=static_cast<std::int64_t>(p_->position*10000000);if(!p_->fresh)return S_FALSE;p_->fresh=false;return S_OK;}
    HRESULT transfer(void*raw,unsigned width,unsigned height)override{ComPtr<ID3D11Texture2D>texture;const auto hr=static_cast<IDXGISurface*>(raw)->QueryInterface(IID_PPV_ARGS(&texture));if(FAILED(hr))return hr;ComPtr<ID3D11Device>device;texture->GetDevice(&device);ComPtr<ID3D11DeviceContext>context;device->GetImmediateContext(&context);
        std::vector<std::uint8_t>pixels(std::size_t(width)*height*4);for(std::size_t i=0;i<pixels.size();i+=4)std::copy(playedBGRA.begin(),playedBGRA.end(),pixels.begin()+std::ptrdiff_t(i));
        context->UpdateSubresource(texture.Get(),0,nullptr,pixels.data(),width*4,0);p_->surface=static_cast<IDXGISurface*>(raw);++p_->frames;return S_OK;}
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
    unsigned ids{};t::MediaAssemblyOwnerOptions options;options.uuid=[&]{return "video-"+std::to_string(++ids);};options.broker=&broker;options.client=client;
    Fixture f(r,hwnd,assets,std::move(options));auto&owner=f.o();const auto&session=f.session();
    auto pump=[&]{MSG msg{};while(PeekMessageW(&msg,hwnd,notice,notice,PM_REMOVE))broker.accept(msg.wParam,f.now);broker.sample(f.now);owner.refreshSharedMedia(f.now);};
    // The fake engine clock advances with the shared frame clock while playing.
    auto frame=[&](double step=.02){f.frame(step,[&]{if(probe&&probe->playing){probe->position+=step;probe->fresh=true;}pump();});broker.collectRetired();};
    auto settle=[&]{for(int k=0;k<6;++k){f.drain();frame();}};
    f.modules.select(c::Module::mediaAssembly,f.now);for(int k=0;k<30;++k)frame();f.drain();frame();
    const std::array<std::string,1>drop{n::mediaAssemblyUTF8(movie)};check(owner.drop(drop,{},f.now),"Movie import");settle();for(int k=0;k<30;++k)frame();
    check(session.document()&&session.document()->video&&owner.controller().previewImage(),"Owned movie opened with a bounded poster");
    const double playStart=f.now;check(f.perform("play")&&session.wantsPlayback(),"Play routed to the shared broker");frame();
    check(engines.empty(),"The original is re-checked on the utility queue before a player exists (source makePlaybackItem)");f.drain();for(int k=0;k<10;++k)frame();
    check(engines.size()==1&&probe->opens==1&&probe->playing&&owner.stats().playbackStarts==1,"One native engine opened and playing");check(owner.stats().videoShown&&session.view().playing&&f.canvas().stage().videoShown(),"Live player texture replaces the poster while playing");
    check(owner.stats().pictureDraws==2,"The video is drawn over the poster on the stage's picture");
    const auto duration=session.document()->duration;std::cout<<"owned movie duration "<<duration<<'\n';check(duration>.3,"Fixture long enough for a range");
    for(int k=0;k<20;++k)frame();
    check(session.currentTime()>0&&session.currentTime()<=f.now-playStart+.001&&f.now-playStart-session.currentTime()<.5,"Position follows the shared frame clock at the source observer granularity");
    check(owner.nextWakeTime(f.now)&&*owner.nextWakeTime(f.now)>f.now&&*owner.nextWakeTime(f.now)<=f.now+duration-session.currentTime()+.001,"One end-of-range host deadline replaces a periodic observer");
    // Edit while playing: the player is released and re-prepared after the new poster.
    check(f.perform("tool:mirror"),"Mirror while playing");check(owner.stats().playbackReleases==1&&!owner.stats().videoShown,"An edit releases the player");f.drain();for(int k=0;k<20;++k){frame();f.drain();}
    check(owner.stats().playbackStarts==2&&engines.size()==2&&engines[0]->stopped&&probe->playing,"Playback resumes with a new player and the new edit after the bounded preview");
    // End of the trim range stops playback (source forwardPlaybackEndTime).
    unsigned wakes{};for(;wakes<6&&session.wantsPlayback();++wakes){const auto wake=owner.nextWakeTime(f.now);check(wake.has_value(),"Resumed playback schedules its end deadline");if(probe->playing)probe->position+=*wake-f.now;f.now=*wake;check(owner.deadline(f.now),"A scheduled deadline fires");frame(.01);}
    check(wakes<=2,"Playback ends on its single end-of-range deadline");
    check(!session.wantsPlayback()&&!session.view().playing&&!owner.stats().videoShown&&!probe->playing,"Trim end stops playback and shows the poster");
    check(f.perform("play"),"Play again");f.drain();for(int k=0;k<6;++k)frame();check(probe->playing&&probe->position<duration*.1,"Replaying near the end restarts at the trim start (engines "+std::to_string(engines.size())+", playing "+std::to_string(probe->playing)+", position "+std::to_string(probe->position)+", session "+std::to_string(session.currentTime())+")");
    // Colour edits while playing (source AVVideoComposition).
    const bool bt709=session.document()->bt709;check(bt709,"The owned movie is read as BT.709 like AVFoundation reads it");
    const std::vector<std::uint8_t>playedRGBA{playedBGRA[2],playedBGRA[1],playedBGRA[0],playedBGRA[3]};
    const auto playedAs=[&](const m::MediaAssemblyAdjustments&adj){auto colour=adj;colour.crop={};colour.rotationQuarterTurns=0;colour.mirrored=false;colour.trimStart=0;colour.trimEnd.reset();colour.stickers.clear();
        return m::MediaAssemblyFrameProcessor(1,1,colour,{},false,{},true).render({1,1,4,playedRGBA,false,m::mediaAssemblyVideoColour().get()},m::MediaAssemblyOutputAlpha::straight);};
    const auto sameBGRA=[](const std::array<std::uint8_t,4>&bgra,const std::vector<std::uint8_t>&rgba){return std::abs(int(bgra[0])-int(rgba[2]))<=1&&std::abs(int(bgra[1])-int(rgba[1]))<=1&&std::abs(int(bgra[2])-int(rgba[0]))<=1&&bgra[3]==rgba[3];};
    {const auto reading=playedAs(session.adjustments());const auto shown=probe->surface?firstPixel(probe->surface.Get()):std::array<std::uint8_t,4>{};
        check(owner.playbackFrameFilter()&&*owner.playbackFrameFilter()&&owner.stats().filteredPlayers>=1,"A BT.709 movie's player carries the played-frame filter with geometry-only edits");
        check(probe->surface&&sameBGRA(shown,reading)&&std::abs(int(reading[0])-int(playedRGBA[0]))>=4,"Frames played through the broker show the CoreMedia 709 reading of the decoded values");}
    {   // Highlights/Shadows applies no change on Windows (documented gap).
        check(f.perform("tool:adjust")&&f.canvas().adjustAccessibilitySlider("parameter:shadows",.6,f.now),"Shadows while playing");f.drain();for(int k=0;k<20;++k){frame();f.drain();}
        check(probe->playing&&owner.playbackFrameFilter()&&session.adjustments().shadows==.6&&probe->surface&&sameBGRA(firstPixel(probe->surface.Get()),playedAs({})),"Highlights/Shadows adds nothing to the played-frame filter (documented gap)");}
    {const auto starts=owner.stats().playbackStarts,filtered=owner.stats().filteredPlayers;
        check(f.canvas().adjustAccessibilitySlider("parameter:brightness",.25,f.now),"Brightness while playing");f.drain();for(int k=0;k<20;++k){frame();f.drain();}
        const auto filter=owner.playbackFrameFilter();
        check(owner.stats().playbackStarts==starts+1&&probe->playing&&owner.stats().filteredPlayers==filtered+1&&filter&&*filter,"The resumed player carries the colour-edit filter");
        const auto expected=playedAs(session.adjustments());const auto close=[&](const std::array<std::uint8_t,4>&bgra){return sameBGRA(bgra,expected);};
        check(std::abs(int(expected[0])-int(playedAs({})[0]))>8,"Brightness visibly changes the played colour");
        {   const auto target=r.createMediaTexture("media.assembly.test.played",4,4);
            ComPtr<ID3D11Texture2D>texture;check(SUCCEEDED(static_cast<IDXGISurface*>(target->targetSurface())->QueryInterface(IID_PPV_ARGS(&texture))),"Media target texture");
            ComPtr<ID3D11Device>device;texture->GetDevice(&device);ComPtr<ID3D11DeviceContext>context;device->GetImmediateContext(&context);
            std::vector<std::uint8_t>pixels(4*4*4);for(std::size_t i=0;i<pixels.size();i+=4)std::copy(playedBGRA.begin(),playedBGRA.end(),pixels.begin()+std::ptrdiff_t(i));
            context->UpdateSubresource(texture.Get(),0,nullptr,pixels.data(),16,0);const auto frames=owner.stats().filteredFrames;(*filter)(target->targetSurface());
            const auto edited=firstPixel(static_cast<IDXGISurface*>(target->targetSurface()));
            check(close(edited)&&owner.stats().filteredFrames==frames+1,"The played-frame filter edits a frame in place like the CPU video compositor");
            check(r.removeTexture("media.assembly.test.played"),"Test media target released");}
        check(owner.stats().frameFilterRoute,"The shared media broker carries the played-frame filter");
        check(owner.stats().filteredFrames>1&&probe->surface&&close(firstPixel(probe->surface.Get())),"Frames played through the shared broker carry the colour edits");
        check(f.perform("toolBack"),"Leaving the adjust tool");f.frames(.3);}
    const auto playing=owner.stats().playbackResidentBytes;
    check(f.perform("pause"),"Pause");for(int k=0;k<4;++k)frame();check(!probe->playing&&!owner.stats().videoShown,"Pause releases the player");
    check(!owner.playbackFrameFilter(),"A released player keeps no filter");
    check(playing>0&&owner.stats().playbackResidentBytes==0,"A released player's frame processor keeps no textures");
    // Source bookmark: a renamed or moved original keeps playing where it now is.
    {const auto folder=temp.path/L"moved";fs::create_directory(folder);const auto moved=folder/L"renamed movie.mp4";
        const auto rename=[&](const fs::path&from,const fs::path&to){std::error_code e;for(int k=0;k<100;++k){fs::rename(from,to,e);if(!e)return true;f.drain();frame();}return false;};
        check(rename(movie,moved),"The owned movie is renamed into another folder");
        const auto opened=engines.size();check(f.perform("play"),"Play the moved original");f.drain();for(int k=0;k<6;++k){frame();f.drain();}
        check(engines.size()==opened+1&&probe->playing&&!session.error()&&n::mediaAssemblySamePath(probe->path,n::mediaAssemblyUTF8(moved)),"The player opens the moved original where it now is");
        check(f.perform("pause"),"Pause");for(int k=0;k<4;++k)frame();check(!probe->playing,"Paused again");
        check(rename(moved,movie),"The owned movie is moved back");fs::remove(folder);}
    // Source makePlaybackItem: a changed or removed original never reaches a player.
    {const auto opened=engines.size();f.drain();for(int k=0;k<4;++k){frame();f.drain();}
        const auto size=[&]{std::ifstream in(movie,std::ios::binary|std::ios::ate);return static_cast<long long>(in.tellg());};const auto before=size();
        for(int k=0;k<100&&size()==before;++k){{std::ofstream(movie,std::ios::app|std::ios::binary)<<'x';}if(size()==before){f.drain();frame();}}
        check(size()==before+1,"The owned movie changes on disk once");
        check(f.perform("play"),"Play with a changed original");f.drain();for(int k=0;k<4;++k){frame();f.drain();}
        check(session.error()==m::MediaAssemblyError::changedOnDisk&&!session.wantsPlayback()&&engines.size()==opened&&!owner.stats().videoShown,"A changed original is reported before a player opens");
        {std::error_code e;for(int k=0;k<100&&!fs::remove(movie,e);++k){f.drain();frame();}}check(!fs::exists(movie),"The owned movie is removed");
        check(f.perform("play"),"Play with a removed original");f.drain();for(int k=0;k<4;++k){frame();f.drain();}
        check(session.error()==m::MediaAssemblyError::sourceUnavailable&&!session.wantsPlayback()&&engines.size()==opened,"A removed original asks to be chosen again before playback");}
    owner.setOverlayVisible(false,f.now);frame();f.finish();broker.hideVideos(client,f.now);broker.collectRetired();
    for(auto&e:engines)e->surface.Reset(); // borrowed frame-server surfaces leave with the renderer
}
}
int wmain(int argc,wchar_t**argv){Apartment apartment;try{frameFilterHook();check(SUCCEEDED(apartment.hr)&&argc==4,"Pass HLSL, Resources/MediaAssembly and the pinned silent movie");Window window;
        {n::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{n::Driver::warpForTests,argv[1],n::RenderTarget::offscreenForTests,true});run(renderer,window.hwnd,fs::absolute(argv[2]));renderer.reset();}
        {n::Renderer same;same.initialize(window.hwnd,1280,800,{n::Driver::warpForTests,argv[1],n::RenderTarget::offscreenForTests,true});parity(same,window.hwnd,fs::absolute(argv[2]));same.reset();}
        {n::Renderer audit;audit.initialize(window.hwnd,1280,800,{n::Driver::warpForTests,argv[1],n::RenderTarget::offscreenForTests,true});residency(audit,window.hwnd,fs::absolute(argv[2]));audit.reset();}
        n::Renderer media;media.initialize(window.hwnd,1280,800,{n::Driver::warpForTests,argv[1],n::RenderTarget::offscreenForTests,true});playback(media,window.hwnd,fs::absolute(argv[2]),fs::absolute(argv[3]));media.reset();std::cout<<"Media Assembly owner: "<<checks<<" checks passed (synthetic media only)\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"Media Assembly owner failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
#include <iostream>
int main(){std::cout<<"Media Assembly owner needs Windows\n";return 0;}
#endif
