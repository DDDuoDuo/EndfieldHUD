#include "modules/shelf_presentation.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};std::size_t checks{};}
void*operator new(std::size_t n){if(counting)++allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
namespace m=endfield::modules;using ehud::data::Json;using endfield::core::Rect;
namespace {
void check(bool b,const char*message){++checks;if(!b)throw std::runtime_error(message);}
void checkNear(double a,double b,const char*message,double tolerance=1e-7){check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<tolerance,message);}
template<class F>void rejects(F f,const char*m){bool rejected{};try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,m);}
Rect rect(const Json&v){const auto&a=v.array();return {a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
const Json*find(const Json&root,std::string_view key,std::string_view value){if(root[key].isString()&&root[key].string()==value)return &root;if(root["children"].isArray())for(const auto&child:root["children"].array())if(const auto*r=find(child,key,value))return r;return nullptr;}
void array(const Json&a,const Json&b,double tolerance=1e-7){check(a.isArray()&&b.isArray()&&a.array().size()==b.array().size(),"Source array shape");for(std::size_t n=0;n<a.array().size();++n)if(a.array()[n].isArray())array(a.array()[n],b.array()[n],tolerance);else checkNear(a.array()[n].number(),b.array()[n].number(),"Source numeric geometry",tolerance);}
void color(const Json&a,const Json&b){check(a.isNull()==b.isNull(),"Source optional color");if(!a.isNull())array(a["sRGB"],b["sRGB"],1e-6);}
void tree(const Json&a,const Json&b,bool localCard=false){
    check(a["kind"]==b["kind"],"Source node kind");array(a["bounds"],b["bounds"]);if(!localCard)array(a["frame"],b["frame"]);
    checkNear(a["opacity"].number(),b["opacity"].number(),"Source node opacity");
    const bool group=b["allowsGroupOpacity"].isNull()||b["allowsGroupOpacity"].boolean();check(a["allowsGroupOpacity"].boolean()==group,"Source group-opacity flag");
    if(a["kind"].string()=="shape"){
        const auto&x=a["shape"];const auto&y=b["shape"];for(const auto key:{"lineWidth","miterLimit"})checkNear(x[key].number(),y[key].number(),"Source stroke dimension");for(const auto key:{"lineCap","lineJoin","fillRule"})check(x[key]==y[key],"Source stroke/fill semantics");color(x["fillColor"],y["fillColor"]);color(x["strokeColor"],y["strokeColor"]);
        check(x["path"].array().size()==y["path"].array().size(),"Source path command count");for(std::size_t n=0;n<x["path"].array().size();++n){const auto&p=x["path"].array()[n];const auto&q=y["path"].array()[n];check(p["op"]==q["op"],"Source path opcode");array(p["points"],q["points"]);}
    }else if(a["kind"].string()=="text"){
        for(const auto key:{"string","alignment","truncation","wrapped"})check(a["text"][key]==b["text"][key],"Source text content/flow");checkNear(a["text"]["fontSize"].number(),b["text"]["fontSize"].number(),"Source font size");color(a["text"]["foregroundColor"],b["text"]["foregroundColor"]);
        check(a["text"]["font"]["postScriptName"]==b["text"]["font"]["postScriptName"],"Source system font weight/role");
        checkNear(a["contentsScale"].number(),b["contentsScale"].number(),"Source glyph-only render scale");
    }
    if(!b["requiredSourceImage"].isNull()||!b["requiredNativeFileIcon"].isNull()){check(!a["contents"].isNull(),"Source image dependency is actual raster artwork");check(a["contentsGravity"]==b["contentsGravity"],"Source image aspect mode");}
    check(a["children"].array().size()==b["children"].array().size(),"Source child paint count/order");for(std::size_t n=0;n<a["children"].array().size();++n)tree(a["children"].array()[n],b["children"].array()[n]);
}
m::FileShelfState::PlatformActions format(){m::FileShelfState::PlatformActions p;p.formatFileSize=[](auto n){return std::to_string(n)+" bytes";};return p;}
std::vector<m::FileShelfItem>items(std::size_t count){std::vector<m::FileShelfItem>out;for(std::size_t n=0;n<count;++n)out.push_back({"synthetic-"+std::to_string(n),"File "+std::to_string(n),"/explicit/synthetic.txt","Text",100,false,{}});return out;}
void tests(){
    m::FileShelfState state(items(10000),{},format());m::ShelfPresentation plan;m::ShelfPresentationStyle style;
    check(plan.update(state,style)&&plan.cards().size()==8,"Large shelf allocates only source-visible artwork");check(plan.chromeImages().size()==1,"Only source add-button Depot dependency in occupied shelf");
    const auto builds=plan.stats().cardBuilds,chrome=plan.chromeRevision(),serial=plan.cards()[0].contentRevision;
    allocations=0;counting=true;for(unsigned n=0;n<1000;++n){check(!plan.update(state,style),"Unchanged state has no work");plan.setFeedback(n%2?std::optional<std::string_view>{"shelf:synthetic-0:select"}:std::nullopt,n%3==0,false);}counting=false;
    check(allocations==0&&plan.stats().cardBuilds==builds,"Steady state and pointer feedback allocate/rebuild nothing");
    state.scrollBy(1);plan.update(state,style);check(plan.stats().cardBuilds==builds&&plan.cards()[0].contentRevision==serial&&plan.cards()[0].full.y==43,"Scroll reuses all existing card content");check(plan.chromeRevision()==chrome,"Scroll keeps chrome raster content");
    const auto placements=plan.placementRevision();state.selectItem("synthetic-0",{},false,false);plan.update(state,style);check(plan.stats().cardBuilds==builds+1&&plan.cards()[0].selectionY==-1.5&&plan.cards()[0].selectionZ==5&&plan.placementRevision()>placements,"Selection changes only its original border and Y/Z placement");
    state.perform("shelf:clear");plan.update(state,style);check(plan.actions()[0].id=="shelf:cancelClear"&&find(plan.chrome(),"id","shelf.clear/question"),"Source clear confirmation controls/text");
    state.scrollBy(80);plan.update(state,style);check(plan.cards().size()==8&&plan.cards()[0].itemID=="synthetic-2"&&plan.actions()[0].id=="shelf:add","Row recycling remains bounded and source scrolling cancels clear");
    const auto clearBuilds=plan.stats().cardBuilds;for(unsigned n=0;n<80;++n){state.scrollBy(n%2?800000:-800000);plan.update(state,style);check(plan.cards().size()<=8,"Repeated large-shelf recycling retains at most eight cards");for(const auto&c:plan.cards())check(c.images.size()==1&&c.feedback.size()<=4,"Per-card icon/control dependencies stay bounded");}check(plan.stats().cardBuilds>clearBuilds,"Recycling actually visits different source rows");
    const auto before=plan.chromeRevision();auto invalid=style;invalid.accent[0]=std::numeric_limits<double>::quiet_NaN();rejects([&]{plan.update(state,invalid);},"Invalid palette rejected before publish");check(plan.chromeRevision()==before,"Rejected update preserves retained output");
    state.showError("Synthetic error");rejects([&]{plan.update(state,style);},"Missing native dynamic error color is explicit");style.errorColor=m::ShelfColor{1,0,0,1};plan.update(state,style);
    style.dark=false;state.setDropTarget(true);rejects([&]{plan.update(state,style);},"Original light accent blend is an explicit dependency");style.lightDropColor=m::ShelfColor{.5,.4,.1,1};plan.update(state,style);check(plan.scrollIndicatorColor()[3]==.6,"Source scrollbar alpha retained");
    m::FileShelfState empty({}, {}, format());style={};style.depotIconAvailable=false;plan.update(empty,style);check(plan.cards().empty()&&plan.chromeImages().empty()&&find(plan.chrome(),"id","shelf.empty/folder"),"Only actual source folder/+ fallback is used when depot is absent");
    check(find(plan.chrome(),"id","shelf:add/title")->operator[]("text")["string"].string()=="+ Add files","Original source fallback add caption");
    auto oversized=items(1);oversized[0].availabilityError=std::string(65537,'x');m::FileShelfState hugeError(std::move(oversized),{},format());const auto saved=plan.chromeRevision();rejects([&]{plan.update(hugeError,style);},"Visible external metadata has an explicit byte bound");check(plan.chromeRevision()==saved&&plan.cards().empty(),"Late invalid card leaves published empty chrome intact");
    auto invalidFormatter=format();invalidFormatter.formatFileSize=[](auto){return std::string(65537,'x');};m::FileShelfState hugeLabel(items(1),{},std::move(invalidFormatter));rejects([&]{plan.update(hugeLabel,style);},"Owner-formatted label must fit retained layout budget");
}
void reference(const char*file){std::ifstream f(file,std::ios::binary|std::ios::ate);check(bool(f),"Explicit source oracle opens");const auto size=f.tellg();check(size>0&&size<16*1024*1024,"Bounded source oracle");std::string text(static_cast<std::size_t>(size),'\0');f.seekg(0);f.read(text.data(),size);check(bool(f),"Oracle read complete");const auto source=Json::parse(text,16*1024*1024);
    check(source["schemaVersion"].integer()==1&&source["cases"].array().size()==20,"Twenty original source states");
    for(const auto&c:source["cases"].array()){
        std::cout<<"Comparing "<<c["name"].string()<<'\n';std::vector<m::FileShelfItem>rows;std::map<std::int64_t,std::string>sizeStrings;
        for(const auto&j:c["items"].array()){m::FileShelfItem item;item.id=j["id"].string();item.name=j["name"].string();item.lastKnownPath=j["lastKnownPath"].string();item.typeDescription=j["typeDescription"].string();item.isDirectory=j["isDirectory"].boolean();if(!j["byteCount"].isNull()){item.byteCount=j["byteCount"].integer();sizeStrings[*item.byteCount]=j["sizeLabel"].string();}if(!j["availabilityError"].isNull())item.availabilityError=j["availabilityError"].string();rows.push_back(std::move(item));}
        m::FileShelfState::PlatformActions platform;platform.formatFileSize=[&](auto bytes){return sizeStrings.at(bytes);};std::optional<std::string>error;if(!c["error"].isNull())error=c["error"].string();m::FileShelfState state(std::move(rows),{},platform,{},error);
        for(const auto&id:c["selectedIDs"].array())state.selectItem(id.string(),{false,true},false,false);state.scrollBy(c["scrollOffset"].number());if(c["confirmingClear"].boolean())state.perform("shelf:clear");state.setDropTarget(c["dropTarget"].boolean());
        m::ShelfPresentationStyle style;style.dark=c["dark"].boolean();style.depotIconAvailable=c["depotAvailable"].boolean();m::ShelfColor red,drop;for(unsigned n=0;n<4;++n){style.accent[n]=c["accent"].array()[n].number();red[n]=c["errorColor"].array()[n].number();drop[n]=c["lightDropColor"].array()[n].number();}style.errorColor=red;style.lightDropColor=drop;
        m::ShelfPresentation plan;plan.update(state,style);const auto&root=c["root"];const auto&actual=root["children"].array();const auto&ours=plan.chrome()["children"].array();
        check(rect(root["bounds"])==m::FileShelfState::bounds(),"Original shelf module bounds");check(actual.size()==6&&ours.size()==5,"Numeric scrollbar separated from retained chrome");
        for(std::size_t n=1;n<ours.size();++n)tree(actual[n+1],ours[n]);
        const auto&collection=actual[0];if(state.items().empty())tree(collection,ours[0]);else check(ours[0]["children"].array().empty(),"Cards are separate retained content resources");
        tree(collection["mask"],ours[0]["mask"]);
        if(const auto scrollbar=plan.scrollIndicator()){check(!actual[1]["hidden"].boolean()&&rect(actual[1]["frame"])==*scrollbar,"Actual source scrollbar position/extent");array(actual[1]["backgroundColor"]["sRGB"],Json::Array{plan.scrollIndicatorColor()[0],plan.scrollIndicatorColor()[1],plan.scrollIndicatorColor()[2],.6},1e-6);checkNear(actual[1]["cornerRadius"].number(),1,"Original scroll radius");}else check(actual[1]["hidden"].boolean(),"Source scrollbar hidden when no overflow");
        std::size_t cardCount{};for(const auto&node:collection["children"].array())if(node["name"].isString()&&node["name"].string().starts_with("shelf.card.")){
            ++cardCount;const auto id=node["name"].string().substr(11);const auto found=std::find_if(plan.cards().begin(),plan.cards().end(),[&](const auto&v){return v.itemID==id;});check(found!=plan.cards().end(),"Every source visible card retained");tree(node,found->artwork,true);
            const auto&transform=node["transform"].array();checkNear(transform[3].array()[1].number(),found->selectionY,"Exact source selection Y");checkNear(transform[3].array()[2].number(),found->selectionZ,"Exact source selection depth");
            check(found->images.size()==1&&found->images[0].requestedPixels==64&&found->images[0].itemID==id,"Native icon owner dependency bounded and source-keyed");
        }check(cardCount==plan.cards().size(),"No invented extra card artwork");
        check(c["actions"].array().size()==plan.actions().size(),"All original actions retained");for(std::size_t n=0;n<plan.actions().size();++n){const auto&a=c["actions"].array()[n];const auto&b=plan.actions()[n];check(a["id"].string()==b.id&&a["label"].string()==b.label&&rect(a["rect"])==b.rect,"Source action identity, text and clipping");}
        for(const auto&a:c["animations"].array())if(a["key"].string()=="shelf.selection.depth"){checkNear(a["duration"].number(),m::FileShelfState::selectionDuration,"Source selection duration");array(a["timing"],Json::Array{Json::Array{0,0},Json::Array{.16,.78},Json::Array{.25,1},Json::Array{1,1}});}else if(a["key"].string()=="shelf.toolbar.reveal")checkNear(a["duration"].number(),m::FileShelfState::toolbarDuration,"Source toolbar duration");else if(a["key"].string()=="shelf.drop.trace")checkNear(a["duration"].number(),m::FileShelfState::dropDuration,"Source drop duration");
    }
}
}
int main(int argc,char**argv){try{tests();if(argc==2)reference(argv[1]);else if(argc!=1)throw std::invalid_argument("Usage: shelf_presentation_tests [shelf.json]");std::cout<<"PASS "<<checks<<" shelf presentation checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
