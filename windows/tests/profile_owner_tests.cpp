// Production Personal ID owner (tools/profile_owner.hpp) on the design library:
// worker load, Work Mode lifetime hours, account sync and per-field locks, the
// host chooser and the posted colour dialog, managed images, the portrait's
// panning images on the worker, the owner-drawn background art (inserted after
// the canvas's art slot, clipped by the box and the shared centre ramp), crop
// events, the bottom-left card binding and its worker-generated textures, and
// release. Hidden HWND, WARP and a new temporary data root only; the pickers
// are injected (no dialog is shown).
#include "tools/profile_owner.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
namespace {
using namespace endfield;namespace m=modules;namespace gpu=native;namespace d=modules::design;using J=ehud::data::Json;using Microsoft::WRL::ComPtr;
unsigned checks{};
void check(bool v,const std::string&why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>bool rejects(F f){try{f();}catch(const std::exception&){return true;}return false;}
struct Window{HWND hwnd{};Window(){hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"Owned hidden Personal ID production owner fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Owner fixture remains hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);}};
struct Temporary{std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-Profile-Owner-"+ehud::data::makeUUID());Temporary(){std::filesystem::create_directories(root);}~Temporary(){std::error_code e;std::filesystem::remove_all(root,e);}};
std::vector<std::uint8_t>read(const std::filesystem::path&p){const auto bytes=ehud::data::detail::readFile(p,64*1024*1024);check(bytes.has_value(),"Read owned fixture");return {bytes->begin(),bytes->end()};}
// Synthetic PNG written only into the owned temporary folder.
void encode(const std::filesystem::path&path,UINT width,UINT height){
    ComPtr<IWICImagingFactory>f;check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f))),"WIC factory");
    ComPtr<IWICStream>stream;check(SUCCEEDED(f->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"Fixture stream");
    ComPtr<IWICBitmapEncoder>encoder;check(SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)),"Fixture encoder");
    ComPtr<IWICBitmapFrameEncode>frame;ComPtr<IPropertyBag2>bag;check(SUCCEEDED(encoder->CreateNewFrame(&frame,&bag))&&SUCCEEDED(frame->Initialize(bag.Get()))&&SUCCEEDED(frame->SetSize(width,height)),"Fixture frame");
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;check(SUCCEEDED(frame->SetPixelFormat(&format))&&format==GUID_WICPixelFormat24bppBGR,"Fixture format");
    const UINT stride=(width*3+3)&~3u;std::vector<BYTE>pixels(std::size_t(stride)*height);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){auto*p=&pixels[std::size_t(y)*stride+x*3];p[0]=BYTE(x);p[1]=BYTE(y);p[2]=BYTE(x+y);}
    check(SUCCEEDED(frame->WritePixels(height,stride,UINT(pixels.size()),pixels.data()))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"Fixture commit");
}
// Hidden WARP figures of the page (printed; Phase D compares them with v1.2.0).
struct Perf {double firstFrameMS{},idleFrameMS{},pointerMoveMS{};std::size_t surfaces{},rasterEntries{},pixelBytes{};}perf;
std::string digest(const m::ProfileImage&image){return core::packet::sha256(std::span<const std::uint8_t>(image.rgba));}
// A fresh store invents its own local UID; the owned file is rewritten with a
// fixed one, a known awakening instant and a stored lifetime.
void seed(const std::filesystem::path&root,double work){
    const auto path=ehud::data::ProfileStore(root).path();
    const auto bytes=ehud::data::detail::readFile(path,1024*1024);check(bytes.has_value(),"Read the fresh owned record");
    auto record=J::parse(*bytes);record["profile"]["uid"]="1000000000";record["profile"]["name"]="Endministrator";record["profile"]["accumulatedWorkSeconds"]=work;
    record["profile"]["awakeningDate"]=(1767225600.-978307200.)-3600; // 2025-12-31 23:00 UTC
    std::ofstream(path,std::ios::binary|std::ios::trunc)<<record.encode();
    check(ehud::data::ProfileStore(root).value().uid=="1000000000","Seeded record reads back");
}
void run(const std::filesystem::path&shader,const std::filesystem::path&resources){
    Window w;Temporary t;seed(t.root,3600);
    gpu::Renderer renderer;renderer.initialize(w.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(1280,800));
    gpu::LayerRasterizer raster;
    tools::DesignOverlayOptions overlayOptions;overlayOptions.raster.pixelsPerPoint=1;overlayOptions.window=w.hwnd;
    tools::DesignOverlayOwner overlay(raster,overlayOptions);auto&context=overlay.context();
    app::UtilityExecutor executor([]{},8);
    const auto settle=[&]{for(int n=0;n<16;++n){executor.waitIdle();if(!executor.drain())break;}};
    std::unique_ptr<tools::ProfileOwner>owner;
    m::WorkModeController controller({{},[&](double total){if(owner)owner->workModeCheckpoint(total);}});
    // Injected host pickers and sinks.
    tools::ProfileOwnerOptions::ImageCompletion chooser;unsigned imageAsks{},colorAsks{},cancels{};std::vector<std::array<double,3>>colorStarts;
    std::function<std::optional<std::array<double,3>>(const std::function<void(std::array<double,3>)>&)>colorScript;
    std::vector<std::string>crops;std::vector<std::optional<std::string>>avatars;unsigned cardChanges{},changes{},loads{};std::int64_t best{3262};
    double time{};
    tools::ProfileOwnerOptions o;o.appRoot=t.root;o.resources=resources;o.timeZone=u"UTC";
    o.design.name="profile";o.design.raster.pixelsPerPoint=1;o.design.now=[&]{return time;};o.design.changed=[&]{++changes;};
    o.design.routing.pointer=30;o.design.routing.key=30;o.design.routing.message=30;
    o.requestImage=[&](m::ProfileImageKind,tools::ProfileOwnerOptions::ImageCompletion done){++imageAsks;chooser=std::move(done);return true;};
    o.cancelPicker=[&]{++cancels;if(chooser){auto c=std::exchange(chooser,{});c(std::nullopt);}};
    o.chooseColor=[&](std::array<double,3>initial,const std::function<void(std::array<double,3>)>&live){++colorAsks;colorStarts.push_back(initial);return colorScript?colorScript(live):std::nullopt;};
    o.minigameBest=[&]{return best;};
    o.cropChanged=[&](std::string_view target){crops.emplace_back(target);};o.avatarChanged=[&](const std::optional<std::string>&v){avatars.push_back(v);};
    o.cardChanged=[&]{++cardChanges;};o.loaded=[&](const m::PersonalProfile&p){++loads;check(p.uid=="1000000000","Loaded callback receives the committed record");};
    owner=std::make_unique<tools::ProfileOwner>(w.hwnd,executor,raster,context,controller,o);
    auto&core=owner->core();auto&canvas=*owner->canvas();
    const app::ModuleAppearance appearance{core::Language::english,true,{0xFA/255.,0xD4/255.,0x1F/255.,1},{0xFA/255.,0xD4/255.,0x1F/255.},false};
    overlay.resize({1280,800,96,1,1280,800});core.resize({1280,800,96,1,1280,800});
    overlay.setAppearance(appearance,0);core.setAppearance(appearance,0);core.focus(true,0);
    // 1. Work Mode counts before the profile is readable: retained, then added once.
    controller.chooseStopwatch(0);controller.start(0);controller.pause(120);
    check(!owner->loaded()&&!owner->state()&&!owner->card(),"Nothing is shown before the worker reads profile.json");
    owner->start(0);settle();
    check(owner->loaded()&&loads==1&&owner->state()&&!owner->loadError()&&canvas.store()==owner->state(),"Worker load gives the canvas its store");
    check(owner->state()->profile().accumulatedWorkSeconds==3720,"Early Work Mode session lands once above the stored lifetime");
    const auto*card=owner->card();check(card&&card->captions()[0].text=="Endministrator"&&card->captions()[1].text=="UID: 1000000000"&&card->captions()[4].text=="MAX","Card captions bind the loaded record");
    check(cardChanges>0&&owner->cardTexture(tools::IdCardTextureKind::background)&&owner->cardTexture(tools::IdCardTextureKind::hover)&&!owner->cardTexture(tools::IdCardTextureKind::avatar),"Worker generates the background and hover; the silhouette avatar stays packet-bound");
    const auto artwork=m::loadProfileSourceArtwork(resources);
    check(digest(owner->cardTexture(tools::IdCardTextureKind::background)->pixels)==artwork.defaultBackgroundSHA256,"No photo and the HUD accent reproduce the Mac packet background byte for byte");
    controller.resume(120);controller.pause(180);check(owner->state()->profile().accumulatedWorkSeconds==3780,"Checkpoints after load persist the absolute lifetime");
    owner->workModeCheckpoint(10);check(owner->state()->profile().accumulatedWorkSeconds==3780,"A lower total never lowers the lifetime");
    check(core.flush(180)&&ehud::data::ProfileStore(t.root).value().accumulatedWorkSeconds==3780,"Flush drains the lifetime to profile.json");
    // 2. The page: a design screen shown through the shared module presentation.
    core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::profile;
    core::ModulePresentation modules(core::Module::profile);modules.setDesignScreen(core::Module::profile,true);
    gpu::LayerComposition composition;time=180;std::vector<gpu::LayerCompositionEntry>all;
    const auto paint=[&](double advance=1./60){
        time+=advance;overlay.deadline(time);
        const auto sample=modules.sample(time).presentation;settings.module=sample.requested;
        overlay.update(app::ModuleFrame{core::Matrix4{},settings,sample,1,time,true});core.update(app::ModuleFrame{core::Matrix4{},settings,sample,1,time,true});
        owner->upload(renderer);overlay.upload(renderer);
        all.assign(core.entries(core::Module::profile).begin(),core.entries(core::Module::profile).end());
        composition.setEntries(renderer,all);composition.present(renderer);renderer.draw(false);owner->collected(renderer);
    };
    {core::ModulePresentation other(core::Module::notes);const auto sample=other.sample(time).presentation;core.prewarm(app::ModuleFrame{core::Matrix4{},settings,sample,0,time,true});
     check(!core.sceneStats(core::Module::profile)||core.sceneStats(core::Module::profile)->parts==0,"A launch prewarm for another module builds nothing for the page");}
    overlay.setOverlayVisible(true,time);core.setOverlayVisible(true,time);owner->setOverlayVisible(true,time);
    using Clock=std::chrono::steady_clock;const auto ms=[](Clock::time_point a){return std::chrono::duration<double,std::milli>(Clock::now()-a).count();};
    {const auto first=Clock::now();paint();perf.firstFrameMS=ms(first);}
    paint(.5);
    {double idle{},moves{};for(unsigned n=0;n<60;++n){const auto a=Clock::now();paint();idle+=ms(a);}perf.idleFrameMS=idle/60;
     for(unsigned n=0;n<60;++n){const auto a=Clock::now();core.pointer({app::PointerKind::move,app::PointerButton::none,500.+n*3,300.+n},time);moves+=ms(a);paint();}perf.pointerMoveMS=moves/60;
     core.pointer({app::PointerKind::leave,app::PointerButton::none,0,0},time);paint(.5);
     if(const auto scene=core.sceneStats(core::Module::profile)){perf.surfaces=scene->surfaces;perf.rasterEntries=scene->rasterEntries;perf.pixelBytes=scene->residentPixelBytes;}}
    check(!core.entries(core::Module::profile).empty()&&canvas.isActive(),"Personal ID is published as the active design page");
    check(canvas.bestText()=="3,262"&&canvas.hoursText()=="1.05","The best plate reads the minigame best once per visit; the hours plate max(stored, live)/3600");
    check(context.nextWakeTime().has_value()&&*context.nextWakeTime()<=time+30,"The 30 s hours refresh is a deadline on the shared clock (the overlay owner runs it)");
    const auto frameImages=owner->stats();
    // 3. Avatar chooser from the ⋯ menu: the host's queued dialog, imported on the worker.
    const auto photo=t.root/"portrait.png";encode(photo,300,200);
    check(canvas.perform("profile:menu",time)&&canvas.menuOpen()&&canvas.perform("profile.more:picture",time)&&imageAsks==1&&owner->pickerOpen()&&canvas.presentingPanel(),
          "Change profile picture asks the host chooser once and holds the HUD");
    {auto c=std::exchange(chooser,{});c(photo);}
    check(!owner->pickerOpen()&&!canvas.presentingPanel(),"The chooser's answer closes it");
    settle();const auto avatarName=owner->state()->profile().avatarFilename;
    check(avatarName&&avatarName->ends_with(".image")&&std::filesystem::exists(t.root/"Profile"/"Images"/ *avatarName),"Chosen picture becomes the managed avatar");
    check(avatars.size()==1&&avatars.back()==avatarName,"The account owner hears about the manual avatar");
    check(context.overlay().toastState().current()&&context.overlay().toastState().current()->title=="Picture changed","A successful avatar import toasts Picture changed");
    settle();paint();settle();paint();
    const auto*avatarTexture=owner->cardTexture(tools::IdCardTextureKind::avatar);
    check(avatarTexture&&avatarTexture->pixels.width==368&&avatarTexture->pixels.height==368,"Card avatar is the 136 pt crop at 2x with the source 1.35 oversampling");
    // 3b. The portrait: one panning image per zoom step on the worker.
    {
        const auto before=owner->stats();
        check(before.portraitPublications>=1&&before.portraitJobs==before.portraitPublications&&before.portraitBytes>0,"The panning image was made on the worker and published");
        check(canvas.perform("profile:menu",time)&&canvas.perform("profile.more:portrait",time)&&canvas.popoverOpen(),"Adjust portrait opens");
        check(canvas.adjustAccessibilitySlider("profile:avatarZoom",2.5,time)&&owner->stats().portraitJobs==before.portraitJobs+1,"A zoom into the next step queues one worker image");
        check(canvas.adjustAccessibilitySlider("profile:avatarZoom",2.8,time)&&owner->stats().portraitJobs==before.portraitJobs+1,"A zoom inside that step adds no job");
        settle();paint();
        check(owner->stats().portraitPublications==before.portraitPublications+1,"The step's image is published ("+std::to_string(before.portraitJobs)+"/"+std::to_string(before.portraitPublications)+" -> "+std::to_string(owner->stats().portraitJobs)+"/"+std::to_string(owner->stats().portraitPublications)+")");
        canvas.closePopover();paint();
    }
    // 4. Colour dialog: posted (modal outside input), live updates, cancel restores, the result commits.
    const auto pump=[&]{MSG msg;bool any{};while(PeekMessageW(&msg,w.hwnd,o.pickerMessage,o.pickerMessage,PM_REMOVE)){any=true;core.message({msg.hwnd,msg.message,msg.wParam,msg.lParam},time);}return any;};
    colorScript=[&](const std::function<void(std::array<double,3>)>&live){live({1,0,0});check(owner->state()->profile().themeColorHex=="FF0000"&&owner->card()->captions()[4].color[0]==1,"Live colour reaches the page and the card");return std::nullopt;};
    check(canvas.perform("profile:backgroundMenu",time)&&canvas.perform("theme:custom",time)&&colorAsks==0&&canvas.colorPanelOpen(),"Custom colour posts the dialog request");
    check(pump()&&colorAsks==1&&!canvas.colorPanelOpen(),"The posted request runs the colour dialog once");
    check(!owner->state()->profile().themeColorHex&&std::abs(colorStarts.back()[0]-0xFA/255.)<1e-9,"Cancel restores the colour it started from (follow HUD)");
    const auto hoverBefore=owner->card()->hoverRevision();
    colorScript=[](const std::function<void(std::array<double,3>)>&){return std::array<double,3>{0,0,1};};
    check(canvas.perform("theme:custom",time)&&pump(),"The colour dialog opens again");settle();
    check(owner->state()->profile().themeColorHex=="0000FF"&&owner->card()->captions()[4].color[2]==1&&owner->card()->hoverRevision()>hoverBefore,"Chosen colour commits and recolours the card");
    // 5. Crop events on committed snapshots only (the slider release saves once).
    check(canvas.adjustAccessibilitySlider("profile:thumbnailZoom",2,time)&&crops==std::vector<std::string>{"thumbnail"},"profileCropChanged {thumbnail}");
    canvas.adjustAccessibilitySlider("profile:backgroundOffsetX",40,time);check(crops.size()==1,"Other geometry is not a crop event");
    canvas.adjustAccessibilitySlider("profile:backgroundZoom",3,time);check(crops.size()==2&&crops.back()=="background","profileCropChanged {background}");
    // 6. Background art: imported, decoded, drawn by the owner after the art slot.
    {
        const auto art=t.root/"background.png";encode(art,400,300);
        check(canvas.perform("theme:background",time)&&imageAsks==2,"Background asks the chooser");
        {auto c=std::exchange(chooser,{});c(art);}
        settle();paint();settle();paint();paint();
        check(owner->state()->profile().backgroundFilename&&canvas.artBox()&&owner->stats().artResident&&owner->stats().rampResident&&owner->stats().artDraws>0,
              "The art layers exist and the owner draws the photo through the shared ramp");
        const auto uploads=owner->stats().artUploads,rasterizations=raster.stats().rasterizations;
        check(canvas.adjustAccessibilitySlider("profile:backgroundZoom",5,time)&&canvas.adjustAccessibilitySlider("profile:backgroundWidth",800,time),"Art zoom and width commit");
        for(unsigned n=0;n<20;++n)paint();
        check(owner->stats().artUploads==uploads,"Moving the art changes numbers, never re-uploads the photo");
        (void)rasterizations;
        renderer.draw(false);
        check(canvas.perform("theme:default",time)&&!owner->state()->profile().backgroundFilename,"Default removes the art");
        paint();paint();check(!canvas.artBox()&&owner->stats().artDraws>0,"Without art the page has no art layers");
        paint();check(!owner->stats().artResident,"The photo texture is released");
        canvas.closePopover();paint();
    }
    // 7. Account sync: per-field locks, game fields, game avatar.
    owner->setSyncLocked(ehud::data::ProfileSyncFields{}.with(ehud::data::ProfileSyncField::name).with(ehud::data::ProfileSyncField::uid),time);
    owner->acceptFromGame([](m::PersonalProfile&p){p.gamePlayerID="1234567890123";p.name="Synced";p.permissionLevel=42;},time);
    check(!canvas.canEdit(m::ProfileField::name)&&!canvas.canEdit(m::ProfileField::playerID)&&canvas.canEdit(m::ProfileField::permissionLevel)&&canvas.canEdit(m::ProfileField::introduction)&&
          owner->state()->profile().name=="Synced","Game fields apply; only the synced fields lock");
    check(owner->card()->captions()[1].text=="UID: 1234567890123"&&owner->card()->captions()[0].text=="Synced","Card shows the synced UID");
    check(rejects([&]{owner->acceptFromGame([](m::PersonalProfile&p){p.uid="999";},time);})&&owner->state()->profile().uid=="1000000000","The local UID is immutable");
    const auto gameAvatar=owner->importGameAvatar(read(photo),time);
    check(owner->state()->profile().avatarFilename==gameAvatar&&avatars.back()==gameAvatar,"Game avatar imports synchronously and is reported");
    check(rejects([&]{owner->importGameAvatar(std::vector<std::uint8_t>{1,2,3},time);})&&owner->state()->profile().avatarFilename==gameAvatar,"Invalid game avatar bytes throw and change nothing");
    // 8. Time-zone change, flush, release.
    check(m::profileValue(m::ProfileField::awakeningDate,owner->state()->profile(),owner->state()->dates())=="2025/12/31","Awakening day in UTC");
    owner->setTimeZone(u"Asia/Tokyo",time);check(m::profileValue(m::ProfileField::awakeningDate,owner->state()->profile(),owner->state()->dates())=="2026/01/01","Zone change re-renders the date");
    // Settled frames rasterize nothing.
    for(unsigned n=0;n<30;++n)paint();
    {const auto before=raster.stats().rasterizations;for(unsigned n=0;n<30;++n)paint();check(raster.stats().rasterizations==before&&!core.requiresFrames(time),"Settled page frames rasterize nothing and need no frames");}
    check(core.flush(time),"Orderly flush");settle();
    const auto saved=ehud::data::ProfileStore(t.root).value();
    check(saved.name=="Synced"&&saved.gamePlayerID=="1234567890123"&&saved.themeColorHex=="0000FF"&&saved.thumbnailZoom==1&&!saved.backgroundFilename&&saved.avatarFilename==gameAvatar,"profile.json holds the final record (Default reset the thumbnail)");
    check(!std::filesystem::exists(t.root/"Profile"/"Images"/ *avatarName),"The replaced manual avatar file is removed after the commit");
    (void)frameImages;
    core.overlayClosing(time);owner->setOverlayVisible(false,time);paint();
    composition.detach(renderer);owner->release(renderer);overlay.release(renderer);check(renderer.stats().textures==0&&renderer.stats().meshes==0,"Owner releases every GPU resource");
    owner.reset();renderer.reset();
    // 9. A record that cannot be read: the source message as the Notice toast, defaults, no writes, no card.
    {
        Temporary bad;std::filesystem::create_directories(bad.root/"Profile");{std::ofstream(bad.root/"Profile"/"profile.json")<<"{not json";}
        const auto before=read(bad.root/"Profile"/"profile.json");
        m::WorkModeController idle;tools::ProfileOwnerOptions b=o;b.appRoot=bad.root;b.loaded={};
        tools::ProfileOwner failed(w.hwnd,executor,raster,context,idle,b);failed.start(0);settle();
        check(!failed.loaded()&&failed.loadError()&&!failed.state()&&!failed.card()&&!failed.canvas()->store()&&!failed.canvas()->canEdit(m::ProfileField::name),
              "Unreadable record: no store, no card, no editing");
        check(rejects([&]{failed.acceptFromGame([](m::PersonalProfile&){},0);})&&rejects([&]{failed.importGameAvatar(read(photo),0);}),"Account writes report an unavailable store");
        check(failed.core().flush(0)&&read(bad.root/"Profile"/"profile.json")==before,"The unreadable record is preserved byte for byte");
    }
    // 10. An account update before the record loads is applied once it does.
    {
        Temporary fresh;seed(fresh.root,0);m::WorkModeController idle;tools::ProfileOwnerOptions f=o;f.appRoot=fresh.root;f.loaded={};
        tools::ProfileOwner early(w.hwnd,executor,raster,context,idle,f);early.setSyncLocked(ehud::data::ProfileSyncFields::all(),0);
        early.acceptFromGame([](m::PersonalProfile&p){p.gamePlayerID="77";p.name="Early";},0);early.start(0);settle();
        check(early.state()->profile().name=="Early"&&early.card()->captions()[1].text=="UID: 77"&&early.core().flush(0)&&ehud::data::ProfileStore(fresh.root).value().name=="Early","A queued account update lands after the load");
    }
    executor.shutdown();
}
}
int wmain(int argc,wchar_t**argv){
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{check(SUCCEEDED(hr)&&argc==3,"Pass the shared shader and windows/resources/profile");run(argv[1],argv[2]);CoUninitialize();
        std::cout<<"Personal ID WARP: first page frame "<<perf.firstFrameMS<<" ms, idle frame "<<perf.idleFrameMS<<" ms, pointer move "<<perf.pointerMoveMS<<" ms; "
                 <<perf.surfaces<<" surfaces, "<<perf.rasterEntries<<" raster entries, "<<perf.pixelBytes/1024<<" KiB resident\n";
        std::cout<<"PASS "<<checks<<" Personal ID production owner checks\n";return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
