#include "core/source_watch_frame.hpp"
#include "core/shell_packet.hpp"
#include <bit>
#include <atomic>
#include <cstdlib>
#include <new>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
namespace {std::atomic<std::size_t> allocations{};}
void* operator new(std::size_t n){allocations.fetch_add(1,std::memory_order_relaxed);if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
using namespace endfield::core;
using namespace endfield::core::source;
namespace {
unsigned checks{};double maximumFloatError{};
void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
void near(double a,double b,const std::string&name,double tolerance=2e-6){const auto error=std::abs(a-b)/std::max(1.,std::abs(b));maximumFloatError=std::max(maximumFloatError,error);if(!std::isfinite(a)||!std::isfinite(b)||error>tolerance){std::ostringstream m;m<<std::setprecision(17)<<name<<": "<<a<<" != "<<b<<" relative "<<error;throw std::runtime_error(m.str());}++checks;}
template<std::size_t N>std::optional<std::array<double,N>> vec(const Json&j){if(j.isNull())return {};check(j.array().size()==N,"Pose vector width");std::array<double,N>v;for(unsigned i=0;i<N;++i)v[i]=j.array()[i].number();return v;}
Pose poseJSON(const Json&j){Pose p;for(const auto&[id,v]:j["transforms"].object()){auto&t=p.transforms[id];t.localPosition=vec<3>(v["position"]);t.localScale=vec<3>(v["scale"]);t.localRotation=vec<4>(v["rotation"]);t.anchoredPosition3D=vec<3>(v["anchored"]);t.anchorMin=vec<2>(v["anchorMin"]);t.anchorMax=vec<2>(v["anchorMax"]);t.sizeDelta=vec<2>(v["sizeDelta"]);t.pivot=vec<2>(v["pivot"]);if(!v["active"].isNull())t.active=v["active"].boolean();for(const auto&[key,x]:v["components"].object())t.positionComponents[static_cast<unsigned>(std::stoul(key))]=x.number();}
 for(const auto&[id,v]:j["properties"].object())for(const auto&[name,x]:v.object())p.properties[id][name]=x.number();for(const auto&v:j["unbound"].array())p.unboundPaths.insert(v.string());for(const auto&v:j["unregistered"].array())p.unregisteredBindings.insert(v.string());return p;}
Matrix4 matrix(const Json&j){Matrix4 m;check(j.array().size()==4,"Matrix columns");for(unsigned c=0;c<4;++c){check(j.array()[c].array().size()==4,"Matrix rows");for(unsigned r=0;r<4;++r)m.values[c*4+r]=j.array()[c].array()[r].number();}return m;}
void compareMatrix(const Matrix4&m,const Json&j,const std::string&label,double tolerance=2e-6){const auto expected=matrix(j);for(unsigned i=0;i<16;++i)near(m.values[i],expected.values[i],label,tolerance);}
void compareRect(const SourceRect&r,const Json&j){for(unsigned i=0;i<2;++i){near(r.origin[i],j["origin"].array()[i].number(),"Hit rect origin",1e-10);near(r.size[i],j["size"].array()[i].number(),"Hit rect size",1e-10);}}
void compare(const SourceWatchFrame&f,const packet::FrameData&expected,const packet::Package&package){
 const auto&j=expected.metadata;check(f.batches.size()==j["batches"].array().size(),"Exact source batch count");check(f.hits.size()==j["hits"].array().size(),"Exact source hit count");
 for(std::size_t i=0;i<f.batches.size();++i){const auto&b=f.batches[i];const auto&e=j["batches"].array()[i];const auto context=std::to_string(i)+"/"+b.sourceMesh;
  check(b.sourceNodeID==e["sourceNodeID"].string(),"Original batch node/order");check(b.sourceMesh==e["sourceMesh"].string(),"Original mesh identity/order");check(b.material==e["material"].string(),"Exact source material variant");check(b.appliesDesktopAccent==e["appliesDesktopAccent"].boolean(),"Profile accent isolation");compareMatrix(b.world,e["worldMatrix"],"Batch world");for(unsigned c=0;c<4;++c)near(b.color[c],e["color"].array()[c].number(),"Color32/tint/linear/alpha");
  check(b.textureOverrides.size()==e["textureOverrides"].object().size(),"Exact texture binding count");for(const auto&[name,id]:b.textureOverrides)check(e["textureOverrides"][name].string()==id,"Original texture override");
  check(b.uniformOverrides.size()==e["uniformOverrides"].object().size(),"Exact source override count");for(const auto&[name,v]:b.uniformOverrides){const auto&w=e["uniformOverrides"][name].array();check(v.size()==w.size(),"Source override width");for(std::size_t c=0;c<v.size();++c)near(v[c],w[c].number(),context+" uniform "+name);}
  if(b.geometry){const auto mesh=package.loadMesh(e["mesh"].string());const auto&g=b.geometry->mesh;check(g.positions.size()==mesh.vertices.size()&&g.uv.size()==mesh.vertices.size(),"Generated source vertex count");check(g.indices==mesh.indices,"Exact source triangle winding");for(std::size_t v=0;v<g.positions.size();++v){for(unsigned c=0;c<4;++c)near(g.positions[v][c],mesh.vertices[v].position[c],context+" vertex");for(unsigned c=0;c<2;++c)check(std::bit_cast<std::uint32_t>(g.uv[v][c])==std::bit_cast<std::uint32_t>(mesh.vertices[v].uv[c]),"Source UV Float bits");}}
 }
 for(std::size_t i=0;i<f.hits.size();++i){const auto&h=f.hits[i];const auto&e=j["hits"].array()[i];if(!e["center"].isNull()){const auto&center=e["center"].array();const auto hit=f.buttonAt({center[0].number(),center[1].number()},expected.projection*expected.view,{0,0,expected.viewport.x,expected.viewport.y});check(hit.has_value()==!e["centerRaycastButtonID"].isNull(),"Original raycast presence at source-projected center");if(hit)check(*hit==e["centerRaycastButtonID"].string(),"Original topmost clipped raycast result");}check(h.graphicID==e["graphicID"].string()&&h.buttonID==e["buttonID"].string(),"Source raycast order/owner");compareRect(h.rect,e["rect"]);compareMatrix(h.world,e["worldMatrix"],"Hit world",1e-10);check(h.masks.size()==e["masks"].array().size(),"Original hit mask count");for(std::size_t m=0;m<h.masks.size();++m){compareRect(h.masks[m].rect,e["masks"].array()[m]["rect"]);compareMatrix(h.masks[m].world,e["masks"].array()[m]["worldMatrix"],"Mask world",1e-10);}}
 check(f.diagnostics.size()==j["diagnostics"].array().size(),"Original diagnostics count");for(std::size_t i=0;i<f.diagnostics.size();++i)check(f.diagnostics[i]==j["diagnostics"].array()[i].string(),"Original diagnostics");
 check(f.resolved.size()==j["nodes"].array().size(),"Resolved original node count");for(const auto&e:j["nodes"].array()){const auto id=e["id"].string();const auto n=std::find_if(f.resolved.begin(),f.resolved.end(),[&](const auto&v){return v.node->id==id;});check(n!=f.resolved.end()&&n->activeInHierarchy==e["active"].boolean(),"Original resolved node/active");compareMatrix(n->worldMatrix,e["worldMatrix"],"Resolved world",1e-10);}
}
void actual(const std::filesystem::path&root){packet::Package package(std::filesystem::absolute(root));const auto data=package.loadAnimation();auto scene=SceneDefinition::fromJson(data["scene"]);auto doc=MountedLayoutDocument::fromJson(data["mountedDocument"]);auto resources=SourceWatchFrameResources::fromJson(data["frameBuilder"]);SourceWatchFrameBuilder builder(scene,doc,resources);unsigned frames{},ambientFrames{};std::optional<Pose> priorAmbient;
 for(const auto&[name,descriptor]:package.frames()){(void)descriptor;const auto oracle=package.loadFrame(name);const auto&input=oracle.metadata["builderInput"];if(input.isNull())continue;builder.setDesktopSettings(SourceDesktopFrameSettings::fromJson(input["desktopSettings"]));const auto pose=poseJSON(input["pose"]);DesktopNavigationLayout navigation(scene,doc,input["entryCount"].integer());SourceFrameTints tints;for(const auto&[id,v]:input["selectableTints"].object()){SourceFloat4 tint;for(unsigned c=0;c<4;++c)tint[c]=static_cast<float>(v.array()[c].number());tints[id]=tint;}
  if(!input["ambientPose"].isNull()){
   const auto ambient=poseJSON(input["ambientPose"]);const auto size=*vec<2>(input["canvasResolution"]);const auto revision=builder.presentationRevision();
   const auto direct=builder.buildSettledAmbient(ambient,revision,oracle.worldRoot,size,input["scroll"].number(),&navigation,tints);
   if(name.ends_with("-ambient-1")||name.ends_with("-ambient-2")){check(direct!=nullptr,"Original settled ambient sample uses retained source subtree");compare(*direct,oracle,package);check(builder.stats().lastAmbientResolvedNodes==29,"Actual ambient sample resolves only 29 of 834 nodes");
    check(priorAmbient.has_value(),"Previous source ambient sample is available");const auto counters=builder.stats();const auto allocated=allocations.load();
    for(unsigned tick=0;tick<120;++tick){const auto*sample=builder.buildSettledAmbient(tick%2?ambient:*priorAmbient,revision,oracle.worldRoot,size,input["scroll"].number(),&navigation,tints);if(!sample)throw std::runtime_error("Actual changing ambient sample rejected");}
    check(allocations.load()==allocated,"120 changing actual ambient frames allocate no memory");
    check(builder.stats().ambientResolvedNodes==counters.ambientResolvedNodes+120*29&&builder.stats().layoutBuilds==counters.layoutBuilds&&builder.stats().localImageBuilds==counters.localImageBuilds,"Actual ambient updates only 29 nodes and retains local image topology/layout");++ambientFrames;}
   else check(direct==nullptr,"Changed ambient size/root/presentation rejects direct sample");
   priorAmbient=ambient;
  }
  std::cerr<<"Comparing "<<name<<'\n';const auto beforeWorld=builder.stats().worldOnlyFrames;const auto scroll=input["scroll"].number();compare(builder.build(pose,oracle.worldRoot,scroll,&navigation,tints),oracle,package);if(name.find("-tilted-")!=std::string::npos)check(builder.stats().worldOnlyFrames==beforeWorld+1,"Tilted original oracle exercises retained world-only path");const auto first=builder.stats();const auto allocated=allocations.load();for(unsigned idle=0;idle<60;++idle)builder.build(pose,oracle.worldRoot,scroll,&navigation,tints);check(allocations.load()==allocated,"60 unchanged actual frames allocate no memory");const auto reused=builder.stats();check(reused.reusedFrames==first.reusedFrames+60&&reused.bakedGeometryBuilds==first.bakedGeometryBuilds,"Unchanged frames reuse retained output without rebuilding geometry");compare(builder.build(pose,oracle.worldRoot,scroll,&navigation,tints,true),oracle,package);++frames;
 }if(!resources.ambientRotationNodes.empty())check(ambientFrames==4,"Both viewports exercise changing original ambient samples");check(frames>0,"Actual original inputPose cases supplied");std::cout<<frames<<" original desktop builder frames compared\n";}
void ambientSynthetic(){
 Node root;root.id="root";root.children={"decoration","button"};root.rect=RectTransform{{},{},{},{100,80},{.5,.5}};
 Node decoration;decoration.id="decoration";decoration.parent="root";decoration.children={"leaf"};decoration.position={4,3,0};decoration.rect=RectTransform{{},{},{},{12,10},{.5,.5}};
 Node leaf;leaf.id="leaf";leaf.parent="decoration";leaf.position={7,9,0};leaf.rect=RectTransform{{},{},{},{3,5},{.5,.5}};
 Node button;button.id="button";button.parent="root";button.rect=RectTransform{{},{},{},{5,5},{.5,.5}};
 SceneDefinition scene("root",{root,decoration,leaf,button});MountedLayoutDocument doc;
 Json canvas(Json::Object{});canvas["m_SortingOrder"]=6080;doc.components["root"]={{"canvas","MonoBehaviour","Canvas",canvas},{"cut","MonoBehaviour","UIWatchPanelCut",Json::Object{}}};
 for(const auto&id:{"decoration","leaf","button"}){Json image(Json::Object{});image["m_RaycastTarget"]=std::string_view(id)=="button";doc.components[id].push_back({std::string("image/")+id,"MonoBehaviour","UIImage",image});}
 doc.components["button"].push_back({"button","MonoBehaviour","UIButton",Json::Object{}});
 SourceWatchFrameResources resources;resources.materialVariants["__ui_default"][0]="__ui_default";resources.textureSizes["__white"]={1,1};resources.ambientRotationNodes.insert("decoration");
 SourceWatchFrameBuilder builder(scene,doc,resources);Pose full;full.transforms["root"].sizeDelta=Vec2{100,80};full.transforms["decoration"].localScale=Vec3{1.2,.8,1};full.transforms["decoration"].localRotation=Quaternion{0,0,0,1};
 const auto&initial=builder.build(full,{});check(initial.hits.size()==1,"Ambient fixture retains static input target");const auto hit=initial.hits[0].world;const auto revision=builder.presentationRevision();const auto stats=builder.stats();
 Pose a,b;a.transforms["decoration"].localRotation=Quaternion{0,0,.2,.98};b.transforms["decoration"].localRotation=Quaternion{0,0,-.3,.95};
 check(builder.buildSettledAmbient(a,revision,{},Vec2{100,80})!=nullptr,"Rotation-only settled sample accepted");
 check(builder.stats().lastAmbientResolvedNodes==2&&builder.stats().layoutBuilds==stats.layoutBuilds,"Ambient visits branch only and skips layout");
 const auto allocated=allocations.load();for(unsigned i=0;i<240;++i){const auto*frame=builder.buildSettledAmbient(i%2?a:b,revision,{},Vec2{100,80});if(!frame)throw std::runtime_error("Retained ambient sample rejected");}
 check(allocations.load()==allocated,"240 changing ambient frames allocate no memory");
 const auto*current=builder.buildSettledAmbient(b,revision,{},Vec2{100,80});const auto leafWorld=current->resolved[2].worldMatrix;const auto positions=current->batches[1].geometry->mesh.positions;
 check(current->hits[0].world==hit&&current->inheritedAlpha[2]==1,"Ambient leaves hits and alpha unchanged");
 full.transforms["decoration"].localRotation=b.transforms.at("decoration").localRotation;const auto&forced=builder.build(full,{},1,nullptr,SourceWatchFrameBuilder::emptyTints(),true);
 check(forced.resolved[2].worldMatrix==leafWorld&&forced.batches[1].geometry->mesh.positions==positions,"Ambient subtree equals full original layout resolution and geometry");
 check(builder.buildSettledAmbient(a,revision,{},Vec2{100,80})==nullptr,"Full rebuild invalidates old revision");const auto next=builder.presentationRevision();
 check(!builder.buildSettledAmbient(a,next,Matrix4::translation(1,0),Vec2{100,80}),"Pointer root change rejects direct ambient");
 check(!builder.buildSettledAmbient(a,next,{},Vec2{101,80}),"Canvas resolution change rejects direct ambient");
 check(!builder.buildSettledAmbient(a,next,{},Vec2{100,80},.5),"Scroll change rejects direct ambient");
 SourceFrameTints tint;tint["leaf"]={1,0,0,1};check(!builder.buildSettledAmbient(a,next,{},Vec2{100,80},1,nullptr,tint),"Hover tint change rejects direct ambient");
 auto malformed=a;malformed.transforms["decoration"].active=false;check(!builder.buildSettledAmbient(malformed,next,{},Vec2{100,80}),"Nonrotation override rejects direct ambient");
 malformed=a;malformed.properties["leaf"]["m_Color.a"]=.2;check(!builder.buildSettledAmbient(malformed,next,{},Vec2{100,80}),"Material mutation rejects direct ambient");
 const auto before=builder.stats();full.transforms["decoration"].localRotation=a.transforms.at("decoration").localRotation;builder.build(full,{});check(builder.stats().ambientFrames==before.ambientFrames+1,"Complete pose detects exact ambient-only change");
 const auto&tilted=builder.build(full,Matrix4::translation(10,20));check(builder.stats().worldOnlyFrames==before.worldOnlyFrames+1,"Pointer path remains retained after ambient sample");const auto moved=tilted.resolved[2].worldMatrix;const auto movedGeometry=tilted.batches[1].geometry->mesh.positions;
 const auto&fullTilt=builder.build(full,Matrix4::translation(10,20),1,nullptr,SourceWatchFrameBuilder::emptyTints(),true);check(fullTilt.resolved[2].worldMatrix==moved&&fullTilt.batches[1].geometry->mesh.positions==movedGeometry,"Pointer after ambient equals complete rebuilt pose");
 SourceDesktopFrameSettings settings;settings.graphicStyles["leaf"].opacity=.5;const auto old=builder.presentationRevision();builder.setDesktopSettings(settings);check(!builder.buildSettledAmbient(a,old,Matrix4::translation(10,20),Vec2{100,80}),"Settings invalidate ambient cache before next full build");
 // A decorative branch becoming interactive cannot keep the hit-free path.
 doc.components["decoration"].push_back({"ambient-button","MonoBehaviour","UIButton",Json::Object{}});doc.components["decoration"][0].data["m_RaycastTarget"]=true;
 SourceWatchFrameBuilder interactive(scene,doc,resources);interactive.build(full,{});check(!interactive.buildSettledAmbient(a,interactive.presentationRevision(),{},Vec2{100,80}),"Moving hit targets disable ambient-only optimization");
}
void synthetic(){Node root;root.id="root";root.name="root";root.rect=RectTransform{{},{},{},{100,80},{.5,.5}};SceneDefinition scene("root",{root});MountedLayoutDocument doc;Json canvas(Json::Object{});canvas["m_SortingOrder"]=6080;doc.components["root"]={{"canvas","MonoBehaviour","Canvas",canvas},{"cut","MonoBehaviour","UIWatchPanelCut",Json::Object{}},{"button","MonoBehaviour","UIButton",Json::Object{}}};Json image(Json::Object{});image["m_RaycastTarget"]=true;doc.components["root"].push_back({"image","MonoBehaviour","UIImage",image});SourceWatchFrameResources resources;resources.materialVariants["__ui_default"][0]="__ui_default";resources.textureSizes["__white"]={1,1};SourceWatchFrameBuilder builder(scene,doc,resources);const auto&f=builder.build({},{});check(f.batches.size()==1&&f.hits.size()==1,"Synthetic original UIImage produces one batch and hit");check(f.batches[0].geometry&&f.batches[0].geometry->mesh.indices==std::vector<std::uint32_t>{0,1,2,2,3,0},"Original source quad winding");const auto initial=builder.stats();const auto&shifted=builder.build({},Matrix4::translation(12,34,56));check(shifted.batches[0].world.values[12]==12&&builder.stats().worldOnlyFrames==initial.worldOnlyFrames+1,"World-only presentation updates source Canvas matrix");check(builder.stats().bakedGeometryBuilds==initial.bakedGeometryBuilds&&builder.stats().layoutBuilds==initial.layoutBuilds,"World-only Canvas movement retains local geometry and metrics");Pose transparent;transparent.properties["root"]["m_Color.a"]=0;const auto&t=builder.build(transparent,{});check(t.batches.empty()&&t.hits.size()==1,"Invisible graphic retains source raycast before alpha filtering");SourceDesktopFrameSettings settings;settings.hiddenNodes.insert("root");builder.setDesktopSettings(settings);check(builder.build({},{}).batches.empty()&&builder.build({},{}).hits.empty(),"Explicit desktop hidden nodes remove original graphics and hits");}
}
int main(int argc,char**argv){try{synthetic();ambientSynthetic();if(argc>1)actual(argv[1]);std::cout<<checks<<" source frame checks passed; max normalized floating error "<<maximumFloatError<<'\n';return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
