// Production Personal Profile owner: worker load, Work Mode lifetime hours,
// account sync, deferred pickers, managed images, crop events, the bottom-left
// card binding and its worker-generated textures. Hidden HWND, WARP and a new
// temporary data root only; pickers are injected (no dialog is shown).
#include "tools/profile_owner.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
namespace {
using namespace endfield;namespace m=modules;namespace gpu=native;using J=ehud::data::Json;using Microsoft::WRL::ComPtr;
unsigned checks{};
void check(bool v,const std::string&why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>bool rejects(F f){try{f();}catch(const std::exception&){return true;}return false;}
struct Window{HWND hwnd{};Window(){hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"Owned hidden Personal Profile production owner fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Owner fixture remains hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);}};
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
std::string digest(const m::ProfileImage&image){return core::packet::sha256(std::span<const std::uint8_t>(image.rgba));}
// A fresh store invents its own local UID; the owned file is rewritten with a
// fixed one, a known awakening instant and a stored lifetime.
void seed(const std::filesystem::path&root,double work){
    const auto path=ehud::data::ProfileStore(root).path();
    const auto bytes=ehud::data::detail::readFile(path,1024*1024);check(bytes.has_value(),"Read the fresh owned record");
    auto record=J::parse(*bytes);record["profile"]["uid"]="1000000000";record["profile"]["accumulatedWorkSeconds"]=work;
    record["profile"]["awakeningDate"]=(1767225600.-978307200.)-3600; // 2025-12-31 23:00 UTC
    std::ofstream(path,std::ios::binary|std::ios::trunc)<<record.encode();
    check(ehud::data::ProfileStore(root).value().uid=="1000000000","Seeded record reads back");
}
void run(const std::filesystem::path&shader,const std::filesystem::path&resources){
    Window w;Temporary t;seed(t.root,3600);
    gpu::Renderer renderer;renderer.initialize(w.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(1280,800));
    gpu::LayerRasterizer raster;gpu::LayerImageSource imageSource;
    app::UtilityExecutor executor([]{},8);
    const auto settle=[&]{for(int n=0;n<16;++n){executor.waitIdle();if(!executor.drain())break;}};
    std::unique_ptr<tools::ProfileOwner>owner;
    m::WorkModeController controller({{},[&](double total){if(owner)owner->workModeCheckpoint(total);}});
    // Injected host pickers and sinks.
    std::optional<std::filesystem::path>nextImage;unsigned imageAsks{},colorAsks{};std::vector<std::array<double,3>>colorStarts;
    std::function<std::optional<std::array<double,3>>(const std::function<void(std::array<double,3>)>&)>colorScript;
    std::vector<std::string>crops;std::vector<std::optional<std::string>>avatars;unsigned cardChanges{},changes{},loads{};
    tools::ProfileOwnerOptions o;o.appRoot=t.root;o.resources=resources;o.timeZone=u"UTC";o.raster.pixelsPerPoint=1;
    bool hideDuringPicker{};unsigned cancels{};
    o.chooseImage=[&](m::ProfileImageKind){++imageAsks;if(hideDuringPicker)owner->setOverlayVisible(false,180);return nextImage;};
    o.cancelPicker=[&]{++cancels;};
    o.chooseColor=[&](std::array<double,3>initial,const std::function<void(std::array<double,3>)>&live){++colorAsks;colorStarts.push_back(initial);return colorScript?colorScript(live):std::nullopt;};
    o.cropChanged=[&](std::string_view target){crops.emplace_back(target);};o.avatarChanged=[&](const std::optional<std::string>&v){avatars.push_back(v);};
    o.cardChanged=[&]{++cardChanges;};o.changed=[&]{++changes;};o.loaded=[&](const m::PersonalProfile&p){++loads;check(p.uid=="1000000000","Loaded callback receives the committed record");};
    owner=std::make_unique<tools::ProfileOwner>(w.hwnd,executor,raster,imageSource,controller,o);
    owner->resize({1280,800,96,1,1280,800});owner->setAppearance({core::Language::english,true,{0xFA/255.,0xD4/255.,0x1F/255.},1,false},0);owner->focus(true,0);
    // 1. Work Mode counts before the profile is readable: retained, then added once.
    controller.chooseStopwatch(0);controller.start(0);controller.pause(120);
    check(!owner->loaded()&&!owner->state()&&!owner->card(),"Nothing is shown before the worker reads profile.json");
    owner->start(0);settle();
    check(owner->loaded()&&loads==1&&owner->state()&&!owner->loadError(),"Worker load constructs the page state");
    check(owner->state()->profile().accumulatedWorkSeconds==3720,"Early Work Mode session lands once above the stored lifetime");
    const auto*card=owner->card();check(card&&card->captions()[0].text=="Endministrator"&&card->captions()[1].text=="UID: 1000000000"&&card->captions()[4].text=="MAX","Card captions bind the loaded record");
    check(cardChanges>0&&owner->cardTexture(tools::IdCardTextureKind::background)&&owner->cardTexture(tools::IdCardTextureKind::hover)&&!owner->cardTexture(tools::IdCardTextureKind::avatar),"Worker generates the background and hover; the silhouette avatar stays packet-bound");
    const auto artwork=m::loadProfileSourceArtwork(resources);
    check(digest(owner->cardTexture(tools::IdCardTextureKind::background)->pixels)==artwork.defaultBackgroundSHA256&&owner->cardTexture(tools::IdCardTextureKind::background)->revision==card->backgroundRevision(),"No photo and the HUD accent reproduce the Mac packet background byte for byte");
    controller.resume(120);controller.pause(180);check(owner->state()->profile().accumulatedWorkSeconds==3780,"Checkpoints after load persist the absolute lifetime");
    owner->workModeCheckpoint(10);check(owner->state()->profile().accumulatedWorkSeconds==3780,"A lower total never lowers the lifetime");
    check(owner->flush(180)&&ehud::data::ProfileStore(t.root).value().accumulatedWorkSeconds==3780,"Flush drains the lifetime to profile.json");
    // 2. The page: shown through the shared module presentation.
    core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::profile;core::ModulePresentation modules(core::Module::profile);
    gpu::LayerComposition composition;double time=180;
    const auto paint=[&](double advance=1./60){
        time+=advance;const auto sample=modules.sample(time).presentation;settings.module=sample.requested;owner->update({},settings,sample,1,time);
        owner->upload(renderer);composition.setEntries(renderer,owner->entries());composition.present(renderer);renderer.draw(false);owner->collected(renderer);
    };
    const auto pump=[&]{MSG msg;bool any{};while(PeekMessageW(&msg,w.hwnd,o.pickerMessage,o.pickerMessage,PM_REMOVE)){any=true;owner->message({msg.hwnd,msg.message,msg.wParam,msg.lParam},time);}return any;};
    // A request while the page is closed never opens a picker.
    check(owner->perform("profile:avatar",time)&&pump()&&imageAsks==0,"Picker requests need the active page");
    paint();paint(.5);check(!owner->entries().empty()&&owner->page()->active(),"Profile page is published");
    check(owner->nextWakeTime(time).has_value(),"Active page schedules its work refresh on the shared clock");
    // 3. Avatar picker: deferred out of page input, imported on the worker.
    const auto photo=t.root/"portrait.png";encode(photo,300,200);nextImage=photo;
    check(owner->perform("profile:avatar",time)&&imageAsks==0,"The picker never runs inside page input");
    check(pump()&&imageAsks==1&&!owner->pickerOpen(),"The posted request runs the host picker once");
    settle();const auto avatarName=owner->state()->profile().avatarFilename;
    check(avatarName&&avatarName->ends_with(".image")&&std::filesystem::exists(t.root/"Profile"/"Images"/ *avatarName),"Chosen picture becomes the managed avatar");
    check(avatars.size()==1&&avatars.back()==avatarName,"The account owner hears about the manual avatar");
    settle();paint();
    const auto*avatarTexture=owner->cardTexture(tools::IdCardTextureKind::avatar);
    check(avatarTexture&&avatarTexture->pixels.width==368&&avatarTexture->pixels.height==368&&avatarTexture->revision==owner->card()->avatarRevision(),"Card avatar is the 136 pt crop at 2x with the source 1.35 oversampling");
    nextImage.reset();owner->perform("profile:avatar",time);pump();settle();check(imageAsks==2&&owner->state()->profile().avatarFilename==avatarName,"A cancelled picker changes nothing");
    // The HUD hiding while the chooser is open closes it; a late result is ignored.
    nextImage=photo;hideDuringPicker=true;owner->perform("profile:avatar",time);pump();settle();hideDuringPicker=false;
    check(imageAsks==3&&cancels==1&&!owner->pickerOpen()&&owner->state()->profile().avatarFilename==avatarName,"Deactivation cancels the open chooser and drops its result");
    owner->setOverlayVisible(true,time);paint();paint(.5);check(owner->page()->active(),"Page is active again after the HUD reopens");
    // 4. Colour picker: live updates, cancel restores, the result commits.
    colorScript=[&](const std::function<void(std::array<double,3>)>&live){live({1,0,0});check(owner->state()->profile().themeColorHex=="FF0000"&&owner->card()->captions()[4].color[0]==1,"Live colour reaches the page and the card");return std::nullopt;};
    owner->perform("profile:themeMenu",time);check(owner->perform("profile:themeCustom",time)&&pump()&&colorAsks==1,"Custom colour asks the host picker");
    check(!owner->state()->profile().themeColorHex&&std::abs(colorStarts.back()[0]-0xFA/255.)<1e-9,"Cancel restores the colour it started from (follow HUD)");
    const auto hoverBefore=owner->card()->hoverRevision();
    colorScript=[](const std::function<void(std::array<double,3>)>&){return std::array<double,3>{0,0,1};};
    owner->perform("profile:themeCustom",time);pump();settle();
    check(owner->state()->profile().themeColorHex=="0000FF"&&owner->card()->captions()[4].color[2]==1&&owner->card()->hoverRevision()>hoverBefore,"Chosen colour commits and recolours the card");
    check(owner->cardTexture(tools::IdCardTextureKind::hover)->revision==owner->card()->hoverRevision(),"Hover plate regenerates for the new accent");
    // 5. Crop events on committed snapshots only.
    owner->perform("profile:backgroundMenu",time);check(owner->setSlider(m::ProfileField::thumbnailZoom,2,time)&&crops==std::vector<std::string>{"thumbnail"},"profileCropChanged {thumbnail}");
    owner->setSlider(m::ProfileField::backgroundOffsetX,40,time);check(crops.size()==1,"Other geometry is not a crop event");
    owner->setSlider(m::ProfileField::backgroundZoom,3,time);check(crops.size()==2&&crops.back()=="background","profileCropChanged {background}");
    // 6. Account sync: lock, game fields, game avatar.
    owner->setSyncLocked(true,time);
    owner->acceptFromGame([](m::PersonalProfile&p){p.gamePlayerID="1234567890123";p.name="Synced";p.permissionLevel=42;},time);
    check(owner->state()->syncLocked()&&!owner->state()->canEdit(m::ProfileField::name)&&owner->state()->profile().name=="Synced","Game fields apply under the sync lock");
    check(owner->card()->captions()[1].text=="UID: 1234567890123"&&owner->card()->captions()[0].text=="Synced"&&owner->card()->captions()[4].text.empty(),"Card shows the synced UID, never a # number");
    check(rejects([&]{owner->acceptFromGame([](m::PersonalProfile&p){p.uid="999";},time);})&&owner->state()->profile().uid=="1000000000","The local UID is immutable");
    const auto gameAvatar=owner->importGameAvatar(read(photo),time);
    check(owner->state()->profile().avatarFilename==gameAvatar&&avatars.back()==gameAvatar&&std::filesystem::exists(t.root/"Profile"/"Images"/gameAvatar),"Game avatar imports synchronously and is reported");
    check(rejects([&]{owner->importGameAvatar(std::vector<std::uint8_t>{1,2,3},time);})&&owner->state()->profile().avatarFilename==gameAvatar,"Invalid game avatar bytes throw and change nothing");
    // 7. Time-zone change, deadline refresh, flush.
    check(owner->state()->value(m::ProfileField::awakeningDate)=="2025/12/31","Awakening day in UTC");
    owner->setTimeZone(u"Asia/Tokyo",time);check(owner->state()->value(m::ProfileField::awakeningDate)=="2026/01/01","Zone change re-renders the date");
    controller.resume(time);const auto due=*owner->nextWakeTime(time);
    check(owner->deadline(due+3600)&&owner->state()->shownHours()!="1.05","Shared-clock deadline refreshes the live hours");
    controller.pause(due+3600);paint();
    check(owner->flush(time),"Orderly flush");settle();
    const auto saved=ehud::data::ProfileStore(t.root).value();
    check(saved.name=="Synced"&&saved.gamePlayerID=="1234567890123"&&saved.themeColorHex=="0000FF"&&saved.thumbnailZoom==2&&saved.avatarFilename==gameAvatar&&saved.accumulatedWorkSeconds>=3780+3600,"profile.json holds the final record");
    check(!std::filesystem::exists(t.root/"Profile"/"Images"/ *avatarName),"The replaced manual avatar file is removed after the commit");
    // 8. Closing the overlay drops pending requests; release frees every resource.
    owner->perform("profile:avatar",time);owner->setOverlayVisible(false,time);pump();check(imageAsks==3,"Hiding the HUD cancels a pending picker");
    composition.detach(renderer);owner->release(renderer);check(renderer.stats().textures==0&&renderer.stats().meshes==0,"Owner releases every GPU resource");
    owner.reset();renderer.reset();
    // 9. A record that cannot be read: source message, defaults, no writes, no card.
    {
        Temporary bad;std::filesystem::create_directories(bad.root/"Profile");{std::ofstream(bad.root/"Profile"/"profile.json")<<"{not json";}
        const auto before=read(bad.root/"Profile"/"profile.json");
        m::WorkModeController idle;tools::ProfileOwnerOptions b=o;b.appRoot=bad.root;b.loaded={};
        tools::ProfileOwner failed(w.hwnd,executor,raster,imageSource,idle,b);failed.start(0);settle();
        check(!failed.loaded()&&failed.loadError()&&failed.state()&&failed.state()->error()==failed.loadError()&&!failed.card(),"Unreadable record shows the source message without a card");
        check(rejects([&]{failed.acceptFromGame([](m::PersonalProfile&){},0);})&&rejects([&]{failed.importGameAvatar(read(photo),0);}),"Account writes report an unavailable store");
        check(failed.flush(0)&&read(bad.root/"Profile"/"profile.json")==before,"The unreadable record is preserved byte for byte");
    }
    // 10. An account update before the record loads is applied once it does.
    {
        Temporary fresh;seed(fresh.root,0);m::WorkModeController idle;tools::ProfileOwnerOptions f=o;f.appRoot=fresh.root;f.loaded={};
        tools::ProfileOwner early(w.hwnd,executor,raster,imageSource,idle,f);early.setSyncLocked(true,0);
        early.acceptFromGame([](m::PersonalProfile&p){p.gamePlayerID="77";p.name="Early";},0);early.start(0);settle();
        check(early.state()->profile().name=="Early"&&early.card()->captions()[1].text=="UID: 77"&&early.flush(0)&&ehud::data::ProfileStore(fresh.root).value().name=="Early","A queued account update lands after the load");
    }
    executor.shutdown();
}
}
int wmain(int argc,wchar_t**argv){
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{check(SUCCEEDED(hr)&&argc==3,"Pass the shared shader and windows/resources/profile");run(argv[1],argv[2]);CoUninitialize();std::cout<<"PASS "<<checks<<" Personal Profile production owner checks\n";return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
