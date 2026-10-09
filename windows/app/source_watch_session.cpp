#include "app/source_watch_session.hpp"
#include "core/source_native_labels.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::app {
using namespace core;
using namespace source;
namespace {
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
bool flag(const Json&v,bool fallback){return v.isBool()?v.boolean():v.isNumber()?v.number()!=0:fallback;}
bool valid(const WatchSessionSettings&s){return std::isfinite(s.parallax)&&std::isfinite(s.perspective)&&std::isfinite(s.hudScale)&&s.hudScale>0&&std::isfinite(s.hudOffset[0])&&std::isfinite(s.hudOffset[1]);}
bool valid(const WatchSessionEnvironment&e){return std::isfinite(e.viewport[0])&&std::isfinite(e.viewport[1])&&e.viewport[0]>=0&&e.viewport[1]>=0&&(!e.pointer||finite(*e.pointer));}
std::string_view trim(std::string_view value){constexpr std::string_view whitespace=" \t\n\r\f\v";const auto start=value.find_first_not_of(whitespace);if(start==value.npos)return {};return value.substr(start,value.find_last_not_of(whitespace)-start+1);}
bool redDot(std::string_view name){if(name.size()<6)return false;name.remove_prefix(name.size()-6);constexpr std::string_view target="reddot";for(unsigned i=0;i<6;++i){const auto c=static_cast<unsigned char>(name[i]);if((c>='A'&&c<='Z'?c+32:c)!=static_cast<unsigned char>(target[i]))return false;}return true;}
}
struct SourceWatchSession::Impl {
    const SceneDefinition& scene;const MountedLayoutDocument& document;const SourceCamera& cameraModel;const SourceWatchFrameResources& resources;
    WatchAnimation animation;VisibilityClock playback;DesktopAmbientMotion ambient;GyroMotion gyro;ButtonAnimation buttons;SelectableColor selectable;DesktopHoverFeedback hoverFeedback;
    SourceWatchFrameBuilder builder;SourceDesktopFrameSettings baseSettings,appliedSettings;WatchSessionSettings settings;WatchSessionEnvironment environment;
    WatchSessionNavigation navigationInput;std::unique_ptr<DesktopNavigationLayout> navigation;bool hasNavigation{};
    std::map<std::string,std::uint64_t,std::less<>> actions;
    struct Click {bool interactable{};double cooldown{};std::optional<double> accepted;};
    std::map<std::string_view,Click,std::less<>> clicks;
    std::vector<std::pair<std::string_view,std::string_view>> animatorButtons;
    std::vector<std::string_view> availableButtons,hiddenDecorations;
    DesktopScrollMotion scrollMotion;double scrollPosition{1};std::optional<double> bindingPosition;
    struct NavigationDrag {Point start;Projection plane;double lastLocalY{},hiddenLength{};bool active{};};
    std::optional<NavigationDrag> navigationDrag;
    void cancelDrag(double time){if(navigationDrag&&navigationDrag->active){scrollMotion.gesture(0,navigationDrag->hiddenLength,time,GesturePhase::ended,GesturePhase::none,settings.reduceMotion);scrollPosition=scrollMotion.position();}navigationDrag.reset();}

    bool input{},hidden{true};std::optional<double> clock,heldTime;
    std::uint64_t lifecycleToken{},poseGeneration{},buttonGeneration{};std::optional<std::uint64_t> completion;std::optional<Vec2> poseCanvas;
    std::optional<Pose> finalPose;Pose ambientPose;
    WatchSessionFrame rendered;bool hasFrame{};
    std::optional<std::string_view> hovered,pressed;
    struct HitQuery {std::optional<Point> point;std::uint64_t revision{};bool interactive{};};
    std::optional<HitQuery> lastHit;
    HitFilter filter;WatchSessionStats stats;SourceWatchFrame scrollProbe;

