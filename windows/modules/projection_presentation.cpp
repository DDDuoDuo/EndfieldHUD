#include "modules/projection_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
using Json=ehud::data::Json;using Rect=core::Rect;using Point=core::Point;
void need(bool ok,const char*message){if(!ok)throw std::invalid_argument(message);}
NotesColor gray(double value,double a=1){return {value,value,value,a};}
NotesColor alpha(NotesColor c,double a){c[3]=a;return c;}
Json color(NotesColor c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
Json box(Rect r){return Json::Array{r.x,r.y,r.width,r.height};}
Json layer(std::string id,Rect r,const char*kind="layer"){
    return Json::Object{{"id",std::move(id)},{"kind",kind},{"bounds",box({0,0,r.width,r.height})},{"frame",box(r)},
        {"position",Json::Array{r.x,r.y}},{"anchorPoint",Json::Array{0,0}},{"opacity",1},{"children",Json::Array{}}};
}
void append(Json&parent,Json child){auto children=parent["children"].array();children.push_back(std::move(child));parent["children"]=std::move(children);}
Json command(const char*op,std::initializer_list<Point>points={}){Json::Array values;for(auto p:points)values.push_back(Json::Array{p.x,p.y});return Json::Object{{"op",op},{"points",std::move(values)}};}
Json::Array cutCorner(Rect r){const double c=std::min({4.,r.width/3,r.height/3});return {command("move",{{r.x+c,r.y}}),command("line",{{r.x+r.width,r.y}}),command("line",{{r.x+r.width,r.y+r.height-c}}),command("line",{{r.x+r.width-c,r.y+r.height}}),command("line",{{r.x,r.y+r.height}}),command("line",{{r.x,r.y+c}}),command("close")};}
Json::Array rounded(Rect r){const double x=r.x,y=r.y,w=r.width,h=r.height,c=std::min({3.,w/2,h/2}),k=c*.5522847498;
    return {command("move",{{x+w,y+h/2}}),command("line",{{x+w,y+h-c}}),command("cubic",{{x+w,y+h-c+k},{x+w-c+k,y+h},{x+w-c,y+h}}),command("line",{{x+c,y+h}}),command("cubic",{{x+c-k,y+h},{x,y+h-c+k},{x,y+h-c}}),command("line",{{x,y+c}}),command("cubic",{{x,y+c-k},{x+c-k,y},{x+c,y}}),command("line",{{x+w-c,y}}),command("cubic",{{x+w-c+k,y},{x+w,y+c-k},{x+w,y+c}}),command("close")};
}
Json shape(std::string id,Rect r,Json::Array path,std::optional<NotesColor>fill,std::optional<NotesColor>stroke={}){
    auto out=layer(std::move(id),r,"shape");out["shape"]=Json::Object{{"path",std::move(path)},{"fillColor",fill?color(*fill):Json{}},{"strokeColor",stroke?color(*stroke):Json{}},{"lineWidth",.9},{"lineCap","butt"},{"lineJoin","miter"},{"miterLimit",10},{"fillRule","non-zero"}};return out;
}
Json label(std::string id,Rect r,std::string value,double size=10,bool inkCentered=false){
    auto out=layer(std::move(id),r,"text");out["contentsScale"]=2;
    out["text"]=Json::Object{{"string",std::move(value)},{"fontSize",size},{"foregroundColor",color(gray(.96))},
        {"font",Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".AppleSystemUIFontDemi"},{"pointSize",size},{"symbolicTraits",2}}},
        {"truncation",inkCentered?"none":"end"},{"alignment","natural"},{"wrapped",false}};
    if(inkCentered)out["projectionInkCentered"]=true;return out;
}
bool contains(Rect r,Point p)noexcept{return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
void validate(const ProjectionControlsInput&i){
    need(static_cast<unsigned>(i.kind)<=static_cast<unsigned>(ProjectionControlsKind::clearConfirmation),"Invalid Projection controls kind");
    need(static_cast<unsigned>(i.language)<=static_cast<unsigned>(core::Language::korean),"Invalid Projection controls language");
    for(const auto&c:{i.accent,i.color})for(double v:c)need(std::isfinite(v)&&v>=0&&v<=1,"Invalid Projection controls color");
    need(std::isfinite(i.brushWidth)&&i.brushWidth>=1&&i.brushWidth<=80&&std::isfinite(i.darkness)&&i.darkness>=0&&i.darkness<=1&&std::isfinite(i.blur)&&i.blur>=0&&i.blur<=1,"Invalid Projection controls value");
}
struct Item{std::string id,title,accessibility;Rect rect;bool selected{};std::optional<NotesColor>swatch{};};
struct Build {
    Json root;std::vector<NotesControlsAction>actions;std::vector<NotesControlsFeedback>feedback;
    std::vector<ProjectionControlSlider>sliders;std::vector<ProjectionInkLabel>inkLabels;NotesColor accent;
    void highlight(Json&face,const Item&item,bool framed){
        const std::string id=item.id+"/highlight";auto group=layer(id,item.rect);group["name"]="hud.control.highlight";group["allowsGroupOpacity"]=false;
        const Rect local{0,0,item.rect.width,item.rect.height},rim=framed?Rect{-2,-2,item.rect.width+4,item.rect.height+4}:local;
        auto tint=shape(id+"/tint",local,framed?cutCorner(local):rounded(local),alpha(accent,.30));tint["opacity"]=0;
        auto edge=shape(id+"/rim",local,framed?cutCorner(rim):rounded(rim),{},accent);edge["opacity"]=framed?.28:0;
        append(group,std::move(tint));append(group,std::move(edge));append(face,std::move(group));
        feedback.push_back({item.id,id+"/tint",id+"/rim",0,framed?.28:0,0,true});
    }
};
}

