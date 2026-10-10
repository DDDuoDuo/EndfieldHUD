#include "tools/profile_preview.hpp"
#include "native/module_scene.hpp"
#include "native/profile_text.hpp"
#include <cmath>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
namespace {
using namespace endfield;namespace m=modules;namespace gpu=native;using J=ehud::data::Json;using M=core::Matrix4;
unsigned checks{};
void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
struct Window{HWND hwnd{};Window(){hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,L"STATIC",L"Owned hidden Personal Profile owner fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Profile owner fixture remains hidden");}~Window(){if(hwnd)DestroyWindow(hwnd);}};
void run(const std::filesystem::path&shader,const std::filesystem::path&resources){
    Window w;gpu::Renderer renderer;renderer.initialize(w.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(1280,800));
    gpu::LayerRasterizer raster;
    m::PersonalProfile seed;seed.uid="1000000000";seed.awakeningDate=721692800;seed.accumulatedWorkSeconds=3600;
    double live=3600;unsigned commits{};m::ProfilePersistence io;io.commit=[&](const m::PersonalProfile&p){++commits;return p;};io.workSeconds=[&]{return live;};
    m::ProfileState state(seed,gpu::nativeProfileTextRules(),gpu::nativeProfileDateRules(u"UTC"),io);
    std::vector<m::ProfileImageKind>chosen;std::vector<std::array<double,3>>colors;
    gpu::LayerImageSource imageSource;
    tools::ProfilePreviewOptions options;options.raster.pixelsPerPoint=1;options.appearance.scale=1;options.images=&imageSource;options.frameSprite=m::loadProfileSourceArtwork(resources).frame;
    options.chooseImage=[&](m::ProfileImageKind k){chosen.push_back(k);};options.chooseColor=[&](std::array<double,3>c){colors.push_back(c);};
    tools::ProfilePreview preview(w.hwnd,state,raster,options);preview.resize({1280,800,96,1,1280,800});preview.focus(true,0);
    core::source::DesktopChromeSettings settings;settings.viewport={0,0,1280,800};settings.module=core::Module::profile;core::ModulePresentation modules(core::Module::profile);
    gpu::LayerScene geometry(raster);geometry.load(J::Object{{"bounds",J::Array{0,0,400,334}},{"children",J::Array{}}},options.raster);gpu::NativeModuleSurface surface(geometry,core::Module::profile);core::Projection plane;
    gpu::LayerComposition composition;double time{};std::vector<gpu::LayerCompositionEntry>published;
    const auto paint=[&](double advance=1./60){
        time+=advance;const auto sample=modules.sample(time).presentation;settings.module=sample.requested;preview.update({},settings,sample,1,time);
        if(sample.current.module==core::Module::profile){surface.update({},settings,sample.current,1);plane=core::Projection::viewport(gpu::layerViewportProjection(1280,800)*surface.pose().contentWorld,1280,800);}
        preview.upload(renderer);const auto entries=preview.entries();composition.setEntries(renderer,entries);composition.present(renderer);renderer.draw(false);preview.collected(renderer);
    };
    const auto screen=[&](double x,double y){const auto p=plane.project({x,y});check(p.has_value(),"Projectable profile point");return *p;};
    const auto click=[&](double x,double y){const auto p=screen(x,y);preview.pointer({app::PointerKind::move,app::PointerButton::none,p.x,p.y,0},time);
        const bool used=preview.pointer({app::PointerKind::down,app::PointerButton::left,p.x,p.y,0},time);preview.pointer({app::PointerKind::up,app::PointerButton::left,p.x,p.y,0},time);paint();return used;};
    const auto key=[&](std::uint32_t vk){return preview.key({app::KeyKind::down,vk},time);};
    const auto type=[&](std::u16string_view text){for(const auto c:text)preview.key({app::KeyKind::character,c},time);};
    paint();paint(.5);
    check(!preview.entries().empty()&&preview.artwork().fields.surfaces.size()>40,"Profile page publishes its retained card");
    check(preview.nextWakeTime().has_value(),"Active page schedules the 30 s work refresh on the shared clock");
    // Identity popover, then the name editor.
    check(click(30,125)&&state.popover()==m::ProfilePopover::identity&&preview.capturesPointer(),"Menu opens the identity popover and captures input");
    check(click(60,153)&&preview.editing()&&preview.editingField()==m::ProfileField::name&&!state.popover(),"Edit name opens the projected field");
    type(u"Zed\U0001F600");check(key(VK_RETURN)&&!preview.editing()&&state.profile().name=="Zed\U0001F600","Return commits the Character-bounded name");
    // A rejected tag keeps the field open; Escape cancels it.
    click(30,125);click(60,179);check(preview.editingField()==m::ProfileField::tag,"Edit # opens the tag field");
    type(u"a b");check(key(VK_RETURN)&&preview.editing()&&state.error().has_value()&&state.profile().tag=="0000","Invalid tag stays in a red field with the source message");
    check(key(VK_ESCAPE)&&!preview.editing()&&state.profile().tag=="0000","Escape cancels the edit");
    // Introduction editor and the 150-Character limit.
    check(click(300,200)&&preview.editingField()==m::ProfileField::introduction,"Introduction editor");
    type(std::u16string(160,u'x'));check(key(VK_RETURN)&&state.profile().introduction==std::string(150,'x'),"Biography keeps 150 Characters");
    // Card theme popover, slider drag and keyboard nudge.
    check(click(300,305)&&state.popover()==m::ProfilePopover::background,"Card theme popover");
    const auto start=screen(142,92),end=screen(260,92);
    preview.pointer({app::PointerKind::down,app::PointerButton::left,start.x,start.y,0},time);check(state.dragging()&&preview.pointerLocked(),"Slider drag holds the plane");
    preview.pointer({app::PointerKind::move,app::PointerButton::left,end.x,end.y,0},time);paint();const auto commitsBefore=commits;
    preview.pointer({app::PointerKind::up,app::PointerButton::left,end.x,end.y,0},time);paint();
    check(!state.dragging()&&commits==commitsBefore+1&&std::abs(state.profile().backgroundWidth-654)<1,"Drag previews in memory and saves once on release");
    check(key(VK_RIGHT)&&state.profile().backgroundWidth==state.sliders().front().value,"Arrow keys nudge the selected slider");
    check(click(150,48)&&chosen.size()==1&&chosen.back()==m::ProfileImageKind::background,"Choose background asks the owner's picker");
    check(click(342,20)&&state.popover()==m::ProfilePopover::themeColor,"Card colour popover");
    check(click(350,215)&&colors.size()==1,"Custom colour asks the owner's picker with the current accent");
    check(key(VK_ESCAPE)&&!state.popover(),"Escape unwinds the popover");
    // Clicking outside an open popover dismisses it and consumes the click.
    click(300,305);const auto outside=screen(-200,10);
    check(preview.pointer({app::PointerKind::down,app::PointerButton::left,outside.x,outside.y,0},time)&&!state.popover(),"Outside click dismisses the popover");
    preview.pointer({app::PointerKind::up,app::PointerButton::left,outside.x,outside.y,0},time);paint();
    // An account lock closes a field that became read-only, without saving.
    click(30,125);click(60,153);type(u"Locked");state.setSyncLocked(true);preview.stateChanged(time);
    check(!preview.editing()&&state.profile().name=="Zed\U0001F600","Sync lock cancels a now read-only edit");state.setSyncLocked(false);preview.stateChanged(time);
    // Work hours refresh at the scheduled deadline only.
    live=7200;const auto due=*preview.nextWakeTime();preview.wake(due-.01);check(state.shownHours()=="1.00","No early work refresh");
    preview.wake(due);check(state.shownHours()=="2.00","Work hours refresh at the deadline");paint();
    // Page imagery: tinted frame, portrait crop following the slider preview, backdrop photo.
    const auto portraitNode=[&]{for(const auto&n:preview.artwork().fields.layers["children"].array())if(n["id"].string().ends_with(".portrait"))return n;return J{};};
    check(!portraitNode()["children"].array()[1]["contents"].isNull(),"Accent-tinted source frame is published for the portrait");
    auto decoded=std::make_shared<gpu::ProfileDecodedImage>();decoded->image={300,200,std::vector<std::uint8_t>(300*200*4,255),false};decoded->orientation=6;decoded->sourceWidth=300;decoded->sourceHeight=200;
    preview.setAvatar(decoded,time);paint();const auto photo=portraitNode()["children"].array()[0];
    check(!photo["contents"].isNull()&&photo["contents"]["memoryImage"].isString()&&photo["children"].array().empty(),"Avatar crop replaces the silhouette");
    const auto firstRevision=photo["contents"]["revision"].integer();
    click(30,125);click(60,231);check(state.popover()==m::ProfilePopover::portrait&&chosen.size()==1,"Adjust portrait popover");
    const auto zoomStart=screen(200,171),zoomEnd=screen(300,171);const double savedZoom=state.profile().avatarZoom;const auto commitsBeforeZoom=commits;
    preview.pointer({app::PointerKind::down,app::PointerButton::left,zoomStart.x,zoomStart.y,0},time);
    preview.pointer({app::PointerKind::move,app::PointerButton::left,zoomEnd.x,zoomEnd.y,0},time);paint();
    check(state.dragging()&&state.profile().avatarZoom==savedZoom&&commits==commitsBeforeZoom&&portraitNode()["children"].array()[0]["contents"]["revision"].integer()>firstRevision,
        "Portrait crop follows the unsaved zoom drag preview");
    preview.pointer({app::PointerKind::up,app::PointerButton::left,zoomEnd.x,zoomEnd.y,0},time);paint();
    check(commits==commitsBeforeZoom+1&&state.profile().avatarZoom>savedZoom,"Zoom drag saves once on release");
    key(VK_ESCAPE);
    auto backdrop=std::make_shared<gpu::ProfileDecodedImage>();backdrop->image={64,36,std::vector<std::uint8_t>(64*36*4,200),false};
    auto withBackground=state.profile();withBackground.backgroundFilename="00000000-0000-4000-8000-000000000002.png";state.refresh(withBackground,false);preview.stateChanged(time);
    preview.setBackground(backdrop,time);paint(.3);check(preview.artwork().backdrop.photo&&preview.artwork().contrast.has_value(),"Background photo switches the card to its backdrop palette");
    preview.setAvatar(nullptr,time);paint();check(portraitNode()["children"].array()[0]["contents"].isNull(),"Removing the avatar restores the silhouette");
    // Leaving the module fades the background section and releases input.
    modules.select(core::Module::notes,time);paint(.05);check(!state.active()&&preview.requiresFrames(time),"Leaving profile deactivates input and fades the section");
    for(int n=0;n<40;++n)paint(.02);check(!preview.requiresFrames(time),"Section fade settles");
    composition.detach(renderer);preview.release(renderer);check(renderer.stats().textures==0&&renderer.stats().meshes==0&&renderer.stats().nativeGroups==0,"Personal Profile owner releases every resource");renderer.reset();
}
}
int wmain(int argc,wchar_t**argv){
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{check(SUCCEEDED(hr)&&argc==3,"Pass the shared shader and windows/resources/profile");run(argv[1],argv[2]);CoUninitialize();std::cout<<"PASS "<<checks<<" Personal Profile owner checks\n";return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