    Impl(const SceneDefinition&s,const MountedLayoutDocument&d,const Library&library,const SourceCamera&c,const SourceWatchFrameResources&r,
        std::span<const AnimatorBinding> bindings,const Json&transitions,std::optional<DesktopHoverProfile> profile,SourceDesktopFrameSettings desktop)
        :scene(s),document(d),cameraModel(c),resources(r),animation(s,library),playback(animation.entrance().lastKeyTime,animation.exit().lastKeyTime),ambient(animation,0),gyro(c.rootRotation()),
        buttons(s,library,bindings,transitions),selectable(s,d),hoverFeedback(s,d,selectable,std::move(profile)),builder(s,d,r,desktop),baseSettings(std::move(desktop)),appliedSettings(baseSettings){
        std::set<std::string_view,std::less<>> main;
        for(const auto& b:document.buttons){const auto*n=scene.node(b.nodeID);need(n,"Missing source main-button node");main.insert(n->id);availableButtons.push_back(n->id);}
        for(const auto& n:scene.nodes()){
            if(const auto* component=document.component("UIButton",n.id)){const auto&raw=(*component)["_clickCd"];const double cooldown=raw.isNumber()?raw.number():0;
                clicks.emplace(n.id,Click{flag((*component)["m_Interactable"],true),cooldown,{}});}
            const auto name=trim(n.name);bool hide=redDot(name);
            if(!hide&&(name=="LockIcon"||name=="SafeZoneIcon")){auto ancestor=n.parent;while(ancestor&&!main.contains(*ancestor))ancestor=scene.node(*ancestor)->parent;hide=ancestor.has_value();}
            if(hide)hiddenDecorations.push_back(n.id);
        }
        for(const auto& binding:bindings){const Node*n=scene.node(binding.rootID);need(n,"Missing source animator node");while(n&&!document.component("UIButton",n->id))n=n->parent?scene.node(*n->parent):nullptr;
            if(n)animatorButtons.emplace_back(scene.node(binding.rootID)->id,n->id);}
        for(const auto&channel:ambient.channels())ambientPose.transforms[channel.node].localRotation=channel.base;
        scrollProbe.hits.resize(1);scrollProbe.hits[0].buttonID="viewport";
        for(const auto&id:hoverFeedback.sideEdgeIDs())appliedSettings.graphicStyles[id].opacity=DesktopHoverFeedback::sideEdgeOpacity;
        // Material substitution for the profile highlight comes from the exact
        // exported desktop settings, never a guessed native material.
        builder.setDesktopSettings(appliedSettings);
    }
    std::optional<double> advance(double time){if(!std::isfinite(time))return {};clock=std::max(clock.value_or(time),time);return clock;}
    bool usesAmbient()const{return settings.ambientEnabled&&!settings.lowPower&&!settings.reduceMotion;}
    Rect viewport()const{return {0,0,environment.viewport[0],environment.viewport[1]};}
    void invalidatePose(){poseCanvas.reset();lastHit.reset();}
    void clearClicks(){for(auto&[id,value]:clicks){(void)id;value.accepted.reset();}}
    void updateStates(double time){
        invalidatePose();const auto tintHovered=hoverFeedback.groupedButton(hovered),tintPressed=hoverFeedback.groupedButton(pressed);
        for(const auto& binding:selectable.bindings()){
            const auto desired=!binding.sourceInteractable?SelectableState::disabled:tintPressed==binding.buttonNodeID?SelectableState::pressed:tintHovered==binding.buttonNodeID?SelectableState::highlighted:SelectableState::normal;
            if(selectable.state(binding.buttonNodeID)!=desired)selectable.setState(desired,binding.buttonNodeID,time,settings.reduceMotion);
        }
        for(const auto&[root,button]:animatorButtons){if(buttons.state(root)==ButtonState::disabled)continue;
            buttons.setState(pressed==button?ButtonState::pressed:hovered==button?ButtonState::highlighted:ButtonState::normal,root,time,settings.reduceMotion);
            buttons.setHovered(hovered==button,root,time,settings.reduceMotion);}
    }
    void cancelInput(double time,bool reset){pressed.reset();hovered.reset();clearClicks();if(reset){buttons.reset(time,settings.reduceMotion);selectable.reset(time);}updateStates(time);}
    void refreshStyles(const SelectableTints&tints){
        bool changed=false;for(const auto&[id,opacity]:hoverFeedback.opacities(tints)){auto&style=appliedSettings.graphicStyles[id];if(style.opacity!=opacity){style.opacity=opacity;changed=true;}}
        if(changed)builder.setDesktopSettings(appliedSettings);
    }
    void refreshBindings(double time,bool force=false){
        if(!hasNavigation||(!force&&bindingPosition==scrollPosition))return;bindingPosition=scrollPosition;
        auto next=navigationInput.fixedActions;
        if(navigation){const auto sample=navigation->sample(scrollPosition);for(const auto&[id,index]:sample.assignments){need(index<navigationInput.rightActions.size(),"Source navigation assignment exceeds supplied actions");next[id]=navigationInput.rightActions[index];}}
        if(!force&&next==actions)return;
        actions=std::move(next);++stats.bindingChanges;cancelInput(time,true);
        appliedSettings.hiddenNodes=baseSettings.hiddenNodes;
        for(const auto&id:navigationInput.managedButtons){if(actions.contains(id))appliedSettings.hiddenNodes.erase(id);else appliedSettings.hiddenNodes.insert(id);}
        builder.setDesktopSettings(appliedSettings);
    }
    std::optional<std::string_view> hit(Point point,bool onScreenRequired=false){
        if(!input||hidden||playback.phase()!=VisibilityPhase::visible||!hasFrame||(onScreenRequired&&!environment.onScreen)||!viewport().contains(point))return {};
        ++stats.hitQueries;const auto hit=rendered.sourceFrame->buttonAt(point,rendered.camera.projection*rendered.camera.view,viewport());
        if(!hit||(filter&&!filter(*hit,point)))return {};const auto*n=scene.node(*hit);return n?std::optional<std::string_view>(n->id):std::nullopt;
    }
    const ResolvedNode* resolved(std::string_view id)const{if(!hasFrame)return nullptr;for(const auto&n:rendered.sourceFrame->resolved)if(n.node->id==id)return &n;return nullptr;}
    std::optional<WatchActivation> perform(std::string_view id,double time,std::optional<Point> point){
        if(!input||hidden||playback.phase()!=VisibilityPhase::visible||!hasFrame)return {};
        const auto*node=resolved(id);const auto click=clicks.find(id);const auto action=actions.find(id);
        if(!node||!node->activeInHierarchy||click==clicks.end()||!click->second.interactable||action==actions.end())return {};
        const auto vp=rendered.camera.projection*rendered.camera.view;bool eligible=false;
        if(point)eligible=viewport().contains(*point)&&rendered.sourceFrame->buttonAt(*point,vp,viewport())==id;
        else for(const auto&h:rendered.sourceFrame->hits)if(h.buttonID==id){
            constexpr Vec2 probes[]{{.5,.5},{.25,.25},{.75,.25},{.25,.75},{.75,.75}};
            for(const auto&probe:probes){const auto p=projectNativePoint({h.rect.origin[0]+h.rect.size[0]*probe[0],h.rect.origin[1]+h.rect.size[1]*probe[1],0},h.world,vp,viewport());
                if(p&&viewport().contains(*p)&&rendered.sourceFrame->buttonAt(*p,vp,viewport())==id){eligible=true;break;}}
            if(eligible)break;
        }
        auto&state=click->second;if(!eligible||!std::isfinite(state.cooldown)||state.cooldown<0||(state.accepted&&!(time>*state.accepted+state.cooldown)))return {};
        state.accepted=time;return WatchActivation{node->node->id,action->second};
    }
    double unitsPerPoint(const ResolvedNode&node,const SourceRect&rect)const{
        const auto vp=rendered.camera.projection*rendered.camera.view,world=rendered.camera.worldRoot*node.worldMatrix;
        const auto first=projectNativePoint({rect.origin[0],rect.origin[1],0},world,vp,viewport()),last=projectNativePoint({rect.origin[0],rect.origin[1]+rect.size[1],0},world,vp,viewport());
        return first&&last?rect.size[1]/std::max(1.,std::hypot(last->x-first->x,last->y-first->y)):1;
    }
    bool insidePlane(Point point,const ResolvedNode&node,const SourceRect&rect){
        auto&h=scrollProbe.hits[0];h.rect=rect;h.world=rendered.camera.worldRoot*node.worldMatrix;
        return scrollProbe.buttonAt(point,rendered.camera.projection*rendered.camera.view,viewport()).has_value();
    }
    void applyAvailability(Pose&pose)const{for(const auto&id:availableButtons)pose.transforms[std::string(id)].active=true;for(const auto&id:hiddenDecorations)pose.transforms[std::string(id)].active=false;}
    bool makePose(const VisibilitySample&sample,Vec2 canvas,double time,bool reduce,bool settled){
        if(settled&&finalPose&&poseCanvas==canvas&&poseGeneration==playback.generation()&&buttonGeneration==buttons.stateGeneration()){
            ++stats.retainedPoses;if(sample.ambientTime){ambient.apply(*sample.ambientTime,*finalPose);++stats.ambientPatches;}return true;
        }
        finalPose=animation.pose(sample,canvas,{},&ambient);buttons.apply(*finalPose,time,reduce);applyAvailability(*finalPose);++stats.fullPoses;
        if(settled){poseCanvas=canvas;poseGeneration=playback.generation();buttonGeneration=buttons.stateGeneration();}else poseCanvas.reset();return false;
    }
};
SourceWatchSession::SourceWatchSession(const SceneDefinition&s,const MountedLayoutDocument&d,const Library&l,const SourceCamera&c,const SourceWatchFrameResources&r,
    std::span<const AnimatorBinding>b,const Json&t,std::optional<DesktopHoverProfile>p,SourceDesktopFrameSettings settings)
    :impl_(std::make_unique<Impl>(s,d,l,c,r,b,t,std::move(p),std::move(settings))){}
