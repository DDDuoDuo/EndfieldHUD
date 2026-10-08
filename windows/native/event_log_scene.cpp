#include "native/event_log_scene.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::native {
namespace {using Json=ehud::data::Json;namespace m=modules;using Matrix=core::Matrix4;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
}
struct NativeEventLogScene::Impl {
    struct Track{double from{},target{},start{},duration{};};
    static double ease(double phase){return core::CubicTiming{0,0,.58,1}.value(phase);}
    static double value(Track t,double time){return t.duration>0?t.from+(t.target-t.from)*ease((time-t.start)/t.duration):t.target;}
    struct Part{
        m::EventLogPart plan;LayerScene scene;std::vector<LayerPlacement>placements,active;std::vector<Track>feedback;std::array<PlaneMask,8>masks{};
        std::optional<double>selectionStart;double stroke{1},uploadedStroke{1};std::uint64_t strokeRevision{1};
        Part(LayerRasterizer&r,m::EventLogPart p,const LayerRasterOptions&o):plan(std::move(p)),scene(r),placements(plan.surfaces.size()),feedback(plan.surfaces.size()){
            scene.load(plan.layers,o);need(scene.report().unsupported.empty(),"Unsupported original Event Log local artwork");active.reserve(placements.size());
            for(std::size_t n=0;n<placements.size();++n){const auto&s=plan.surfaces[n];placements[n].surface=scene.surfaceIndex(s.id).value_or(std::size_t(-1));placements[n].world=s.local;placements[n].opacity=0;feedback[n].from=feedback[n].target=s.rim&&s.framed?.28:0;if(placements[n].surface!=std::size_t(-1))active.push_back(placements[n]);}scene.setPlacements(active);scene.prepareDraws();
        }
    };
    struct Page{std::unique_ptr<Part>empty,scrollbar;std::vector<std::unique_ptr<Part>>rows;Page(){rows.reserve(5);}template<class F>void each(F&&f){if(empty)f(*empty);for(auto&p:rows)f(*p);if(scrollbar)f(*scrollbar);}};
    m::EventLogState&state;LayerRasterizer&raster;LayerRasterOptions options;m::EventLogAppearance appearance;std::unique_ptr<Part>header,toolbar;std::array<std::unique_ptr<Page>,2>pages;unsigned selected{};
    std::vector<std::unique_ptr<Part>>retired;std::vector<LayerCompositionEntry>entries;std::uint64_t revision{};bool dirty{true},pressed{};std::optional<std::string>hover;std::optional<double>handoffStart,confirmationStart;double handoffDirection{1},confirmationDirection{1},lastTime{};EventLogSceneStats stats;DrawObject validation;
    Impl(m::EventLogState&s,LayerRasterizer&r,LayerRasterOptions o,m::EventLogAppearance a):state(s),raster(r),options(std::move(o)),appearance(a){retired.reserve(18);entries.reserve(16);validation.sourceID="event-log-pose";validation.masks.reserve(8);}
    void time(double time)const{need(std::isfinite(time)&&time>=lastTime,"Event Log requires finite monotonic caller time");}
    void retire(std::unique_ptr<Part>&p){if(p)retired.push_back(std::move(p));}
    void retirePage(unsigned index){if(pages[index]){auto&p=*pages[index];retire(p.empty);for(auto&row:p.rows)retire(row);retire(p.scrollbar);pages[index].reset();}}
    void rebuildEntries(){entries.clear();if(header)entries.push_back({&header->scene,{}});for(auto&p:pages)if(p)p->each([&](auto&part){entries.push_back({&part.scene,{}});});if(toolbar)entries.push_back({&toolbar->scene,{}});}
    template<class F>void each(F&&f){if(header)f(*header);for(auto&p:pages)if(p)p->each(f);if(toolbar)f(*toolbar);}
};
NativeEventLogScene::NativeEventLogScene(m::EventLogState&s,LayerRasterizer&r,LayerRasterOptions options,m::EventLogAppearance appearance):impl_(std::make_unique<Impl>(s,r,std::move(options),appearance)){}
NativeEventLogScene::~NativeEventLogScene()=default;
void NativeEventLogScene::setAppearance(m::EventLogAppearance value){auto&i=*impl_;if(i.appearance==value)return;i.appearance=value;i.dirty=true;}
bool NativeEventLogScene::syncContent(double time){auto&i=*impl_;i.time(time);if(!i.dirty&&i.revision==i.state.revision()&&i.state.pendingChanges().empty())return false;need(i.retired.empty(),"Publish Event Log prior generation before replacement");const auto changes=i.state.pendingChanges();bool exchange{},settle{};for(const auto&change:changes){exchange|=change.kind==m::EventLogState::ChangeKind::exchange;settle|=change.kind==m::EventLogState::ChangeKind::settle;}
    auto plan=m::prepareEventLogArtwork(i.state,i.appearance);const auto same=[](const auto&p,const auto&v){return p&&p->plan.layers==v.layers;};const unsigned destination=exchange?1-i.selected:i.selected;auto*prior=exchange?nullptr:i.pages[destination].get();std::size_t needed{};
    if(!same(i.header,plan.header))needed+=plan.header.surfaces.size();if(!same(i.toolbar,plan.toolbar))needed+=plan.toolbar.surfaces.size();if(!prior||!same(prior->empty,plan.empty))needed+=plan.empty.surfaces.size();if(!prior||!same(prior->scrollbar,plan.scrollbar))needed+=plan.scrollbar.surfaces.size();
    std::array<std::size_t,5>reuse{};reuse.fill(5);for(std::size_t n=0;n<plan.rows.size();++n){if(prior)for(std::size_t old=0;old<prior->rows.size();++old)if(prior->rows[old]->plan.rowID==plan.rows[n].rowID&&same(prior->rows[old],plan.rows[n])){reuse[n]=old;break;}if(reuse[n]==5)needed+=plan.rows[n].surfaces.size();}
    need(needed<=LayerRasterizer::maximumEntries-i.raster.stats().entries,"Event Log replacement exceeds shared raster budget");auto header=same(i.header,plan.header)?nullptr:std::make_unique<Impl::Part>(i.raster,plan.header,i.options),toolbar=same(i.toolbar,plan.toolbar)?nullptr:std::make_unique<Impl::Part>(i.raster,plan.toolbar,i.options);
    auto next=std::make_unique<Impl::Page>();if(!prior||!same(prior->empty,plan.empty))next->empty=std::make_unique<Impl::Part>(i.raster,plan.empty,i.options);if(!prior||!same(prior->scrollbar,plan.scrollbar))next->scrollbar=std::make_unique<Impl::Part>(i.raster,plan.scrollbar,i.options);
    for(std::size_t n=0;n<plan.rows.size();++n)next->rows.push_back(reuse[n]==5?std::make_unique<Impl::Part>(i.raster,plan.rows[n],i.options):nullptr);
    const auto inheritFeedback=[](Impl::Part&next,const Impl::Part&previous){for(std::size_t n=0;n<next.plan.surfaces.size();++n){const auto&surface=next.plan.surfaces[n];if(surface.action.empty())continue;for(std::size_t old=0;old<previous.plan.surfaces.size();++old){const auto&before=previous.plan.surfaces[old];if(surface.id==before.id&&surface.action==before.action&&surface.rim==before.rim){next.feedback[n]=previous.feedback[old];break;}}}};
    if(toolbar&&i.toolbar)inheritFeedback(*toolbar,*i.toolbar);
    if(prior)for(auto&nextRow:next->rows)if(nextRow)for(const auto&oldRow:prior->rows)if(nextRow->plan.rowID==oldRow->plan.rowID){inheritFeedback(*nextRow,*oldRow);break;}
    // Everything that can allocate/parse/rasterize has succeeded before old
    // pages are moved or exposed to the borrowed shared composition.
    if(header){i.retire(i.header);i.header=std::move(header);++i.stats.builds;}if(toolbar){i.retire(i.toolbar);i.toolbar=std::move(toolbar);++i.stats.builds;}
    if(prior){if(!next->empty)next->empty=std::move(prior->empty);if(!next->scrollbar)next->scrollbar=std::move(prior->scrollbar);for(std::size_t n=0;n<next->rows.size();++n)if(reuse[n]!=5){next->rows[n]=std::move(prior->rows[reuse[n]]);next->rows[n]->plan.full=plan.rows[n].full;}}
    i.retirePage(destination);i.pages[destination]=std::move(next);i.selected=destination;++i.stats.builds;
    if(exchange&&i.pages[1-i.selected])i.pages[1-i.selected]->each([](auto&part){part.selectionStart.reset();part.stroke=1;});
    for(const auto&change:changes){if(change.kind==m::EventLogState::ChangeKind::settle){i.handoffStart.reset();i.confirmationStart.reset();for(auto&p:i.pages)if(p)p->each([](auto&part){part.selectionStart.reset();part.stroke=1;});}
        else if(change.kind==m::EventLogState::ChangeKind::exchange){i.handoffStart=change.animated?std::optional(time):std::nullopt;i.handoffDirection=change.direction;}
        else if(change.kind==m::EventLogState::ChangeKind::confirmation){i.confirmationStart=change.animated?std::optional(time):std::nullopt;i.confirmationDirection=change.direction;}
        else if(change.kind==m::EventLogState::ChangeKind::selection)for(auto&p:i.pages[i.selected]->rows)if(p->plan.rowID==change.selected){p->selectionStart=change.animated?std::optional(time):std::nullopt;p->stroke=change.animated?0:1;}}
    if(settle||!i.handoffStart)i.retirePage(1-i.selected);
    // The outgoing source page freezes without action feedback. The new page
    // adopts the current pointer once, without retaining any hidden payloads.
    i.each([&](auto&part){for(std::size_t n=0;n<part.plan.surfaces.size();++n){const auto&s=part.plan.surfaces[n];if(s.action.empty())continue;const bool hit=i.hover&&*i.hover==s.action;const double target=s.rim?(hit?1:s.framed?.28:0):(hit?(i.pressed?1:.62):0);auto&track=part.feedback[n];if(track.target!=target)track={Impl::value(track,time),target,time,i.state.reduceMotion()?0:i.pressed&&hit?.06:.14};}});
    i.rebuildEntries();i.state.acknowledgeChanges();i.revision=i.state.revision();i.dirty=false;i.lastTime=time;return true;
}
bool NativeEventLogScene::setFeedback(std::optional<std::string_view>action,bool pressed,double time){auto&i=*impl_;i.time(time);if(!i.state.reduceMotion()&&i.hover.has_value()==action.has_value()&&(!action||*i.hover==*action)&&i.pressed==pressed)return false;
    const auto update=[&](auto&part){for(std::size_t n=0;n<part.plan.surfaces.size();++n){const auto&s=part.plan.surfaces[n];if(s.action.empty())continue;const bool old=i.hover&&*i.hover==s.action,hit=action&&*action==s.action;if(old==hit&&(!hit||pressed==i.pressed)&&!i.state.reduceMotion())continue;auto&track=part.feedback[n];const double target=s.rim?(hit?1:s.framed?.28:0):(hit?(pressed?1:.62):0);track={Impl::value(track,time),target,time,i.state.reduceMotion()?0:pressed&&hit?.06:.14};}};
    if(i.toolbar)update(*i.toolbar);if(i.pages[i.selected])i.pages[i.selected]->each(update);i.hover=action?std::optional(std::string(*action)):std::nullopt;i.pressed=pressed;i.lastTime=time;return true;
}
void NativeEventLogScene::updatePose(const NativeEventLogPose&pose){auto&i=*impl_;i.time(pose.time);need(i.revision==i.state.revision()&&!i.dirty,"Synchronize Event Log before pose");need(pose.world.finite()&&std::isfinite(pose.opacity)&&pose.opacity>=0&&pose.opacity<=1&&pose.ownerMasks.size()<=7,"Invalid Event Log placement");if(pose.shutter)validatePlaneShutter(*pose.shutter);
    const auto inverse=core::source::inverseSourceMatrix(pose.world);const auto sample=i.handoffStart?m::sampleEventLogHandoff(i.handoffDirection,pose.time-*i.handoffStart):m::EventLogHandoffSample{{0,0,376,198},{},0,0,false};
    const auto prepare=[&](Impl::Part&part,std::optional<unsigned>page){core::Rect clip=m::EventLogState::viewport();double pageX{};bool pageVisible=true;if(page&&i.handoffStart){const bool incoming=*page==i.selected;const auto local=incoming?sample.incoming:sample.outgoing;clip={local.x+12,local.y+94,local.width,local.height};pageVisible=clip.width>0;pageX=incoming?sample.incomingX:sample.outgoingX;if(!pageVisible)clip=m::EventLogState::viewport();}
        const Matrix base=pose.world*Matrix::translation(pageX,0)*(part.plan.rowID.empty()?Matrix{}:Matrix::translation(part.plan.full.x,part.plan.full.y));const auto maskCount=pose.ownerMasks.size()+std::size_t(page.has_value());std::copy(pose.ownerMasks.begin(),pose.ownerMasks.end(),part.masks.begin());if(page)part.masks[pose.ownerMasks.size()]={inverse,clip};const double toolbar=i.confirmationStart?1-Impl::ease((pose.time-*i.confirmationStart)/.18):0;
        for(std::size_t n=0;n<part.placements.size();++n){const auto&s=part.plan.surfaces[n];auto&value=part.placements[n];value.world=base*(s.toolbar?Matrix::translation(10*i.confirmationDirection*toolbar,0,-8*toolbar):Matrix{})*s.local;value.opacity=pageVisible?pose.opacity*(s.action.empty()?s.opacity:float(Impl::value(part.feedback[n],pose.time))):0;value.masks={part.masks.data(),maskCount};i.validation.world=value.world;i.validation.opacity=value.opacity;i.validation.masks.assign(value.masks.begin(),value.masks.end());i.validation.shutter=pose.shutter;validateDrawObject(i.validation);}};
    if(i.header)prepare(*i.header,{});for(unsigned page=0;page<2;++page)if(i.pages[page])i.pages[page]->each([&](auto&part){prepare(part,page);});if(i.toolbar)prepare(*i.toolbar,{});
    i.each([&](auto&part){part.active.clear();for(const auto&placement:part.placements)if(placement.surface!=std::size_t(-1))part.active.push_back(placement);part.scene.setPlacements(part.active);part.scene.setGroupShutter(pose.shutter);if(part.selectionStart){part.stroke=Impl::ease((pose.time-*part.selectionStart)/.18);if(pose.time-*part.selectionStart>=.18)part.selectionStart.reset();}});
    if(i.handoffStart&&!sample.active){i.handoffStart.reset();i.retirePage(1-i.selected);i.rebuildEntries();}if(i.confirmationStart&&pose.time-*i.confirmationStart>=.18)i.confirmationStart.reset();i.lastTime=pose.time;++i.stats.poses;
}
bool NativeEventLogScene::uploadAnimations(Renderer&){auto&i=*impl_;bool changed{};for(auto&page:i.pages)if(page)for(auto&part:page->rows)if(part->uploadedStroke!=part->stroke){for(std::size_t n=0;n<part->plan.surfaces.size();++n)if(part->plan.surfaces[n].outline){auto node=part->plan.layers["children"].array()[n];node["shape"]["path"]=m::eventLogOutlinePath(part->stroke);part->scene.updateLocalContent(part->plan.surfaces[n].id,++part->strokeRevision,node,i.options);++i.stats.outlineRasters;changed=true;}part->uploadedStroke=part->stroke;}return changed;}
bool NativeEventLogScene::requiresFrames(double time)const{auto&i=*impl_;i.time(time);if(i.handoffStart&&time-*i.handoffStart<.26)return true;if(i.confirmationStart&&time-*i.confirmationStart<.18)return true;bool demand{};i.each([&](const auto&part){demand|=part.selectionStart&&time-*part.selectionStart<.18;for(auto track:part.feedback)demand|=track.duration>0&&track.from!=track.target&&time<track.start+track.duration;});return demand;}
std::span<const LayerCompositionEntry>NativeEventLogScene::entries()const noexcept{return impl_->entries;}
bool NativeEventLogScene::collectRetired(Renderer&r){auto&i=*impl_;for(auto&part:i.retired)if(!part->scene.releaseResources(r))return false;i.retired.clear();return true;}
bool NativeEventLogScene::releaseResources(Renderer&r){bool okay=collectRetired(r);impl_->each([&](auto&part){okay=part.scene.releaseResources(r)&&okay;});return okay;}
EventLogSceneStats NativeEventLogScene::stats()const noexcept{auto result=impl_->stats;result.retiredParts=impl_->retired.size();const auto current=impl_->selected;if(impl_->pages[current])result.rows=impl_->pages[current]->rows.size();if(impl_->pages[1-current])result.outgoingRows=impl_->pages[1-current]->rows.size();return result;}
}
#endif
