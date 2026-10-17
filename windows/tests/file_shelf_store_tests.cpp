#include "core/data/file_shelf_store.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
using namespace ehud::data;
namespace {
std::size_t checks{};
void check(bool b,const char* message){++checks;if(!b)throw std::runtime_error(message);}
template<class F>void rejects(StoreErrorCode code,F f,const char* message){try{f();}catch(const StoreError& e){check(e.code()==code,message);return;}throw std::runtime_error(message);}
template<class F>void fails(F f,const char* message){try{f();}catch(const std::exception&){++checks;return;}throw std::runtime_error(message);}
struct Temporary {
    std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-shelf-fixture-"+makeUUID());
    Temporary(){std::filesystem::create_directory(root);}
    ~Temporary(){std::error_code e;std::filesystem::remove_all(root,e);}
};
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("Missing fixture");return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& p,const std::string& value){std::filesystem::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out.write(value.data(),std::streamsize(value.size()));if(!out)throw std::runtime_error("Fixture write failed");}
std::string uuid(unsigned n){const auto suffix=std::to_string(n);return "00000000-0000-4000-8000-"+std::string(12-suffix.size(),'0')+suffix;}
ShelfFileMetadata metadata(unsigned n){ShelfFileMetadata value;value.windowsPath="C:\\synthetic\\file-"+std::to_string(n)+".txt";value.name="file-"+std::to_string(n)+".txt";value.typeDescription="Text document";value.byteCount=n*1024;
    value.identity.objectID[0]=std::uint8_t(n);value.identity.objectID[15]=0xfe;value.identity.volumeSerial=std::numeric_limits<std::uint64_t>::max();value.identity.volumeUUID="AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE";return value;}
