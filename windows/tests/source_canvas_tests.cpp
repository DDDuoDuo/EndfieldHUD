#include "core/source_canvas.hpp"
#include "core/source_camera.hpp"
#include "core/shell_packet.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {std::atomic<std::size_t> allocations{};}
void* operator new(std::size_t n){++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
using namespace endfield::core;
using namespace endfield::core::source;
namespace {
unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char*message,double tolerance=1e-10){check(std::isfinite(actual)&&std::abs(actual-expected)<=tolerance*std::max(1.,std::abs(expected)),message);}
template<class F>void rejects(F&&f,const char*message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,message);}
WatchComponent component(std::string id,std::string type,Json::Object data={}){return {std::move(id),std::move(type),{},std::move(data)};}
Json vec4(double x,double y,double z,double w){return Json::Object{{"x",x},{"y",y},{"z",z},{"w",w}};}
void fixture(){
    Node root;root.id="root";root.children={"mask"};root.rect=RectTransform{{},{},{},{100,80},{.5,.5}};
    Node mask;mask.id="mask";mask.parent="root";mask.children={"graphic","boundary"};mask.rect=RectTransform{{.5,.5},{.5,.5},{},{50,40},{.5,.5}};
    Node graphic;graphic.id="graphic";graphic.parent="mask";graphic.rect=RectTransform{{.5,.5},{.5,.5},{},{60,60},{.5,.5}};
    Node boundary;boundary.id="boundary";boundary.parent="mask";boundary.children={"inside"};
    Node inside;inside.id="inside";inside.parent="boundary";
    SceneDefinition scene("root",{graphic,root,inside,mask,boundary});SourceLayout layout(scene);MountedLayoutDocument doc;
    doc.components["root"]={component("rootCanvas","Canvas",{{"m_SortingOrder",6080}}),component("rootGroup","CanvasGroup",{{"m_Alpha",.5},{"m_BlocksRaycasts",false}}),component("rootSoft","UISoftMask")};
    doc.components["mask"]={component("maskCanvas","Canvas",{{"m_SortingOrder",42}}),component("maskRect","RectMask2D",{{"m_Padding",vec4(1,2,3,4)},{"m_Softness",vec4(5,6,0,0)},{"m_HGSoftness",vec4(7,8,9,10)}}),component("button","UIButton"),component("maskGroup","CanvasGroup",{{"m_Alpha",.4}})};
    doc.components["graphic"]={component("graphicGroup","CanvasGroup",{{"m_Alpha",.25},{"m_IgnoreParentGroups",true}}),component("disabledSoft","UISoftMask",{{"m_Enabled",false}})};
    doc.components["boundary"]={component("boundaryCanvas","Canvas",{{"m_SortingOrder",4}}),component("sorting","UISortingOrder",{{"m_Enabled",false},{"_renderType",1},{"_sortingOrderOffset",9}})};
    SourceCanvasPlan plan(scene,doc,100);const auto rootI=*layout.nodeIndex("root"),maskI=*layout.nodeIndex("mask"),graphicI=*layout.nodeIndex("graphic"),boundaryI=*layout.nodeIndex("boundary"),insideI=*layout.nodeIndex("inside");
    check(plan.sorting()[rootI].sortingOrder==100,"Runtime panel base replaces authored root order");
    check(plan.sorting()[maskI].sortingOrder==100&&plan.sorting()[maskI].localSortingOrder==42,"Non-override subcanvas inherits effective order while preserving local order");
    check(plan.sorting()[boundaryI].sortingOrder==109&&plan.sorting()[insideI].sortingOrder==109,"Registered disabled sorting component uses panel base, not parent offset");
    check(plan.ancestry()[insideI].rectMasks.empty(),"Override Canvas resets ancestor rect-mask stack");
    check(plan.ancestry()[graphicI].button==maskI&&!plan.ancestry()[graphicI].softMask,"Closest disabled soft mask blocks ancestor search while UIButton still inherits");
    check(plan.ancestry()[graphicI].acceptsInput&&!plan.ancestry()[maskI].acceptsInput,"Ignore-parent CanvasGroup restores input without altering alpha inheritance");
    near(plan.inheritedAlpha()[graphicI],.05,"CanvasGroup alpha multiplies all enabled ancestors regardless of input ignore flag");
    auto nodes=layout.resolve();auto clip=plan.clip(graphicI,nodes,inverseSourceMatrix(nodes[maskI].worldMatrix));check(clip.has_value(),"Graphic receives nearest source rectangle");
    const std::array<float,4> bounds{-24,-18,22,16};check(clip->rectangle==bounds&&clip->parameters==std::array<float,4>{0,5,6,0}&&clip->hgSoftness==std::array<float,4>{7,8,9,10},"Clip padding and softness remain in authored units");
    check(!plan.clip(maskI,nodes,inverseSourceMatrix(nodes[maskI].worldMatrix))->hasSoftness,"Own mask does not supply graphic softness");
    check(!plan.clip(insideI,nodes,{}),"Sorting-boundary descendant has no inherited clip");
    Pose pose;pose.properties["mask"]["m_Alpha"]=.2;plan.updateAlpha(pose);near(plan.inheritedAlpha()[graphicI],.025,"Animated CanvasGroup channel updates descendants");
    const auto rebuilt=plan.alphaRebuilds();const auto before=allocations.load();for(unsigned i=0;i<240;++i){plan.updateAlpha(pose);(void)plan.clip(graphicI,nodes,{});}const auto after=allocations.load();
    check(before==after&&plan.alphaRebuilds()==rebuilt,"Idle alpha and pointer clip updates allocate no memory");
    pose.properties["root"]["m_Alpha"]=std::numeric_limits<double>::infinity();rejects([&]{plan.updateAlpha(pose);},"Nonfinite alpha rejects the entire pose");near(plan.inheritedAlpha()[graphicI],.025,"Rejected alpha leaves prior frame intact");
    const std::set<std::string,std::less<>> none;SourceCanvasPlan unregistered(scene,doc,100,&none);check(unregistered.sorting()[boundaryI].sortingOrder==100&&!unregistered.ancestry()[insideI].rectMasks.empty(),"Explicit unregistered sorting set preserves parent canvas mask and order");
    rejects([&]{SourceCanvasPlan overflow(scene,doc,std::numeric_limits<std::int64_t>::max());},"Sorting overflow is rejected");
}
Json read(const std::filesystem::path&path){std::ifstream f(path);if(!f)throw std::runtime_error("Cannot read explicit oracle");return Json::parse(std::string(std::istreambuf_iterator<char>(f),{}),64*1024*1024);}
Matrix4 matrix(const Json&j){Matrix4 m;for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)m.values[c*4+r]=j.array()[c].array()[r].number();return m;}
void oracle(const std::filesystem::path&packetRoot,const std::filesystem::path&oraclePath){
    endfield::core::packet::Package packet(packetRoot);const auto animation=packet.loadAnimation();const auto scene=SceneDefinition::fromJson(animation["scene"]);const auto doc=MountedLayoutDocument::fromJson(animation["mountedDocument"]);SourceLayout layout(scene);const auto expected=read(oraclePath);
    for(const auto&row:expected["sorting"].array()){
        std::set<std::string,std::less<>> registered;for(const auto&id:row["registered"].isArray()?row["registered"].array():Json::Array{})registered.insert(id.string());
        SourceCanvasPlan plan(scene,doc,std::int64_t(row["base"].number()),row["registered"].isNull()?nullptr:&registered);
        for(std::size_t i=0;i<scene.nodes().size();++i){const auto&e=row["states"][scene.nodes()[i].id];const auto&s=plan.sorting()[i];
            check((s.nearestCanvas?scene.nodes()[*s.nearestCanvas].id:"")==(e["nearest"].isNull()?"":e["nearest"].string()),"Original Swift nearest Canvas matches");
            check(s.sortingOrder==std::int64_t(e["order"].number())&&s.overrideSorting==e["override"].boolean(),"Original Swift effective sorting order and boundary match");
            check(s.localSortingOrder==(e["local"].isNull()?std::optional<std::int64_t>{}:std::optional<std::int64_t>(std::int64_t(e["local"].number()))),"Original Swift local Canvas sorting matches");
        }
    }
    SourceCanvasPlan plan(scene,doc,6080);
    for(const auto&row:expected["alphas"].array()){Pose pose;for(const auto&[id,props]:row["properties"].object())for(const auto&[key,v]:props.object())pose.properties[id][key]=v.number();plan.updateAlpha(pose);
        for(std::size_t i=0;i<scene.nodes().size();++i)near(plan.inheritedAlpha()[i],row["alpha"][scene.nodes()[i].id].number(),"Original Swift inherited alpha matches",1e-14);
    }
    unsigned comparedClips{};
    for(const auto&record:packet.metadata()["frames"].array()){
        const auto frame=packet.loadFrame(record["name"].string());std::vector<ResolvedNode> nodes(scene.nodes().size());
        for(const auto&n:frame.metadata["nodes"].array()){const auto index=layout.nodeIndex(n["id"].string());check(index.has_value(),"Frame node belongs to mounted source scene");auto&r=nodes[*index];r.node=&scene.nodes()[*index];r.worldMatrix=matrix(n["worldMatrix"]);r.activeInHierarchy=n["active"].boolean();if(!n["rect"].isNull()){const auto&rect=n["rect"];r.rect=SourceRect{{rect["origin"].array()[0].number(),rect["origin"].array()[1].number()},{rect["size"].array()[0].number(),rect["size"].array()[1].number()}};}}
        for(const auto&batch:frame.metadata["batches"].array()){
            const auto&u=batch["uniformOverrides"];if(u["clipRect"].isNull())continue;const auto node=*layout.nodeIndex(batch["sourceNodeID"].string());const auto canvas=plan.sorting()[node].nearestCanvas;check(canvas.has_value(),"Clipped source graphic has Canvas");
            const auto actual=plan.clip(node,nodes,inverseSourceMatrix(nodes[*canvas].worldMatrix));check(actual.has_value(),"Original source clipped pass retains rectangular mask");
            for(unsigned i=0;i<4;++i)near(actual->rectangle[i],u["clipRect"].array()[i].number(),"Mac frame clip rectangle matches",2e-6);
            if(!u["clipRectParam"].isNull()){check(actual->hasSoftness,"Source clip softness comes from an ancestor");for(unsigned i=0;i<4;++i){near(actual->parameters[i],u["clipRectParam"].array()[i].number(),"Mac frame softness matches");near(actual->hgSoftness[i],u["uiMaskHGSoftness"].array()[i].number(),"Mac frame HG softness matches");}}
            ++comparedClips;
        }
    }
    check(comparedClips>100,"Clipping verification covered actual animated shell passes");
}
}
int main(int argc,char**argv){try{fixture();if(argc==3)oracle(std::filesystem::absolute(argv[1]),argv[2]);else if(argc!=1)throw std::runtime_error("Expected optional explicit packet root and oracle");std::cout<<checks<<" source Canvas checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
