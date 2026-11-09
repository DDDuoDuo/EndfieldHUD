#include "core/source_watch_layout.hpp"
#include "core/shell_packet.hpp"
#include "tests/source_watch_list_fixture.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <iomanip>
#include <sstream>
using namespace endfield::core;
using namespace endfield::core::source;
namespace {
unsigned checks{};
void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
double maximumRelativeError{};
void near(double a,double b,const char*m,double tolerance=1e-11){const auto error=std::abs(a-b)/std::max(1.,std::abs(b));maximumRelativeError=std::max(maximumRelativeError,error);if(!std::isfinite(a)||!std::isfinite(b)||error>tolerance){std::ostringstream message;message<<std::setprecision(17)<<m<<": "<<a<<" != "<<b<<" (error "<<error<<")";throw std::runtime_error(message.str());}++checks;}

template<class F>void rejects(F&&f,const char*m){bool failed{};try{f();}catch(const std::invalid_argument&){failed=true;}check(failed,m);}
Node rectNode(std::string id,std::optional<std::string>parent,Vec2 size,std::vector<std::string>children={}){Node n;n.id=id;n.parent=parent;n.children=std::move(children);n.rect=RectTransform{{},{},{},size,{.5,.5}};return n;}
SceneDefinition syntheticScene(){return SceneDefinition("root",{rectNode("root",{}, {200,160},{"group","text"}),rectNode("group","root",{100,40},{"a","b","ignored"}),rectNode("a","group",{20,10}),rectNode("b","group",{20,10}),rectNode("ignored","group",{900,900}),rectNode("text","root",{40,10})});}
void add(MountedLayoutDocument&d,std::string id,std::string kind,Json data){const auto componentID=id+"-"+kind+std::to_string(d.components[id].size());d.components[id].push_back({componentID,"MonoBehaviour",kind,std::move(data)});}
MountedLayoutDocument syntheticDocument(bool reverse=false,bool fit=false){
    MountedLayoutDocument d;Json group(Json::Object{}),padding(Json::Object{});padding["m_Left"]=2;padding["m_Right"]=3;padding["m_Top"]=5;padding["m_Bottom"]=7;group["m_Padding"]=padding;group["m_Spacing"]=4;
    group["m_ChildControlWidth"]=true;group["m_ChildControlHeight"]=true;group["m_ChildAlignment"]=0;group["m_ReverseArrangement"]=reverse;add(d,"group","HorizontalLayoutGroup",group);
    Json element(Json::Object{});element["m_LayoutPriority"]=1;element["m_MinWidth"]=5;element["m_PreferredWidth"]=30;element["m_FlexibleWidth"]=0;element["m_MinHeight"]=2;element["m_PreferredHeight"]=12;element["m_FlexibleHeight"]=0;add(d,"a","LayoutElement",element);
    element["m_MinWidth"]=10;element["m_PreferredWidth"]=50;element["m_FlexibleWidth"]=1;element["m_PreferredHeight"]=20;add(d,"b","LayoutElement",element);
    element["m_IgnoreLayout"]=true;element["m_Enabled"]=false;add(d,"ignored","LayoutElement",element);
    add(d,"text","UIText",Json::Object{});Json fitter(Json::Object{});fitter["m_HorizontalFit"]=2;fitter["m_VerticalFit"]=2;add(d,"text","ContentSizeFitter",fitter);if(fit)add(d,"group","ContentSizeFitter",fitter);
    add(d,"root","GridLayoutGroup",Json::Object{});return d;
}
void spriteProjection(){
    Json value=Json::parse(R"({"components":{},"buttons":[],"spriteByComponent":{"image":{"id":"source-sprite","name":"sample","bindings":[1,2,3],"decoded_render_mesh":{"vertices":[1,2,3]},"raw_sprite":{"m_Border":{"x":1,"y":2,"z":3,"w":4,"archive":99},"m_Rect":{"x":7,"y":9,"width":32,"height":64},"m_PixelsToUnits":100,"m_RD":{"large":"unused"}},"texture":{"id":"source-texture","asset_paths":["not-a-runtime-file"]}}}})");
    const auto document=MountedLayoutDocument::fromJson(value);const auto&sprite=document.spriteByComponent.at("image");
    check(sprite["id"].string()=="source-sprite"&&sprite["name"].string()=="sample"&&sprite["texture"]["id"].string()=="source-texture","Runtime sprite retains exact bound identities");
    check(sprite["raw_sprite"]["m_Rect"]==value["spriteByComponent"]["image"]["raw_sprite"]["m_Rect"]&&sprite["raw_sprite"]["m_PixelsToUnits"].number()==100,"Runtime sprite preserves rect and pixels-per-unit");
    for(const auto*axis:{"x","y","z","w"})check(sprite["raw_sprite"]["m_Border"][axis]==value["spriteByComponent"]["image"]["raw_sprite"]["m_Border"][axis],"Runtime sprite preserves each border channel");
    check(!sprite.contains("bindings")&&!sprite.contains("decoded_render_mesh")&&!sprite["raw_sprite"].contains("m_RD")&&!sprite["raw_sprite"]["m_Border"].contains("archive")&&!sprite["texture"].contains("asset_paths"),"Runtime sprite excludes all unused archive subtrees");
    auto malformed=value;malformed["spriteByComponent"]["image"]["raw_sprite"]["m_Rect"]["width"]="broken";rejects([&]{MountedLayoutDocument::fromJson(malformed);},"Malformed retained numeric field rejected");
    malformed=value;malformed["spriteByComponent"]["image"]["texture"]=Json::Array{1};rejects([&]{MountedLayoutDocument::fromJson(malformed);},"Malformed retained texture reference rejected");
    malformed=value;malformed["spriteByComponent"]["image"]=42;rejects([&]{MountedLayoutDocument::fromJson(malformed);},"Nonobject sprite record rejected");
    value["spriteByComponent"]["image"]=Json::Object{};const auto missing=MountedLayoutDocument::fromJson(value);check(missing.spriteByComponent.at("image")["raw_sprite"]["m_Rect"].isNull(),"Absent optional sprite metadata preserves source fallback lookup");
    check(sprite["raw_sprite"]["m_Rect"]["width"].number()==32,"Typed document owns projected metadata independently of input JSON");
}
void analytic(){
    auto scene=syntheticScene();auto document=syntheticDocument();WatchLayout layout(scene,document);Pose pose;pose.transforms["a"].localPosition=Vec3{99,88,7};pose.transforms["a"].positionComponents[2]=13;
    auto report=layout.apply(pose);const auto&a=pose.transforms.at("a"),&b=pose.transforms.at("b");
    check(a.sizeDelta==Vec2{30,12}&&b.sizeDelta==Vec2{61,20},"Group distributes remaining width by flexible metric and keeps cross-axis preferred heights");
    check(a.anchoredPosition3D==Vec3{17,-11,0}&&b.anchoredPosition3D==Vec3{66.5,-15,0},"Horizontal control uses padding, spacing and source pivot convention");
    check(!a.localPosition&&a.positionComponents.size()==1&&a.positionComponents.at(2)==13,"Layout rewrites driven XY while preserving independent source Z");
    check(a.anchorMin==Vec2{0,1}&&a.anchorMax==Vec2{0,1},"Driven layout resets source anchors to top-left");check(!pose.transforms.contains("ignored"),"Disabled LayoutElement still participates in ignore-layout filtering");
    check(report.missingTextMetrics.contains("text")&&pose.transforms.at("text").sizeDelta==Vec2{40,10},"Unavailable text metrics retain authored visible fallback and remain diagnosed");
    check(report.unverifiedCustomComponents.contains("GridLayoutGroup"),"Unsupported custom layout is explicit rather than guessed");
    auto reversed=syntheticDocument(true);WatchLayout reverse(scene,reversed);Pose reversePose;reverse.apply(reversePose);near((*reversePose.transforms.at("b").anchoredPosition3D)[0],32.5,"Reverse arrangement preserves metrics but reverses source child order");
    auto fitted=syntheticDocument(false,true);WatchLayout fitter(scene,fitted,[](std::string_view,const auto&){return Vec2{70,22};});Pose fitPose;const auto fitReport=fitter.apply(fitPose);check(fitPose.transforms.at("group").sizeDelta==Vec2{89,32},"ContentSizeFitter uses bottom-up group preferred size before child control");check(fitPose.transforms.at("text").sizeDelta==Vec2{70,22}&&fitReport.missingTextMetrics.empty(),"Explicit intrinsic callback supplies text metrics");
    // One false ILayoutIgnorer includes the child even if another component
    // ignores it; its disabled state is deliberately unrelated to this query.
    Json include(Json::Object{});include["m_IgnoreLayout"]=false;include["m_Enabled"]=false;add(document,"ignored","LayoutElement",include);WatchLayout included(scene,document);Pose includedPose;included.apply(includedPose);check(includedPose.transforms.contains("ignored"),"Any false layout ignorer includes source child");
    rejects([&]{Pose p;layout.apply(p,std::numeric_limits<double>::infinity());},"Nonfinite scroll input rejected");
    std::optional<WatchLayout::ScrollInfo> scroll=WatchLayout::ScrollInfo{"","","",100,4,.5};near(WatchLayout::scrolledPosition(.5,5,scroll),.7,"Native wheel applies source sensitivity divided by hidden length");near(WatchLayout::scrolledPosition(.5,-100,scroll),0,"Wheel target clamps at original bottom");
}
void scrolling(){
    auto scene=SceneDefinition("root",{rectNode("root",{}, {100,80},{"content"}),rectNode("content","root",{100,240})});MountedLayoutDocument d;Json scroll(Json::Object{}),ref(Json::Object{});scroll["m_Vertical"]=true;ref["target_id"]="content";scroll["m_Content"]=ref;ref["target_id"]="root";scroll["m_Viewport"]=ref;scroll["m_ScrollSensitivity"]=3;add(d,"root","UIScrollRect",scroll);
    WatchLayout writer(scene,d);Pose top;auto report=writer.apply(top,1);check(report.scroll&&report.scroll->hiddenLength==160,"Scroll bounds use transformed source rect corners");near((*top.transforms.at("content").anchoredPosition3D)[1],-40,"Top normalized position aligns upper viewport edge");
    Pose bottom;writer.apply(bottom,0);near((*bottom.transforms.at("content").anchoredPosition3D)[1],120,"Bottom normalized position aligns lower viewport edge");
    check(writer.scrollResolutionNodeCount("root")==2,"Scroll chain contains only its required source ancestors");
    check(report.unverifiedCustomComponents.contains("UIScrollRect.elasticInertiaAndSmoothScrollScheduling"),"Writer does not invent a second inertia scheduler");
}
Json loadJSON(const std::filesystem::path&p){std::ifstream in(p,std::ios::binary);check(bool(in),"Swift writer oracle opens");std::string bytes((std::istreambuf_iterator<char>(in)),{});return Json::parse(bytes,64*1024*1024);}
template<std::size_t N>void compareVector(const std::optional<std::array<double,N>>& actual,const Json& expected,double tolerance=1e-11){check(actual.has_value()==!expected.isNull(),"Oracle optional vector presence");if(actual){check(expected.array().size()==N,"Oracle vector width");for(unsigned i=0;i<N;++i)near((*actual)[i],expected.array()[i].number(),"Original Swift pose component",tolerance);}}
// Slant/scroll round-trip positions use Apple's SIMD inverse in the oracle
// and a portable pivoted inverse here. Cancellation in the 10,000-entry case
// measures < 2.8e-11 relative error; allow 1e-10 only for these position fields.
void comparePose(const Pose& pose,const Json& expected,bool invertedPosition=false){
    const auto& transforms=expected["transforms"].object();check(pose.transforms.size()==transforms.size(),"Oracle transform binding count");
    for(const auto& [id,t]:pose.transforms){const auto& e=transforms.at(id);
        compareVector(t.localPosition,e["position"]);compareVector(t.localScale,e["scale"]);compareVector(t.localRotation,e["rotation"]);
        compareVector(t.anchoredPosition3D,e["anchored"],invertedPosition?1e-10:1e-11);compareVector(t.anchorMin,e["anchorMin"]);compareVector(t.anchorMax,e["anchorMax"]);compareVector(t.sizeDelta,e["sizeDelta"]);compareVector(t.pivot,e["pivot"]);
        check(t.active.has_value()==!e["active"].isNull(),"Oracle active optional");if(t.active)check(*t.active==e["active"].boolean(),"Oracle active value");
        check(t.positionComponents.size()==e["components"].object().size(),"Oracle position component count");for(const auto& [axis,value]:t.positionComponents)near(value,e["components"][std::to_string(axis)].number(),"Oracle position component",invertedPosition?1e-10:1e-11);
    }
    check(pose.properties.size()==expected["properties"].object().size(),"Oracle property node count");
    for(const auto& [id,props]:pose.properties){const auto& e=expected["properties"][id].object();check(props.size()==e.size(),"Oracle property count");for(const auto& [name,value]:props)near(value,e.at(name).number(),"Oracle scalar property");}
    auto strings=[](const Json& array){std::set<std::string,std::less<>> s;for(const auto& v:array.array())s.insert(v.string());return s;};
    check(pose.unboundPaths==strings(expected["unbound"]),"Oracle unbound source paths");check(pose.unregisteredBindings==strings(expected["unregistered"]),"Oracle unregistered class bindings");
}
Matrix4 matrix(const Json&j){Matrix4 m;check(j.array().size()==16,"Oracle world matrix width");for(unsigned i=0;i<16;++i)m.values[i]=j.array()[i].number();return m;}
void compareReport(const WatchLayout::Report&r,const Json&e){
    auto strings=[](const Json&j){std::set<std::string,std::less<>>v;for(const auto&s:j.array())v.insert(s.string());return v;};
    check(r.missingTextMetrics==strings(e["missingTextMetrics"]),"Original text-metric diagnostics match");check(r.unverifiedCustomComponents==strings(e["unverified"]),"Original custom-component diagnostics match");
    check(r.scroll.has_value()==!e["scroll"].isNull(),"Original optional scroll report");if(r.scroll){const auto&s=e["scroll"];check(r.scroll->nodeID==s["node"].string()&&r.scroll->contentID==s["content"].string()&&r.scroll->viewportID==s["viewport"].string(),"Original scroll owner/content/viewport binding");near(r.scroll->hiddenLength,s["hidden"].number(),"Original scroll hidden length");near(r.scroll->sensitivity,s["sensitivity"].number(),"Original scroll sensitivity");near(r.scroll->normalizedPosition,s["position"].number(),"Original elastic normalized position");}
}
void compareNavigation(const DesktopNavigationLayout::Sample&s,const Json&e){
    near(s.contentHeight,e["contentHeight"].number(),"Original recycled content height");check(s.assignments.size()==e["assignments"].object().size()&&s.logicalRows.size()==e["logicalRows"].object().size(),"Original physical row pool remains bounded");
    for(const auto&[id,index]:s.assignments)check(static_cast<std::size_t>(e["assignments"][id].integer())==index,"Original logical entry binds exact physical button ID");
    for(const auto&[id,index]:s.logicalRows)check(static_cast<std::size_t>(e["logicalRows"][id].integer())==index,"Original row recycling starts at exact source slot");
}
void actual(const std::filesystem::path&root,const std::optional<std::filesystem::path>&oraclePath){
    packet::Package package(std::filesystem::absolute(root));const auto data=package.loadAnimation();auto scene=SceneDefinition::fromJson(data["scene"]);auto library=Library::fromJson(data["library"]);auto document=MountedLayoutDocument::fromJson(data["mountedDocument"]);WatchAnimation animation(scene,library);
    auto input=[&](double time){auto pose=animation.pose(time,{},{},{1000,640});for(const auto&button:document.buttons)pose.transforms[button.nodeID].active=true;return pose;};
    if(oraclePath){const auto oracle=loadJSON(*oraclePath);for(const auto&row:oracle["rows"].array()){
        WatchLayout::IntrinsicSize metrics;if(row["metrics"].boolean())metrics=[](std::string_view,const std::optional<SourceRect>&rect)->std::optional<Vec2>{return Vec2{std::abs(rect?rect->size[0]:0)*.7+12,22};};
        WatchLayout writer(scene,document,metrics);DesktopNavigationLayout navigation(scene,document,row["entries"].integer());const auto position=row["position"].number();compareNavigation(navigation.sample(position),row["nav"]);
        auto pose=input(row["time"].number());Pose before;const auto report=writer.apply(pose,position,matrix(row["worldRoot"]),&navigation,{},[&](const Pose&p){before=p;});
        comparePose(before,row["beforeSlant"]);comparePose(pose,row["pose"],true);compareReport(report,row["report"]);
        auto forced=input(row["time"].number());const auto forcedReport=writer.apply(forced,position,matrix(row["worldRoot"]),&navigation,{},{},true);comparePose(forced,row["pose"],true);compareReport(forcedReport,row["report"]);
        const auto initialCount=writer.initialRebuiltNodeCount(),slantCount=writer.slantRebuiltNodeCount();auto repeat=input(row["time"].number());writer.apply(repeat,position,matrix(row["worldRoot"]),&navigation);comparePose(repeat,row["pose"],true);check(writer.initialRebuiltNodeCount()==initialCount&&writer.slantRebuiltNodeCount()==slantCount,"Repeated identical source inputs reuse both retained resolution stages");
        writer.applySlant(before,matrix(row["gyroRoot"]));comparePose(before,row["gyroPose"],true);check(writer.initialRebuiltNodeCount()==initialCount&&writer.slantRebuiltNodeCount()==slantCount,"Gyro-only slant does not rerun metrics or retained full layout");
        if(report.scroll)check(writer.scrollResolutionNodeCount(report.scroll->nodeID).value()<scene.nodes().size(),"Actual scroll resolves only its required ancestor chain");
    }}else{
        WatchLayout writer(scene,document);DesktopNavigationLayout navigation(scene,document,101);auto pose=input(.75);const auto report=writer.apply(pose,.5,{},&navigation);check(report.scroll&&navigation.rows().size()==9,"Actual desktop uses original nine-row source pool");check(navigation.sample(.5).assignments.size()<=18,"Long logical list keeps at most 18 source buttons");
    }
    std::cout<<scene.nodes().size()<<" mounted nodes, "<<document.buttons.size()<<" source buttons; Swift oracle "<<(oraclePath?"compared":"not supplied")<<'\n';
}
}
// Arrange on HUD: HUDSourceDesktopNavigationLayout.positions(binding:) and
// the HUDSourceDesktopTiles helpers.
void arrangeHelpers(){
    const auto fixture=endfield::tests::sourceListFixture();
    for(const std::int64_t entries:{11,21,5}){
        DesktopNavigationLayout navigation(fixture.scene,fixture.document,entries);check(navigation.rows().size()==4,"The fixture pools four rows");
        for(std::size_t index=0;index<static_cast<std::size_t>(entries);++index){
            const auto range=navigation.positions(index);check(range&&range->first<=range->second&&range->first>=0&&range->second<=1,"A listed entry has a range inside 0...1");
            // Brute force: every sampled position inside the range binds the entry to a pooled button.
            for(int step=0;step<=2000;++step){const double position=step/2000.;const auto sample=navigation.sample(position);
                const bool bound=std::any_of(sample.assignments.begin(),sample.assignments.end(),[&](const auto&a){return a.second==index;});
                if(position>=range->first&&position<=range->second)check(bound,"Inside its range the entry stays bound to a pooled row");}
            const auto inside=[&](double position){const auto sample=navigation.sample(position);return std::any_of(sample.assignments.begin(),sample.assignments.end(),[&](const auto&a){return a.second==index;});};
            if(range->first>1e-3)check(!inside(range->first-1e-3),"Just past the low end the entry leaves the pool");
            if(range->second<1-1e-3)check(!inside(range->second+1e-3),"Just past the high end the entry leaves the pool");
        }
        check(!navigation.positions(static_cast<std::size_t>(entries)),"Past the list: no range");
    }
    {DesktopNavigationLayout shortList(fixture.scene,fixture.document,3);const auto all=shortList.positions(2);check(all&&all->first==0&&all->second==1,"Nothing hidden: every position binds");}
    {DesktopNavigationLayout list(fixture.scene,fixture.document,21);const auto first=list.positions(0),middle=list.positions(10),last=list.positions(20);
     check(first&&first->second==1&&first->first>0,"The first row binds from the top down to where it leaves the pool");
     check(middle&&middle->first>0&&middle->second<1,"A middle row binds within the list");
     check(last&&last->first==0&&last->second<1,"A row inside the last pool rows binds down to the bottom");}
    // Tiles: the plane, translation, rest and raising rules.
    const auto plane=Matrix4::translation(10,20,5)*Matrix4::scale(2,2,1),world=Matrix4::translation(30,40,5)*Matrix4::scale(2,2,1);const SourceRect rect{{-5,-5},{10,10}};
    const auto center=DesktopTiles::center(world,rect,plane);check(center&&std::abs((*center)[0]-10)<1e-12&&std::abs((*center)[1]-10)<1e-12,"A tile's centre in the viewport's plane");
    const auto offset=DesktopTiles::translation(DesktopTilePlacement::offset({3,-4}),world,rect,plane);check(offset&&*offset==Vec3{6,-8,0},"An offset is scaled into the scene by the plane");
    const auto held=DesktopTiles::translation(DesktopTilePlacement::center({13,6}),world,rect,plane);check(held&&*held==Vec3{6,-8,0},"A held centre is the offset from the resting centre");
    const auto moved=DesktopTiles::translated(ResolvedNode{nullptr,{},world,rect,true},*held);const auto root=Matrix4::scale(1.5,1.5,1);
    const auto rest=DesktopTiles::restWorld(root*moved.worldMatrix,*held,root);for(unsigned n=0;n<16;++n)check(std::abs(rest.values[n]-(root*world).values[n])<1e-12,"The drawn world moved back is the resting world");
    std::vector<int> items{1,2,3,4,5,6};DesktopTiles::raise(items,[](int v){return v==2;},[](int v){return v<=4;});
    check(items==std::vector<int>{1,3,4,2,5,6},"The raised item draws just after the last item of its group");
    DesktopTiles::raise(items,[](int v){return v==9;},[](int v){return v<=4;});check(items==std::vector<int>{1,3,4,2,5,6},"Nothing raised: unchanged");
    // The subtree walk shared by the frame builder.
    SourceLayout layout(fixture.scene);const auto row=*layout.nodeIndex("row1");const auto inside=layout.subtrees(std::array{row});
    check(inside[row]&&inside[*layout.nodeIndex("b10")]&&inside[*layout.nodeIndex("b11label")]&&!inside[*layout.nodeIndex("b00")]&&!inside[*layout.nodeIndex("content")],"A root and everything under it");
    auto nodes=layout.resolve();const auto label=*layout.nodeIndex("b10label");const auto before=nodes[label].worldMatrix;
    check(DesktopTiles::moveNodes(nodes,{{*layout.nodeIndex("b10"),Vec3{1,2,0}}},layout)==2&&nodes[label].worldMatrix==Matrix4::translation(1,2,0)*before,"A moved button carries its subtree");
}
int main(int argc,char**argv){try{spriteProjection();analytic();scrolling();arrangeHelpers();if(argc>1)actual(argv[1],argc>2?std::optional<std::filesystem::path>(argv[2]):std::nullopt);std::cout<<std::setprecision(8)<<"Maximum relative error "<<maximumRelativeError<<"\n";std::cout<<checks<<" source Watch layout checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
