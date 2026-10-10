#include "modules/profile_service.hpp"
#include "modules/work_mode_profile_hours.hpp"
#include "native/profile_text.hpp"
#include "core/data/file_io.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace m=endfield::modules;namespace n=endfield::native;namespace app=endfield::app;namespace data=ehud::data;
namespace {
unsigned checks{};
void check(bool v,const std::string&why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char*why){bool threw{};try{f();}catch(const std::exception&){threw=true;}check(threw,why);}
struct Temporary {
    std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("Endfield-profile-service-"+data::makeUUID());
    ~Temporary(){std::error_code e;std::filesystem::remove_all(root,e);}
};
std::string read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void write(const std::filesystem::path&p,std::string_view s){std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(s.data(),static_cast<std::streamsize>(s.size()));check(bool(f),"Write owned fixture");}
data::Json saved(const Temporary&t){return data::Json::parse(read(t.root/"Profile"/"profile.json"));}
// Another instance holding the store's advisory lock: a transient save failure.
struct HeldLock {
#ifdef _WIN32
    HANDLE handle{INVALID_HANDLE_VALUE};
    explicit HeldLock(const std::filesystem::path&p){handle=CreateFileW((p.wstring()+L".lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);check(handle!=INVALID_HANDLE_VALUE,"Hold profile lock");}
    ~HeldLock(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
#else
    int fd{-1};
    explicit HeldLock(const std::filesystem::path&p){fd=::open((p.string()+".lock").c_str(),O_RDWR|O_CREAT,0600);check(fd>=0&&::flock(fd,LOCK_EX|LOCK_NB)==0,"Hold profile lock");}
    ~HeldLock(){if(fd>=0){::flock(fd,LOCK_UN);::close(fd);}}
#endif
};
struct Owner {
    Temporary temporary;app::UtilityExecutor executor{[]{},4};
    std::optional<m::PersonalProfile>loaded;unsigned loads{},changes{},saveFailures{};std::optional<m::ProfileFailure>loadFailure,saveFailure;
    std::unique_ptr<m::ProfileService>service;std::unique_ptr<m::ProfileState>state;bool locked{};double live{};
    m::ProfileTextRules text=n::nativeProfileTextRules();m::ProfileDateRules dates=n::nativeProfileDateRules(u"UTC");
    Owner(){make();}
    void make(){
        service=std::make_unique<m::ProfileService>(temporary.root,executor,m::ProfileServiceCallbacks{
            [this](const m::PersonalProfile&p){loaded=p;++loads;},
            [this](m::ProfileFailure f,std::string){loadFailure=f;},
            [this](m::ProfileFailure f,std::string detail){saveFailure=f;++saveFailures;if(state)state->persistenceFailed(f,detail);},
            [this]{++changes;}});
    }
    void open(){
        service->start();check(service->flush()&&loaded,"Worker load completes on the owner thread");
        state=std::make_unique<m::ProfileState>(*loaded,text,dates,service->persistence([this]{return locked;},[this]{return live;}));
    }
    void settle(){executor.waitIdle();executor.drain();}
};
m::PersonalProfile seed(const Temporary&t,double work){
    data::ProfileStore store(t.root);auto p=store.value();p.uid="1000000000";
    // A fresh store invented its own local UID; replace the file with a fixed one.
    auto record=data::Json::parse(read(store.path()));record["profile"]["uid"]="1000000000";record["profile"]["accumulatedWorkSeconds"]=work;
    record["profile"]["futureField"]="opaque";record["futureEnvelope"]=data::Json::Object{{"kept",true}};
    write(store.path(),record.encode());return p;
}
void lifecycle(){
    Owner o;
    check(!std::filesystem::exists(o.temporary.root)&&!o.executor.stats().started,"Construction performs no filesystem or worker work");
    rejects([&]{o.service->accept(m::PersonalProfile{},false);},"Edits wait for the worker load");
    rejects([]{app::UtilityExecutor e([]{},1);m::ProfileService invalid(std::filesystem::path("relative"),e);},"Explicit absolute root only");
    o.open();
    check(o.loads==1&&std::filesystem::exists(o.temporary.root/"Profile"/"profile.json"),"First launch commits a new local identity like UserProfileStore");
    check(o.loaded->uid.size()==10&&o.loaded->accumulatedWorkSeconds==0,"New identity starts with no lifetime hours");
    bool wrongThread{};std::thread outsider([&]{try{(void)o.service->status();}catch(const std::logic_error&){wrongThread=true;}});outsider.join();
    check(wrongThread,"Owner-only service access");
}
void editsAndCoalescing(){
    Owner o;seed(o.temporary,1000);o.open();auto&s=*o.state;
    check(s.profile().accumulatedWorkSeconds==1000,"Stored lifetime loads");
    check(s.commit(m::ProfileField::name,"  管理员😀 ")&&o.service->flush(),"Local edit saves on the worker");
    auto file=saved(o.temporary);
    check(file["profile"]["name"].string()=="管理员😀"&&file["profile"]["futureField"].string()=="opaque"&&file["futureEnvelope"]["kept"].boolean(),"Unknown Mac fields survive an edit");
    const auto route=o.executor.makeRoute();check(o.executor.submit(route,[]{},[](auto){}),"Another module occupies the worker");
    const auto before=o.executor.stats().accepted;
    for(int i=0;i<50;++i)check(s.commit(m::ProfileField::introduction,"Draft "+std::to_string(i)),"Optimistic edit accepted");
    check(o.service->status().dirty&&o.service->status().revision>=50,"Edits coalesce while the worker is busy");
    check(o.service->flush()&&saved(o.temporary)["profile"]["introduction"].string()=="Draft 49","Latest edit persisted");
    check(o.executor.stats().accepted-before<=3,"A burst becomes at most one queued save after the busy job");
    const auto accepted=o.executor.stats().accepted;check(s.commit(m::ProfileField::introduction,"Draft 49")&&o.executor.stats().accepted==accepted,"Unchanged edit performs no IO");
}
void syncLocks(){
    Owner o;seed(o.temporary,0);o.open();auto&s=*o.state;
    o.locked=true;s.setSyncLocked(true);
    check(!s.commit(m::ProfileField::name,"Manual"),"Locked identity is not editable");
    check(s.commit(m::ProfileField::birthday,"2/29"),"Birthday stays editable while synced");
    auto synced=s.profile();synced.name="Official";synced.tag="5678";synced.gamePlayerID="1234567890123456789";synced.playerIDOverride.reset();
    o.service->acceptFromGame(synced,true);s.refresh(synced,true);
    check(s.commit(m::ProfileField::introduction,"Edited after sync"),"Local edit coalesces after the account record");
    check(o.service->flush(),"Coalesced account+local record is written as an account update");
    const auto file=saved(o.temporary)["profile"];
    check(file["name"].string()=="Official"&&file["gamePlayerID"].string()=="1234567890123456789"&&file["uid"].string()=="1000000000"&&!file.contains("playerIDOverride"),"Synced identity keeps the original local UID");
    check(file["birthdayMonth"].integer()==2&&file["birthdayDay"].integer()==29&&file["introduction"].string()=="Edited after sync","Unlocked fields persist while synced");
    check(s.profile().displayedUID()=="1234567890123456789","Display priority override ?? game ?? uid");
}
void images(){
    Owner o;seed(o.temporary,0);o.open();auto&s=*o.state;
    const auto first=data::makeUUID()+".image",second=data::makeUUID()+".image",background=data::makeUUID()+".png";
    for(const auto&name:{first,second,background})write(o.service->imagePath(name),"synthetic bytes");
    s.beginImageImport();check(s.imageImported(m::ProfileImageKind::avatar,first)&&o.service->flush(),"Avatar import commits");
    s.beginImageImport();check(s.imageImported(m::ProfileImageKind::avatar,second)&&s.imageImported(m::ProfileImageKind::background,background)&&o.service->flush(),"Replacement commits");
    check(!std::filesystem::exists(o.service->imagePath(first))&&std::filesystem::exists(o.service->imagePath(second))&&std::filesystem::exists(o.service->imagePath(background)),"Replaced avatar file is deleted after its replacement commits");
    check(s.restoreImage(m::ProfileImageKind::avatar)&&s.restoreImage(m::ProfileImageKind::background)&&o.service->flush(),"Defaults restore");
    check(!std::filesystem::exists(o.service->imagePath(second))&&!std::filesystem::exists(o.service->imagePath(background)),"Removed managed images are deleted");
    rejects([&]{(void)o.service->imagePath("../escape.png");},"Managed image names only");
}
void workHours(){
    Owner o;seed(o.temporary,1000);o.open();auto&s=*o.state;m::WorkModeProfileHours hours;double now{};
    m::WorkModeController controller({{},[&](double total){hours.checkpoint(total);}});
    hours.profileLoaded(controller,s.profile().accumulatedWorkSeconds,[&](double v){return s.setWorkSeconds(v);});
    o.live=0;auto liveTotal=[&]{return hours.totalSeconds(controller,now);};
    check(hours.restored()&&liveTotal()==1000,"Idle controller restores the stored lifetime once");
    controller.chooseStopwatch(now);controller.start(now);now=60;
    o.live=liveTotal();check(o.live==1060&&s.formattedHours()=="0.29","Profile page shows max(stored, live)");
    const auto jobs=o.executor.stats().accepted;
    controller.pause(now);check(o.service->flush()&&saved(o.temporary)["profile"]["accumulatedWorkSeconds"].number()==1060,"Pause persists the absolute total");
    controller.setVisible(false,now);controller.setVisible(true,now);check(o.service->flush()&&o.executor.stats().accepted==jobs+1,"A repeated checkpoint of the same total writes nothing");
    controller.resume(now);now=100;
    {HeldLock lock(o.temporary.root/"Profile"/"profile.json");controller.setVisible(false,now);o.settle();}
    check(o.saveFailures==1&&o.service->status().failed&&o.service->status().dirty&&s.error(),"A failed checkpoint is reported once and stays dirty");
    check(saved(o.temporary)["profile"]["accumulatedWorkSeconds"].number()==1060,"The failed write left the record untouched");
    controller.setVisible(true,now);now=130;controller.shutdown(now);
    check(o.service->flush()&&saved(o.temporary)["profile"]["accumulatedWorkSeconds"].number()==1130,"The next checkpoint retries the absolute total without double counting");
    check(!hours.unsaved()&&s.profile().accumulatedWorkSeconds==1130,"Quit persisted the final total");
    hours.checkpoint(5);check(hours.unsaved()==5.,"A lower total is rejected and reported as unsaved");
    check(saved(o.temporary)["profile"]["accumulatedWorkSeconds"].number()==1130,"Lower totals never reach the file");
    rejects([&]{hours.profileLoaded(controller,1,{});},"Restore happens once");
}
void lateLoad(){
    Owner o;seed(o.temporary,500);double now{};m::WorkModeProfileHours hours;
    m::WorkModeController controller({{},[&](double total){hours.checkpoint(total);}});
    controller.chooseStopwatch(now);controller.start(now);now=40;controller.pause(now);
    check(hours.unsaved()==40.,"Checkpoints before the profile loads are retained, not written");
    o.open();auto&s=*o.state;
    hours.profileLoaded(controller,s.profile().accumulatedWorkSeconds,[&](double v){return s.setWorkSeconds(v);});
    check(!hours.restored()&&hours.offset()==500&&hours.totalSeconds(controller,now)==540,"Time tracked before loading is added once above the stored lifetime");
    check(o.service->flush()&&saved(o.temporary)["profile"]["accumulatedWorkSeconds"].number()==540,"Early session persisted on load");
    controller.resume(now);now=50;controller.stop(now);check(o.service->flush()&&saved(o.temporary)["profile"]["accumulatedWorkSeconds"].number()==550,"Later checkpoints include the offset exactly once");
}
// The app's Work Mode owner exposes a read-only controller that started at
// zero: the stored lifetime becomes the offset, and retry() re-offers a total
// whose write was refused.
void readOnlyController(){
    Owner o;seed(o.temporary,900);double now{};m::WorkModeProfileHours hours;
    m::WorkModeController controller({{},[&](double total){hours.checkpoint(total);}});
    check(!hours.retry(),"Nothing can be retried before the profile loads");
    controller.chooseStopwatch(now);controller.start(now);now=30;controller.pause(now);
    o.open();auto&s=*o.state;bool refuse{};
    hours.profileLoaded(s.profile().accumulatedWorkSeconds,[&](double v){return !refuse&&s.setWorkSeconds(v);});
    check(!hours.restored()&&hours.offset()==900&&controller.trackedWorkSeconds(now)==30&&hours.totalSeconds(controller,now)==930,"Read-only controller: stored lifetime is a fixed offset");
    check(o.service->flush()&&saved(o.temporary)["profile"]["accumulatedWorkSeconds"].number()==930,"Early session persisted once on load");
    controller.resume(now);now=45;refuse=true;controller.pause(now);
    check(hours.unsaved()==945.&&saved(o.temporary)["profile"]["accumulatedWorkSeconds"].number()==930,"A refused checkpoint stays unsaved");
    refuse=false;check(hours.retry()&&!hours.unsaved()&&o.service->flush()&&saved(o.temporary)["profile"]["accumulatedWorkSeconds"].number()==945,"Flush retry persists the absolute total once");
    check(hours.retry(),"Nothing left to retry");
    rejects([&]{hours.profileLoaded(1,{});},"The offset is set once");
}
void loadFailures(){
    {
        Owner o;write(o.temporary.root/"Profile"/"profile.json",R"({"version":2,"profile":{}})");const auto bytes=read(o.temporary.root/"Profile"/"profile.json");
        o.service->start();check(!o.service->flush()&&o.loadFailure==m::ProfileFailure::newerVersion&&!o.loaded,"Newer record is not opened");
        check(read(o.temporary.root/"Profile"/"profile.json")==bytes,"Newer record preserved byte for byte");
        rejects([&]{o.service->accept(m::PersonalProfile{},false);},"No persistence after a failed load");
    }
    {
        Owner o;write(o.temporary.root/"Profile"/"profile.json","{not json");o.service->start();
        check(!o.service->flush()&&o.loadFailure==m::ProfileFailure::record,"Unreadable record shows the source invalid-record message");
        check(m::profileFailureMessage(*o.loadFailure,endfield::core::Language::english)=="The personal card could not be read. The saved data has been preserved.","Source message");
    }
}
}
int main(){
    try{lifecycle();editsAndCoalescing();syncLocks();images();workHours();lateLoad();readOnlyController();loadFailures();
        std::cout<<"Profile service: "<<checks<<" checks passed\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Profile service failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
