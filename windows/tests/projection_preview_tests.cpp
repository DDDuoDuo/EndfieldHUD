#ifdef _WIN32
#include "tools/projection_preview.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#include "modules/projection_settings.hpp"
#include "tests/notes_host_capture.hpp"
#include "core/data/data_store.hpp"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>
namespace {
namespace c=endfield::core;namespace a=endfield::app;namespace m=endfield::modules;namespace n=endfield::native;namespace t=endfield::tools;namespace d=ehud::data;using J=d::Json;
unsigned checks{};void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
struct Window{HWND h{};Window(){WNDCLASSW cl{};cl.lpfnWndProc=DefWindowProcW;cl.hInstance=GetModuleHandleW(nullptr);cl.lpszClassName=L"OwnedProjectionOwnerFixture";check(RegisterClassW(&cl)!=0,"Register isolated Projection owner");h=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,cl.lpszClassName,L"Hidden synthetic Projection",WS_POPUP,0,0,1280,800,nullptr,nullptr,cl.hInstance,nullptr);check(h&&!IsWindowVisible(h),"Projection fixture never shows or activates a window");}~Window(){if(h)DestroyWindow(h);UnregisterClassW(L"OwnedProjectionOwnerFixture",GetModuleHandleW(nullptr));}};
void run(n::Renderer&renderer,const std::filesystem::path&resources){n::LayerRasterizer raster;
    // The HUD's design environment the chrome takes (palette, measurer, grapheme rules).
    t::DesignOverlayOptions designOptions;designOptions.raster.pixelsPerPoint=2;t::DesignOverlayOwner overlay(raster,designOptions);
    m::ProjectionModel model({.98,.83,.12,1},.5,.5);m::ProjectionInteraction interaction(model,{1280,800});t::ProjectionPreviewOptions options;options.raster.assetRoot=resources/"notes-format";options.raster.pixelsPerPoint=2;options.raster.paddingPoints=1;options.design=&overlay.context().env();
    std::vector<m::NotesShelfChoice>shelf;for(unsigned k=0;k<8;++k)shelf.push_back({"shelf"+std::to_string(k),"Synthetic "+std::to_string(k),"PNG",k!=3,true});
    options.shelfChoices=[&]{return shelf;};std::vector<t::ProjectionPreviewEvent>events;options.event=[&](const auto&e){events.push_back(e);};t::ProjectionPreview owner(model,interaction,raster,std::move(options));owner.resize({1280,800,96,1,1280,800});n::LayerComposition composition;double now{};bool released{};struct Cleanup{n::Renderer&r;n::LayerComposition&c;t::ProjectionPreview&o;bool&released;~Cleanup(){if(released)return;try{c.detach(r);o.release(r);}catch(...){r.reset();}}}cleanup{renderer,composition,owner,released};
    auto frame=[&](double dt=.02){now+=dt;owner.update(now);owner.upload(renderer);composition.setEntries(renderer,owner.entries());composition.present(renderer);owner.collected(renderer);renderer.setCamera(n::layerViewportProjection(1280,800));};
    auto down=[&](double x,double y){check(owner.pointer({a::PointerKind::down,a::PointerButton::left,x,y,0},now),"Source pointer down is consumed");};auto up=[&](double x,double y){check(owner.pointer({a::PointerKind::up,a::PointerButton::left,x,y,0},now),"Source pointer up is consumed");};auto click=[&](double x,double y){down(x,y);up(x,y);frame(.25);};auto escape=[&]{check(owner.key({a::KeyKind::down,27},now),"Source Escape consumed by owner");frame(.25);};
    std::vector<n::NotesAutomationElement>list;const auto read=[&]{list.clear();owner.automationElements(list);};const auto find=[&](std::string_view id)->const n::NotesAutomationElement*{read();for(const auto&e:list)if(e.id==id)return &e;return nullptr;};
    const auto centre=[&](std::string_view id){const auto*e=find(id);check(e!=nullptr,"The chrome lists the control");return c::Point{(e->rect.x+e->rect.width/2),(e->rect.y+e->rect.height/2)};};
    const auto press=[&](std::string_view id){const auto p=centre(id);click(p.x,p.y);};
    owner.setActive(true,now);frame();check(owner.active()&&!owner.menuOpen()&&!owner.requiresFrames(now+1),"Source blank Projection settles with no background timer");frame(1);const auto before=raster.stats();for(unsigned k=0;k<30;++k)frame();check(raster.stats().rasterizations==before.rasterizations,"Steady workspace does no CPU raster work");
    // ProjectionToolbar (Mac 1.3.0): the content-sized redesign panel, centred under the 64 pt inset.
    read();std::size_t toolbar{};for(const auto&e:list)if(e.id.starts_with("projection.toolbar:"))++toolbar;
    check(toolbar==15&&find("projection.toolbar:close")->label=="Return"&&find("projection.toolbar:brush")->label=="Brush thickness"&&find("projection.toolbar:color:22A6F2")->label=="Pen color #22A6F2","The fifteen toolbar controls carry their source accessibility titles");
    {double left=1e9,right=-1e9,top=1e9;for(const auto&e:list)if(e.id.starts_with("projection.toolbar:")){left=std::min(left,e.rect.x);right=std::max(right,e.rect.x+e.rect.width);top=std::min(top,e.rect.y);}
     check(right-left>600&&std::abs((left+right)/2-640)<20&&top>=64,"The toolbar is about 740 wide, centred and below the 64 pt inset");}
    down(100,400);check(owner.pointerLocked(),"Canvas drawing owns capture");check(owner.pointer({a::PointerKind::move,a::PointerButton::none,200,450,0},now),"Source live segment accepts event");frame();up(200,450);frame();check(model.drawing().strokes().size()==1&&!owner.pointerLocked()&&events.back().kind==t::ProjectionPreviewEvent::Kind::strokeCompleted,"Mouse-up commits one stroke and one source event");down(300,400);owner.cancelInteraction(now);frame();check(model.drawing().strokes().size()==1&&!interaction.liveStroke(),"Capture loss discards only uncommitted live ink");
    // Swatches set the pen and return from the eraser; pen / eraser; the custom swatch.
    press("projection.toolbar:eraser");check(model.erasing()&&events.back().kind==t::ProjectionPreviewEvent::Kind::brushChanged,"The eraser toggles erasing and logs brushChanged");
    press("projection.toolbar:color:22A6F2");check(!model.erasing()&&m::ProjectionPenSettings::hex(model.color())==0x22A6F2&&events.back().kind==t::ProjectionPreviewEvent::Kind::brushChanged,"A swatch sets the pen colour and returns to the pen at once");
    press("projection.toolbar:eraser");press("projection.toolbar:pen");check(!model.erasing(),"The pen tool sets the pen (it does not toggle)");
    // The brush pop-over: one slider applied live, one brushChanged when it closes.
    press("projection.toolbar:brush");check(owner.menuOpen()&&owner.popupID()==std::optional<std::string>("brush"),"The brush pop-over opens");
    {const auto*slider=find("projection.menu:brush");check(slider&&slider->role==n::NotesAutomationRole::slider&&slider->minimum==1&&slider->maximum==80,"Its slider spans 1...80");
     const auto count=events.size();check(owner.setAutomationValue("projection.menu:brush",30,now)&&model.brushWidth()==30&&events.size()==count,"The slider applies live and defers its event");
     escape();check(!owner.menuOpen()&&events.size()==count+1&&events.back().kind==t::ProjectionPreviewEvent::Kind::brushChanged,"Esc closes the pop-over and logs one brushChanged");}
    // A second press on the control that opened the popup closes it.
    press("projection.toolbar:appearance");check(owner.popupID()==std::optional<std::string>("appearance"),"The appearance pop-over opens");
    check(owner.setAutomationValue("projection.menu:darkness",100,now)&&model.darkness()==1&&owner.setAutomationValue("projection.menu:blur",0,now)&&model.blur()==0,"Darkness and blur apply live in percent");
    owner.performAction("appearance",now);frame(.25);check(!owner.menuOpen()&&events.back().kind==t::ProjectionPreviewEvent::Kind::backgroundChanged,"A second press closes it and logs backgroundChanged");
    // The custom colour: the shared pop-over (wheel + brightness), picks apply on release.
    press("projection.toolbar:color:custom");check(owner.popupID()==std::optional<std::string>("color")&&find("projection.menu:penColor.wheel")&&find("projection.menu:penColor.brightness"),"The custom swatch opens the shared colour pop-over");
    {const auto color=model.color();check(owner.perform("projection.menu:penColor.wheel",now)&&model.color()!=color,"Activating the wheel picks another colour live");const auto count=events.size();
     press("projection.toolbar:color:custom");check(!owner.menuOpen()&&events.size()==count+1&&events.back().kind==t::ProjectionPreviewEvent::Kind::brushChanged,"Closing it logs one brushChanged");}
    // Background: the plane fades .18 s.
    press("projection.toolbar:background");check(!model.backgroundEnabled()&&owner.backgroundOpacity()==0&&events.back().kind==t::ProjectionPreviewEvent::Kind::backgroundChanged,"Source background disabled after finite .18-second fade");press("projection.toolbar:background");check(model.backgroundEnabled()&&owner.backgroundOpacity()==1,"Source background restored without full-window raster replacement");
    // Right-click toggles the eraser only while no popup is open; the wheel changes the brush only off the chrome.
    check(owner.pointer({a::PointerKind::down,a::PointerButton::right,640,500,0},now)&&model.erasing(),"A right click toggles the eraser");owner.pointer({a::PointerKind::down,a::PointerButton::right,640,500,0},now);check(!model.erasing(),"A second right click restores the pen");
    press("projection.toolbar:brush");check(owner.pointer({a::PointerKind::down,a::PointerButton::right,640,500,0},now)&&!model.erasing(),"Right-click is ignored while a popup is open (DOCS-628)");escape();
    {const auto width=model.brushWidth();const auto bar=centre("projection.toolbar:pen");check(owner.wheel({bar.x,bar.y,1,false,0,3},now)&&model.brushWidth()==width,"The wheel does nothing over the toolbar (DOCS-629)");
     check(owner.wheel({640,500,1,false,0,3},now)&&model.brushWidth()==width+1,"Over the canvas it changes the brush");}
    // Insert media: Choose in File Explorer queues the picker; the shelf is a direct-pick menu.
    press("projection.toolbar:media");check(owner.popupID()==std::optional<std::string>("media")&&find("projection.menu:media:finder")&&find("projection.menu:media:finder")->label=="Choose in File Explorer","The media menu offers the File Explorer chooser and the shelf");
    press("projection.menu:media:finder");auto action=owner.takeAction();check(!owner.menuOpen()&&action&&action->kind==t::ProjectionPreviewAction::Kind::chooseLocal,"The File Explorer row closes the menu and queues the native picker");
    check(owner.beginImport(action->generation,1,{},now),"Injected validated import begins");check(owner.message()=="Loading media…","The status line shows the import");const auto raw=d::makeWindowsMediaReference("C:\\owned\\not-opened.png","Synthetic.png",640,480,"image",{},1);auto lease=std::make_shared<int>(7);auto ref=m::ProjectionMediaReference::fromWindowsReference(raw,lease);const std::array refs{ref};check(owner.receiveImport(action->generation,refs,now)&&model.media().size()==1&&events.back().kind==t::ProjectionPreviewEvent::Kind::mediaAdded&&owner.message().empty(),"Generation-bound import retains metadata/lease and source event");frame();
    press("projection.toolbar:media");press("projection.menu:media:shelf");check(owner.popupID()==std::optional<std::string>("shelf"),"Choose from Shelf opens the shelf menu");
    {const auto*unsupported=find("projection.menu:shelf:shelf3");check(unsupported&&!unsupported->enabled&&find("projection.menu:shelf:shelf1")->enabled,"Unsupported shelf entries are disabled");press("projection.menu:shelf:shelf1");action=owner.takeAction();check(!owner.menuOpen()&&action&&action->kind==t::ProjectionPreviewAction::Kind::useShelf&&action->shelfID=="shelf1","One click imports that shelf entry");check(owner.receiveImportError(action->generation,{},now),"The synthetic shelf import ends");}
    shelf.clear();press("projection.toolbar:media");press("projection.menu:media:shelf");check(!owner.menuOpen()&&owner.message()=="The shelf has no images or videos.","An empty shelf shows the message instead of a menu");frame();
    const auto id=model.media()[0].id;const auto oldFrame=model.media()[0].frame;down(oldFrame.x+40,oldFrame.y+10);owner.pointer({a::PointerKind::move,a::PointerButton::none,oldFrame.x+80,oldFrame.y+30,0},now);up(oldFrame.x+80,oldFrame.y+30);frame();check(model.find(id)->frame.x==oldFrame.x+40,"Media header moves actual model geometry");
    press("projection.toolbar:media");press("projection.menu:media:finder");action=owner.takeAction();check(action&&action->kind==t::ProjectionPreviewAction::Kind::chooseLocal&&owner.beginImport(action->generation,1,{},now),"Second synthetic import starts");owner.setActive(false,now);check(!owner.receiveImport(action->generation,refs,now)&&model.media().size()==1,"Hide invalidates import and retains existing media");owner.setActive(true,now);frame();check(model.drawing().strokes().size()==1&&model.media().size()==1,"Projection session survives return/reopen");
    // UI Automation: the toolbar, the media buttons, the open popup.
    {const auto*eraser=find("projection.toolbar:eraser");check(eraser->rect.width>=29&&eraser->rect.height>=29&&eraser->rect.x>0&&eraser->rect.y>0,"Toolbar peers are physical client rectangles of their controls");
     const auto mediaID="projection.media:"+std::to_string(model.media().front().id);check(find(mediaID+":remove")&&find(mediaID+":remove")->label=="Remove media"&&!find(mediaID+":play"),"A still image offers only its remove button");
     check(owner.perform("projection.toolbar:eraser",now)&&model.erasing(),"Invoking the eraser toggles erasing");check(owner.perform("projection.toolbar:eraser",now)&&!model.erasing(),"Invoking it again restores the brush");
     check(!owner.perform("projection.menu:popover.close",now)&&!owner.perform("projection.toolbar:unknown",now)&&!owner.perform("projection.media:999:remove",now)&&!owner.setAutomationValue("projection.menu:brush",10,now),"Unavailable ids are refused");}
    // The clear confirmation pop-over (Cancel / Clear all, not danger).
    owner.setPreferences(c::Language::korean,{.2,.7,1,1},false,now);frame();check(model.drawing().strokes().size()==1&&model.media().size()==1,"Language/accent event preserves session data");
    press("projection.toolbar:clear");check(owner.popupID()==std::optional<std::string>("clear")&&find("projection.menu:clear:cancel")&&find("projection.menu:clear:confirm"),"Clear all asks in its pop-over");press("projection.menu:clear:cancel");check(!owner.menuOpen()&&model.drawing().strokes().size()==1,"Cancel keeps the drawing");
    press("projection.toolbar:clear");press("projection.menu:clear:confirm");check(model.drawing().strokes().empty()&&model.media().empty()&&events.back().kind==t::ProjectionPreviewEvent::Kind::cleared,"Confirmed clear removes owned drawing/media only");check(*lease==7&&ref->encoded()==raw,"Clear never mutates external reference or lease");frame();escape();action=owner.takeAction();check(action&&action->kind==t::ProjectionPreviewAction::Kind::returnToHUD,"Canvas Escape delegates HUD handoff");
    // Live order (an RC died opening Projection with "Projection controls require a finite
    // monotonic owner clock"): the host samples its time, the owner receives later times
    // inside that callback (a pointer handler, a UI Automation invoke reading the clock),
    // then the host's calls with the earlier sample arrive. Each is taken at the latest
    // time seen, as the Mac's single CACurrentMediaTime never decreases.
    {frame(.3);const double sample=now;const auto bar=centre("projection.toolbar:brush");
     check(owner.pointer({a::PointerKind::move,a::PointerButton::none,bar.x,bar.y,0},sample+.5),"A toolbar hover inside the callback is taken at its later time");
     check(owner.perform("projection.toolbar:media",sample+.6)&&owner.menuOpen(),"A UI Automation invoke inside the callback opens the media menu at its later time");
     check(owner.requiresFrames(sample),"Frame demand with the earlier host sample is the opening menu's demand at the latest time");
     owner.update(sample);owner.upload(renderer);composition.setEntries(renderer,owner.entries());composition.present(renderer);owner.collected(renderer);
     check(owner.pointer({a::PointerKind::move,a::PointerButton::none,bar.x,bar.y,0},sample)&&owner.wheel({bar.x,bar.y,1,false,0,3},sample)&&owner.key({a::KeyKind::down,VK_DOWN},sample),"Input with the earlier host sample is handled at the latest time");
     owner.cancelInteraction(sample);owner.setPreferences(c::Language::korean,{.2,.7,1,1},false,sample);check(owner.menuOpen(),"The menu opened inside the callback stays open");
     check(owner.requiresFrames(sample+.7)&&!owner.requiresFrames(sample+.9),"The menu opening runs its .18 s wipe from the later time, not from the earlier sample");
     now=sample+.9;frame(0);check(owner.key({a::KeyKind::down,27},sample)&&!owner.menuOpen(),"An Esc with the earlier host sample closes the menu");frame(.3);}
    // The host's Projection route (app/application_events.cpp: capturePointer(
    // pointerLocked()) after every event) over real OverlayHost messages. With
    // the capture taken only when the window does not already hold it, a drag
    // draws. Live RC 128 drew nothing: every drag event took the capture again,
    // and Windows reported that as a capture loss that cancelled the stroke.
    {
        namespace host=endfield::tests;
        check(host::HostCaptureHarness::selfRecaptureNotifies(),"Windows answers SetCapture on the window holding the capture with WM_CAPTURECHANGED naming that window");
        host::HostCaptureHarness capture(host::HostCaptureRoute::pointerLocked,host::HostCaptureCall::whenNotHeld,[&](const a::PointerEvent&e){return owner.pointer(e,now);},[&]{return owner.pointerLocked();});
        // A canvas band clear of the toolbar and every media item, so each press draws.
        double top=-1;for(double y=140;y+160<790&&top<0;y+=10){bool clear=true;for(const auto&item:model.media()){const auto&f=item.frame;clear=clear&&!(f.x<100+260&&f.x+f.width>100&&f.y<y+160&&f.y+f.height>y);}if(clear)top=y;}
        check(top>=0,"The canvas has a band clear of the media for the drag");
        const auto strokes=model.drawing().strokes().size();const auto losses=capture.captureLosses();
        now+=.01;check(capture.send(WM_LBUTTONDOWN,MK_LBUTTON,120,top)&&capture.holdsCapture()&&owner.pointerLocked(),"A Projection drawing press takes the host capture");
        for(unsigned k=1;k<=4;++k){now+=.01;check(capture.send(WM_MOUSEMOVE,MK_LBUTTON,120+40.*k,top+10.*k),"Every drag event reaches the canvas");}
        check(capture.holdsCapture()&&capture.captureLosses()==losses&&interaction.liveStroke()&&interaction.liveStroke()->points.size()==5,"The drag keeps the one capture and every live point");
        now+=.01;capture.send(WM_LBUTTONUP,0,280,top+40);frame();
        check(model.drawing().strokes().size()==strokes+1&&model.drawing().strokes().back().points.size()==5&&!capture.holdsCapture()&&!owner.pointerLocked(),"Releasing commits the whole stroke and ends the capture");
        // A real loss (the capture taken away) still discards only the live ink.
        const auto released=capture.captureLosses();now+=.01;check(capture.send(WM_LBUTTONDOWN,MK_LBUTTON,120,top+100)&&capture.holdsCapture(),"A second press takes the capture again");now+=.01;capture.send(WM_MOUSEMOVE,MK_LBUTTON,200,top+120);
        ReleaseCapture();check(capture.captureLosses()==released+1&&!interaction.liveStroke()&&!owner.pointerLocked()&&model.drawing().strokes().size()==strokes+1,"A real capture loss discards only the live ink");
        now+=.01;capture.send(WM_LBUTTONUP,0,200,top+120);frame();check(model.drawing().strokes().size()==strokes+1&&!capture.holdsCapture(),"The release after a capture loss commits nothing");
    }
    composition.detach(renderer);owner.release(renderer);released=true;check(renderer.stats().textures==0&&renderer.stats().meshes==0,"Projection owner releases every local resource after detach");
}
}
// Source OverlayController.recordProjectionEvent names, one per event kind.
void actionNames(){using K=t::ProjectionPreviewEvent::Kind;const std::array<std::pair<K,std::string_view>,7> expected{{{K::strokeCompleted,"drawingEdited"},{K::erased,"erased"},{K::brushChanged,"brushChanged"},{K::backgroundChanged,"backgroundChanged"},{K::cleared,"cleared"},{K::mediaAdded,"mediaAdded"},{K::mediaRemoved,"mediaRemoved"}}};
    for(const auto&[kind,name]:expected)check(t::projectionEventAction({kind,{}})==name,"Projection events record the source projectionAction names");}
int wmain(int argc,wchar_t**argv){auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==3,"Pass HLSL and original resources root");actionNames();Window window;n::Renderer renderer;renderer.initialize(window.h,1280,800,{n::Driver::warpForTests,argv[1],n::RenderTarget::offscreenForTests});run(renderer,std::filesystem::absolute(argv[2]));renderer.reset();CoUninitialize();std::cout<<"PASS "<<checks<<" Projection owner checks\n";return 0;}catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
