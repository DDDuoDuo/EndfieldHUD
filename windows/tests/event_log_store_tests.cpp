#include "core/data/event_log_store.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>

namespace data=ehud::data;namespace model=endfield::modules;using Json=data::Json;
namespace {
std::size_t checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&&f,const char*why){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
struct Temporary {
    std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("Endfield-event-log-fixture-"+data::makeUUID());
    Temporary(){check(std::filesystem::create_directory(root),"Create explicit new synthetic root");}
    ~Temporary(){std::error_code error;std::filesystem::remove_all(root,error);}
    std::filesystem::path path()const{return root/"EventLog"/"events.json";}
};
std::string id(unsigned value){const auto suffix=std::to_string(value);return "00000000-0000-4000-8000-"+std::string(12-suffix.size(),'0')+suffix;}
model::SystemEvent event(unsigned n){return {id(n),model::EventKind::clipboardCopied,800000000.+n,{{"kind","text"},{"body","PRIVATE_BODY"},{"url","PRIVATE_URL"}}};}
void write(const std::filesystem::path&path,const std::string&bytes){std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(out),"Write only injected malformed/archive fixture");}
std::string read(const std::filesystem::path&path){std::ifstream in(path,std::ios::binary);check(bool(in),"Read only injected archive fixture");return {std::istreambuf_iterator<char>(in),{}};}
Json row(unsigned n){auto e=event(n);Json::Object metadata;metadata.emplace("kind","text");metadata.emplace("body","PRIVATE_BODY");return Json::Object{{"id",e.id},{"kind","clipboardCopied"},{"createdAt",e.createdAt},{"metadata",std::move(metadata)}};}
void memory(){
    data::EventLogStore store(std::nullopt,2);check(!store.path()&&!store.saveDeadline()&&!store.statusMessage(),"Memory-only source mode starts no persistence");
    for(unsigned n=1;n<=4;++n)store.record(event(n),n);
    check(store.capacity()==2&&store.events().size()==2&&store.events().front().id==id(4),"Memory-only model retains bounded newest-first events");
    check(!store.takeSave(4.4,true)&&!store.saveDeadline(),"Explicit memory-only flush has no write request");
    store.clear(5);const auto before=store.revision();store.clear(6);check(store.revision()==before+1&&store.events().empty(),"Empty clear changes source revision without recording itself");
    const auto stable=store.revision();rejects([&]{store.record(event(7),5.9);},"Backwards owner time rejects before model mutation");rejects([&]{store.clear(std::numeric_limits<double>::quiet_NaN());},"Nonfinite owner time rejects");check(store.revision()==stable,"Invalid owner clock preserves session model");
}
void save(){
    Temporary fixture;data::EventLogStore store(fixture.root,3);check(store.path()==fixture.path(),"Original relative EventLog/events.json path is preserved");
    store.record(event(1),1);check(store.saveDeadline()&&std::abs(*store.saveDeadline()-1.35)<1e-12,"First event requests original .35-second owner deadline");
    check(!store.takeSave(1.1)&&!std::filesystem::exists(fixture.path()),"Before deadline no snapshot or file write occurs");
    store.record(event(2),1.2);check(std::abs(*store.saveDeadline()-1.55)<1e-12,"Second event replaces the one pending deadline");
    auto request=store.takeSave(1.55);check(request&&request->revision()==2&&!store.saveDeadline(),"Due snapshot is revisioned and consumes its deadline");
    check(!std::filesystem::exists(fixture.path())&&request->bytes().find("PRIVATE")==std::string_view::npos,"Snapshot preparation performs no I/O and retains no disallowed metadata");
    check(!store.takeSave(1.56),"Unchanged frame/owner checks cannot generate repeated requests");
    const auto result=request->execute();check(result.success&&result.revision==2&&!store.complete(result),"Atomic worker save succeeds without spurious status notification");
    const auto document=Json::parse(read(fixture.path()));check(document["version"].integer()==1&&document["events"].array().size()==2,"Saved document preserves source v1 envelope");
    check(document["events"].array()[0]["createdAt"].number()==800000002.&&document["events"].array()[0]["metadata"].object().size()==1,"Source Date epoch and sanitized metadata survive persistence");
    data::EventLogStore loaded(fixture.root,3);check(loaded.events().size()==2&&loaded.events()[0].id==id(2)&&!loaded.statusMessage(),"Saved log reloads through source model");
    store.clear(2);auto clear=store.takeSave(2,true);check(clear&&clear->execute().success,"Explicit clear writes an empty source document");
    store.clear(3);auto emptyClear=store.takeSave(3,true);check(emptyClear&&emptyClear->revision()==4&&emptyClear->execute().success,"Repeated empty clear remains an explicit persisted operation");
    check(Json::parse(read(fixture.path()))["events"].array().empty(),"Clearing never appends its own event");
}
void loading(){
    Temporary fixture;Json::Array rows;for(unsigned n=1;n<=510;++n)rows.push_back(row(n));
    auto duplicate=row(1);duplicate["createdAt"]=900000000.;rows.push_back(std::move(duplicate));
    write(fixture.path(),Json(Json::Object{{"version",1},{"events",rows}}).encode());data::EventLogStore store(fixture.root);
    check(!store.statusMessage()&&store.events().size()==500&&store.events().front().id==id(510)&&store.events().back().id==id(11),"Load validates all rows, drops later UUID duplicates, sorts and then caps at500");
    for(const auto&e:store.events())check(e.metadata==model::EventMetadata{{"kind","text"}},"Loaded legacy private fields are sanitized before session display");
    Temporary lower;auto a=row(1);a["id"]="aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";auto b=a;b["id"]="AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE";
    write(lower.path(),Json(Json::Object{{"version",1},{"events",Json::Array{a,b}}}).encode());data::EventLogStore canonical(lower.root);check(canonical.events().size()==1&&canonical.events()[0].id=="AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE","UUID deduplication is canonical across letter case");
}
void blocked(){
    const std::array<std::string,5>archives{"not JSON",R"({"version":0,"events":[]})",R"({"version":2,"events":[{"kind":"futureEnum"}]})",R"({"version":1,"events":[{}]})",std::string(data::EventLogStore::maximumArchiveBytes+1,' ')};
    for(std::size_t n=0;n<archives.size();++n){Temporary fixture;write(fixture.path(),archives[n]);data::EventLogStore store(fixture.root);
        check(store.events().empty()&&store.statusMessage()&&store.statusMessage()->find(n==2?"newer format":"could not be read")!=std::string_view::npos,"Malformed/newer archive reports source status without decoding future enum");
        store.record(event(1),1);store.clear(2);auto request=store.takeSave(2,true);check(request&&!request->execute().success,"Blocked archive can keep session changes but cannot be overwritten");
        check(!store.complete({store.revision(),true})&&store.statusMessage(),"Save result cannot erase a protected load/version failure");check(read(fixture.path())==archives[n],"Original unreadable/newer bytes remain exact");
    }
    Temporary unknown;auto unknownRow=row(1);unknownRow["kind"]="futureEnum";write(unknown.path(),Json(Json::Object{{"version",1},{"events",Json::Array{unknownRow}}}).encode());data::EventLogStore unknownStore(unknown.root);check(unknownStore.statusMessage()&&unknownStore.events().empty(),"Unknown kind under current version blocks whole source archive");
}
void revisionsAndFailure(){
    Temporary fixture;data::EventLogStore store(fixture.root);store.record(event(1),1);auto old=store.takeSave(1,true);store.record(event(2),2);auto newer=store.takeSave(2,true);
    check(old&&newer&&newer->execute().success&&old->execute().success,"Writer serializes snapshots and treats an older token as already superseded");
    check(Json::parse(read(fixture.path()))["events"].array().size()==2,"Out-of-order worker delivery never rewinds newer persisted events");
    check(!store.complete({1,false})&&!store.statusMessage(),"Stale failed completion does not mark the latest session unsaved");
    check(store.complete({2,false})&&store.statusMessage()&&!store.complete({2,false}),"Current save failure updates status exactly once");
    check(store.complete({2,true})&&!store.statusMessage(),"Current successful retry clears save-only failure");
    Temporary blockedDirectory;data::EventLogStore failed(blockedDirectory.root);failed.record(event(1),1);auto request=failed.takeSave(1,true);
    write(blockedDirectory.root/"EventLog","occupied synthetic path");const auto fail=request->execute();check(!fail.success&&failed.complete(fail)&&failed.events().size()==1,"I/O failure preserves in-memory event and exposes save status");
    std::filesystem::remove(blockedDirectory.root/"EventLog");const auto retry=request->execute();check(retry.success&&failed.complete(retry)&&!failed.statusMessage(),"Same immutable request can retry after a transient save failure");
    const auto external=R"({"version":1,"events":[],"external":"preserve"})";write(fixture.path(),external);store.record(event(3),3);const auto conflicting=store.takeSave(3,true);const auto conflict=conflicting->execute();
    check(!conflict.success&&store.complete(conflict)&&read(fixture.path())==external,"Atomic expected-byte check preserves external replacement instead of overwriting it");
    Temporary late;std::optional<data::EventLogWrite>owned;
    {data::EventLogStore ephemeral(late.root);ephemeral.record(event(4),1);owned=ephemeral.takeSave(1,true);}
    check(owned&&owned->execute().success&&Json::parse(read(late.path()))["events"].array().size()==1,"Detached save snapshot safely outlives the UI/store without retaining either");
}
}
int main(){try{memory();save();loading();blocked();revisionsAndFailure();std::cout<<"Event Log persistence: "<<checks<<" checks passed (temporary roots only)\n";return 0;}catch(const std::exception&error){std::cerr<<"Event Log persistence failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
