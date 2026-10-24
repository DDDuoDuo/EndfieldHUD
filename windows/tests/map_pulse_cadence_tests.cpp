// Pin pulse cadence. With pins on the Map and ambient animation on, the pin
// pulse used to report itself as finite motion, so the host presented the
// whole HUD at the display rate (165 Hz on the user's laptop) for as long as
// the Map showed. On the Mac the pulse is a repeating Core Animation
// compositor animation (WorldMapCanvas "map.pulse": 1.7 s, ease-out, scale
// .65 -> 2.7, opacity .05 -> .65 -> 0); a 30 Hz update shows the same curve.
// The pulse is now ambient motion: the host's 30 Hz ambient cadence (on
// whenever the pulse can run) presents it, and each frame samples it from the
// shared clock. Interactive Map motion - camera moves, the design buttons'
// press and hover motion, marker style transitions - stays finite motion at
// the display rate; drags and pin additions/removals present per event. The
// owner is the redesigned Map (tools/map_owner) on the shared design Context.
//
//   map_pulse_cadence_tests <native/hud.hlsl> [--trace-seconds S] [--hardware|--warp]
//
// Part one is a scheduling contract on a synthetic clock: the Map owner's
// demand fed through the host's own FrameWakePlan and DisplayPacing at a
// 165 Hz display. Part two is a hidden trace on the real clock: the
// production OverlayHost scheduling (offscreen, never shown, paced at
// 165 Hz) presents a 12-pin Map and measures the process CPU per window -
// the old display-rate pulse, the ambient pulse, the low-power fallback and
// display-rate interaction. The ctest run presents through the Map owner
// only (update per frame, no renderer: cheap on a shared machine); with
// --hardware every frame is also uploaded, composed and drawn offscreen on
// the GPU at 1280 x 800, as the idle trace does (--warp: the software
// rasterizer, whose CPU numbers are not comparable). No window is shown and
// no input is injected; the Map data is synthetic.
#ifdef _WIN32
#include "tools/map_owner.hpp"
#include "tools/hud_design_overlay_owner.hpp"
#include "app/overlay_host.hpp"
#include "modules/map_geography.hpp"
#include "modules/map_presentation.hpp"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

namespace {
namespace c=endfield::core;namespace m=endfield::modules;namespace n=endfield::native;namespace a=endfield::app;namespace t=endfield::tools;
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}
constexpr double ambientTick=1./30,refreshPeriod=1./165,pulseFallback=1./30+1./120;
std::string uuid(unsigned n){char text[40];std::snprintf(text,sizeof text,"00000000-0000-0000-0000-%012u",n);return text;}
std::shared_ptr<const n::MapPaintImage>image(unsigned size){auto p=std::make_shared<n::MapPaintImage>();p->width=p->height=size;p->straightRGBA.assign(std::size_t(size)*size*4,255);return p;}
std::shared_ptr<const m::MapGeography>geography(){auto p=std::make_shared<m::MapGeography>();p->terrain=m::MapTerrain{};return p;}
n::MapPaintResult paint(const n::MapPaintRequest&q,const n::MapPaintCancelled&cancel){if(cancel())return {};const auto g=*m::mapRasterGeometry(q.viewport,q.contentsScale);auto pixels=std::make_shared<n::MapPaintImage>();pixels->width=pixels->height=g.pixelDimension;pixels->straightRGBA.assign(std::size_t(g.pixelDimension)*g.pixelDimension*4,127);n::MapPaintResult result{pixels,{},m::MapRasterFrame{g.viewport,g.screenRect,g.pixelsPerPoint},.001};if(q.backdrop){auto b=std::make_shared<n::MapPaintImage>();b->width=1024;b->height=512;b->straightRGBA.assign(1024*512*4,96);result.backdrop=b;}return result;}
// Twelve pins around the default camera (the Mac pulses at most twelve).
m::MapSnapshot pinned(unsigned count){m::MapSnapshot s;const m::MapViewport v;
    for(unsigned k=0;k<count;++k)s.pins.push_back({uuid(k+1),v.centerX+(double(k%4)-1.5)*.04,v.centerY+(double(k/4)-1)*.08,double(k+1),m::MapPinStyle::yellow});return s;}
