#include "native/chrome_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
using namespace core;
using namespace core::source;
namespace {
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
constexpr const char* styleNames[]{"digital","split","dial","rail","stacked"};
void preserveClockFonts(Json&node){
    if(node["text"].isObject())node["text"]["preserveSourceFont"]=true;
    if(node["children"].isArray()){auto children=node["children"].array();for(auto&child:children)preserveClockFonts(child);node["children"]=std::move(children);}
}
Json clockFontTemplate(const Json&status){auto copy=status;auto children=copy["children"].array();for(auto&child:children)if(child["name"]==Json{"hud.clock.viewport"})preserveClockFonts(child);copy["children"]=std::move(children);return copy;}
const Json& statusTemplate(const Json&reference,DesktopClockStyle style,bool hover=false){
    const auto index=static_cast<unsigned>(style);need(index<5,"Invalid chrome clock style");
    for(const auto&row:reference["styles"].array())if(row["style"].string()==styleNames[index]&&row["hover"].boolean()==hover)return row["status"];
    throw std::invalid_argument("Missing original chrome style template");
}
void referenceValid(const Json&reference){
    need(reference["schemaVersion"].integer()==1&&reference["header"]["id"].isString()&&reference["footer"]["children"].isArray()&&reference["footer"]["children"].array().size()==1,"Invalid source chrome reference");
    for(unsigned i=0;i<5;++i){const auto&s=statusTemplate(reference,static_cast<DesktopClockStyle>(i));need(s["children"].array().size()==6,"Source status child topology changed");}
}
Json retainedReference(const Json&reference){
    referenceValid(reference);Json::Array styles;
    // Never retain oracle timelines, duplicate readings or benchmark layouts
    // in the running presentation. Only five source trees and hover variants.
    for(unsigned i=0;i<5;++i)for(const bool hover:{false,true})styles.push_back(Json::Object{
        {"style",styleNames[i]},{"hover",hover},{"status",clockFontTemplate(statusTemplate(reference,static_cast<DesktopClockStyle>(i),hover))}});
    return Json::Object{{"schemaVersion",1},{"header",reference["header"]},{"footer",reference["footer"]},{"styles",std::move(styles)}};
}
Json coordinate(double x,double y){return Json::Array{x,y};}
Json rectangle(Rect r){return Json::Array{r.x,r.y,r.width,r.height};}
void text(Json&node,const ChromeTextPlacement&p){
    node["text"]["string"]=p.text;node["text"]["fontSize"]=p.fontSize;
    node["text"]["alignment"]=std::array{"left","center","right"}[static_cast<unsigned>(p.alignment)];
    node["bounds"]=rectangle({0,0,p.frame.width,p.frame.height});node["frame"]=rectangle(p.frame);
    const auto&anchor=node["anchorPoint"].array();node["position"]=coordinate(p.frame.x+anchor[0].number()*p.frame.width,p.frame.y+anchor[1].number()*p.frame.height);node["hidden"]=p.hidden;
}
Json lines(std::span<const ChromePathElement> elements){Json::Array result;for(const auto&e:elements){need(e.kind==ChromePathElement::Kind::move||e.kind==ChromePathElement::Kind::line,"Clock hand path is not linear");result.push_back(Json::Object{{"op",e.kind==ChromePathElement::Kind::move?"move":"line"},{"points",Json::Array{coordinate(e.geometry.x,e.geometry.y)}}});}return result;}
Json statusContent(const Json&reference,const DesktopClockArtwork&art,DesktopClockStyle style,DesktopWorkPhase phase,bool hover,std::string_view id){
    Json status=statusTemplate(reference,style,hover);status["id"]=std::string(id);auto children=status["children"].array();
    need(children[3]["name"].string()=="hud.clock.viewport"&&children[4]["name"].string()=="hud.clock.selection"&&children[5]["name"].string()=="hud.workMode.badge","Source status layers changed");
    auto pageChildren=children[3]["children"].array();need(pageChildren.size()==1&&pageChildren[0]["name"].string()=="hud.clock.page","Source clipped clock page changed");
    auto inner=pageChildren[0]["children"].array();need(inner.size()==3&&inner[0]["name"].string()=="hud.clock.style"&&inner[1]["name"].string()=="hud.clock.time"&&inner[2]["name"].string()=="hud.clock.date","Source clock page contents changed");
    auto shapes=inner[0]["children"].array();need(shapes.size()==3,"Source clock instrument topology changed");
    if(style==DesktopClockStyle::dial)shapes[1]["shape"]["path"]=lines(art.hands);
    text(shapes[2],art.seconds);inner[0]["children"]=std::move(shapes);text(inner[1],art.time);text(inner[2],art.date);
    pageChildren[0]["children"]=std::move(inner);children[3]["children"]=std::move(pageChildren);
    const auto badge=DesktopClockArtworkPlan::workBadge(phase);children[5]["text"]["string"]=std::string(badge);children[5]["hidden"]=badge.empty();status["children"]=std::move(children);return status;
}
// HUDClockPageViewport: the inner page (status children[3].children[0]).
const Json& pageOf(const Json&status){return status["children"].array()[3]["children"].array()[0];}
Json withoutPage(Json status){
    auto children=status["children"].array();auto pages=children[3]["children"].array();pages[0]["hidden"]=true;
    children[3]["children"]=std::move(pages);status["children"]=std::move(children);return status;
}
void reidentify(Json&node,std::string_view prefix){
    if(node["id"].isString())node["id"]=std::string(prefix)+"/"+node["id"].string();
    if(node["children"].isArray()){auto children=node["children"].array();for(auto&child:children)reidentify(child,prefix);node["children"]=std::move(children);}
}
// The page as its own surface in status-local coordinates: the viewport
// (14,12) and the page frame (-14,-12) cancel, so the page's 340 x 122 bounds
// map onto the status panel with an identity local transform.
Json transitionPage(const Json&page,const std::string&id){
    auto copy=page;reidentify(copy,id);copy["id"]=id;copy["hidden"]=false;
    copy["bounds"]=Json::Array{0,0,NativeChromePresentation::pageWidth,122};copy["anchorPoint"]=Json::Array{.5,.5};
    copy["position"]=Json::Array{NativeChromePresentation::pageWidth*.5,61};copy["frame"]=Json::Array{0,0,NativeChromePresentation::pageWidth,122};return copy;
}
constexpr std::string_view transitionRootID="hud.clock.transition";
std::string transitionSurfaceID(bool incoming){return std::string(transitionRootID)+(incoming?"/incoming":"/outgoing");}
void validContent(const DesktopChromeContent&c){
    auto valid=[](const std::string&s,std::size_t maximum){return s.size()<=maximum&&Json::validUtf8(s);};
    need(static_cast<unsigned>(c.style)<5&&static_cast<unsigned>(c.workPhase)<3&&valid(c.uppercaseShortcut,256)&&valid(c.localizedClose,512),"Invalid source chrome content");
    need(!c.reading||(valid(c.reading->time,128)&&valid(c.reading->date,256)),"Invalid caller-formatted chrome clock");
}
}
Json desktopChromeAppearanceReference(const Json&reference,const DesktopChromeAppearance&a){
    for(const auto c:a.effectiveAccent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid source chrome accent");
    auto result=retainedReference(reference);const auto ink=[](std::array<double,3>rgb,double alpha=1){return Json::Object{{"sRGB",Json::Array{rgb[0],rgb[1],rgb[2],alpha}}};};
    const auto gray=[&](double value){return ink({value,value,value});};
    auto header=result["header"]["children"].array();need(header.size()==2&&header[0]["text"]["string"].string()=="ENDFIELDHUD"&&header[1]["text"]["string"].string()=="SYSTEM INTERFACE","Original desktop header roles changed");
    header[0]["text"]["foregroundColor"]=gray(a.dark?.94:.12);header[1]["text"]["foregroundColor"]=gray(a.dark?.66:.39);result["header"]["children"]=std::move(header);
    auto footer=result["footer"]["children"].array();footer[0]["text"]["foregroundColor"]=gray(a.dark?.66:.39);result["footer"]["children"]=std::move(footer);
    auto styles=result["styles"].array();for(auto&row:styles){auto children=row["status"]["children"].array();need(children.size()==6&&children[3]["name"].string()=="hud.clock.viewport"&&children[5]["name"].string()=="hud.workMode.badge","Original clock color roles changed");
        children[1]["shape"]["strokeColor"]=ink(a.effectiveAccent,.70);children[4]["shape"]["strokeColor"]=ink(a.effectiveAccent);children[5]["text"]["foregroundColor"]=ink(a.effectiveAccent);
        auto pages=children[3]["children"].array();need(pages.size()==1,"Original clock page changed");auto inner=pages[0]["children"].array();need(inner.size()==3&&inner[0]["name"].string()=="hud.clock.style","Original clock artwork changed");
        auto shapes=inner[0]["children"].array();need(shapes.size()==3,"Original clock instrument changed");shapes[0]["shape"]["strokeColor"]=ink(a.effectiveAccent,.55);shapes[2]["text"]["foregroundColor"]=ink(a.effectiveAccent);
        inner[0]["children"]=std::move(shapes);pages[0]["children"]=std::move(inner);children[3]["children"]=std::move(pages);row["status"]["children"]=std::move(children);
    }result["styles"]=std::move(styles);return result;
}
Json NativeChromePresentation::combinedReferenceRoot(const Json&nativeRoot,const Json&reference){
    referenceValid(reference);Json root=nativeRoot;auto children=root["children"].array();
    children.push_back(reference["header"]);children.push_back(clockFontTemplate(statusTemplate(reference,DesktopClockStyle::digital)));children.push_back(reference["footer"]["children"].array()[0]);
    // This synthetic non-drawing grouping node owns no geometry. A neutral
    // camera must not make the entire HUD eligible for one large raster group.
    root["bounds"]=Json::Array{0,0,0,0};root["children"]=std::move(children);return root;
}
NativeChromePresentation::NativeChromePresentation(DesktopChromeProjectionPlan&projection,LayerScene&layers,const Json&reference,LayerRasterOptions options,LayerRasterizer&transitionRaster)
    :NativeChromePresentation(projection,layers,reference,std::move(options)){transitionRaster_=&transitionRaster;}
NativeChromePresentation::~NativeChromePresentation()=default;
NativeChromePresentation::NativeChromePresentation(DesktopChromeProjectionPlan&projection,LayerScene&layers,const Json&reference,LayerRasterOptions options)
    :projection_(&projection),layers_(&layers),options_(std::move(options)),reference_(retainedReference(reference)){
    referenceValid(reference_);headerID_=reference_["header"]["id"].string();footer_=reference_["footer"]["children"].array()[0];footerID_=footer_["id"].string();statusID_=statusTemplate(reference_,DesktopClockStyle::digital)["id"].string();
    unsigned index=0;for(const auto&id:{headerID_,footerID_,statusID_}){const auto surface=layers_->surfaceIndex(id);need(surface.has_value(),"Original chrome surface was not installed in LayerScene");surfaces_[index]=*surface;placements_[index].surface=*surface;++index;}
    for(auto&placement:placements_)placement.opacity=0;
    layers_->setPlacements(placements_);sceneRevision_=layers_->contentRevision();
}
bool NativeChromePresentation::setContent(const DesktopChromeContent&content){
    need(layers_->contentRevision()==sceneRevision_,"Recreate chrome bindings after structural LayerScene reload");
    validContent(content);if(content_==content)return false;bool changed=false;
    if(!content_||content_->style!=content.style||content_->reading!=content.reading||content_->workPhase!=content.workPhase||content_->clockHovered!=content.clockHovered){
        artwork_.update(content.style,content.reading);auto status=statusContent(reference_,artwork_.artwork(),content.style,content.workPhase,content.clockHovered,statusID_);
        // The page shown before a style change is the push's outgoing snapshot
        // (beginStyleTransition); any other sample ends that offer.
        const bool styleChanged=content_&&content_->style!=content.style;
        if(styleChanged){outgoingPage_=std::move(currentPage_);styleChangePending_=!outgoingPage_.isNull();}else{styleChangePending_=false;outgoingPage_=Json{};}
        currentPage_=pageOf(status);
        if(transition_){
            // The incoming page is live (time samples) while the push runs.
            const auto id=transitionSurfaceID(true);
            if(transitionLayers_->updateLocalContent(id,++transitionRevision_,transitionPage(currentPage_,id),options_))changed=true;
            status=withoutPage(std::move(status));
        }
        if(layers_->updateLocalContent(statusID_,++statusRevision_,status,options_)){changed=true;++stats_.statusRasterChanges;}
        statusWithoutPage_=transition_.has_value();
    }
    if(!content_||content_->uppercaseShortcut!=content.uppercaseShortcut||content_->localizedClose!=content.localizedClose){
        auto footer=footer_;footer["text"]["string"]=DesktopClockArtworkPlan::footerText(content.uppercaseShortcut,content.localizedClose);
        if(layers_->updateLocalContent(footerID_,++footerRevision_,footer,options_)){changed=true;++stats_.footerRasterChanges;}
    }
    content_=content;++stats_.contentUpdates;return changed;
}
bool NativeChromePresentation::setAppearance(const DesktopChromeAppearance&a){
    need(layers_->contentRevision()==sceneRevision_,"Recreate chrome bindings after structural LayerScene reload");if(appearance_==a)return false;
    // A recolored page cannot continue an old push: settle it first.
    if(transition_){transition_.reset();statusWithoutPage_=false;}
    styleChangePending_=false;outgoingPage_=Json{};
    auto reference=desktopChromeAppearanceReference(reference_,a);auto footer=reference["footer"]["children"].array()[0];auto status=statusTemplate(reference,DesktopClockStyle::digital);
    if(content_){status=statusContent(reference,artwork_.artwork(),content_->style,content_->workPhase,content_->clockHovered,statusID_);footer["text"]["string"]=DesktopClockArtworkPlan::footerText(content_->uppercaseShortcut,content_->localizedClose);currentPage_=pageOf(status);}
    bool changed=layers_->updateLocalContent(headerID_,++headerRevision_,reference["header"],options_);
    changed=layers_->updateLocalContent(statusID_,++statusRevision_,status,options_)||changed;
    changed=layers_->updateLocalContent(footerID_,++footerRevision_,footer,options_)||changed;
    reference_=std::move(reference);footer_=reference_["footer"]["children"].array()[0];appearance_=a;++stats_.contentUpdates;++stats_.statusRasterChanges;++stats_.footerRasterChanges;return changed;
}
bool NativeChromePresentation::update(const SourceWatchFrame&frame,const CameraFrame&camera,const DesktopChromeSettings&settings,float opacity){
    need(layers_->contentRevision()==sceneRevision_,"Recreate chrome bindings after structural LayerScene reload");
    need(std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid source chrome canvas opacity");projection_->update(frame.resolved,camera,settings.viewport,settings.contentScale);const auto&projected=projection_->projection();
    if(!projected.center||!projected.status)return false;
    const auto layout=DesktopChromeLayout::make(settings,*projected.center,*projected.status);
    const std::array<Matrix4,3>worlds{*projected.center*Matrix4::translation(layout.header.x,layout.header.y),layout.designToScreen*Matrix4::translation(layout.footerText.x,layout.footerText.y),layout.statusToScreen};
    if(previousWorlds_==worlds&&previousOpacity_==opacity)return false;
    for(unsigned i=0;i<3;++i){placements_[i].world=worlds[i];placements_[i].opacity=opacity;}layers_->setPlacements(placements_);previousWorlds_=worlds;previousOpacity_=opacity;++stats_.placementUpdates;
    if(transition_)placeTransition();
    return true;
}
void NativeChromePresentation::rasterizeStatus(bool withPage){
    if(!content_)return;auto status=statusContent(reference_,artwork_.artwork(),content_->style,content_->workPhase,content_->clockHovered,statusID_);
    if(!withPage)status=withoutPage(std::move(status));
    if(layers_->updateLocalContent(statusID_,++statusRevision_,status,options_))++stats_.statusRasterChanges;
    statusWithoutPage_=!withPage;
}
bool NativeChromePresentation::beginStyleTransition(bool forward,double time){
    need(std::isfinite(time),"Clock style transition requires a finite frame time");
    need(layers_->contentRevision()==sceneRevision_,"Recreate chrome bindings after structural LayerScene reload");
    if(!styleChangePending_||!transitionRaster_||!content_||outgoingPage_.isNull()||currentPage_.isNull())return false;
    styleChangePending_=false;
    const auto outID=transitionSurfaceID(false),inID=transitionSurfaceID(true);
    auto outgoing=transitionPage(outgoingPage_,outID),incoming=transitionPage(currentPage_,inID);outgoingPage_=Json{};
    if(!transitionLayers_){
        auto scene=std::make_unique<LayerScene>(*transitionRaster_);
        scene->load(Json::Object{{"id",std::string(transitionRootID)},{"bounds",Json::Array{0,0,0,0}},{"children",Json::Array{std::move(outgoing),std::move(incoming)}}},options_);
        const auto a=scene->surfaceIndex(outID),b=scene->surfaceIndex(inID);need(a&&b,"Clock transition pages were not rasterized");
        transitionSurfaces_={*a,*b};transitionLayers_=std::move(scene);
    }else{
        (void)transitionLayers_->updateLocalContent(outID,++transitionRevision_,outgoing,options_);
        (void)transitionLayers_->updateLocalContent(inID,++transitionRevision_,incoming,options_);
    }
    if(!statusWithoutPage_)rasterizeStatus(false);
    transition_=Transition{forward,time,0};placeTransition();return true;
}
void NativeChromePresentation::cancelStyleTransition(){
    styleChangePending_=false;outgoingPage_=Json{};
    if(!transition_)return;transition_.reset();rasterizeStatus(true);
}
bool NativeChromePresentation::advanceStyleTransition(double time){
    if(!transition_)return false;
    const double t=(time-transition_->start)/DesktopChromeTiming::styleDuration;
    if(!(t<1)){transition_.reset();rasterizeStatus(true);return false;}
    transition_->progress=DesktopChromeTiming::styleCurve.value(std::max(0.,t));placeTransition();return true;
}
std::optional<std::array<double,2>> NativeChromePresentation::transitionOffsets()const noexcept{
    if(!transition_)return std::nullopt;const double p=transition_->progress,direction=transition_->forward?1:-1;
    // CATransition push: the incoming page enters from the subtype's side,
    // pushing the outgoing snapshot out by the same distance.
    return std::array<double,2>{-direction*pageWidth*p,direction*pageWidth*(1-p)};
}
void NativeChromePresentation::placeTransition(){
    if(!transition_||!transitionLayers_)return;const auto offsets=*transitionOffsets();
    const bool placed=previousWorlds_.has_value();const auto world=placed?placements_[2].world:Matrix4{};
    transitionMask_[0]={core::source::inverseSourceMatrix(world),pageViewport,0};
    for(unsigned i=0;i<2;++i){auto&placement=transitionPlacements_[i];placement.surface=transitionSurfaces_[i];
        placement.world=world*Matrix4::translation(offsets[i],0);placement.opacity=placed?previousOpacity_:0.f;placement.masks=transitionMask_;}
    transitionLayers_->setPlacements(transitionPlacements_);
}
std::optional<Point> NativeChromePresentation::statusPoint(Point client,const Matrix4& camera,std::uint32_t pixelWidth,std::uint32_t pixelHeight,double scale)const{
    if(!previousWorlds_||!(previousOpacity_>0)||!pixelWidth||!pixelHeight||!(scale>0)||!std::isfinite(client.x)||!std::isfinite(client.y))return std::nullopt;
    const auto plane=Projection::viewport(camera*placements_[2].world,pixelWidth,pixelHeight);
    const auto local=plane.unproject({client.x*scale,client.y*scale});
    if(!local||!artwork_.artwork().hitBounds.contains(*local))return std::nullopt;
    return local;
}
} // namespace endfield::native
