#include "native/clipboard_scene.hpp"
#include "core/motion.hpp"
#include "core/source_color.hpp"
#include "core/subsection_transition.hpp"
#include <atomic>
#include "core/source_camera.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;using Rect=core::Rect;using Point=core::Point;using Matrix=core::Matrix4;using Color=std::array<double,4>;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
bool contains(Rect r,Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
std::optional<Rect>clip(Rect r){const auto c=ClipboardState::contentRect();const double x=std::max(r.x,c.x),y=std::max(r.y,c.y),w=std::min(r.x+r.width,c.x+c.width)-x,h=std::min(r.y+r.height,c.y+c.height)-y;if(w<=0||h<2)return {};return Rect{x,y,w,h};}
std::string action(std::uint64_t id,std::string_view verb){return "clipboard:"+std::to_string(id)+":"+std::string(verb);}
void validText(std::string_view s,std::size_t maximum=65536){need(s.size()<=maximum&&Json::validUtf8(s),"Invalid Clipboard presentation string");}
std::string countStatus(const ClipboardStrings&s,std::size_t count,std::size_t capacity){
    if(s.countPattern.empty())return std::to_string(count)+" / "+std::to_string(capacity)+s.countSuffix;
    std::string out;bool first{},second{};
    for(std::size_t n=0;n<s.countPattern.size();){if(s.countPattern.compare(n,3,"{0}")==0){out+=std::to_string(count);first=true;n+=3;}else if(s.countPattern.compare(n,3,"{1}")==0){out+=std::to_string(capacity);second=true;n+=3;}else{need(s.countPattern[n]!='{'&&s.countPattern[n]!='}',"Invalid Clipboard count pattern");out+=s.countPattern[n++];}}
    need(first&&second,"Clipboard count pattern needs both source arguments");return out;
}
void validate(const ClipboardStrings&s){for(const auto*p:{&s.heading,&s.emptyTitle,&s.emptyHelp,&s.countSuffix,&s.copied,&s.copyFailed,&s.pinned,&s.copy,&s.pin,&s.unpin,&s.remove,&s.clear,&s.cancel,&s.confirm,&s.keepPinned,&s.countPattern,&s.imagePrefix})validText(*p);for(const auto&kind:s.kinds)validText(kind);(void)countStatus(s,0,1);}

}
ClipboardState::ClipboardState(ClipboardActions callbacks,ClipboardStrings strings):callbacks_(std::move(callbacks)),strings_(std::move(strings)){
    validate(strings_);need(bool(callbacks_.snapshot),"Clipboard owner must supply isolated metadata snapshot");events_.reserve(8);actions_.reserve(24);refresh();
}
bool ClipboardState::setStrings(ClipboardStrings value){if(value==strings_)return false;validate(value);auto feedback=feedback_;if(feedback){if(feedbackKind_==1)feedback=value.copied;else if(feedbackKind_==2)feedback=value.copyFailed;}strings_=std::move(value);feedback_=std::move(feedback);rebuild();return true;}
void ClipboardState::event(EventKind kind,std::uint64_t id,double direction){events_.push_back({kind,id,direction,active_&&!reduced_});}
void ClipboardState::prepareImageRows(std::span<const std::uint64_t>ids){need(ids.size()<=14,"Clipboard image preparation exceeds old/incoming row bound");if(callbacks_.prepareImageRows)callbacks_.prepareImageRows(ids);}
void ClipboardState::activate(){if(active_)return;active_=true;refresh();}
void ClipboardState::deactivate(){if(!active_)return;active_=false;confirming_=false;feedback_.reset();event(EventKind::settle);rebuild();}
void ClipboardState::setReduceMotion(bool value){if(reduced_==value)return;reduced_=value;if(value)event(EventKind::settle);}
double ClipboardState::maximumOffset()const noexcept{return std::max(0.,double(rows_.size())*41-246);}
std::pair<std::size_t,std::size_t>ClipboardState::visibleRange()const noexcept{const auto first=std::min(rows_.size(),std::size_t(std::floor(offset_/41)));const auto end=std::min(rows_.size(),std::size_t(std::ceil((offset_+246)/41)));return {first,std::max(first,end)};}
std::optional<Rect>ClipboardState::rowRect(std::uint64_t id,bool clipped)const{for(std::size_t n=0;n<rows_.size();++n)if(rows_[n].id==id){const Rect r{12,43+double(n)*41-offset_,376,37};return clipped?clip(r):std::optional(r);}return {};}
void ClipboardState::refresh(){auto next=callbacks_.snapshot();need(next.rows.size()<=ClipboardHistory::maximum_capacity&&next.capacity>0&&next.capacity<=ClipboardHistory::maximum_capacity&&next.rows.size()<=next.capacity,"Clipboard metadata exceeds shared history bounds");std::set<std::uint64_t>ids;
    for(const auto&r:next.rows){need(r.id&&ids.insert(r.id).second,"Clipboard metadata identity missing/repeated");validText(r.preview,ClipboardHistory::maximum_text_bytes);need(unsigned(r.kind)<=unsigned(ClipboardKind::image),"Invalid Clipboard kind");}if(next.status)validText(*next.status);
    const auto first=std::size_t(std::floor(offset_/41));const auto anchor=offset_>0&&first<rows_.size()?std::optional(rows_[first].id):std::nullopt;const auto remainder=std::fmod(offset_,41);
    rows_=std::move(next.rows);capacity_=next.capacity;storeStatus_=std::move(next.status);feedback_.reset();if(anchor)for(std::size_t n=0;n<rows_.size();++n)if(rows_[n].id==*anchor){offset_=double(n)*41+remainder;break;}
    if(selected_&&!ids.contains(*selected_))selected_.reset();offset_=std::clamp(offset_,0.,maximumOffset());if(std::none_of(rows_.begin(),rows_.end(),[](const auto&r){return !r.pinned;}))confirming_=false;rebuild();
}
std::string ClipboardState::displayPreview(const ClipboardRow&r)const{if(r.kind==ClipboardKind::image)for(std::string_view prefix:{"Image · ","图像 · ","图片 · "})if(r.preview.starts_with(prefix))return strings_.imagePrefix+r.preview.substr(prefix.size());return r.preview;}
void ClipboardState::rebuild(){actions_.clear();if(confirming_){actions_.push_back({"clipboard:cancelClear",strings_.cancel,{218,299,67,27},true});actions_.push_back({"clipboard:confirmClear",strings_.confirm,{294,299,94,27},true});}else if(std::any_of(rows_.begin(),rows_.end(),[](const auto&r){return !r.pinned;}))actions_.push_back({"clipboard:clear",strings_.clear,{12,299,140,27},true});
    const auto[first,end]=visibleRange();for(auto n=first;n<end;++n){const auto&r=rows_[n];const auto full=rowRect(r.id,false),visible=rowRect(r.id);if(!visible)continue;const auto preview=displayPreview(r);actions_.push_back({action(r.id,"copy"),strings_.copy+preview,*visible,true});
        for(bool remove:{false,true})if(const auto bounds=clip({full->x+(remove?348:320),full->y+6,23,25}))actions_.push_back({action(r.id,remove?"remove":"pin"),(remove?strings_.remove:r.pinned?strings_.unpin:strings_.pin)+preview,*bounds,false});}
    status_=feedback_.value_or(storeStatus_.value_or(countStatus(strings_,rows_.size(),capacity_)));++revision_;
}
std::optional<std::string_view>ClipboardState::actionAt(Point p)const{for(auto it=actions_.rbegin();it!=actions_.rend();++it)if(contains(it->rect,p))return it->id;return {};}
std::optional<std::string_view>ClipboardState::feedbackActionAt(Point point)const{
    for(auto it=actions_.rbegin();it!=actions_.rend();++it){if(!contains(it->rect,point))continue;Rect bounds=it->rect;
        const std::string_view id=it->id;const auto tail=id.substr(10);const auto colon=tail.find(':');if(colon!=std::string_view::npos){std::uint64_t item{};const auto parsed=std::from_chars(tail.data(),tail.data()+colon,item);if(parsed.ec==std::errc{})if(auto row=rowRect(item,false)){const auto verb=tail.substr(colon+1);bounds=verb=="copy"?*row:Rect{row->x+(verb=="pin"?320:348),row->y+6,23,25};}}
        const double x=point.x-bounds.x,y=point.y-bounds.y;if(it->framed){const double corner=std::min(4.,std::min(bounds.width,bounds.height)/3);if(x+y<corner||(bounds.width-x)+(bounds.height-y)<corner)continue;}
        else{const double radius=std::min(3.,std::min(bounds.width,bounds.height)*.5),cx=std::clamp(x,radius,bounds.width-radius),cy=std::clamp(y,radius,bounds.height-radius);if(std::hypot(x-cx,y-cy)>radius)continue;}
        return it->id;
    }return {};
}
bool ClipboardState::mouseDown(Point p){if(!std::isfinite(p.x)||!std::isfinite(p.y)||!contains(bounds(),p))return false;if(auto id=actionAt(p))perform(*id);return true;}
bool ClipboardState::scroll(Point p,double delta){if(!contains(contentRect(),p)||!std::isfinite(delta))return false;return scrollBy(delta);}
bool ClipboardState::scrollBy(double delta){if(!std::isfinite(delta))return false;const double next=std::clamp(offset_+delta,0.,maximumOffset());if(next==offset_)return true;offset_=next;confirming_=false;feedback_.reset();event(EventKind::settle);rebuild();return true;}
void ClipboardState::reveal(std::size_t n){const double top=double(n)*41;if(top<offset_)offset_=top;else if(top+41>offset_+246)offset_=top+41-246;offset_=std::clamp(offset_,0.,maximumOffset());}
void ClipboardState::selectNext(int direction){if(rows_.empty())return;std::size_t index=direction<0?rows_.size()-1:0;if(selected_)for(std::size_t n=0;n<rows_.size();++n)if(rows_[n].id==*selected_){const auto next=std::clamp(std::int64_t(n)+std::int64_t(direction),std::int64_t(0),std::int64_t(rows_.size()-1));index=std::size_t(next);break;}selected_=rows_[index].id;reveal(index);feedback_.reset();confirming_=false;rebuild();}
void ClipboardState::copy(std::uint64_t id){if(!rowRect(id,false))return;selected_=id;confirming_=false;const bool ok=callbacks_.copy&&callbacks_.copy(id);auto next=callbacks_.snapshot();storeStatus_=std::move(next.status);feedbackKind_=ok?1:storeStatus_?0:2;feedback_=ok?strings_.copied:storeStatus_.value_or(strings_.copyFailed);rebuild();if(ok)event(EventKind::engage,id);}
void ClipboardState::copySelection(){if(selected_)copy(*selected_);}void ClipboardState::copyVisibleItem(unsigned index){if(index>=6)return;const auto[first,end]=visibleRange();unsigned visible{};for(auto n=first;n<end;++n)if(rowRect(rows_[n].id)){if(visible++==index){copy(rows_[n].id);return;}}}
void ClipboardState::remove(std::uint64_t id){if(callbacks_.remove&&callbacks_.remove(id)){refresh();event(EventKind::reflow);}}
void ClipboardState::deleteSelection(){if(selected_)remove(*selected_);}
void ClipboardState::perform(std::string_view value){const std::string owned(value);value=owned; // actionAt aliases actions_, rebuilt below
    if(value=="clipboard:clear"){if(std::none_of(rows_.begin(),rows_.end(),[](const auto&r){return !r.pinned;}))return;confirming_=true;rebuild();event(EventKind::toolbar,0,1);return;}
    if(value=="clipboard:cancelClear"){confirming_=false;rebuild();event(EventKind::toolbar,0,-1);return;}
    if(value=="clipboard:confirmClear"){if(!confirming_)return;confirming_=false;const bool changed=callbacks_.clearUnpinned&&callbacks_.clearUnpinned();refresh();if(changed)event(EventKind::reflow);event(EventKind::toolbar,0,-1);return;}
    constexpr std::string_view prefix="clipboard:";if(!value.starts_with(prefix))return;value.remove_prefix(prefix.size());const auto colon=value.find(':');if(colon==std::string_view::npos)return;std::uint64_t id{};const auto parsed=std::from_chars(value.data(),value.data()+colon,id);if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+colon||!rowRect(id,false))return;const auto verb=value.substr(colon+1);
    if(verb=="copy")copy(id);else if(verb=="remove")remove(id);else if(verb=="pin"&&callbacks_.togglePin&&callbacks_.togglePin(id)){refresh();event(EventKind::engage,id);}
}
std::vector<ClipboardState::Event>ClipboardState::takeEvents(){auto out=std::move(events_);events_.clear();events_.reserve(8);return out;}
namespace {
Color gray(double w,double a=1){return {w,w,w,a};}Color alpha(Color c,double a){c[3]=a;return c;}
Json color(Color c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
Json box(Rect r){return Json::Array{r.x,r.y,r.width,r.height};}
Json layer(std::string id,Rect r,const char*kind="layer"){return Json::Object{{"id",std::move(id)},{"kind",kind},{"bounds",box({0,0,r.width,r.height})},{"position",Json::Array{r.x,r.y}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
Json cmd(const char*op,std::initializer_list<Point>points={}){Json::Array out;for(auto p:points)out.push_back(Json::Array{p.x,p.y});return Json::Object{{"op",op},{"points",std::move(out)}};}
std::array<Point,6>cutPoints(Rect r,double c){return {{{r.x+c,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height-c},{r.x+r.width-c,r.y+r.height},{r.x,r.y+r.height},{r.x,r.y+c}}};}
Json::Array cut(Rect r,double c,double fraction=1){const auto p=cutPoints(r,c);Json::Array out{cmd("move",{p[0]})};if(fraction==1){for(unsigned n=1;n<6;++n)out.push_back(cmd("line",{p[n]}));out.push_back(cmd("close"));return out;}double length{};for(unsigned n=0;n<6;++n)length+=std::hypot(p[(n+1)%6].x-p[n].x,p[(n+1)%6].y-p[n].y);double left=length*fraction;for(unsigned n=0;n<6&&left>0;++n){const auto a=p[n],b=p[(n+1)%6];const auto d=std::hypot(b.x-a.x,b.y-a.y);if(left>=d){out.push_back(n==5?cmd("close"):cmd("line",{b}));left-=d;}else{out.push_back(cmd("line",{{a.x+(b.x-a.x)*left/d,a.y+(b.y-a.y)*left/d}}));break;}}return out;}
Json::Array rounded(Rect r,double c){const double x=r.x,y=r.y,w=r.width,h=r.height,k=c*.5522847498;return {cmd("move",{{x+w,y+h/2}}),cmd("line",{{x+w,y+h-c}}),cmd("cubic",{{x+w,y+h-c+k},{x+w-c+k,y+h},{x+w-c,y+h}}),cmd("line",{{x+c,y+h}}),cmd("cubic",{{x+c-k,y+h},{x,y+h-c+k},{x,y+h-c}}),cmd("line",{{x,y+c}}),cmd("cubic",{{x,y+c-k},{x+c-k,y},{x+c,y}}),cmd("line",{{x+w-c,y}}),cmd("cubic",{{x+w-c+k,y},{x+w,y+c-k},{x+w,y+c}}),cmd("close")};}
Json::Array ellipse(Rect r){const double x=r.x,y=r.y,w=r.width/2,h=r.height/2,k=.5522847498;return {cmd("move",{{x+2*w,y+h}}),cmd("cubic",{{x+2*w,y+h+k*h},{x+w+k*w,y+2*h},{x+w,y+2*h}}),cmd("cubic",{{x+w-k*w,y+2*h},{x,y+h+k*h},{x,y+h}}),cmd("cubic",{{x,y+h-k*h},{x+w-k*w,y},{x+w,y}}),cmd("cubic",{{x+w+k*w,y},{x+2*w,y+h-k*h},{x+2*w,y+h}}),cmd("close")};}
Json shape(std::string id,Rect r,Json::Array path,std::optional<Color>fill={},std::optional<Color>stroke={},double width=1,bool round=false){auto out=layer(std::move(id),r,"shape");out["shape"]=Json::Object{{"path",std::move(path)},{"fillColor",fill?color(*fill):Json{}},{"strokeColor",stroke?color(*stroke):Json{}},{"lineWidth",width},{"lineCap",round?"round":"butt"},{"lineJoin",round?"round":"miter"},{"miterLimit",10},{"fillRule","non-zero"}};return out;}
Json label(std::string id,Rect r,std::string value,double size,Color ink,double scale,const char*weight="regular",const char*align="left",bool wrap=false){validText(value,ClipboardHistory::maximum_text_bytes);auto out=layer(std::move(id),r,"text");out["contentsScale"]=std::min(4.,std::ceil(std::clamp(scale,1.,4.)*1.35));out["text"]=Json::Object{{"string",std::move(value)},{"fontSize",size},{"font",Json::Object{{"postScriptName",std::string_view(weight)=="semibold"?".AppleSystemUIFontDemi":std::string_view(weight)=="medium"?".AppleSystemUIFontMedium":".AppleSystemUIFont"},{"familyName",".AppleSystemUIFont"},{"pointSize",size},{"symbolicTraits",std::string_view(weight)=="semibold"?2:0}}},{"foregroundColor",color(ink)},{"alignment",align},{"wrapped",wrap},{"truncation",wrap?"none":"end"}};return out;}
struct Build {ClipboardPart part;Json::Array nodes;
    void add(Json node,std::string actionID={},bool rim=false,bool framed=false,bool outline=false,bool toolbar=false,float opacity=1){const auto&id=node["id"].string();const auto&p=node["position"].array();part.surfaces.push_back({id,Matrix::translation(p[0].number(),p[1].number()),opacity,std::move(actionID),rim,framed,outline,toolbar});nodes.push_back(std::move(node));}
    void feedback(std::string id,Rect r,bool framed,const Color&accent,bool toolbar=false){const Rect local{0,0,r.width,r.height},rim=framed?Rect{-2,-2,r.width+4,r.height+4}:local;const auto path=[&](Rect rect){return framed?cut(rect,std::min(4.,std::min(rect.width,rect.height)/3)):rounded(rect,std::min(3.,std::min(rect.width,rect.height)/2));};add(shape(id+"/tint",r,path(local),alpha(accent,.30)),id,false,framed,false,toolbar,0);add(shape(id+"/rim",r,path(rim),{},accent,.9),id,true,framed,false,toolbar,framed?.28f:0);}
    ClipboardPart finish(){
        if(part.rowID){Json::Array compact;std::vector<ClipboardSurface>surfaces;compact.reserve(nodes.size());surfaces.reserve(part.surfaces.size());
            for(std::size_t begin=0;begin<nodes.size();){auto end=begin+1;const auto&first=part.surfaces[begin];if(first.action.empty()&&!first.outline)while(end<nodes.size()&&part.surfaces[end].action.empty()&&!part.surfaces[end].outline)++end;
                if(end-begin==1){compact.push_back(std::move(nodes[begin]));surfaces.push_back(std::move(part.surfaces[begin]));}
                else{const auto id=first.id+"/static";auto group=layer(id,{0,0,376,37});Json::Array children;for(auto n=begin;n<end;++n)children.push_back(std::move(nodes[n]));group["children"]=std::move(children);group["allowsGroupOpacity"]=false;compact.push_back(std::move(group));surfaces.push_back({id,{},1,{},false,false,false,false});}begin=end;}
            nodes=std::move(compact);part.surfaces=std::move(surfaces);
        }
        part.layers=layer("clipboard.part",{0,0,0,0});part.layers["allowsGroupOpacity"]=false;part.layers["children"]=std::move(nodes);return std::move(part);
    }
};
void imageBinding(const Json&j){need(j.isObject(),"Missing source-prepared Clipboard image");if(j.contains("memoryImage")){need(j.object().size()==2&&j["memoryImage"].isString()&&!j["memoryImage"].string().empty()&&j["revision"].isNumber()&&j["revision"].integer()>0,"Invalid Clipboard memory image metadata");}else need(j.object().size()==2&&j["asset"].isString()&&!j["asset"].string().empty()&&j["sha256"].isString()&&j["sha256"].string().size()==64&&j["sha256"].string().find_first_not_of("0123456789abcdef")==std::string::npos,"Clipboard image requires pinned asset metadata");}
Json kindImage(const ClipboardRow&r,const ClipboardAppearance&s,const ClipboardImages&images,std::string id){auto out=layer(std::move(id),{37,6,25,25});if(!r.thumbnail.isNull()){imageBinding(r.thumbnail);out["contents"]=r.thumbnail;out["contentsGravity"]="resizeAspect";out["contentsScale"]=s.scale;return out;}
    const auto&icon=r.kind==ClipboardKind::text?images.text:images.files;need(bool(icon),"Clipboard source game icon must be supplied explicitly");const auto resource=r.kind==ClipboardKind::text?"Operational_Manual_icon":"Depot_icon";need(icon->sourceResource==resource&&icon->pixels==unsigned(std::ceil(25*s.scale))&&icon->tint==Color{.14,.14,.14,.78},"Clipboard source icon tint/size/resource mismatch");imageBinding(icon->contents);out["contents"]=icon->contents;out["contentsGravity"]="resizeAspect";out["contentsScale"]=s.scale;return out;
}
}
ClipboardScenePlan prepareClipboardScene(const ClipboardState&state,const ClipboardAppearance&s,const ClipboardImages&images){need(std::isfinite(s.scale)&&s.scale>=1&&s.scale<=8,"Invalid Clipboard render scale");for(auto c:s.accent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid Clipboard accent");ClipboardScenePlan result;const auto primary=gray(s.dark?.94:.11),muted=gray(s.dark?.68:.38),ink=gray(.14);const auto&strings=state.strings();
    Build empty;if(state.rows().empty()){empty.add(label("clipboard.empty",{24,145,352,46},strings.emptyTitle,14,primary,s.scale,"regular","center",true));empty.add(label("clipboard.help",{24,197,352,32},strings.emptyHelp,10.5,muted,s.scale,"regular","center",true));}result.collection=empty.finish();
    Build fore;auto heading=label("clipboard.heading",{12,0,376,20},(strings.heading.starts_with("//")?strings.heading:"// "+strings.heading),15,primary,s.scale,"semibold","natural");heading["text"]["truncation"]="none";fore.add(std::move(heading));auto statusColor=muted;if(state.copiedFeedback()){statusColor=s.accent;if(!s.dark){const auto blend=core::source::sourceBlackBlend({s.accent[0],s.accent[1],s.accent[2]},.4);std::copy(blend.begin(),blend.end(),statusColor.begin());}}fore.add(label("clipboard.status",{12,22,376,13},state.status(),9.5,statusColor,s.scale,"regular","natural"));
    if(state.confirmingClear())fore.add(label("clipboard.keepPinned",{12,306,202,15},strings.keepPinned,10.5,primary,s.scale),{},false,false,false,true);
    for(const auto&a:state.actions())if(a.id=="clipboard:clear"||a.id=="clipboard:cancelClear"||a.id=="clipboard:confirmClear"){const auto&r=a.rect;fore.add(shape(a.id+"/plate",r,cut({0,0,r.width,r.height},4),a.id=="clipboard:confirmClear"?s.accent:gray(s.dark?.82:.9),gray(s.dark?.93:.38,.65),.6),{},false,false,false,true);fore.feedback(a.id,r,true,s.accent,true);fore.add(label(a.id+"/label",{r.x+4,r.y+6,r.width-8,17},a.label,11,ink,s.scale,"semibold","center"),{},false,false,false,true);}result.foreground=fore.finish();
    Build scroll;if(state.maximumOffset()>0){const double h=std::max(24.,246.*246/(double(state.rows().size())*41));auto node=layer("clipboard.scrollbar",{392,41+(246-h)*state.scrollOffset()/state.maximumOffset(),2,h});node["cornerRadius"]=1;node["backgroundColor"]=color(alpha(s.accent,.55));scroll.add(std::move(node));}result.scrollbar=scroll.finish();
    const auto[first,end]=state.visibleRange();result.rows.reserve(end-first);for(auto n=first;n<end;++n){const auto&r=state.rows()[n];const auto prefix="clipboard.row."+std::to_string(r.id);Build row;row.part.rowID=r.id;row.part.full=*state.rowRect(r.id,false);const bool selected=state.selected()==r.id;const Rect box{0,0,376,37};row.add(shape(prefix+"/fill",box,cut(box,5),gray(s.dark?.77:.90)));row.add(shape(prefix+"/outline",box,cut(box,5),{},selected?s.accent:gray(s.dark?.90:.37,.7),selected?1.4:.6),{},false,false,true);row.feedback(action(r.id,"copy"),box,true,s.accent);
        const auto number=n+1;row.add(label(prefix+"/number",{7,10,23,18},(number<10?"0":"")+std::to_string(number),10.5,alpha(ink,.65),s.scale,"semibold","center"));
        if(!r.thumbnail.isNull()||r.kind==ClipboardKind::text||r.kind==ClipboardKind::files)row.add(kindImage(r,s,images,prefix+"/kind"));else{Json::Array path;if(r.kind==ClipboardKind::url){path=rounded({36,10,16,12},5);auto second=rounded({46,16,16,12},5);path.insert(path.end(),second.begin(),second.end());}else{path={cmd("move",{{37,8}}),cmd("line",{{62,8}}),cmd("line",{{62,29}}),cmd("line",{{37,29}}),cmd("close"),cmd("move",{{39,25}}),cmd("line",{{47,17}}),cmd("line",{{53,23}}),cmd("line",{{58,19}}),cmd("line",{{61,22}})};auto circle=ellipse({53,11,4,4});path.insert(path.end(),circle.begin(),circle.end());}row.add(shape(prefix+"/kind",{},std::move(path),{},alpha(ink,.78),1.5,true));}
        row.add(label(prefix+"/preview",{71,5,243,17},state.displayPreview(r),11.5,ink,s.scale,"medium"));row.add(label(prefix+"/kindTitle",{71,23,243,11},strings.kinds[unsigned(r.kind)]+(r.pinned?strings.pinned:""),8.5,alpha(ink,.64),s.scale));row.feedback(action(r.id,"pin"),{320,6,23,25},false,s.accent);row.feedback(action(r.id,"remove"),{348,6,23,25},false,s.accent);
        if(r.pinned)row.add(shape(prefix+"/pinHalo",{},ellipse({321,7,22,23}),s.accent));Json::Array pin{cmd("move",{{327,11}}),cmd("line",{{336,11}}),cmd("move",{{329,11}}),cmd("line",{{329,17}}),cmd("line",{{326,20}}),cmd("line",{{337,20}}),cmd("line",{{334,17}}),cmd("line",{{334,11}}),cmd("move",{{331.5,20}}),cmd("line",{{331.5,27}})};row.add(shape(prefix+"/pin",{},std::move(pin),{},alpha(ink,r.pinned?1:.68),1.1,true));row.add(shape(prefix+"/remove",{},Json::Array{cmd("move",{{356,13}}),cmd("line",{{365,23}}),cmd("move",{{365,13}}),cmd("line",{{356,23}})},{},alpha(ink,.76),1.1,true));result.rows.push_back(row.finish());}
    return result;
}
#ifdef _WIN32
namespace {
Json sampledMaskLayer(const core::SubsectionCurvePath&p){Json::Array path;std::size_t at{};const auto point=[&](){const Point value{p.coordinates[at],p.coordinates[at+1]};at+=2;return value;};
    for(std::size_t n=0;n<p.opcodeCount;++n)switch(p.opcodes[n]){case 0:path.push_back(cmd("move",{point()}));break;case 1:path.push_back(cmd("line",{point()}));break;case 2:{const auto a=point(),b=point();path.push_back(cmd("quadratic",{a,b}));break;}case 3:{const auto a=point(),b=point(),c=point();path.push_back(cmd("cubic",{a,b,c}));break;}case 4:path.push_back(cmd("close"));break;default:need(false,"Invalid Clipboard source mask opcode");}
    need(at==p.coordinateCount,"Invalid Clipboard source mask coordinate count");return shape("clipboard.curved-mask",{0,0,376,246},std::move(path),Color{1,1,1,1});
}
}
struct NativeClipboardScene::Impl {
    struct Track {double from{},target{},start{},duration{};};
    struct Part {
        ClipboardPart plan;LayerScene scene;std::vector<LayerPlacement>placements;std::vector<std::array<Track,2>>feedback;
        std::array<PlaneMask,8>masks{};std::vector<LayerPlacement>active;std::optional<double>engageStart,reflowStart;double reflowFrom{};double stroke{1},uploadedStroke{1};std::uint64_t strokeRevision{};
        Part(LayerRasterizer&r,ClipboardPart p,const LayerRasterOptions&o):plan(std::move(p)),scene(r),placements(plan.surfaces.size()),feedback(plan.surfaces.size()){
            scene.load(plan.layers,o);need(scene.report().unsupported.empty(),"Clipboard artwork contains unsupported local effects");active.reserve(placements.size());
            for(std::size_t n=0;n<placements.size();++n){const auto&s=plan.surfaces[n];placements[n].surface=scene.surfaceIndex(s.id).value_or(std::size_t(-1));placements[n].world=s.local;placements[n].opacity=0;feedback[n][0].from=feedback[n][0].target=0;feedback[n][1].from=feedback[n][1].target=s.framed?.28:0;if(placements[n].surface!=std::size_t(-1))active.push_back(placements[n]);}scene.setPlacements(active);scene.prepareDraws();
        }
    };
    ClipboardState&state;LayerRasterizer&raster;LayerRasterOptions options;ClipboardAppearance appearance;ClipboardImages images;
    std::unique_ptr<Part>collection,foreground,scrollbar;std::vector<std::unique_ptr<Part>>rows,retired;std::vector<LayerCompositionEntry>entries;
    std::shared_ptr<const core::SubsectionMaskSampler>sampler;std::optional<double>revealStart;double revealDirection{-1};
    std::optional<core::SubsectionCurvePath>maskPath;std::optional<PlaneAlphaMask>alphaMask;std::shared_ptr<const LayerRasterImage>maskImage;
    std::string maskID;std::uint64_t maskRevision{},uploadedMask{};bool maskDirty{};Renderer*resourceOwner{};
    std::uint64_t revision{};bool appearanceDirty{true};double lastTime{};std::optional<double>toolbarStart;double toolbarDirection{1};std::optional<std::string>hover;bool pressed{};ClipboardSceneStats stats;DrawObject validation;
    Impl(ClipboardState&s,LayerRasterizer&r,LayerRasterOptions o,ClipboardAppearance a,ClipboardImages b,std::shared_ptr<const core::SubsectionMaskSampler>samples):state(s),raster(r),options(std::move(o)),appearance(a),images(std::move(b)),sampler(std::move(samples)){static std::atomic<std::uint64_t>identity{};maskID="cbmask"+std::to_string(++identity);if(sampler)need(sampler->sourceViewport()==Rect{0,0,376,246},"Clipboard mask must match original rows.bounds");rows.reserve(7);retired.reserve(10);entries.reserve(10);validation.sourceID="clipcheck";validation.masks.reserve(8);}
    void time(double t)const{need(std::isfinite(t)&&t>=lastTime,"Clipboard requires finite monotonic owner time");}
    template<class F>void each(F&&f){if(collection)f(*collection);for(auto&p:rows)f(*p);if(scrollbar)f(*scrollbar);if(foreground)f(*foreground);}
    static double ease(double p){return core::CubicTiming{0,0,.58,1}.value(p);}
    static double sample(Track t,double now){return t.duration>0?t.from+(t.target-t.from)*ease((now-t.start)/t.duration):t.target;}
    static double y(const Part&p,double now){return p.reflowStart?p.reflowFrom+(p.plan.full.y-p.reflowFrom)*ease((now-*p.reflowStart)/.20):p.plan.full.y;}
};
NativeClipboardScene::NativeClipboardScene(ClipboardState&s,LayerRasterizer&r,LayerRasterOptions o,ClipboardAppearance a,ClipboardImages images,std::shared_ptr<const core::SubsectionMaskSampler>samples):impl_(std::make_unique<Impl>(s,r,std::move(o),a,std::move(images),std::move(samples))){}
NativeClipboardScene::~NativeClipboardScene(){if(impl_)impl_->raster.remove(impl_->maskID);}
void NativeClipboardScene::setAppearance(ClipboardAppearance a,ClipboardImages images){auto&i=*impl_;if(i.appearance==a&&i.images.text.has_value()==images.text.has_value()&&i.images.files.has_value()==images.files.has_value()){
    const auto same=[](const auto&x,const auto&y){return !x||(x->contents==y->contents&&x->sourceResource==y->sourceResource&&x->pixels==y->pixels&&x->tint==y->tint);};if(same(i.images.text,images.text)&&same(i.images.files,images.files))return;}
    i.appearance=a;i.images=std::move(images);i.appearanceDirty=true;
}
bool NativeClipboardScene::syncContent(double time){auto&i=*impl_;i.time(time);if(i.revision==i.state.revision()&&!i.appearanceDirty&&!i.state.hasPendingEvents())return false;need(i.retired.empty(),"Publish and collect Clipboard prior generation before replacement");
    std::array<std::uint64_t,14> imageRows{};std::size_t imageCount{};const auto addImageRow=[&](std::uint64_t id){if(std::find(imageRows.begin(),imageRows.begin()+static_cast<std::ptrdiff_t>(imageCount),id)==imageRows.begin()+static_cast<std::ptrdiff_t>(imageCount))imageRows[imageCount++]=id;};
    for(const auto&row:i.rows)addImageRow(row->plan.rowID);const auto visible=i.state.visibleRange();for(auto n=visible.first;n<visible.second;++n)addImageRow(i.state.rows()[n].id);i.state.prepareImageRows(std::span(imageRows).first(imageCount));
    auto plan=prepareClipboardScene(i.state,i.appearance,i.images);std::size_t needed{};const auto same=[](const auto&p,const ClipboardPart&q){return p&&p->plan.layers==q.layers;};
    const auto count=[&](const auto&p,const ClipboardPart&q){if(!same(p,q))needed+=q.surfaces.size();};count(i.collection,plan.collection);count(i.foreground,plan.foreground);count(i.scrollbar,plan.scrollbar);
    for(const auto&r:plan.rows){const auto old=std::find_if(i.rows.begin(),i.rows.end(),[&](const auto&p){return p->plan.rowID==r.rowID;});if(old==i.rows.end()||!same(*old,r))needed+=r.surfaces.size();}need(needed<=LayerRasterizer::maximumEntries-i.raster.stats().entries,"Clipboard replacement exceeds shared raster budget");
    const auto inherit=[](Impl::Part&next,const Impl::Part&prior){
        next.engageStart=prior.engageStart;next.reflowStart=prior.reflowStart;next.reflowFrom=prior.reflowFrom;next.stroke=prior.stroke;
        for(std::size_t n=0;n<next.plan.surfaces.size();++n)for(std::size_t old=0;old<prior.plan.surfaces.size();++old)if(next.plan.surfaces[n].id==prior.plan.surfaces[old].id)next.feedback[n]=prior.feedback[old];
    };
    const auto build=[&](const auto&p,ClipboardPart&q){if(same(p,q))return std::unique_ptr<Impl::Part>{};auto next=std::make_unique<Impl::Part>(i.raster,std::move(q),i.options);if(p)inherit(*next,*p);return next;};
    auto collection=build(i.collection,plan.collection),foreground=build(i.foreground,plan.foreground),scrollbar=build(i.scrollbar,plan.scrollbar);
    std::array<std::unique_ptr<Impl::Part>,7>built;std::array<std::size_t,7>old{};old.fill(7);std::array<double,7>previousY{};
    for(std::size_t n=0;n<plan.rows.size();++n){previousY[n]=plan.rows[n].full.y+30;const Impl::Part*previous{};for(std::size_t k=0;k<i.rows.size();++k)if(i.rows[k]->plan.rowID==plan.rows[n].rowID){previous=i.rows[k].get();previousY[n]=Impl::y(*previous,time);if(same(i.rows[k],plan.rows[n]))old[n]=k;break;}
        if(old[n]==7){built[n]=std::make_unique<Impl::Part>(i.raster,plan.rows[n],i.options);
            if(previous)inherit(*built[n],*previous);
        }
    }
    if(!i.sampler)for(const auto&e:i.state.pendingEvents())if(e.kind==ClipboardState::EventKind::reflow&&e.animated){bool moved{};for(std::size_t n=0;n<plan.rows.size();++n)moved|=previousY[n]!=plan.rows[n].full.y;need(moved,"Animated Clipboard subsection needs its original normalized mask asset");}auto events=i.state.takeEvents();std::vector<std::unique_ptr<Impl::Part>>next;next.reserve(7);const auto replace=[&](auto&current,auto&candidate){if(candidate){if(current)i.retired.push_back(std::move(current));current=std::move(candidate);++i.stats.builds;}};
    replace(i.collection,collection);replace(i.foreground,foreground);replace(i.scrollbar,scrollbar);
    for(std::size_t n=0;n<plan.rows.size();++n){if(old[n]!=7){next.push_back(std::move(i.rows[old[n]]));next.back()->plan.full=plan.rows[n].full;}else{next.push_back(std::move(built[n]));++i.stats.builds;}}
    for(auto&p:i.rows)if(p)i.retired.push_back(std::move(p));i.rows=std::move(next);i.entries.clear();i.each([&](auto&p){i.entries.push_back({&p.scene,{}});});
    for(const auto&e:events){if(e.kind==ClipboardState::EventKind::settle){i.revealStart.reset();i.toolbarStart.reset();for(auto&p:i.rows){p->engageStart.reset();p->reflowStart.reset();p->stroke=1;}}
        else if(e.kind==ClipboardState::EventKind::toolbar){i.toolbarStart=e.animated?std::optional(time):std::nullopt;i.toolbarDirection=e.direction;}
        else if(e.kind==ClipboardState::EventKind::engage){for(auto&p:i.rows)if(p->plan.rowID==e.id){p->engageStart=e.animated?std::optional(time):std::nullopt;p->stroke=e.animated?0:1;}}
        else if(e.kind==ClipboardState::EventKind::reflow&&e.animated){i.revealStart.reset();bool moved{};for(std::size_t n=0;n<i.rows.size();++n){auto&p=*i.rows[n];if(previousY[n]!=p.plan.full.y){p.reflowFrom=previousY[n];p.reflowStart=time;moved=true;}}if(!moved){need(bool(i.sampler),"Animated Clipboard subsection needs its original normalized mask asset");i.revealStart=time;i.revealDirection=-1;}}
    }
    // Source repaint schedules one highlight refresh. Restore the current
    // target on new/recycled leaves now; pointer motion never rebuilds artwork.
    i.each([&](auto&p){for(std::size_t n=0;n<p.plan.surfaces.size();++n){const auto&s=p.plan.surfaces[n];if(s.action.empty())continue;const bool hit=i.hover&&*i.hover==s.action;auto&t=p.feedback[n][s.rim?1:0];const double target=s.rim?(hit?1:s.framed?.28:0):(hit?(i.pressed?1:.62):0);if(t.target!=target)t={Impl::sample(t,time),target,time,i.state.reduceMotion()?0:i.pressed&&hit?.06:.14};}});
    i.revision=i.state.revision();i.appearanceDirty=false;i.lastTime=time;return true;
}
bool NativeClipboardScene::setFeedback(std::optional<std::string_view>actionID,bool pressed,double time){auto&i=*impl_;i.time(time);const bool changed=i.state.reduceMotion()||i.hover.has_value()!=actionID.has_value()||(i.hover&&*i.hover!=*actionID)||i.pressed!=pressed;if(!changed)return false;
    i.each([&](auto&p){for(std::size_t n=0;n<p.plan.surfaces.size();++n){const auto&s=p.plan.surfaces[n];if(s.action.empty())continue;const bool old=i.hover&&*i.hover==s.action,hit=actionID&&*actionID==s.action;if(old==hit&&(!hit||i.pressed==pressed)&&!i.state.reduceMotion())continue;auto&t=p.feedback[n][s.rim?1:0];const double target=s.rim?(hit?1:s.framed?.28:0):(hit?(pressed?1:.62):0);t={Impl::sample(t,time),target,time,i.state.reduceMotion()?0:pressed&&hit?.06:.14};}});
    i.hover=actionID?std::optional(std::string(*actionID)):std::nullopt;i.pressed=pressed;i.lastTime=time;return true;
}
bool NativeClipboardScene::updatePose(const NativeClipboardPose&pose){auto&i=*impl_;i.time(pose.time);need(i.revision==i.state.revision()&&!i.appearanceDirty,"Synchronize Clipboard before pose");need(pose.world.finite()&&std::isfinite(pose.opacity)&&pose.opacity>=0&&pose.opacity<=1&&pose.ownerMasks.size()<=7,"Invalid Clipboard placement");if(pose.shutter)validatePlaneShutter(*pose.shutter);const PlaneMask clipMask{core::source::inverseSourceMatrix(pose.world),ClipboardState::contentRect()};
    std::optional<core::SubsectionCurvePath>nextPath;Matrix collectionWorld=pose.world;if(i.revealStart&&pose.time-*i.revealStart<.26){const double elapsed=pose.time-*i.revealStart;collectionWorld=pose.world*Matrix::translation(200,164)*core::sampleSubsectionTransform(i.revealDirection,elapsed).sublayerTransform*Matrix::translation(-200,-164);nextPath=i.sampler->sample(i.revealDirection,elapsed/.26);}
    std::optional<PlaneAlphaMask>nextAlpha;if(nextPath)nextAlpha=PlaneAlphaMask{core::source::inverseSourceMatrix(pose.world*Matrix::translation(12,41)),i.maskImage?i.maskImage->bounds:Rect{0,0,376,246},i.maskID};
    i.each([&](auto&p){const bool row=p.plan.rowID!=0,clipped=row||&p==i.collection.get();const auto base=row?collectionWorld*Matrix::translation(p.plan.full.x,Impl::y(p,pose.time)):clipped?collectionWorld:pose.world;double engage=0;if(p.engageStart)engage=1-Impl::ease((pose.time-*p.engageStart)/.18);const double toolbar=i.toolbarStart?1-Impl::ease((pose.time-*i.toolbarStart)/.18):0;std::copy(pose.ownerMasks.begin(),pose.ownerMasks.end(),p.masks.begin());const auto count=pose.ownerMasks.size()+std::size_t(clipped);if(clipped)p.masks[pose.ownerMasks.size()]=clipMask;
        for(std::size_t n=0;n<p.placements.size();++n){const auto&s=p.plan.surfaces[n];auto&value=p.placements[n];value.world=base*(row?Matrix::translation(4*engage,0,-8*engage):s.toolbar?Matrix::translation(10*i.toolbarDirection*toolbar,0,-8*toolbar):Matrix{})*s.local;value.opacity=pose.opacity*(s.action.empty()?s.opacity:float(Impl::sample(p.feedback[n][s.rim?1:0],pose.time)));value.masks={p.masks.data(),count};i.validation.world=value.world;i.validation.opacity=value.opacity;i.validation.masks.assign(value.masks.begin(),value.masks.end());i.validation.shutter=pose.shutter;i.validation.alphaMask=clipped?nextAlpha:std::nullopt;validateDrawObject(i.validation);}
    });
    i.each([&](auto&p){p.active.clear();for(const auto&v:p.placements)if(v.surface!=std::size_t(-1))p.active.push_back(v);p.scene.setPlacements(p.active);p.scene.setGroupShutter(pose.shutter);p.scene.setGroupAlphaMask((p.plan.rowID||&p==i.collection.get())?nextAlpha:std::nullopt);if(p.engageStart){p.stroke=Impl::ease((pose.time-*p.engageStart)/.18);if(pose.time-*p.engageStart>=.18)p.engageStart.reset();}if(p.reflowStart&&pose.time-*p.reflowStart>=.20)p.reflowStart.reset();});if(i.toolbarStart&&pose.time-*i.toolbarStart>=.18)i.toolbarStart.reset();if(i.revealStart&&pose.time-*i.revealStart>=.26)i.revealStart.reset();if(nextPath!=i.maskPath){i.maskPath=std::move(nextPath);i.maskDirty=bool(i.maskPath);if(i.maskPath&&i.maskPath->topologyGap)++i.stats.topologyGapSamples;}i.alphaMask=std::move(nextAlpha);i.lastTime=pose.time;++i.stats.poses;return true;
}
bool NativeClipboardScene::uploadAnimations(Renderer&renderer){auto&i=*impl_;need(!i.resourceOwner||i.resourceOwner==&renderer,"Clipboard animation belongs to another renderer");bool changed{};
    if(i.maskPath){if(i.maskDirty){auto options=i.options;options.paddingPoints=0;auto image=i.raster.rasterize(i.maskID,++i.maskRevision,sampledMaskLayer(*i.maskPath),options);need(image->complete()&&image->width<=1600&&image->height<=1100,"Unsupported/unbounded Clipboard curve raster");i.maskImage=std::move(image);i.maskDirty=false;++i.stats.maskRasters;}
        if(i.uploadedMask!=i.maskRevision){renderer.setTexture(i.maskID,i.maskRevision,{i.maskImage->width,i.maskImage->height,i.maskImage->straightRGBA,TextureColorSpace::linear,TextureFilter::linear});i.uploadedMask=i.maskRevision;i.resourceOwner=&renderer;++i.stats.maskUploads;changed=true;}
        i.alphaMask->bounds=i.maskImage->bounds;i.collection->scene.setGroupAlphaMask(i.alphaMask);for(auto&p:i.rows)p->scene.setGroupAlphaMask(i.alphaMask);
    }
    for(auto&p:i.rows)if(p->uploadedStroke!=p->stroke){for(std::size_t n=0;n<p->plan.surfaces.size();++n)if(p->plan.surfaces[n].outline){auto node=p->plan.layers["children"].array()[n];node["shape"]["path"]=cut({0,0,376,37},5,p->stroke);p->scene.updateLocalContent(p->plan.surfaces[n].id,++p->strokeRevision,node,i.options);++i.stats.outlineRasters;changed=true;}p->uploadedStroke=p->stroke;}return changed;}
bool NativeClipboardScene::requiresFrames(double time)const{auto&i=*impl_;i.time(time);if(i.revealStart&&time-*i.revealStart<.26)return true;if(i.toolbarStart&&time-*i.toolbarStart<.18)return true;bool value{};i.each([&](const auto&p){value|=p.engageStart&&time-*p.engageStart<.18;value|=p.reflowStart&&time-*p.reflowStart<.20;for(const auto&pair:p.feedback)for(auto track:pair)value|=track.duration>0&&track.from!=track.target&&time<track.start+track.duration;});return value;}
std::span<const LayerCompositionEntry>NativeClipboardScene::entries()const noexcept{return impl_->entries;}
bool NativeClipboardScene::collectRetired(Renderer&r){auto&i=*impl_;for(auto&p:i.retired)if(!p->scene.releaseResources(r))return false;i.retired.clear();if(!i.maskPath&&i.uploadedMask&&(!r.stats().initialized||r.removeTexture(i.maskID))){i.uploadedMask=0;i.maskImage.reset();i.raster.remove(i.maskID);}return true;}
bool NativeClipboardScene::releaseResources(Renderer&r){auto&i=*impl_;bool okay=collectRetired(r);i.each([&](auto&p){okay=p.scene.releaseResources(r)&&okay;});if(i.uploadedMask){if(!r.stats().initialized||r.removeTexture(i.maskID)){i.uploadedMask=0;i.maskImage.reset();i.raster.remove(i.maskID);}else okay=false;}return okay;}
ClipboardSceneStats NativeClipboardScene::stats()const noexcept{auto result=impl_->stats;result.rows=impl_->rows.size();result.retiredParts=impl_->retired.size();return result;}
#endif
} // namespace endfield::native