struct Fake {
    struct Data {std::map<std::string,ShelfFileMetadata,std::less<>> imports,resolved;std::set<std::string,std::less<>> unavailable;unsigned acquired{},released{},active{},resolutions{},importsCalled{},ids{},times{};std::function<void()> duringImport;};
    std::shared_ptr<Data> data=std::make_shared<Data>();
    void add(unsigned n){auto value=metadata(n);data->imports.emplace(std::to_string(n),value);data->resolved.emplace(value.windowsPath,value);}
    static ShelfFileAccess lease(const std::shared_ptr<Data>& data,ShelfFileMetadata value){++data->acquired;++data->active;return {std::move(value),[data]{++data->released;--data->active;}};}
    FileShelfStore::Platform platform(){auto shared=data;return {[shared](std::string_view token){++shared->importsCalled;if(shared->duringImport)shared->duringImport();const auto i=shared->imports.find(token);if(i==shared->imports.end())throw StoreError(StoreErrorCode::unavailable,"Synthetic input is unavailable");return lease(shared,i->second);},
        [shared](const ShelfRecord& record){++shared->resolutions;const auto i=shared->resolved.find(record.windowsPath);if(i==shared->resolved.end()||shared->unavailable.contains(record.windowsPath))throw StoreError(StoreErrorCode::unavailable,"Synthetic reference is offline");return lease(shared,i->second);}};}
    FileShelfStore::Creation creation(){auto shared=data;return {[shared]{return uuid(++shared->ids);},[shared]{return 812345678.125+double(++shared->times);}};}
};
std::vector<std::string> tokens(std::initializer_list<const char*> values){std::vector<std::string> result;for(auto* value:values)result.emplace_back(value);return result;}
void identityAndLease(){
    auto a=metadata(1).identity,b=a;b.volumeSerial=1;check(a.matches(b),"Stable volume UUID wins over changed device/serial as in Mac source");b.volumeUUID.reset();check(!a.matches(b),"Missing stable volume UUID falls back to serial");b=a;(*b.volumeUUID)[0]='a';check(a.matches(b),"Native UUID spelling is case insensitive");b.objectID[1]=1;check(!a.matches(b),"Same path/volume cannot replace different object identity");
    Fake f;auto access=Fake::lease(f.data,metadata(1));check(access.open()&&f.data->active==1,"Injected lease owns acquired scope");auto moved=std::move(access);check(!access.open()&&moved.open()&&f.data->active==1,"Move transfers scope without release");moved.close();moved.close();check(f.data->active==0&&f.data->released==1,"Explicit repeated close balances once");
    {auto first=Fake::lease(f.data,metadata(1));auto second=Fake::lease(f.data,metadata(2));first=std::move(second);check(f.data->active==1&&!second.open(),"Move assignment releases prior scope first");}check(f.data->active==0&&f.data->acquired==f.data->released,"Destruction releases remaining scope");
}
void importsAndRoundtrip(){
    Temporary temp;Fake f;f.add(1);f.add(2);auto alias=f.data->imports.at("1");alias.windowsPath="D:\\another-name.txt";f.data->imports["alias"]=alias;
    FileShelfStore store(temp.root,f.platform(),f.creation());check(store.items().empty()&&!std::filesystem::exists(store.path())&&f.data->acquired==0,"Empty construction reads only metadata and creates no reference or archive");
    check(store.add(tokens({"1","alias","2","2"}))==2&&store.items().size()==2,"Whole batch deduplicates alias/hardlink identities and repeated inputs");
    check(f.data->importsCalled==4&&f.data->ids==2&&f.data->times==2&&f.data->active==0&&f.data->released==4,"Duplicate input scopes close; UUID/time allocated only for new identities");
    const auto bytes=read(store.path());const auto archive=Json::parse(bytes);const auto& item=archive["items"].array()[0];
    check(item["referencePlatform"].string()=="windows"&&item["windowsPath"].string()==metadata(1).windowsPath&&!item.contains("bookmark")&&!item.contains("lastKnownPath"),"Native locator never fabricates Mac bookmark fields");
    check(item["windowsIdentity"]["volumeSerial"].string()=="ffffffffffffffff"&&item["windowsIdentity"]["objectID"].string()=="010000000000000000000000000000fe","128-bit object and full unsigned serial persist losslessly");
    check(!item.contains("availabilityError")&&item["createdAt"].number()==812345679.125,"Source Foundation time and session-only availability contract");
    check(store.add(tokens({"alias"}))==0&&read(store.path())==bytes,"Duplicate-only batch performs no metadata rewrite");
    FileShelfStore reopened(temp.root,f.platform(),f.creation());check(reopened.items().size()==2&&reopened.items()[0].id==store.items()[0].id&&reopened.items()[0].identity==store.items()[0].identity,"Native metadata reopens without resolving live files");
    check(f.data->resolutions==0,"Open and dedupe never resolve existing references or read contents");
    auto lease=reopened.access(reopened.items()[0].id);check(lease.open()&&lease.metadata().windowsPath==metadata(1).windowsPath&&f.data->active==1,"Access returns verified caller-owned lease");lease.close();
    const auto savedInputs=f.data->imports;check(reopened.remove(store.items()[0].id)&&reopened.items().size()==1&&!reopened.remove(uuid(999)),"Remove commits only requested reference; missing ID is no-op");
    check(reopened.clear()&&reopened.items().empty()&&!reopened.clear(),"Clear commits reference archive once; empty clear is no-op");check(f.data->imports.size()==savedInputs.size()&&f.data->imports.at("1").windowsPath==savedInputs.at("1").windowsPath,"Reference remove/clear never call native delete or change originals");
}
void atomicImportAndPersistenceFailures(){
    Temporary temp;Fake f;for(unsigned i=1;i<=4;++i)f.add(i);FileShelfStore store(temp.root,f.platform(),f.creation());store.add(tokens({"1"}));const auto bytes=read(store.path());const auto before=store.items();
    rejects(StoreErrorCode::unavailable,[&]{store.add(tokens({"2","missing","3"}));},"One unavailable import rejects whole batch");check(store.items()==before&&read(store.path())==bytes&&f.data->active==0,"Failed batch preserves memory/disk and releases earlier scopes");
    f.data->imports["bad"]=metadata(4);f.data->imports["bad"].windowsPath="https://example.invalid/file";
    rejects(StoreErrorCode::invalid,[&]{store.add(tokens({"2","bad"}));},"Non-filesystem provider metadata rejects");check(store.items()==before&&read(store.path())==bytes&&f.data->active==0,"Invalid metadata closes even the rejected provider lease");
    f.data->imports["bad"].windowsPath=metadata(4).windowsPath;f.data->imports["bad"].kind=static_cast<ShelfFileKind>(99);rejects(StoreErrorCode::invalid,[&]{store.add(tokens({"bad"}));},"Device/unsupported object types never become shelf references");
    FileShelfStore stale(temp.root,f.platform(),f.creation());const auto staleBefore=stale.items();store.add(tokens({"2"}));const auto current=read(store.path());
    rejects(StoreErrorCode::changedOnDisk,[&]{stale.add(tokens({"3"}));},"Concurrent writer compare rejects stale add");check(stale.items()==staleBefore&&read(store.path())==current&&f.data->active==0,"Concurrent add failure preserves old in-memory and newer disk state");
    rejects(StoreErrorCode::changedOnDisk,[&]{stale.clear();},"Stale clear cannot erase newer references");check(stale.items()==staleBefore&&read(store.path())==current,"Failed clear preserves both sides");
    write(store.path(),"external corrupt bytes");rejects(StoreErrorCode::changedOnDisk,[&]{store.remove(store.items()[0].id);},"Unexpected external bytes prevent reference removal");check(read(store.path())=="external corrupt bytes"&&store.items().size()==2,"Corrupt external archive remains untouched");
    for(const auto& entry:std::filesystem::directory_iterator(store.path().parent_path()))check(entry.path().extension()!=".tmp","No abandoned temporary save after failure");
}
void refreshAvailabilityAndReplacement(){
    Temporary temp;Fake f;f.add(1);f.add(2);FileShelfStore store(temp.root,f.platform(),f.creation());store.add(tokens({"1","2"}));const auto initial=read(store.path());const auto a=store.items()[0].id,b=store.items()[1].id;
    f.data->unavailable.insert(metadata(1).windowsPath);store.refresh();check(store.items()[0].availabilityError=="Synthetic reference is offline"&&!store.items()[1].availabilityError,"Availability failures belong to individual session cards");check(read(store.path())==initial&&f.data->active==0,"Availability-only refresh never saves transient errors");
    {FileShelfStore reopened(temp.root,f.platform(),f.creation());check(!reopened.items()[0].availabilityError,"Session error is absent after reopen until next refresh");}
    f.data->unavailable.clear();store.refresh();check(!store.items()[0].availabilityError&&read(store.path())==initial,"Recovered reference clears session error without saving");
    auto replacement=metadata(1);replacement.identity.objectID[5]=12;f.data->resolved[metadata(1).windowsPath]=replacement;
    rejects(StoreErrorCode::unavailable,[&]{(void)store.access(a);},"Same-path different identity cannot be opened");check(f.data->active==0,"Rejected replacement scope closes");store.refresh();check(store.items()[0].availabilityError&&store.items()[0].identity==metadata(1).identity&&read(store.path())==initial,"Replacement refresh preserves original identity/path and archive");
    auto renamed=metadata(1);renamed.windowsPath="D:\\renamed-original.txt";renamed.name="renamed-original.txt";renamed.byteCount=999;f.data->resolved[metadata(1).windowsPath]=renamed;f.data->resolved[renamed.windowsPath]=renamed;
    f.data->unavailable.insert(metadata(2).windowsPath);store.refresh();check(store.items()[0].windowsPath==renamed.windowsPath&&store.items()[0].name==renamed.name&&!store.items()[0].availabilityError&&store.items()[1].availabilityError,"Rename metadata updates with original identity; other unavailable cards survive");
    const auto saved=Json::parse(read(store.path()));check(saved["items"].array()[0]["byteCount"].integer()==999&&!saved["items"].array()[1].contains("availabilityError"),"Metadata refresh saves atomically without session availability");
    check(store.items()[0].id==a&&store.items()[1].id==b&&store.items()[0].createdAt==812345679.125,"Refresh preserves reference UUID/time/order");
    FileShelfStore stale(temp.root,f.platform(),f.creation());store.remove(b);const auto stable=stale.items();renamed.name="changed-name.txt";f.data->resolved[renamed.windowsPath]=renamed;
    rejects(StoreErrorCode::changedOnDisk,[&]{stale.refresh();},"Metadata refresh persistence failure propagates");check(stale.items()==stable,"Failed refresh commit rolls back metadata and session availability together");
}
void copyingAndIndependentLeaseLifetime(){
    Temporary temp;Fake f;f.add(1);f.add(2);f.add(3);auto store=std::make_unique<FileShelfStore>(temp.root,f.platform(),f.creation());store->add(tokens({"1","2","3"}));const auto a=store->items()[0].id,b=store->items()[1].id,c=store->items()[2].id;
    const std::vector<std::string> selected{c,a,c,b};bool called=false;std::optional<ShelfCopyBundle> pending;
    store->copy(selected,[&](ShelfCopyBundle bundle){called=true;check(bundle.ids==std::vector<std::string>{a,b,c},"Copy bundle follows shelf order and deduplicates selection");check(bundle.operation==ShelfCopyBundle::Operation::copy&&bundle.accesses.size()==3,"Transfer operation is copy-only");pending=std::move(bundle);store.reset();});
    check(called&&!store&&f.data->active==3,"Receiver retains all verified leases for asynchronous transfer");store.reset();check(f.data->active==3&&pending->accesses[0].open(),"Closing shelf/store does not revoke native transfer references");pending.reset();check(f.data->active==0,"Receiver completion releases all leases independently");
    FileShelfStore reopened(temp.root,f.platform(),f.creation());f.data->unavailable.insert(metadata(2).windowsPath);called=false;
    rejects(StoreErrorCode::unavailable,[&]{reopened.copy(selected,[&](ShelfCopyBundle){called=true;});},"One unavailable outgoing reference prevents whole transfer");check(!called&&f.data->active==0,"No partial URLs escape and earlier outgoing leases close");
    f.data->unavailable.clear();fails([&]{reopened.copy(selected,[](ShelfCopyBundle){throw std::runtime_error("receiver canceled");});},"Receiver errors release handed-off temporary bundle");check(f.data->active==0,"Rejected copy receiver leaks no access");
    const std::vector<std::string> unknown{a,uuid(999)};const auto before=f.data->resolutions;rejects(StoreErrorCode::invalid,[&]{(void)reopened.prepareCopy(unknown);},"Unknown selected identity rejects before native resolution");check(f.data->resolutions==before,"Copy prevalidation opens no partial selection");
    called=false;reopened.copy({},[&](ShelfCopyBundle){called=true;});check(!called,"Empty selection does not launch copy action");
}
void archiveGuardsAndUnknownFields(){
    Temporary temp;Fake f;f.add(1);f.add(2);FileShelfStore store(temp.root,f.platform(),f.creation());store.add(tokens({"1"}));auto archive=Json::parse(read(store.path()));
    archive["futureEnvelope"]=Json::parse("18446744073709551615");auto array=archive["items"].array();auto row=array[0];row["futureField"]="preserve";row["windowsIdentity"]["futureIdentity"]=42;row["createdAt"]=Json::parse("812345679.12500000");array[0]=row;archive["items"]=array;write(store.path(),archive.encode());
    FileShelfStore imported(temp.root,f.platform(),f.creation());imported.add(tokens({"2"}));const auto roundtrip=Json::parse(read(store.path()));check(roundtrip["futureEnvelope"].encode()=="18446744073709551615"&&roundtrip["items"].array()[0]["futureField"].string()=="preserve"&&roundtrip["items"].array()[0]["windowsIdentity"]["futureIdentity"].integer()==42,"Unknown additive envelope/record/identity fields survive changes");check(roundtrip["items"].array()[0]["createdAt"].encode()=="812345679.12500000","Unchanged Foundation Date token remains exact");
    const auto valid=roundtrip;auto invalid=valid;invalid["version"]=2;write(store.path(),invalid.encode());rejects(StoreErrorCode::newerVersion,[&]{FileShelfStore future(temp.root,f.platform());},"Newer archive rejects without rewrite");check(read(store.path())==invalid.encode(),"Newer archive preserved");
    invalid=valid;array=invalid["items"].array();array.push_back(array[0]);invalid["items"]=array;write(store.path(),invalid.encode());rejects(StoreErrorCode::invalid,[&]{FileShelfStore duplicate(temp.root,f.platform());},"Duplicate UUID archive rejects");
    invalid=valid;array=invalid["items"].array();row=array[0];row["bookmark"]="AQID";array[0]=row;invalid["items"]=array;write(store.path(),invalid.encode());rejects(StoreErrorCode::invalid,[&]{FileShelfStore mixed(temp.root,f.platform());},"Mixed native/Mac bookmark locator rejects");check(read(store.path())==invalid.encode(),"Rejected mixed archive preserved");
    write(store.path(),R"({"version":1,"items":[{"id":"00000000-0000-4000-8000-000000000001","bookmark":"AQID","lastKnownPath":"/synthetic/source","isSecurityScoped":true,"name":"source","typeDescription":"File","isDirectory":false,"createdAt":1}]})");const auto mac=read(store.path());rejects(StoreErrorCode::invalid,[&]{FileShelfStore foreign(temp.root,f.platform());},"Native store never interprets a Mac security bookmark");check(read(store.path())==mac,"Foreign Mac bookmark archive remains byte-identical");
    write(store.path(),std::string(FileShelfStore::maximumArchiveBytes+1,'x'));rejects(StoreErrorCode::tooLarge,[&]{FileShelfStore huge(temp.root,f.platform());},"Archive size bound rejects before parsing");
    rejects(StoreErrorCode::invalid,[&]{FileShelfStore relative("relative",f.platform());},"No implicit or relative live-data root");
    Temporary large;f.data->imports["huge"]=metadata(3);f.data->imports["huge"].typeDescription=std::string(FileShelfStore::maximumArchiveBytes,'x');FileShelfStore bounded(large.root,f.platform(),f.creation());rejects(StoreErrorCode::tooLarge,[&]{bounded.add(tokens({"huge"}));},"Oversized metadata rejects before first save");check(bounded.items().empty()&&!std::filesystem::exists(bounded.path())&&f.data->active==0,"Oversized batch has no published archive/rows/leases");
}
void adapterGuard(){
    Temporary temp;Fake f;f.add(1);FileShelfStore store(temp.root,f.platform(),f.creation());f.data->duringImport=[&]{store.clear();};
    rejects(StoreErrorCode::invalid,[&]{store.add(tokens({"1"}));},"Provider cannot reenter store mutation");check(store.items().empty()&&!std::filesystem::exists(store.path()),"Reentrant provider leaves store untouched");
    f.data->duringImport={};auto bad=f.platform();bad.acquireImport=[](std::string_view){return ShelfFileAccess{};};FileShelfStore closed(temp.root,bad,f.creation());rejects(StoreErrorCode::invalid,[&]{closed.add(tokens({"1"}));},"Provider must transfer an actual open lease");
    auto repeated=f.creation();repeated.id=[] {return uuid(1);};FileShelfStore duplicates(temp.root,f.platform(),repeated);f.add(2);rejects(StoreErrorCode::invalid,[&]{duplicates.add(tokens({"1","2"}));},"Duplicate injected UUID aborts whole batch");check(duplicates.items().empty()&&f.data->active==0,"Bad identity provider releases scopes and publishes no rows");
    Temporary rootLink;const auto target=rootLink.root/"target",link=rootLink.root/"linked";std::filesystem::create_directory(target);std::error_code error;std::filesystem::create_directory_symlink(target,link,error);
    if(!error)rejects(StoreErrorCode::invalid,[&]{FileShelfStore linked(link,f.platform());},"Explicit data root refuses symlink/reparse redirection");
}
}
int main(){try{identityAndLease();importsAndRoundtrip();atomicImportAndPersistenceFailures();refreshAvailabilityAndReplacement();copyingAndIndependentLeaseLifetime();archiveGuardsAndUnknownFields();adapterGuard();std::cout<<"File shelf store: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"File shelf store failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
