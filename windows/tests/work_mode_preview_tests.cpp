#ifdef _WIN32
#include "tools/work_mode_preview.hpp"
#include "native/module_scene.hpp"
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace {
namespace gpu=endfield::native;namespace c=endfield::core;namespace a=endfield::app;namespace t=endfield::tools;namespace m=endfield::modules;using Json=ehud::data::Json;
unsigned checks{};void check(bool v,const char*message){++checks;if(!v)throw std::runtime_error(message);}
// The injected clipboard (never the system one): one value, counted reads/writes.
struct SyntheticClipboard final:gpu::NotesClipboard{
    std::optional<gpu::NotesClipboardText>value;unsigned reads{},writes{};
    std::optional<gpu::NotesClipboardText>readText(std::uint32_t maximum)override{++reads;if(!value||value->text.size()>maximum)return std::nullopt;return value;}
    std::vector<std::string>readFiles()override{return {};}
    bool writeText(const gpu::NotesClipboardText&v)override{++writes;value=v;return true;}
};
struct Window{HWND window{};Window(){WNDCLASSW w{};w.lpfnWndProc=DefWindowProcW;w.hInstance=GetModuleHandleW(nullptr);w.lpszClassName=L"EndfieldWorkOwnerHidden";check(RegisterClassW(&w)!=0,"Register owned fixture");window=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,w.lpszClassName,L"Synthetic",WS_POPUP,0,0,1280,800,nullptr,nullptr,w.hInstance,nullptr);check(window&&!IsWindowVisible(window),"Synthetic Work Mode stays hidden");}~Window(){if(window)DestroyWindow(window);UnregisterClassW(L"EndfieldWorkOwnerHidden",GetModuleHandleW(nullptr));}};
void run(const std::filesystem::path&shader){Window window;gpu::Renderer renderer;renderer.initialize(window.window,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});const auto camera=gpu::layerViewportProjection(1280,800);renderer.setCamera(camera);gpu::LayerRasterizer raster;gpu::LayerComposition composition;
    std::vector<double>saves;std::vector<bool>focus;unsigned permissions{};t::WorkModePreviewOptions options;options.raster.pixelsPerPoint=1;options.restoredWorkSeconds=100;options.saveWorkSeconds=[&](double total){saves.push_back(total);};options.focusDesired=[&](bool desired){focus.push_back(desired);};options.requestFocusAccess=[&]{++permissions;};
    const auto clipboard=std::make_shared<SyntheticClipboard>();options.clipboard=clipboard;
    t::WorkModePreview owner(window.window,raster,nullptr,TF_CLIENTID_NULL,options);owner.resize({1280,800,96,1,1280,800});owner.focus(true,0);c::ModulePresentation modules{c::Module::workMode};c::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},c::Module::workMode,true};auto center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;
    gpu::LayerScene geometry(raster);geometry.load(Json::Object{{"bounds",Json::Array{0,0,440,440}},{"children",Json::Array{}}},{});gpu::NativeModuleSurface plane(geometry,c::Module::workMode);double now{};bool done{};
    struct Cleanup{gpu::Renderer&r;gpu::LayerComposition&c;t::WorkModePreview&o;bool&done;~Cleanup(){if(done)return;try{c.detach(r);o.release(r);}catch(...){r.reset();}}}cleanup{renderer,composition,owner,done};
    const auto frame=[&](double delta=.01){now+=delta;const auto sample=modules.sample(now).presentation;settings.module=sample.requested;owner.update(center,settings,sample,1,now);owner.upload(renderer);composition.setEntries(renderer,owner.entries());composition.present(renderer);owner.collected(renderer);};
    const auto point=[&](c::Point p){const auto sample=modules.sample(now).presentation;const auto*surface=sample.current.module==c::Module::workMode?&sample.current:&*sample.incoming;plane.update(center,settings,*surface,1);const auto result=c::Projection::viewport(camera*plane.pose().contentWorld,1280,800).project(p);check(result.has_value(),"Actual current source module plane projects");return *result;};
    const auto click=[&](c::Point p){const auto q=point(p);check(owner.covers(q)&&owner.pointer({a::PointerKind::down,a::PointerButton::left,q.x,q.y},now),"Original source control receives projected press");check(!owner.pointerLocked(),"User-requested button press preserves gyro motion");owner.pointer({a::PointerKind::up,a::PointerButton::left,q.x,q.y},now);frame();};
    const auto type=[&](std::string_view value){for(unsigned char ch:value)check(owner.key({a::KeyKind::character,ch},t::WorkModeKeyModifiers{},now),"Short field consumes caller characters");frame();};
    frame();check(owner.entries().size()==8&&!owner.nextWakeTime()&&saves.empty()&&focus.empty(),"Idle owner paints seven source entries (the ring base and the ring share one carrier) plus registration without services/deadlines");renderer.draw(false);const auto image=renderer.readback();check(std::any_of(image.pixels.begin(),image.pixels.end(),[](auto v){return v!=0;}),"Original Work Mode paints hidden shared target");
    click({315,139});check(owner.editing(),"Source custom control creates borrowed-service field");frame();check(owner.entries().size()==11,"Field backing/glyphs/border share one composition");std::shared_ptr<const gpu::PaintedTextLayout>fieldLayout;for(const auto&entry:owner.entries())if(auto found=entry.scene->paintedTextLayout("projected-editor-glyphs"))fieldLayout=std::move(found);check(fieldLayout&&fieldLayout->contentInset().x==3&&fieldLayout->documentWidth()==312&&fieldLayout->documentHeight()==68,"Duration uses original centered inset and finite single-line document");type("invalid");{std::shared_ptr<const gpu::PaintedTextLayout>layout;for(const auto&entry:owner.entries())if(auto found=entry.scene->paintedTextLayout("projected-editor-glyphs"))layout=std::move(found);const auto revision=owner.controller().revision();owner.setLanguage(c::Language::japanese);frame();std::shared_ptr<const gpu::PaintedTextLayout>after;for(const auto&entry:owner.entries())if(auto found=entry.scene->paintedTextLayout("projected-editor-glyphs"))after=std::move(found);check(owner.editing()&&after==layout&&owner.controller().revision()==revision&&owner.state().strings().heading=="作業モード","Language changes keep the same Work Mode draft and painted editor handle");owner.setLanguage(c::Language::simplifiedChinese);frame();}check(owner.key({a::KeyKind::down,VK_RETURN},t::WorkModeKeyModifiers{},now)&&owner.editing()&&owner.state().phaseText()==options.strings.invalidDuration,"Invalid committed text remains editable and selected");type("0:02");check(owner.key({a::KeyKind::down,VK_RETURN},t::WorkModeKeyModifiers{},now)&&!owner.editing()&&owner.controller().snapshot(now).duration==2,"Selected invalid draft replaces and commits source min:sec");frame();
    check(owner.key({a::KeyKind::down,VK_SPACE},t::WorkModeKeyModifiers{},now),"Space starts countdown");frame(.1);check(owner.controller().snapshot(now).phase==m::WorkModePhase::running&&focus==std::vector<bool>{true}&&owner.nextWakeTime(),"Running owner exposes shared deadline and explicit Focus desire");
    check(owner.key({a::KeyKind::down,VK_RETURN},t::WorkModeKeyModifiers{},now),"Return pauses source timer");frame(.5);check(owner.controller().snapshot(now).phase==m::WorkModePhase::paused&&!owner.nextWakeTime()&&focus==std::vector<bool>{true},"Paused Work Mode keeps Focus desire but no deadline");
    check(owner.key({a::KeyKind::down,VK_SPACE},t::WorkModeKeyModifiers{},now),"Space resumes source timer");frame(.1);const auto hiddenStart=now;const auto completion=owner.controller().completionDeadline();owner.setOverlayVisible(false,now);frame(.1);check(!owner.controller().visible()&&!owner.controller().displayDeadline()&&owner.nextWakeTime()==completion&&!owner.requiresFrames(now),"Closed HUD with still-selected Work Mode keeps completion only");owner.setOverlayVisible(true,now);frame(.1);check(owner.controller().visible()&&owner.controller().displayDeadline(),"Reopening uses source visible label deadline");modules.select(c::Module::power,now);frame(.5);frame(.4);check(owner.entries().empty()&&!owner.requiresFrames(now)&&owner.nextWakeTime()&&owner.controller().snapshot(now).phase==m::WorkModePhase::running,"Hidden countdown retains completion only, no UI frames");
    now=hiddenStart+4;owner.wake(now);check(owner.controller().snapshot(now).phase==m::WorkModePhase::completed&&!owner.nextWakeTime()&&focus==std::vector<bool>({true,false}),"Delayed shared wake completes countdown and requests Focus release once");check(!saves.empty()&&std::abs(saves.back()-102)<1e-9,"Absolute source work checkpoint counts exactly two active seconds");
    modules.select(c::Module::workMode,now);frame(.6);frame(.5);click({270,310});frame(.5);click({315,139});type("1:23");check(owner.key({a::KeyKind::down,VK_ESCAPE},t::WorkModeKeyModifiers{},now)&&!owner.editing()&&owner.controller().snapshot(now).duration==2,"Escape discards duration draft without changing controller");frame();
    // UI Automation Value of the open field (HUDWorkDurationField's
    // accessibility value): read the draft, replace it through the editor's
    // document like typing; Return still validates and commits.
    check(!owner.editingText()&&!owner.setEditingText("0:05",now),"Without an open field there is no draft to read or write");
    click({315,139});
    check(owner.editing()&&owner.editingText()==m::workModeDurationEditText(owner.controller().snapshot(now).duration),"The open field's draft is the current duration's edit text");
    {
        // HUDProjectedTextView.projectedCursor: the I-beam over the open field only.
        const auto field=owner.clientRect(m::WorkModePresentation::durationEditorRect());const auto beside=point({40,150});
        check(field&&owner.textCursorAt({field->x+field->width/2,field->y+field->height/2})&&!owner.textCursorAt(beside),"The open field answers the I-beam inside its projected rect only");
    }
    type("4");check(owner.editingText()==std::optional<std::string>("4"),"Typed text replaces the selected draft and is read back");
    {const auto field=owner.clientRect(m::WorkModePresentation::durationEditorRect());check(field.has_value(),"The field projects while open");
     const c::Point center{field->x+field->width/2,field->y+field->height/2};check(owner.finishEditing(false,now)&&!owner.textCursorAt(center),"A closed field shows no I-beam");click({315,139});type("4");}
    check(owner.setEditingText("0:09",now)&&owner.editing()&&owner.editingText()==std::optional<std::string>("0:09")&&owner.controller().snapshot(now).duration==2,"A Value write replaces the whole draft and keeps the field open without committing");
    frame();
    check(!owner.setEditingText("\xff\xfe",now)&&!owner.setEditingText(std::string(70000,'1'),now)&&owner.editingText()==std::optional<std::string>("0:09"),"Invalid UTF-8 and text past the field capacity are refused unchanged");
    check(owner.key({a::KeyKind::down,VK_RETURN},t::WorkModeKeyModifiers{},now)&&!owner.editing()&&owner.controller().snapshot(now).duration==9,"Return commits the written draft like typed min:sec");frame();
    click({315,139});check(owner.setEditingText("2",now)&&owner.key({a::KeyKind::down,VK_RETURN},t::WorkModeKeyModifiers{},now)&&!owner.editing()&&owner.controller().snapshot(now).duration==120,"A written whole number commits as minutes");frame();
    click({315,139});check(owner.setEditingText("0:02",now)&&owner.perform("work:countdown",now)&&!owner.editing()&&owner.controller().snapshot(now).duration==2,"An accessible press commits the written draft first (activateAction)");frame();
    // AppDelegate Edit menu over the field (cut:/copy:/paste:), through the
    // injected clipboard only, with explicitly stated modifiers.
    {
        const t::WorkModeKeyModifiers control{false,true,false},controlShift{true,true,false};
        click({315,139});const auto draft=owner.editingText();check(draft.has_value()&&!draft->empty(),"The field opens with its selected draft");
        check(owner.key({a::KeyKind::down,'C'},control,now)&&clipboard->writes==1&&clipboard->value&&clipboard->value->text==std::u16string(draft->begin(),draft->end())&&owner.editingText()==draft,"Ctrl+C copies the selected draft");
        check(owner.key({a::KeyKind::down,'X'},control,now)&&clipboard->writes==2&&owner.editingText()==std::optional<std::string>(""),"Ctrl+X cuts the selected draft");
        clipboard->value=gpu::NotesClipboardText{u"0:11",std::nullopt,std::nullopt};
        check(owner.key({a::KeyKind::down,'V'},control,now)&&clipboard->reads==1&&owner.editingText()==std::optional<std::string>("0:11"),"Ctrl+V pastes plain text into the field");
        check(!owner.key({a::KeyKind::down,'V'},controlShift,now)&&clipboard->reads==1&&!owner.key({a::KeyKind::character,'v'},control,now)&&owner.editingText()==std::optional<std::string>("0:11"),"Ctrl+Shift+V and control characters are not the source paste");
        check(owner.key({a::KeyKind::down,'C'},control,now)&&clipboard->writes==2,"A collapsed selection copies nothing");
        check(owner.key({a::KeyKind::down,VK_RETURN},t::WorkModeKeyModifiers{},now)&&!owner.editing()&&owner.controller().snapshot(now).duration==11,"The pasted draft commits like typed text");frame();
        check(!owner.key({a::KeyKind::down,'V'},control,now)&&clipboard->reads==1,"Without an open field the paste key is not consumed");
        click({315,139});check(owner.setEditingText("0:02",now)&&owner.key({a::KeyKind::down,VK_RETURN},t::WorkModeKeyModifiers{},now)&&owner.controller().snapshot(now).duration==2,"The duration returns to two seconds");frame();
    }
    click({315,139});type("2:34");modules.select(c::Module::power,now);frame(.5);frame(.5);check(!owner.editing()&&owner.controller().snapshot(now).duration==2,"Leaving Work Mode cancels unsaved custom input");
    modules.select(c::Module::workMode,now);frame(.5);frame(.5);owner.setFocusStatus("Explicit unsupported platform",false,now);frame();check(std::none_of(owner.state().actions().begin(),owner.state().actions().end(),[](const auto&v){return v.id=="work:focusAccess";}),"Unsupported Focus does not invent a permission action");owner.setFocusStatus("Injected permission needed",true,now);frame();click({220,37});check(permissions==1,"Permission control reaches injected callback only");frame(.3);
    click({280,99});frame(.3);owner.key({a::KeyKind::down,VK_SPACE},t::WorkModeKeyModifiers{},now);frame(.4);modules.select(c::Module::power,now);frame(.5);frame(.5);check(!owner.nextWakeTime()&&owner.controller().snapshot(now).phase==m::WorkModePhase::running,"Hidden stopwatch has no periodic wake");const auto beforeSleep=owner.controller().snapshot(now).elapsed;owner.setSuspended(true,now);now+=3600;owner.setSuspended(false,now);check(owner.controller().snapshot(now).elapsed>=beforeSleep+3600&&!owner.nextWakeTime(),"Caller continuous clock includes sleep without a second ticker");
    // Accessibility-equivalent presses (HUDWorkModeInteraction.activateAction)
    // and the projected control rects only exist on the presented page.
    check(!owner.perform("work:pause",now)&&!owner.clientRect({100,296,116,30}),"Accessible presses and projections need the presented page");
    modules.select(c::Module::workMode,now);frame(.5);frame(.5);
    const auto pauseRect=owner.clientRect({100,296,116,30});
    check(pauseRect&&pauseRect->width>0&&pauseRect->height>0&&owner.covers({pauseRect->x+pauseRect->width/2,pauseRect->y+pauseRect->height/2}),"The pause control projects into the client over the page");
    const auto savesBefore=saves.size();
    check(!owner.perform("work:start",now)&&!owner.perform("work:unknown",now)&&owner.controller().snapshot(now).phase==m::WorkModePhase::running,"Only listed source actions perform");
    check(owner.perform("work:pause",now)&&owner.controller().snapshot(now).phase==m::WorkModePhase::paused&&saves.size()==savesBefore+1,"The accessible pause press pauses and checkpoints like its click");
    frame(.5);
    check(owner.perform("work:resume",now)&&owner.controller().snapshot(now).phase==m::WorkModePhase::running,"The accessible resume press resumes the stopwatch");frame();
    // Out-of-order owner times (the live Projection crash class): the host
    // samples its time, a callback delivers a later one, then the earlier
    // sample arrives. The owner keeps the latest time it saw, like the Mac's
    // single monotonic clock: nothing fails, no tracked second is lost or
    // counted twice and the checkpointed lifetime never decreases.
    {
        const double sampled=now,later=now+.25;owner.wake(later);const auto savedBefore=saves.back();
        check(owner.perform("work:pause",sampled)&&owner.controller().snapshot(later).phase==m::WorkModePhase::paused,"An earlier accessible press after a later wake pauses the stopwatch");
        const auto paused=owner.controller().snapshot(later).elapsed;
        check(saves.back()>=savedBefore&&std::abs(saves.back()-owner.controller().trackedWorkSeconds(later))<1e-9,"The pause checkpoints the total at the latest time seen, never a lower one");
        const auto q=point({40,150});owner.pointer({a::PointerKind::move,a::PointerButton::none,q.x,q.y},sampled-.1);
        check(owner.key({a::KeyKind::down,VK_SPACE},t::WorkModeKeyModifiers{},sampled-.05)&&owner.controller().snapshot(later).phase==m::WorkModePhase::running&&std::abs(owner.controller().snapshot(later).elapsed-paused)<1e-9,
              "An earlier Space resumes at the latest time seen: no tracked time is invented");
        const auto sample=modules.sample(sampled).presentation;owner.update(center,settings,sample,1,sampled);owner.upload(renderer);composition.setEntries(renderer,owner.entries());composition.present(renderer);owner.collected(renderer);
        check(!owner.entries().empty()&&owner.requiresFrames(sampled)==owner.requiresFrames(later),"An earlier frame is posed and queried at the latest time seen");
        now=later;
    }
    // HUDModuleContent keeps only the shown screen: closing the HUD while
    // another module is shown releases the Work Mode GPU resources (ring
    // meshes, group targets) and its rasters while the stopwatch keeps
    // running; reopening on another module builds and uploads nothing, and
    // showing it again rebuilds the same artwork from the controller.
    {
        const auto built=owner.resources();
        modules.select(c::Module::power,now);frame(.5);frame(.5);const auto rasters=raster.stats();
        check(built.sceneParts>0&&built.scenePixelBytes>0&&built.gpuResident&&owner.resources().gpuResident&&owner.resources().parks==built.parks&&
              owner.resources().scenePixelBytes==built.scenePixelBytes&&renderer.stats().nativeGroups>=1,
              "A module switch inside the open HUD keeps the Work Mode artwork and GPU resources");
        owner.cancelInteraction(now);owner.setOverlayVisible(false,now);frame(.1);
        const auto parked=owner.resources();
        check(!parked.gpuResident&&parked.parks==built.parks+1&&parked.sceneParts==0&&parked.scenePixelBytes==0&&renderer.stats().nativeGroups==0&&
              raster.stats().entries<rasters.entries&&raster.stats().resourceBytes+built.scenePixelBytes<=rasters.resourceBytes&&
              owner.controller().snapshot(now).phase==m::WorkModePhase::running,
              "Closing the HUD on another module releases the Work Mode GPU resources and rasters; the stopwatch keeps running");
        const auto hidden=raster.stats();
        owner.setOverlayVisible(true,now);frame(.1);frame(.5);
        check(!owner.resources().gpuResident&&owner.resources().sceneParts==0&&owner.resources().parks==parked.parks&&raster.stats().rasterizations==hidden.rasterizations,
              "Reopening on another module builds and uploads nothing for Work Mode");
        modules.select(c::Module::workMode,now);frame(.5);frame(.5);
        check(owner.resources().gpuResident&&renderer.stats().nativeGroups>=1&&!owner.entries().empty()&&owner.resources().sceneParts==built.sceneParts&&
              owner.resources().scenePixelBytes==built.scenePixelBytes&&owner.resources().builds>built.builds&&owner.controller().snapshot(now).phase==m::WorkModePhase::running,
              "Showing Work Mode again rebuilds the same parts and raster bytes");
        renderer.draw(false);const auto shown=renderer.readback();
        check(std::any_of(shown.pixels.begin(),shown.pixels.end(),[](auto v){return v!=0;}),"The rebuilt Work Mode paints");
    }
    check(owner.message({window.window,options.textMessage,123456,0},now)&&!owner.editing(),"Stale private field notice cannot reopen/edit a retired document");composition.detach(renderer);owner.shutdown(now);owner.release(renderer);done=true;check(renderer.stats().textures==0&&renderer.stats().meshes==0&&renderer.stats().nativeGroups==0&&!IsWindowVisible(window.window),"Detached owner retires all resources and never activates TSF, Focus or real UI");
}
}
int wmain(int argc,wchar_t**argv){const auto result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(result)&&argc==2,"Pass HLSL to hidden Work Mode owner test");run(argv[1]);CoUninitialize();std::cout<<"Work Mode owner: "<<checks<<" checks passed\n";return 0;}catch(const std::exception&e){if(SUCCEEDED(result))CoUninitialize();std::cerr<<"Work Mode owner after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
