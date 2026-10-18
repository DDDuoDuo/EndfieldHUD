#include "modules/projection_presentation.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
namespace m=endfield::modules;namespace c=endfield::core;using Json=ehud::data::Json;unsigned checks{};
void check(bool ok,const char*message){++checks;if(!ok)throw std::runtime_error(message);}
void near(double a,double b,const char*message){check(std::abs(a-b)<1e-9,message);}
template<class F>void rejects(F action){try{action();}catch(const std::invalid_argument&){++checks;return;}throw std::runtime_error("Invalid descriptor input was accepted");}
const Json*find(const Json&node,std::string_view id){if(node["id"].isString()&&node["id"].string()==id)return &node;for(const auto&child:node["children"].array())if(auto result=find(child,id))return result;return nullptr;}
const Json&get(const Json&node,std::string_view id){const auto result=find(node,id);check(result!=nullptr,"Stable source layer ID exists");return *result;}
c::Rect rect(const Json&j){const auto&a=j.array();check(a.size()==4,"Rectangle has four coordinates");return {a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
void tests(){
    m::ProjectionControls plan;m::ProjectionControlsInput input;rejects([&]{plan.setFeedback({},false,false);});check(plan.update(input),"First content event builds toolbar");
    check(plan.bounds()==c::Rect{0,0,336,42}&&plan.actions().size()==8&&plan.inkLabels().size()==7&&plan.sliders().empty(),"Source toolbar contains eight actions and seven actual-ink labels");
    const std::array<std::string_view,8>ids{"color","brush","eraser","clear","background","appearance","media","close"};
    const std::array<c::Rect,8>geometry{{{6,6,30,30},{40,6,34,30},{78,6,30,30},{112,6,54,30},{170,6,30,30},{204,6,30,30},{238,6,30,30},{272,6,58,30}}};
    for(unsigned n=0;n<8;++n){const auto&a=plan.actions()[n];check(a.id==ids[n]&&a.rect==geometry[n],"Toolbar actions preserve source ordering and geometry");check(plan.actionAt({a.rect.x+1,a.rect.y+1})==a.id,"Toolbar hit uses the actual plate geometry");}
    check(!plan.actionAt({39,8})&&!plan.actionAt({330,8})&&!plan.actionAt({NAN,8}),"Toolbar gaps, exclusive edges and invalid points have no action");
    check(plan.actions()[4].selected&&!plan.actions()[2].selected&&plan.actions()[7].label=="Return","Source background selection and Return accessibility survive");
    near(get(plan.artwork(),"projection.toolbar/back")["cornerRadius"].number(),10,"Detached backing has toolbar radius10");near(get(plan.artwork(),"color/plate")["cornerRadius"].number(),5,"Toolbar item radius5");
    const auto&face=get(plan.artwork(),"projection.toolbar/face");check(face["children"].array().size()==23,"Toolbar removes base labels/highlights before inserting its own");
    for(unsigned n=0;n<8;++n)check(face["children"].array()[n]["id"].string()==std::string(ids[n])+"/plate","All toolbar plates precede ink rasters and rounded feedback");
    for(const auto&label:plan.inkLabels()){const auto&node=get(plan.artwork(),label.layerID);check(node["projectionInkCentered"].boolean()&&rect(node["frame"])==label.rect&&node["text"]["string"].string()==label.text,"Native ink dependency matches exact text node and full item frame");near(label.fontSize,10,"Source toolbar font10");near(label.contentsScale,2,"Source ink raster scale2");}
    const auto staticTree=plan.artwork();const auto revision=plan.contentRevision();const auto*actions=plan.actions().data();const auto*ink=plan.inkLabels().data();
    for(unsigned n=0;n<1000;++n){check(!plan.update(input),"Repeated input retains artwork");plan.setFeedback(n%2?std::optional<std::string_view>{"brush"}:std::nullopt,false,false);}
    check(plan.contentRevision()==revision&&plan.actions().data()==actions&&plan.inkLabels().data()==ink&&plan.artwork()==staticTree,"Pointer feedback preserves all retained glyph/action/artwork storage");
    plan.setFeedback("brush",true,false);near(plan.feedback()[1].tintOpacity,1,"Pressed tint target");near(plan.feedback()[1].rimOpacity,1,"Pressed rim target");near(plan.feedback()[1].duration,.06,"Source press duration");
    check(!plan.setFeedback("brush",true,false),"Identical feedback does not restart transition");plan.setFeedback({},false,false);near(plan.feedback()[1].duration,.14,"Source release duration");near(plan.feedback()[1].rimOpacity,0,"Toolbar unframed rim rests hidden");
    plan.setFeedback("brush",false,true);near(plan.feedback()[1].duration,0,"Reduced motion removes transition duration");rejects([&]{plan.setFeedback("invented",false,false);});
    auto invalid=input;invalid.brushWidth=INFINITY;rejects([&]{plan.update(invalid);});check(plan.contentRevision()==revision&&plan.artwork()==staticTree,"Rejected content update leaves coherent prior artwork");
    input.brushWidth=5.9;input.erasing=true;input.backgroundEnabled=false;plan.update(input);check(plan.inkLabels()[0].text=="5"&&plan.actions()[2].selected&&!plan.actions()[4].selected,"Toolbar truncates brush width and reflects toggles");
    input.kind=m::ProjectionControlsKind::brush;plan.update(input);check(plan.bounds()==c::Rect{0,0,260,102}&&plan.actions().size()==3&&plan.sliders().size()==1&&plan.inkLabels().empty(),"Brush uses the one-row source menu");
    const auto&slider=plan.sliders()[0];check(slider.id=="brush"&&slider.rail==c::Rect{42,63,170,20}&&slider.hitRect==c::Rect{37,57,180,32},"Slider geometry includes original expanded drag target");
    check(slider.valueAt(-100)==1&&slider.valueAt(500)==80&&!slider.valueAt(NAN),"Slider drag clamps endpoints and rejects invalid input");near(*slider.valueAt(127),40.5,"Slider maps rail midpoint in source value units");
    check(plan.sliderAt({37,57})==0&&!plan.sliderAt({217,89})&&plan.actionAt({9,57})=="minus:0"&&plan.actionAt({225,57})=="plus:0","Step and expanded slider targets stay separate");
    check(get(plan.artwork(),"projection.brush/brush/label")["text"]["string"].string()=="Brush thickness  6","Adjustment label rounds where toolbar truncates");
    near(get(plan.artwork(),"projection.brush/face")["cornerRadius"].number(),0,"Only toolbar plates gain rounded corners");for(const auto&f:plan.feedback())near(f.rimOpacity,.28,"Adjustment menu uses framed resting rims");
    input.kind=m::ProjectionControlsKind::appearance;input.darkness=.235;input.blur=1;plan.update(input);check(plan.bounds()==c::Rect{0,0,260,164}&&plan.actions().size()==5&&plan.sliders().size()==2,"Appearance has exactly two source adjustment rows");
    near(plan.sliders()[0].value,23.5,"Darkness expressed as source percent");near(plan.sliders()[0].fill.width,39.95,"Fractional slider fill remains unrounded");check(plan.sliders()[1].rail==c::Rect{42,125,170,20}&&plan.sliders()[1].thumb==c::Rect{209.5,132,5,8},"Second row positions and full-range thumb match source");
    input.kind=m::ProjectionControlsKind::clearConfirmation;plan.update(input);check(plan.bounds()==c::Rect{0,0,260,92}&&plan.actions().size()==2&&plan.actions()[0].id=="close"&&plan.actions()[0].label=="Cancel"&&plan.actions()[1].selected,"Clear confirmation preserves Cancel/selected Clear all");
    check(get(plan.artwork(),"projection.clear/question")["text"]["string"].string()=="Clear drawings and media?","Source confirmation includes both drawings and media");
    for(const auto language:{c::Language::english,c::Language::simplifiedChinese,c::Language::traditionalChinese,c::Language::japanese,c::Language::korean}){input.language=language;plan.update(input);check(plan.actions()[0].label==c::localized("Cancel","取消",language)&&get(plan.artwork(),"projection.clear/question")["text"]["string"].string()==c::localized("Clear drawings and media?","清空绘画和媒体？",language),"Projection resolves actual shared translations for all five languages");}
    invalid=input;invalid.darkness=-.01;rejects([&]{plan.update(invalid);});invalid=input;invalid.color[0]=NAN;rejects([&]{plan.update(invalid);});invalid=input;invalid.kind=static_cast<m::ProjectionControlsKind>(99);rejects([&]{plan.update(invalid);});
}
void sourceReference(const char*path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);check(bool(file),"Original source contract opens");const auto length=file.tellg();check(length>0&&length<65536,"Original source contract bounded");std::string bytes(std::size_t(length),'\0');file.seekg(0);file.read(bytes.data(),length);check(bool(file),"Original source contract read complete");const auto source=Json::parse(bytes);
    check(source["kind"].string()=="original-swift-source-contract"&&!source["nativeGlyphComparison"].boolean(),"Reference accurately scopes source-only comparison");
    m::ProjectionControls plan;m::ProjectionControlsInput input;
    for(unsigned language=0;language<2;++language){input.language=language?c::Language::simplifiedChinese:c::Language::english;
        for(unsigned kind=0;kind<2;++kind){input.kind=kind?m::ProjectionControlsKind::clearConfirmation:m::ProjectionControlsKind::toolbar;plan.update(input);const auto&expected=source[kind?"clear":"toolbar"];check(plan.bounds()==rect(expected["bounds"]),"Actual Swift menu size matches");check(plan.actions().size()==expected["items"].array().size(),"Actual Swift item count matches");
            for(std::size_t n=0;n<plan.actions().size();++n){const auto&a=plan.actions()[n];const auto&item=expected["items"].array()[n];check(a.id==item["id"].string()&&a.rect==rect(item["rect"])&&a.label==item["label"].array()[language].string(),"Actual Swift action IDs, labels and frames match");auto title=item["title"].array()[language].string();if(title=="@brush.truncated")title="5";if(kind||a.id!="color"){const auto id=kind?a.id+"/label":"projection.toolbar.label."+a.id;check(get(plan.artwork(),id)["text"]["string"].string()==title,"Actual Swift captions match");}}
        }
    }
    input.kind=m::ProjectionControlsKind::toolbar;plan.update(input);const auto&palette=source["palette"];near(get(plan.artwork(),"projection.toolbar/face")["borderWidth"].number(),palette["borderWidth"].number(),"Actual Swift menu border width");
    near(get(plan.artwork(),"projection.toolbar/face")["backgroundColor"]["sRGB"].array()[0].number(),palette["face"].array()[0].number(),"Actual Swift dark-only face intensity");near(get(plan.artwork(),"projection.toolbar/back")["backgroundColor"]["sRGB"].array()[3].number(),palette["backAlpha"].number(),"Actual Swift detached back alpha");
    near(get(plan.artwork(),"projection.toolbar/face")["cornerRadius"].number(),source["toolbar"]["surfaceRadius"].number(),"Actual Swift toolbar radius");near(get(plan.artwork(),"brush/plate")["cornerRadius"].number(),source["toolbar"]["itemRadius"].number(),"Actual Swift item radius");
    for(const auto kind:{m::ProjectionControlsKind::brush,m::ProjectionControlsKind::appearance}){input.kind=kind;plan.update(input);const auto&value=source["adjustment"];const auto&rail=value["rail"].array();near(plan.bounds().height,value["height"].array()[0].number()+plan.sliders().size()*value["height"].array()[1].number(),"Actual Swift adjustment height formula");check(plan.actions()[0].rect==rect(value["close"]),"Actual Swift adjustment close target");for(std::size_t n=0;n<plan.sliders().size();++n){const auto&s=plan.sliders()[n];check(s.rail==c::Rect{rail[0].number(),rail[1].number()+n*rail[2].number(),rail[3].number(),rail[4].number()},"Actual Swift per-row slider rail");near(s.labelRect.y,value["labelY"].number()+n*rail[2].number(),"Actual Swift per-row label origin");near(plan.actions()[1+n*2].rect.y,value["stepY"].number()+n*rail[2].number(),"Actual Swift step control row origin");}}
}
}
int main(int argc,char**argv){try{check(argc<=2,"Pass optional source contract JSON only");tests();if(argc==2)sourceReference(argv[1]);std::cout<<"PASS "<<checks<<" Projection controls checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
