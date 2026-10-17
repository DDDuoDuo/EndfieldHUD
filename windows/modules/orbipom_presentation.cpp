#include "modules/orbipom_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::modules {
namespace {
using J=ehud::data::Json;using R=core::Rect;using P=core::Point;using C=OrbiPomColor;
void need(bool b,const char*m){if(!b)throw std::invalid_argument(m);}
constexpr std::array<OrbiPomAsset,15>assets{{
    {"OrbiPom/level-1.png","c6e40bf723a0ff63e247616ff608128b9926320a33004d133351c81792afe099",130,145},
    {"OrbiPom/level-2.png","c427f5d98af01154b8502711ab12a2ea850bd54cd75bad174bab835a4c660181",148,180},
    {"OrbiPom/level-3.png","54682200e3325aa8bcddb1cd8fca3e95375c990d4c05a8012aaceb919981006f",217,205},
    {"OrbiPom/level-4.png","5cb064a3cecdda942700b3f7c433840fc8e7c38cfd463999a5821e158e9b50e0",213,201},
    {"OrbiPom/level-5.png","75de0721acd15569123808db245849d46802fc899b1c7af1c24b596ac2531677",213,237},
    {"OrbiPom/level-6.png","b32c6540d485ddf6627233c88be2baa0f5fa40be1f020dc014c0eb0833d7443f",224,253},
    {"OrbiPom/level-7.png","b865cdf42d87da05e8a211d03d93c63bb525dc47c3a2cc5defb680e5335283c2",237,266},
    {"OrbiPom/level-8.png","2192bee10c754ecb9b17391336aa8b28e0cd9dc75076ac362dce572e2b7937b6",257,294},
    {"OrbiPom/level-9.png","06571247101c34956052dee0215d1598b34865dc1996d914950d0f03fbef887f",428,312},
    {"OrbiPom/level-10.png","b7f56f809f2e2df18648fbe69b26b674ae707b66716bd5949e9c1b42960cae17",307,310},
    {"OrbiPom/level-11.png","3f44a0dad2c7318f5d90c28493434fab7f7b58e558c8fe9026fe9c0aa0e39db4",376,393},
    {"OrbiPom/skill-clear.png","eced5a3bbfbe94e2a819ce4e2ce95d1fd3ad4e928dba8bd69cd589389fb24474",115,116},
    {"OrbiPom/skill-wind.png","23321a1657ab729b813af7b469fe554f71cc44628dfbe3610b3296e1bc569196",116,116},
    {"OrbiPom/skill-shake.png","1a20d4ec832c78946615d916454e35ca89a58dd5f838f54613d16b8d24589c5b",116,116},
    {"OrbiPom/skill-swap.png","741a23bb6f0716ef33eacf22ddcb32c5f775589c8cd1012a7571c2a3fbcb6985",116,115}}};
C gray(double w,double a=1){return {w,w,w,a};}C alpha(C c,double a){c[3]=a;return c;}
J color(C c){return J::Object{{"sRGB",J::Array{c[0],c[1],c[2],c[3]}}};}J box(R r){return J::Array{r.x,r.y,r.width,r.height};}
J node(R r,const char*kind="layer"){return J::Object{{"kind",kind},{"bounds",box({0,0,r.width,r.height})},{"position",J::Array{r.x,r.y}},{"anchorPoint",J::Array{0,0}},{"opacity",1},{"children",J::Array{}}};}
J command(const char*op,std::initializer_list<P>points={}){J::Array p;for(auto q:points)p.push_back(J::Array{q.x,q.y});return J::Object{{"op",op},{"points",std::move(p)}};}
J::Array cut(R r,double k){return {command("move",{{r.x+k,r.y}}),command("line",{{r.x+r.width,r.y}}),command("line",{{r.x+r.width,r.y+r.height-k}}),command("line",{{r.x+r.width-k,r.y+r.height}}),command("line",{{r.x,r.y+r.height}}),command("line",{{r.x,r.y+k}}),command("close")};}
J::Array rounded(double w,double h,double r){const double k=r*.5522847498;return {command("move",{{w,h/2}}),command("line",{{w,h-r}}),command("cubic",{{w,h-r+k},{w-r+k,h},{w-r,h}}),command("line",{{r,h}}),command("cubic",{{r-k,h},{0,h-r+k},{0,h-r}}),command("line",{{0,r}}),command("cubic",{{0,r-k},{r-k,0},{r,0}}),command("line",{{w-r,0}}),command("cubic",{{w-r+k,0},{w,r-k},{w,r}}),command("close")};}
J shape(R r,J::Array path,std::optional<C>fill,std::optional<C>stroke={},double width=1){auto v=node(r,"shape");v["shape"]=J::Object{{"path",std::move(path)},{"fillColor",fill?color(*fill):J{}},{"strokeColor",stroke?color(*stroke):J{}},{"lineWidth",width},{"lineCap","butt"},{"lineJoin","miter"},{"fillRule","non-zero"}};return v;}
J plate(R r,C fill,std::optional<C>border={},double width=0,double radius=0){auto v=node(r);v["backgroundColor"]=color(fill);v["cornerRadius"]=radius;v["borderWidth"]=width;if(border)v["borderColor"]=color(*border);return v;}
void add(std::vector<OrbiPomSurface>&out,std::string id,J v,OrbiPomSurfaceRole role=OrbiPomSurfaceRole::artwork,std::optional<OrbiPomAction>a={},float opacity=1){const auto&p=v["position"].array();auto m=core::Matrix4::translation(p[0].number(),p[1].number());v["position"]=J::Array{0,0};v["id"]=id;out.push_back({std::move(id),std::move(v),m,opacity,role,a});}
void validate(const OrbiPomAppearance&a){for(const auto&c:{a.accent,a.systemRed})for(double x:c)need(std::isfinite(x)&&x>=0&&x<=1,"Invalid OrbiPom source color");}
std::string L(const OrbiPomAppearance&a,std::string_view e,std::string_view z){return core::localized(e,z,a.language);}
void highlight(std::vector<OrbiPomSurface>&out,std::string id,R r,C accent,OrbiPomAction action){
    add(out,id+"/tint",shape(r,cut({0,0,r.width,r.height},std::min(4.,std::min(r.width,r.height)/3)),alpha(accent,.30)),OrbiPomSurfaceRole::tint,action,0);
    const R expanded{-2,-2,r.width+4,r.height+4};add(out,id+"/rim",shape(r,cut(expanded,std::min(4.,std::min(expanded.width,expanded.height)/3)),{},accent,.9),OrbiPomSurfaceRole::rim,action,.28f);
}
}
std::span<const OrbiPomAsset>orbiPomAssets()noexcept{return assets;}
J orbiPomImage(unsigned index,R r){need(index<assets.size(),"Missing original OrbiPom artwork");auto v=node(r);v["contents"]=J::Object{{"asset",std::string(assets[index].file)},{"sha256",std::string(assets[index].sha256)}};v["contentsGravity"]="resizeAspect";return v;}
J orbiPomText(std::string text,R r,double size,C ink,bool center,bool wrap,bool mono){auto v=node(r,"text");v["text"]=J::Object{{"string",std::move(text)},{"fontSize",size},{"font",J::Object{{"familyName",mono?".AppleSystemUIFontMonospaced":".AppleSystemUIFont"},{"postScriptName",mono?".AppleSystemUIFontMonospaced-Semibold":".AppleSystemUIFontDemi"},{"pointSize",size},{"symbolicTraits",mono?1026:2}}},{"foregroundColor",color(ink)},{"alignment",center?"center":"natural"},{"wrapped",wrap},{"truncation",wrap?"none":"end"}};return v;}
OrbiPomArtwork prepareOrbiPomArtwork(const OrbiPomSession&session,const OrbiPomState&state,const OrbiPomAppearance&a){validate(a);OrbiPomArtwork result;const auto&s=session.snapshot();const auto ink=gray(a.dark?.94:.12);auto text=[&](std::string id,std::string value,R r,double size){add(result.face,std::move(id),orbiPomText(std::move(value),r,size,ink));};
    text("orbipom.heading","// "+L(a,"Closure's Minigame","可露希尔的小游戏"),{12,7,340,24},15);
    add(result.face,"orbipom.line",plate({12,35,416,1},alpha(ink,.22)));
    add(result.face,"orbipom.board",shape(orbiPomBoard,rounded(253,308,7),gray(a.dark?.05:.9,.86),alpha(a.accent,.7),1));
    text("orbipom.score-label",L(a,"Score","得分"),{87,43,74,16},9);text("orbipom.best-label",L(a,"Best","最高分"),{235,43,105,16},9);
    text("orbipom.score",std::to_string(s.score),{87,59,100,23},16);text("orbipom.best",std::to_string(std::max(session.bestScore(),s.highScore)),{235,59,105,23},16);
    text("orbipom.next-label",L(a,"Next","下一个"),{12,51,65,16},9);text("orbipom.energy",std::to_string(s.energy)+" / 3",{352,58,75,24},13);text("orbipom.sp",L(a,"SP","技力"),{352,42,75,16},9);
    add(result.overlay,"orbipom.dimmer",plate(orbiPomBoard,gray(0,.74),{},0,7),OrbiPomSurfaceRole::dimmer,{},0);
    std::string message;if(session.error())message=*session.error();else if(state.restartConfirmation())message=L(a,"Restart the game?","重新开始游戏？");else if(s.state=="idle")message=L(a,"Merge! OrbiPom!","融合！山团团！");else if(!s.isPlaying())message=L(a,"Game over","游戏结束");else if(session.manuallyPaused())message=L(a,"Paused","已暂停");
    auto status=orbiPomText(message,{94,230,239,24},15,state.boardDimmed()?gray(.96):ink,true,true);status["text"]["truncation"]="end";add(result.overlay,"orbipom.status",std::move(status));
    const auto countdown=s.dangerSeconds&&s.isPlaying()?std::to_string(static_cast<std::int64_t>(std::ceil(*s.dangerSeconds))):"";
    add(result.overlay,"orbipom.danger",orbiPomText(countdown,{354,218,68,55},36,a.systemRed,true));
    const auto actions=state.actions();for(std::size_t n=0;n<actions.count;++n){const auto&hit=actions.items[n];std::string title;int skill=-1;
        switch(hit.action){case OrbiPomAction::start:title=s.state=="idle"?L(a,"Start","开始"):L(a,"Play again","再来一局");break;case OrbiPomAction::pause:title=session.manuallyPaused()?"▶":"Ⅱ";break;case OrbiPomAction::restart:title="↻";break;case OrbiPomAction::confirmRestart:title="✓";break;case OrbiPomAction::cancelRestart:case OrbiPomAction::cancelSkill:title="×";break;case OrbiPomAction::rules:title="?";break;case OrbiPomAction::clear:skill=0;title=L(a,"Eliminate","消除")+" 1";break;case OrbiPomAction::wind:skill=1;title=L(a,"Wind","风场")+" 2";break;case OrbiPomAction::shake:skill=2;title=L(a,"Shake","震动")+" 3";break;case OrbiPomAction::swap:skill=3;title=L(a,"Swap","交换")+" "+std::to_string(s.swapCharge)+"/6";break;}
        const auto id="orbipom.control."+std::to_string(static_cast<unsigned>(hit.action));const auto r=hit.rect;auto group=node(r);group["allowsGroupOpacity"]=true;
        auto p=shape({0,0,r.width,r.height},cut({0,0,r.width,r.height},5),gray(a.dark?.16:.9,.96),alpha(a.accent,.45));
        J::Array children;children.push_back(std::move(p));if(skill>=0)children.push_back(orbiPomImage(11+skill,{9,4,r.width-18,32}));
        children.push_back(orbiPomText(title,skill>=0?R{2,37,r.width-4,13}:R{3,(r.height-16)/2,r.width-6,16},skill>=0?8:11,ink,true));
        // Child group opacity is baked once by the shared rasterizer. Root
        // placement opacity remains1, so disabled overlapping art is not faded twice.
        auto body=node({0,0,r.width,r.height});body["opacity"]=hit.enabled?1:.4;body["allowsGroupOpacity"]=true;body["children"]=std::move(children);group["children"]=J::Array{std::move(body)};
        add(result.overlay,id,std::move(group));if(hit.enabled)highlight(result.overlay,id,r,a.accent,hit.action);
    }return result;
}
std::array<std::string,9>orbiPomRuleParagraphs(core::Language language){const auto L=[&](auto e,auto z){return core::localized(e,z,language);};return {
L("Move to aim; click or press Space to drop.","移动鼠标瞄准，点击或按空格投放。"),
L("Merge matching levels. Two level-11 OrbiPoms disappear when merged.","相同等级相碰即可合成；两个11级山团团合成后消失。"),
L("Points by source level: 1 / 3 / 6 / 10 / 15 / 21 / 28 / 36 / 45 / 55 / 66.","各等级合成得分：1／3／6／10／15／21／28／36／45／55／66。"),
L("12 merges grant 1 SP, up to 3. Spend 6 SP to charge Swap.","每合成12次获得1点技力，最多3点；累计消耗6点技力可使用交换。"),
L("Partial SP charge can decay while idle.","闲置时，未充满的技力进度可能衰减。"),
L("Eliminate (1 SP): select one target. Wind (2 SP) lifts the stack; Shake (3 SP) shakes it.","消除（1技力）：选择一个目标。风场（2技力）托起堆叠；震动（3技力）摇动堆叠。"),
L("Select a skill, then click the board to confirm. Swap needs two different targets.","选择技能后点击游戏区确认；交换需要选择两个不同目标。"),
L("A settled overflow above the red line starts a 5-second countdown.","静止的山团团超过红线时，会开始5秒倒计时。"),
L("P pauses. Escape cancels a selected skill.","按P暂停，按Esc取消已选择的技能。")};}
OrbiPomRules prepareOrbiPomRules(const OrbiPomAppearance&a,std::span<const double,9>measure){validate(a);OrbiPomRules out;std::array<double,9>height{};for(unsigned n=0;n<9;++n){need(std::isfinite(measure[n])&&measure[n]>=0&&measure[n]<=1000,"Invalid original OrbiPom paragraph measurement");height[n]=std::max(18.,std::ceil(measure[n])+3);out.contentHeight+=height[n]+(n?9:0);}
    out.bounds={0,0,322,std::min(348.,48+out.contentHeight)};out.viewport={12,40,298,out.bounds.height-49};out.origin={104,std::max(42.,391-out.bounds.height)};out.maximumScroll=std::max(0.,out.contentHeight-out.viewport.height);const auto ink=gray(a.dark?.96:.10);
    add(out.surfaces,"orbipom.rules.shadow",plate({-3,4,322,out.bounds.height},gray(0,.30)));
    add(out.surfaces,"orbipom.rules.face",plate(out.bounds,gray(a.dark?.08:.92,.98)));
    add(out.surfaces,"orbipom.rules.close",plate({291,8,23,23},alpha(ink,.07)));highlight(out.surfaces,"orbipom.rules.close",{291,8,23,23},a.accent,OrbiPomAction::rules);
    add(out.surfaces,"orbipom.rules.close-label",orbiPomText("×",{297,13,11,13},10,ink,false,false,false));
    add(out.surfaces,"orbipom.rules.heading",orbiPomText("// "+L(a,"Rules","游戏规则"),{12,11,269,20},12,ink,false,false,false));
    const auto paragraphs=orbiPomRuleParagraphs(a.language);double y{};for(unsigned n=0;n<9;++n){out.paragraphSurfaces[n]=out.surfaces.size();add(out.surfaces,"orbipom.rules.paragraph."+std::to_string(n),orbiPomText(paragraphs[n],{12,40+y,294,height[n]},10,ink,false,true,false));y+=height[n]+9;}
    // CALayer paints its border after its children.
    add(out.surfaces,"orbipom.rules.border",plate(out.bounds,gray(0,0),alpha(a.accent,.7),.7));return out;
}
J orbiPomSelectionRing(double size,C accent){need(std::isfinite(size)&&size>0&&size<=440,"Invalid OrbiPom selection size");return plate({0,0,size,size},gray(0,0),accent,1.5,size/2);}
J orbiPomDangerLine(C red){auto v=shape({0,0,253,1},J::Array{command("move",{{0,0}}),command("line",{{253,0}})},{},red,1);v["shape"]["lineDashPattern"]=J::Array{5,4};return v;}
}