std::optional<double>ProjectionControlSlider::valueAt(double x)const noexcept{
    if(!std::isfinite(x)||!std::isfinite(rail.x)||!std::isfinite(rail.width)||rail.width<=0||!std::isfinite(minimum)||!std::isfinite(maximum)||maximum<=minimum)return {};
    return minimum+std::clamp((x-rail.x)/rail.width,0.,1.)*(maximum-minimum);
}
bool ProjectionControls::update(const ProjectionControlsInput&i){
    if(input_&&*input_==i)return false;validate(i);Build b;b.accent=i.accent;
    const auto text=[&](const char*en,const char*zh){return core::localized(en,zh,i.language);};
    const bool toolbar=i.kind==ProjectionControlsKind::toolbar;Rect bounds;std::string rootID;std::vector<Item>items;
    if(toolbar){bounds={0,0,336,42};rootID="projection.toolbar";
        items={{"color","",text("Color","颜色"),{6,6,30,30},false,i.color},
            {"brush",std::to_string(static_cast<int>(i.brushWidth)),text("Brush thickness","画笔粗细"),{40,6,34,30}},
            {"eraser","◇",text("Eraser","橡皮擦"),{78,6,30,30},i.erasing},
            {"clear",text("Clear all","清空"),"",{112,6,54,30}},
            {"background","▦",text("Background","背景"),{170,6,30,30},i.backgroundEnabled},
            {"appearance","◐",text("Background appearance","背景外观"),{204,6,30,30}},
            {"media","+",text("Image/Video","图片/视频"),{238,6,30,30}},
            {"close",text("Return","返回"),text("Return","返回"),{272,6,58,30}}};
    }else if(i.kind==ProjectionControlsKind::clearConfirmation){bounds={0,0,260,92};rootID="projection.clear";
        items={{"close",text("Cancel","取消"),text("Cancel","取消"),{10,48,116,32}},
            {"confirm",text("Clear all","清空"),"",{134,48,116,32},true}};
    }else{
        const bool brush=i.kind==ProjectionControlsKind::brush;bounds={0,0,260,brush?102.:164.};rootID=brush?"projection.brush":"projection.appearance";
        items={{"close","×",text("Close","关闭"),{227,7,23,23}}};
        const unsigned count=brush?1:2;
        for(unsigned n=0;n<count;++n){const std::string id=brush?"brush":n?"blur":"darkness";
            const std::string title=brush?text("Brush thickness","画笔粗细"):n?text("Blur","模糊"):text("Darkness","背景深度");
            const double value=brush?i.brushWidth:(n?i.blur:i.darkness)*100,minimum=brush?1.:0.,maximum=brush?80.:100.;
            const double y=double(n)*62,fraction=(value-minimum)/(maximum-minimum);
            b.sliders.push_back({id,title,{42,63+y,170,20},{37,57+y,180,32},{10,35+y,236,18},
                {42,73+y,170,2},{42,73+y,170*fraction,2},{42+170*fraction-2.5,70+y,5,8},minimum,maximum,value});
            items.push_back({"minus:"+std::to_string(n),"−",title+" −",{8,56+y,26,30}});
            items.push_back({"plus:"+std::to_string(n),"+",title+" +",{224,56+y,26,30}});
        }
    }
    b.root=layer(rootID,bounds);b.root["name"]=toolbar?"projection.toolbar":"notes.secondaryMenu";b.root["zPosition"]=toolbar?2000000:3000000;
    auto back=layer(rootID+"/back",{-3,4,bounds.width,bounds.height});back["backgroundColor"]=color(gray(0,.30));back["cornerRadius"]=toolbar?10:0;append(b.root,std::move(back));
    auto face=layer(rootID+"/face",bounds);face["backgroundColor"]=color(gray(.08,.98));face["borderWidth"]=.7;face["borderColor"]=color(alpha(i.accent,.7));face["cornerRadius"]=toolbar?10:0;
    for(const auto&item:items){b.actions.push_back({item.id,item.accessibility.empty()?item.title:item.accessibility,item.rect,true,item.selected});
        auto plate=layer(item.id+"/plate",item.rect);plate["backgroundColor"]=color(item.swatch.value_or(item.selected?alpha(i.accent,.22):gray(.96,.07)));plate["cornerRadius"]=toolbar?5:0;append(face,std::move(plate));
        if(!toolbar){b.highlight(face,item,true);append(face,label(item.id+"/label",{item.rect.x+6,item.rect.y+5,item.rect.width-12,item.rect.height-10},item.title));}
    }
    if(toolbar){
        // The source override leaves all plates first, then replaces the base
        // labels/highlights with full-rect ink rasters and unframed round rims.
        for(const auto&item:items){if(!item.swatch){const auto id="projection.toolbar.label."+item.id;auto node=label(id,item.rect,item.title,10,true);node["name"]=id;append(face,std::move(node));b.inkLabels.push_back({id,item.title,item.rect,gray(.96)});}b.highlight(face,item,false);}
    }else if(i.kind==ProjectionControlsKind::clearConfirmation){append(face,label(rootID+"/question",{12,14,236,22},text("Clear drawings and media?","清空绘画和媒体？"),12));}
    else for(const auto&slider:b.sliders){const std::string id=rootID+"/"+slider.id;
        append(face,label(id+"/label",slider.labelRect,slider.label+"  "+std::to_string(static_cast<int>(std::round(slider.value)))));
        auto track=layer(id+"/track",slider.track);track["backgroundColor"]=color(gray(1,.20));append(face,std::move(track));
        auto fill=layer(id+"/fill",slider.fill);fill["backgroundColor"]=color(i.accent);append(face,std::move(fill));
        auto thumb=layer(id+"/thumb",slider.thumb);thumb["backgroundColor"]=color(gray(1));append(face,std::move(thumb));
    }
    append(b.root,std::move(face));auto next=i;
    input_=std::move(next);artwork_=std::move(b.root);actions_=std::move(b.actions);feedback_=std::move(b.feedback);sliders_=std::move(b.sliders);inkLabels_=std::move(b.inkLabels);bounds_=bounds;
    hovered_.reset();pressed_=false;reduceMotion_=false;++revision_;++feedbackRevision_;return true;
}
bool ProjectionControls::setFeedback(std::optional<std::string_view>id,bool pressed,bool reduce){
    need(input_.has_value(),"Projection controls must exist before feedback");std::optional<std::size_t>next;
    if(id){for(std::size_t n=0;n<feedback_.size();++n)if(feedback_[n].actionID==*id){next=n;break;}need(next.has_value(),"Unknown Projection feedback action");}
    pressed=pressed&&next.has_value();if(hovered_==next&&pressed_==pressed&&reduceMotion_==reduce)return false;
    const bool framed=input_->kind!=ProjectionControlsKind::toolbar;
    for(std::size_t n=0;n<feedback_.size();++n){auto&f=feedback_[n];const bool active=next&&*next==n;const double tint=active?(pressed?1.:.62):0.,rim=active?1.:framed?.28:0.;
        f.duration=!reduce&&(f.tintOpacity!=tint||f.rimOpacity!=rim)?(active&&pressed?.06:.14):0;f.tintOpacity=tint;f.rimOpacity=rim;}
    hovered_=next;pressed_=pressed;reduceMotion_=reduce;++feedbackRevision_;return true;
}
std::optional<std::string_view>ProjectionControls::actionAt(Point point)const noexcept{
    for(auto it=actions_.rbegin();it!=actions_.rend();++it)if(it->enabled&&contains(it->rect,point))return it->id;return {};
}
std::optional<std::size_t>ProjectionControls::sliderAt(Point point)const noexcept{
    for(std::size_t n=0;n<sliders_.size();++n)if(contains(sliders_[n].hitRect,point))return n;return {};
}
}
