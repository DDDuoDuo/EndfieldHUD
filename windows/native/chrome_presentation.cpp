#include "native/chrome_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
using namespace core;
using namespace core::source;
namespace {
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
constexpr std::string_view recordDotID="hud.clock.recordDot";
Json coordinate(double x,double y){return Json::Array{x,y};}
Json rectangle(Rect r){return Json::Array{r.x,r.y,r.width,r.height};}
Json srgb(const ChromeColor&c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
// SystemHUDView.text(...): the header, footer and clock texts are
// HUDStandardTextLayer (Mac 1.3.0) instead of the monospaced system font.
void standardText(Json&node,int weight){
    auto&t=node["text"];need(t.isObject()&&t["fontSize"].isNumber(),"Original chrome text changed");
    t["font"]=standardFaceFont(t["fontSize"].number());t["fontWeight"]=weight;t["tabularFigures"]=false;
}
// The original status panel reduced to what 1.3.0 keeps from it: the plate,
// frame and selection marks (children 0-2, the plate's hover look per
// variant) and the clipped page viewport with an empty page. The page, the
// selection, the status row and the badge are built from the clock artwork.
Json panelTemplate(const Json&status){
    need(status["children"].isArray(),"Original status panel changed");const auto&children=status["children"].array();
    need(children.size()>=4&&children[0]["kind"].string()=="shape"&&children[1]["kind"].string()=="shape"&&children[2]["kind"].string()=="shape","Original status plate, frame and marks changed");
    const auto viewport=std::find_if(children.begin(),children.end(),[](const Json&c){return c["name"]==Json{"hud.clock.viewport"};});
    need(viewport!=children.end()&&(*viewport)["masksToBounds"].boolean(),"Original clipped clock viewport is missing");
    auto clip=*viewport;auto pages=clip["children"].array();need(pages.size()==1&&pages[0]["name"]==Json{"hud.clock.page"},"Original clipped clock page changed");
    pages[0]["children"]=Json::Array{};clip["children"]=std::move(pages);
    auto panel=status;panel["children"]=Json::Array{children[0],children[1],children[2],std::move(clip)};return panel;
}
const Json& panel(const Json&retained,bool hover){return retained["panels"].array().at(hover?1:0);}
const Json& exportedStatus(const Json&reference,bool hover){
    for(const auto&row:reference["styles"].array())if(row["style"].string()=="digital"&&row["hover"].boolean()==hover)return row["status"];
    throw std::invalid_argument("Missing original chrome status template");
}
void referenceValid(const Json&reference){
    need(reference["schemaVersion"].integer()==1&&reference["header"]["id"].isString()&&reference["header"]["children"].isArray()&&reference["header"]["children"].array().size()==2&&
        reference["footer"]["children"].isArray()&&reference["footer"]["children"].array().size()==1,"Invalid source chrome reference");
}
Json retainedReference(const Json&reference){
    referenceValid(reference);
    if(reference.contains("panels"))return reference; // already retained
    // Never retain oracle timelines, duplicate readings or benchmark layouts
    // in the running presentation: the header, footer and the panel per hover.
    auto header=reference["header"];auto titles=header["children"].array();
    need(titles[0]["text"]["string"].string()=="ENDFIELDHUD"&&titles[1]["text"]["string"].string()=="SYSTEM INTERFACE","Original desktop header roles changed");
    standardText(titles[0],600);standardText(titles[1],400);header["children"]=std::move(titles);
    auto footer=reference["footer"];auto hint=footer["children"].array();standardText(hint[0],400);footer["children"]=std::move(hint);
    return Json::Object{{"schemaVersion",1},{"header",std::move(header)},{"footer",std::move(footer)},
        {"panels",Json::Array{panelTemplate(exportedStatus(reference,false)),panelTemplate(exportedStatus(reference,true))}}};
}
Json node(std::string id,std::string kind,Rect frame){
    return Json::Object{{"id",std::move(id)},{"kind",std::move(kind)},{"bounds",Json::Array{0,0,frame.width,frame.height}},{"frame",rectangle(frame)},
        {"position",coordinate(frame.x,frame.y)},{"anchorPoint",Json::Array{0,0}},{"opacity",1},{"hidden",false},{"children",Json::Array{}}};
}
Json textNode(std::string id,const ChromeTextPlacement&p){
    auto n=node(std::move(id),"text",p.frame);n["hidden"]=p.hidden;
    n["text"]=Json::Object{{"string",p.text},{"fontSize",p.fontSize},{"font",standardFaceFont(p.fontSize)},{"fontWeight",p.weight},{"tabularFigures",p.tabular},
        {"alignment",std::array{"left","center","right"}[static_cast<unsigned>(p.alignment)]},{"wrapped",false},{"truncation",p.truncates?"end":"none"},
        {"foregroundColor",srgb(p.color)},{"runs",Json::Array{}}};
    return n;
}
Json command(const char*op,std::initializer_list<Point> points){Json::Array v;for(const auto&p:points)v.push_back(coordinate(p.x,p.y));return Json::Object{{"op",op},{"points",std::move(v)}};}
// CGPath: addRect is move + three lines + close; addEllipse(in:) four cubic
// quarters from (maxX, midY) through (midX, maxY), CoreGraphics' kappa.
Json path(std::span<const ChromePathElement> elements){
    constexpr double k=.5522847498;Json::Array out;
    for(const auto&e:elements){const auto&r=e.geometry;switch(e.kind){
    case ChromePathElement::Kind::move:out.push_back(command("move",{{r.x,r.y}}));break;
    case ChromePathElement::Kind::line:out.push_back(command("line",{{r.x,r.y}}));break;
    case ChromePathElement::Kind::close:out.push_back(command("close",{}));break;
    case ChromePathElement::Kind::rectangle:
        out.push_back(command("move",{{r.x,r.y}}));out.push_back(command("line",{{r.x+r.width,r.y}}));out.push_back(command("line",{{r.x+r.width,r.y+r.height}}));
        out.push_back(command("line",{{r.x,r.y+r.height}}));out.push_back(command("close",{}));break;
    case ChromePathElement::Kind::ellipse:{
        const double cx=r.x+r.width/2,cy=r.y+r.height/2,rx=r.width/2,ry=r.height/2;
        out.push_back(command("move",{{cx+rx,cy}}));
        out.push_back(command("cubic",{{cx+rx,cy+ry*k},{cx+rx*k,cy+ry},{cx,cy+ry}}));out.push_back(command("cubic",{{cx-rx*k,cy+ry},{cx-rx,cy+ry*k},{cx-rx,cy}}));
        out.push_back(command("cubic",{{cx-rx,cy-ry*k},{cx-rx*k,cy-ry},{cx,cy-ry}}));out.push_back(command("cubic",{{cx+rx*k,cy-ry},{cx+rx,cy-ry*k},{cx+rx,cy}}));
        out.push_back(command("close",{}));break;}
    }}
    return out;
}
Json shapeNode(std::string id,const ChromeShape&s){
    auto n=node(std::move(id),"shape",{0,0,0,0});
    n["shape"]=Json::Object{{"path",path(s.path)},{"fillColor",s.fill?srgb(*s.fill):Json{}},{"strokeColor",s.stroke?srgb(*s.stroke):Json{}},{"lineWidth",s.lineWidth},
        {"lineCap",std::array{"butt","round","square"}[static_cast<unsigned>(s.cap)]},{"lineJoin","miter"},{"fillRule","non-zero"}};
    return n;
}
// SystemHUDView's status panel: plate, frame, marks, the clipped page
// (HUDClockStyleArtwork.layer, time, date), the selection, the status row and
// the Work Mode badge, in the Mac's sublayer order.
Json statusContent(const Json&templ,const DesktopClockArtwork&a,std::string_view id){
    Json status=templ;status["id"]=std::string(id);auto children=status["children"].array();
    const std::string base(id);auto pages=children[3]["children"].array();
    auto style=node(base+"/style","layer",DesktopClockArtwork::body);
    style["children"]=Json::Array{shapeNode(base+"/style/track",a.track),shapeNode(base+"/style/fill",a.fill),shapeNode(base+"/style/instrument",a.instrument),
        shapeNode(base+"/style/hands",a.hands),shapeNode(base+"/style/secondHand",a.secondHand),textNode(base+"/style/seconds",a.seconds),textNode(base+"/style/aux",a.aux)};
    pages[0]["children"]=Json::Array{std::move(style),textNode(base+"/time",a.time),textNode(base+"/date",a.date)};children[3]["children"]=std::move(pages);
    children.push_back(shapeNode(base+"/selection",a.selection));
    auto row=node(base+"/status","layer",DesktopClockArtwork::body);
    row["children"]=Json::Array{shapeNode(base+"/status/rail",a.rail),textNode(base+"/status/recording",a.recordLabel)};
    children.push_back(std::move(row));children.push_back(textNode(base+"/workBadge",a.workBadge));
    status["children"]=std::move(children);return status;
}
Json dotContent(const ChromeColor&color){
    auto n=node(std::string(recordDotID),"layer",{0,0,7,7});n["position"]=Json::Array{0,0};n["frame"]=Json::Array{0,0,7,7};n["backgroundColor"]=srgb(color);return n;
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
DesktopChromeAppearance defaultAppearance(){return {};}
}
Json desktopChromeAppearanceReference(const Json&reference,const DesktopChromeAppearance&a){
    for(const auto c:a.effectiveAccent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid source chrome accent");
    auto result=retainedReference(reference);const auto ink=[](std::array<double,3>rgb,double alpha=1){return Json::Object{{"sRGB",Json::Array{rgb[0],rgb[1],rgb[2],alpha}}};};
    const auto gray=[&](double value){return ink({value,value,value});};
    auto header=result["header"]["children"].array();
    header[0]["text"]["foregroundColor"]=gray(a.dark?.94:.12);header[1]["text"]["foregroundColor"]=gray(a.dark?.66:.39);result["header"]["children"]=std::move(header);
    auto footer=result["footer"]["children"].array();footer[0]["text"]["foregroundColor"]=gray(a.dark?.66:.39);result["footer"]["children"]=std::move(footer);
    // updateStatusPanel: the frame strokes the accent at .70; the plate and
    // marks keep their authored dark look in either theme.
    auto panels=result["panels"].array();for(auto&p:panels){auto children=p["children"].array();children[1]["shape"]["strokeColor"]=ink(a.effectiveAccent,.70);p["children"]=std::move(children);}
    result["panels"]=std::move(panels);return result;
}
Json NativeChromePresentation::combinedReferenceRoot(const Json&nativeRoot,const Json&reference){
    const auto retained=retainedReference(reference);Json root=nativeRoot;auto children=root["children"].array();
    children.push_back(retained["header"]);children.push_back(panel(retained,false));children.push_back(retained["footer"]["children"].array()[0]);
    children.push_back(dotContent({.45,.45,.45,1}));
    // This synthetic non-drawing grouping node owns no geometry. A neutral
    // camera must not make the entire HUD eligible for one large raster group.
    root["bounds"]=Json::Array{0,0,0,0};root["children"]=std::move(children);return root;
}
NativeChromePresentation::~NativeChromePresentation()=default;
NativeChromePresentation::NativeChromePresentation(DesktopChromeProjectionPlan&projection,LayerScene&layers,const Json&reference,LayerRasterOptions options,LayerRasterizer&rasterizer)
    :projection_(&projection),layers_(&layers),options_(std::move(options)),reference_(retainedReference(reference)),
     // NSString.size(withAttributes:) of one clock line: the same descriptor
     // the text is painted from (standard face, weight, tnum).
     artwork_([this](std::string_view text,double size,int weight,bool tabular){
         const Json descriptor=Json::Object{{"string",std::string(text)},{"fontSize",size},{"font",standardFaceFont(size)},{"fontWeight",weight},
             {"tabularFigures",tabular},{"wrapped",false},{"runs",Json::Array{}}};
         return raster_->measureSourceText("hud.clock.measure",descriptor,1e6,options_).width;}),
     raster_(&rasterizer){
    headerID_=reference_["header"]["id"].string();footer_=reference_["footer"]["children"].array()[0];footerID_=footer_["id"].string();statusID_=panel(reference_,false)["id"].string();
    unsigned index=0;for(const auto&id:{headerID_,footerID_,statusID_,std::string(recordDotID)}){const auto surface=layers_->surfaceIndex(id);need(surface.has_value(),"Original chrome surface was not installed in LayerScene");surfaces_[index]=*surface;placements_[index].surface=*surface;++index;}
    for(auto&placement:placements_)placement.opacity=0;
    layers_->setPlacements(placements_);sceneRevision_=layers_->contentRevision();
}
// Content event: the clock artwork for the current content and accent.
void NativeChromePresentation::updateArtwork(){
    const auto accent=(appearance_?*appearance_:defaultAppearance()).effectiveAccent;
    artwork_.update(content_->style,content_->reading,{content_->workPhase,content_->recording},accent);
}
void NativeChromePresentation::rasterizeDot(){
    const auto&color=artwork_.artwork().recordDotColor;if(dotColor_==color)return;
    if(layers_->updateLocalContent(std::string(recordDotID),++dotRevision_,dotContent(color),options_))++stats_.statusRasterChanges;dotColor_=color;
}
bool NativeChromePresentation::setContent(const DesktopChromeContent&content){
    need(layers_->contentRevision()==sceneRevision_,"Recreate chrome bindings after structural LayerScene reload");
    validContent(content);if(content_==content)return false;bool changed=false;
    const auto previous=content_;content_=content;
    if(!previous||previous->style!=content.style||previous->reading!=content.reading||previous->workPhase!=content.workPhase||previous->clockHovered!=content.clockHovered||previous->recording!=content.recording){
        updateArtwork();auto status=statusContent(panel(reference_,content.clockHovered),artwork_.artwork(),statusID_);
        // The page shown before a style change is the push's outgoing snapshot
        // (beginStyleTransition); any other sample ends that offer.
        const bool styleChanged=previous&&previous->style!=content.style;
        if(styleChanged){outgoingPage_=std::move(currentPage_);styleChangePending_=!outgoingPage_.isNull();}else{styleChangePending_=false;outgoingPage_=Json{};}
        currentPage_=pageOf(status);
        if(transition_){
            // The incoming page is live (time samples) while the push runs.
            const auto id=transitionSurfaceID(true);
            if(transitionLayers_->updateLocalContent(id,++transitionRevision_,transitionPage(currentPage_,id),options_))changed=true;
            status=withoutPage(std::move(status));
        }
        if(layers_->updateLocalContent(statusID_,++statusRevision_,status,options_)){changed=true;++stats_.statusRasterChanges;}
        statusWithoutPage_=transition_.has_value();rasterizeDot();
        // HUDClockStyleArtwork.update: each sample while recording fades the
        // dot from where it is to this second's opacity (on the next frame).
        if(content.recording&&!content.reduceMotion)blinkTarget_=artwork_.artwork().recordDotOpacity;
        else{blinkTarget_.reset();blink_={};dotOpacity_=1;}
    }else if(previous->reduceMotion!=content.reduceMotion&&content.reduceMotion){blinkTarget_.reset();blink_={};dotOpacity_=1;}
    if(!previous||previous->uppercaseShortcut!=content.uppercaseShortcut||previous->localizedClose!=content.localizedClose){
        auto footer=footer_;footer["text"]["string"]=DesktopClockArtworkPlan::footerText(content.uppercaseShortcut,content.localizedClose);
        if(layers_->updateLocalContent(footerID_,++footerRevision_,footer,options_)){changed=true;++stats_.footerRasterChanges;}
    }
    ++stats_.contentUpdates;return changed;
}
bool NativeChromePresentation::setAppearance(const DesktopChromeAppearance&a){
    need(layers_->contentRevision()==sceneRevision_,"Recreate chrome bindings after structural LayerScene reload");if(appearance_==a)return false;
    // A recolored page cannot continue an old push: settle it first.
    if(transition_){transition_.reset();statusWithoutPage_=false;}
    styleChangePending_=false;outgoingPage_=Json{};
    auto reference=desktopChromeAppearanceReference(reference_,a);auto footer=reference["footer"]["children"].array()[0];
    appearance_=a;auto status=panel(reference,content_&&content_->clockHovered);
    if(content_){updateArtwork();status=statusContent(status,artwork_.artwork(),statusID_);footer["text"]["string"]=DesktopClockArtworkPlan::footerText(content_->uppercaseShortcut,content_->localizedClose);currentPage_=pageOf(status);}
    bool changed=layers_->updateLocalContent(headerID_,++headerRevision_,reference["header"],options_);
    changed=layers_->updateLocalContent(statusID_,++statusRevision_,status,options_)||changed;
    changed=layers_->updateLocalContent(footerID_,++footerRevision_,footer,options_)||changed;
    reference_=std::move(reference);footer_=reference_["footer"]["children"].array()[0];++stats_.contentUpdates;++stats_.statusRasterChanges;++stats_.footerRasterChanges;return changed;
}
bool NativeChromePresentation::update(const SourceWatchFrame&frame,const CameraFrame&camera,const DesktopChromeSettings&settings,float opacity){
    need(layers_->contentRevision()==sceneRevision_,"Recreate chrome bindings after structural LayerScene reload");
    need(std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid source chrome canvas opacity");projection_->update(frame.resolved,camera,settings.viewport,settings.contentScale);const auto&projected=projection_->projection();
    if(!projected.center||!projected.status)return false;
    const auto layout=DesktopChromeLayout::make(settings,*projected.center,*projected.status);const auto&dot=artwork_.artwork().recordDot;
    const std::array<Matrix4,4>worlds{*projected.center*Matrix4::translation(layout.header.x,layout.header.y),layout.designToScreen*Matrix4::translation(layout.footerText.x,layout.footerText.y),
        layout.statusToScreen,layout.statusToScreen*Matrix4::translation(dot.x,dot.y)};
    const auto dotOpacity=static_cast<float>(dotOpacity_);
    if(previousWorlds_==worlds&&previousOpacity_==opacity&&placedDot_==dotOpacity)return false;
    for(unsigned i=0;i<4;++i){placements_[i].world=worlds[i];placements_[i].opacity=i==3?opacity*dotOpacity:opacity;}
    layers_->setPlacements(placements_);previousWorlds_=worlds;previousOpacity_=opacity;placedDot_=dotOpacity;++stats_.placementUpdates;
    if(transition_)placeTransition();
    return true;
}
void NativeChromePresentation::rasterizeStatus(bool withPage){
    if(!content_)return;auto status=statusContent(panel(reference_,content_->clockHovered),artwork_.artwork(),statusID_);
    if(!withPage)status=withoutPage(std::move(status));
    if(layers_->updateLocalContent(statusID_,++statusRevision_,status,options_))++stats_.statusRasterChanges;
    statusWithoutPage_=!withPage;
}
bool NativeChromePresentation::beginStyleTransition(bool forward,double time){
    need(std::isfinite(time),"Clock style transition requires a finite frame time");
    need(layers_->contentRevision()==sceneRevision_,"Recreate chrome bindings after structural LayerScene reload");
    if(!styleChangePending_||!content_||outgoingPage_.isNull()||currentPage_.isNull())return false;
    styleChangePending_=false;
    const auto outID=transitionSurfaceID(false),inID=transitionSurfaceID(true);
    auto outgoing=transitionPage(outgoingPage_,outID),incoming=transitionPage(currentPage_,inID);outgoingPage_=Json{};
    if(!transitionLayers_){
        auto scene=std::make_unique<LayerScene>(*raster_);
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
bool NativeChromePresentation::advance(double time){
    need(std::isfinite(time),"Chrome animation requires a finite frame time");
    // The record dot: a pending sample starts its .35 s ease-in-out from the
    // current opacity (CABasicAnimation from presentation opacity).
    if(blinkTarget_){blink_={dotOpacity_,*blinkTarget_,time,true};blinkTarget_.reset();}
    if(blink_.running){
        const double t=(time-blink_.start)/recordDotFade;
        if(!(t<1)){dotOpacity_=blink_.to;blink_.running=false;}
        else dotOpacity_=blink_.from+(blink_.to-blink_.from)*DesktopChromeTiming::styleCurve.value(std::max(0.,t));
    }
    bool running=blink_.running;
    if(transition_){
        const double t=(time-transition_->start)/DesktopChromeTiming::styleDuration;
        if(!(t<1)){transition_.reset();rasterizeStatus(true);}
        else{transition_->progress=DesktopChromeTiming::styleCurve.value(std::max(0.,t));placeTransition();running=true;}
    }
    return running;
}
bool NativeChromePresentation::requiresFrames(double time)const noexcept{
    return transition_.has_value()||blinkTarget_.has_value()||(blink_.running&&time<blink_.start+recordDotFade+1./60);
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
    if(!local||!DesktopClockArtwork::hitBounds.contains(*local))return std::nullopt;
    return local;
}
} // namespace endfield::native
