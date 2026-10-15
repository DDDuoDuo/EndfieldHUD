#include "native/app_shortcut_scene.hpp"
#include "modules/app_shortcut_trace.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include "core/motion.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
namespace endfield::native {namespace {
namespace m=modules;using J=ehud::data::Json;using M=core::Matrix4;using R=core::Rect;
void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
bool roundedContains(R r,core::Point p){if(!r.contains(p))return false;const double dx=std::abs(p.x-std::clamp(p.x,r.x+3,r.x+r.width-3)),dy=std::abs(p.y-std::clamp(p.y,r.y+3,r.y+r.height-3));if(dx==0||dy==0)return true;constexpr double k=.5522847498;const double x=dx/3,y=dy/3;double lo=0,hi=1;for(unsigned n=0;n<32;++n){const double t=(lo+hi)*.5,u=1-t,c=u*u*u+3*u*u*t+3*u*t*t*k;if(c>x)lo=t;else hi=t;}const double t=(lo+hi)*.5,u=1-t;return y<=3*u*u*t*k+3*u*t*t+t*t*t;}
bool feedbackRole(m::ShortcutSurfaceRole r){return r==m::ShortcutSurfaceRole::tint||r==m::ShortcutSurfaceRole::rim;}
// A hover/press tint or rim that is not showing holds one transparent point
// instead of its control-sized bitmap (Mac: a CALayer background that keeps
// no bitmap); the leaf paints its source shape while its feedback shows.
J feedbackPlaceholder(const std::string&id){return J::Object{{"id",id},{"kind","layer"},{"bounds",J::Array{0,0,1,1}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"backgroundColor",J::Object{{"sRGB",J::Array{0,0,0,0}}}},{"children",J::Array{}}};}
J root(const std::vector<m::ShortcutSurface>&parts){J::Array children;children.reserve(parts.size());for(const auto&p:parts)children.push_back(feedbackRole(p.role)?feedbackPlaceholder(p.id):p.content);return J::Object{{"bounds",J::Array{0,0,400,334}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",std::move(children)}};}
J traceContent(std::string_view id,m::ShortcutColor accent,double phase){return J::Object{{"id",std::string(id)},{"kind","shape"},{"bounds",J::Array{0,0,48,48}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"shape",J::Object{{"path",m::shortcutPresetTracePath(phase)},{"fillColor",J{}},{"strokeColor",J::Object{{"sRGB",J::Array{accent[0],accent[1],accent[2],accent[3]}}}},{"lineWidth",1.5},{"lineCap","butt"},{"lineJoin","miter"}}}};}
void appendTrace(std::vector<m::ShortcutSurface>&plan,const m::ShortcutPresetTransition&transition,m::ShortcutColor accent){
 const auto base="apps:icon:"+transition.icon;const auto glyph=std::find_if(plan.begin(),plan.end(),[&](const auto&s){return s.id==base+"/glyph";});need(glyph!=plan.end(),"Missing selected source preset face");
 const auto icon=std::find(m::shortcutIconOrder.begin(),m::shortcutIconOrder.end(),transition.icon);need(icon!=m::shortcutIconOrder.end(),"Unknown source preset trace");const auto box=m::shortcutIconRect(std::size_t(icon-m::shortcutIconOrder.begin()));const auto id=base+"/register";
 // Keep the original complete source path in the initially invisible leaf.
 // A move-only path at strokeEnd zero has no native raster extent.
 plan.insert(glyph+1,{id,base,traceContent(id,accent,1),M::translation(box.x,box.y),{},m::ShortcutSurfaceRole::presetTrace});
}
J maskContent(const core::SubsectionCurvePath&p){J::Array commands;std::size_t at{};for(std::size_t n=0;n<p.opcodeCount;++n){const auto op=p.opcodes[n];const auto count=op<2?1:op==2?2:op==3?3:0;J::Array points;for(unsigned k=0;k<unsigned(count);++k){points.push_back(J::Array{p.coordinates[at],p.coordinates[at+1]});at+=2;}commands.push_back(J::Object{{"op",op==0?"move":op==1?"line":op==2?"quadratic":op==3?"cubic":"close"},{"points",std::move(points)}});}need(at==p.coordinateCount,"Invalid original shortcut mask");return J::Object{{"kind","shape"},{"bounds",J::Array{0,0,400,334}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"shape",J::Object{{"path",std::move(commands)},{"fillColor",J::Object{{"sRGB",J::Array{1,1,1,1}}}},{"strokeColor",J{}}}}};}
}
struct NativeAppShortcutScene::Impl {
 struct Track {double from{},target{},start{},duration{};};
 static double value(const Track&t,double time){return t.duration>0?t.from+(t.target-t.from)*core::CubicTiming{0,0,.58,1}.value((time-t.start)/t.duration):t.target;}
 // The leaves of one Part in draw order. On the list screen the saved
 // cards come first as retained slots (cardSlots), then the chrome; a
 // hidden card is a slot's neighbour outside the list viewport.
 struct Layout {std::vector<m::ShortcutSurface>surfaces;std::vector<std::uint8_t>hidden;};
 struct Part {
  // painted: a tint/rim leaf currently holds its source shape (else its
  // transparent point; feedbackPlaceholder).
  std::vector<m::ShortcutSurface>plan;std::vector<std::uint8_t>hidden,painted;std::uint64_t contentRevision{};LayerScene scene;std::vector<LayerPlacement>placements;std::vector<std::array<PlaneMask,8>>masks;std::vector<Track>feedback;std::vector<R>feedbackBounds;bool uploaded{};std::uint64_t uploadedResourceRevision{};
  std::optional<std::size_t>traceIndex;std::optional<m::ShortcutPresetTransition>trace;m::ShortcutColor traceAccent{};double traceProgress{},paintedTraceProgress{};std::uint64_t traceRevision{1};
  std::string maskID;std::optional<core::SubsectionCurvePath>maskPath;std::optional<PlaneAlphaMask>alpha;std::shared_ptr<const LayerRasterImage>maskImage;std::uint64_t maskRevision{},uploadedMask{};bool maskDirty{};
  Part(LayerRasterizer&r,Layout l,const LayerRasterOptions&o):plan(std::move(l.surfaces)),hidden(std::move(l.hidden)),painted(plan.size(),0),scene(r),placements(plan.size()),masks(plan.size()),feedback(plan.size()),feedbackBounds(plan.size()){
   need(hidden.size()==plan.size(),"Every Shortcut leaf states its visibility");static std::atomic<std::uint64_t>id{};maskID="apps.mask."+std::to_string(++id);scene.load(root(plan),o);need(scene.report().unsupported.empty(),"Unsupported source Shortcut artwork");
   for(std::size_t n=0;n<plan.size();++n){const auto&s=plan[n];if(s.role==m::ShortcutSurfaceRole::presetTrace)traceIndex=n;bounds(n);const auto index=scene.surfaceIndex(s.id);need(index&&*index==n,"Shortcut source leaves must remain independent");placements[n]={n,s.local,(hidden[n]||s.role==m::ShortcutSurfaceRole::tint||s.role==m::ShortcutSurfaceRole::rim||s.role==m::ShortcutSurfaceRole::presetTrace)?0.f:1.f,{}};}scene.setPlacements(placements);scene.prepareDraws();
  }
  void bounds(std::size_t n){const auto&s=plan[n];const auto&box=s.content["bounds"].array();feedbackBounds[n]={s.local.values[12],s.local.values[13],box[2].number(),box[3].number()};}
 };
 m::AppShortcutState&state;const NativeAppShortcutAssets&assets;LayerRasterizer&raster;LayerRasterOptions options;m::ShortcutAppearance appearance;m::ShortcutOriginalImages images;
 std::unique_ptr<Part>current,outgoing;std::vector<std::unique_ptr<Part>>retired;std::array<LayerCompositionEntry,2>entries{};std::size_t entryCount{};std::uint64_t revision{},screenGeneration{},presetGeneration{};double presetStart{};std::string presetIcon;bool dirty{true},posed{},pressed{},reduced{};std::optional<core::Point>pointer;double time{};AppShortcutSceneStats stats;Renderer*owner{};DrawObject validator;
 Impl(m::AppShortcutState&s,const NativeAppShortcutAssets&a,LayerRasterizer&r,LayerRasterOptions o,m::ShortcutAppearance v):state(s),assets(a),raster(r),options(std::move(o)),appearance(v){retired.reserve(4);validator.sourceID="apps.pose";validator.masks.reserve(8);}
 void clock(double t)const{need(std::isfinite(t)&&t>=time,"Shortcut scene needs monotonic time");}
 // AppShortcutCanvas.repaint rebuilds every card on each scroll step; here
 // the cards that can meet the 252-point list viewport at once (at most six
 // of the 52-point rows) keep slot index % 6, filled with the visible cards
 // and their nearest neighbours in the saved order. Cards never overlap, so
 // their order between slots draws the same pixels.
 static constexpr std::size_t cardSlots=6;
 Layout layout(std::vector<m::ShortcutSurface>plan)const{
  Layout out;const auto&items=state.file().items;
  if(state.editing()||items.empty()){out.hidden.assign(plan.size(),0);out.surfaces=std::move(plan);return out;}
  std::optional<std::size_t>shown;for(const auto&s:plan)if(s.row)shown=shown?std::min(*shown,*s.row):*s.row;
  const auto count=items.size(),slots=std::min(count,cardSlots),start=std::min(shown.value_or(static_cast<std::size_t>(state.scrollOffset()/52)),count-slots);
  std::array<std::vector<m::ShortcutSurface>,cardSlots>cards;std::vector<m::ShortcutSurface>chrome;
  for(auto&s:plan){if(!s.row){chrome.push_back(std::move(s));continue;}need(*s.row>=start&&*s.row<start+slots,"Visible Shortcut card outside its retained window");cards[*s.row%cardSlots].push_back(std::move(s));}
  for(std::size_t k=0;k<slots;++k){
   const auto index=start+(k+cardSlots-start%cardSlots)%cardSlots;const bool visible=!cards[k].empty();
   auto card=visible?std::move(cards[k]):m::prepareAppShortcutRow(state,index,assets.artwork(),appearance,images);
   const auto prefix="apps:"+items[index].id,slot="apps.card."+std::to_string(k);
   for(auto&s:card){need(s.row==index&&s.id.starts_with(prefix),"Shortcut card leaf outside its row");s.id=slot+s.id.substr(prefix.size());s.content["id"]=s.id;out.surfaces.push_back(std::move(s));out.hidden.push_back(!visible);}
  }
  for(auto&s:chrome){out.surfaces.push_back(std::move(s));out.hidden.push_back(0);}
  return out;
 }
 // The same leaves (identity, role, clip and chrome action) as the retained
 // Part: repaint only the leaves whose content changed and re-place the
 // rest; feedback restarts as on a rebuilt canvas. False leaves it as is.
 bool replace(Part&part,Layout&next){
  auto&leaves=next.surfaces;if(part.plan.size()!=leaves.size()||part.scene.fontRevision()!=raster.fontRevision())return false;
  for(std::size_t n=0;n<leaves.size();++n){const auto&a=part.plan[n];const auto&b=leaves[n];if(a.id!=b.id||a.role!=b.role||a.clip!=b.clip||a.row.has_value()!=b.row.has_value()||(!a.row&&a.action!=b.action))return false;}
  for(std::size_t n=0;n<leaves.size();++n){auto&a=part.plan[n];auto&b=leaves[n];
   if(a.content!=b.content&&(!feedbackRole(b.role)||part.painted[n])){need(part.contentRevision!=UINT64_MAX,"Shortcut leaf revision exhausted");need(part.scene.updateLocalContent(b.id,++part.contentRevision,b.content,options),"Shortcut leaf repaint was refused");++stats.leafRepaints;}
   a=std::move(b);part.hidden[n]=next.hidden[n];part.bounds(n);part.feedback[n]={};
  }
  return true;
 }
 // A tint/rim leaf paints its source shape while its feedback shows and
 // returns to its transparent point once it faded out (feedbackPlaceholder).
 void paintFeedback(Part&p,std::size_t n,bool shown){const auto&s=p.plan[n];need(p.contentRevision!=UINT64_MAX,"Shortcut leaf revision exhausted");
  need(p.scene.updateLocalContent(s.id,++p.contentRevision,shown?s.content:feedbackPlaceholder(s.id),options),"Shortcut feedback repaint was refused");p.painted[n]=shown;++(shown?stats.feedbackPaints:stats.feedbackReleases);}
 void refresh(){entryCount=0;if(outgoing)entries[entryCount++]={&outgoing->scene,{}};if(current)entries[entryCount++]={&current->scene,{}};}
 void retire(std::unique_ptr<Part>p){if(!p)return;if(p->uploaded||p->uploadedMask)retired.push_back(std::move(p));else raster.remove(p->maskID);}
 template<class F>void each(F&&f){if(outgoing)f(*outgoing,true);if(current)f(*current,false);}
 bool releasePart(Part&p,Renderer&r){p.uploadedResourceRevision=0;if(!p.scene.releaseResources(r))return false;if(p.uploadedMask&&r.stats().initialized&&!r.removeTexture(p.maskID))return false;p.uploaded=false;p.uploadedMask=0;p.maskImage.reset();raster.remove(p.maskID);return true;}
};
NativeAppShortcutScene::NativeAppShortcutScene(m::AppShortcutState&s,const NativeAppShortcutAssets&a,LayerRasterizer&r,LayerRasterOptions o,m::ShortcutAppearance v):impl_(std::make_unique<Impl>(s,a,r,std::move(o),v)){}
NativeAppShortcutScene::~NativeAppShortcutScene(){auto&i=*impl_;i.each([&](auto&p,bool){i.raster.remove(p.maskID);});for(auto&p:i.retired)i.raster.remove(p->maskID);}
bool NativeAppShortcutScene::setAppearance(m::ShortcutAppearance a){auto&i=*impl_;if(a==i.appearance)return false;i.appearance=a;i.dirty=true;return true;}
bool NativeAppShortcutScene::setOriginalImages(m::ShortcutOriginalImages v){auto&i=*impl_;if(v==i.images)return false;i.images=std::move(v);i.dirty=true;return true;}
bool NativeAppShortcutScene::syncContent(double t){auto&i=*impl_;i.clock(t);const auto&preset=i.state.presetTransition();if(!preset.generation)i.presetGeneration=0;if(!i.dirty&&i.revision==i.state.revision())return false;auto plan=m::prepareAppShortcutArtwork(i.state,i.assets.artwork(),i.appearance,i.images);
 const bool newTrace=i.state.editing()&&preset.generation&&(preset.generation!=i.presetGeneration||preset.start!=i.presetStart||preset.icon!=i.presetIcon);if(newTrace)appendTrace(plan,preset,i.appearance.accent);
 auto layout=i.layout(std::move(plan));const auto transition=i.state.transition();
 // A finished handoff still names its mask in the published draws until
 // the next present, while the owner collects before it: that canvas is
 // replaced, so the published list drops the mask with it.
 const bool handoffEnded=i.current&&!transition.active&&(i.current->maskPath||i.current->uploadedMask);
 if(!newTrace&&i.current&&!handoffEnded&&transition.generation==i.screenGeneration&&i.replace(*i.current,layout)){
  i.presetIcon=preset.icon;i.presetGeneration=preset.generation;i.presetStart=preset.start;if(!transition.active)i.retire(std::move(i.outgoing));
  i.revision=i.state.revision();i.dirty=false;i.time=t;++i.stats.replacements;i.refresh();const auto point=i.pointer;const auto pressed=i.pressed;i.pointer.reset();i.pressed=!pressed;setFeedback(point,pressed,i.reduced,t);return true;
 }
 need(layout.surfaces.size()<=LayerRasterizer::maximumEntries-i.raster.stats().entries,"Shortcut replacement exceeds shared raster budget");auto next=std::make_unique<Impl::Part>(i.raster,std::move(layout),i.options);if(newTrace){next->trace=preset;next->traceAccent=i.appearance.accent;}
 i.presetIcon=preset.icon;i.presetGeneration=preset.generation;i.presetStart=preset.start;
 if(transition.active&&transition.generation!=i.screenGeneration){i.retire(std::move(i.outgoing));i.outgoing=std::move(i.current);}else i.retire(std::move(i.current));if(!transition.active)i.retire(std::move(i.outgoing));i.current=std::move(next);i.revision=i.state.revision();i.screenGeneration=transition.generation;i.dirty=false;i.time=t;++i.stats.builds;i.refresh();const auto point=i.pointer;const auto pressed=i.pressed;i.pointer.reset();i.pressed=!pressed;setFeedback(point,pressed,i.reduced,t);return true;}
bool NativeAppShortcutScene::setFeedback(std::optional<core::Point>p,bool pressed,bool reduced,double t){auto&i=*impl_;i.clock(t);if(p)need(std::isfinite(p->x)&&std::isfinite(p->y),"Invalid shortcut pointer");if(i.pointer==p&&i.pressed==pressed&&i.reduced==reduced)return false;std::string_view action;double area=INFINITY;if(p&&i.current&&i.state.active()&&!i.state.transitioning())for(std::size_t n=0;n<i.current->plan.size();++n){const auto&s=i.current->plan[n];const auto&r=i.current->feedbackBounds[n];if(s.role==m::ShortcutSurfaceRole::tint&&!i.current->hidden[n]&&(!s.clip||s.clip->contains(*p))&&roundedContains(r,*p)&&r.width*r.height<=area){action=s.action;area=r.width*r.height;}}
 i.each([&](auto&part,bool old){for(std::size_t n=0;n<part.plan.size();++n){const auto&s=part.plan[n];if(s.role!=m::ShortcutSurfaceRole::tint&&s.role!=m::ShortcutSurfaceRole::rim)continue;const bool matches=!old&&s.action==action;const double target=matches?(s.role==m::ShortcutSurfaceRole::rim?1:pressed?1:.62):0;auto&track=part.feedback[n];if(track.target!=target||(reduced&&track.duration))track={Impl::value(track,t),target,t,reduced?0:pressed&&matches?.06:.14};if(target>0&&!part.painted[n])i.paintFeedback(part,n,true);}});i.pointer=p;i.pressed=pressed;i.reduced=reduced;i.time=t;return true;}
void NativeAppShortcutScene::updatePose(const AppShortcutScenePose&p){auto&i=*impl_;i.clock(p.time);need(!i.dirty&&i.revision==i.state.revision()&&i.current,"Synchronize Shortcut content before pose");need(p.world.finite()&&std::isfinite(p.opacity)&&p.opacity>=0&&p.opacity<=1&&p.ownerMasks.size()<=7,"Invalid Shortcut pose");if(p.shutter)validatePlaneShutter(*p.shutter);const auto sample=m::shortcutScreenSample(i.state.transition(),p.time);const auto preset=m::shortcutPresetTransform(i.state.presetTransition(),p.time);
 i.each([&](auto&part,bool old){const auto screen=p.world*M::translation(200,167)*(old?sample.outgoing:sample.incoming)*M::translation(-200,-167),inverse=core::source::inverseSourceMatrix(screen);std::optional<core::SubsectionCurvePath>path;if(sample.active)path=(old?i.assets.departure():i.assets.arrival())->sample(i.state.transition().direction,sample.phase);if(path!=part.maskPath){part.maskPath=path;part.maskDirty=bool(path);}part.alpha=path?std::optional(PlaneAlphaMask{inverse,part.maskImage?part.maskImage->bounds:R{0,0,400,334},part.maskID}):std::nullopt;
  if(part.trace)part.traceProgress=i.state.presetTransition().generation?m::shortcutPresetProgress(*part.trace,p.time):1;
  for(std::size_t n=0;n<part.plan.size();++n){const auto&s=part.plan[n];auto&v=part.placements[n];v.world=screen*((!old&&s.role==m::ShortcutSurfaceRole::preview)?preset:M{})*s.local;v.opacity=part.hidden[n]?0.f:p.opacity*((s.role==m::ShortcutSurfaceRole::tint||s.role==m::ShortcutSurfaceRole::rim)?float(Impl::value(part.feedback[n],p.time)):s.role==m::ShortcutSurfaceRole::presetTrace&&part.traceProgress==0?0.f:1.f);std::copy(p.ownerMasks.begin(),p.ownerMasks.end(),part.masks[n].begin());auto count=p.ownerMasks.size();if(s.clip)part.masks[n][count++]={inverse,*s.clip};v.masks=std::span(part.masks[n]).first(count);i.validator.world=v.world;i.validator.opacity=v.opacity;i.validator.masks.assign(v.masks.begin(),v.masks.end());i.validator.shutter=p.shutter?std::optional(*p.shutter):std::nullopt;validateDrawObject(i.validator);}
 });i.each([&](auto&part,bool){part.scene.setPlacements(part.placements);part.scene.setGroupShutter(p.shutter?std::optional(*p.shutter):std::nullopt);part.scene.setGroupAlphaMask(part.alpha);});i.posed=true;i.time=p.time;++i.stats.poses;}
bool NativeAppShortcutScene::uploadAnimations(Renderer&r){auto&i=*impl_;need(i.posed&&r.stats().initialized&&(!i.owner||i.owner==&r),"Invalid Shortcut resource owner");bool changed{};
 // Feedback that finished fading out on the current screen gives back its
 // bitmap (an outgoing screen is retired whole after its handoff).
 if(i.current)for(std::size_t n=0;n<i.current->plan.size();++n){const auto&track=i.current->feedback[n];
  if(i.current->painted[n]&&track.target==0&&(track.duration<=0||i.time>=track.start+track.duration)){i.paintFeedback(*i.current,n,false);changed=true;}}
 i.each([&](auto&part,bool){if(part.traceIndex&&part.traceProgress!=part.paintedTraceProgress){
  const auto&surface=part.plan[*part.traceIndex];const auto content=traceContent(surface.id,part.traceAccent,part.traceProgress);need(part.traceRevision!=UINT64_MAX,"Shortcut trace revision exhausted");
  part.scene.updateLocalContent(surface.id,++part.traceRevision,content,i.options);part.paintedTraceProgress=part.traceProgress;changed=true;
 }if(part.maskPath){if(part.maskDirty){auto options=i.options;options.paddingPoints=0;auto image=i.raster.rasterize(part.maskID,++part.maskRevision,maskContent(*part.maskPath),options);need(image->complete()&&std::size_t(image->width)*image->height<=LayerRasterizer::maximumPixels,"Unbounded or unsupported Shortcut source mask");part.maskImage=std::move(image);part.maskDirty=false;++i.stats.maskRasters;i.stats.maskBytes+=part.maskImage->straightRGBA.size();}if(part.uploadedMask!=part.maskRevision){r.setTexture(part.maskID,part.maskRevision,{part.maskImage->width,part.maskImage->height,part.maskImage->straightRGBA,TextureColorSpace::linear,TextureFilter::linear});part.uploadedMask=part.maskRevision;changed=true;}part.alpha->bounds=part.maskImage->bounds;part.scene.setGroupAlphaMask(part.alpha);}if(part.uploadedResourceRevision!=part.scene.resourceRevision()){part.scene.uploadResources(r);part.uploadedResourceRevision=part.scene.resourceRevision();}part.uploaded=true;});i.owner=&r;return changed;}
bool NativeAppShortcutScene::requiresFrames(double t)const{const auto&i=*impl_;i.clock(t);if(i.state.requiresFrames(t))return true;if(!i.state.active()||i.reduced)return false;const auto check=[&](const auto&p){return p&&std::any_of(p->feedback.begin(),p->feedback.end(),[&](const auto&v){return v.duration&&v.from!=v.target&&t<v.start+v.duration;});};return check(i.current)||check(i.outgoing);}
std::span<const LayerCompositionEntry>NativeAppShortcutScene::entries()const noexcept{return std::span(impl_->entries).first(impl_->entryCount);}
bool NativeAppShortcutScene::collectRetired(Renderer&r){auto&i=*impl_;for(auto&p:i.retired)if(!i.releasePart(*p,r))return false;i.retired.clear();bool okay=true;i.each([&](auto&p,bool){p.scene.collectRetiredResources(r);if(!p.maskPath&&p.uploadedMask){if(!r.stats().initialized||r.removeTexture(p.maskID)){p.uploadedMask=0;p.maskImage.reset();i.raster.remove(p.maskID);}else okay=false;}});return okay;}
bool NativeAppShortcutScene::releaseResources(Renderer&r){auto&i=*impl_;bool okay=collectRetired(r);i.each([&](auto&p,bool){okay=i.releasePart(p,r)&&okay;});if(okay)i.owner=nullptr;return okay;}
bool NativeAppShortcutScene::park(Renderer&r){auto&i=*impl_;if(!i.current&&!i.outgoing&&i.retired.empty())return false;
 // A part still in the caller's published composition refuses (LayerScene
 // throws before releasing it): the canvas stays resident as it is.
 try{if(!releaseResources(r))return false;}catch(const std::bad_alloc&){throw;}catch(const std::exception&){return false;}
 // Destroying a part removes its leaves from the shared rasterizer
 // (~LayerScene); releasePart already removed its mask.
 i.current.reset();i.outgoing.reset();i.retired.clear();i.refresh();
 // The screen and preset generations it already showed stay seen: the
 // rebuilt canvas paints the same leaves, without replaying a trace.
 i.dirty=true;i.posed=false;i.owner=nullptr;++i.stats.parks;return true;}
AppShortcutSceneStats NativeAppShortcutScene::stats()const noexcept{const auto&i=*impl_;auto out=i.stats;
 out.surfaces=(i.current?i.current->plan.size():0)+(i.outgoing?i.outgoing->plan.size():0);out.retired=i.retired.size();
 const auto add=[&](const Impl::Part&p){++out.parts;out.rasterSurfaces+=p.scene.report().surfaces;out.pixelBytes+=p.scene.report().pixelBytes+(p.maskImage?p.maskImage->straightRGBA.size():0);};
 if(i.current)add(*i.current);if(i.outgoing)add(*i.outgoing);for(const auto&p:i.retired)add(*p);return out;}
}
#endif
