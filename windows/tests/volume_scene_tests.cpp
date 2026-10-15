#include "native/volume_scene.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include "native/layer_scene.hpp"
#include "native/layer_image_source.hpp"
#include <objbase.h>
#endif
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
namespace gpu=endfield::native;namespace core=endfield::core;using Json=ehud::data::Json;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}template<class F>void rejects(F&&f,const char*s){bool no{};try{f();}catch(const std::exception&){no=true;}check(no,s);}
gpu::VolumeSnapshot fixture(){gpu::VolumeSnapshot s;s.outputID="o0";s.inputID="i0";s.volume=.55;s.balance=0;s.muted=false;s.canSetVolume=s.canSetMute=s.canSetBalance=s.canSetDefaultOutput=s.canSetDefaultInput=s.applicationActivitySupported=true;
    for(unsigned n=0;n<8;++n){s.outputs.push_back({"o"+std::to_string(n),"Output "+std::to_string(n),bool(n%2),bool(n==3)});s.inputs.push_back({"i"+std::to_string(n),"Input "+std::to_string(n)});}for(unsigned n=0;n<12;++n)s.applications.push_back({std::to_string(n),"App "+std::to_string(n),100+n,true,gpu::VolumeAppState::direct,{},{}});return s;}
const gpu::VolumeControl&action(const gpu::VolumeController&c,std::string_view id){for(const auto&a:c.actions())if(a.id==id)return a;throw std::runtime_error("Missing action");}
const gpu::VolumeSlider&slider(const gpu::VolumeController&c,std::string_view id){for(const auto&a:c.sliders())if(a.id==id)return a;throw std::runtime_error("Missing slider");}
const Json&leaf(const gpu::VolumeScenePlan&p,std::string_view id){for(const auto&n:p.layers["children"].array())if(n["id"].string()==id)return n;throw std::runtime_error("Missing artwork");}
void portable(){
    auto s=fixture();std::vector<std::string>writes;gpu::VolumeController*owner{};gpu::VolumeCallbacks calls;
    calls.setActive=[&](bool active){writes.push_back(active?"on":"off");};
    calls.setVolume=[&](auto endpoint,double value){writes.push_back("volume:"+std::string(endpoint));auto updated=owner->snapshot();updated.volume=value;owner->receiveSnapshot(std::move(updated));return true;};
    calls.setBalance=[&](auto endpoint,double value){writes.push_back("balance:"+std::string(endpoint));auto updated=owner->snapshot();updated.balance=value;owner->receiveSnapshot(std::move(updated));return true;};
    calls.setMute=[&](auto endpoint,bool value){writes.push_back("mute:"+std::string(endpoint));auto updated=owner->snapshot();updated.muted=value;owner->receiveSnapshot(std::move(updated));return true;};
    calls.setDefaultOutput=[&](auto endpoint){writes.push_back("output:"+std::string(endpoint));auto updated=owner->snapshot();updated.outputID=endpoint;owner->receiveSnapshot(std::move(updated));return true;};
    calls.setDefaultInput=[&](auto endpoint){writes.push_back("input:"+std::string(endpoint));return true;};
    calls.stopAllApps=[&]{writes.push_back("stopAll");};calls.setAppGain=[&](auto id,double){writes.push_back("gain:"+std::string(id));return true;};calls.stopApp=[&](auto id){writes.push_back("stop:"+std::string(id));return true;};
    gpu::VolumeController c(s,std::move(calls));owner=&c;
    auto withIcon=c.snapshot();withIcon.applications[0].icon=gpu::VolumeApplicationIcon{"volume.synthetic.icon",1};c.receiveSnapshot(withIcon);auto iconPlan=gpu::prepareVolumeScene(c);
    check(leaf(iconPlan,"volume/app-row/0/icon")["bounds"]==Json::Array{0,0,20,20}&&leaf(iconPlan,"volume/app-row/0/icon")["position"]==Json::Array{18,238}&&leaf(iconPlan,"volume/app-row/0/icon")["contentsGravity"]==Json{"resizeAspect"},"Source app icon remains untinted20point aspect-fit artwork at original position");
    check(leaf(iconPlan,"volume/app-row/0/icon")["backgroundColor"].isNull()&&leaf(iconPlan,"volume/app-row/0/name")["position"]==Json::Array{44,235}&&leaf(iconPlan,"volume/app-row/0/name")["bounds"]==Json::Array{0,0,103,15},"Ready icon uses original source44point title origin without tint/background");
    withIcon.applications[0].icon.reset();c.receiveSnapshot(withIcon);auto noIconPlan=gpu::prepareVolumeScene(c);check(leaf(noIconPlan,"volume/app-row/0/name")["position"]==Json::Array{18,235}&&leaf(noIconPlan,"volume/app-row/0/name")["bounds"]==Json::Array{0,0,129,15},"Missing icon restores original text-only origin and width");
    auto badIcon=c.snapshot();badIcon.applications[0].icon=gpu::VolumeApplicationIcon{"",1};rejects([&]{c.receiveSnapshot(badIcon);},"Malformed icon descriptor rejects before snapshot replacement");
    check(!c.active()&&!c.mouseDown({100,120},0),"Inactive owner never writes audio");check(c.setActive(true)&&!c.setActive(true)&&writes.size()==1,"Activation is explicit and idempotent");
    check(action(c,"audio:output").rect==core::Rect{82,40,306,28}&&action(c,"audio:input").rect==core::Rect{82,75,306,28},"Source endpoint chooser geometry retained");
    check(slider(c,"volume").rect==core::Rect{82,116,235,25}&&slider(c,"balance").rect==core::Rect{102,158,244,25},"Original slider geometry and ranges retained");
    check(c.mouseDown({88,128},1)&&c.dragging()&&c.snapshot().volume==0,"Source six-point slider inset maps beginning to zero");c.mouseDragged({311,128});check(c.snapshot().volume==1,"Dragging clamps to original track endpoint");
    auto changed=c.snapshot();changed.outputID="o1";c.receiveSnapshot(changed);auto before=writes.size();check(!c.dragging()&&!c.mouseDragged({180,128})&&writes.size()==before,"Device change cancels gesture rather than retargeting a new endpoint");
    c.mouseDown({199.5,128},2);c.mouseUp();check(std::abs(*c.snapshot().volume-.5)<1e-12&&c.key(39,false,2)&&std::abs(*c.snapshot().volume-.52)<1e-12,"Right arrow applies source two-percent nudge to selected slider");
    check(!c.key(39,true,2),"Modified keys remain available to owner");check(c.perform("audio:mute",3)&&c.snapshot().muted==true,"Mute reflects event-returned state");
    check(c.perform("audio:output",4)&&c.choosing()&&!c.choosingInput()&&c.pageCount()==2&&c.actions().size()==8,"Output chooser has exactly six rows plus back and next");
    check(action(c,"audio:output:o1").label.ends_with(", Selected"),"Selected endpoint exposes source accessible state");
    check(!c.key(39,false,4),"Chooser does not redirect keyboard arrows to hidden volume slider");
    const auto sample=c.actionSample(4);check(sample.active&&sample.reveal==core::Rect{10,0,390,334}&&sample.transform.values[12]==11&&sample.transform.values[14]==-10,"Source action transform/reveal endpoints retained");
    check(c.scroll({20,70},39,4.1)&&c.pageIndex()==0&&c.scroll({20,70},1,4.2)&&c.pageIndex()==1,"Device wheel changes page only after forty-point threshold");
    check(c.scroll({20,70},-40,4.4)&&c.pageIndex()==0&&c.actionSample(4.4).transform.values[12]==-11,"Reverse wheel restores previous page with source reverse motion");
    check(c.perform("audio:output:o2",5)&&!c.choosing()&&writes[writes.size()-2]=="stopAll"&&writes.back()=="output:o2","Switching output stops routes before injected device transaction");
    check(c.perform("audio:input",6)&&c.choosingInput(),"Input chooser remains distinct from output");auto disconnected=c.snapshot();disconnected.inputs.clear();c.receiveSnapshot(std::move(disconnected));
    auto artwork=gpu::prepareVolumeScene(c);check(leaf(artwork,"volume/title")["text"]["string"].string()=="// Input device","Disconnected input chooser retains its title and empty-device artwork");
    check(c.key(27,false,7)&&!c.choosing()&&!c.key(27,false,7),"Escape only consumes a live chooser");
    check(c.scroll({20,260},10000,8)&&c.applicationScroll()==12*31-95.&&c.applicationScroll()==c.maximumApplicationScroll()&&c.selectedSlider()=="volume","Application list has smooth clamped point-based scrolling over its three-row viewport");
    artwork=gpu::prepareVolumeScene(c);check(leaf(artwork,"volume/app-track")["bounds"].array()[2].number()==2,"Source app scrollbar retains two-point width");
    check(c.scroll({20,260},-10000,9)&&c.applicationScroll()==0,"Application list returns to top without page switching");
    before=writes.size();check(c.setSlider("app:0",1)&&writes.size()==before,"Untouched direct route at unity never starts or recreates a service");
    changed=c.snapshot();changed.applications[0].state=gpu::VolumeAppState::active;changed.applications[0].gain=.5;changed.applications[1].state=gpu::VolumeAppState::failed;changed.applications[1].gain=.2;changed.applications[2].state=gpu::VolumeAppState::stopping;changed.applications[2].gain=.2;c.receiveSnapshot(changed);
    check(c.setSlider("app:0",1)&&writes.back()=="gain:0","Existing route at unity remains active");check(c.setSlider("app:1",1)&&writes.back()=="stop:1","Failed route unity retries cleanup explicitly");
    c.scroll({20,260},30,10);check(!c.setSlider("app:2",.4),"Stopping routes cannot accept new gain writes");
    check(c.perform("audio:headphones",11,true)&&c.headphones()&&c.applicationScroll()==30&&c.pageCount()==2&&!c.actionSample(11).active,"Detail toggle keeps source scroll state while reduced motion settles action");
    c.setActive(false);check(c.headphones()&&!c.choosing()&&!c.dragging()&&c.applicationScroll()==0&&c.pageIndex()==0&&writes.back()=="off","Leaving module resets transient input but preserves detail preference");
    auto invalid=c.snapshot();invalid.volume=std::numeric_limits<double>::quiet_NaN();const auto revision=c.contentRevision();rejects([&]{c.receiveSnapshot(invalid);},"Nonfinite provider snapshot rejected");check(c.contentRevision()==revision,"Rejected snapshot leaves prior retained model unchanged");
    rejects([&]{c.actionSample(std::numeric_limits<double>::infinity());},"Nonfinite frame clocks rejected");
    gpu::AudioSnapshot audio;audio.available=true;audio.paused=false;audio.devices={{L"opaque:{id}",L"Speakers 音箱",true}};audio.default_device_id=L"opaque:{id}";audio.volume=.3f;audio.muted=false;
    const auto mapped=gpu::volumeSnapshotFromSystemAudio(audio);check(mapped.outputs[0].name=="Speakers 音箱"&&mapped.outputID=="opaque:{id}"&&mapped.canSetVolume&&mapped.canSetMute&&!mapped.canSetDefaultOutput&&!mapped.canSetBalance&&!mapped.applicationActivitySupported,"Existing audio provider maps only its real supported capabilities");
    audio.application_supported=true;audio.applications_paused=false;audio.applications.push_back({"42:100",L"Synthetic 音频",42,true,gpu::AudioApplicationRouteState::active,.5f,0});const auto appMapped=gpu::volumeSnapshotFromSystemAudio(audio);check(appMapped.applicationActivitySupported&&appMapped.applications[0].name=="Synthetic 音频"&&appMapped.applications[0].id=="42:100"&&appMapped.applications[0].gain==.5&&appMapped.applications[0].state==gpu::VolumeAppState::active,"Confirmed owned native routes retain relative gain, opaque process identity and Unicode label");
    audio.applications_paused=true;check(!gpu::volumeSnapshotFromSystemAudio(audio).applicationActivitySupported,"Paused application backend cannot expose a stale writable route");
    audio.paused=true;check(!gpu::volumeSnapshotFromSystemAudio(audio).canSetVolume,"Paused provider cannot leave an enabled stale volume writer");
    gpu::VolumeController minimal({});artwork=gpu::prepareVolumeScene(minimal);check(artwork.surfaces.size()==artwork.layers["children"].array().size(),"Unknown devices/values retain bounded explicit unavailable artwork");
    std::set<std::string>ids;for(const auto&item:artwork.surfaces)check(ids.insert(item.id).second,"Source surface identities are unique");
}
gpu::VolumeSnapshot apps(unsigned count){auto s=fixture();s.applications.resize(count);return s;}
std::size_t fullyVisibleRows(const gpu::VolumeController&c){std::size_t rows{};const auto view=c.applicationViewport();for(const auto&s:c.sliders())if(s.id.starts_with("app:")&&s.visibleRect&&s.visibleRect->height==s.rect.height&&s.rect.y>=view.y)++rows;return rows;}
std::vector<double>appSliderTops(const gpu::VolumeController&c){std::vector<double>tops;for(const auto&s:c.sliders())if(s.id.starts_with("app:"))tops.push_back(s.rect.y);return tops;}
// User change: the app list shows at most three rows and scrolls beyond that
// (wheel, content drag or scrollbar drag) within the source clamp limits.
void listLayout(){
    unsigned writes{};gpu::VolumeCallbacks calls;calls.setAppGain=[&](auto,double){++writes;return true;};calls.stopApp=[&](auto){++writes;return true;};calls.setVolume=[&](auto,double){++writes;return true;};
    gpu::VolumeController three(apps(3),calls);three.setActive(true);
    check(three.applicationViewport()==core::Rect{12,234,376,95}&&three.layout().applicationRows==3,"The app list viewport holds three source rows (31 points each plus the source two-point sliver)");
    check(fullyVisibleRows(three)==3&&three.maximumApplicationScroll()==0,"Three apps are all shown and the list does not scroll");
    check(three.scroll({20,260},40,1)&&three.applicationScroll()==0&&three.mouseDown({60,250},1)&&!three.panning()&&!three.dragging(),"A list that fits ignores wheel travel and never starts a list drag");three.mouseUp();
    auto plan=gpu::prepareVolumeScene(three);bool track{};for(const auto&n:plan.layers["children"].array())track=track||n["id"].string()=="volume/app-track";check(!track,"A list that fits shows no scrollbar");
    gpu::VolumeController source(apps(3),{},{},gpu::VolumeLayout::source());source.setActive(true);
    check(source.applicationViewport()==core::Rect{12,234,376,64}&&source.maximumApplicationScroll()==29&&fullyVisibleRows(source)==2,"The source layout keeps its 64-point two-row viewport");
    gpu::VolumeController four(apps(4),calls);four.setActive(true);
    check(four.maximumApplicationScroll()==29&&fullyVisibleRows(four)==3,"A fourth app scrolls (by the source rule: rows·31 − viewport)");
    plan=gpu::prepareVolumeScene(four);const Json*thumb{};const Json*trackLayer{};for(const auto&n:plan.layers["children"].array()){if(n["id"].string()=="volume/app-thumb")thumb=&n;if(n["id"].string()=="volume/app-track")trackLayer=&n;}
    check(trackLayer&&(*trackLayer)["bounds"]==Json::Array{0,0,2,95}&&thumb&&std::abs((*thumb)["bounds"].array()[3].number()-95.*95/(29+95))<1e-9,"The source scrollbar track and thumb span the three-row viewport");
    // Wheel: continuous point offsets, only the app sliders move.
    gpu::VolumeController c(fixture(),calls);c.setActive(true);const auto artwork=c.artworkRevision(),content=c.contentRevision();const auto before=appSliderTops(c);
    check(c.scroll({20,300},7.25,2)&&c.applicationScroll()==7.25&&c.artworkRevision()==artwork&&c.contentRevision()==content+1,"A wheel step changes the list offset without changing any artwork");
    const auto after=appSliderTops(c);check(after.size()>=3&&std::abs(after[0]-(before[0]-7.25))<1e-12&&std::abs(after[1]-(before[1]-7.25))<1e-12,"App sliders follow fractional wheel travel exactly (no row snapping)");
    const auto[first,last]=c.applicationWindow();check(first==0&&last==5,"The retained window is the visible rows (four, one partly) plus one below");
    check(c.scroll({20,300},31*3,3)&&c.applicationWindow().first==2&&c.applicationWindow().second==8,"Scrolling slides the retained window with the rows");
    check(!c.scroll({20,331},10,4)&&c.scroll({20,328},-1000,4)&&c.applicationScroll()==0,"The wheel acts inside the three-row viewport and clamps at the top");
    // Content drag: the rows follow the pointer; nothing is written.
    check(c.mouseDown({60,300},5)&&c.panning()&&c.dragging()&&writes==0,"A press on a scrollable list outside its sliders starts a list drag");
    check(c.mouseDragged({60,260})&&c.applicationScroll()==40&&c.mouseDragged({60,250})&&c.applicationScroll()==50&&c.artworkRevision()==artwork,"Dragging up scrolls the list by the pointer travel");
    check(c.mouseDragged({60,-5000})&&c.applicationScroll()==c.maximumApplicationScroll()&&!c.mouseDragged({60,-6000}),"A list drag clamps at the end of the list");
    check(c.mouseDragged({60,5000})&&c.applicationScroll()==0&&writes==0,"A list drag clamps at the top and never changes a gain");c.mouseUp();check(!c.panning()&&!c.dragging(),"Pointer up ends the list drag");
    check(c.mouseDown({200,248},6)&&!c.panning()&&c.dragging()&&writes==1,"A press on an app slider still adjusts that app, not the list");c.mouseUp();
    // Scrollbar drag: the thumb follows the pointer over the track travel.
    const auto max=c.maximumApplicationScroll();const auto size=std::max(12.,95.*95/(max+95));
    check(c.mouseDown({393,236},7)&&c.panning()&&c.applicationScroll()==0,"A press on the thumb grabs it without moving the list");
    check(c.mouseDragged({393,236+(95-size)*.5})&&std::abs(c.applicationScroll()-max*.5)<1e-9,"Dragging the thumb half its travel scrolls half the list");
    c.mouseDragged({393,1000});check(c.applicationScroll()==max,"The thumb drag clamps at the end");c.mouseUp();
    check(c.mouseDown({393,240},8)&&c.applicationScroll()==0,"A press on the track outside the thumb centres the thumb at the pointer (clamped)");c.mouseUp();
    // A per-app status keeps the source status line clear: two rows.
    auto status=c.snapshot();status.perAppStatus="Synthetic per-app status";c.receiveSnapshot(status);
    check(c.applicationViewport()==core::Rect{12,234,376,64}&&c.maximumApplicationScroll()==12*31-64.,"A per-app status message shows the source two-row viewport");
    check(c.scroll({20,260},10000,9)&&c.applicationScroll()==12*31-64.&&!c.scroll({20,300},-10,9),"The two-row viewport takes the wheel only above the status line");
    plan=gpu::prepareVolumeScene(c);for(const auto&n:plan.layers["children"].array())if(n["id"].string()=="volume/per-app-status")check(n["text"]["string"].string()=="Synthetic per-app status","The status line keeps its source text and place");
    status.perAppStatus.reset();status.routingStopped=true;c.receiveSnapshot(status);check(c.applicationViewport().height==64,"Stopped routing shows its source status line too");
    status.routingStopped=false;c.receiveSnapshot(status);check(c.applicationViewport().height==95&&c.applicationScroll()==12*31-95.,"Without a status the list returns to three rows and clamps its offset");
    // Composed plan (source paint order): visible rows clipped to the viewport.
    c.scroll({20,300},-1000,9);c.scroll({20,300},15,9);plan=gpu::prepareVolumeScene(c);std::size_t rows{};for(std::size_t n=0;n<plan.surfaces.size();++n){const auto&id=plan.surfaces[n].id;if(id.starts_with("volume/app-row/")){check(plan.surfaces[n].clip==c.applicationViewport(),"Every app row leaf is clipped to the list viewport");if(id.ends_with("/name"))++rows;}}
    check(rows==4,"At a partial offset the clipped list paints the three rows' worth of partially visible rows");
    check(leaf(plan,"volume/app-row/0/name")["position"]==Json::Array{18,235-15.}&&leaf(plan,"volume/app-row/3/name")["position"]==Json::Array{18,235+93-15.},"Row captions follow the offset exactly");
    rejects([&]{gpu::VolumeController bad(fixture(),{},{},gpu::VolumeLayout{4});},"The canvas fits at most three app rows");
}
// User-reported: switching between Headphones / Bluetooth and App volume
// "shook". The source motion (VolumeCanvas.animateAction) is a 0.18 s
// sublayerTransform from translate(11,0,-10)·m34(-1/700) to identity on the
// content layer, which Core Animation renders about the layer's anchor (its
// centre) and flattens into its plane, starting when the transaction commits.
// Applied about the top-left corner it shrank the module towards the corner
// and dragged the whole page up and left by ~2.4-2.8 points; started at the
// press it lost its first frames to the page rebuild.
// Memory: the source form paints every plate and slider part as a zero-frame
// shape with a canvas-coordinate path, so each raster spanned from the canvas
// origin to the shape (13.2 MB of rasters for the App volume page at 2x). The
// native (compact) form moves each such shape to a whole-point origin two
// points above-left of its path: the absolute geometry is unchanged.
std::vector<core::Point>points(const Json&path,core::Point offset){std::vector<core::Point>out;for(const auto&c:path.array())for(const auto&q:c["points"].array())out.push_back({q.array()[0].number()+offset.x,q.array()[1].number()+offset.y});return out;}
void compactForm(){
    gpu::VolumeController c(fixture());c.setActive(true);std::size_t shapes{};
    const auto compare=[&](const char*page){
        const auto source=gpu::prepareVolumeScene(c),compact=gpu::prepareVolumeScene(c,{},true);
        check(source.surfaces.size()==compact.surfaces.size()&&source.surfaces.size()==compact.layers["children"].array().size(),"The compact form keeps every source leaf in paint order");
        for(std::size_t n=0;n<source.surfaces.size();++n){
            const auto&a=source.layers["children"].array()[n],&b=compact.layers["children"].array()[n];const auto&sa=source.surfaces[n],&sb=compact.surfaces[n];
            check(sa.id==sb.id&&sa.clip==sb.clip&&sa.feedback==sb.feedback&&sa.rim==sb.rim&&sa.opacity==sb.opacity,"Surface identity, clip, feedback and opacity are unchanged");
            if(a["kind"].string()!="shape"||a["position"].array()[0].number()!=0||a["position"].array()[1].number()!=0||!(sa.local==core::Matrix4{})){check(a==b&&sa.local==sb.local,"Leaves with their own frame are unchanged");continue;}
            double nearest=INFINITY;for(const auto&q:points(a["shape"]["path"],{}))nearest=std::min({nearest,q.x,q.y});
            if(nearest<2){check(a==b&&sa.local==sb.local,"A shape within two points of the canvas origin keeps the source form");continue;}
            ++shapes;const core::Point origin{sb.local.values[12],sb.local.values[13]};
            check(origin.x==std::floor(origin.x)&&origin.y==std::floor(origin.y)&&b["position"]==Json(Json::Array{origin.x,origin.y})&&sb.local==core::Matrix4::translation(origin.x,origin.y),"A compact shape sits at a whole-point origin");
            const auto absolute=points(a["shape"]["path"],{}),moved=points(b["shape"]["path"],origin);check(absolute.size()==moved.size(),"The compact path keeps every command");
            double left=INFINITY,top=INFINITY;for(std::size_t k=0;k<absolute.size();++k){check(std::abs(absolute[k].x-moved[k].x)<1e-9&&std::abs(absolute[k].y-moved[k].y)<1e-9,"The compact path paints the same absolute geometry");left=std::min(left,absolute[k].x);top=std::min(top,absolute[k].y);}
            check(origin.x==std::floor(left)-2&&origin.y==std::floor(top)-2,"The origin is two whole points above-left of the path");
            auto x=a,y=b;x["shape"]["path"]=Json{};y["shape"]["path"]=Json{};y["position"]=Json::Array{0,0};check(x==y,"Fill, stroke and every other shape property are unchanged");}
        std::cout<<"Volume compact form ("<<page<<"): "<<source.surfaces.size()<<" leaves\n";};
    compare("App volume");c.perform("audio:headphones",1);compare("Headphones");c.perform("audio:output",2);compare("output chooser");
    check(shapes>40,"Every plate and slider part is compacted");
}
std::array<double,3>apply(const core::Matrix4&m,double x,double y){const auto&v=m.values;return {v[0]*x+v[4]*y+v[12],v[1]*x+v[5]*y+v[13],v[3]*x+v[7]*y+v[15]};}
void actionMotion(){
    gpu::VolumeController c(fixture());c.setActive(true);
    check(gpu::volumeContentTransform({})==core::Matrix4{},"A settled action leaves the content exactly in place");
    check(c.perform("audio:headphones",1)&&c.headphones(),"The detail switch requests the source action");
    // Pending until a frame presents it: the start pose, however late.
    for(const double t:{1.,1.05,3.}){const auto s=c.actionSample(t);check(s.active&&s.transform.values[12]==11&&s.transform.values[14]==-10&&s.reveal==core::Rect{10,0,390,334},"A requested action holds the source start pose until a frame presents it (Core Animation begins at the commit, after the repaint)");}
    check(c.presentAction(3)&&!c.presentAction(3.01),"The first presenting frame begins the motion once");
    const auto start=c.actionSample(3);check(start.active&&start.transform.values[12]==11,"The first presented frame shows the start pose, not a motion already advanced by the rebuild");
    const double w0=1+10./700;const auto rendered=gpu::volumeContentTransform(start.transform);
    const auto anchor=apply(rendered,200,167),corner=apply(rendered,0,0),opposite=apply(rendered,400,334);
    check(std::abs(anchor[0]/anchor[2]-(200+11/w0))<1e-9&&std::abs(anchor[1]/anchor[2]-167)<1e-9,"The content centre only slides sideways (11 points through the source depth)");
    check(std::abs(corner[0]/corner[2]-(200-189/w0))<1e-9&&std::abs(corner[1]/corner[2]-(167-167/w0))<1e-9&&std::abs(opposite[1]/opposite[2]-(167+167/w0))<1e-9,"The source depth shrinks the page symmetrically about its centre, not towards its top-left corner");
    check(rendered.values[2]==0&&rendered.values[6]==0&&rendered.values[14]==0,"The content plane is flattened: no depth leaves the plane");
    // The anchor moves monotonically back to rest, never vertically.
    double previous=anchor[0]/anchor[2];for(int n=1;n<=12;++n){const auto t=3+n/60.;const auto s=c.actionSample(t);const auto p=apply(gpu::volumeContentTransform(s.transform),200,167);const double x=p[0]/p[2];
        check(std::abs(p[1]/p[2]-167)<1e-9&&x<=previous+1e-12&&x>=200-1e-12,"Each frame moves the page centre straight back towards rest, without vertical motion or overshoot");previous=x;}
    check(!c.actionSample(3.18).active&&!c.presentAction(3.2),"The motion ends after the source 0.18 s");
    check(c.perform("audio:applications",4)&&!c.headphones()&&c.actionSample(4).active,"Back to App volume requests the motion again");c.setActive(false);check(!c.actionSample(5).active&&!c.presentAction(5),"Leaving Volume drops a pending motion");
    c.setActive(true);check(c.perform("audio:headphones",6,true)&&!c.actionSample(6).active&&!c.presentAction(6),"Reduced motion requests no action");
}
#ifdef _WIN32
class Window{public:Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldVolumeOwnedFixture";atom=RegisterClassW(&c);check(atom!=0,"Register owned Volume fixture");hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned Volume fixture",WS_POPUP,0,0,400,334,nullptr,nullptr,c.hInstance,nullptr);check(hwnd!=nullptr,"Create hidden owned Volume fixture");}~Window(){if(hwnd)DestroyWindow(hwnd);if(atom)UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}HWND hwnd{};ATOM atom{};};
// Names of the composed app rows' caption draws, in paint order: y of each.
std::vector<double>rowCaptionTops(const gpu::LayerComposition&composition){std::vector<double>tops;for(const auto&d:composition.draws())if(d.sourceID.ends_with("volume/app-row/name"))tops.push_back(d.world.values[13]);return tops;}
// User-reported: scrolling more than two apps was not smooth because every
// wheel event reloaded and repainted the whole module. Rows are now retained
// scenes that move by the offset alone.
template<class Entries>void scrolling(gpu::Renderer&renderer,gpu::LayerRasterizer&raster,gpu::LayerComposition&composition,gpu::NativeVolumeScene&scene,gpu::VolumeController&c,Entries entries){
    double time=20;auto publish=[&]{scene.syncContent();scene.updatePose({},1,time);composition.setEntries(renderer,entries());check(scene.collectRetired(renderer),"Retired rows release after publication");composition.present(renderer);};
    c.scroll({20,300},-1000,time);publish();renderer.draw(false);
    const auto stats=scene.stats();const auto rasterBefore=raster.stats();const auto tops=rowCaptionTops(composition);check(tops.size()==scene.rowCount()&&tops.size()>=4,"Each retained row composes its caption");
    // A fractional (precision touchpad) step inside the current row window.
    check(c.scroll({20,300},7.25,time+=.01),"Wheel step lands in the list");publish();
    const auto moved=rowCaptionTops(composition);check(moved.size()==tops.size(),"A step inside the window keeps the same retained rows");for(std::size_t n=0;n<tops.size();++n)check(std::abs(moved[n]-(tops[n]-7.25))<1e-9,"Rows move by the exact continuous offset");
    const auto scrolled=scene.stats();check(scrolled.structureUpdates==stats.structureUpdates&&scrolled.localUpdates==stats.localUpdates&&scrolled.rowBuilds==stats.rowBuilds&&scrolled.scrollSyncs==stats.scrollSyncs+1,"A scroll event neither rebuilds nor repaints the module or any row");
    check(raster.stats().textLayoutsCreated==rasterBefore.textLayoutsCreated&&raster.stats().rasterizations-rasterBefore.rasterizations<=4&&scrolled.rowUpdates-stats.rowUpdates<=4,"Only the clipped highlight of a row crossing the viewport edge repaints; no caption is laid out again");
    // Settled frames after the scroll: no allocation, raster or upload.
    const auto settledRaster=raster.stats();const auto settledGPU=renderer.stats();allocations=0;counting=true;
    try{for(unsigned n=0;n<60;++n){core::Matrix4 matrix;matrix.values[3]=n*.000001;scene.updatePose(matrix,1,time+=1./60);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==settledRaster.rasterizations&&raster.stats().textLayoutsCreated==settledRaster.textLayoutsCreated&&renderer.stats().textureUploads==settledGPU.textureUploads&&renderer.stats().meshUploads==settledGPU.meshUploads&&!scene.requiresFrames(time),"Settled scroll frames allocate nothing, rasterize nothing, upload nothing and demand no frames");
    // A step that brings a new row into the window builds only that row; the
    // next one also retires the row that left the window above.
    auto windowed=scene.stats();auto layouts=raster.stats().textLayoutsCreated;check(c.scroll({20,300},31,time+=.01),"Second wheel step");publish();
    check(scene.stats().rowBuilds==windowed.rowBuilds+1&&scene.stats().rowRetirements==windowed.rowRetirements&&scene.stats().structureUpdates==windowed.structureUpdates&&scene.stats().localUpdates==windowed.localUpdates&&raster.stats().textLayoutsCreated-layouts<=3,"A row entering the window is built once (its three captions) and nothing else is rebuilt");
    windowed=scene.stats();layouts=raster.stats().textLayoutsCreated;check(c.scroll({20,300},31,time+=.01),"Third wheel step");publish();
    check(scene.stats().rowBuilds==windowed.rowBuilds+1&&scene.stats().rowRetirements==windowed.rowRetirements+1&&scene.stats().structureUpdates==windowed.structureUpdates&&raster.stats().textLayoutsCreated-layouts<=3&&scene.rowCount()==6,"The far row retires as the next one enters; the window stays bounded");
    // Back within the retained window: the retained rows return as they were.
    const auto back=scene.stats();check(c.scroll({20,300},-31,time+=.01),"Reverse wheel step");publish();check(scene.stats().structureUpdates==back.structureUpdates&&scene.stats().rowBuilds<=back.rowBuilds+1,"Reverse scrolling rebuilds at most the one row that re-enters the window");
    // Continuous drag over many rows keeps the module scene untouched.
    c.scroll({20,300},-1000,time+=.01);publish();const auto dragged=scene.stats();check(c.applicationScroll()==0&&c.mouseDown({60,300},time+=.01)&&c.panning(),"List drag starts");for(unsigned n=1;n<=40;++n){c.mouseDragged({60,300-n*3.5});publish();}c.mouseUp();
    check(scene.stats().structureUpdates==dragged.structureUpdates&&scene.stats().localUpdates==dragged.localUpdates&&std::abs(c.applicationScroll()-140)<1e-9&&scene.rowCount()<=7,"Dragging through the list moves retained rows only and keeps the window bounded");
}
// User-reported shaking on the Headphones / App volume switch: the switch
// used to reload the whole module (every caption, plate and slider) and to
// destroy and rebuild every app row, so its first frame came late. Now the
// header keeps its surfaces (two highlight leaves change), the detail page
// alone reloads, and the Headphones page parks the rows instead.
template<class Entries>void detailSwitch(gpu::Renderer&renderer,gpu::LayerRasterizer&raster,gpu::LayerComposition&composition,gpu::NativeVolumeScene&scene,gpu::VolumeController&c,Entries entries){
    double time=40;auto publish=[&](double t){scene.syncContent();scene.updatePose({},1,t);composition.setEntries(renderer,entries());check(scene.collectRetired(renderer),"Retired rows release after publication");composition.present(renderer);};
    c.scroll({20,300},-1000,time);publish(time);const auto rows=scene.rowCount();check(rows>=4&&!c.headphones(),"App volume composes its retained rows");
    auto before=scene.stats();auto raster0=raster.stats();
    check(c.perform("audio:headphones",time+=.5)&&c.headphones(),"Switch to Headphones / Bluetooth");publish(time+=.02);
    auto after=scene.stats();const auto toHeadphones=raster.stats().rasterizations-raster0.rasterizations,headphonesLayouts=raster.stats().textLayoutsCreated-raster0.textLayoutsCreated;
    std::cout<<"Volume scene: App volume -> Headphones: "<<toHeadphones<<" rasterizations, "<<headphonesLayouts<<" text layouts (header "<<scene.scene().draws().size()<<" surfaces kept)\n";
    check(after.structureUpdates==before.structureUpdates&&after.detailBuilds==before.detailBuilds+1&&after.localUpdates==before.localUpdates+2,"The switch keeps the header (two detail-button highlights change) and loads only the detail page");
    check(scene.rowCount()==0&&scene.parkedRowCount()==rows&&after.rowParks==before.rowParks+rows&&after.rowRetirements==before.rowRetirements&&after.rowBuilds==before.rowBuilds,"The Headphones page parks the app rows instead of destroying them");
    check(toHeadphones<=13&&headphonesLayouts<=6,"Two highlights plus the two device rows and the page controls are all the switch paints");
    check(scene.entries().size()==2,"Parked rows are not composed");
    before=scene.stats();raster0=raster.stats();
    check(c.perform("audio:applications",time+=.5)&&!c.headphones(),"Back to App volume");publish(time+=.02);after=scene.stats();
    const auto toApps=raster.stats().rasterizations-raster0.rasterizations,appsLayouts=raster.stats().textLayoutsCreated-raster0.textLayoutsCreated;
    std::cout<<"Volume scene: Headphones -> App volume: "<<toApps<<" rasterizations, "<<appsLayouts<<" text layouts ("<<rows<<" rows revived)\n";
    check(scene.rowCount()==rows&&scene.parkedRowCount()==0&&after.rowRevivals==before.rowRevivals+rows&&after.rowBuilds==before.rowBuilds&&after.rowUpdates==before.rowUpdates,"App volume composes the parked rows again without rebuilding or repainting them");
    check(after.structureUpdates==before.structureUpdates&&toApps<=5&&appsLayouts<=1,"Returning repaints the two highlights and the list's status, track and thumb only");
    // Leaving Volume from the Headphones page releases its parked rows once
    // the departing artwork is published without them.
    check(c.perform("audio:headphones",time+=.5),"Headphones again");publish(time+=.02);check(scene.parkedRowCount()==rows,"Rows parked");
    c.setActive(false);scene.retainDepartingArtwork(true);composition.setEntries(renderer,entries());check(scene.collectRetired(renderer)&&scene.parkedRowCount()==0,"A departing Volume releases its parked rows after the next publication");
    scene.retainDepartingArtwork(false);c.setActive(true);check(c.perform("audio:applications",time+=.5),"Reactivated on App volume");publish(time+=.02);check(scene.rowCount()==rows,"Rows are rebuilt after a departure released them");
}
// The compact raster of every shape holds exactly the source raster's pixels
// on the same pixel grid (at 1x and the production 2x), and nothing else.
void compactRasters(){
    gpu::LayerRasterizer raster;gpu::VolumeController c(fixture());c.setActive(true);std::size_t serial{};
    for(const double density:{1.,2.}){
        gpu::LayerRasterOptions options;options.pixelsPerPoint=density;std::size_t sourceBytes{},compactBytes{};
        for(const char*page:{"audio:applications","audio:headphones","audio:output","audio:back"}){
            check(c.perform(page,double(++serial)),"Page shown");
            const auto source=gpu::prepareVolumeScene(c),compact=gpu::prepareVolumeScene(c,{},true);
            for(std::size_t n=0;n<source.surfaces.size();++n){
                const auto&a=source.layers["children"].array()[n],&b=compact.layers["children"].array()[n];if(a==b)continue;
                const auto x=raster.rasterize("source/"+std::to_string(++serial),1,a,options),y=raster.rasterize("compact/"+std::to_string(serial),1,b,options);
                check(x&&y&&x->complete()&&y->complete(),"Both forms rasterize completely");sourceBytes+=x->straightRGBA.size();compactBytes+=y->straightRGBA.size();
                const double fx=(compact.surfaces[n].local.values[12]+y->bounds.x-x->bounds.x)*density,fy=(compact.surfaces[n].local.values[13]+y->bounds.y-x->bounds.y)*density;
                check(fx==std::floor(fx)&&fy==std::floor(fy),"The compact raster lies on the source raster's pixel grid");
                const auto dx=static_cast<long long>(fx),dy=static_cast<long long>(fy);bool same=true;
                for(long long j=0;j<static_cast<long long>(x->height)&&same;++j)for(long long i=0;i<static_cast<long long>(x->width)&&same;++i){
                    const auto*p=&x->straightRGBA[(std::size_t(j)*x->width+std::size_t(i))*4];const auto u=i-dx,v=j-dy;
                    if(u<0||v<0||u>=static_cast<long long>(y->width)||v>=static_cast<long long>(y->height)){same=p[3]==0;continue;}
                    const auto*q=&y->straightRGBA[(std::size_t(v)*y->width+std::size_t(u))*4];same=p[3]==q[3]&&(p[3]==0||(p[0]==q[0]&&p[1]==q[1]&&p[2]==q[2]));}
                for(long long v=0;v<static_cast<long long>(y->height)&&same;++v)for(long long u=0;u<static_cast<long long>(y->width)&&same;++u){
                    const auto i=u+dx,j=v+dy;if(i<0||j<0||i>=static_cast<long long>(x->width)||j>=static_cast<long long>(x->height))same=y->straightRGBA[(std::size_t(v)*y->width+std::size_t(u))*4+3]==0;}
                check(same,"The compact raster paints exactly the source raster's pixels");
            }
        }
        std::cout<<"Volume shape rasters at "<<density<<"x over four pages: source form "<<sourceBytes<<" B, compact "<<compactBytes<<" B\n";
        check(compactBytes*3<sourceBytes,"Compact shape rasters are a fraction of the source form's");
    }
}
void native(const std::filesystem::path&shader){Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,400,334,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});renderer.setCamera(gpu::layerViewportProjection(400,334));gpu::LayerRasterizer raster;gpu::LayerRasterOptions options;options.pixelsPerPoint=1;gpu::LayerImageSource icons;options.memoryImages=&icons;
    auto state=fixture();gpu::VolumeController c(state);c.setActive(true);gpu::NativeVolumeScene scene(c,raster,options);check(scene.syncContent()&&!scene.syncContent(),"Retained Volume source content loads only on explicit events");check(scene.scene().report().unsupported.empty(),"Volume source shapes/text are supported by shared raster");
    check(scene.rowCount()==5&&scene.entries().size()==7,"The visible app rows and one row below are retained scenes after the module's header and detail scenes");for(const auto&e:scene.entries())check(e.scene->report().unsupported.empty(),"Retained app rows are supported by the shared raster");
    scene.updatePose({},1,0);gpu::LayerComposition composition;const auto entries=[&]{return scene.entries();};composition.setEntries(renderer,entries());composition.present(renderer);renderer.draw(false);const auto first=renderer.readback();check(!first.pixels.empty(),"Hidden fixture paints real original-style Volume artwork");
    scene.setFeedback(core::Point{100,55},false,false,1);scene.updatePose({},1,1.14);composition.present(renderer);const auto steadyRaster=raster.stats();const auto steadyGPU=renderer.stats();allocations=0;counting=true;try{for(unsigned n=0;n<120;++n){core::Matrix4 matrix;matrix.values[3]=n*.000001;scene.updatePose(matrix,.8f,2+n/60.);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0,"120 Volume tilt frames allocate no CPU storage");check(raster.stats().rasterizations==steadyRaster.rasterizations&&raster.stats().textLayoutsCreated==steadyRaster.textLayoutsCreated&&renderer.stats().textureUploads==steadyGPU.textureUploads&&renderer.stats().meshUploads==steadyGPU.meshUploads,"Pointer feedback and tilt reuse artwork/text/GPU resources");
    state.volume=0;c.receiveSnapshot(state);scene.syncContent();scene.updatePose({},1,5);composition.upload(renderer);composition.present(renderer);renderer.draw(false);check(scene.scene().report().unsupported.empty(),"Zero gain degenerate source fill is supported explicitly");check(scene.stats().localUpdates>0&&scene.stats().structureUpdates==1,"Master value event updates changed leaves without rebuilding complete module");
    c.perform("audio:output",6);scene.syncContent();scene.setFeedback({},false,true,6);scene.updatePose({},1,6);check(scene.rowCount()==0,"The device chooser retains no app rows");composition.setEntries(renderer,entries());check(scene.collectRetired(renderer),"Rows leaving the window release after the composition published without them");composition.present(renderer);renderer.draw(false);check(scene.requiresFrames(6)&&!scene.requiresFrames(6.18),"Device menu action uses only its finite source frame deadline");
    c.dismissChooser(7);scene.syncContent();scene.updatePose({},1,7.18);composition.setEntries(renderer,entries());composition.present(renderer);state.volume.reset();state.balance.reset();state.canSetVolume=state.canSetBalance=false;c.receiveSnapshot(state);scene.syncContent();scene.updatePose({},1,8);composition.setEntries(renderer,entries());composition.present(renderer);renderer.draw(false);check(scene.scene().report().unsupported.empty(),"Unavailable controls render without guessing values");
    std::array<std::uint8_t,64*64*4>rgba{};for(std::size_t k=0;k<rgba.size();k+=4){rgba[k]=100;rgba[k+1]=20;rgba[k+2]=10;rgba[k+3]=255;}icons.publish("volume.synthetic.icon",1,64,64,rgba);
    state=c.snapshot();state.applications[0].icon=gpu::VolumeApplicationIcon{"volume.synthetic.icon",1};c.receiveSnapshot(state);scene.syncContent();scene.updatePose({},1,9);composition.setEntries(renderer,entries());composition.present(renderer);renderer.draw(false);const auto iconFrame=renderer.readback();const auto offset=std::size_t(248)*iconFrame.rowBytes+28*4;
    check(iconFrame.pixels[offset+3]>=254&&std::abs(int(iconFrame.pixels[offset+2])-100)<=2&&std::abs(int(iconFrame.pixels[offset+1])-20)<=2&&std::abs(int(iconFrame.pixels[offset])-10)<=2,"Shared native instance icon paints at source position with original color and no accent tint");
    const auto warmedRaster=raster.stats();const auto warmedImages=icons.stats();const auto warmedGPU=renderer.stats();allocations=0;counting=true;try{for(unsigned n=0;n<120;++n){core::Matrix4 matrix;matrix.values[3]=n*.000001;scene.updatePose(matrix,.9f,10+n/60.);composition.present(renderer);}}catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&raster.stats().rasterizations==warmedRaster.rasterizations&&icons.stats().cacheHits==warmedImages.cacheHits&&renderer.stats().textureUploads==warmedGPU.textureUploads,"120 icon-bearing tilt frames perform no allocations, rasterization, cache lookup or texture upload");
    scrolling(renderer,raster,composition,scene,c,entries);
    detailSwitch(renderer,raster,composition,scene,c,entries);
    // Memory: once no composition publishes it, park releases every part, the
    // rasterizer's entries (and CPU pixels behind them) and every texture; the
    // next content sync rebuilds the same artwork.
    {const auto resident=scene.stats();check(resident.parts>=3&&resident.pixelBytes>0&&!scene.park(renderer)&&scene.resident()&&scene.stats().pixelBytes==resident.pixelBytes&&scene.stats().parks==0,"A composed Volume refuses to park and keeps every raster");
     composition.setEntries(renderer,{});check(scene.park(renderer)&&!scene.resident()&&scene.stats().parts==0&&scene.stats().pixelBytes==0&&scene.stats().parks==1&&raster.stats().entries==0&&renderer.stats().textures==0,"Unpublished, park releases every Volume part, rasterizer entry and texture");
     check(!scene.park(renderer)&&scene.stats().parks==1,"Parking released artwork does nothing");
     check(scene.syncContent()&&scene.resident()&&scene.stats().parts==resident.parts&&scene.stats().pixelBytes==resident.pixelBytes,"The next content sync rebuilds the same artwork");
     scene.updatePose({},1,100);composition.setEntries(renderer,entries());composition.present(renderer);}
    composition.detach(renderer);check(scene.releaseResources(renderer)&&renderer.stats().textures==0&&renderer.stats().meshes==0,"Owner detach retires every Volume resource (module scene and app rows) without another publisher");renderer.reset();
}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{check(SUCCEEDED(hr)&&argc==2,"Pass HLSL to hidden Volume fixture");portable();listLayout();actionMotion();compactForm();compactRasters();native(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" Volume checks\n";return 0;}catch(const std::exception&e){counting=false;if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){try{portable();listLayout();actionMotion();compactForm();std::cout<<"PASS "<<checks<<" Volume portable checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
