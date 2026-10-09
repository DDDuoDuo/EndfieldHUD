#include "core/watch_runtime_input.hpp"
#include "app/source_watch_session.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace {bool countAllocations{};std::size_t allocations{};}
void* operator new(std::size_t n){if(countAllocations)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using namespace endfield::app;
using namespace endfield::core;
using namespace endfield::core::source;
namespace {
std::size_t checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
void checkNear(double a,double b,double tolerance,const std::string&message){++checks;if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-b)>tolerance*std::max(1.,std::abs(b)))throw std::runtime_error(message+": "+std::to_string(a)+" != "+std::to_string(b));}
template<class F>void rejects(F call,const char*message){bool rejected=false;try{call();}catch(const std::exception&){rejected=true;}check(rejected,message);}
Json emptyTransitions(){return Json::Object{{"controllers",Json::Array{}},{"instances",Json::Array{}}};}
Json syntheticCamera(){
    const auto v3=[](double x,double y,double z){return Json::Object{{"x",x},{"y",y},{"z",z}};};
    const auto transform=[&](const char*id,const char*go,const char*kind,double z){
        Json result=Json::Object{{"cab","test"},{"path_id",id},{"type",kind}};
        result["data"]=Json::Object{{"m_GameObject",Json::Object{{"m_PathID",go}}},{"m_Father",Json::Object{{"m_PathID","0"},{"m_FileID",0}}},
            {"m_LocalPosition",v3(0,0,z)},{"m_LocalScale",v3(1,1,1)},{"m_LocalRotation",Json::Object{{"x",0},{"y",0},{"z",0},{"w",1}}}};return result;
    };
    Json::Array keys;for(int t:{-1,1})keys.push_back(Json::Object{{"time",t},{"value",t},{"inSlope",1},{"outSlope",1},{"weightedMode",0}});
    Json axis=Json::Object{{"valueCurve",Json::Object{{"m_Curve",keys}}},{"maxAngle",5}};
    return Json::Array{transform("root","hud","RectTransform",10),transform("ct","camera","Transform",0),
        Json::Object{{"cab","test"},{"type","MonoBehaviour"},{"script",Json::Object{{"m_ClassName","UIGyroscopeEffect"}}},{"data",Json::Object{{"m_GameObject",Json::Object{{"m_PathID","hud"}}},{"ease",6},{"enableDetect",true},{"time",.3},{"x",axis},{"y",axis}}}},
        Json::Object{{"cab","test"},{"type","MonoBehaviour"},{"script",Json::Object{{"m_ClassName","UICanvasScaleHelper"}}},{"data",Json::Object{{"m_GameObject",Json::Object{{"m_PathID","hud"}}}}}},
        Json::Object{{"cab","test"},{"type","Camera"},{"data",Json::Object{{"m_GameObject",Json::Object{{"m_PathID","camera"}}},{"m_NormalizedViewPortRect",Json::Object{{"x",0},{"y",0},{"width",1},{"height",1}}},
            {"m_LensShift",Json::Object{{"x",0},{"y",0}}},{"field of view",60},{"near clip plane",.1},{"far clip plane",1000}}}}};
}
Key scalar(double time,double value){Key k;k.time=time;k.value=Value::scalar(value);k.inSlope=k.outSlope=Value::scalar(0);k.inWeight=k.outWeight=Value::scalar(1./3);return k;}
Clip clip(std::string binding,std::string id,double length,double from,double to){Clip c;c.binding=std::move(binding);c.id=std::move(id);c.lastKeyTime=length;c.sampleRate=60;c.wrapMode=2;c.curves.emplace_back("m_FloatCurves","","m_Alpha",std::vector<std::string>{"root"},std::vector<Key>{scalar(0,from),scalar(length,to)});return c;}
struct Synthetic {
    SceneDefinition scene;
    MountedLayoutDocument doc;
    Library library{std::vector<Clip>{clip("_animationIn","in",.75,0,1),clip("_animationLoop","loop",2,1,1),clip("_animationOut","out",.333,1,0)}};
    SourceCamera camera{syntheticCamera()};SourceWatchFrameResources resources;
    Synthetic():scene([]{Node root;root.id="root";root.name="Root";root.path="Root";root.children={"button","reddot"};root.rect=RectTransform{};
        Node b;b.id="button";b.name="TestBtn";b.path="Root/TestBtn";b.parent="root";b.children={"lock"};b.rect=RectTransform{{.5,.5},{.5,.5},{},{300,300},{.5,.5}};
        Node dot;dot.id="reddot";dot.name=" NoticeRedDot \n";dot.path="Root/RedDot";dot.parent="root";dot.rect=RectTransform{};
        Node lock;lock.id="lock";lock.name="LockIcon";lock.path="Root/TestBtn/LockIcon";lock.parent="button";lock.rect=RectTransform{};
        return SceneDefinition("root",{root,b,dot,lock});}()){
        doc.components["root"]={{"canvas","Canvas",{},Json::Object{{"m_SortingOrder",6080}}},{"cut","MonoBehaviour","UIWatchPanelCut",Json::Object{}},{"alpha","CanvasGroup",{},Json::Object{{"m_Alpha",1}}}};
        auto color=[](double x){return Json::Object{{"r",1},{"g",1},{"b",1},{"a",x}};};Json colors=Json::Object{{"m_NormalColor",color(.2)},{"m_HighlightedColor",color(1)},{"m_PressedColor",color(.5)},{"m_SelectedColor",color(.8)},{"m_DisabledColor",color(.1)},{"m_ColorMultiplier",1},{"m_FadeDuration",.2}};
        doc.components["button"]={{"image","MonoBehaviour","UIImage",Json::Object{{"m_RaycastTarget",true}}},{"button-component","MonoBehaviour","UIButton",Json::Object{{"m_Transition",1},{"m_TargetGraphic",Json::Object{{"target_id","image"}}},{"m_Colors",colors},{"_clickCd",.2}}}};
        doc.buttons={{"button","Root/TestBtn",{}}};resources.materialVariants["__ui_default"][0]="__ui_default";resources.textureSizes["__white"]={1,1};
    }
};
WatchSessionEnvironment environment(){return {{1280,800},true,true,true,false,{}};}
void synthetic(){
    Synthetic data;SourceWatchSession session(data.scene,data.doc,data.library,data.camera,data.resources,{},emptyTransitions());
    auto env=environment();session.setEnvironment(env,0);WatchSessionSettings settings;settings.ambientEnabled=false;session.setSettings(settings,0);
    WatchSessionNavigation navigation;navigation.fixedActions["button"]=7;navigation.managedButtons.insert("button");session.setNavigation(navigation,0);
    check(!session.sample(0)&&!FrameDemandGate{}.refresh(session.demand(0)).present,"Concealed session has no frame or wake demand");
    const auto before=session.frameStats();allocations=0;countAllocations=true;for(unsigned i=0;i<1000;++i){session.sample(i*.001);session.demand(i*.001);}countAllocations=false;
    check(allocations==0&&session.frameStats().builds==before.builds,"Concealed samples do not allocate or traverse/build the scene");
    const auto token=session.open(2,4,true);check(session.pendingOpening()&&!FrameDemandGate{}.refresh(session.demand(2)).timerInterval,"Held opening has a first pose but no animation wake");
    auto first=session.sample(2);check(first&&first->visibility.entranceTime==0,"Held source entrance begins at authored time zero");
    check(session.sample(20)->visibility.entranceTime==0,"External preparation delay cannot consume entrance time");session.close(20);
    check(!session.releaseOpening(token,20)&&!session.pendingOpening(),"Closing invalidates the outstanding opening token");session.conceal(20);
    const auto opening=session.open(21,9);env.pointer=Point{0,0};session.setEnvironment(env,21);session.setInputEnabled(true,21);
    const auto cameraStart=session.sample(21)->camera.worldRoot;check(!session.activate("button",21),"Actions remain disabled during the authored opening phase");
    check(session.sample(21.1)->camera.worldRoot!=cameraStart,"Pointer tilt remains active while opening");session.sample(22);check(session.phase()==VisibilityPhase::visible&&session.completedTransition()==opening,"Opening completion is observed once on its current generation");
    env.pointer.reset();env.pointerLocked=true;session.setEnvironment(env,22);settings.reduceMotion=true;session.setSettings(settings,22);session.sample(22);settings.reduceMotion=false;session.setSettings(settings,22);session.sample(22);
    const auto*frame=session.currentFrame();check(frame&&frame->sourceFrame->hits.size()==1,"Pure session builds original source hit geometry");
    for(const auto&node:frame->sourceFrame->resolved)if(node.node->id=="reddot"||node.node->id=="lock")check(!node.activeInHierarchy,"Desktop availability suppresses source account locks and unread decoration");
    const Point center{640,400};session.pointerDown(center,23);check(session.pressed()=="button","Press records the resolved original source button");
    const auto activation=session.pointerUp(center,23);check(activation&&activation->action==7&&activation->buttonID=="button","Release dispatches only matching source press/release");
    check(!session.activate("button",23.2)&&session.activate("button",23.2001).has_value(),"Original click cooldown requires strict greater-than at the endpoint");
    session.pointerDown(center,24);navigation.fixedActions["button"]=8;session.setNavigation(navigation,24);check(!session.pressed()&&!session.pointerUp(center,24),"Rebinding cancels an in-flight press before the new action becomes eligible");
    session.sample(24.5);check(session.activate("button",24.5)->action==8,"Rebound action is available after a new activation");
    session.setHitFilter([](std::string_view,Point){return false;},25);session.pointerMove(center,25);check(!session.hovered(),"Caller source cutout policy filters native pointer ownership");session.setHitFilter({},25);
    session.pointerMove({},26);session.sample(27);session.sample(28);const auto stable=session.frameStats();const auto pose=session.stats().fullPoses;
    allocations=0;countAllocations=true;for(unsigned i=0;i<120;++i)session.sample(29+i*.01);countAllocations=false;
    check(allocations==0&&session.stats().fullPoses==pose&&session.frameStats().localImageBuilds==stable.localImageBuilds,"Steady ambient-off frames retain pose, local geometry and heap storage");
    check(!FrameDemandGate{}.refresh(session.demand(31)).timerInterval,"Stable ambient-off session owns no animation wake");
    env.pointerLocked=false;env.pointer=Point{1270,790};session.setEnvironment(env,32);session.sample(32);const auto geometry=session.frameStats().localImageBuilds;
    session.sample(32.1);check(session.frameStats().localImageBuilds==geometry&&session.frameStats().worldOnlyFrames>0,"Pointer tilt reuses local image geometry");
    session.close(33);const auto closing=session.generation();env.pointer=Point{0,0};session.setEnvironment(env,33);const auto exitCamera=session.sample(33)->camera.worldRoot;
    check(session.sample(33.1)->camera.worldRoot!=exitCamera&&!session.activate("button",33.1),"Closing keeps tilt but gates activation");
    check(!session.sample(34)&&session.phase()==VisibilityPhase::concealed&&session.completedTransition()==closing&&!session.currentFrame(),"Close completion conceals and releases frame visibility without another build");
    const auto cancelled=session.open(35,1,true);session.conceal(35);session.open(36,2,true);check(!session.releaseOpening(cancelled,100),"Late completion cannot consume time or affect a newer opening");check(session.releaseOpening(session.generation(),36),"Current opening releases on the caller's clock");
    settings.reduceMotion=true;session.setSettings(settings,36);session.sample(36);check(session.phase()==VisibilityPhase::visible&&!FrameDemandGate{}.refresh(session.demand(36)).timerInterval,"Reduced motion completes source opening without a periodic wake");
    rejects([&]{auto invalid=env;invalid.viewport[0]=std::numeric_limits<double>::infinity();session.setEnvironment(invalid,36);},"Nonfinite host geometry rejects before presentation");
    SourceDesktopFrameSettings broken;broken.sprites["button"]="missing-source-sprite";session.setDesktopSettings(broken);
    rejects([&]{session.sample(37);},"A missing explicit source asset reports the original builder error");
    check(session.phase()==VisibilityPhase::concealed&&!session.currentFrame()&&!FrameDemandGate{}.refresh(session.demand(37)).timerInterval,"A failed frame cancels its lifetime and cannot leave a repeating error wake");
    auto disabledDocument=data.doc;disabledDocument.components["button"][1].data["m_Enabled"]=false;
    SourceWatchSession disabled(data.scene,disabledDocument,data.library,data.camera,data.resources,{},emptyTransitions());disabled.setEnvironment(environment(),0);disabled.setSettings(settings,0);disabled.setNavigation(navigation,0);disabled.showStable(0,0);disabled.setInputEnabled(true,0);disabled.sample(0);
    check(!disabled.activate("button",0),"A source-disabled UIButton never enters the click eligibility snapshot");
}
void compare(const WatchSessionFrame&frame,const packet::FrameData&oracle){
    const auto&value=oracle.metadata;const auto&actual=*frame.sourceFrame;check(actual.batches.size()==value["batches"].array().size(),"Session matches original snapshot batch count");
    for(std::size_t i=0;i<actual.batches.size();++i){const auto&a=actual.batches[i];const auto&b=value["batches"].array()[i];check(a.sourceNodeID==b["sourceNodeID"].string()&&a.sourceMesh==b["sourceMesh"].string()&&a.material==b["material"].string(),"Session keeps original source batch order/material binding");
        for(unsigned c=0;c<4;++c){checkNear(a.color[c],b["color"].array()[c].number(),2e-6,"Session source vertex tint");for(unsigned r=0;r<4;++r)checkNear(a.world.values[c*4+r],b["worldMatrix"].array()[c].array()[r].number(),2e-6,"Session batch world");}
        check(a.uniformOverrides.size()==b["uniformOverrides"].object().size(),"Session original material override count");for(const auto&[key,values]:a.uniformOverrides){check(values.size()==b["uniformOverrides"][key].array().size(),"Session override vector width");for(std::size_t c=0;c<values.size();++c)checkNear(values[c],b["uniformOverrides"][key].array()[c].number(),2e-6,"Session original override");}
    }
    check(actual.hits.size()==value["hits"].array().size(),"Session original hit count");
    for(const auto&expected:value["nodes"].array()){const auto id=expected["id"].string();const auto n=std::find_if(actual.resolved.begin(),actual.resolved.end(),[&](const auto&node){return node.node->id==id;});check(n!=actual.resolved.end()&&n->activeInHierarchy==expected["active"].boolean(),"Session original resolved visibility");
        for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)checkNear(n->worldMatrix.values[c*4+r],expected["worldMatrix"].array()[c].array()[r].number(),1e-10,"Session original resolved world");}
}
void actual(const std::filesystem::path&path,const std::filesystem::path&runtimeRoot={}){
    packet::Package package(std::filesystem::absolute(path));const auto metadata=package.loadAnimation();const auto referenceScene=SceneDefinition::fromJson(metadata["scene"]);const auto referenceDocument=MountedLayoutDocument::fromJson(metadata["mountedDocument"]);
    const auto referenceLibrary=Library::fromJson(metadata["library"]);const SourceCamera referenceCamera(metadata["runtimeRoot"]);const auto referenceResources=SourceWatchFrameResources::fromJson(metadata["frameBuilder"]);
    const auto referenceAnimators=AnimatorBinding::fromJson(metadata["mountedDocument"]["animators"]);const auto referenceProfile=DesktopHoverProfile::fromJson(metadata["frameBuilder"]["profileHover"]);
    std::unique_ptr<WatchRuntimeInput> runtime;if(!runtimeRoot.empty())runtime=std::make_unique<WatchRuntimeInput>(std::filesystem::absolute(runtimeRoot));
    const auto&scene=runtime?runtime->scene():referenceScene;const auto&document=runtime?runtime->document():referenceDocument;const auto&library=runtime?runtime->library():referenceLibrary;const auto&camera=runtime?runtime->camera():referenceCamera;const auto&resources=runtime?runtime->resources():referenceResources;
    const auto&animators=runtime?runtime->animators():referenceAnimators;const auto&profile=runtime?runtime->profile():referenceProfile;const auto&transitions=runtime?runtime->controllerTransitions():metadata["controllerTransitions"];unsigned frames{};
    for(const auto&size:{std::pair{"1280x800",Vec2{1280,800}},std::pair{"1920x1080",Vec2{1920,1080}}}){
        const auto stable=package.loadFrame(std::string("desktop-shell-")+size.first+"-opening-4");const auto&input=stable.metadata["builderInput"];const auto desktop=runtime?runtime->desktopSettings():SourceDesktopFrameSettings::fromJson(input["desktopSettings"]);
        SourceWatchSession session(scene,document,library,camera,resources,animators,transitions,profile,desktop);
        auto env=environment();env.viewport=size.second;env.pointerLocked=true;session.setEnvironment(env,0);WatchSessionSettings settings;settings.reduceMotion=true;settings.ambientEnabled=false;session.setSettings(settings,0);
        // Supplying the recorded logical count does not invent an OS action.
        WatchSessionNavigation nav;for(std::size_t i=0;i<std::min<std::size_t>(4,document.buttons.size());++i)nav.fixedActions[document.buttons[i].nodeID]=i;
        for(std::int64_t i=0;i<input["entryCount"].integer();++i)nav.rightActions.push_back(static_cast<std::uint64_t>(i+4));session.setNavigation(nav,0);
        session.showStable(0,0x5eed);compare(*session.sample(0),stable);++frames;
        {
            SourceWatchSession opening(scene,document,library,camera,resources,animators,transitions,profile,desktop);
            opening.setEnvironment(env,0);auto motion=settings;motion.reduceMotion=false;opening.setSettings(motion,0);opening.setNavigation(nav,0);opening.open(0,0x5eed);
            for(unsigned i=0;i<5;++i){const auto oracle=package.loadFrame(std::string("desktop-shell-")+size.first+"-opening-"+std::to_string(i));
                compare(*opening.sample(double(i)*metadata["playback"]["openingDuration"].number()/4),oracle);++frames;}
        }
        settings.reduceMotion=false;session.setSettings(settings,0);session.close(0);
        for(unsigned i=0;i<4;++i){const auto oracle=package.loadFrame(std::string("desktop-shell-")+size.first+"-closing-"+std::to_string(i));const double time=double(i)*metadata["playback"]["closingDuration"].number()/4;
            compare(*session.sample(time),oracle);++frames;}
        session.conceal(1);settings.ambientEnabled=true;session.setSettings(settings,1);session.showStable(1,0x5eed);session.setScrollPosition(.37,1);
        const double elapsed[]{0,.371,1.113};for(unsigned i=0;i<3;++i){const auto oracle=package.loadFrame(std::string("desktop-shell-")+size.first+"-ambient-"+std::to_string(i));compare(*session.sample(1+elapsed[i]),oracle);++frames;}
        session.sample(3);const auto statistics=session.frameStats();const auto poses=session.stats().fullPoses;allocations=0;countAllocations=true;for(unsigned i=0;i<120;++i)session.sample(3.1+i/30.);countAllocations=false;
        check(allocations==0&&session.stats().fullPoses==poses,"Actual session ambient loop performs no wrapper/button pose allocation");
        check(session.frameStats().layoutBuilds==statistics.layoutBuilds&&session.frameStats().localImageBuilds==statistics.localImageBuilds&&session.frameStats().directAmbientFrames>=statistics.directAmbientFrames+120,"Actual session retains layout/topology and applies the 29-node ambient path");
        settings.ambientEnabled=false;session.setSettings(settings,10);env.pointerLocked=false;env.pointer=Point{-10,10};session.setEnvironment(env,10);session.sample(10);session.sample(10.4);
        const auto pointerStats=session.frameStats();const auto pointerPoses=session.stats().fullPoses;allocations=0;countAllocations=true;
        for(unsigned i=0;i<120;++i){session.pointerMove(Point{-10,20.+i*4},11.+i/60.);session.sample(11.+i/60.);}countAllocations=false;
        if(allocations||session.stats().fullPoses!=pointerPoses)std::cerr<<"Actual pointer allocations="<<allocations<<" full poses="<<session.stats().fullPoses-pointerPoses<<" world-only="<<session.frameStats().worldOnlyFrames-pointerStats.worldOnlyFrames<<'\n';
        check(allocations==0&&session.stats().fullPoses==pointerPoses,"Actual changing pointer frames reuse the settled pose with no heap allocations");
        check(session.frameStats().layoutBuilds==pointerStats.layoutBuilds&&session.frameStats().localImageBuilds==pointerStats.localImageBuilds&&session.frameStats().worldOnlyFrames>pointerStats.worldOnlyFrames,"Actual pointer motion retains layout and local image topology");
        env.pointerLocked=true;env.pointer.reset();session.setEnvironment(env,20);session.setInputEnabled(true,20);session.setScrollPosition(.5,20);session.sample(20);
        const auto*scrollFrame=session.currentFrame();const auto scrollInfo=*scrollFrame->sourceFrame->layoutReport.scroll;
        const ResolvedNode*scrollNode=nullptr;for(const auto&node:scrollFrame->sourceFrame->resolved)if(node.node->id==scrollInfo.viewportID)scrollNode=&node;
        check(scrollNode&&scrollNode->rect,"Navigation viewport is available for native input checks");
        const auto plane=Projection::viewport(scrollFrame->camera.projection*scrollFrame->camera.view*scrollFrame->camera.worldRoot*scrollNode->worldMatrix,env.viewport[0],env.viewport[1]);
        const auto scrollPoint=plane.project({scrollNode->rect->origin[0]+scrollNode->rect->size[0]*.5,scrollNode->rect->origin[1]+scrollNode->rect->size[1]*.5});
        check(scrollPoint.has_value(),"Navigation plane projects into the owned viewport");
        check(!session.wheel(*scrollPoint,-1,0,20)&&session.scrollPosition()==.5,"Windows disabled wheel setting does not scroll");
        check(session.wheel(*scrollPoint,-1,3,20),"Windows wheel accepts configured line count");session.sample(21);const double threeLineTravel=.5-session.scrollPosition();
        const auto edgeTop=plane.project({scrollNode->rect->origin[0],scrollNode->rect->origin[1]}),edgeBottom=plane.project({scrollNode->rect->origin[0],scrollNode->rect->origin[1]+scrollNode->rect->size[1]});
        check(edgeTop&&edgeBottom,"Wheel travel measures its actual tilted viewport");
        const double units=scrollNode->rect->size[1]/std::max(1.,std::hypot(edgeBottom->x-edgeTop->x,edgeBottom->y-edgeTop->y));
        checkNear(threeLineTravel*scrollInfo.hiddenLength/units,60,1e-4,"Default wheel notch now travels sixty screen points while retaining original spring timing");
        session.setScrollPosition(.5,21);session.sample(21);session.wheel(*scrollPoint,-1,1,21);session.sample(22);
        checkNear(threeLineTravel,3*(.5-session.scrollPosition()),1e-6,"System line count scales wheel movement without changing spring timing");
        session.setScrollPosition(.5,22);session.sample(22);session.wheel(*scrollPoint,-1,UINT32_MAX,22);session.sample(23);
        check(.5-session.scrollPosition()>threeLineTravel,"Windows page-scroll setting advances farther than three lines");
        session.setScrollPosition(.5,23);session.sample(23);session.pointerDown(*scrollPoint,23);
        check(session.navigationPointerActive()&&!session.navigationDragging(),"Press inside navigation starts a drag candidate");
        session.pointerMove(Point{scrollPoint->x,scrollPoint->y-2},23.01);check(!session.navigationDragging(),"Small click jitter does not become a drag");
        const Point draggedPoint{scrollPoint->x,scrollPoint->y-90};session.pointerMove(draggedPoint,23.02);session.sample(23.02);
        check(session.navigationDragging()&&!session.pressed()&&session.scrollPosition()<.5,"Dragging upward follows the original plane and cancels pressed button");
        const auto heldPosition=session.scrollPosition();check(session.wheel(draggedPoint,-1,3,23.1)&&session.navigationDragging()&&session.scrollPosition()==heldPosition,"Captured drag consumes wheel without starting an unfinished spring");
        session.sample(23.5);check(!FrameDemandGate{}.refresh(session.demand(23.5)).timerInterval,"Stationary pointer drag needs only input-driven frames when ambient motion is off");
        check(!session.pointerUp(draggedPoint,23.51)&&!session.navigationPointerActive(),"Releasing a navigation drag cannot activate a recycled button");
        session.pointerDown(*scrollPoint,24);session.pointerMove(Point{scrollPoint->x,scrollPoint->y+6000},24.01);session.sample(24.01);
        check(session.scrollPosition()<=1+DesktopScrollMotion::gestureEdgeTravel/scrollInfo.hiddenLength,"Dragging overscroll remains within original bounce extent");
        session.setInputEnabled(false,24.02);check(!session.navigationPointerActive(),"Focus/input loss cancels captured navigation drag");session.sample(26);
        check(session.scrollPosition()>=0&&session.scrollPosition()<=1,"Released overscroll settles within navigation limits");

    }
    std::cout<<frames<<" original macOS session checkpoints compared\n";
}
}
int main(int argc,char**argv){try{synthetic();check(argc<=3,"Pass original packet and optional compact runtime input");if(argc>=2)actual(argv[1],argc==3?std::filesystem::path(argv[2]):std::filesystem::path{});std::cout<<"PASS "<<checks<<" source Watch session checks\n";return 0;}catch(const std::exception&error){countAllocations=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
