#include "core/source_native_labels.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
    // An eased entrance can round its CanvasGroup alpha one step past 1 at its end (Core Animation clamps layer opacity).
    alpha[2]=std::nextafter(1.,2.);plan.update(nodes,alpha,std::span(&button,1),camera,viewport);close(plan.placements()[0].opacity,1,0,"An inherited alpha rounded past 1 is clamped to 1");
    alpha[2]=-1e-17;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);close(plan.placements()[0].opacity,0,0,"An inherited alpha rounded below 0 is clamped to 0");
    alpha[2]=.375;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);
    alpha[2]=std::numeric_limits<double>::quiet_NaN();rejects([&]{plan.update(nodes,alpha,std::span(&button,1),camera,viewport);},"A NaN source alpha is rejected");close(plan.placements()[0].opacity,.375,0,"Rejected update leaves published placements unchanged");alpha[2]=1;
    camera.worldRoot.values[14]=2;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);check(!plan.placements()[0].visible&&!plan.placements()[2].visible,"Depth-clipped source planes do not emit caption geometry");camera.worldRoot={};
    masks.push_back({{{-.5,-.25},{1,.5}}, {}});button.masks=masks;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);
    check(plan.placements()[0].masks.size()==2&&plan.placements()[0].clippedPolygon.size()==4,"Source mask and viewport remain separate exact GPU planes");
    for(const auto point:plan.placements()[0].clippedPolygon)check(point.x>=50&&point.x<=150&&point.y>=37.5&&point.y<=62.5,"Source clip intersection retains its true projected edges");
    const auto clipBefore=plan.placements()[0].clippedPolygon;masks[0].world.values[12]=4;button.masks=masks;plan.update(nodes,alpha,std::span(&button,1),camera,viewport);
    check(!plan.placements()[0].visible&&plan.placements()[2].visible,"Disjoint mask hides only the masked native content");(void)clipBefore;
    NativeLabelPlan right(scene,{{"right-icon","icon","button",NativeLabelKind::icon,false,true}});button.masks={};right.update(nodes,alpha,std::span(&button,1),camera,viewport);
    close(right.placements()[0].world.values[0],250,1e-12,"Right navigation icon uses80source units over its32point raster");
    // Mac 1.3.0 left icons: an 80-unit box in the caption's plane, centred over
    // the caption, the glyph's visible bottom 5 units above its top (y up).
    NativeLabelPlan left(scene,{{"text","caption","button",NativeLabelKind::caption,false,false},{"left-icon","icon","button",NativeLabelKind::icon,false,false,"caption"}});
    left.update(nodes,alpha,std::span(&button,1),camera,viewport);const auto authored=left.placements()[1].world;
    check(!left.setIconGlyph("text",{4.5,4.5,23,23})&&left.setIconGlyph("left-icon",{4.5,4.5,23,23})&&!left.setIconGlyph("left-icon",{4.5,4.5,23,23}),"Only a left icon takes a glyph, once per change");
    check(left.update(nodes,alpha,std::span(&button,1),camera,viewport)&&left.placements()[1].world!=authored,"A new glyph reprojects the left icon");
    {const auto&caption=left.placements()[0].world;const auto&icon=left.placements()[1].world;const double perUnit=caption.values[0]; // content units are source units
     close(icon.values[0],80*perUnit/32,1e-9,"The left icon box is 80 source units over its 32-point raster");
     close(icon.values[12],caption.values[12]+(1-40)*perUnit,1e-9,"The box is centred over the caption");
     close(icon.values[13],caption.values[13]-(5-4.5*80/32+80)*caption.values[5],1e-9,"The glyph's bottom sits 5 units above the caption's top");}
    check(left.setIconGlyph("left-icon",{2,7,28,18})&&left.update(nodes,alpha,std::span(&button,1),camera,viewport),"A wider glyph moves the box");
    close(left.placements()[1].world.values[13],left.placements()[0].world.values[13]-(5-7*80/32.+80)*left.placements()[0].world.values[5],1e-9,"The visible bottom keeps the gap for any glyph height");
    // A wipe from the left (HUDMotion.wipe): one more plane, the surface's own, clips x > edge.
    {const auto planes=left.placements()[0].masks.size();
     check(left.setRevealEdge("text",.5)&&!left.setRevealEdge("text",.5)&&!left.setRevealEdge("missing",.5),"A reveal edge applies once per change, to a known surface");
     check(left.update(nodes,alpha,std::span(&button,1),camera,viewport)&&left.placements()[0].masks.size()==planes+1,"The reveal adds one plane mask");
     const auto&m=left.placements()[0].masks.back();const auto&w=left.placements()[0].world;
     const auto local=[&](double x,double y){const auto&v=m.worldToLocal.values;const auto px=w.values[12]+w.values[0]*x+w.values[4]*y,py=w.values[13]+w.values[1]*x+w.values[5]*y;
         const double h=v[3]*px+v[7]*py+v[15];return Point{(v[0]*px+v[4]*py+v[12])/h,(v[1]*px+v[5]*py+v[13])/h};};
     const auto inside=[&](Point p){return p.x>=m.bounds.x&&p.x<=m.bounds.x+m.bounds.width&&p.y>=m.bounds.y&&p.y<=m.bounds.y+m.bounds.height;};
     check(inside(local(.25,.5))&&!inside(local(.75,.5)),"Content left of the edge shows, right of it is clipped");
     allocations=0;countAllocations=true;for(unsigned n=0;n<60;++n){left.setRevealEdge("text",.01*n);left.update(nodes,alpha,std::span(&button,1),camera,viewport);}countAllocations=false;
     check(allocations==0,"A running wipe allocates nothing per frame");
     check(left.setRevealEdge("text",std::nullopt)&&left.update(nodes,alpha,std::span(&button,1),camera,viewport)&&left.placements()[0].masks.size()==planes,"The finished wipe removes its mask");}
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
// Display > Modules > Arrange on HUD: corner marks on the draggable tiles,
// and the tile drawn above the others cut out of its neighbours' captions,
// icons and marks (an excluding mask: the Mac's even-odd clip path).
void arrangeCover(){
    const auto node=[](std::string id,std::optional<std::string> parent,std::vector<std::string> children,Vec2 size){Node n;n.id=id;n.name=id;n.path=parent?*parent+"/"+id:id;n.parent=parent;n.children=std::move(children);n.rect=RectTransform{{0,0},{0,0},{0,0},size,{.5,.5}};return n;};
    SceneDefinition scene("root",{node("root",{},{"a","b"},{2,2}),node("a","root",{"acap","aicon"},{1,1}),node("b","root",{"bcap","bicon"},{1,1}),
        node("acap","a",{},{.8,.2}),node("aicon","a",{},{.4,.4}),node("bcap","b",{},{.8,.2}),node("bicon","b",{},{.4,.4})});
    auto nodes=SourceLayout(scene).resolve();std::vector<double> alpha(nodes.size(),1);
    NativeLabelPlan plan(scene,{{"a.text","acap","a",NativeLabelKind::caption,false,false},{"a.icon","aicon","a",NativeLabelKind::icon,false,true},
        {"b.text","bcap","b",NativeLabelKind::caption,false,false},{"b.icon","bicon","b",NativeLabelKind::icon,false,true}});
    CameraFrame camera;const Rect viewport{0,0,200,200};
    std::array<NativeLabelButtonState,2> buttons{NativeLabelButtonState{true,true,false,{}},NativeLabelButtonState{true,true,false,{}}};
    buttons[0].outline=NativeClipPlane{{{-1,-.5},{1,1}},{}};buttons[1].outline=NativeClipPlane{{{0,-.5},{1,1}},{}};
    plan.update(nodes,alpha,buttons,camera,viewport);
    check(plan.marks().empty()&&std::none_of(plan.placements().begin(),plan.placements().end(),[](const auto&p){return std::any_of(p.masks.begin(),p.masks.end(),[](const auto&m){return m.exclude;});}),"Not arranging: no marks and no cut");
    buttons[0].arrangeMark=buttons[1].arrangeMark=true;check(plan.update(nodes,alpha,buttons,camera,viewport),"Arranging changes the marks");
    check(plan.marks().size()==2&&plan.marks()[0].buttonID=="a"&&!plan.marks()[0].lifted&&std::abs(plan.marks()[0].size[0]-100)<1e-9&&std::abs(plan.marks()[0].size[1]-100)<1e-9,"Each draggable tile is marked, sized in points on the projected tile");
    close(plan.marks()[0].world.values[12],0,1e-9,"The marks' box starts at the tile's projected top left");close(plan.marks()[0].world.values[13],50,1e-9,"Y converted once");
    // b is dragged over a: lifted marks on b, and a's captions, icons and marks lose b's outline.
    buttons[1].liftedMark=buttons[1].raised=true;buttons[1].outline=NativeClipPlane{{{-.6,-.5},{1,1}},{}};check(plan.update(nodes,alpha,buttons,camera,viewport),"Lifting changes the marks");
    const auto excluded=[](std::span<const NativeLabelMask> masks){return std::count_if(masks.begin(),masks.end(),[](const auto&m){return m.exclude;});};
    const auto p=plan.placements();check(excluded(p[0].masks)==1&&excluded(p[1].masks)==1&&excluded(p[2].masks)==0&&excluded(p[3].masks)==0,"The raised tile is cut out of its neighbour's caption and icon only");
    check(plan.marks()[1].lifted&&excluded(plan.marks()[0].masks)==1&&excluded(plan.marks()[1].masks)==0,"The neighbour's marks lose the raised outline too; the raised tile's marks widen");
    {const auto&hole=p[0].masks.back();check(hole.bounds==Rect{0,0,1,1},"The hole is the raised tile's projected outline");
     const auto local=[&](double x,double y){const auto&m=hole.worldToLocal.values;const double w=m[3]*x+m[7]*y+m[15];return Point{(m[0]*x+m[4]*y+m[12])/w,(m[1]*x+m[5]*y+m[13])/w};};
     const auto inside=local(100-.6*100+50,100);check(inside.x>0&&inside.x<1&&inside.y>0&&inside.y<1,"The raised tile's centre lies in the hole");
     const auto outside=local(10,100);check(outside.x<0,"The neighbour's far side stays outside it");}
    buttons[1].outline=NativeClipPlane{{{1.5,-.5},{1,1}},{}};plan.update(nodes,alpha,buttons,camera,viewport);
    check(excluded(plan.placements()[0].masks)==0,"A raised tile outside the neighbour's clip cuts nothing");
    const auto before=plan.stats();check(!plan.update(nodes,alpha,buttons,camera,viewport)&&plan.stats().unchangedUpdates==before.unchangedUpdates+1,"Unchanged marks and cover skip the projection");
    buttons[0].arrangeMark=buttons[1].arrangeMark=buttons[1].liftedMark=buttons[1].raised=false;check(plan.update(nodes,alpha,buttons,camera,viewport)&&plan.marks().empty(),"Arranging off removes the marks");
}
int main(int argc,char**argv){try{synthetic();arrangeCover();if(argc==2){const std::filesystem::path root=argv[1];const auto animation=read(root/"animation.json");
    for(const auto name:{"desktop-shell-1280x800-top","desktop-shell-1280x800-bottom","desktop-shell-1920x1080-top","desktop-shell-1920x1080-bottom"})oracle(animation,read(root/"frame"/(std::string(name)+".json")));}
    else check(argc==1,"Pass at most one explicit owned Mac package root");
    std::cout<<"PASS "<<checks<<" source-native label projection checks\n";return 0;
}catch(const std::exception& e){countAllocations=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