struct Owner {
    n::LayerRasterizer raster;a::UtilityExecutor utility{[]{}};double now{};unsigned ids{100};
    std::unique_ptr<t::DesignOverlayOwner>overlay;std::unique_ptr<t::MapOwner>map;c::ModulePresentation modules{c::Module::map};
    c::source::DesktopChromeSettings settings{{0,0,1280,800},1,{},c::Module::map,true};c::Matrix4 center;
    explicit Owner(unsigned pins,bool ambient=true,bool reduced=false){
        t::DesignOverlayOptions overlayOptions;overlayOptions.raster.pixelsPerPoint=1;overlay=std::make_unique<t::DesignOverlayOwner>(raster,overlayOptions);
        t::MapOwnerOptions o;o.initial=pinned(pins);o.persistence={[](const m::MapSnapshot&){},[this]{return uuid(++ids);},[]{return 1.;}};
        o.geography=geography();o.clock=[this]{return now;};o.paint=paint;o.player={image(2),image(2),image(2)};o.owner.name="map";o.owner.raster.pixelsPerPoint=1;
        map=std::make_unique<t::MapOwner>(raster,utility,overlay->context(),std::move(o));
        const a::ClientMetrics metrics{1280,800,96,1,1280,800};a::ModuleAppearance appearance;appearance.ambient=ambient;appearance.reduceMotion=reduced;
        for(a::ModuleOwner*owner:{static_cast<a::ModuleOwner*>(overlay.get()),static_cast<a::ModuleOwner*>(map.get())}){owner->resize(metrics);owner->setAppearance(appearance,0);owner->setOverlayVisible(true,0);}
        modules.setDesignScreen(c::Module::map,true);center=c::source::DesktopChromeLayout::make(settings,{},{}).designToScreen;}
    ~Owner(){map.reset();overlay.reset();utility.waitIdle();utility.drain();utility.shutdown();}
    void frame(double t){now=std::max(now,t);utility.drain();map->utilityCompleted(now);const auto sample=modules.sample(now).presentation;const a::ModuleFrame f{center,settings,sample,1,now,true};overlay->update(f);map->update(f);}
    // Box px of a map control -> its logical client centre.
    std::pair<double,double>controlCentre(std::string_view id){std::vector<endfield::modules::design::AccessibilityItem>items;map->canvas().accessibilityItems(items);
        for(const auto&item:items)if(item.id.ends_with(id)){const auto r=map->core().clientRect(item.rect);check(r.has_value(),"A Map control projects");return {r->x+r->width/2,r->y+r->height/2};}
        check(false,"A listed Map control");return {};}
    // Shown, terrain painted and every raster deadline delivered (30 Hz frames).
    double settle(double t){frame(t);for(unsigned k=0;k<90;++k){utility.waitIdle();t+=ambientTick;frame(t);if(const auto due=map->nextWakeTime(now);due&&*due<=t)(void)map->deadline(t);}
        check(map->state().active()&&map->rasterStats().published>0&&!map->rasterStats().painting&&!map->rasterStats().waiting,"The Map is shown, accepts input and its terrain is painted");return t;}
};
// Application::Impl::demand for a visible HUD on the source shell with
// ambient animation on, and the cadence OverlayHost derives from it.
c::FrameDemand demand(bool finite,bool lowPower=false){c::FrameDemand d;d.phase=c::VisibilityPhase::visible;d.presented=d.onScreen=d.canAdvanceTransition=true;d.finiteAnimation=finite;d.ambientEnabled=true;d.lowPower=lowPower;return d;}
struct Cadence {double interval{};bool paced{};};
std::optional<Cadence>cadence(const c::FrameDemand&d){a::FrameWakePlan plan;a::DisplayPacing pacing;pacing.setRefreshPeriod(refreshPeriod);
    (void)plan.refresh(d,true);for(unsigned k=0;k<4;++k)check(!plan.interval()||plan.wake().present,"Every planned wake presents");
    const auto interval=pacing.interval(plan);if(!interval)return std::nullopt;return Cadence{*interval,a::DisplayPacing::paced(plan)};}
bool isAt(std::optional<double>a,double b){return a&&std::abs(*a-b)<1e-9;}

