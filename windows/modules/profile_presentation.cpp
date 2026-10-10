#include "modules/profile_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::modules {
namespace {
using Json=ehud::data::Json;using R=core::Rect;using P=core::Point;using C=ProfileColor;
constexpr double kappa=0.5522847498;
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
C gray(double w,double a=1){return {w,w,w,a};}
C alpha(C c,double a){c[3]=a;return c;}
Json color(const C&c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
Json pair(double a,double b){return Json::Array{a,b};}
std::string L(const char*en,const char*zh,core::Language l){return core::localized(en,zh,l);}
// CGPath elements in absolute module coordinates; leaves store them locally.
struct Path {
    struct Op{const char*op;std::vector<P>points;};
    std::vector<Op>ops;
    void move(P p){ops.push_back({"move",{p}});}
    void line(P p){ops.push_back({"line",{p}});}
    void quad(P c,P p){ops.push_back({"quadratic",{c,p}});}
    void cubic(P a,P b,P p){ops.push_back({"cubic",{a,b,p}});}
    void close(){ops.push_back({"close",{}});}
    // CGPath(rect:): move(min), three lines, close.
    void rect(R r){move({r.x,r.y});line({r.x+r.width,r.y});line({r.x+r.width,r.y+r.height});line({r.x,r.y+r.height});close();}
    // CGPath(ellipseIn:): starts at (maxX, midY) with four cubic quadrants.
    void ellipse(R r){
        const double cx=r.x+r.width/2,cy=r.y+r.height/2,rx=r.width/2,ry=r.height/2,mx=r.x+r.width,my=r.y+r.height;
        move({mx,cy});cubic({mx,cy+ry*kappa},{cx+rx*kappa,my},{cx,my});cubic({cx-rx*kappa,my},{r.x,cy+ry*kappa},{r.x,cy});
        cubic({r.x,cy-ry*kappa},{cx-rx*kappa,r.y},{cx,r.y});cubic({cx+rx*kappa,r.y},{mx,cy-ry*kappa},{mx,cy});close();
    }
    // CGPath(roundedRect:): starts at (maxX, midY), counter-clockwise in y-down.
    void rounded(R r,double c){
        const double mx=r.x+r.width,my=r.y+r.height,k=c*kappa;
        move({mx,r.y+r.height/2});line({mx,my-c});cubic({mx,my-c+k},{mx-c+k,my},{mx-c,my});line({r.x+c,my});cubic({r.x+c-k,my},{r.x,my-c+k},{r.x,my-c});
        line({r.x,r.y+c});cubic({r.x,r.y+c-k},{r.x+c-k,r.y},{r.x+c,r.y});line({mx-c,r.y});cubic({mx-c+k,r.y},{mx,r.y+c-k},{mx,r.y+c});close();
    }
    void cut(R r){
        const double c=std::min(4.,std::min(r.width,r.height)/3);
        move({r.x+c,r.y});line({r.x+r.width,r.y});line({r.x+r.width,r.y+r.height-c});line({r.x+r.width-c,r.y+r.height});line({r.x,r.y+r.height});line({r.x,r.y+c});close();
    }
    void polygon(std::initializer_list<P>points,double sx,double sy){
        bool first=true;for(const auto p:points){if(first)move({p.x*sx,p.y*sy});else line({p.x*sx,p.y*sy});first=false;}close();
    }
    R bounds()const{
        double x0=std::numeric_limits<double>::max(),y0=x0,x1=-x0,y1=-x0;
        for(const auto&o:ops)for(const auto&p:o.points){x0=std::min(x0,p.x);y0=std::min(y0,p.y);x1=std::max(x1,p.x);y1=std::max(y1,p.y);}
        if(x0>x1)return {0,0,0,0};
        return {x0,y0,x1-x0,y1-y0};
    }
    Json local(P origin)const{
        Json::Array out;out.reserve(ops.size());
        for(const auto&o:ops){Json::Array points;for(const auto&p:o.points)points.push_back(pair(p.x-origin.x,p.y-origin.y));out.push_back(Json::Object{{"op",o.op},{"points",std::move(points)}});}
        return out;
    }
};
Json node(std::string id,R frame,const char*kind="layer"){
    return Json::Object{{"id",std::move(id)},{"kind",kind},{"bounds",Json::Array{0,0,frame.width,frame.height}},{"position",pair(frame.x,frame.y)},{"anchorPoint",pair(0,0)},{"children",Json::Array{}}};
}
Json shapeDescriptor(const Path&path,P origin,std::optional<C>fill,std::optional<C>stroke,double width,const char*rule="non-zero"){
    return Json::Object{{"path",path.local(origin)},{"fillColor",fill?color(*fill):Json{}},{"strokeColor",stroke?color(*stroke):Json{}},{"lineWidth",width},
        {"lineCap","butt"},{"lineJoin","miter"},{"miterLimit",10},{"fillRule",rule}};
}
// A shape leaf sized to its geometry plus its stroke, so nothing is clipped.
Json shape(std::string id,const Path&path,std::optional<C>fill,std::optional<C>stroke={},double width=1){
    auto b=path.bounds();const double pad=(stroke?width/2:0)+1;b={b.x-pad,b.y-pad,b.width+2*pad,b.height+2*pad};
    auto n=node(std::move(id),b,"shape");n["shape"]=shapeDescriptor(path,{b.x,b.y},fill,stroke,width);return n;
}
// A shape whose authored frame owns local path coordinates (source sublayer).
Json shapeIn(std::string id,R frame,const Path&local,std::optional<C>fill,std::optional<C>stroke={},double width=1,const char*rule="non-zero"){
    auto n=node(std::move(id),frame,"shape");n["shape"]=shapeDescriptor(local,{0,0},fill,stroke,width,rule);return n;
}
enum class Weight {regular,medium,semibold,bold};
Json text(std::string id,R frame,const std::string&value,double size,Weight weight,const C&ink,double scale,const char*alignment="left",bool wrapped=false){
    need(Json::validUtf8(value)&&value.size()<=32768,"Invalid profile caption text");
    auto n=node(std::move(id),frame,"text");
    const char*names[]{".AppleSystemUIFont",".AppleSystemUIFontMedium",".AppleSystemUIFontDemi",".AppleSystemUIFontBold"};
    const int traits=weight==Weight::semibold||weight==Weight::bold?2:0;
    n["contentsScale"]=std::min(4.,std::ceil(std::clamp(scale,1.,4.)*1.35)); // HUDRenderScale text oversampling
    n["text"]=Json::Object{{"string",value},{"fontSize",size},{"font",Json::Object{{"postScriptName",names[static_cast<int>(weight)]},{"familyName",".AppleSystemUIFont"},{"pointSize",size},{"symbolicTraits",traits}}},
        {"foregroundColor",color(ink)},{"alignment",alignment},{"wrapped",wrapped},{"truncation","end"}};
    return n;
}
ProfileGradient gradient(std::vector<C>colors,std::vector<double>locations,P start,P end){return {std::move(colors),std::move(locations),start,end};}
struct Builder {
    ProfileArtworkPart part;Json::Array children;std::vector<ProfileHighlight>&highlights;ProfilePartKind kind;C accent;bool suppressed{};std::size_t serial{};
    Builder(std::vector<ProfileHighlight>&h,ProfilePartKind k,C a):highlights(h),kind(k),accent(a){}
    std::string id(const char*role){static constexpr const char*prefixes[]{"profile.fields.","profile.toolbar.","profile.popover."};return std::string(prefixes[static_cast<int>(kind)])+std::to_string(serial++)+"."+role;}
    void add(Json n,std::string animation={}){
        const auto&b=n["bounds"].array();const auto&p=n["position"].array();
        part.surfaces.push_back({n["id"].string(),{p[0].number(),p[1].number(),b[2].number(),b[3].number()},{},false,std::move(animation)});children.push_back(std::move(n));
    }
    void highlight(R rect,ProfileHighlightShape kind,bool framed=false){
        Path control,rim;R local{0,0,rect.width,rect.height};R expanded{framed?-2.:0.,framed?-2.:0.,rect.width+(framed?4:0),rect.height+(framed?4:0)};
        const auto draw=[&](Path&p,R r){switch(kind){case ProfileHighlightShape::rounded:p.rounded(r,3);break;case ProfileHighlightShape::ellipse:p.ellipse(r);break;case ProfileHighlightShape::cutCorner:p.cut(r);break;}};
        draw(control,local);draw(rim,expanded);
        const auto index=highlights.size();ProfileHighlight h{rect,kind,framed,true,id("tint"),{}};h.rim=std::string(h.tint.substr(0,h.tint.size()-4))+"rim";
        auto tint=shapeIn(h.tint,rect,control,alpha(accent,.30));
        R rimFrame{rect.x-(framed?3.:1.),rect.y-(framed?3.:1.),rect.width+(framed?6:2),rect.height+(framed?6:2)};
        Path shifted;for(const auto&o:rim.ops){Path::Op v{o.op,{}};for(const auto&q:o.points)v.points.push_back({q.x+rect.x-rimFrame.x,q.y+rect.y-rimFrame.y});shifted.ops.push_back(std::move(v));}
        auto stroke=shapeIn(h.rim,rimFrame,shifted,{},accent,.9);
        highlights.push_back(std::move(h));highlights.back().enabled=!suppressed;
        part.surfaces.push_back({highlights.back().tint,rect,index,false,{}});children.push_back(std::move(tint));
        part.surfaces.push_back({highlights.back().rim,rimFrame,index,true,{}});children.push_back(std::move(stroke));
    }
    ProfileArtworkPart finish(const char*root){part.layers=node(root,{0,0,0,0}); // zero root: every child is its own retained surface
        part.layers["children"]=std::move(children);return std::move(part);}
};
}

ProfileArtwork prepareProfileArtwork(const ProfileState&state,const ProfileAppearance&appearance,const ProfileContents&contents){
    need(std::isfinite(appearance.scale)&&appearance.scale>=1&&appearance.scale<=8,"Invalid profile render scale");
    for(const auto c:appearance.hudAccent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid HUD accent");
    const auto&p=state.preview();const auto l=state.language();const bool dark=appearance.dark,backdrop=contents.background;const double scale=appearance.scale;
    const auto rgb=profileAccent(p,appearance.hudAccent);const C accent{rgb[0],rgb[1],rgb[2],1};
    const C ink=backdrop?gray(1):gray(dark?.96:.10),muted=alpha(ink,.65);
    ProfileArtwork result;result.accent=accent;result.fieldsOpacity=state.textHidden()?0:1;
    Builder fields(result.highlights,ProfilePartKind::fields,accent),toolbar(result.highlights,ProfilePartKind::toolbar,accent),popover(result.highlights,ProfilePartKind::popover,accent);
    const bool open=state.popover().has_value();fields.suppressed=toolbar.suppressed=open;
    if(backdrop)result.contrast=ProfileMaskedGradient{{0,0,272,334},gradient({gray(0,.52),gray(0,.46),gray(0,0)},{0,.80,1},{0,.5},{1,.5}),
        gradient({gray(0,0),gray(0,1),gray(0,1),gray(0,0)},{0,.04,.95,1},{.5,0},{.5,1})};
    fields.add(text(fields.id("heading"),{18,4,360,21},L("PERSONAL PROFILE","个人名片",l),13,Weight::bold,ink,scale));
    {   // HUDPortraitArtwork.makeLayer at 55x55 with the selected source frame.
        constexpr R frame{28,44,55,55};auto portrait=node(fields.id("portrait"),frame);portrait["allowsGroupOpacity"]=false;
        auto photo=node("profile.portrait.image",{0,0,55,55});photo["backgroundColor"]=color(gray(.22,.85));photo["masksToBounds"]=true;photo["contentsScale"]=scale;
        if(!contents.avatar.isNull()){photo["contents"]=contents.avatar;photo["contentsGravity"]="resize";}
        else{Path s;s.ellipse({55*.36,55*.20,55*.30,55*.31});s.move({55*.18,55*.86});s.cubic({55*.24,55*.45},{55*.77,55*.45},{55*.83,55*.86});s.close();
            photo["children"]=Json::Array{shapeIn("profile.portrait.silhouette",{0,0,55,55},s,alpha(ink,.63))};}
        const double unit=55./136,side=195*unit;
        auto ring=node("profile.portrait.frame",{27.5+.4164*unit-side/2,27.5-side/2,side,side});ring["contentsScale"]=scale;ring["contentsGravity"]="resize";
        if(!contents.frame.isNull())ring["contents"]=contents.frame;
        portrait["children"]=Json::Array{std::move(photo),std::move(ring)};fields.add(std::move(portrait),"portrait");
    }
    fields.add(text(fields.id("name"),{97,38,286,28},p.name+"#"+p.tag,15,Weight::semibold,ink,scale),"name");
    {Path m;m.move({99,61});m.line({99,69});m.line({106,69});m.move({136,61});m.line({143,61});m.line({143,69});m.move({109,61});m.line({118,69});m.move({118,61});m.line({109,69});
     fields.add(shape(fields.id("nameMark"),m,{},alpha(muted,.45),.9));}
    {Path r;r.rect({98,72,20,3});fields.add(shape(fields.id("registry"),r,alpha(muted,.75)));}
    constexpr R dateLabel{98,77,67,16},dateValue{165,77,90,16};
    {Path r;r.rect(dateLabel);fields.add(shape(fields.id("dateLabel"),r,gray(0,.73)));}
    for(const double x:{102.,109.}){Path t;t.move({x,81});t.line({x+5,85});t.line({x,89});t.close();fields.add(shape(fields.id("dateArrow"),t,accent));}
    const auto dateField=p.showsBirthday?ProfileField::birthday:ProfileField::awakeningDate;
    // Bold 8 pt and semibold 10 pt system line boxes (ceil(ascender-descender)).
    fields.add(text(fields.id("dateCaption"),{117,dateLabel.y+dateLabel.height/2-5,46,10},p.showsBirthday?L("Birthday","生日",l):L("Awakening","苏醒日",l),8,Weight::bold,gray(1),scale),"dateCaption");
    {Path r;r.rect(dateValue);fields.add(shape(fields.id("dateValue"),r,gray(1,.90)));}
    fields.add(text(fields.id("dateText"),{169,dateValue.y+dateValue.height/2-6,84,12},state.value(dateField),10,Weight::semibold,gray(.16),scale),"value:"+std::string(profileFieldID(dateField)));
    fields.highlight(dateLabel,ProfileHighlightShape::rounded);
    if(state.canEdit(dateField))fields.highlight(dateValue,ProfileHighlightShape::rounded);
    fields.add(text(fields.id("uid"),{98,99,260,18},"UID: "+p.displayedUID(),11,Weight::regular,muted,scale),"value:playerID");
    {   constexpr R menu{20,116,19,19};auto n=node(fields.id("menu"),menu);n["backgroundColor"]=color(alpha(ink,.85));n["cornerRadius"]=menu.height/2;
        Json::Array dots;for(const double x:{5.,9.5,14.}){Path d;d.rect({x-1,8.5,2,2});dots.push_back(shapeIn("profile.menu.dot."+std::to_string(dots.size()),{0,0,19,19},d,gray(dark||backdrop?.12:.94)));}
        n["children"]=std::move(dots);fields.add(std::move(n));fields.highlight(menu,ProfileHighlightShape::ellipse);
    }
    const auto level=[&](ProfileField f,double y){
        Path band;band.rect({18,y,219,25});fields.add(shape(fields.id("levelBand"),band,gray(0,.56)));
        constexpr double sx=22./34,sy=21./32;const R artFrame{21,y+2,22,21};
        if(f==ProfileField::permissionLevel){
            Path a;a.polygon({{5,10},{15,17},{15,30},{5,23}},sx,sy);a.move({10*sx,6*sy});a.line({22*sx,14*sy});a.line({22*sx,28*sy});a.move({17*sx,2*sy});a.line({29*sx,10*sy});a.line({29*sx,25*sy});
            fields.add(shapeIn(fields.id("authority"),artFrame,a,{},gray(1),22./34*2.7));
        }else{
            Path a;a.polygon({{8,8},{12,4},{12,10}},sx,sy);a.polygon({{22,8},{25,4},{26,12}},sx,sy);a.polygon({{8,24},{12,28},{12,22}},sx,sy);a.polygon({{22,24},{25,28},{26,20}},sx,sy);
            a.polygon({{5,5},{7,10},{6,16},{7,22},{5,27},{3,21},{3,11}},sx,sy);a.polygon({{1,8},{2,13},{2,19},{1,24},{0,20},{0,12}},sx,sy);
            a.polygon({{29,5},{27,10},{28,16},{27,22},{29,27},{31,21},{31,11}},sx,sy);a.polygon({{33,8},{32,13},{32,19},{33,24},{34,20},{34,12}},sx,sy);
            Path eye;eye.ellipse({7*sx,6*sy,20*sx,20*sy});eye.ellipse({13*sx,12*sy,8*sx,8*sy});
            auto crest=shapeIn(fields.id("exploration"),artFrame,a,gray(1));crest["children"]=Json::Array{shapeIn("profile.level.aperture",{0,0,22,21},eye,gray(1),{},1,"even-odd")};
            fields.add(std::move(crest));
        }
        fields.add(text(fields.id("levelTitle"),{46,y+4,133,19},profileFieldTitle(f,l),12,Weight::regular,gray(1),scale));
        const auto r=profileFieldRect(f);fields.highlight(r,ProfileHighlightShape::rounded);
        fields.add(text(fields.id("levelValue"),{r.x+5,r.y+1,r.width-10,r.height-2},state.value(f),20,Weight::medium,gray(1),scale,"right"),"value:"+std::string(profileFieldID(f)));
    };
    const auto marker=[&](double y){Path m;m.rect({7,y+2,6,3});fields.add(shape(fields.id("marker"),m,accent));};
    level(ProfileField::permissionLevel,145);level(ProfileField::explorationLevel,177);marker(211);
    for(const auto f:{ProfileField::operatorsCount,ProfileField::weaponsCount,ProfileField::archivesCount}){
        const auto r=profileFieldRect(f);Path box;box.rect(r);fields.add(shape(fields.id("counterBox"),box,gray(dark||backdrop?.01:1,.29)));
        fields.highlight(r,ProfileHighlightShape::rounded);
        fields.add(text(fields.id("counterValue"),{r.x+6,r.y+1,r.width-12,25},state.value(f),21,Weight::medium,accent,scale),"value:"+std::string(profileFieldID(f)));
        fields.add(text(fields.id("counterTitle"),{r.x+6,r.y+26,r.width-9,15},"· "+profileFieldTitle(f,l),9,Weight::regular,alpha(accent,.88),scale));
    }
    marker(272);
    fields.add(text(fields.id("construction"),{18,269,187,17},L("REGION CONSTRUCTION","地区建设概况",l),10,Weight::bold,accent,scale));
    {Path r;r.rect({18,290,190,31});fields.add(shape(fields.id("workBand"),r,alpha(ink,.13)));}
    fields.add(text(fields.id("workTitle"),{25,299,75,16},L("Work Mode","工作模式",l),10,Weight::regular,ink,scale));
    fields.add(text(fields.id("workHours"),{105,292,70,28},state.shownHours(),21,Weight::medium,ink,scale,"right"),"work");
    fields.add(text(fields.id("workUnit"),{180,302,25,13},L("h","小时",l),8,Weight::regular,muted,scale));
    {   // Solid blocks with tapered tails; the closing mark is mirrored.
        const C quoteInk=dark||backdrop?gray(1,.96):ink;
        const auto quote=[](bool mirrored){Path q;for(const double x:{0.,9.}){
            const std::initializer_list<P>points{{x,0},{x+6,0},{x+6,6},{x+2,6},{x+2,8},{x+6,12},{x+4.5,12},{x,8}};
            bool first=true;for(auto v:points){if(mirrored)v.x=15-v.x;if(first)q.move(v);else q.line(v);first=false;}q.close();}return q;};
        fields.add(shapeIn(fields.id("quoteOpen"),{257,159,15,12},quote(false),quoteInk));
        fields.add(shapeIn(fields.id("quoteClose"),{374,244,15,12},quote(true),quoteInk));
        fields.add(text(fields.id("introduction"),{264,181,118,59},p.introduction.empty()?"…":p.introduction,10,Weight::medium,ink,scale,"left",true),"value:introduction");
        Path pencil;pencil.move({373,171});pencil.line({383,161});pencil.line({386,164});pencil.line({376,174});pencil.line({372,175});pencil.close();pencil.move({371,177});pencil.line({387,177});
        fields.add(shape(fields.id("pencil"),pencil,{},ink,1.1));
        fields.highlight({257,157,132,101},ProfileHighlightShape::rounded);
    }
    // Toolbar: card theme and text visibility.
    constexpr R backgroundButton{243,294,109,24},eyeRect{364,294,25,24};
    toolbar.highlight(backgroundButton,ProfileHighlightShape::cutCorner);
    {Path plate;plate.ellipse({eyeRect.x+eyeRect.width/2-12,eyeRect.y+eyeRect.height/2-12,24,24});toolbar.add(shape(toolbar.id("eyePlate"),plate,gray(dark||backdrop?0:1,.26)));}
    toolbar.highlight(eyeRect,ProfileHighlightShape::ellipse);
    toolbar.add(text(toolbar.id("theme"),{246,300,83,17},L("Card theme","更换名片主题",l),10,Weight::medium,ink,scale));
    {Path t;t.rect({333,299,13,14});t.move({335,309});t.line({339,302});t.line({343,307});t.move({342,314});t.line({349,305});toolbar.add(shape(toolbar.id("themeIcon"),t,{},ink,1.2));}
    {Path s;s.rect({357,298,1.3,17});toolbar.add(shape(toolbar.id("separator"),s,alpha(ink,.74)));}
    {Path e;e.move({366,307});e.quad({377,294},{387,307});e.quad({376,319},{366,307});e.ellipse({374,303,6,7});
     if(!state.textHidden()){e.move({368,317});e.line({385,296});}toolbar.add(shape(toolbar.id("eye"),e,{},ink,1.4));}
    if(const auto popoverKind=state.popover()){
        const auto rect=*state.popoverBounds();const C face=gray(dark?.96:.10);
        {auto n=node(popover.id("backing"),{rect.x-3,rect.y+4,rect.width,rect.height});n["backgroundColor"]=color(gray(0,.30));popover.add(std::move(n));}
        {auto n=node(popover.id("face"),rect);n["backgroundColor"]=color(gray(dark?.08:.92,.98));n["borderWidth"]=.7;n["borderColor"]=color(alpha(accent,.7));popover.add(std::move(n));}
        const bool portrait=*popoverKind==ProfilePopover::portrait;
        if(*popoverKind==ProfilePopover::background||portrait){
            popover.add(text(popover.id("header"),portrait?R{143,121,204,20}:R{138,rect.y+8,182,20},portrait?L("PORTRAIT","调整头像",l):L("CARD THEME","名片主题",l),12,Weight::bold,face,scale));
            for(const auto&slider:state.sliders()){
                const auto row=profileFieldRect(slider.field);const R track{slider.rect.x+4,slider.rect.y,slider.rect.width-8,slider.rect.height};
                popover.add(text(popover.id("sliderLabel"),{slider.rect.x,row.y,slider.rect.width-62,13},slider.label,10,Weight::regular,face,scale));
                popover.add(text(popover.id("sliderValue"),{slider.rect.x+slider.rect.width-61,row.y,61,13},slider.valueDescription,10,Weight::semibold,face,scale,"right"),"slider:"+std::string(profileFieldID(slider.field)));
                const double y=track.y+track.height/2,fraction=(slider.value-slider.minimum)/(slider.maximum-slider.minimum),x=track.x+track.width*fraction;
                {Path t;t.rect({track.x,y-1,track.width,2});popover.add(shape(popover.id("track"),t,alpha(face,.19)));}
                {Path t;t.rect({track.x,y-1,track.width*fraction,2});popover.add(shape(popover.id("fill"),t,alpha(accent,.85)),"slider:"+std::string(profileFieldID(slider.field)));}
                if(slider.minimum<0){Path t;t.rect({track.x+track.width/2-.5,y-3,1,6});popover.add(shape(popover.id("center"),t,alpha(face,.40)));}
                {Path h;h.rounded({x-3.5,y-4,7,8},1);popover.add(shape(popover.id("handle"),h,accent,alpha(face,.65),.6),"slider:"+std::string(profileFieldID(slider.field)));}
                popover.highlight(slider.rect,ProfileHighlightShape::cutCorner);
            }
        }
        if(*popoverKind==ProfilePopover::themeColor)popover.add(text(popover.id("header"),{169,168,182,20},L("CARD COLOR","名片颜色",l),12,Weight::bold,face,scale));
        const bool identity=*popoverKind==ProfilePopover::identity;
        for(const auto&action:state.actions()){
            if(identity&&action.id=="profile:popoverClose")continue;
            if(action.id.starts_with("profile:theme:")||action.id=="profile:themeMenu"){
                auto swatchProfile=p;if(action.id.starts_with("profile:theme:"))swatchProfile.themeColorHex=action.id.substr(14);
                const auto swatch=profileAccent(swatchProfile,appearance.hudAccent);
                Path f;f.rect({action.rect.x+4,action.rect.y+4,action.rect.width-8,action.rect.height-8});popover.add(shape(popover.id("swatch"),f,C{swatch[0],swatch[1],swatch[2],1}));
                popover.highlight(action.rect,ProfileHighlightShape::cutCorner,true);
                if(action.id=="profile:theme:"+p.themeColorHex.value_or("")){Path s;s.rect(action.rect);popover.add(shape(popover.id("selected"),s,{},face,1.2));}
                continue;
            }
            std::string label;
            if(action.id=="profile:popoverClose")label="×";
            else if((action.id=="profile:menu"&&portrait)||(action.id=="profile:backgroundMenu"&&*popoverKind==ProfilePopover::themeColor))label="‹";
            else if(action.id=="profile:themeCustom")label="◉";
            else label=action.label;
            {Path f;f.rect(action.rect);popover.add(shape(popover.id("button"),f,alpha(face,.07)));}
            popover.highlight(action.rect,ProfileHighlightShape::cutCorner,true);
            popover.add(text(popover.id("buttonLabel"),{action.rect.x+6,action.rect.y+5,action.rect.width-12,action.rect.height-10},label,10,Weight::semibold,face,scale,identity?"left":"center"));
        }
    }
    if(const auto&error=state.error())
        toolbar.add(text(toolbar.id("error"),{18,322,372,12},*error,9,Weight::regular,dark?C{1,69./255,58./255,1}:C{1,59./255,48./255,1},scale,"left",true));
    result.fields=fields.finish("profile.fields");result.toolbar=toolbar.finish("profile.toolbar");result.popover=popover.finish("profile.popover");
    // profile.background: frame and crop; fades and shade are the original gradients.
    auto&b=result.backdrop;b.frame=profileBackgroundRect(p);b.photo=backdrop;
    if(backdrop&&contents.backgroundPixels.x>0&&contents.backgroundPixels.y>0)b.contentsRect=profilePortraitCrop(contents.backgroundPixels,{b.frame.width,b.frame.height},p.backgroundZoom,{0,0});
    b.horizontal=gradient({gray(0,0),gray(0,1),gray(0,1),gray(0,0)},{0,.08,.92,1},{0,.5},{1,.5});
    b.vertical=gradient({gray(0,0),gray(0,1),gray(0,1),gray(0,0)},{0,.08,.90,1},{.5,0},{.5,1});
    b.shade=backdrop?gradient({gray(0,.70),gray(0,.38),gray(0,.03)},{0,.60,1},{0,.5},{1,.5}):gradient({gray(0,0),gray(0,0),gray(0,0)},{0,.60,1},{0,.5},{1,.5});
    b.shadeOpacity=state.textHidden()?0:1;
    return result;
}
ProfileUpdateAnimation profileUpdateAnimation(const ProfileState&state,const PersonalProfile&before,const PersonalProfile&after){
    ProfileUpdateAnimation result;
    const auto add=[&](std::string role){if(std::find(result.roles.begin(),result.roles.end(),role)==result.roles.end())result.roles.push_back(std::move(role));};
    for(std::size_t n=0;n<21;++n){
        const auto f=static_cast<ProfileField>(n);if(profileGeometryField(f)||f==ProfileField::playerID)continue;
        if(state.value(f,before)==state.value(f,after))continue;
        if(f==ProfileField::name||f==ProfileField::tag)add("name");
        else if(profileDateField(f)){if(f==(after.showsBirthday?ProfileField::birthday:ProfileField::awakeningDate))add("value:"+std::string(profileFieldID(f)));}
        else add("value:"+std::string(profileFieldID(f)));
    }
    if(before.showsBirthday!=after.showsBirthday){add("dateCaption");add("value:"+std::string(after.showsBirthday?"birthday":"awakeningDate"));}
    if(before.avatarZoom!=after.avatarZoom||before.avatarOffsetX!=after.avatarOffsetX||before.avatarOffsetY!=after.avatarOffsetY)add("portrait");
    if(before.avatarFilename!=after.avatarFilename||before.backgroundFilename!=after.backgroundFilename){result.backdrop=true;result.fields=true;}
    if(before.themeColorHex!=after.themeColorHex){result.fields=true;result.toolbar=true;}
    return result;
}
namespace {
C sampleGradient(const ProfileGradient&g,double t){
    need(!g.colors.empty()&&g.colors.size()==g.locations.size(),"Gradient stops");
    if(t<=g.locations.front())return g.colors.front();
    for(std::size_t k=1;k<g.colors.size();++k)if(t<=g.locations[k]){
        const double span=g.locations[k]-g.locations[k-1],u=span>0?(t-g.locations[k-1])/span:1;C c{};
        for(std::size_t i=0;i<4;++i)c[i]=g.colors[k-1][i]+(g.colors[k][i]-g.colors[k-1][i])*u;return c;}
    return g.colors.back();
}
double project(const ProfileGradient&g,double x,double y){
    const double dx=g.end.x-g.start.x,dy=g.end.y-g.start.y,length=dx*dx+dy*dy;
    return length>0?std::clamp(((x-g.start.x)*dx+(y-g.start.y)*dy)/length,0.,1.):0;
}
std::uint8_t byte(double v){return static_cast<std::uint8_t>(std::lround(std::clamp(v,0.,1.)*255));}
}
ProfileBitmap profileGradientBitmap(const ProfileGradient&g,unsigned width,unsigned height){
    need(width&&height&&width<=4096&&height<=4096,"Bounded gradient bitmap");
    ProfileBitmap b{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)};
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const auto c=sampleGradient(g,project(g,(x+.5)/width,(y+.5)/height));auto*p=&b.rgba[(std::size_t(y)*width+x)*4];
        for(unsigned i=0;i<4;++i)p[i]=byte(c[i]);
    }
    return b;
}
ProfileBitmap profileFadeBitmap(const ProfileGradient&first,const ProfileGradient*second,unsigned width,unsigned height){
    need(width&&height&&width<=4096&&height<=4096,"Bounded fade bitmap");
    ProfileBitmap b{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4,255)};
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const double u=(x+.5)/width,v=(y+.5)/height;double a=sampleGradient(first,project(first,u,v))[3];
        if(second)a*=sampleGradient(*second,project(*second,u,v))[3];
        b.rgba[(std::size_t(y)*width+x)*4+3]=byte(a);
    }
    return b;
}
bool profileHighlightContains(const ProfileHighlight&h,P p)noexcept{
    const auto&r=h.rect;
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<r.x||p.y<r.y||p.x>r.x+r.width||p.y>r.y+r.height)return false;
    const double x=p.x-r.x,y=p.y-r.y;
    switch(h.shape){
    case ProfileHighlightShape::ellipse:{const double dx=(x-r.width/2)/(r.width/2),dy=(y-r.height/2)/(r.height/2);return dx*dx+dy*dy<=1;}
    case ProfileHighlightShape::cutCorner:{const double c=std::min(4.,std::min(r.width,r.height)/3);return x+y>=c&&(r.width-x)+(r.height-y)>=c;}
    case ProfileHighlightShape::rounded:{
        constexpr double c=3;const double cx=x<c?c:x>r.width-c?r.width-c:x,cy=y<c?c:y>r.height-c?r.height-c:y;
        return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=c*c;
    }}
    return false;
}
std::optional<std::size_t>profileHighlightAt(const ProfileArtwork&art,P p)noexcept{
    std::optional<std::size_t>hit;
    for(std::size_t n=0;n<art.highlights.size();++n){
        const auto&h=art.highlights[n];
        if(!h.enabled||(art.fieldsOpacity<=.01&&h.tint.starts_with("profile.fields."))||!profileHighlightContains(h,p))continue;
        if(!hit||h.rect.width*h.rect.height<=art.highlights[*hit].rect.width*art.highlights[*hit].rect.height)hit=n;
    }
    return hit;
}
}