SourceWatchSession::~SourceWatchSession()=default;
void SourceWatchSession::setSettings(WatchSessionSettings value,double time){need(valid(value),"Invalid Watch session settings");auto&p=*impl_;const auto at=p.advance(time);if(!at||p.settings==value)return;p.settings=value;
    if(value.reduceMotion){p.scrollMotion.reset(p.scrollMotion.target(),*at);p.scrollPosition=p.scrollMotion.position();p.gyro.retarget({},*at,p.cameraModel.gyro().duration,true);}p.invalidatePose();}
void SourceWatchSession::setEnvironment(WatchSessionEnvironment value,double time){need(valid(value),"Invalid Watch presentation environment");auto&p=*impl_;if(!p.advance(time)||p.environment==value)return;if(p.environment.viewport!=value.viewport){p.cancelDrag(*p.clock);p.invalidatePose();}p.environment=value;}
void SourceWatchSession::setInputEnabled(bool value,double time){auto&p=*impl_;const auto at=p.advance(time);if(!at||p.input==value)return;p.input=value;if(!value){p.cancelDrag(*at);p.hovered.reset();p.pressed.reset();p.updateStates(*at);}p.lastHit.reset();}
void SourceWatchSession::setDesktopSettings(SourceDesktopFrameSettings value){auto&p=*impl_;p.baseSettings=value;p.appliedSettings=std::move(value);for(const auto&id:p.hoverFeedback.sideEdgeIDs())p.appliedSettings.graphicStyles[id].opacity=DesktopHoverFeedback::sideEdgeOpacity;
    for(const auto&id:p.navigationInput.managedButtons){if(p.actions.contains(id))p.appliedSettings.hiddenNodes.erase(id);else p.appliedSettings.hiddenNodes.insert(id);}p.builder.setDesktopSettings(p.appliedSettings);p.lastHit.reset();}
