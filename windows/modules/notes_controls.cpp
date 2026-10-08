#include "modules/notes_controls.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace endfield::modules {
namespace {
using Json=ehud::data::Json;using Rect=core::Rect;using Point=core::Point;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
void textCheck(const std::string&s){need(s.size()<=65536&&Json::validUtf8(s),"Invalid Notes control string");}
void colorCheck(const NotesColor&c){for(auto v:c)need(std::isfinite(v)&&v>=0&&v<=1,"Invalid Notes control color");}
NotesColor gray(double v,double a=1){return {v,v,v,a};}
NotesColor alpha(NotesColor v,double a){v[3]=a;return v;}
Json color(const NotesColor&c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
Json box(Rect r){return Json::Array{r.x,r.y,r.width,r.height};}
Json layer(std::string id,Rect r,const char*kind="layer"){return Json::Object{{"id",std::move(id)},{"kind",kind},{"bounds",box({0,0,r.width,r.height})},{"frame",box(r)},{"position",Json::Array{r.x,r.y}},{"anchorPoint",Json::Array{0,0}},{"opacity",1},{"children",Json::Array{}}};}
void append(Json&parent,Json child){auto children=parent["children"].array();children.push_back(std::move(child));parent["children"]=std::move(children);}
Json command(const char*op,std::initializer_list<Point> points={}){Json::Array values;for(auto p:points)values.push_back(Json::Array{p.x,p.y});return Json::Object{{"op",op},{"points",std::move(values)}};}
Json::Array cutCorner(Rect r){const auto c=std::min(4.,std::min(r.width,r.height)/3);return {command("move",{{r.x+c,r.y}}),command("line",{{r.x+r.width,r.y}}),command("line",{{r.x+r.width,r.y+r.height-c}}),command("line",{{r.x+r.width-c,r.y+r.height}}),command("line",{{r.x,r.y+r.height}}),command("line",{{r.x,r.y+c}}),command("close")};}
Json::Array rounded(Rect r,double radius){
    const auto x=r.x,y=r.y,w=r.width,h=r.height,c=std::min({radius,w/2,h/2}),k=c*.5522847498;
    return {command("move",{{x+w,y+h/2}}),command("line",{{x+w,y+h-c}}),command("cubic",{{x+w,y+h-c+k},{x+w-c+k,y+h},{x+w-c,y+h}}),command("line",{{x+c,y+h}}),command("cubic",{{x+c-k,y+h},{x,y+h-c+k},{x,y+h-c}}),command("line",{{x,y+c}}),command("cubic",{{x,y+c-k},{x+c-k,y},{x+c,y}}),command("line",{{x+w-c,y}}),command("cubic",{{x+w-c+k,y},{x+w,y+c-k},{x+w,y+c}}),command("close")};
}
Json shape(std::string id,Rect r,Json::Array path,std::optional<NotesColor> fill,std::optional<NotesColor> stroke={},double width=1){
    auto out=layer(std::move(id),r,"shape");out["shape"]=Json::Object{{"path",std::move(path)},{"fillColor",fill?color(*fill):Json{}},{"strokeColor",stroke?color(*stroke):Json{}},{"lineWidth",width},{"lineCap","butt"},{"lineJoin","miter"},{"miterLimit",10},{"fillRule","non-zero"}};return out;
}
Json label(std::string id,Rect r,std::string value,double size,NotesColor ink,bool semibold=true,bool truncate=true){
    auto out=layer(std::move(id),r,"text");out["text"]=Json::Object{{"string",std::move(value)},{"fontSize",size},{"foregroundColor",color(ink)},{"font",Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",semibold?".AppleSystemUIFontDemi":".AppleSystemUIFont"},{"pointSize",size},{"symbolicTraits",semibold?2:0}}},{"truncation",truncate?"end":"none"},{"alignment","natural"},{"wrapped",false}};return out;
}
struct Build {
    Json root;std::vector<NotesControlsAction> actions;std::vector<NotesControlsImage> images;std::vector<NotesControlsFeedback> feedback;
    NotesColor accent;bool menu{};
    void highlight(Json&parent,std::string_view action,Rect r,bool framed){
        const auto id=std::string(action)+"/highlight";auto group=layer(id,r);group["name"]="hud.control.highlight";group["allowsGroupOpacity"]=false;
        const Rect b{0,0,r.width,r.height},rim=framed?Rect{-2,-2,r.width+4,r.height+4}:b;
        auto tint=shape(id+"/tint",b,framed?cutCorner(b):rounded(b,3),alpha(accent,.30));tint["opacity"]=0;
        auto edge=shape(id+"/rim",b,framed?cutCorner(rim):rounded(rim,3),{},accent,.9);edge["opacity"]=framed?.28:0;
        append(group,std::move(tint));append(group,std::move(edge));append(parent,std::move(group));
        feedback.push_back({std::string(action),id+"/tint",id+"/rim",0,framed?.28:0,0,true});
    }
};
struct Item {
    std::string id,title,ax;Rect rect;bool enabled{true},selected{};std::optional<NotesColor> swatch;
    Item(std::string id_,std::string title_,std::string ax_,Rect rect_,bool enabled_=true,bool selected_=false,std::optional<NotesColor> swatch_={})
        :id(std::move(id_)),title(std::move(title_)),ax(std::move(ax_)),rect(rect_),enabled(enabled_),selected(selected_),swatch(swatch_){}
};
void validate(const NotesControlsInput&i){
    colorCheck(i.accent);colorCheck(i.currentColor);if(i.systemOrange)colorCheck(*i.systemOrange);
    need(std::isfinite(i.contentsScale)&&i.contentsScale>=1&&i.contentsScale<=8,"Invalid Notes control render scale");
    if(i.error){textCheck(*i.error);need(i.systemOrange.has_value(),"Notes save-error artwork requires the original owner's system orange");}
    const auto&s=i.strings;for(const auto*p:{&s.cancelDeletion,&s.confirmDeletion,&s.heading,&s.saveErrorPrefix,&s.storageUnavailable,&s.chooseFile,&s.chooseShelf,&s.close,&s.currentColor,&s.mediaOnly,&s.useMedia,&s.shelfHeading})textCheck(*p);
    for(const auto&v:s.tools)textCheck(v);for(const auto&v:s.traits)textCheck(v);textCheck(i.selectedValue);
    need(i.values.size()<=100000&&i.choices.size()<=100000,"Notes menu data exceeds descriptor budget");for(const auto&v:i.values)textCheck(v);
    std::set<std::string,std::less<>> ids;bool selected=!i.selectedID;
    for(const auto&c:i.choices){textCheck(c.id);textCheck(c.title);textCheck(c.detail);need(!c.id.empty()&&ids.insert(c.id).second,"Duplicate Notes shelf identity");if(i.selectedID&&*i.selectedID==c.id){need(c.supported&&c.available,"Selected shelf media is unavailable");selected=true;}}
    need(selected,"Missing selected shelf identity");
    need(std::isfinite(i.colorWheelSelection.x)&&std::isfinite(i.colorWheelSelection.y)&&i.colorWheelSelection.x>=0&&i.colorWheelSelection.x<=1&&i.colorWheelSelection.y>=0&&i.colorWheelSelection.y<=1,"Invalid source color-wheel selection");
}
}
bool NotesControls::update(const NotesControlsInput&i){
    if(input_&&*input_==i)return false;validate(i);Build b;b.accent=i.accent;b.menu=i.kind!=NotesControlsKind::center;
    const auto ink=gray(i.dark?.96:.10),primary=gray(i.dark?.94:.11),muted=gray(i.dark?.65:.37),border=gray(i.dark?.72:.24,i.dark?.28:.24);
    Rect bounds{0,0,400,334};std::vector<Item> items;
    if(i.kind==NotesControlsKind::deletion){
        // NotesCanvas.renderDeletionControls: source actions are right-aligned
        // under the delete icon by NotesState. This descriptor is their local
        // 56x25 artwork; the caller supplies that workspace origin and .16s rise.
        bounds={0,0,56,25};b.root=layer("notes.confirmation",bounds);
        for(unsigned n=0;n<2;++n){const std::string id=n?"confirmDelete":"cancelDelete";const Rect r{double(n)*31,0,25,25},local{0,0,25,25};
            b.actions.push_back({id,n?i.strings.confirmDeletion:i.strings.cancelDeletion,r});
            auto plate=shape(id,r,rounded(local,3),gray(i.dark?.15:.91,.98),n?i.accent:border,1);
            b.highlight(plate,id,local,false);Json::Array path;
            if(n)path={command("move",{{6,12}}),command("line",{{10,16}}),command("line",{{19,7}})};
            else path={command("move",{{0,0}}),command("line",{{9,9}}),command("move",{{9,0}}),command("line",{{0,9}})};
            auto symbol=shape(id+"/symbol",n?local:Rect{8,8,9,9},std::move(path),{},n?i.accent:primary,1);
            symbol["shape"]["lineCap"]="round";symbol["shape"]["lineJoin"]="round";append(plate,std::move(symbol));append(b.root,std::move(plate));
        }
    }else if(!b.menu){
        b.root=layer("notes.controls",bounds);b.root["name"]="module.notes.canvas";b.root["allowsGroupOpacity"]=false;
        append(b.root,label("notes.controls/heading",{11,0,250,19},i.strings.heading.starts_with("//")?i.strings.heading:"// "+i.strings.heading,15,primary,true,false));
        const auto status=i.error?i.strings.saveErrorPrefix+*i.error:!i.storageAvailable?i.strings.storageUnavailable:std::string{};
        append(b.root,label("notes.controls/status",{12,20,376,13},status,9.5,i.error?*i.systemOrange:muted,false));
        auto toolbar=layer("notes.controls/toolbar",bounds);
        constexpr std::array<const char*,4> ids{"tool:text","tool:todo","tool:image","tool:drawing"};
        constexpr std::array<const char*,4> symbols{"T","☑","▧","✎"};
        for(std::size_t n=0;n<4;++n){const std::string id=ids[n];const Rect r{7+double(n)*98,294,92,31},local{0,0,92,31};
            if(i.notesSelected)b.actions.push_back({id,i.strings.tools[n],r,true,false});auto plate=shape(id,r,rounded(local,3),gray(i.dark?.16:.84),border,.8);plate["name"]=id;
            b.highlight(plate,id,local,false);const Rect icon{9,5,18,18};
            if(n==3){const Rect pencil{11,7,14,14};Json::Array path;const auto fit=[](double x,double y){return Point{x*14/16,y*14/16};};
                path={command("move",{fit(2,10)}),command("line",{fit(12,0)}),command("line",{fit(15,3)}),command("line",{fit(5,13)}),command("line",{fit(1,14)}),command("close"),command("move",{fit(0,16)}),command("line",{fit(16,16)})};
                auto drawing=shape(id+"/icon",pencil,std::move(path),{},primary,1.1);drawing["name"]="hud.pencil";append(plate,std::move(drawing));
            }else if((n==0&&i.manualIconAvailable)||(n==1&&i.missionIconAvailable)){
                const std::string name=n==0?"Operational_Manual_icon":"Mission_Icon";auto image=layer(id+"/icon",icon);image["name"]="endfield.icon."+name;
                image["requiredSourceImage"]=name;image["contentsGravity"]="resizeAspect";append(plate,std::move(image));
                b.images.push_back({id+"/icon","AppIconSources/EndfieldWiki/"+name+".png",icon,primary,static_cast<unsigned>(std::ceil(18*i.contentsScale)),true,true});
            }else append(plate,label(id+"/icon",icon,symbols[n],14,primary));
            append(plate,label(id+"/label",{27,8,63,17},i.strings.tools[n],10,primary));append(toolbar,std::move(plate));
        }append(b.root,std::move(toolbar));
    }else{
        if(i.kind==NotesControlsKind::mediaSource){bounds={0,0,208,75};items={{"finder",i.strings.chooseFile,"",{8,8,166,25}},{"shelf",i.strings.chooseShelf,"",{8,40,166,25}},{"close","×",i.strings.close,{178,8,23,23}}};}
        else if(i.kind==NotesControlsKind::shelfMedia){
            bounds={0,0,340,260};std::vector<const NotesShelfChoice*> filtered;for(const auto&c:i.choices)if(!i.mediaOnly||c.supported)filtered.push_back(&c);
            need(i.firstRow<=std::max<std::size_t>(4,filtered.size())-4,"Notes shelf first row is out of range");
            items={{"close","×",i.strings.close,{307,7,23,23}},{"filter",std::string(i.mediaOnly?"✓  ":"□  ")+i.strings.mediaOnly,"",{8,33,280,23}},{"use",i.strings.useMedia,"",{174,225,156,27},i.selectedID.has_value()}};
            for(auto row=i.firstRow;row<std::min(filtered.size(),i.firstRow+4);++row){const auto&c=*filtered[row];items.push_back({"row:"+std::to_string(row),c.title+" · "+c.detail,"",{8,62+double(row-i.firstRow)*39,316,35},c.supported&&c.available,i.selectedID&&*i.selectedID==c.id});}
        }else{
            bounds={0,0,242,i.kind==NotesControlsKind::special?48.:184.};items={{"close","×",i.strings.close,{211,8,23,23}}};
            if(i.kind==NotesControlsKind::font||i.kind==NotesControlsKind::size){need(i.firstRow<=std::max<std::size_t>(7,i.values.size())-7,"Notes format first row is out of range");
                for(auto row=i.firstRow;row<std::min(i.values.size(),i.firstRow+7);++row)items.push_back({"value:"+std::to_string(row),i.values[row],"",{8,8+double(row-i.firstRow)*24,195,22},true,i.values[row]==i.selectedValue});
            }else if(i.kind==NotesControlsKind::special){constexpr std::array<const char*,4> titles{"B","I","U","S"};for(std::size_t n=0;n<4;++n)items.push_back({"trait:"+std::to_string(n),titles[n],i.strings.traits[n],{8+double(n)*41,8,32,32},true,i.traits[n]});}
            else{need(i.kind==NotesControlsKind::color,"Unknown Notes control kind");items.push_back({"swatch",i.strings.currentColor,"",{187,80,32,32},false,false,i.currentColor});}
        }
        b.root=layer("notes.menu",bounds);b.root["name"]="notes.secondaryMenu";b.root["zPosition"]=2000000;
        auto back=layer("notes.menu/back",{-3,4,bounds.width,bounds.height});back["backgroundColor"]=color(gray(0,.30));append(b.root,std::move(back));
        auto face=layer("notes.menu/face",bounds);face["backgroundColor"]=color(gray(i.dark?.08:.92,.98));face["borderWidth"]=.7;face["borderColor"]=color(alpha(i.accent,.7));
        for(const auto&item:items){b.actions.push_back({item.id,item.ax.empty()?item.title:item.ax,item.rect,item.enabled,item.selected});
            auto plate=layer(item.id+"/plate",item.rect);plate["backgroundColor"]=color(item.swatch.value_or(item.selected?alpha(i.accent,.22):alpha(ink,.07)));plate["opacity"]=item.enabled||item.swatch?1:.4;append(face,std::move(plate));
            if(item.enabled)b.highlight(face,item.id,item.rect,true);
            if(!item.swatch)append(face,label(item.id+"/label",{item.rect.x+6,item.rect.y+5,item.rect.width-12,item.rect.height-10},item.title,10,ink));
        }
        if(i.kind==NotesControlsKind::shelfMedia){append(face,label("notes.menu/heading",{10,10,285,20},i.strings.shelfHeading,12,ink));std::size_t count=0;for(const auto&c:i.choices)if(!i.mediaOnly||c.supported)++count;
            if(count>4){auto track=layer("notes.menu/scrollTrack",{329,62,2,152});track["backgroundColor"]=color(alpha(ink,.12));append(face,std::move(track));const auto h=std::max(10.,152*4/double(count));auto thumb=layer("notes.menu/scrollThumb",{329,62+double(i.firstRow)/double(count-4)*(152-h),2,h});thumb["backgroundColor"]=color(alpha(ink,.6));append(face,std::move(thumb));}
        }else if(i.kind==NotesControlsKind::color){const Rect wheel{8,8,166,166};auto image=layer("notes.menu/wheel",wheel);image["requiredSourceImage"]="NotesColorWheelView.wheel";append(face,std::move(image));b.images.push_back({"notes.menu/wheel","source-generated:NotesColorWheelView.wheel",wheel,{1,1,1,1},192,false,false});
            const auto x=8+i.colorWheelSelection.x*166,y=8+i.colorWheelSelection.y*166;constexpr double k=4*.5522847498;
            Json::Array circle{command("move",{{x+4,y}}),command("cubic",{{x+4,y+k},{x+k,y+4},{x,y+4}}),command("cubic",{{x-k,y+4},{x-4,y+k},{x-4,y}}),command("cubic",{{x-4,y-k},{x-k,y-4},{x,y-4}}),command("cubic",{{x+k,y-4},{x+4,y-k},{x+4,y}}),command("close")};
            append(face,shape("notes.menu/wheelMarker",{0,0,0,0},std::move(circle),{},gray(0),1.5));
        }append(b.root,std::move(face));
    }
    // Copy owner state before publishing: allocation failure preserves the old
    // descriptor and action/feedback identity set as one coherent revision.
    auto snapshot=i;input_=std::move(snapshot);artwork_=std::move(b.root);actions_=std::move(b.actions);images_=std::move(b.images);feedback_=std::move(b.feedback);bounds_=bounds;
    hovered_.reset();pressed_=false;reduceMotion_=false;++revision_;++feedbackRevision_;return true;
}
bool NotesControls::setFeedback(std::optional<std::string_view> id,bool pressed,bool reduce){
    need(input_.has_value(),"Notes controls must exist before feedback");std::optional<std::size_t> next;
    if(id){for(std::size_t n=0;n<feedback_.size();++n)if(feedback_[n].actionID==*id){next=n;break;}need(next.has_value(),"Notes action has no enabled source highlight");}
    pressed=pressed&&next.has_value();if(hovered_==next&&pressed_==pressed&&reduceMotion_==reduce)return false;
    const bool framed=input_->kind!=NotesControlsKind::center&&input_->kind!=NotesControlsKind::deletion;
    for(std::size_t n=0;n<feedback_.size();++n){auto&f=feedback_[n];const bool active=next&&*next==n;const auto tint=active?(pressed?1.:.62):0.,rim=active?1.:framed?.28:0.;const bool changed=f.tintOpacity!=tint||f.rimOpacity!=rim;
        f.duration=changed&&!reduce?(active&&pressed?.06:.14):0;f.tintOpacity=tint;f.rimOpacity=rim;}
    hovered_=next;pressed_=pressed;reduceMotion_=reduce;++feedbackRevision_;return true;
}
std::optional<std::string_view> NotesControls::actionAt(Point p)const noexcept{
    if(!std::isfinite(p.x)||!std::isfinite(p.y))return {};for(auto i=actions_.rbegin();i!=actions_.rend();++i)if(i->enabled&&p.x>=i->rect.x&&p.y>=i->rect.y&&p.x<i->rect.x+i->rect.width&&p.y<i->rect.y+i->rect.height)return i->id;return {};
}
std::vector<std::string> NotesControls::sizeValues(double current){need(std::isfinite(current)&&current>=0&&current<=100000,"Invalid Notes font size");std::set<int> values{8,10,11,12,14,16,18,20,24,28,32,40,48,64,72,96,144,static_cast<int>(std::round(current))};std::vector<std::string> out;out.reserve(values.size());for(auto v:values)out.push_back(std::to_string(v));return out;}
std::size_t NotesControls::initialFirstRow(std::span<const std::string> values,std::string_view selected){const auto found=std::find(values.begin(),values.end(),selected);return std::min(std::max<std::size_t>(7,values.size())-7,found==values.end()?0:std::size_t(found-values.begin()));}
} // namespace endfield::modules
