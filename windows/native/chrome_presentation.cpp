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
        {"style",styleNames[i]},{"hover",hover},{"status",statusTemplate(reference,static_cast<DesktopClockStyle>(i),hover)}});
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
void validContent(const DesktopChromeContent&c){
    auto valid=[](const std::string&s,std::size_t maximum){return s.size()<=maximum&&Json::validUtf8(s);};
    need(static_cast<unsigned>(c.style)<5&&static_cast<unsigned>(c.workPhase)<3&&valid(c.uppercaseShortcut,256)&&valid(c.localizedClose,512),"Invalid source chrome content");
    need(!c.reading||(valid(c.reading->time,128)&&valid(c.reading->date,256)),"Invalid caller-formatted chrome clock");
}
}
Json NativeChromePresentation::combinedReferenceRoot(const Json&nativeRoot,const Json&reference){
    referenceValid(reference);Json root=nativeRoot;auto children=root["children"].array();
    children.push_back(reference["header"]);children.push_back(statusTemplate(reference,DesktopClockStyle::digital));children.push_back(reference["footer"]["children"].array()[0]);
    // This synthetic non-drawing grouping node owns no geometry. A neutral
    // camera must not make the entire HUD eligible for one large raster group.
    root["bounds"]=Json::Array{0,0,0,0};root["children"]=std::move(children);return root;
}
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
        if(layers_->updateLocalContent(statusID_,++statusRevision_,status,options_)){changed=true;++stats_.statusRasterChanges;}
    }
    if(!content_||content_->uppercaseShortcut!=content.uppercaseShortcut||content_->localizedClose!=content.localizedClose){
        auto footer=footer_;footer["text"]["string"]=DesktopClockArtworkPlan::footerText(content.uppercaseShortcut,content.localizedClose);
        if(layers_->updateLocalContent(footerID_,++footerRevision_,footer,options_)){changed=true;++stats_.footerRasterChanges;}
    }
    content_=content;++stats_.contentUpdates;return changed;
}
bool NativeChromePresentation::update(const SourceWatchFrame&frame,const CameraFrame&camera,const DesktopChromeSettings&settings,float opacity){
    need(layers_->contentRevision()==sceneRevision_,"Recreate chrome bindings after structural LayerScene reload");
    need(std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid source chrome canvas opacity");projection_->update(frame.resolved,camera,settings.viewport);const auto&projected=projection_->projection();
    if(!projected.center||!projected.status)return false;
    const auto layout=DesktopChromeLayout::make(settings,*projected.center,*projected.status);
    const std::array<Matrix4,3>worlds{*projected.center*Matrix4::translation(layout.header.x,layout.header.y),layout.designToScreen*Matrix4::translation(layout.footerText.x,layout.footerText.y),layout.statusToScreen};
    if(previousWorlds_==worlds&&previousOpacity_==opacity)return false;
    for(unsigned i=0;i<3;++i){placements_[i].world=worlds[i];placements_[i].opacity=opacity;}layers_->setPlacements(placements_);previousWorlds_=worlds;previousOpacity_=opacity;++stats_.placementUpdates;return true;
}
} // namespace endfield::native
