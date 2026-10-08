#include "native/volume_scene.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
namespace gpu=endfield::native;namespace core=endfield::core;using Json=ehud::data::Json;
std::size_t checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
double number(const Json&j,double fallback=0){return j.isNull()?fallback:j.number();}
bool flag(const Json&j,bool fallback=false){return j.isNull()?fallback:j.boolean();}
Json read(const std::filesystem::path&path){std::ifstream file(path,std::ios::binary);check(bool(file),"Explicit original source fixture exists");file.seekg(0,std::ios::end);const auto length=file.tellg();check(length>=0&&length<=4*1024*1024,"Source fixture is bounded");file.seekg(0);std::string text(static_cast<std::size_t>(length),'\0');file.read(text.data(),length);check(bool(file),"Read whole isolated source fixture");return Json::parse(text);}
void equivalent(const Json&a,const Json&b,const char*message){if(a.isNumber()&&b.isNumber()){check(std::isfinite(a.number())&&std::abs(a.number()-b.number())<1e-6,message);return;}check(a.isArray()==b.isArray()&&a.isObject()==b.isObject(),message);if(a.isArray()){check(a.array().size()==b.array().size(),message);for(std::size_t n=0;n<a.array().size();++n)equivalent(a.array()[n],b.array()[n],message);}else if(a.isObject()){check(a.object().size()==b.object().size(),message);for(const auto&[key,value]:a.object()){check(b.contains(key),message);equivalent(value,b[key],message);}}else check(a==b,message);}
Json rectangle(double x,double y,double w,double h){return Json::Array{x,y,w,h};}
Json rgba(const Json&c){return c.isNull()?Json{}:c["sRGB"];}
// CoreGraphics may start a rounded rectangle halfway along its straight right
// edge and close the remaining half. The prepared path starts at the tangent.
// Normalize only redundant straight-line subdivision; every curved control
// point, corner, orientation and stroke property remains compared below.
Json pathGeometry(const Json&value){try{if(value.isNull())return value;auto path=value.array();if(path.size()<3||path.front()["op"]!=Json{"move"}||path.back()["op"]!=Json{"close"})return path;
    const auto point=[](const Json&command){return core::Point{command["points"].array().back().array()[0].number(),command["points"].array().back().array()[1].number()};};
    const auto same=[](core::Point a,core::Point b){return std::abs(a.x-b.x)<1e-9&&std::abs(a.y-b.y)<1e-9;};
    const auto first=point(path.front()),last=point(path[path.size()-2]);if(path[1]["op"]==Json{"line"}){const auto next=point(path[1]);const auto cross=(first.x-last.x)*(next.y-last.y)-(first.y-last.y)*(next.x-last.x);const auto between=(first.x-last.x)*(first.x-next.x)+(first.y-last.y)*(first.y-next.y);if(std::abs(cross)<1e-9&&between<=1e-9)path.front()["points"]=Json::Array{Json{Json::Array{last.x,last.y}}};}
    for(std::size_t n=1;n+1<path.size();)if(path[n]["op"]==Json{"line"}&&same(point(path[n]),point(path[n-1])))path.erase(path.begin()+static_cast<std::ptrdiff_t>(n));else ++n;
    if(path.size()>2&&path[path.size()-2]["op"]==Json{"line"}&&same(point(path[path.size()-2]),point(path.front())))path.erase(path.end()-2);return path;}catch(...){std::cerr<<"Invalid path: "<<value.encode()<<'\n';throw;}
}
Json canonical(const Json&n,core::Point origin,double opacity,const Json&clips){const auto kind=n["kind"].isNull()?"layer":n["kind"].string();Json result=Json::Object{{"kind",kind},{"origin",Json::Array{origin.x,origin.y}},{"opacity",opacity},{"clips",clips}};
    if(kind=="text"){const auto&t=n["text"];result["bounds"]=n["bounds"];result["text"]=Json::Object{{"string",t["string"]},{"fontSize",t["fontSize"]},{"family",t["font"]["familyName"]},{"face",t["font"]["postScriptName"]},{"foreground",rgba(t["foregroundColor"])},{"alignment",t["alignment"]},{"wrapped",t["wrapped"]},{"truncation",t["truncation"]}};}
    else if(kind=="shape"){const auto&s=n["shape"];result["shape"]=Json::Object{{"path",pathGeometry(s["path"])},{"fill",rgba(s["fillColor"])},{"stroke",rgba(s["strokeColor"])},{"width",s["strokeColor"].isNull()?Json{0}:s["lineWidth"]},{"cap",s["lineCap"]},{"join",s["lineJoin"]},{"rule",s["fillRule"]}};}
    else {result["bounds"]=n["bounds"];result["background"]=rgba(n["backgroundColor"]);}return result;
}
void flatten(const Json&n,core::Point parent,double parentOpacity,Json::Array clips,std::vector<Json>&result){
    const auto&bounds=n["bounds"].array();const auto&position=n["position"].array();const auto&anchor=n["anchorPoint"].array();core::Point origin{parent.x+position[0].number()-bounds[0].number()-anchor[0].number()*bounds[2].number(),parent.y+position[1].number()-bounds[1].number()-anchor[1].number()*bounds[3].number()};
    const auto opacity=parentOpacity*number(n["opacity"],1);const auto kind=n["kind"].string();if(kind=="text"||kind=="shape"||!n["backgroundColor"].isNull())result.push_back(canonical(n,origin,opacity,clips));
    if(!n["mask"].isNull()){const auto&path=n["mask"]["shape"]["path"].array();double left=std::numeric_limits<double>::infinity(),top=left,right=-left,bottom=-left;for(const auto&command:path)for(const auto&p:command["points"].array()){left=std::min(left,p.array()[0].number());right=std::max(right,p.array()[0].number());top=std::min(top,p.array()[1].number());bottom=std::max(bottom,p.array()[1].number());}
        // Root content reveal is the fixed module-bounds clip owned separately
        // by NativeVolumeScene; appRows' inner clip is compared per-leaf here.
        if(!(left==0&&top==0&&right==400&&bottom==334))clips.push_back(rectangle(origin.x+left,origin.y+top,right-left,bottom-top));}
    for(const auto&child:n["children"].array())flatten(child,origin,opacity,clips,result);
}
gpu::VolumeSnapshot initial(){gpu::VolumeSnapshot s;s.outputID="10";s.inputID="100";s.volume=.55;s.balance=0;s.muted=false;s.canSetVolume=s.canSetMute=s.canSetBalance=s.canSetDefaultOutput=s.canSetDefaultInput=s.applicationActivitySupported=true;
    for(unsigned n=0;n<8;++n){s.outputs.push_back({std::to_string(n+10),"Output "+std::to_string(n),n%2==1,n==3});s.inputs.push_back({std::to_string(n+100),"Input "+std::to_string(n)});}for(unsigned n=0;n<12;++n)s.applications.push_back({std::to_string(n+40),"App "+std::to_string(n),1000+n,true,gpu::VolumeAppState::direct,{},{}});return s;
}
void compare(const std::filesystem::path&root,std::string_view name,const gpu::VolumeController&controller,gpu::VolumeStyle style={}){const auto source=read(root/("volume-"+std::string(name)+".json"));const auto plan=gpu::prepareVolumeScene(controller,style);std::vector<Json>sourceLeaves;flatten(source["root"],{},1,{},sourceLeaves);check(sourceLeaves.size()==plan.surfaces.size(),"Actual CALayer paint leaves and prepared source surfaces match");
    for(std::size_t n=0;n<sourceLeaves.size();++n){const auto&s=plan.surfaces[n];Json::Array clips;if(s.clip)clips.push_back(rectangle(s.clip->x,s.clip->y,s.clip->width,s.clip->height));const auto prepared=canonical(plan.layers["children"].array()[n],{s.local.values[12],s.local.values[13]},s.opacity,clips);try{equivalent(sourceLeaves[n],prepared,"Source paint order, path points, text, font, palette, clipping and placement preserved");}catch(const std::exception&){std::cerr<<name<<" leaf "<<n<<"\nSource: "<<sourceLeaves[n].encode()<<"\nPort: "<<prepared.encode()<<'\n';throw;}}
    const auto&actions=source["actions"].array();check(actions.size()==controller.actions().size(),"Source action count preserved");for(std::size_t n=0;n<actions.size();++n){const auto&a=controller.actions()[n];equivalent(actions[n]["id"],a.id,"Source action identity preserved");equivalent(actions[n]["label"],a.label,"Original accessible action label preserved");equivalent(actions[n]["rect"],rectangle(a.rect.x,a.rect.y,a.rect.width,a.rect.height),"Source action geometry preserved");check(flag(actions[n]["enabled"])==a.enabled,"Source action availability preserved");}
    const auto&sliders=source["sliders"].array();check(sliders.size()==controller.sliders().size(),"Source slider count preserved");for(std::size_t n=0;n<sliders.size();++n){const auto&s=controller.sliders()[n];equivalent(sliders[n]["id"],s.id,"Source slider identity preserved");equivalent(sliders[n]["label"],s.label,"Source slider accessible label preserved");equivalent(sliders[n]["rect"],rectangle(s.rect.x,s.rect.y,s.rect.width,s.rect.height),"Source slider geometry preserved");equivalent(sliders[n]["visibleRect"],s.visibleRect?rectangle(s.visibleRect->x,s.visibleRect->y,s.visibleRect->width,s.visibleRect->height):Json{},"Source clipped slider hit rectangle preserved");equivalent(sliders[n]["value"],s.value?Json{*s.value}:Json{},"Source known/unknown scalar preserved");check(sliders[n]["enabled"].boolean()==s.enabled,"Source slider capability preserved");}
    check(number(source["pageIndex"])==double(controller.pageIndex())&&number(source["scrollOffset"])==controller.applicationScroll(),"Original source page/scroll state preserved");
}
void run(const std::filesystem::path&root){const auto index=read(root/"volume-reference.json");check(index["windowCreated"].boolean()==false&&index["realAudioActivated"].boolean()==false,"Reference records detached synthetic setup");gpu::VolumeController controller(initial());controller.setActive(true);compare(root,"main",controller);controller.scroll({20,260},30,1);compare(root,"applications-scroll",controller);controller.perform("audio:headphones",2);compare(root,"headphones",controller);controller.perform("audio:output",3);compare(root,"output-chooser",controller);controller.scroll({20,70},40,4);compare(root,"output-next",controller);controller.dismissChooser(5);controller.perform("audio:input",6);compare(root,"input-chooser",controller);controller.dismissChooser(7);controller.perform("audio:applications",8);auto state=controller.snapshot();state.volume=0;controller.receiveSnapshot(state);compare(root,"zero-volume",controller);compare(root,"light",controller,{false});}
}
int main(int argc,char**argv){try{check(argc==2,"Pass detached actual Mac Volume reference directory");run(std::filesystem::path(argv[1]));std::cout<<"PASS "<<checks<<" original Mac Volume reference checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