void SourceWatchSession::setNavigation(WatchSessionNavigation value,double time){auto&p=*impl_;const auto at=p.advance(time);if(!at||(p.hasNavigation&&p.navigationInput==value))return;
    for(const auto&[id,action]:value.fixedActions){(void)action;need(p.scene.node(id),"Navigation action node is absent");}for(const auto&id:value.managedButtons)need(p.scene.node(id),"Managed source button is absent");
    std::unique_ptr<DesktopNavigationLayout> navigation;if(!value.rightActions.empty())navigation=std::make_unique<DesktopNavigationLayout>(p.scene,p.document,static_cast<std::int64_t>(value.rightActions.size()));
    if(value.rightActions.size()!=p.navigationInput.rightActions.size()){p.scrollPosition=1;p.scrollMotion.reset(1,*at);}
    p.cancelDrag(*at);p.navigationInput=std::move(value);p.navigation=std::move(navigation);p.hasNavigation=true;p.refreshBindings(*at,true);
}
void SourceWatchSession::setHitFilter(HitFilter filter,double time){auto&p=*impl_;const auto at=p.advance(time);if(!at)return;p.filter=std::move(filter);p.hovered.reset();p.pressed.reset();p.updateStates(*at);}
std::uint64_t SourceWatchSession::open(double time,std::uint64_t seed,bool held){auto&p=*impl_;const auto at=p.advance(time);if(!at)return p.lifecycleToken;
    p.cancelDrag(*at);++p.lifecycleToken;p.completion.reset();p.hidden=false;p.heldTime=held?at:std::nullopt;p.ambient=DesktopAmbientMotion(p.animation,seed);p.selectable.reset(*at);p.hovered.reset();p.pressed.reset();p.invalidatePose();
    if(!held){p.buttons.reset(*at,p.settings.reduceMotion);p.playback.open(*at,p.settings.reduceMotion);p.updateStates(*at);if(p.settings.reduceMotion)p.completion=p.lifecycleToken;}else p.playback.open(*at,false);return p.lifecycleToken;
}
bool SourceWatchSession::releaseOpening(std::uint64_t token,double time){auto&p=*impl_;if(token!=p.lifecycleToken||!p.heldTime||p.hidden||p.playback.phase()!=VisibilityPhase::opening||!std::isfinite(time))return false;const auto at=*p.advance(time);p.heldTime.reset();p.buttons.reset(at,p.settings.reduceMotion);p.playback.open(at,p.settings.reduceMotion);p.updateStates(at);if(p.settings.reduceMotion)p.completion=p.lifecycleToken;return true;}
void SourceWatchSession::showStable(double time,std::uint64_t seed){auto&p=*impl_;const auto at=p.advance(time);if(!at)return;++p.lifecycleToken;p.completion.reset();if(p.playback.phase()==VisibilityPhase::concealed){p.ambient=DesktopAmbientMotion(p.animation,seed);p.selectable.reset(*at);}p.hidden=false;p.heldTime.reset();p.playback.showStable(*at);p.invalidatePose();}
void SourceWatchSession::close(double time){auto&p=*impl_;const auto at=p.advance(time);if(!at)return;++p.lifecycleToken;p.completion.reset();p.heldTime.reset();p.scrollMotion.reset(p.scrollPosition,*at);p.scrollPosition=p.scrollMotion.position();setInputEnabled(false,*at);p.playback.close(*at,p.settings.reduceMotion);p.invalidatePose();if(p.playback.phase()==VisibilityPhase::concealed){p.hidden=true;p.hasFrame=false;p.completion=p.lifecycleToken;}}
void SourceWatchSession::conceal(double time){auto&p=*impl_;const auto at=p.advance(time);if(!at)return;++p.lifecycleToken;p.completion.reset();p.heldTime.reset();p.scrollMotion.reset(p.scrollPosition,*at);p.playback.conceal();p.navigationDrag.reset();p.input=false;p.hovered.reset();p.pressed.reset();p.hidden=true;p.gyro.stop(*at);p.hasFrame=false;p.finalPose.reset();p.invalidatePose();}
void SourceWatchSession::pointerMove(std::optional<Point> point,double time){
    need(!point||finite(*point),"Invalid Watch pointer");auto&p=*impl_;const auto at=p.advance(time);if(!at)return;p.environment.pointer=point;
    if(point&&p.navigationDrag){auto&drag=*p.navigationDrag;
        if(const auto local=drag.plane.unproject(*point)){
            if(!drag.active&&std::hypot(point->x-drag.start.x,point->y-drag.start.y)>=5){
                drag.active=true;p.pressed.reset();p.hovered.reset();p.updateStates(*at);
                p.scrollMotion.gesture(0,drag.hiddenLength,*at,GesturePhase::began,GesturePhase::none,p.settings.reduceMotion);
            }
            if(drag.active){p.scrollMotion.gesture(-(local->y-drag.lastLocalY)/drag.hiddenLength,drag.hiddenLength,*at,GesturePhase::changed,GesturePhase::none,p.settings.reduceMotion);p.scrollPosition=p.scrollMotion.position();drag.lastLocalY=local->y;return;}
        }
    }
    const auto next=point?p.hit(*point):std::nullopt;if(next!=p.hovered){p.hovered=next;p.updateStates(*at);}
}
void SourceWatchSession::pointerDown(Point point,double time){
    need(finite(point),"Invalid Watch pointer");auto&p=*impl_;const auto at=p.advance(time);if(!at)return;p.cancelDrag(*at);p.environment.pointer=point;p.pressed=p.hit(point);p.hovered=p.pressed;p.updateStates(*at);
    if(!p.input||p.hidden||p.playback.phase()!=VisibilityPhase::visible||!p.hasFrame)return;
    const auto&info=p.rendered.sourceFrame->layoutReport.scroll;if(!info||info->hiddenLength<=0)return;
    const auto*node=p.resolved(info->viewportID);if(!node||!node->rect||!p.insidePlane(point,*node,*node->rect))return;
    const auto plane=Projection::viewport(p.rendered.camera.projection*p.rendered.camera.view*p.rendered.camera.worldRoot*node->worldMatrix,p.environment.viewport[0],p.environment.viewport[1]);
    if(const auto local=plane.unproject(point))p.navigationDrag=Impl::NavigationDrag{point,plane,local->y,info->hiddenLength,false};
}
std::optional<WatchActivation> SourceWatchSession::pointerUp(Point point,double time){
    need(finite(point),"Invalid Watch pointer");auto&p=*impl_;const auto at=p.advance(time);if(!at)return {};
    pointerMove(point,*at);const bool dragged=p.navigationDrag&&p.navigationDrag->active;p.cancelDrag(*at);
    p.environment.pointer=point;const auto released=p.hit(point),down=p.pressed;p.pressed.reset();p.hovered=released;p.updateStates(*at);
    return !dragged&&down&&released==down?p.perform(*down,*at,point):std::nullopt;
}
bool SourceWatchSession::navigationPointerActive()const noexcept{return impl_->navigationDrag.has_value();}
bool SourceWatchSession::navigationDragging()const noexcept{return impl_->navigationDrag&&impl_->navigationDrag->active;}
bool SourceWatchSession::wheel(Point point,double steps,std::uint32_t lines,double time){
    need(finite(point)&&std::isfinite(steps),"Invalid native wheel input");auto&p=*impl_;
    if(lines==0||!p.hasFrame)return false;
    // A captured drag owns navigation until release. Mixing a wheel spring
    // into that direct gesture would disable its gesture state mid-drag.
    if(p.navigationDrag&&p.navigationDrag->active)return true;
    const auto&info=p.rendered.sourceFrame->layoutReport.scroll;if(!info||info->hiddenLength<=0)return false;
    const auto*node=p.resolved(info->viewportID);if(!node||!node->rect)return false;
    const double units=p.unitsPerPoint(*node,*node->rect),page=node->rect->size[1]/std::max(.0001,units);
    // Windows wheel notches need more travel than the source trackpad input.
    // Keep the original spring/edge behavior and scale only the OS line step.
    const double distance=lines==UINT32_MAX?page:std::min(page,double(lines)*20.);
    return scroll(point,steps*distance,true,GesturePhase::none,GesturePhase::none,time);
}
std::optional<WatchActivation> SourceWatchSession::activate(std::string_view id,double time){auto&p=*impl_;const auto at=p.advance(time);return at?p.perform(id,*at,{}):std::nullopt;}
bool SourceWatchSession::scroll(Point point,double delta,bool precise,GesturePhase phase,GesturePhase momentum,double time){need(finite(point)&&std::isfinite(delta),"Invalid Watch scroll input");auto&p=*impl_;const auto at=p.advance(time);if(!at||!p.input||p.playback.phase()!=VisibilityPhase::visible||!p.hasFrame)return false;
    const auto&info=p.rendered.sourceFrame->layoutReport.scroll;if(!info||!(info->hiddenLength>0))return false;const auto*node=p.resolved(info->viewportID);if(!node||!node->rect)return false;
    const bool inside=p.insidePlane(point,*node,*node->rect);if(!inside&&!p.scrollMotion.isGestureActive()&&!(p.scrollMotion.ownsMomentum()&&momentum!=GesturePhase::none))return false;
    if(phase==GesturePhase::began&&momentum==GesturePhase::none&&!inside){p.scrollMotion.reset(p.scrollPosition,*at);p.scrollPosition=p.scrollMotion.position();return false;}
    p.scrollMotion.gesture((precise?delta:delta*10)*p.unitsPerPoint(*node,*node->rect)/std::max(1.,info->hiddenLength),info->hiddenLength,*at,phase,momentum,p.settings.reduceMotion);p.scrollPosition=p.scrollMotion.position();return true;
}
bool SourceWatchSession::scrollDirection(int direction,bool animated,double time){auto&p=*impl_;const auto at=p.advance(time);if(!at||!p.input||p.playback.phase()!=VisibilityPhase::visible||!p.hasFrame)return false;const auto&info=p.rendered.sourceFrame->layoutReport.scroll;
    if(!info||!(info->hiddenLength>0)||!p.scrollMotion.canScroll(direction))return false;const auto*node=p.resolved(info->viewportID);if(!node||!node->rect)return false;
    p.scrollMotion.scroll(-double(direction)*32*p.unitsPerPoint(*node,*node->rect)/std::max(1.,info->hiddenLength),info->hiddenLength,*at,!animated||p.settings.reduceMotion);p.scrollPosition=p.scrollMotion.position();return true;
}
void SourceWatchSession::setScrollPosition(double value,double time){need(std::isfinite(value),"Invalid Watch scroll position");auto&p=*impl_;const auto at=p.advance(time);if(!at)return;p.scrollMotion.reset(value,*at);p.scrollPosition=p.scrollMotion.position();p.lastHit.reset();}
const WatchSessionFrame* SourceWatchSession::sample(double value){auto&p=*impl_;const auto at=p.advance(value);if(!at)return p.hasFrame?&p.rendered:nullptr;
    if(p.hidden||!p.environment.presented||p.playback.phase()==VisibilityPhase::concealed||p.environment.viewport[0]<=0||p.environment.viewport[1]<=0){++p.stats.concealedSamples;return nullptr;}
    try {
    const double time=p.heldTime.value_or(*at);const bool reduce=!p.heldTime&&p.settings.reduceMotion;
    if(p.playback.phase()==VisibilityPhase::visible)p.scrollPosition=p.scrollMotion.advance(time);
    if(p.environment.pointerLocked)p.gyro.stop(time);
    if(!reduce&&(p.environment.onScreen||p.environment.canAdvanceTransition)&&!p.environment.pointerLocked&&p.environment.pointer){const auto point=*p.environment.pointer;
        p.gyro.retarget(p.cameraModel.gyro().desktopTarget({point.x,point.y},p.environment.viewport,p.settings.parallax,p.settings.perspective),time,p.cameraModel.gyro().duration);}
    p.gyro.finishIfNeeded(time);const auto camera=p.cameraModel.desktopFrame(p.environment.viewport,p.gyro.rotation(time),p.settings.hudOffset,p.settings.hudScale);
    p.refreshBindings(time);const auto&tints=p.selectable.colors(time,reduce);p.refreshStyles(tints);
    const bool settled=p.playback.phase()==VisibilityPhase::visible&&!p.buttons.requiresFrames(time)&&!p.selectable.requiresFrames(time);
    const auto visibility=p.playback.sample(time,reduce,p.usesAmbient());if(visibility&&visibility->completedGeneration)p.completion=p.lifecycleToken;
    if(!visibility||visibility->phase==VisibilityPhase::concealed){p.hidden=true;p.hasFrame=false;p.input=false;return nullptr;}
    const bool retained=p.makePose(*visibility,camera.layout.canvasSize,time,reduce,settled);
    const SourceWatchFrame* frame=nullptr;
    if(retained&&visibility->ambientTime){p.ambient.apply(*visibility->ambientTime,p.ambientPose);frame=p.builder.buildSettledAmbient(p.ambientPose,p.builder.presentationRevision(),camera.worldRoot,camera.layout.canvasSize,p.scrollPosition,p.navigation.get(),tints);}
    if(!frame)frame=&p.builder.build(*p.finalPose,camera.worldRoot,p.scrollPosition,p.navigation.get(),tints);
    p.rendered={frame,camera,p.cameraModel.gpu(camera),p.usesAmbient()?static_cast<float>(time):0,*visibility};p.hasFrame=true;
    const bool interactive=p.input&&p.playback.phase()==VisibilityPhase::visible&&p.environment.onScreen;
    const auto revision=p.builder.presentationRevision();std::optional<std::string_view> next;
    if(p.lastHit&&p.lastHit->point==p.environment.pointer&&p.lastHit->revision==revision&&p.lastHit->interactive==interactive)next=p.hovered;
    else if(interactive&&p.environment.pointer&&!(p.navigationDrag&&p.navigationDrag->active))next=p.hit(*p.environment.pointer,true);
    if(next!=p.hovered){p.hovered=next;p.updateStates(time);const auto&updated=p.selectable.colors(time,reduce);p.refreshStyles(updated);
        p.makePose(*visibility,camera.layout.canvasSize,time,reduce,false);p.rendered.sourceFrame=&p.builder.build(*p.finalPose,camera.worldRoot,p.scrollPosition,p.navigation.get(),updated);++p.stats.hoverRebuilds;}
    p.lastHit=Impl::HitQuery{p.environment.pointer,p.builder.presentationRevision(),interactive};++p.stats.samples;return &p.rendered;
    }catch(...){conceal(*at);throw;}
}
FrameDemand SourceWatchSession::demand(double time){auto&p=*impl_;if(!std::isfinite(time)||p.hidden||!p.environment.presented||p.playback.phase()==VisibilityPhase::concealed)return {};const auto sampleTime=std::max(time,p.clock.value_or(time));const bool finite=p.playback.phase()!=VisibilityPhase::visible||p.gyro.isAnimating()||p.buttons.requiresFrames(sampleTime)||p.selectable.requiresFrames(sampleTime)||(p.scrollMotion.requiresFrames()&&!(p.navigationDrag&&p.navigationDrag->active));
    return {p.playback.phase(),!p.hidden&&p.environment.presented,p.environment.onScreen,p.environment.canAdvanceTransition,p.heldTime.has_value(),finite,p.settings.ambientEnabled,p.settings.reduceMotion,p.settings.lowPower};}
