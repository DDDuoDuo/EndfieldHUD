#include "modules/shelf_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::modules {
namespace {
using Json=ehud::data::Json;using Rect=core::Rect;using Point=core::Point;
void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
ShelfColor gray(double w,double a=1){return {w,w,w,a};}
ShelfColor alpha(ShelfColor c,double a){c[3]=a;return c;}
void validate(const ShelfPresentationStyle&s){
    const auto color=[](const ShelfColor&c){for(auto n:c)need(std::isfinite(n)&&n>=0&&n<=1,"Invalid shelf color");};
    color(s.accent);if(s.errorColor)color(*s.errorColor);if(s.lightDropColor)color(*s.lightDropColor);
    need(std::isfinite(s.contentsScale)&&s.contentsScale>=1&&s.contentsScale<=8,"Invalid shelf contents scale");
    for(const auto*p:{&s.heading,&s.emptyTitle,&s.emptyHelp,&s.clearQuestion,&s.unavailable})need(p->size()<=65536&&Json::validUtf8(*p),"Invalid shelf presentation text");
}
Json box(Rect r){return Json::Array{r.x,r.y,r.width,r.height};}
Json color(ShelfColor c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
Json layer(std::string id,Rect r,const char*kind="layer"){
    return Json::Object{{"id",std::move(id)},{"kind",kind},{"bounds",box({0,0,r.width,r.height})},{"frame",box(r)},
        {"position",Json::Array{r.x,r.y}},{"anchorPoint",Json::Array{0,0}},{"opacity",1},{"children",Json::Array{}}};
}
void add(Json&parent,Json child){auto children=parent["children"].array();children.push_back(std::move(child));parent["children"]=std::move(children);}
Json command(const char*op,std::initializer_list<Point>points={}){Json::Array p;for(auto n:points)p.push_back(Json::Array{n.x,n.y});return Json::Object{{"op",op},{"points",std::move(p)}};}
Json::Array cut(Rect r,double c){Json::Array p;const auto points=FileShelfState::cutCorner(r,c);for(std::size_t n=0;n<points.size();++n)p.push_back(command(n?"line":"move",{points[n]}));p.push_back(command("close"));return p;}
Json::Array rounded(Rect r,double radius){const auto x=r.x,y=r.y,w=r.width,h=r.height,c=std::min({radius,w/2,h/2}),k=c*.5522847498;
    return {command("move",{{x+w,y+h/2}}),command("line",{{x+w,y+h-c}}),command("cubic",{{x+w,y+h-c+k},{x+w-c+k,y+h},{x+w-c,y+h}}),command("line",{{x+c,y+h}}),command("cubic",{{x+c-k,y+h},{x,y+h-c+k},{x,y+h-c}}),command("line",{{x,y+c}}),command("cubic",{{x,y+c-k},{x+c-k,y},{x+c,y}}),command("line",{{x+w-c,y}}),command("cubic",{{x+w-c+k,y},{x+w,y+c-k},{x+w,y+c}}),command("close")};
}
Json shape(std::string id,Rect r,Json::Array path,std::optional<ShelfColor>fill={},std::optional<ShelfColor>stroke={},double width=1,bool round=false){auto out=layer(std::move(id),r,"shape");
    out["shape"]=Json::Object{{"path",std::move(path)},{"fillColor",fill?color(*fill):Json{}},{"strokeColor",stroke?color(*stroke):Json{}},{"lineWidth",width},{"lineCap",round?"round":"butt"},{"lineJoin",round?"round":"miter"},{"miterLimit",10},{"fillRule","non-zero"}};return out;
}
Json label(std::string id,Rect r,std::string text,double size,ShelfColor ink,const char*weight="regular",const char*align="left",bool wrapped=false,double scale=2){
    need(text.size()<=65536&&Json::validUtf8(text),"Visible shelf label exceeds presentation budget");
    auto out=layer(std::move(id),r,"text");out["contentsScale"]=std::min(4.,std::ceil(std::clamp(scale,1.,4.)*1.35));
    const bool semi=std::string_view(weight)=="semibold",medium=std::string_view(weight)=="medium";
    out["text"]=Json::Object{{"string",std::move(text)},{"fontSize",size},{"foregroundColor",color(ink)},
        {"font",Json::Object{{"postScriptName",semi?".AppleSystemUIFontDemi":medium?".AppleSystemUIFontMedium":".AppleSystemUIFont"},{"familyName",".AppleSystemUIFont"},{"pointSize",size},{"symbolicTraits",semi?2:0}}},
        {"truncation",wrapped?"none":"end"},{"alignment",align},{"wrapped",wrapped}};return out;
}
void highlight(Json&parent,std::vector<ShelfPresentationFeedback>&feedback,std::string action,Rect rect,bool framed,ShelfColor accent){
    const auto id=action+"/highlight";auto group=layer(id,rect);group["name"]="hud.control.highlight";group["allowsGroupOpacity"]=false;
    const Rect b{0,0,rect.width,rect.height},rim=framed?Rect{-2,-2,rect.width+4,rect.height+4}:b;
    const auto outline=[](Rect r,bool f){return f?cut(r,std::min(4.,std::min(r.width,r.height)/3)):rounded(r,3);};
    auto tint=shape(id+"/tint",b,outline(b,framed),alpha(accent,.30));tint["opacity"]=0;
    auto edge=shape(id+"/rim",b,outline(rim,framed),{},accent,.9);edge["opacity"]=framed?.28:0;
    add(group,std::move(tint));add(group,std::move(edge));add(parent,std::move(group));feedback.push_back({std::move(action),id+"/tint",id+"/rim",0,framed?.28:0,0,framed});
}
Json::Array folder(Rect r){return {command("move",{{r.x,r.y+r.height}}),command("line",{{r.x,r.y}}),command("line",{{r.x+r.width*.4,r.y}}),command("line",{{r.x+r.width*.54,r.y+r.height*.25}}),command("line",{{r.x+r.width,r.y+r.height*.25}}),command("line",{{r.x+r.width,r.y+r.height}}),command("close")};}
Json::Array eye(Rect r){const double x=r.x+r.width/2,y=r.y+r.height/2,radius=1.5,k=radius*.5522847498307936;
    return {command("move",{{r.x,y}}),command("quadratic",{{x,r.y-2},{r.x+r.width,y}}),command("quadratic",{{x,r.y+r.height+2},{r.x,y}}),
        command("move",{{x+radius,y}}),command("cubic",{{x+radius,y+k},{x+k,y+radius},{x,y+radius}}),command("cubic",{{x-k,y+radius},{x-radius,y+k},{x-radius,y}}),command("cubic",{{x-radius,y-k},{x-k,y-radius},{x,y-radius}}),command("cubic",{{x+k,y-radius},{x+radius,y-k},{x+radius,y}}),command("close")};
}
void depot(Json&parent,std::vector<ShelfPresentationImage>&images,const std::string&id,Rect rect,ShelfColor tint,double scale){auto image=layer(id,rect);image["name"]="endfield.icon.Depot_icon";image["requiredSourceImage"]="Depot_icon";image["contentsGravity"]="resizeAspect";image["contentsScale"]=scale;add(parent,std::move(image));images.push_back({ShelfPresentationImage::Kind::sourceDepot,id,{},"AppIconSources/EndfieldWiki/Depot_icon.png",{},rect,tint,static_cast<unsigned>(std::clamp(std::ceil(std::max(rect.width,rect.height)*scale),8.,1024.)),false,false,true,true});}
struct Chrome {Json artwork;std::vector<ShelfPresentationImage>images;std::vector<ShelfPresentationFeedback>feedback;};
Chrome chrome(const FileShelfState&state,const ShelfPresentationStyle&s,const std::string&status){
    Chrome c;const auto style=FileShelfState::style(s.dark);const auto primary=gray(style.primary),muted=gray(style.muted),ink=gray(style.cardInk);
    c.artwork=layer("shelf.chrome",FileShelfState::bounds());c.artwork["name"]="module.fileShelf.canvas";c.artwork["allowsGroupOpacity"]=false;
    auto collection=layer("shelf.collection",FileShelfState::bounds());collection["name"]="shelf.collection";collection["allowsGroupOpacity"]=false;
    // Actual collection mask belongs here, outside each card's selected depth.
    collection["mask"]=shape("shelf.collection/mask",FileShelfState::bounds(),{command("move",{{9,40}}),command("line",{{391,40}}),command("line",{{391,288}}),command("line",{{9,288}}),command("close")},gray(0));
    if(state.items().empty()){
        auto symbol=layer("shelf.empty/symbol",{165,88,70,56});
        if(s.depotIconAvailable)depot(symbol,c.images,"shelf.empty/depot",{7,0,56,56},muted,s.contentsScale);
        else add(symbol,shape("shelf.empty/folder",{},folder({4,4,62,42}),{},muted,1.8,true));
        add(collection,std::move(symbol));add(collection,label("shelf.empty/title",{28,164,344,22},s.emptyTitle,14,primary,"medium","center",false,s.contentsScale));
        add(collection,label("shelf.empty/help",{24,194,352,38},s.emptyHelp,10.5,muted,"regular","center",true,s.contentsScale));
    }add(c.artwork,std::move(collection));
    // Scrollbar rect is a separate numeric output: scrolling never rebuilds
    // this local chrome or rerasterizes the unchanged title/status/toolbar.
    auto outline=shape("shelf.drop",FileShelfState::contentRect(),rounded({0,0,382,248},5),{},state.dropTarget()?s.accent:ShelfColor{},1.5);add(c.artwork,std::move(outline));
    auto title=label("shelf.heading",{11,0,376,20},s.heading.starts_with("//")?s.heading:"// "+s.heading,15,primary,"semibold","natural",false,s.contentsScale);title["text"]["truncation"]="none";add(c.artwork,std::move(title));
    ShelfColor statusColor=muted;if(state.dropTarget()){if(s.dark)statusColor=s.accent;else{need(s.lightDropColor.has_value(),"Light shelf drop requires original owner blend color");statusColor=*s.lightDropColor;}}
    else if(state.error()){need(s.errorColor.has_value(),"Shelf error requires actual owner's system error color");statusColor=*s.errorColor;}
    add(c.artwork,label("shelf.status",{12,22,376,13},status,9.5,statusColor,"regular","natural",false,s.contentsScale));
    auto toolbar=layer("shelf.toolbar",FileShelfState::bounds());if(state.confirmingClear())add(toolbar,label("shelf.clear/question",{12,306,202,15},s.clearQuestion,10.5,primary,"regular","left",false,s.contentsScale));
    for(const auto&a:state.toolbarActions()){
        const bool highlighted=a.id=="shelf:add"||a.id=="shelf:confirmClear";const Rect local{0,0,a.rect.width,a.rect.height};auto plate=shape(a.id,a.rect,cut(local,4),highlighted?s.accent:gray(style.toolbarWhite),gray(style.toolbarBorderWhite,.65),.6);
        highlight(plate,c.feedback,a.id,local,true,s.accent);const bool icon=a.id=="shelf:add"&&s.depotIconAvailable;
        if(icon)depot(plate,c.images,a.id+"/depot",{7,5,17,17},ink,s.contentsScale);add(toolbar,std::move(plate));
        const double inset=icon?28:4;add(toolbar,label(a.id+"/title",{a.rect.x+inset,a.rect.y+6,a.rect.width-inset-4,17},a.id=="shelf:add"&&!icon?"+ "+a.label:a.label,11,ink,"semibold","center",false,s.contentsScale));
    }add(c.artwork,std::move(toolbar));return c;
}
ShelfPresentedCard card(const FileShelfState&state,const FileShelfItem&item,const FileShelfState::Card&p,const ShelfPresentationStyle&s,std::uint64_t revision){
    ShelfPresentedCard out;out.itemID=item.id;out.full=p.full;out.clipped=p.clipped;out.selectionY=p.selectionY;out.selectionZ=p.selectionZ;out.contentRevision=revision;
    const auto style=FileShelfState::style(s.dark);const auto ink=gray(style.cardInk);const auto id="shelf.card."+item.id;out.artwork=layer(id,{0,0,184,74});out.artwork["name"]=id;out.artwork["allowsGroupOpacity"]=false;
    add(out.artwork,shape(id+"/plate",{},cut({0,0,184,74},7),gray(style.cardWhite),p.selected?s.accent:gray(style.unselectedBorderWhite,.75),p.borderWidth));
    highlight(out.artwork,out.feedback,"shelf:"+item.id+":select",{0,0,184,74},true,s.accent);
    auto image=layer(id+"/icon",FileShelfState::iconRect());image["contentsGravity"]="resizeAspect";image["contentsScale"]=s.contentsScale;image["opacity"]=p.iconOpacity;image["requiredNativeFileIcon"]=item.id;add(out.artwork,std::move(image));
    out.images.push_back({ShelfPresentationImage::Kind::nativeFileIcon,id+"/icon",item.id,{},item.lastKnownPath,FileShelfState::iconRect(),{},64,item.isDirectory,!p.available,false,true});
    add(out.artwork,label(id+"/name",FileShelfState::nameRect(),item.name,11.5,ink,"semibold","left",false,s.contentsScale));
    add(out.artwork,label(id+"/type",FileShelfState::typeRect(),p.available?state.typeLabel(item):s.unavailable,9,p.available?alpha(ink,.70):ShelfColor{.61,.19,.11,1},"regular","left",false,s.contentsScale));
    add(out.artwork,label(id+"/size",FileShelfState::sizeRect(),state.sizeLabel(item),9,alpha(ink,.72),"regular","left",false,s.contentsScale));
    add(out.artwork,shape(id+"/separator",{},{command("move",{{8,46}}),command("line",{{176,46}})},{},alpha(ink,.17),.6,true));
    for(const auto&a:state.cardActions(item.id,false))highlight(out.artwork,out.feedback,a.id,{a.rect.x-p.full.x,a.rect.y-p.full.y,a.rect.width,a.rect.height},false,s.accent);
    const auto activeColor=alpha(ink,p.available?.82:.22);add(out.artwork,shape(id+"/eye",{},eye({116,54,14,10}),{},activeColor,1,true));add(out.artwork,shape(id+"/folder",{},folder({139,54,14,11}),{},activeColor,1,true));
    add(out.artwork,shape(id+"/cross",{},{command("move",{{164,54}}),command("line",{{173,63}}),command("move",{{173,54}}),command("line",{{164,63}})},{},alpha(ink,.80),1.1,true));return out;
}
}
struct ShelfPresentation::Impl {
    const FileShelfState*state{};std::uint64_t revision{},chromeRevision{},placementRevision{},serial{};std::optional<ShelfPresentationStyle>style;
    Json chrome;std::vector<ShelfPresentedCard>cards,spare;std::vector<FileShelfItem>keys,keySpare;
    std::vector<ShelfPresentationImage>images;std::vector<ShelfPresentationFeedback>feedback;std::vector<FileShelfState::Action>actions;
    std::string status;bool empty{},confirm{},drop{},error{};std::optional<Rect>indicator;ShelfPresentationStats stats;
    std::string target;bool pressed{},reduced{};
    Impl(){cards.reserve(8);spare.reserve(8);keys.reserve(8);keySpare.reserve(8);}
    bool applyFeedback(std::optional<std::string_view>id,bool press,bool reduce){bool changed{};const auto apply=[&](auto&list){for(auto&f:list){const bool hit=id&&*id==f.actionID;const double tint=hit?(press?1:.62):0,rim=hit?1:(f.framed?.28:0);if(tint!=f.tintOpacity||rim!=f.rimOpacity){changed=true;f.tintOpacity=tint;f.rimOpacity=rim;f.duration=reduce?0:press?.06:.14;}}};apply(feedback);for(auto&c:cards)apply(c.feedback);return changed;}
};
ShelfPresentation::ShelfPresentation():impl_(std::make_unique<Impl>()){}ShelfPresentation::~ShelfPresentation()=default;
bool ShelfPresentation::update(const FileShelfState&state,const ShelfPresentationStyle&style){auto&i=*impl_;if(i.state==&state&&i.revision==state.revision()&&i.style&&*i.style==style)return false;validate(style);
    const bool styleChanged=!i.style||*i.style!=style,stateChanged=i.state!=&state;const auto status=state.statusText();
    const bool chromeChanged=styleChanged||stateChanged||i.status!=status||i.empty!=state.items().empty()||i.confirm!=state.confirmingClear()||i.drop!=state.dropTarget()||i.error!=state.error().has_value();
    std::optional<Chrome>newChrome;if(chromeChanged)newChrome=::endfield::modules::chrome(state,style,status);
    const auto range=state.visibleRange();need(range.end-range.begin<=8,"Source shelf visible-card bound exceeded");
    struct Pending {const FileShelfItem*item{};FileShelfState::Card placement;std::size_t old{8};std::optional<ShelfPresentedCard>built;};std::array<Pending,8>pending{};std::size_t count{};auto serial=i.serial;
    for(auto n=range.begin;n<range.end;++n){const auto&item=state.items()[n];for(const auto*value:{&item.id,&item.name,&item.lastKnownPath,&item.typeDescription})need(value->size()<=65536,"Visible shelf metadata exceeds presentation budget");if(item.availabilityError)need(item.availabilityError->size()<=65536,"Shelf availability error exceeds presentation budget");const auto placement=state.card(item.id);if(!placement)continue;auto&p=pending[count++];p.item=&item;p.placement=*placement;
        const auto old=std::find_if(i.cards.begin(),i.cards.end(),[&](const auto&c){return c.itemID==item.id;});if(old!=i.cards.end())p.old=static_cast<std::size_t>(old-i.cards.begin());
        if(styleChanged||stateChanged||p.old==8||i.keys[p.old]!=item||i.cards[p.old].selectionZ!=placement->selectionZ){need(serial<std::numeric_limits<std::uint64_t>::max(),"Shelf content revision exhausted");p.built=card(state,item,*placement,style,++serial);}
    }
    // Complete external formatting/validation before moving retained trees.
    auto actions=state.accessibleActions();auto newStyle=style;auto newStatus=status;std::array<FileShelfItem,8>newKeys;for(std::size_t n=0;n<count;++n)newKeys[n]=*pending[n].item;
    std::size_t targetCapacity{};for(const auto&a:actions)targetCapacity=std::max(targetCapacity,a.id.size());i.target.reserve(targetCapacity);
    bool placementChanged=i.cards.size()!=count||i.indicator!=state.scrollIndicator();i.spare.clear();i.keySpare.clear();
    for(std::size_t n=0;n<count;++n){auto&p=pending[n];if(p.old==8)placementChanged=true;else{const auto&old=i.cards[p.old];placementChanged=placementChanged||p.old!=n||old.full!=p.placement.full||old.clipped!=p.placement.clipped||old.selectionY!=p.placement.selectionY||old.selectionZ!=p.placement.selectionZ;}
        if(p.built){i.spare.push_back(std::move(*p.built));++i.stats.cardBuilds;}else{i.spare.push_back(std::move(i.cards[p.old]));}
        auto&c=i.spare.back();
        c.full=p.placement.full;c.clipped=p.placement.clipped;c.selectionY=p.placement.selectionY;c.selectionZ=p.placement.selectionZ;i.keySpare.push_back(std::move(newKeys[n]));
    }i.cards.swap(i.spare);i.keys.swap(i.keySpare);i.spare.clear();i.keySpare.clear();
    if(newChrome){i.chrome=std::move(newChrome->artwork);i.images=std::move(newChrome->images);i.feedback=std::move(newChrome->feedback);++i.chromeRevision;++i.stats.chromeBuilds;}
    i.actions=std::move(actions);i.style=std::move(newStyle);i.status=std::move(newStatus);i.empty=state.items().empty();i.confirm=state.confirmingClear();i.drop=state.dropTarget();i.error=state.error().has_value();i.indicator=state.scrollIndicator();
    i.state=&state;i.revision=state.revision();i.serial=serial;++i.stats.updates;if(placementChanged){++i.placementRevision;++i.stats.placementUpdates;}i.applyFeedback(i.target.empty()?std::nullopt:std::optional<std::string_view>(i.target),i.pressed,i.reduced);return true;
}
bool ShelfPresentation::setFeedback(std::optional<std::string_view>id,bool pressed,bool reduce){auto&i=*impl_;if(id){bool known{};for(const auto&f:i.feedback)known=known||f.actionID==*id;for(const auto&c:i.cards)for(const auto&f:c.feedback)known=known||f.actionID==*id;if(!known)id.reset();}
    if(id)i.target.assign(*id);else i.target.clear();i.pressed=pressed;i.reduced=reduce;return i.applyFeedback(id,pressed,reduce);
}
const Json&ShelfPresentation::chrome()const noexcept{return impl_->chrome;}std::span<const ShelfPresentedCard>ShelfPresentation::cards()const noexcept{return impl_->cards;}
std::span<const ShelfPresentationImage>ShelfPresentation::chromeImages()const noexcept{return impl_->images;}std::span<const ShelfPresentationFeedback>ShelfPresentation::chromeFeedback()const noexcept{return impl_->feedback;}
std::span<const FileShelfState::Action>ShelfPresentation::actions()const noexcept{return impl_->actions;}std::optional<Rect>ShelfPresentation::scrollIndicator()const noexcept{return impl_->indicator;}
ShelfColor ShelfPresentation::scrollIndicatorColor()const noexcept{return gray(FileShelfState::style(!impl_->style||impl_->style->dark).muted,.6);}
std::uint64_t ShelfPresentation::chromeRevision()const noexcept{return impl_->chromeRevision;}std::uint64_t ShelfPresentation::placementRevision()const noexcept{return impl_->placementRevision;}ShelfPresentationStats ShelfPresentation::stats()const noexcept{return impl_->stats;}
} // namespace endfield::modules
