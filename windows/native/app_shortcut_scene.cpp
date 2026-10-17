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
// A plain plate leaf (below) is drawn as a GPU mesh and holds the same point.
J root(const std::vector<m::ShortcutSurface>&parts,const std::vector<std::uint8_t>&meshed){J::Array children;children.reserve(parts.size());for(std::size_t n=0;n<parts.size();++n){const auto&p=parts[n];children.push_back(feedbackRole(p.role)||meshed[n]?feedbackPlaceholder(p.id):p.content);}return J::Object{{"bounds",J::Array{0,0,400,334}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",std::move(children)}};}
// A CALayer plate (AppShortcutCanvas cards, buttons and preset tiles): a
// background colour, a corner radius and an optional inside border, nothing
// else. Colours are linear straight RGBA (the GPU tint).
struct PlainPlate {double width{},height{},radius{},border{};std::array<float,4>fill{},stroke{};};
std::optional<std::array<float,4>>linearColor(const J&value){
 if(!value.isObject())return std::nullopt;const auto&c=value["sRGB"];if(!c.isArray()||c.array().size()!=4)return std::nullopt;std::array<double,4>v{};
 for(std::size_t k=0;k<4;++k){if(!c.array()[k].isNumber())return std::nullopt;v[k]=c.array()[k].number();if(!(v[k]>=0&&v[k]<=1))return std::nullopt;}
 return appShortcutLinearTint(v);
}
std::optional<PlainPlate> plainPlate(const m::ShortcutSurface&s){
 const auto&c=s.content;if(s.role!=m::ShortcutSurfaceRole::artwork||!c.isObject()||c["kind"]!=J("layer"))return std::nullopt;
 for(const auto&[key,value]:c.object()){
  if(key=="id"||key=="kind"||key=="bounds"||key=="backgroundColor"||key=="cornerRadius"||key=="borderColor"||key=="borderWidth")continue;
  if((key=="position"||key=="anchorPoint")&&value==J(J::Array{0,0}))continue;
  if(key=="children"&&value.isArray()&&value.array().empty())continue;
  return std::nullopt;
 }
 const auto&box=c["bounds"];if(!box.isArray()||box.array().size()!=4||!c["cornerRadius"].isNumber())return std::nullopt;
 for(const auto&v:box.array())if(!v.isNumber())return std::nullopt;
 if(box.array()[0].number()!=0||box.array()[1].number()!=0)return std::nullopt;
 PlainPlate out{box.array()[2].number(),box.array()[3].number(),c["cornerRadius"].number()};
 if(!(out.width>0&&out.height>0&&std::isfinite(out.width)&&std::isfinite(out.height)&&out.radius>=0&&std::isfinite(out.radius)))return std::nullopt;
 const auto fill=linearColor(c["backgroundColor"]);if(!fill)return std::nullopt;out.fill=*fill;
 // CALayer draws its border inside the bounds over the background (the
 // raster strokes the inset outline); no colour draws none.
 const auto&width=c["borderWidth"];
 if(!width.isNull()){
  if(!width.isNumber()||!(width.number()>=0)||!std::isfinite(width.number()))return std::nullopt;
  if(width.number()>0&&!c["borderColor"].isNull()){const auto stroke=linearColor(c["borderColor"]);
   if(!stroke||out.radius<width.number()||2*width.number()>=std::min(out.width,out.height))return std::nullopt;out.border=width.number();out.stroke=*stroke;}
 }
 return out;
}
bool overlaps(R a,R b){return a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height;}
R inflated(R r,double x,double y){return {r.x-x,r.y-y,r.width+2*x,r.height+2*y};}
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
std::array<float,4>appShortcutLinearTint(const std::array<double,4>&c){std::array<float,4>out{};
 for(std::size_t k=0;k<4;++k){need(std::isfinite(c[k])&&c[k]>=0&&c[k]<=1,"Invalid Shortcut plate colour");out[k]=static_cast<float>(k==3?c[k]:c[k]<=.04045?c[k]/12.92:std::pow((c[k]+.055)/1.055,2.4));}return out;}
namespace {
constexpr double plateArcPi=3.14159265358979323846;constexpr unsigned plateArcSegments=8; // eight segments a quarter arc
// The outline samples of a rounded rect (corner centre and outward normal),
// clockwise from the top-left corner's left end.
template<class F>void plateOutline(double w,double t,double r,F&&f){
 const std::array<std::array<double,3>,4>corners{{{r,r,plateArcPi},{w-r,r,1.5*plateArcPi},{w-r,t-r,0},{r,t-r,.5*plateArcPi}}};
 for(const auto&c:corners)for(unsigned k=0;k<=plateArcSegments;++k){const double a=c[2]+.5*plateArcPi*k/plateArcSegments;f(c[0],c[1],std::cos(a),std::sin(a));}}
void checkPlate(double w,double t,double r,double ppp){need(std::isfinite(w)&&std::isfinite(t)&&std::isfinite(r)&&w>0&&t>0&&r>=0&&std::isfinite(ppp)&&ppp>0,"Invalid Shortcut plate mesh");}
}
// The fill: a fan from the centre to the inner outline, and the coverage ramp
// across one raster pixel centred on the edge.
void appShortcutPlateFill(double w,double t,double radius,double ppp,std::vector<Vertex>&vertices,std::vector<std::uint32_t>&indices){
 checkPlate(w,t,radius,ppp);vertices.clear();indices.clear();const double r=std::min(radius,std::min(w,t)*.5),h=.5/ppp;
 const auto vertex=[&](double x,double y,double alpha){vertices.push_back({{static_cast<float>(x),static_cast<float>(y),0},{0,0},{1,1,1,static_cast<float>(alpha)}});};
 vertex(w*.5,t*.5,1);plateOutline(w,t,r,[&](double cx,double cy,double nx,double ny){vertex(cx+nx*std::max(0.,r-h),cy+ny*std::max(0.,r-h),1);vertex(cx+nx*(r+h),cy+ny*(r+h),0);});
 const auto samples=static_cast<std::uint32_t>((vertices.size()-1)/2);
 for(std::uint32_t k=0;k<samples;++k){const auto a=1+k*2,b=1+((k+1)%samples)*2;for(const auto v:{0u,a,b,a,a+1,b+1,a,b+1,b})indices.push_back(v);}
}
// The border: along each outline normal (outward offsets from the edge, h
// half a raster pixel), a band `border` wide inside the edge filtered by a
// one-pixel box: 0 at +h, a=min(1,border/2h) from h-min(border,2h) to
// h-max(border,2h), 0 at -border-h. Concentric with the fill's corners (the
// raster strokes the outline inset by border/2 with radius-border/2).
void appShortcutPlateRing(double w,double t,double radius,double border,double ppp,std::vector<Vertex>&vertices,std::vector<std::uint32_t>&indices){
 checkPlate(w,t,radius,ppp);need(std::isfinite(border)&&border>0&&radius>=border&&2*border<std::min(w,t),"Invalid Shortcut plate border");vertices.clear();indices.clear();
 const double r=std::min(radius,std::min(w,t)*.5),h=.5/ppp,a=std::min(1.,border/(2*h));
 const std::array<double,4>offset{h,h-std::min(border,2*h),h-std::max(border,2*h),-border-h},alpha{0,a,a,0};
 plateOutline(w,t,r,[&](double cx,double cy,double nx,double ny){for(std::size_t j=0;j<4;++j){const double d=std::max(0.,r+offset[j]);vertices.push_back({{static_cast<float>(cx+nx*d),static_cast<float>(cy+ny*d),0},{0,0},{1,1,1,static_cast<float>(alpha[j])}});}});
 const auto samples=static_cast<std::uint32_t>(vertices.size()/4);
 for(std::uint32_t k=0;k<samples;++k){const auto a0=k*4,b0=((k+1)%samples)*4;for(std::uint32_t j=0;j<3;++j)for(const auto v:{a0+j,a0+j+1,b0+j+1,a0+j,b0+j+1,b0+j})indices.push_back(v);}
}
struct NativeAppShortcutScene::Impl {
 struct Track {double from{},target{},start{},duration{};};
 static double value(const Track&t,double time){return t.duration>0?t.from+(t.target-t.from)*core::CubicTiming{0,0,.58,1}.value((time-t.start)/t.duration):t.target;}
 // The leaves of one Part in draw order. On the list screen the saved
 // cards come first as retained slots (cardSlots), then the chrome; a
 // hidden card is a slot's neighbour outside the list viewport.
 // meshed: plates drawn as GPU meshes under the part (see meshPlates): 1 a
 // fill, 2 a fill and its border ring.
 struct Layout {std::vector<m::ShortcutSurface>surfaces;std::vector<std::uint8_t>hidden,meshed;};
 // One GPU mesh per plate size, and per border width for a ring (the scene
 // shares them between its parts).
 struct PlateMesh {std::string id;double width{},height{},radius{},border{};std::vector<Vertex>vertices;std::vector<std::uint32_t>indices;bool resident{};};
 // A caption's measured width (one per descriptor and typography generation).
 struct Measure {J text;double width{};};
 struct Part {
  // painted: a tint/rim leaf currently holds its source shape (else its
  // transparent point; feedbackPlaceholder).
  // meshed: a plate drawn by plates[plateDraw[n]] (and its ring by the next
  // draw) in the underlay (an empty scene composed right before this part's
  // scene).
  std::vector<m::ShortcutSurface>plan;std::vector<std::uint8_t>hidden,painted,meshed;std::vector<std::uint32_t>plateDraw;std::vector<DrawObject>plates;LayerScene underlay;std::uint64_t contentRevision{};LayerScene scene;std::vector<LayerPlacement>placements;std::vector<std::array<PlaneMask,8>>masks;std::vector<Track>feedback;std::vector<R>feedbackBounds;bool uploaded{};std::uint64_t uploadedResourceRevision{};
  std::optional<std::size_t>traceIndex;std::optional<m::ShortcutPresetTransition>trace;m::ShortcutColor traceAccent{};double traceProgress{},paintedTraceProgress{};std::uint64_t traceRevision{1};
  std::string maskID;std::optional<core::SubsectionCurvePath>maskPath;std::optional<PlaneAlphaMask>alpha;std::shared_ptr<const LayerRasterImage>maskImage;std::uint64_t maskRevision{},uploadedMask{};bool maskDirty{};
  template<class Mesh>Part(LayerRasterizer&r,Layout l,const LayerRasterOptions&o,Mesh&&mesh):plan(std::move(l.surfaces)),hidden(std::move(l.hidden)),painted(plan.size(),0),meshed(std::move(l.meshed)),plateDraw(plan.size(),0),underlay(r),scene(r),placements(plan.size()),masks(plan.size()),feedback(plan.size()),feedbackBounds(plan.size()){
   need(hidden.size()==plan.size()&&meshed.size()==plan.size(),"Every Shortcut leaf states its visibility");static std::atomic<std::uint64_t>id{};maskID="apps.mask."+std::to_string(++id);scene.load(root(plan,meshed),o);need(scene.report().unsupported.empty(),"Unsupported source Shortcut artwork");
   underlay.load(J::Object{{"bounds",J::Array{0,0,0,0}},{"children",J::Array{}}},o);
   plates.reserve(static_cast<std::size_t>(std::count(meshed.begin(),meshed.end(),1)+2*std::count(meshed.begin(),meshed.end(),2)));
   for(std::size_t n=0;n<plan.size();++n)if(meshed[n]){const auto plate=plainPlate(plan[n]);need(plate.has_value()&&meshed[n]==(plate->border>0?2:1),"A meshed Shortcut leaf is a plate");plateDraw[n]=static_cast<std::uint32_t>(plates.size());
    for(const bool ring:{false,true}){if(ring&&!(plate->border>0))break;auto&d=plates.emplace_back();d.sourceID=maskID+(ring?"/ring/":"/plate/")+std::to_string(n);d.meshID=mesh(*plate,ring);d.linearTint=ring?plate->stroke:plate->fill;d.opacity=0;d.masks.reserve(8);}}
   for(std::size_t n=0;n<plan.size();++n){const auto&s=plan[n];if(s.role==m::ShortcutSurfaceRole::presetTrace)traceIndex=n;bounds(n);const auto index=scene.surfaceIndex(s.id);need(index&&*index==n,"Shortcut source leaves must remain independent");placements[n]={n,s.local,(hidden[n]||s.role==m::ShortcutSurfaceRole::tint||s.role==m::ShortcutSurfaceRole::rim||s.role==m::ShortcutSurfaceRole::presetTrace)?0.f:1.f,{}};}scene.setPlacements(placements);scene.prepareDraws();
  }
  void bounds(std::size_t n){const auto&s=plan[n];const auto&box=s.content["bounds"].array();feedbackBounds[n]={s.local.values[12],s.local.values[13],box[2].number(),box[3].number()};}
 };
 m::AppShortcutState&state;const NativeAppShortcutAssets&assets;LayerRasterizer&raster;LayerRasterOptions options;m::ShortcutAppearance appearance;m::ShortcutOriginalImages images;
 std::vector<PlateMesh>meshes;std::string meshPrefix;std::vector<Measure>measures;std::uint64_t measureFont{~std::uint64_t{0}};
 std::unique_ptr<Part>current,outgoing;std::vector<std::unique_ptr<Part>>retired;std::array<LayerCompositionEntry,4>entries{};std::size_t entryCount{};std::uint64_t revision{},screenGeneration{},presetGeneration{};double presetStart{};std::string presetIcon;bool dirty{true},posed{},pressed{},reduced{};std::optional<core::Point>pointer;double time{};AppShortcutSceneStats stats;Renderer*owner{};DrawObject validator;
 Impl(m::AppShortcutState&s,const NativeAppShortcutAssets&a,LayerRasterizer&r,LayerRasterOptions o,m::ShortcutAppearance v):state(s),assets(a),raster(r),options(std::move(o)),appearance(v){retired.reserve(4);validator.sourceID="apps.pose";validator.masks.reserve(8);static std::atomic<std::uint64_t>id{};meshPrefix="apps.plate."+std::to_string(++id)+".";}
 // The scene's fill or ring mesh of a plate (bounded: the source layout names
 // a few fixed control sizes and two tile border widths).
 const std::string&mesh(const PlainPlate&p,bool ring){const double border=ring?p.border:0;
  for(const auto&m:meshes)if(m.width==p.width&&m.height==p.height&&m.radius==p.radius&&m.border==border)return m.id;
  need(meshes.size()<32,"Too many Shortcut plate sizes");auto&m=meshes.emplace_back();m.id=meshPrefix+std::to_string(meshes.size());m.width=p.width;m.height=p.height;m.radius=p.radius;m.border=border;
  if(ring)appShortcutPlateRing(p.width,p.height,p.radius,border,options.pixelsPerPoint,m.vertices,m.indices);else appShortcutPlateFill(p.width,p.height,p.radius,options.pixelsPerPoint,m.vertices,m.indices);return m.id;}
 // A left-aligned single-line caption (AppShortcutCanvas names, headings and
 // the name button) rasterizes only the width its glyphs take, plus a margin
 // for overhanging ink: the text layer lays it out from the same origin, so
 // the narrower bitmap holds every painted pixel and drops transparent
 // columns. A caption that does not fit keeps its bounds (and truncation).
 // Measured once per caption descriptor and typography generation (the
 // cards of a scroll step reuse their measurements).
 void tighten(std::vector<m::ShortcutSurface>&leaves){
  if(measureFont!=raster.fontRevision()){measures.clear();measureFont=raster.fontRevision();}
  for(auto&s:leaves){const auto&c=s.content; // read-only: a missing key must not be inserted
   if(s.role!=m::ShortcutSurfaceRole::artwork||!c.isObject()||c["kind"]!=J("text"))continue;
   const auto&text=c["text"];const auto&box=c["bounds"];
   if(!text.isObject()||text["alignment"]!=J("left")||text["wrapped"]!=J(false)||!text["runs"].isNull()||!text["fontSize"].isNumber()||(!c["children"].isNull()&&!(c["children"].isArray()&&c["children"].array().empty())))continue;
   if(!box.isArray()||box.array().size()!=4||std::any_of(box.array().begin(),box.array().end(),[](const J&v){return !v.isNumber();})||box.array()[0].number()!=0||box.array()[1].number()!=0)continue;
   const double width=box.array()[2].number(),margin=std::ceil(text["fontSize"].number()*.5)+1;if(!(width>margin)||!std::isfinite(width))continue;
   const auto found=std::find_if(measures.begin(),measures.end(),[&](const Measure&m){return m.text==text;});double measured;
   if(found!=measures.end())measured=found->width;
   else{measured=raster.measureSourceText("apps.measure",text,width,options).width;++stats.textMeasures;if(measures.size()>=64)measures.erase(measures.begin());measures.push_back({text,measured});}
   if(const auto tight=std::ceil(measured+margin);tight<width){J::Array bounds{box.array()[0],box.array()[1],tight,box.array()[3]};s.content["bounds"]=std::move(bounds);}}
 }
 // Plates move under the part's other leaves as GPU meshes when no earlier
 // leaf can paint where they do (with a point of antialiasing around both,
 // and the preview's preset approach of 7 points): the same pixels in the
 // same order, without a control-sized bitmap.
 void meshPlates(Layout&l)const{
  const auto&leaves=l.surfaces;l.meshed.assign(leaves.size(),0);std::vector<R>painted;painted.reserve(leaves.size());
  for(std::size_t n=0;n<leaves.size();++n){const auto&s=leaves[n];const auto&box=s.content["bounds"].array();const R r{s.local.values[12],s.local.values[13],box[2].number(),box[3].number()};
   const auto plate=plainPlate(s);
   if(plate&&s.local==M::translation(r.x,r.y)&&std::none_of(painted.begin(),painted.end(),[&](const R&p){return overlaps(p,inflated(r,1,1));})){l.meshed[n]=plate->border>0?2:1;continue;}
   // A clipped leaf never paints outside its clip (the list viewport).
   auto paints=s.role==m::ShortcutSurfaceRole::preview?inflated(r,9,2):inflated(r,1,1);
   if(s.clip){const auto x0=std::max(paints.x,s.clip->x),y0=std::max(paints.y,s.clip->y),x1=std::min(paints.x+paints.width,s.clip->x+s.clip->width),y1=std::min(paints.y+paints.height,s.clip->y+s.clip->height);
    if(x1<=x0||y1<=y0)continue;paints={x0,y0,x1-x0,y1-y0};}
   painted.push_back(paints);}
 }
 void clock(double t)const{need(std::isfinite(t)&&t>=time,"Shortcut scene needs monotonic time");}
 // AppShortcutCanvas.repaint rebuilds every card on each scroll step; here
 // the cards that can meet the 252-point list viewport at once (at most six
 // of the 52-point rows) keep slot index % 6, filled with the visible cards
 // and their nearest neighbours in the saved order. Cards never overlap, so
 // their order between slots draws the same pixels.
 static constexpr std::size_t cardSlots=6;
 Layout layout(std::vector<m::ShortcutSurface>plan){
  Layout out;const auto&items=state.file().items;
  if(state.editing()||items.empty()){out.hidden.assign(plan.size(),0);out.surfaces=std::move(plan);tighten(out.surfaces);meshPlates(out);return out;}
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
  tighten(out.surfaces);meshPlates(out);return out;
 }
 // The same leaves (identity, role, clip and chrome action) as the retained
 // Part: repaint only the leaves whose content changed and re-place the
 // rest; feedback restarts as on a rebuilt canvas. False leaves it as is.
 bool replace(Part&part,Layout&next){
  auto&leaves=next.surfaces;if(part.plan.size()!=leaves.size()||part.scene.fontRevision()!=raster.fontRevision())return false;
  for(std::size_t n=0;n<leaves.size();++n){const auto&a=part.plan[n];const auto&b=leaves[n];if(a.id!=b.id||a.role!=b.role||a.clip!=b.clip||a.row.has_value()!=b.row.has_value()||(!a.row&&a.action!=b.action)||part.meshed[n]!=next.meshed[n])return false;}
  // A plate keeps its draws: a new size or border width names that size's
  // mesh (created before anything changes), new colours retint them.
  for(std::size_t n=0;n<leaves.size();++n)if(part.meshed[n]&&part.plan[n].content!=leaves[n].content){const auto y=plainPlate(leaves[n]);need(y.has_value(),"A meshed Shortcut leaf is a plate");(void)mesh(*y,false);if(y->border>0)(void)mesh(*y,true);}
  for(std::size_t n=0;n<leaves.size();++n){auto&a=part.plan[n];auto&b=leaves[n];
   if(part.meshed[n]&&a.content!=b.content){const auto y=*plainPlate(b);for(std::uint32_t k=0;k<part.meshed[n];++k){auto&d=part.plates[part.plateDraw[n]+k];if(const auto&id=mesh(y,k==1);d.meshID!=id)d.meshID=id;d.linearTint=k?y.stroke:y.fill;}}
   else if(a.content!=b.content&&(!feedbackRole(b.role)||part.painted[n])){need(part.contentRevision!=UINT64_MAX,"Shortcut leaf revision exhausted");need(part.scene.updateLocalContent(b.id,++part.contentRevision,b.content,options),"Shortcut leaf repaint was refused");++stats.leafRepaints;}
   a=std::move(b);part.hidden[n]=next.hidden[n];part.bounds(n);part.feedback[n]={};
  }
  return true;
 }
 // A tint/rim leaf paints its source shape while its feedback shows and
 // returns to its transparent point once it faded out (feedbackPlaceholder).
 void paintFeedback(Part&p,std::size_t n,bool shown){const auto&s=p.plan[n];need(p.contentRevision!=UINT64_MAX,"Shortcut leaf revision exhausted");
  need(p.scene.updateLocalContent(s.id,++p.contentRevision,shown?s.content:feedbackPlaceholder(s.id),options),"Shortcut feedback repaint was refused");p.painted[n]=shown;++(shown?stats.feedbackPaints:stats.feedbackReleases);}
 void refresh(){entryCount=0;for(auto*p:{outgoing.get(),current.get()})if(p){if(!p->plates.empty())entries[entryCount++]={&p->underlay,p->plates};entries[entryCount++]={&p->scene,{}};}}
 void retire(std::unique_ptr<Part>p){if(!p)return;if(p->uploaded||p->uploadedMask)retired.push_back(std::move(p));else raster.remove(p->maskID);}
 template<class F>void each(F&&f){if(outgoing)f(*outgoing,true);if(current)f(*current,false);}
 bool releasePart(Part&p,Renderer&r){p.uploadedResourceRevision=0;if(!p.underlay.releaseResources(r)||!p.scene.releaseResources(r))return false;if(p.uploadedMask&&r.stats().initialized&&!r.removeTexture(p.maskID))return false;p.uploaded=false;p.uploadedMask=0;p.maskImage.reset();raster.remove(p.maskID);return true;}
};
NativeAppShortcutScene::NativeAppShortcutScene(m::AppShortcutState&s,const NativeAppShortcutAssets&a,LayerRasterizer&r,LayerRasterOptions o,m::ShortcutAppearance v):impl_(std::make_unique<Impl>(s,a,r,std::move(o),v)){}
NativeAppShortcutScene::~NativeAppShortcutScene(){auto&i=*impl_;i.each([&](auto&p,bool){i.raster.remove(p.maskID);});for(auto&p:i.retired)i.raster.remove(p->maskID);}
bool NativeAppShortcutScene::setAppearance(m::ShortcutAppearance a){auto&i=*impl_;if(a==i.appearance)return false;i.appearance=a;i.dirty=true;return true;}
bool NativeAppShortcutScene::setOriginalImages(m::ShortcutOriginalImages v){auto&i=*impl_;if(v==i.images)return false;i.images=std::move(v);i.dirty=true;return true;}
bool NativeAppShortcutScene::syncContent(double t){auto&i=*impl_;i.clock(t);const auto&preset=i.state.presetTransition();if(!preset.generation)i.presetGeneration=0;
 // A new typography generation (the font language) measures the captions
 // again before the host could repaint them at their earlier widths.
 if(!i.dirty&&i.revision==i.state.revision()&&(!i.current||i.current->scene.fontRevision()==i.raster.fontRevision()))return false;auto plan=m::prepareAppShortcutArtwork(i.state,i.assets.artwork(),i.appearance,i.images);
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
 need(layout.surfaces.size()<=LayerRasterizer::maximumEntries-i.raster.stats().entries,"Shortcut replacement exceeds shared raster budget");auto next=std::make_unique<Impl::Part>(i.raster,std::move(layout),i.options,[&](const PlainPlate&plate,bool ring)->const std::string&{return i.mesh(plate,ring);});if(newTrace){next->trace=preset;next->traceAccent=i.appearance.accent;}
 i.presetIcon=preset.icon;i.presetGeneration=preset.generation;i.presetStart=preset.start;
 if(transition.active&&transition.generation!=i.screenGeneration){i.retire(std::move(i.outgoing));i.outgoing=std::move(i.current);}else i.retire(std::move(i.current));if(!transition.active)i.retire(std::move(i.outgoing));i.current=std::move(next);i.revision=i.state.revision();i.screenGeneration=transition.generation;i.dirty=false;i.time=t;++i.stats.builds;i.refresh();const auto point=i.pointer;const auto pressed=i.pressed;i.pointer.reset();i.pressed=!pressed;setFeedback(point,pressed,i.reduced,t);return true;}
bool NativeAppShortcutScene::setFeedback(std::optional<core::Point>p,bool pressed,bool reduced,double t){auto&i=*impl_;i.clock(t);if(p)need(std::isfinite(p->x)&&std::isfinite(p->y),"Invalid shortcut pointer");if(i.pointer==p&&i.pressed==pressed&&i.reduced==reduced)return false;std::string_view action;double area=INFINITY;if(p&&i.current&&i.state.active()&&!i.state.transitioning())for(std::size_t n=0;n<i.current->plan.size();++n){const auto&s=i.current->plan[n];const auto&r=i.current->feedbackBounds[n];if(s.role==m::ShortcutSurfaceRole::tint&&!i.current->hidden[n]&&(!s.clip||s.clip->contains(*p))&&roundedContains(r,*p)&&r.width*r.height<=area){action=s.action;area=r.width*r.height;}}
 i.each([&](auto&part,bool old){for(std::size_t n=0;n<part.plan.size();++n){const auto&s=part.plan[n];if(s.role!=m::ShortcutSurfaceRole::tint&&s.role!=m::ShortcutSurfaceRole::rim)continue;const bool matches=!old&&s.action==action;const double target=matches?(s.role==m::ShortcutSurfaceRole::rim?1:pressed?1:.62):0;auto&track=part.feedback[n];if(track.target!=target||(reduced&&track.duration))track={Impl::value(track,t),target,t,reduced?0:pressed&&matches?.06:.14};if(target>0&&!part.painted[n])i.paintFeedback(part,n,true);}});i.pointer=p;i.pressed=pressed;i.reduced=reduced;i.time=t;return true;}
void NativeAppShortcutScene::updatePose(const AppShortcutScenePose&p){auto&i=*impl_;i.clock(p.time);need(!i.dirty&&i.revision==i.state.revision()&&i.current,"Synchronize Shortcut content before pose");need(p.world.finite()&&std::isfinite(p.opacity)&&p.opacity>=0&&p.opacity<=1&&p.ownerMasks.size()<=7,"Invalid Shortcut pose");if(p.shutter)validatePlaneShutter(*p.shutter);const auto sample=m::shortcutScreenSample(i.state.transition(),p.time);const auto preset=m::shortcutPresetTransform(i.state.presetTransition(),p.time);
 i.each([&](auto&part,bool old){const auto screen=p.world*M::translation(200,167)*(old?sample.outgoing:sample.incoming)*M::translation(-200,-167),inverse=core::source::inverseSourceMatrix(screen);std::optional<core::SubsectionCurvePath>path;if(sample.active)path=(old?i.assets.departure():i.assets.arrival())->sample(i.state.transition().direction,sample.phase);if(path!=part.maskPath){part.maskPath=path;part.maskDirty=bool(path);}part.alpha=path?std::optional(PlaneAlphaMask{inverse,part.maskImage?part.maskImage->bounds:R{0,0,400,334},part.maskID}):std::nullopt;
  if(part.trace)part.traceProgress=i.state.presetTransition().generation?m::shortcutPresetProgress(*part.trace,p.time):1;
  for(std::size_t n=0;n<part.plan.size();++n){const auto&s=part.plan[n];auto&v=part.placements[n];v.world=screen*((!old&&s.role==m::ShortcutSurfaceRole::preview)?preset:M{})*s.local;v.opacity=part.hidden[n]?0.f:p.opacity*((s.role==m::ShortcutSurfaceRole::tint||s.role==m::ShortcutSurfaceRole::rim)?float(Impl::value(part.feedback[n],p.time)):s.role==m::ShortcutSurfaceRole::presetTrace&&part.traceProgress==0?0.f:1.f);std::copy(p.ownerMasks.begin(),p.ownerMasks.end(),part.masks[n].begin());auto count=p.ownerMasks.size();if(s.clip)part.masks[n][count++]={inverse,*s.clip};v.masks=std::span(part.masks[n]).first(count);i.validator.world=v.world;i.validator.opacity=v.opacity;i.validator.masks.assign(v.masks.begin(),v.masks.end());i.validator.shutter=p.shutter?std::optional(*p.shutter):std::nullopt;validateDrawObject(i.validator);
   for(std::uint32_t k=0;k<part.meshed[n];++k){auto&d=part.plates[part.plateDraw[n]+k];d.world=v.world;d.opacity=v.opacity;d.masks.assign(v.masks.begin(),v.masks.end());d.shutter=i.validator.shutter;d.alphaMask=part.alpha;}}
 });i.each([&](auto&part,bool){part.scene.setPlacements(part.placements);part.scene.setGroupShutter(p.shutter?std::optional(*p.shutter):std::nullopt);part.scene.setGroupAlphaMask(part.alpha);});i.posed=true;i.time=p.time;++i.stats.poses;}
bool NativeAppShortcutScene::uploadAnimations(Renderer&r){auto&i=*impl_;need(i.posed&&r.stats().initialized&&(!i.owner||i.owner==&r),"Invalid Shortcut resource owner");bool changed{};
 // Feedback that finished fading out on the current screen gives back its
 // bitmap (an outgoing screen is retired whole after its handoff).
 if(i.current)for(std::size_t n=0;n<i.current->plan.size();++n){const auto&track=i.current->feedback[n];
  if(i.current->painted[n]&&track.target==0&&(track.duration<=0||i.time>=track.start+track.duration)){i.paintFeedback(*i.current,n,false);changed=true;}}
 // The plate meshes the composed parts draw (uploaded once per renderer).
 for(auto&m:i.meshes)if(!m.resident&&std::any_of(i.entries.begin(),i.entries.begin()+std::ptrdiff_t(i.entryCount),[&](const auto&e){return std::any_of(e.after.begin(),e.after.end(),[&](const auto&d){return d.meshID==m.id;});})){
  r.setMesh(m.id,1,{m.vertices,m.indices});m.resident=true;changed=true;}
 i.each([&](auto&part,bool){if(part.traceIndex&&part.traceProgress!=part.paintedTraceProgress){
  const auto&surface=part.plan[*part.traceIndex];const auto content=traceContent(surface.id,part.traceAccent,part.traceProgress);need(part.traceRevision!=UINT64_MAX,"Shortcut trace revision exhausted");
  part.scene.updateLocalContent(surface.id,++part.traceRevision,content,i.options);part.paintedTraceProgress=part.traceProgress;changed=true;
 }if(part.maskPath){if(part.maskDirty){auto options=i.options;options.paddingPoints=0;auto image=i.raster.rasterize(part.maskID,++part.maskRevision,maskContent(*part.maskPath),options);need(image->complete()&&std::size_t(image->width)*image->height<=LayerRasterizer::maximumPixels,"Unbounded or unsupported Shortcut source mask");part.maskImage=std::move(image);part.maskDirty=false;++i.stats.maskRasters;i.stats.maskBytes+=part.maskImage->straightRGBA.size();}if(part.uploadedMask!=part.maskRevision){r.setTexture(part.maskID,part.maskRevision,{part.maskImage->width,part.maskImage->height,part.maskImage->straightRGBA,TextureColorSpace::linear,TextureFilter::linear});part.uploadedMask=part.maskRevision;changed=true;}part.alpha->bounds=part.maskImage->bounds;part.scene.setGroupAlphaMask(part.alpha);for(auto&d:part.plates)d.alphaMask=part.alpha;}if(part.uploadedResourceRevision!=part.scene.resourceRevision()){part.scene.uploadResources(r);part.uploadedResourceRevision=part.scene.resourceRevision();}part.uploaded=true;});i.owner=&r;return changed;}
bool NativeAppShortcutScene::requiresFrames(double t)const{const auto&i=*impl_;i.clock(t);if(i.state.requiresFrames(t))return true;if(!i.state.active()||i.reduced)return false;const auto check=[&](const auto&p){return p&&std::any_of(p->feedback.begin(),p->feedback.end(),[&](const auto&v){return v.duration&&v.from!=v.target&&t<v.start+v.duration;});};return check(i.current)||check(i.outgoing);}
std::span<const LayerCompositionEntry>NativeAppShortcutScene::entries()const noexcept{return std::span(impl_->entries).first(impl_->entryCount);}
bool NativeAppShortcutScene::collectRetired(Renderer&r){auto&i=*impl_;for(auto&p:i.retired)if(!i.releasePart(*p,r))return false;i.retired.clear();bool okay=true;i.each([&](auto&p,bool){p.scene.collectRetiredResources(r);if(!p.maskPath&&p.uploadedMask){if(!r.stats().initialized||r.removeTexture(p.maskID)){p.uploadedMask=0;p.maskImage.reset();i.raster.remove(p.maskID);}else okay=false;}});return okay;}
bool NativeAppShortcutScene::releaseResources(Renderer&r){auto&i=*impl_;bool okay=collectRetired(r);i.each([&](auto&p,bool){okay=i.releasePart(p,r)&&okay;});
 for(auto&m:i.meshes)if(m.resident){if(!r.stats().initialized||r.removeMesh(m.id))m.resident=false;else okay=false;}if(okay)i.owner=nullptr;return okay;}
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
 const auto add=[&](const Impl::Part&p){++out.parts;out.meshPlates+=static_cast<std::size_t>(std::count_if(p.meshed.begin(),p.meshed.end(),[](std::uint8_t v){return v!=0;}));out.rasterSurfaces+=p.scene.report().surfaces;out.pixelBytes+=p.scene.report().pixelBytes+(p.maskImage?p.maskImage->straightRGBA.size():0);};
 if(i.current)add(*i.current);if(i.outgoing)add(*i.outgoing);for(const auto&p:i.retired)add(*p);return out;}
}
#endif
