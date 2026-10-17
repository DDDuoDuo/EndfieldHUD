#include "core/source_layout.hpp"
#include "core/shell_packet.hpp"
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

// Counts actual allocations around steady resolves, not implementation counters.
// All test inputs and diagnostics are constructed outside the measured interval.
namespace { std::atomic<std::size_t> allocations{}; bool failAllocation{}; }
void* operator new(std::size_t n){allocations.fetch_add(1,std::memory_order_relaxed);if(failAllocation)throw std::bad_alloc();if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p) noexcept {std::free(p);}
void operator delete[](void*p) noexcept {std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t) noexcept {std::free(p);}
void operator delete[](void*p,std::size_t) noexcept {std::free(p);}
#endif
using namespace endfield::core;
using namespace endfield::core::source;
namespace {
unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char*message,double tolerance=1e-11){check(std::isfinite(actual)&&std::isfinite(expected)&&std::abs(actual-expected)<=tolerance*std::max(1.,std::abs(expected)),message);}
template<class F>void rejects(F&& f,const char*message){bool failed{};try{f();}catch(const std::invalid_argument&){failed=true;}check(failed,message);}
SceneDefinition fixture(){
    Node root;root.id="root";root.rect=RectTransform{{0,0},{0,0},{0,0},{100,80},{.5,.5}};root.children={"rect","bridge","sibling"};
    Node rect;rect.id="rect";rect.parent="root";rect.children={"child"};rect.position={90,90,3};
    rect.rect=RectTransform{{.25,.25},{.75,.75},{3,4},{-10,-8},{.25,.75}};
    Node child;child.id="child";child.parent="rect";child.position={1,2,3};
    Node bridge;bridge.id="bridge";bridge.parent="root";bridge.children={"detachedRect"};bridge.position={7,8,9};
    Node detached;detached.id="detachedRect";detached.parent="bridge";detached.position={100,100,6};
    detached.rect=RectTransform{{0,0},{1,1},{2,3},{20,-10},{.5,.5}};
    Node sibling;sibling.id="sibling";sibling.parent="root";sibling.position={10,20,0};
    // Deliberately not depth-first or parent-first. Source m_Children still wins.
    return SceneDefinition("root",{child,detached,root,sibling,bridge,rect});
}
void sameNodes(std::span<const ResolvedNode>a,std::span<const ResolvedNode>b){
    check(a.size()==b.size(),"Resolved counts match");
    for(std::size_t i=0;i<a.size();++i){
        check(a[i].node==b[i].node&&a[i].rect==b[i].rect&&a[i].activeInHierarchy==b[i].activeInHierarchy,"Resolved identities, rects and inherited activity match");
        for(unsigned j=0;j<16;++j){
            check(std::bit_cast<std::uint64_t>(a[i].localMatrix.values[j])==std::bit_cast<std::uint64_t>(b[i].localMatrix.values[j]),"Incremental local matrix is bit-identical to full source resolve");
            check(std::bit_cast<std::uint64_t>(a[i].worldMatrix.values[j])==std::bit_cast<std::uint64_t>(b[i].worldMatrix.values[j]),"Incremental world matrix is bit-identical to full source resolve");
        }
    }
}
void analytic(){
    const auto definition=fixture();SourceLayout full(definition);IncrementalResolver retained(definition);
    check(retained.nodes().empty()&&!retained.node("root"),"No fabricated resolved nodes before initial evaluation");
    const auto nodes=retained.resolve();check(nodes.size()==6&&retained.lastRebuiltNodeCount()==6,"Initial resolve builds every source node");
    const std::array<std::string_view,6> order{"root","rect","child","bridge","detachedRect","sibling"};
    for(std::size_t i=0;i<order.size();++i)check(nodes[retained.traversalIndices()[i]].node->id==order[i],"Original child traversal ignores serialized array order");
    const auto*r=retained.node("rect");check(r&&r->rect==SourceRect{{-10,-24},{40,32}},"Stretched source rect preserves anchors, delta and off-center pivot");
    near(r->localMatrix.values[12],-9.5,"Parent rect origin and pivot-weighted anchor reference X");
    near(r->localMatrix.values[13],14,"Parent rect origin and pivot-weighted anchor reference Y");near(r->localMatrix.values[14],3,"Rect local Z comes from source position");
    const auto*d=retained.node("detachedRect");check(d&&d->rect==SourceRect{{-10,5},{20,-10}},"Signed sizes survive immediate ordinary Transform parent");
    near(d->localMatrix.values[12],2,"Ordinary Transform parent prevents borrowing distant root anchors");near(d->worldMatrix.values[12],9,"Ordinary Transform still contributes world translation");
    check(d->rect->contains({0,0})&&!d->rect->contains({11,0}),"Signed rect contains uses both bounds");
    check(!SourceRect{{},{0,1}}.contains({0,0}),"Collapsed rect cannot be hit");
    check(d->rect->corners()[2]==Vec3{10,-5,0},"Signed rect corner order is original local geometry");
    sameNodes(nodes,full.resolve());
    const auto*storage=nodes.data();const auto count=allocations.load();
    for(unsigned i=0;i<1000;++i)retained.resolve();
    const auto after=allocations.load();check(after==count,"1000 unchanged frames allocate no memory");
    check(retained.nodes().data()==storage&&retained.lastRebuiltNodeCount()==0&&retained.rebuiltNodeCount()==6&&retained.reusedNodeCount()==6000,"Steady cache retains storage and skips every matrix");
    Overrides overrides;overrides["rect"].localPosition=Vec3{30,40,50};overrides["rect"].positionComponents[2]=17;
    overrides["rect"].localRotation=Quaternion{0,0,std::sqrt(.5),std::sqrt(.5)};overrides["rect"].localScale=Vec3{2,3,4};
    retained.resolve({},overrides);check(retained.lastRebuiltNodeCount()==2,"A changed branch rebuilds only its descendants");
    r=retained.node("rect");near(r->localMatrix.values[12],30,"Full position overrides anchor position");near(r->localMatrix.values[14],17,"Scalar local axis applies after full position and anchors");
    near(r->localMatrix.values[1],2,"TRS scales local X before rotating to +Y");near(r->localMatrix.values[4],-3,"TRS scales local Y before rotating to -X");
    near(retained.node("child")->worldMatrix.values[12],24,"Parent world matrix multiplies child local position");
    sameNodes(retained.nodes(),full.resolve({},overrides));
    overrides["root"].active=false;overrides["child"].active=true;retained.resolve({},overrides);
    for(const auto&n:retained.nodes())check(!n.activeInHierarchy,"Inactive ancestor wins over enabled child");
    check(retained.lastRebuiltNodeCount()==6,"Parent activity invalidates whole branch");
    overrides.erase("root");retained.resolve({},overrides);check(retained.node("child")->activeInHierarchy,"Removing activity override restores authored activity");
    // Known zero is compared by IEEE bits, including a sign that may matter to
    // later source vertex arithmetic. Ordinary operator== would skip this update.
    overrides["child"].positionComponents[0]=0.;retained.resolve({},overrides);
    overrides["child"].positionComponents[0]=-0.;retained.resolve({},overrides);check(retained.lastRebuiltNodeCount()==1,"Signed zero input change is not hidden by numeric equality");
    sameNodes(retained.nodes(),full.resolve({},overrides));
    const auto nan=std::numeric_limits<double>::quiet_NaN();overrides["unknown-source-id"].localPosition=Vec3{nan,nan,nan};
    retained.resolve({},overrides);check(retained.lastRebuiltNodeCount()==0,"Unbound override is ignored like the source resolver");
    const auto unknownCount=allocations.load();retained.resolve({},overrides);check(allocations.load()==unknownCount,"Unchanged NaN bits on an ignored binding do not allocate repeatedly");
    const auto saved=std::vector<ResolvedNode>(retained.nodes().begin(),retained.nodes().end());const auto revision=retained.revision(),rebuilt=retained.rebuiltNodeCount(),reused=retained.reusedNodeCount();
    const auto last=retained.lastRebuiltNodeCount();
    auto invalid=overrides;invalid["sibling"].localPosition=Vec3{nan,0,0};invalid["rect"].sizeDelta=Vec2{200,100};
    rejects([&]{retained.resolve({},invalid);},"Late invalid sibling rejects complete staged update");
    sameNodes(retained.nodes(),saved);check(retained.revision()==revision&&retained.rebuiltNodeCount()==rebuilt&&retained.reusedNodeCount()==reused&&retained.lastRebuiltNodeCount()==last,"Rejected update leaves counters and prior snapshot unchanged");
    retained.resolve({},overrides);check(retained.lastRebuiltNodeCount()==0,"Valid request after rejected update reuses last good inputs");
    auto insufficientMemory=overrides;insufficientMemory["rect"].sizeDelta=Vec2{250,100};insufficientMemory["new-structural-key"].active=true;
    bool memoryRejected=false;failAllocation=true;
    try{retained.resolve({},insufficientMemory);}catch(const std::bad_alloc&){memoryRejected=true;}catch(...){failAllocation=false;throw;}
    failAllocation=false;check(memoryRejected,"Structural override snapshot allocation failure is observable");
    sameNodes(retained.nodes(),saved);check(retained.revision()==revision&&retained.nodes().data()==storage,"Allocation failure cannot publish staged geometry or replace retained storage");
    invalid=overrides;invalid["child"].positionComponents[3]=1;rejects([&]{retained.resolve({},invalid);},"Invalid scalar axis rejected before array access");
    invalid=overrides;invalid["rect"].localRotation=Quaternion{};rejects([&]{retained.resolve({},invalid);},"Zero quaternion transaction rejected");
    invalid=overrides;invalid["rect"].localScale=Vec3{1e308,1e308,1e308};invalid["child"].localScale=Vec3{1e308,1e308,1e308};rejects([&]{retained.resolve({},invalid);},"Finite inputs causing nonfinite descendant world matrix reject transaction");
    sameNodes(retained.nodes(),saved);
    Overrides empty;const SourceRect parent{{-200,-100},{400,200}};retained.resolve(parent,empty);check(retained.lastRebuiltNodeCount()==6,"Root parent rect is an explicit hierarchy dependency");
    sameNodes(retained.nodes(),full.resolve(parent));retained.resolve();check(retained.lastRebuiltNodeCount()==6,"Removing root parent dependency rebuilds all nodes");
    // Source rect validity is independent of an explicit local position. Invalid
    // anchor layout cannot be hidden by a later finite translation override.
    invalid.clear();invalid["rect"].anchorMax=Vec2{nan,1};invalid["rect"].localPosition=Vec3{};
    rejects([&]{retained.resolve({},invalid);},"Nonfinite rect is rejected even with finite explicit position");
}
void changingPayloads(){
    const auto scene=fixture();SourceLayout full(scene);IncrementalResolver retained(scene);
    Overrides pose;pose["rect"].positionComponents[2]=0;pose["sibling"].active=true;
    pose["unknown"].localPosition=Vec3{std::numeric_limits<double>::quiet_NaN(),0,0};
    retained.resolve({},pose);
    for(unsigned i=0;i<40;++i){
        auto&t=pose.at("rect");t.localPosition=i%2?std::optional<Vec3>{{3.,double(i),7.}}:std::nullopt;
        t.anchoredPosition3D=Vec3{double(i),2,3};t.sizeDelta=Vec2{double(i)+10,40};
        t.localScale=Vec3{1,double(i)+1,1};t.localRotation=Quaternion{0,0,.2,1};
        t.anchorMin=Vec2{.1,.2};t.anchorMax=Vec2{.7,.8};t.pivot=Vec2{.3,.4};
        t.positionComponents.at(2)=i%2?-0.:0.;pose.at("sibling").active=i%2?std::optional<bool>(false):std::nullopt;
        const auto count=allocations.load();failAllocation=true;
        try{retained.resolve({},pose);}catch(...){failAllocation=false;throw;}
        failAllocation=false;check(allocations.load()==count,"Same-key scalar and optional changes do not allocate");
        sameNodes(retained.nodes(),full.resolve({},pose));
    }
    const auto saved=std::vector<ResolvedNode>(retained.nodes().begin(),retained.nodes().end());
    const auto revision=retained.revision(),rebuilt=retained.rebuiltNodeCount(),reused=retained.reusedNodeCount();
    auto invalid=pose;invalid.at("rect").sizeDelta=Vec2{200,100};invalid.at("sibling").localPosition=Vec3{std::numeric_limits<double>::quiet_NaN(),0,0};
    rejects([&]{retained.resolve({},invalid);},"Same-key late invalid sibling rejects before committing any payload");
    sameNodes(retained.nodes(),saved);check(retained.revision()==revision&&retained.rebuiltNodeCount()==rebuilt&&retained.reusedNodeCount()==reused,"Same-key rejection preserves geometry, snapshots and counters");
    retained.resolve({},pose);check(retained.lastRebuiltNodeCount()==0,"Retry after same-key failure reuses last successful inputs");
    auto structural=pose;structural.at("rect").positionComponents[0]=4;
    bool failed=false;failAllocation=true;try{retained.resolve({},structural);}catch(const std::bad_alloc&){failed=true;}catch(...){failAllocation=false;throw;}
    failAllocation=false;check(failed,"Adding nested axis key preserves fallible snapshot transaction");sameNodes(retained.nodes(),saved);
    retained.resolve({},structural);sameNodes(retained.nodes(),full.resolve({},structural));
    structural.at("rect").positionComponents.erase(2);retained.resolve({},structural);sameNodes(retained.nodes(),full.resolve({},structural));
    structural.erase("rect");retained.resolve({},structural);sameNodes(retained.nodes(),full.resolve({},structural));
    check(retained.lastRebuiltNodeCount()==2,"Missing override key restores the complete source branch");
}
Json loadJSON(const std::filesystem::path&p){std::ifstream input(p,std::ios::binary);check(bool(input),"Swift oracle opens");const std::string bytes((std::istreambuf_iterator<char>(input)),{});return Json::parse(bytes,32*1024*1024);}
void compareMatrix(const Matrix4& actual,const Json& expected){check(expected.array().size()==16,"Oracle matrix column-major width");for(unsigned i=0;i<16;++i)near(actual.values[i],expected.array()[i].number(),"Original Swift matrix parity");}
void compareOracle(std::span<const ResolvedNode> nodes,const Json& expected){
    check(nodes.size()==expected.object().size(),"Original Swift resolved node count");
    for(const auto&node:nodes){const auto&e=expected[node.node->id];compareMatrix(node.localMatrix,e["local"]);compareMatrix(node.worldMatrix,e["world"]);check(node.activeInHierarchy==e["active"].boolean(),"Original Swift active hierarchy");
        check(node.rect.has_value()==!e["rect"].isNull(),"Original Swift optional rect");if(node.rect)for(unsigned i=0;i<2;++i){near(node.rect->origin[i],e["rect"]["origin"].array()[i].number(),"Original Swift signed rect origin");near(node.rect->size[i],e["rect"]["size"].array()[i].number(),"Original Swift signed rect size");}
    }
}
void actualPacket(const std::filesystem::path&root,const std::optional<std::filesystem::path>&oraclePath){
    packet::Package package(std::filesystem::absolute(root));const auto data=package.loadAnimation();
    auto definition=SceneDefinition::fromJson(data["scene"]);auto library=Library::fromJson(data["library"]);WatchAnimation watch(definition,library);DesktopAmbientMotion ambient(watch,123);
    SourceLayout full(definition);IncrementalResolver retained(definition);
    if(oraclePath){const auto oracle=loadJSON(*oraclePath);for(const auto&row:oracle["poseSamples"].array()){
        auto pose=watch.pose(row["entrance"].number(),{},{},{1000,640});
        if(!row["ambient"].isNull())ambient.apply(row["ambient"].number(),pose);
        if(!row["exit"].isNull())applyClip(watch.exit(),row["exit"].number(),pose,definition);
        const auto nodes=retained.resolve({},pose.transforms);sameNodes(nodes,full.resolve({},pose.transforms));compareOracle(nodes,row["resolved"]);
    }}else{
        for(double t:{0.,.1875,.375,.75}){auto pose=watch.pose(t,{},{},{1000,640});sameNodes(retained.resolve({},pose.transforms),full.resolve({},pose.transforms));}
    }
    auto settled=watch.pose(.75,{},{},{1000,640});retained.resolve({},settled.transforms);
    const auto before=allocations.load();for(unsigned i=0;i<240;++i)retained.resolve({},settled.transforms);const auto after=allocations.load();
    check(before==after&&retained.lastRebuiltNodeCount()==0,"240 unchanged actual desktop poses allocate nothing and rebuild no nodes");
    Pose decoration;ambient.apply(3.25,decoration);for(const auto&[id,t]:decoration.transforms)settled.transforms[id].localRotation=t.localRotation;
    retained.resolve({},settled.transforms);check(retained.lastRebuiltNodeCount()>0&&retained.lastRebuiltNodeCount()<definition.nodes().size()/4,"Ambient motion rebuilds only the original decorative branches");sameNodes(retained.nodes(),full.resolve({},settled.transforms));
    std::cout<<definition.nodes().size()<<" source nodes; ambient rebuilds "<<retained.lastRebuiltNodeCount()<<"; Swift oracle "<<(oraclePath?"compared":"not supplied")<<'\n';
}
}
int main(int argc,char**argv){try{analytic();changingPayloads();if(argc>1)actualPacket(argv[1],argc>2?std::optional<std::filesystem::path>(argv[2]):std::nullopt);std::cout<<checks<<" source layout checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
