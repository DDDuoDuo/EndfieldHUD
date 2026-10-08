#include "core/source_native_labels.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {bool countAllocations=false;std::size_t allocations=0;}
void* operator new(std::size_t n){if(countAllocations)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

using namespace endfield::core;
using namespace endfield::core::source;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void close(double a,double b,double tolerance,const char* message){++checks;if(std::abs(a-b)>tolerance)throw std::runtime_error(std::string(message)+": "+std::to_string(a)+" vs "+std::to_string(b));}
template<class F>void rejects(F action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,message);}
Matrix4 matrix(const Json& j){Matrix4 m;check(j.array().size()==4,"Oracle matrix has four columns");for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)m.values[c*4+r]=j.array()[c].array()[r].number();return m;}
SourceRect rectangle(const Json& j){return {{j["origin"].array()[0].number(),j["origin"].array()[1].number()},{j["size"].array()[0].number(),j["size"].array()[1].number()}};}
Json read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);check(bool(f),"Explicit owned oracle file opens");const std::string bytes((std::istreambuf_iterator<char>(f)),{});return Json::parse(bytes,64*1024*1024);}
SceneDefinition syntheticScene(){
    Node root;root.id="root";root.name="root";root.path="root";root.children={"button"};
    Node button;button.id="button";button.name="Button";button.path="root/Button";button.parent="root";button.children={"caption","icon"};
    Node caption;caption.id="caption";caption.name="Caption";caption.path="root/Button/Caption";caption.parent="button";caption.rect=RectTransform{{0,0},{0,0},{0,0},{2,1},{.5,.5}};
    Node icon;icon.id="icon";icon.name="Icon";icon.path="root/Button/Icon";icon.parent="button";icon.rect=RectTransform{{0,0},{0,0},{0,0},{1,1},{.5,.5}};
    return SceneDefinition("root",{root,button,caption,icon});
}
void synthetic(){
    auto scene=syntheticScene();auto nodes=SourceLayout(scene).resolve();std::vector<double> alpha(nodes.size(),1);
    NativeLabelPlan plan(scene,{{"text","caption","button",NativeLabelKind::caption,false,false},
        {"icon","icon","button",NativeLabelKind::icon,false,false},{"profile","caption","button",NativeLabelKind::caption,true,false}});
    check(plan.buttonIDs().size()==1,"Captions and icons share one compiled button clip entry");
    std::vector<NativeClipPlane> masks;NativeLabelButtonState button{true,true,false,masks};CameraFrame camera;Rect viewport{0,0,200,100};
    check(plan.update(nodes,alpha,std::span(&button,1),camera,viewport),"First visible frame computes native placements");
    auto p=plan.placements();check(p[0].visible&&p[1].visible&&p[2].visible,"Visible bound source labels and icons are exposed");
    close(p[0].world.values[0],100,1e-12,"Caption projects original local width");close(p[0].world.values[5],50,1e-12,"Caption converts source Y exactly once");
    close(p[0].world.values[12],0,1e-12,"Centered caption starts at viewport left");close(p[0].world.values[13],25,1e-12,"Caption top comes from source top edge");
    check(p[0].contentBounds==Rect{0,0,2,1}&&p[1].contentBounds==Rect{0,0,32,32},"Captions retain point bounds while icons retain their32point raster");
    check(plan.stats().clipComputations==1,"Caption/icon shared button clip is computed once");
    auto before=plan.stats();for(unsigned i=0;i<120;++i)check(!plan.update(nodes,alpha,std::span(&button,1),camera,viewport),"Identical native frame is retained");
    check(plan.stats().projectionComputations==before.projectionComputations&&plan.stats().clipComputations==before.clipComputations,"Idle updates do no projection or clipping work");
    allocations=0;countAllocations=true;
    for(unsigned i=0;i<120;++i){camera.worldRoot.values[12]=.001*i;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);}
    countAllocations=false;check(allocations==0,"Pointer-driven placement updates allocate no memory");
    check(plan.stats().contentBoundsChanges==before.contentBoundsChanges,"Pointer tilt/translation never changes local raster content bounds");
    camera.worldRoot={};button.expandFileShelfCaption=true;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);
    check(plan.placements()[0].contentBounds==Rect{0,0,124,56},"Japanese/Korean shelf caption expands using the authored minimum dimensions");
    button.expandFileShelfCaption=false;button.hasHit=false;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);
    check(!plan.placements()[0].visible&&!plan.placements()[1].visible&&plan.placements()[2].visible,"Missing hit hides ordinary captions/icons while profile uses viewport clipping");
    button.actionBound=false;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);check(plan.placements()[2].opacity==0,"Unbound profile action hides its caption too");
    button.actionBound=true;button.hasHit=true;nodes[2].activeInHierarchy=false;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);
    check(!plan.placements()[0].visible&&plan.placements()[1].visible,"Caption visibility follows its source node independently");
    nodes[2].activeInHierarchy=true;nodes[3].activeInHierarchy=false;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);
    check(plan.placements()[1].visible,"Desktop replacement icon follows active button, even if original source icon is hidden");
    nodes[3].activeInHierarchy=true;alpha[2]=.375;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);close(plan.placements()[0].opacity,.375,0,"Inherited source alpha reaches the GPU placement");
    alpha[2]=2;rejects([&]{plan.update(nodes,alpha,std::span(&button,1),camera,viewport);},"Invalid source alpha is rejected");close(plan.placements()[0].opacity,.375,0,"Rejected update leaves published placements unchanged");alpha[2]=1;
    camera.worldRoot.values[14]=2;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);check(!plan.placements()[0].visible&&!plan.placements()[2].visible,"Depth-clipped source planes do not emit caption geometry");camera.worldRoot={};
    masks.push_back({{{-.5,-.25},{1,.5}}, {}});button.masks=masks;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);
    check(plan.placements()[0].masks.size()==2&&plan.placements()[0].clippedPolygon.size()==4,"Source mask and viewport remain separate exact GPU planes");
    for(const auto point:plan.placements()[0].clippedPolygon)check(point.x>=50&&point.x<=150&&point.y>=37.5&&point.y<=62.5,"Source clip intersection retains its true projected edges");
    const auto clipBefore=plan.placements()[0].clippedPolygon;masks[0].world.values[12]=4;button.masks=masks;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);
    check(!plan.placements()[0].visible&&plan.placements()[2].visible,"Disjoint mask hides only the masked native content");(void)clipBefore;
    NativeLabelPlan right(scene,{{"right-icon","icon","button",NativeLabelKind::icon,false,true}});button.masks={};right.update(nodes,alpha,std::span(&button,1),camera,viewport);
    close(right.placements()[0].world.values[0],250,1e-12,"Right navigation icon uses80source units over its32point raster");
    const std::array<Point,4> subject{{{0,0},{10,0},{10,10},{0,10}}}, tilted{{{3,-2},{8,2},{5,12},{0,8}}};
    auto clipped=nativeClipPolygon(subject,tilted);check(clipped.size()>4,"Intersection retains additional vertices instead of reducing to a rectangle");
    for(const auto point:clipped)check(point.x>=0&&point.x<=10&&point.y>=0&&point.y<=10,"Convex clip remains within viewport");
}
void oracle(const Json& animation,const Json& frame){
    const auto scene=SceneDefinition::fromJson(animation["scene"]);
    auto bindings=nativeLabelBindingsFromExport(scene,animation["mountedDocument"]["buttons"],frame["nativeLayers"],frame["nativeProfileBindings"]);
    NativeLabelPlan plan(scene,std::move(bindings));check(plan.bindings().size()==53,"All actual desktop captions and icons receive explicit source bindings");
    std::map<std::string,const Json*,std::less<>> nodeRows;for(const auto& row:frame["nodes"].array())nodeRows.emplace(row["id"].string(),&row);
    std::vector<ResolvedNode> nodes;std::vector<double> alpha;
    for(const auto& node:scene.nodes()){const auto& row=*nodeRows.at(node.id);std::optional<SourceRect> rect;if(!row["rect"].isNull())rect=rectangle(row["rect"]);
        nodes.push_back({&node,matrix(row["localMatrix"]),matrix(row["worldMatrix"]),rect,row["active"].boolean()});alpha.push_back(row["inheritedAlpha"].isNull()?1:row["inheritedAlpha"].number());}
    std::vector<std::vector<NativeClipPlane>> masks(plan.buttonIDs().size());std::vector<NativeLabelButtonState> buttons(plan.buttonIDs().size());
    for(std::size_t i=0;i<buttons.size();++i){buttons[i].actionBound=true;for(const auto& hit:frame["hits"].array())if(hit["buttonID"].string()==plan.buttonIDs()[i]){
        buttons[i].hasHit=true;for(const auto& m:hit["masks"].array())masks[i].push_back({rectangle(m["rect"]),matrix(m["worldMatrix"])});break;}buttons[i].masks=masks[i];}
    CameraFrame camera;camera.view=matrix(frame["camera"]["view"]);camera.projection=matrix(frame["camera"]["projection"]);camera.worldRoot=matrix(frame["camera"]["worldRoot"]);
    const Rect viewport{0,0,frame["viewport"].array()[0].number(),frame["viewport"].array()[1].number()};
    plan.update(nodes,alpha,buttons,camera,viewport);
    std::map<std::string,const Json*,std::less<>> containers;for(const auto& container:frame["nativeLayers"]["children"].array())containers.emplace(container["children"].array().front()["id"].string(),&container);
    for(const auto& p:plan.placements()){const auto& container=*containers.at(p.surfaceID);const auto& child=container["children"].array().front();
        const bool visible=!container["hidden"].boolean();check(p.visible==visible,"Visibility matches original Swift native layer");if(!visible)continue;
        const auto expected=matrix(child["transform"]);for(unsigned i=0;i<16;++i)close(p.world.values[i],expected.values[i],2e-8,"Native homography matches original Swift source");
        const auto& bounds=child["bounds"].array();close(p.contentBounds.width,bounds[2].number(),1e-10,"Local caption/icon width matches original Swift");close(p.contentBounds.height,bounds[3].number(),1e-10,"Local caption/icon height matches original Swift");
        close(p.opacity,container["opacity"].number(),1e-7,"Native opacity matches original Swift");
        const auto& path=container["mask"]["shape"]["path"].array();check(p.clippedPolygon.size()+1==path.size(),"Projected mask vertex count matches original Swift");
        for(std::size_t i=0;i<p.clippedPolygon.size();++i){const auto& xy=path[i]["points"].array().front().array();close(p.clippedPolygon[i].x,xy[0].number(),2e-8,"Exact mask X follows original Swift clip order");close(p.clippedPolygon[i].y,xy[1].number(),2e-8,"Exact mask Y follows original Swift clip order");}
    }
    const auto before=plan.stats();allocations=0;countAllocations=true;for(unsigned i=0;i<120;++i)plan.update(nodes,alpha,buttons,camera,viewport);countAllocations=false;
    check(allocations==0&&plan.stats().projectionComputations==before.projectionComputations,"Actual shell retained idle path allocates and projects nothing");
    allocations=0;countAllocations=true;for(unsigned i=0;i<120;++i){camera.worldRoot.values[12]+=.0001;plan.update(nodes,alpha,buttons,camera,viewport);}countAllocations=false;
    check(allocations==0&&plan.stats().contentBoundsChanges==before.contentBoundsChanges,"Actual shell pointer placement path allocates nothing and preserves raster bounds");
}
}
int main(int argc,char**argv){try{synthetic();if(argc==2){const std::filesystem::path root=argv[1];const auto animation=read(root/"animation.json");
    for(const auto name:{"desktop-shell-1280x800-top","desktop-shell-1280x800-bottom","desktop-shell-1920x1080-top","desktop-shell-1920x1080-bottom"})oracle(animation,read(root/"frame"/(std::string(name)+".json")));}
    else check(argc==1,"Pass at most one explicit owned Mac package root");
    std::cout<<"PASS "<<checks<<" source-native label projection checks\n";return 0;
}catch(const std::exception& e){countAllocations=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