void contracts(){
    // The curve: every frame samples mapPulse at its own time, so 30 Hz
    // samples lie on the Mac's curve; the steps between them stay small.
    {double scale{},alpha{};for(unsigned phase=0;phase<10;++phase)for(unsigned k=0;k<51;++k){const double a0=(phase*.1+k)*ambientTick,a1=a0+ambientTick;if(std::floor(a0/1.7)!=std::floor(a1/1.7))continue;
        const auto p=m::mapPulse(a0),q=m::mapPulse(a1);scale=std::max(scale,std::abs(q.scale-p.scale));alpha=std::max(alpha,std::abs(q.opacity-p.opacity));}
     std::printf("MAP_PULSE_CURVE 30 Hz samples of the Mac curve: largest step between frames %.3f of scale (%.2f pt of the 8 pt ring radius), %.3f of opacity\n",scale,scale*8,alpha);}
    Owner o(12);double t=o.settle(1);
    check(o.map->sceneStats().visiblePins==12&&o.map->sceneStats().pulsingPins==12&&o.map->pulsing(),"Twelve pins are shown and pulse (the Mac's bound)");
    check(!o.map->requiresFrames(t),"The pulse alone asks for no finite (display-rate) frames");
    check(isAt(o.map->nextWakeTime(o.now),t+pulseFallback),"Its only wake is the fallback, an ambient tick and a quarter after the last frame");
    const auto ambient=cadence(demand(o.map->requiresFrames(t))),legacy=cadence(demand(o.map->requiresFrames(t)||o.map->pulsing()));
    check(ambient&&!ambient->paced&&std::abs(ambient->interval-ambientTick)<1e-12,"With pins shown the host presents at the source's 30 Hz ambient cadence, not display-paced");
    check(legacy&&legacy->paced&&std::abs(legacy->interval-refreshPeriod)<1e-12,"Before, the pulse counted as finite motion and was paced at the 165 Hz display");
    // One pulse period at the ambient cadence: each frame samples the pulse
    // and the fallback wake never precedes the next ambient frame.
    {const auto updates=o.map->sceneStats().localUpdates;const double start=t;double margin=INFINITY;
     for(unsigned k=1;k<=51;++k){const double next=start+k*ambientTick;const auto wake=o.map->nextWakeTime(o.now);margin=std::min(margin,wake?*wake-next:-1.);o.frame(next);}
     t=start+51*ambientTick;
     check(o.map->sceneStats().localUpdates==updates+51,"Every ambient frame samples the pulse (51 samples over its 1.7 s period)");
     check(margin>=1./120-1e-9,"Between ambient frames no pulse wake is due: the fallback comes 1/120 s after the next ambient tick");}
    // Without frames (low power: no ambient cadence) the pulse asks for one
    // frame per tick and a quarter, and only after a frame presented it.
    check(!cadence(demand(false,true)),"Low-power visual mode has no ambient cadence");
    {const double last=t;check(isAt(o.map->nextWakeTime(o.now),last+pulseFallback),"The pulse wake follows the last frame");
     check(!o.map->deadline(last+ambientTick),"It is not due at the next ambient tick");
     check(o.map->deadline(last+pulseFallback),"A tick and a quarter without a frame: the pulse reports changed artwork (one frame)");
     check(!o.map->nextWakeTime(o.now)&&!o.map->deadline(last+.5),"It asks once: a frame the host does not present (an occluded HUD) leaves no wake loop");
     t=last+.5;o.frame(t);check(isAt(o.map->nextWakeTime(o.now),t+pulseFallback),"The next presented frame re-arms it");}
    // Interaction keeps the display rate.
    const auto paced=[&](double at){const auto value=cadence(demand(o.map->requiresFrames(at)));return value&&value->paced&&std::abs(value->interval-refreshPeriod)<1e-12;};
    check(o.map->perform("map.zoomIn",t)&&o.map->requiresFrames(t)&&paced(t),"A zoom's camera motion is finite and display-paced");
    o.frame(t+.1);check(paced(t+.1),"...throughout its .18 s");t+=.2;o.frame(t);check(!o.map->requiresFrames(t)&&o.map->pulsing()&&!paced(t),"After it settles only the ambient pulse remains");
    t=o.settle(t);
    {const auto [x,y]=o.controlCentre("map.zoomOut");
     check(o.map->pointer({a::PointerKind::down,a::PointerButton::left,x,y},t)&&o.map->requiresFrames(t)&&paced(t),"A button press (its press motion, then the camera) is display-paced");
     o.map->pointer({a::PointerKind::up,a::PointerButton::left,x,y},t+.01);o.frame(t+.3);t+=.3;check(!o.map->requiresFrames(t),"The press settles");}
    t=o.settle(t);
    {const std::string id=o.map->state().pins()[0].id;check(o.map->perform("map.pin:"+id,t)&&!o.map->requiresFrames(t),"Selecting a pin starts no motion");
     check(o.map->perform("map.pin:"+id,t)&&o.map->requiresFrames(t)&&paced(t),"Cycling the selected pin runs its .16 s turn-in display-paced");
     t+=.2;o.frame(t);check(!o.map->requiresFrames(t),"The style transition ends");}
    // A drag presents per pointer event (the host refreshes on each handled
    // move); pin additions and removals change in one frame, as the Mac's
    // withoutActions render does.
    {const auto r=o.map->core().clientRect({180,200,0,0});check(r.has_value(),"An empty Map point projects");const double x=r->x,y=r->y;const auto before=o.map->state().camera();
     check(o.map->pointer({a::PointerKind::down,a::PointerButton::left,x,y},t),"A press on the Map starts a drag");
     check(o.map->pointer({a::PointerKind::move,a::PointerButton::none,x+40,y+20},t+.01)&&!(o.map->state().camera()==before),"Each drag move is handled and moves the camera at once (the host presents it)");
     o.map->pointer({a::PointerKind::up,a::PointerButton::left,x+40,y+20},t+.02);t+=.03;o.frame(t);
     // The pointer left the zoom column: its hover fades out (finite, display-paced) before the next check.
     t+=.5;o.frame(t);}
    {const auto pins=o.map->state().pins().size();const auto r=o.map->core().clientRect({420,380,0,0});check(r.has_value(),"A terrain point projects");
     check(o.map->pointer({a::PointerKind::down,a::PointerButton::right,r->x,r->y},t)&&o.map->state().pins().size()==pins+1,"A right press adds a pin");o.frame(t);
     check(!o.map->requiresFrames(t)&&o.map->pulsing(),"Adding a pin needs no finite frames; the pulses continue");}
    // Reduce motion or ambient animation off: no pulse, no wake.
    {Owner still(12,true,true);double s=still.settle(1);check(!still.map->pulsing()&&!still.map->nextWakeTime(still.now)&&!still.map->requiresFrames(s),"Reduce motion: no pulse and no wake");}
    {Owner off(12,false);double s=off.settle(1);check(!off.map->pulsing()&&!off.map->nextWakeTime(off.now)&&!off.map->requiresFrames(s),"Ambient animation off: no pulse and no wake");}
}

