#include "modules/event_log_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::modules {
namespace {
using Json=ehud::data::Json;using Rect=core::Rect;using Point=core::Point;using Matrix=core::Matrix4;using Color=std::array<double,4>;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
Color gray(double w,double a=1){return {w,w,w,a};}Color alpha(Color c,double a){c[3]=a;return c;}
Json color(Color c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
Json box(Rect r){return Json::Array{r.x,r.y,r.width,r.height};}
Json layer(std::string id,Rect r,const char*kind="layer"){return Json::Object{{"id",std::move(id)},{"kind",kind},{"bounds",box({0,0,r.width,r.height})},{"position",Json::Array{r.x,r.y}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
Json cmd(const char*op,std::initializer_list<Point>points={}){Json::Array out;for(auto p:points)out.push_back(Json::Array{p.x,p.y});return Json::Object{{"op",op},{"points",std::move(out)}};}
std::array<Point,6>cutPoints(Rect r,double c){return {{{r.x+c,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height-c},{r.x+r.width-c,r.y+r.height},{r.x,r.y+r.height},{r.x,r.y+c}}};}
Json::Array cut(Rect r,double c,double fraction=1){const auto p=cutPoints(r,c);Json::Array out{cmd("move",{p[0]})};if(fraction==1){for(unsigned n=1;n<6;++n)out.push_back(cmd("line",{p[n]}));out.push_back(cmd("close"));return out;}double length{};for(unsigned n=0;n<6;++n)length+=std::hypot(p[(n+1)%6].x-p[n].x,p[(n+1)%6].y-p[n].y);double left=length*fraction;for(unsigned n=0;n<6&&left>0;++n){const auto a=p[n],b=p[(n+1)%6];const auto d=std::hypot(b.x-a.x,b.y-a.y);if(left>=d){out.push_back(n==5?cmd("close"):cmd("line",{b}));left-=d;}else{out.push_back(cmd("line",{{a.x+(b.x-a.x)*left/d,a.y+(b.y-a.y)*left/d}}));break;}}return out;}
Json shape(std::string id,Rect r,Json::Array path,std::optional<Color>fill={},std::optional<Color>stroke={},double width=1,bool round=false){auto out=layer(std::move(id),r,"shape");out["shape"]=Json::Object{{"path",std::move(path)},{"fillColor",fill?color(*fill):Json{}},{"strokeColor",stroke?color(*stroke):Json{}},{"lineWidth",width},{"lineCap",round?"round":"butt"},{"lineJoin",round?"round":"miter"},{"miterLimit",10},{"fillRule","non-zero"}};return out;}
Json label(std::string id,Rect r,std::string value,double size,Color ink,double scale,const char*weight="regular",const char*align="left",bool wrap=false){need(value.size()<=4096&&Json::validUtf8(value),"Invalid bounded Event Log text");auto out=layer(std::move(id),r,"text");out["contentsScale"]=std::min(4.,std::ceil(std::clamp(scale,1.,4.)*1.35));out["text"]=Json::Object{{"string",std::move(value)},{"fontSize",size},{"font",Json::Object{{"postScriptName",std::string_view(weight)=="semibold"?".AppleSystemUIFontDemi":std::string_view(weight)=="medium"?".AppleSystemUIFontMedium":".AppleSystemUIFont"},{"familyName",".AppleSystemUIFont"},{"pointSize",size},{"symbolicTraits",std::string_view(weight)=="semibold"?2:0}}},{"foregroundColor",color(ink)},{"alignment",align},{"wrapped",wrap},{"truncation",wrap?"none":"end"}};return out;}

struct Build {EventLogPart part;Json::Array nodes;
    void add(Json node,std::string action={},bool rim=false,bool framed=false,bool outline=false,bool toolbar=false,float opacity=1){const auto&p=node["position"].array();part.surfaces.push_back({node["id"].string(),Matrix::translation(p[0].number(),p[1].number()),opacity,std::move(action),rim,framed,outline,toolbar});nodes.push_back(std::move(node));}
    void feedback(std::string id,Rect rect,bool framed,const Color&accent,bool toolbar=false){const Rect local{0,0,rect.width,rect.height},rim=framed?Rect{-2,-2,rect.width+4,rect.height+4}:local;
        add(shape(id+"/tint",rect,cut(local,4),alpha(accent,.30)),id,false,framed,false,toolbar,0);
        add(shape(id+"/rim",rect,cut(rim,4),{},accent,.9),id,true,framed,false,toolbar,framed?.28f:0);}
    EventLogPart finish(){part.layers=layer("eventLog.part",{});part.layers["allowsGroupOpacity"]=false;part.layers["children"]=std::move(nodes);return std::move(part);}
};
}
Json eventLogOutlinePath(double strokeEnd){need(std::isfinite(strokeEnd)&&strokeEnd>=0&&strokeEnd<=1,"Invalid source Event Log registration phase");return cut({0,0,369,51},4,strokeEnd);}
EventLogArtwork prepareEventLogArtwork(const EventLogState&state,const EventLogAppearance&appearance){need(std::isfinite(appearance.scale)&&appearance.scale>=1&&appearance.scale<=8,"Invalid Event Log render scale");for(double component:appearance.accent)need(std::isfinite(component)&&component>=0&&component<=1,"Invalid Event Log accent");
    const auto primary=gray(appearance.dark?.94:.11),muted=gray(appearance.dark?.68:.38),ink=gray(.13);const auto&strings=state.strings();EventLogArtwork out;Build header,toolbar,empty,scroll;
    header.add(label("eventLog.heading",{12,0,376,20},strings.heading.starts_with("//")?strings.heading:"// "+strings.heading,15,primary,appearance.scale,"semibold"));
    header.add(label("eventLog.status",{12,22,376,14},state.status(),9.5,muted,appearance.scale));out.header=header.finish();
    const auto selectedCategory="eventLog:category:"+std::string(state.category()?eventCategoryKey(*state.category()):"all");
    for(const auto&action:state.actions()){if(action.id.starts_with("eventLog:row:"))continue;const auto&r=action.rect;const bool selected=action.id==selectedCategory||action.id=="eventLog:confirmClear",bottom=r.y>=296;
        toolbar.add(shape(action.id+"/plate",r,cut({0,0,r.width,r.height},4),selected?appearance.accent:gray(appearance.dark?.78:.90),gray(appearance.dark?.94:.35,.6),.6),{},false,false,false,bottom);
        toolbar.feedback(action.id,r,true,appearance.accent,bottom);
        toolbar.add(label(action.id+"/label",{r.x+4,r.y+3,r.width-8,r.height-6},action.label,10,ink,appearance.scale,"medium","center"),{},false,false,false,bottom);}
    toolbar.add(label("eventLog.footer",{12,309,state.confirmingClear()?204.:250.,14},state.confirmingClear()?strings.confirm:strings.localHistory,10,muted,appearance.scale),{},false,false,false,true);out.toolbar=toolbar.finish();
    if(!state.filteredCount())empty.add(label("eventLog.empty",{20,175,360,26},state.itemCount()?strings.emptyCategory:strings.empty,14,primary,appearance.scale,"regular","center"));out.empty=empty.finish();
    if(state.maximumOffset()>0){const double height=std::max(18.,198.*198/(double(state.filteredCount())*55));auto thumb=layer("eventLog.scrollbar",{385,94+(198-height)*state.scrollOffset()/state.maximumOffset(),2,height});thumb["backgroundColor"]=color(alpha(muted,.6));scroll.add(std::move(thumb));}out.scrollbar=scroll.finish();out.rows.reserve(5);
    for(const auto&row:state.visibleRows()){Build body;body.part.rowID=row.id;body.part.full=row.rect;const std::string prefix="eventLog.row."+row.id;const Rect rect{0,0,369,51};
        body.add(shape(prefix+"/fill",rect,cut(rect,4),gray(appearance.dark?.16:.91)));
        body.add(shape(prefix+"/outline",rect,cut(rect,4),{},row.selected?appearance.accent:alpha(muted,.33),row.selected?1:.5),{},false,false,true);
        body.feedback("eventLog:row:"+row.id,rect,false,appearance.accent);
        auto labels=layer(prefix+"/labels",rect);Json::Array children;
        children.push_back(label(prefix+"/timestamp",{9,5,149,12},row.timestamp,8.7,muted,appearance.scale));
        children.push_back(label(prefix+"/category",{211,5,146,12},row.category,8.7,muted,appearance.scale,"regular","right"));
        children.push_back(label(prefix+"/title",{9,19,348,15},row.title,11.5,primary,appearance.scale,"medium"));
        if(!row.detail.empty())children.push_back(label(prefix+"/detail",{9,35,348,13},row.detail,9.5,muted,appearance.scale));
        labels["children"]=std::move(children);labels["allowsGroupOpacity"]=false;body.add(std::move(labels));out.rows.push_back(body.finish());
    }return out;
}
} // namespace endfield::modules
