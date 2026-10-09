#include "modules/storage_presentation.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>

namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};unsigned checks{};}
void*operator new(std::size_t n){if(counting)++allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using namespace endfield::modules;using J=ehud::data::Json;
namespace {
void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}
void checkNear(double a,double b,const char*m,double epsilon=1e-7){check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<epsilon,m);}
template<class F>void rejects(F f,const char*m){bool caught{};try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,m);}
void array(const J&a,const J&b,double epsilon=1e-7){check(a.isArray()&&b.isArray()&&a.array().size()==b.array().size(),"Original numeric array extent");for(std::size_t i=0;i<a.array().size();++i)if(a.array()[i].isArray())array(a.array()[i],b.array()[i],epsilon);else checkNear(a.array()[i].number(),b.array()[i].number(),"Original numeric value",epsilon);}
void color(const J&a,const J&b){check(a.isNull()==b.isNull(),"Original optional color");if(!a.isNull())array(a["sRGB"],b["sRGB"],1e-6);}
const J&named(const J&node,std::string_view name){if(node["name"].isString()&&node["name"].string()==name)return node;if(node["children"].isArray())for(const auto&child:node["children"].array()){try{return named(child,name);}catch(const std::out_of_range&){}}throw std::out_of_range("Original layer name absent");}
struct SourceLeaf {const J*node;double x,y;std::string id;};
void flatten(const J&node,double x,double y,std::vector<SourceLeaf>&out,std::string parent={}){
    const auto&b=node["bounds"].array();const auto&p=node["position"].array();const auto&a=node["anchorPoint"].array();
    x+=p[0].number()-a[0].number()*b[2].number();y+=p[1].number()-a[1].number()*b[3].number();
    auto name=node["name"].isString()?node["name"].string():std::string{};
    if(name.starts_with("storage."))out.push_back({&node,x,y,name});
    if(name=="hud.control.highlight")for(std::size_t i=0;i<node["children"].array().size();++i)out.push_back({&node["children"].array()[i],x,y,parent+(i?".rim":".tint")});
    else if(node["children"].isArray())for(const auto&child:node["children"].array())flatten(child,x,y,out,name=="storage.settings.button"?"storage.settings":name=="storage.refresh.button"?"storage.refresh":name);
}
void artwork(const J&source,const StorageArtwork&actual){std::vector<SourceLeaf>leaves;flatten(source,0,0,leaves);check(leaves.size()==actual.surfaces.size()&&leaves.size()==actual.layers["children"].array().size(),"Every original drawing leaf retained in source paint order");
    for(std::size_t i=0;i<leaves.size();++i){const auto&expected=leaves[i];const auto&a=actual.layers["children"].array()[i];const auto&b=*expected.node;const auto&placement=actual.surfaces[i];
        if(a["id"].string()!=expected.id)throw std::runtime_error("Source paint order differs: "+a["id"].string()+" / "+expected.id);
        check(a["kind"]==b["kind"],"Original layer kind");array(a["bounds"],b["bounds"]);checkNear(placement.local.values[12],expected.x,"Original nested X placement");checkNear(placement.local.values[13],expected.y,"Original nested Y placement");
        checkNear(placement.opacity,b["hidden"].boolean()?0:b["opacity"].number(),"Hidden content and feedback opacity use numeric placements");checkNear(a["contentsScale"].number(),b["contentsScale"].number(),"Original text-only supersampling scale");
        color(a["backgroundColor"],b["backgroundColor"]);if(a["borderWidth"].isNumber()){checkNear(a["borderWidth"].number(),b["borderWidth"].number(),"Original border width");color(a["borderColor"],b["borderColor"]);}if(a["cornerRadius"].isNumber())checkNear(a["cornerRadius"].number(),b["cornerRadius"].number(),"Original plate radius");
        if(a["kind"].string()=="text"){
            for(const auto key:{"string","alignment","truncation","wrapped"})check(a["text"][key]==b["text"][key],"Original text content and alignment");
            checkNear(a["text"]["fontSize"].number(),b["text"]["fontSize"].number(),"Original font size");for(const auto key:{"postScriptName","familyName","symbolicTraits"})check(a["text"]["font"][key]==b["text"]["font"][key],"Original font role");color(a["text"]["foregroundColor"],b["text"]["foregroundColor"]);
        }else if(a["kind"].string()=="shape"){
            const auto&sa=a["shape"];const auto&sb=b["shape"];for(const auto key:{"lineCap","lineJoin","fillRule"})check(sa[key]==sb[key],"Original fill/stroke semantics");for(const auto key:{"lineWidth","miterLimit"})checkNear(sa[key].number(),sb[key].number(),"Original stroke dimension");color(sa["fillColor"],sb["fillColor"]);color(sa["strokeColor"],sb["strokeColor"]);
            check(sa["path"].array().size()==sb["path"].array().size(),"Original path command count");for(std::size_t n=0;n<sa["path"].array().size();++n){const auto&x=sa["path"].array()[n];const auto&y=sb["path"].array()[n];check(x["op"]==y["op"],"Original path opcode");array(x["points"],y["points"]);}
        }
        check(placement.rotates==(expected.id=="storage.refresh.arrow"),"Only retained refresh arrow rotates");
    }
}
void original(const char*path){std::ifstream f(path,std::ios::binary|std::ios::ate);check(bool(f)&&f.tellg()>0&&f.tellg()<4*1024*1024,"Bounded explicit original Storage oracle");std::string bytes(static_cast<std::size_t>(f.tellg()),'\0');f.seekg(0);f.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(f),"Complete oracle read");const auto doc=J::parse(bytes);check(doc["schemaVersion"].integer()==1&&!doc["filesystemQueries"].boolean()&&!doc["windowsCreated"].boolean(),"Original fixture is detached with injected capacity");check(doc["cases"].array().size()==10,"Original dark/light initial/loading/ready/failure cases");
    for(const auto&c:doc["cases"].array()){const auto name=c["name"].string();std::cout<<"Comparing "<<name<<'\n';StorageSnapshot snapshot;if(name.ends_with("ready")||name.ends_with("refreshing")||name.ends_with("failed-with-capacity"))snapshot.capacity=StorageCapacity{"Owned startup fixture",1000000000000,420000000000,100};snapshot.isLoading=name.ends_with("loading")||name.ends_with("refreshing");if(name.ends_with("failed-with-capacity"))snapshot.error="Storage capacity is unavailable.";
        StorageAppearance appearance;appearance.dark=c["dark"].boolean();std::array<double,4>blend;for(unsigned n=0;n<4;++n){appearance.accent[n]=c["accent"].array()[n].number();blend[n]=c["cyan"].array()[n].number();}appearance.availableColor=blend;StoragePresentation p(snapshot,appearance);
        check(p.labels().status==c["status"].string(),"Original accessibility status");const auto&actions=c["actions"].array();check(actions.size()==p.actions().size(),"Loading removes only refresh action");for(std::size_t n=0;n<actions.size();++n){const auto&a=p.actions()[n];check(actions[n]["id"].string()==a.id&&actions[n]["label"].string()==a.label,"Original action identity/text");array(actions[n]["rect"],J::Array{a.rect.x,a.rect.y,a.rect.width,a.rect.height});}
        const auto&root=c["root"];StorageSourcePaths paths{named(root,"storage.refresh.arrow")["shape"]["path"],named(root,"storage.settings.button")["children"].array()[1]["children"].array()[0]["shape"]["path"],named(root,"storage.refresh.button")["children"].array()[1]["children"].array()[0]["shape"]["path"]};artwork(root,prepareStorageArtwork(p,paths));
        auto custom=appearance;custom.accent={.1,.2,.3,1};custom.availableColor.reset();p.setAppearance(custom);rejects([&]{prepareStorageArtwork(p,paths);},"Unprovided custom calibrated blend cannot silently approximate source color");
    }
}
void contracts(){
    StoragePresentation p;check(p.labels().caption=="Startup disk"&&p.actions().size()==2,"Initial Storage controls");check(p.actionAt({12,252})=="storage:settings"&&!p.actionAt({178,252}),"Source half-open button hit geometry");
    StorageSnapshot s{StorageCapacity{"disk",1000,400,1},false,{}};check(p.receive(s)&&p.labels().values[0]=="1.0 KB"&&p.labels().meter=="60.0% used","Decimal capacity formatting");auto revision=p.revision();s.capacity->updatedAt=2;check(!p.receive(s)&&p.revision()==revision,"Snapshot timestamp alone does not reraster displayed content");s.isLoading=true;p.receive(s);check(p.actions().size()==1&&!p.actionAt({360,260})&&p.labels().caption=="disk","Refresh loading retains volume label and disables refresh hit");s.error="Storage capacity is unavailable.";p.receive(s);check(p.labels().caption=="disk","Routine loading does not replace quiet volume caption");s.isLoading=false;p.receive(s);check(p.labels().caption=="Storage capacity is unavailable.","Completed error remains actionable with cached capacity");
    check(storageBytes({})=="—"&&storageBytes(-1)=="—"&&storageBytes(0)=="0 B"&&storageBytes(999)=="999 B"&&storageBytes(100000)=="100 KB"&&storageBytes(1e15)=="1.0 PB","Source invalid,decimal and largest units");const auto meter=storageMeter(.58);checkNear(meter[18].width,8.84375*.56,"Original fractional last segment");check(meter[19].width==0&&meter[31].height==17,"Meter geometry retains fixed bounded topology");
    StorageRefreshMotion motion;motion.update(false,true,false,0);check(!motion.sample(0).active,"Concealed loading starts no animation");motion.update(true,true,false,1);auto sample=motion.sample(1);const auto token=sample.generation;check(sample.active&&sample.angle==0,"Loading starts finite source turn");checkNear(motion.sample(1.36).angle,std::numbers::pi,"Source rotation is linear for0.72s");motion.update(true,false,false,1.4);check(motion.sample(1.5).active,"Fast query completion preserves remaining turn");check(motion.complete(token,true,1.72)&&!motion.running(),"Completed one-shot rests at identical orientation");check(!motion.complete(token,true,2),"Duplicate source completion cannot restart turn");
    motion.manualTurn(2);motion.manualTurn(2.1);motion.manualTurn(2.2);auto first=motion.sample(2.2);check(motion.complete(first.generation,true,2.72)&&motion.running(),"Many manual clicks coalesce one queued turn");auto second=motion.sample(2.72);check(second.angle==0&&motion.complete(second.generation,true,3.44)&&!motion.running(),"Only one extra turn is retained");motion.update(true,true,false,4);auto stale=motion.sample(4);motion.update(true,true,true,4.1);check(!motion.running()&&!motion.complete(stale.generation,true,4.72),"Reduce Motion cancels source completion generation");motion.update(true,true,false,5);check(motion.sample(7).active&&motion.sample(7).angle==0,"Delayed callback begins one new source turn without catch-up loops");motion.update(false,false,false,7.1);
    allocations=0;counting=true;for(unsigned n=0;n<10000;++n){check(!motion.sample(8+n*.01).active,"Settled sample remains inactive");check(p.actions().size()==2,"Retained action span");(void)p.actionAt({360,260});}counting=false;check(allocations==0,"Steady samples and action hit tests allocate nothing");
    rejects([&]{motion.sample(std::numeric_limits<double>::quiet_NaN());},"Invalid animation clock rejected");auto bad=s;bad.capacity->availableBytes=1001;revision=p.revision();rejects([&]{p.receive(bad);},"Invalid late capacity rejected before mutation");check(p.revision()==revision,"Rejected capacity preserves displayed revision");
}
}
int main(int argc,char**argv){try{contracts();if(argc==2)original(argv[1]);else check(argc==1,"Pass optional original storage.json");std::cout<<"PASS "<<checks<<" Storage presentation checks\n";}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
