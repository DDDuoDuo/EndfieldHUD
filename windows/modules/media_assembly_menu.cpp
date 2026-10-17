#include "modules/media_assembly_menu.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
using J=ehud::data::Json;using R=core::Rect;using C=MediaAssemblyColor;
void need(bool v,const char*why){if(!v)throw std::invalid_argument(why);}
J color(C v){for(double x:v)need(std::isfinite(x)&&x>=0&&x<=1,"Invalid Media Assembly menu color");return J::Object{{"sRGB",J::Array{v[0],v[1],v[2],v[3]}}};}
C gray(double v,double a=1){return {v,v,v,a};}C alpha(C v,double a){v[3]=a;return v;}
J node(std::string id,R r,const char*kind="layer"){return J::Object{{"id",std::move(id)},{"kind",kind},{"bounds",J::Array{0,0,r.width,r.height}},{"position",J::Array{r.x,r.y}},{"anchorPoint",J::Array{0,0}}};}
J plate(std::string id,R r,C c,double opacity=1){auto j=node(std::move(id),r);j["backgroundColor"]=color(c);if(opacity!=1)j["opacity"]=opacity;return j;}
J cmd(const char*op,std::initializer_list<core::Point>points={}){J::Array out;for(auto p:points)out.push_back(J::Array{p.x,p.y});return J::Object{{"op",op},{"points",std::move(out)}};}
J cut(R r){const auto k=std::min(4.,std::min(r.width,r.height)/3);return J::Array{cmd("move",{{r.x+k,r.y}}),cmd("line",{{r.x+r.width,r.y}}),cmd("line",{{r.x+r.width,r.y+r.height-k}}),cmd("line",{{r.x+r.width-k,r.y+r.height}}),cmd("line",{{r.x,r.y+r.height}}),cmd("line",{{r.x,r.y+k}}),cmd("close")};}
J shape(std::string id,R r,J path,std::optional<C>fill,std::optional<C>stroke,double width,double opacity){auto j=node(std::move(id),r,"shape");j["opacity"]=opacity;j["shape"]=J::Object{{"path",std::move(path)},{"fillColor",fill?color(*fill):J{}},{"strokeColor",stroke?color(*stroke):J{}},{"lineWidth",width},{"fillRule","non-zero"},{"lineCap","butt"},{"lineJoin","miter"}};return j;}
J text(std::string id,std::string value,R r,double size,C c){auto j=node(std::move(id),r,"text");j["text"]=J::Object{{"string",std::move(value)},{"fontSize",size},{"foregroundColor",color(c)},{"font",J::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".AppleSystemUIFontDemi"},{"pointSize",size},{"symbolicTraits",2}}},{"truncation","end"},{"alignment","natural"},{"wrapped",false}};return j;}
MediaAssemblyControlMenu control(MediaAssemblyMenuKind kind,std::string heading,std::vector<std::pair<std::string,std::string>>commands){
    MediaAssemblyControlMenu m;m.kind=kind;m.heading=std::move(heading);m.size={370,std::max(75.,42+double(commands.size())*31)};
    m.items.push_back({"close","×",{339,8,23,23},true});
    for(std::size_t i=0;i<commands.size();++i)m.items.push_back({std::move(commands[i].first),std::move(commands[i].second),{8,38+double(i)*31,354,26},true});
    return m;
}
}
std::optional<std::string_view>MediaAssemblyControlMenu::actionAt(core::Point p)const noexcept{
    // Source activate(at:): the last enabled item containing the point.
    for(auto it=items.rbegin();it!=items.rend();++it)if(it->enabled&&it->rect.contains(p))return std::string_view(it->id);
    return {};
}
MediaAssemblyControlMenu mediaAssemblyExportMenu(const MediaAssemblyDocumentInfo&d,core::Language l){
    std::vector<std::pair<std::string,std::string>>commands{{"saveAs",core::localized("Save As…","另存为…",l)}};
    if(d.kind!=MediaAssemblyKind::gif)commands.push_back({"overwrite",core::localized("Overwrite original","覆盖原文件",l)});
    return control(MediaAssemblyMenuKind::exportMedia,d.animated()?core::localized("GIF · Export first frame","GIF · 导出首帧",l):core::localized("Export","导出",l),std::move(commands));
}
MediaAssemblyControlMenu mediaAssemblyOverwriteMenu(core::Language l){
    return control(MediaAssemblyMenuKind::confirmOverwrite,core::localized("Replace the original file?","覆盖原文件？",l),{{"cancel",core::localized("Cancel","取消",l)},{"confirm",core::localized("Overwrite original","覆盖原文件",l)}});
}
core::Point mediaAssemblyMenuOrigin(R anchor,core::Point size)noexcept{
    const double proposed=anchor.y+anchor.height+6;
    return {std::min(std::max(8.,anchor.x),432-size.x),proposed+size.y<=420?proposed:std::max(42.,anchor.y-size.y-8)};
}
R mediaAssemblyMenuAnchor(std::span<const MediaAssemblyAction>actions,std::string_view id)noexcept{
    for(const auto&a:actions)if(a.id==id)return a.rect;return {12,331,66,28};
}
double MediaAssemblyMenuMotion::opacity(bool opening,double elapsed,bool reduced)noexcept{
    if(reduced||!std::isfinite(elapsed))return opening?1:0;const double t=std::clamp(elapsed/fade,0.,1.);return float(opening?t:1-t);
}
J mediaAssemblyMenuArtwork(const MediaAssemblyControlMenu&m,const MediaAssemblyAppearance&a){
    need(m.size.x>0&&m.size.y>0,"Empty Media Assembly menu");(void)color(a.accent);
    const auto ink=gray(a.dark?.96:.10);J::Array children;children.reserve(8+m.items.size()*4);
    children.push_back(plate("menu.back",{-3,4,m.size.x,m.size.y},gray(0,.30)));
    auto face=plate("menu.face",{0,0,m.size.x,m.size.y},gray(a.dark?.08:.92,.98));face["borderWidth"]=.7;face["borderColor"]=color(alpha(a.accent,.7));children.push_back(std::move(face));
    for(const auto&item:m.items){
        const auto id="menu.item."+item.id;
        children.push_back(plate(id+"/plate",item.rect,alpha(ink,.07),item.enabled?1:.4));
        if(item.enabled){
            // HUDControlHighlightLayer.add(shape:.cutCorner, framed:true):
            // retained tint/rim leaves; only their opacities animate.
            children.push_back(shape(id+"/tint",item.rect,cut({0,0,item.rect.width,item.rect.height}),alpha(a.accent,.3),{},1,0));
            children.push_back(shape(id+"/rim",item.rect,cut({-2,-2,item.rect.width+4,item.rect.height+4}),{},a.accent,.9,.28));
        }
        children.push_back(text(id+"/title",item.title,{item.rect.x+6,item.rect.y+5,item.rect.width-12,item.rect.height-10},10,ink));
    }
    children.push_back(text("menu.heading","// "+m.heading,{10,11,317,20},12,ink));
    return J::Object{{"bounds",J::Array{0,0,m.size.x,m.size.y}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",std::move(children)}};
}
}