struct Window{HWND hwnd{};Window(){WNDCLASSW w{};w.lpfnWndProc=DefWindowProcW;w.hInstance=GetModuleHandleW(nullptr);w.lpszClassName=L"OwnedMapPulseFixture";check(RegisterClassW(&w)!=0,"Register the hidden Map pulse fixture");
    hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,w.lpszClassName,L"Synthetic Map pulse",WS_POPUP,0,0,1280,800,nullptr,nullptr,w.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"The fixture never shows or activates a window");}
    ~Window(){if(hwnd)DestroyWindow(hwnd);UnregisterClassW(L"OwnedMapPulseFixture",GetModuleHandleW(nullptr));}};
double processSeconds(){FILETIME created{},exited{},kernel{},user{};check(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=0,"Process times");
    const auto seconds=[](FILETIME f){return (double(f.dwHighDateTime)*4294967296.+double(f.dwLowDateTime))*1e-7;};return seconds(kernel)+seconds(user);}

// The hidden trace: Application::Impl's frame/deadline/refresh flow for one
// owner, through the production OverlayHost scheduling.
enum class Render {none,warp,hardware};
void trace(const wchar_t*shader,double seconds,Render render){
    Owner o(12);Window window;n::Renderer renderer;const bool draws=render!=Render::none;
    if(draws)renderer.initialize(window.hwnd,1280,800,{render==Render::hardware?n::Driver::hardware:n::Driver::warpForTests,shader,n::RenderTarget::offscreenForTests});
    n::LayerComposition composition;bool legacy{},lowPower{},interact{},zoomIn{true};double nextZoom{};std::uint64_t frames{};
    a::OverlayHost host;a::OverlayCallbacks callbacks;
    const auto demandAt=[&](double at){return demand(o.map->requiresFrames(at)||(legacy&&o.map->pulsing()),lowPower);};
    callbacks.frame=[&](double at){at=std::max(at,o.now);
        if(interact&&at>=nextZoom){(void)o.map->perform(zoomIn?"map.zoomIn":"map.zoomOut",at);zoomIn=!zoomIn;nextZoom=at+.4;}
        o.frame(at);if(draws){o.overlay->upload(renderer);o.map->upload(renderer);composition.setEntries(renderer,o.map->entries(c::Module::map));composition.present(renderer);renderer.setCamera(n::layerViewportProjection(1280,800));renderer.draw(false);}++frames;
        host.setFrameDemand(demandAt(o.now));host.setDeadline(o.map->nextWakeTime(o.now));};
    callbacks.deadline=[&](double at){at=std::max(at,o.now);o.now=at;o.utility.drain();o.map->utilityCompleted(at);
        if(o.map->deadline(at)){host.setFrameDemand(demandAt(at));host.invalidate();}host.setDeadline(o.map->nextWakeTime(o.now));};
    a::OverlayOptions options{L"Hidden Map pulse cadence",0,0,1280,800,nullptr};options.offscreenScheduling=true;options.refreshRate=165;
    host.create(options,callbacks);host.show(false);
    const auto pump=[&](double span){const double end=a::OverlayHost::clockNow()+span;for(;;){const double left=end-a::OverlayHost::clockNow();if(left<=0)return;check(host.pumpOnce(static_cast<std::uint32_t>(std::ceil(left*1000))),"The traced host stopped");}};
    // Shown and settled before the first window (terrain painted, raster idle).
    o.now=a::OverlayHost::clockNow();host.setFrameDemand(demandAt(o.now));host.invalidate();pump(1.5);
    check(o.map->state().active()&&o.map->pulsing()&&o.map->rasterStats().published>0,"The traced Map shows twelve pulsing pins");
    struct Result {double fps{},paced{},timer{},deadline{},cpu{},perFrame{};};
    const auto window_=[&](const char*name,bool old,bool low,bool moving){legacy=old;lowPower=low;interact=moving;nextZoom=a::OverlayHost::clockNow()+.1;
        host.setFrameDemand(demandAt(o.now));host.setDeadline(o.map->nextWakeTime(o.now));host.invalidate();pump(.5);
        const auto before=host.stats();const auto counted=frames;const double cpu0=processSeconds(),start=a::OverlayHost::clockNow();
        pump(seconds);const double elapsed=a::OverlayHost::clockNow()-start,cpu=processSeconds()-cpu0;const auto after=host.stats();const auto count=double(frames-counted);
        Result r{count/elapsed,double(after.pacedFrames-before.pacedFrames)/elapsed,double(after.timerWakes-before.timerWakes)/elapsed,double(after.externalWakes-before.externalWakes)/elapsed,cpu/elapsed,count?cpu*1000/count:0};
        std::printf("MAP_PULSE_TRACE %-22s %.1f s, %s: %6.1f frames/s (%6.1f display-paced), %5.1f timer wakes/s (%4.1f deadline), CPU %5.1f%% of one core = %6.1f ms/s, %.2f ms/frame\n",
            name,elapsed,render==Render::hardware?"GPU":render==Render::warp?"WARP":"owner only",r.fps,r.paced,r.timer,r.deadline,r.cpu*100,r.cpu*1000,r.perFrame);
        return r;};
    const auto old=window_("pins, display-rate (old)",true,false,false);
    const auto now=window_("pins, ambient 30 Hz",false,false,false);
    const auto low=window_("pins, low power",false,true,false);
    const auto moving=window_("zoom every 0.4 s",false,false,true);
    std::printf("MAP_PULSE_TRACE CPU with 12 pins shown: %.1f -> %.1f ms per second (%.0f%% less), frames %.0f -> %.0f per second\n",old.cpu*1000,now.cpu*1000,old.cpu>0?(1-now.cpu/old.cpu)*100:0.,old.fps,now.fps);
    check(old.paced>old.fps*.5,"The old demand presented the pulse display-paced");
    // A pulse wake fires only when an ambient frame comes over 1/120 s later than the previous one did (a loaded machine).
    check(now.paced==0&&now.fps<=36&&now.fps>=10&&now.deadline<=now.fps*.25,"With pins shown the pulse is presented at the 30 Hz ambient cadence: no display-paced frame, (almost) no pulse wake");
    check(low.paced==0&&low.fps<=30&&low.fps>=8&&low.deadline>=low.fps*.8,"Without the ambient cadence the pulse's own wakes present it, at most 24 Hz");
    check(moving.paced>0,"Camera motion is still display-paced");
    host.destroy();if(draws){composition.detach(renderer);o.map->release(renderer);o.overlay->release(renderer);check(renderer.stats().textures==0,"Every traced Map resource is released");renderer.reset();}
}
}
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{check(SUCCEEDED(hr)&&argc>=2,"Pass the original HUD shader");double seconds=1;Render render{Render::none};
        for(int k=2;k<argc;++k){const std::wstring arg=argv[k];if(arg==L"--trace-seconds"&&k+1<argc){seconds=std::stod(argv[++k]);check(seconds>=.5&&seconds<=60,"Trace windows are .5...60 s");}else if(arg==L"--hardware")render=Render::hardware;else if(arg==L"--warp")render=Render::warp;else check(false,"Unknown argument");}
        contracts();trace(argv[1],seconds,render);
        CoUninitialize();std::cout<<"PASS "<<checks<<" Map pulse cadence checks\n";return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