VisibilityPhase SourceWatchSession::phase()const noexcept{return impl_->playback.phase();}
std::uint64_t SourceWatchSession::generation()const noexcept{return impl_->lifecycleToken;}
std::optional<std::uint64_t> SourceWatchSession::completedTransition()const noexcept{return impl_->completion;}
bool SourceWatchSession::inputEnabled()const noexcept{return impl_->input;}
bool SourceWatchSession::pendingOpening()const noexcept{return impl_->heldTime.has_value();}
double SourceWatchSession::scrollPosition()const noexcept{return impl_->scrollPosition;}
std::optional<std::string_view> SourceWatchSession::hovered()const noexcept{return impl_->hovered;}
std::optional<std::string_view> SourceWatchSession::pressed()const noexcept{return impl_->pressed;}
const std::map<std::string,std::uint64_t,std::less<>>& SourceWatchSession::actions()const noexcept{return impl_->actions;}
const WatchSessionFrame* SourceWatchSession::currentFrame()const noexcept{return impl_->hasFrame?&impl_->rendered:nullptr;}
SourceWatchFrameStats SourceWatchSession::frameStats()const noexcept{return impl_->builder.stats();}
WatchSessionStats SourceWatchSession::stats()const noexcept{return impl_->stats;}
} // namespace endfield::app
