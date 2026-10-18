#include "modules/battery_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {namespace {
using Json=ehud::data::Json;using Color=std::array<double,4>;using Rect=core::Rect;using Point=core::Point;
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}
void validate(const BatteryReading&r){need(!r.percentage||*r.percentage<=100,"Invalid battery percentage");if(r.capacity){const auto&c=*r.capacity;need(c.maximum>0&&c.maximum<=1000000&&c.current<=c.maximum&&(c.unit=="mAh"||c.unit=="mWh"||c.unit=="%"),"Invalid matching battery capacity");}if(r.health)need(r.health->size()<=4096&&Json::validUtf8(*r.health),"Invalid battery health category");}
void validate(const BatteryAppearance&a){need(std::isfinite(a.scale)&&a.scale>=1&&a.scale<=8,"Invalid battery scale");need(a.language!=core::Language::system,"Resolve battery language once in application owner");for(auto c:a.accent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid battery accent");}
std::string grouped(unsigned n){auto s=std::to_string(n);for(auto i=static_cast<std::ptrdiff_t>(s.size())-3;i>0;i-=3)s.insert(static_cast<std::size_t>(i),",");return s;}
Color gray(double v){return {v,v,v,1};}Json color(Color c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
Json box(Rect r){return Json::Array{r.x,r.y,r.width,r.height};}
Json layer(std::string id,Rect r,const char*kind="layer"){return Json::Object{{"id",std::move(id)},{"kind",kind},{"bounds",box({0,0,r.width,r.height})},{"position",Json::Array{r.x,r.y}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
Json cmd(const char*op,std::initializer_list<Point>p={}){Json::Array v;for(auto x:p)v.push_back(Json::Array{x.x,x.y});return Json::Object{{"op",op},{"points",std::move(v)}};}
Json::Array cut(Rect r){return {cmd("move",{{r.x+4,r.y}}),cmd("line",{{r.x+r.width,r.y}}),cmd("line",{{r.x+r.width,r.y+r.height-4}}),cmd("line",{{r.x+r.width-4,r.y+r.height}}),cmd("line",{{r.x,r.y+r.height}}),cmd("line",{{r.x,r.y+4}}),cmd("close")};}
Json shape(std::string id,Rect r,Json::Array p,std::optional<Color>fill={},std::optional<Color>stroke={},double width=1){auto n=layer(std::move(id),r,"shape");n["shape"]=Json::Object{{"path",std::move(p)},{"fillColor",fill?color(*fill):Json{}},{"strokeColor",stroke?color(*stroke):Json{}},{"lineWidth",width},{"lineCap","butt"},{"lineJoin","miter"},{"miterLimit",10},{"fillRule","non-zero"}};return n;}
Json text(std::string id,Rect r,const std::string&value,double size,Color c,double scale,const char*font=".AppleSystemUIFontMonospaced-Regular"){auto n=layer(std::move(id),r,"text");n["contentsScale"]=scale;n["text"]=Json::Object{{"string",value},{"fontSize",size},{"font",Json::Object{{"postScriptName",font},{"familyName",".AppleSystemUIFontMonospaced"},{"pointSize",size},{"symbolicTraits",17408}}},{"foregroundColor",color(c)},{"alignment","center"},{"wrapped",false},{"truncation","end"}};return n;}
}
BatteryPresentation::BatteryPresentation(BatteryReading r,BatteryAppearance a):reading_(std::move(r)),appearance_(a){validate(reading_);validate(appearance_);refresh();}
bool BatteryPresentation::receive(BatteryReading r){validate(r);if(reading_==r)return false;reading_=std::move(r);refresh();++revision_;return true;}
bool BatteryPresentation::setAppearance(BatteryAppearance a){validate(a);if(appearance_==a)return false;appearance_=a;refresh();++revision_;return true;}
void BatteryPresentation::refresh(){auto tr=[&](const char*en,const char*zh){return core::localized(en,zh,appearance_.language);};const auto&r=reading_;labels_.heading=tr("// BATTERY STATUS","// 电池状态");labels_.percentage=r.percentage?std::to_string(*r.percentage)+"%":"—";
 if(!r.present)labels_.state=tr("No internal battery","无内置电池");else if(r.charging)labels_.state=tr("CHARGING","正在充电");else if(r.fullyCharged)labels_.state=tr("FULLY CHARGED","已充满");else if(r.pluggedIn)labels_.state=tr("POWER CONNECTED","已连接电源");else labels_.state=tr("ON BATTERY","电池供电");
 labels_.source=tr("SOURCE  /  ","电源  /  ")+(r.pluggedIn?tr("External power","外接电源"):r.present?tr("Battery power","电池供电"):tr("Unavailable","暂无数据"));
 if(r.present&&r.capacity)labels_.capacity=grouped(r.capacity->current)+" / "+grouped(r.capacity->maximum)+" "+r.capacity->unit;else if(r.present&&r.percentage)labels_.capacity=std::to_string(*r.percentage)+" / 100 %";else labels_.capacity="—";
 labels_.health=r.present&&r.health&&!r.health->empty()?*r.health:tr("Unavailable","暂无数据");labels_.settings=tr("Battery alert","电池提醒")+"  ›";
}
Color BatteryPresentation::levelColor()const noexcept{const bool dark=appearance_.dark;if(!reading_.percentage)return gray(dark?.66:.39);if(*reading_.percentage>50)return dark?Color{.30,.90,.50,1}:Color{.04,.48,.21,1};if(*reading_.percentage>=20)return dark?Color{.98,.83,.12,1}:Color{.65,.47,.03,1};return dark?Color{1,.32,.31,1}:Color{.72,.13,.12,1};}
bool BatteryPresentation::hitsSettings(Point p)noexcept{const auto r=settingsRect();return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=r.x&&p.x<r.x+r.width&&p.y>=r.y&&p.y<r.y+r.height;}
BatteryArtwork prepareBatteryArtwork(const BatteryPresentation&s){BatteryArtwork result;Json::Array children;const auto&a=s.appearance();const auto&v=s.labels();const auto foreground=gray(a.dark?.94:.12),muted=gray(a.dark?.66:.39);
 auto add=[&](Json n,bool tint=false,bool rim=false){const auto&p=n["position"].array();result.surfaces.push_back({n["id"].string(),core::Matrix4::translation(p[0].number(),p[1].number()),tint,rim});children.push_back(std::move(n));};
 add(text("battery.heading",{0,8,400,22},v.heading,11,muted,a.scale));
 // Source CGPath rounded screen and outlined laptop base. The shared vector
 // rasterizer retains this tiny geometry; no uploaded bitmap asset is needed.
 Json::Array machine{cmd("move",{{62,19}}),cmd("line",{{62,34}}),cmd("cubic",{{62,35.1045694996},{61.1045694996,36},{60,36}}),cmd("line",{{14,36.00000000000001}}),cmd("cubic",{{12.8954305004,36.00000000000001},{12,35.104569499600004},{12,34.00000000000001}}),cmd("line",{{12.000000000000002,4}}),cmd("cubic",{{12.000000000000002,2.8954305004},{12.895430500400002,1.9999999999999998},{14.000000000000002,2}}),cmd("line",{{60,2}}),cmd("cubic",{{61.1045694996,2},{62,2.8954305004},{62,4}}),cmd("close"),cmd("move",{{12,38}}),cmd("line",{{4,45}}),cmd("line",{{70,45}}),cmd("line",{{62,38}}),cmd("close")};
 add(shape("battery.laptop",{163,42,74,51},std::move(machine),{},foreground,2));add(text("battery.percentage",{0,96,400,96},v.percentage,82,s.levelColor(),a.scale,".AppleSystemUIFontMonospaced-Light"));add(text("battery.state",{0,196,400,27},v.state,18,foreground,a.scale,".AppleSystemUIFontMonospaced-Medium"));add(text("battery.source",{0,232,400,22},v.source,11,muted,a.scale));add(text("battery.capacity",{0,264,400,20},v.capacity,12,foreground,a.scale));add(text("battery.health",{0,291,400,20},v.health,10,muted,a.scale));
 const auto r=BatteryPresentation::settingsRect();auto tint=a.accent;tint[3]=.30;add(shape("battery.settings.tint",r,cut({0,0,r.width,r.height}),tint),true);add(shape("battery.settings.rim",r,cut({-2,-2,r.width+4,r.height+4}),{},a.accent,.9),false,true);add(text("battery.settings.label",{r.x+4,r.y+2,r.width-8,r.height-4},v.settings,10,muted,a.scale));result.layers=layer("battery",{});result.layers["children"]=std::move(children);return result;
}
}
