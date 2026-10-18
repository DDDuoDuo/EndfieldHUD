#include "app/settings_save_queue.hpp"
#include "core/data/file_io.hpp"
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

namespace app=endfield::app;namespace data=ehud::data;
namespace {
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
template<class F>void rejects(F f){bool threw{};try{f();}catch(const std::exception&){threw=true;}check(threw,"Invalid queue lifecycle rejects before IO");}
struct Temporary {
    std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("Endfield-settings-save-"+data::makeUUID());
    ~Temporary(){std::error_code e;std::filesystem::remove_all(root,e);}
};
void write(const std::filesystem::path&p,std::string_view s){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(s.data(),static_cast<std::streamsize>(s.size()));check(bool(f),"Write only owned temporary fixture");}
void readAndCoalesce(){
    Temporary t;unsigned loaded{},changed{},otherCompletions{};data::Settings initial;
    app::UtilityExecutor executor([]{},1);const auto owner=std::this_thread::get_id();
    app::SettingsSaveQueue queue(t.root,executor,{[&](const auto&s){check(std::this_thread::get_id()==owner,"Load callback stays on owner thread");initial=s;++loaded;},[&]{++changed;}});
    check(!std::filesystem::exists(t.root)&&!executor.stats().started&&executor.stats().accepted==0,"Construction has no filesystem/worker effects");
    rejects([&]{queue.save(data::Settings::defaults());});
    const auto route=executor.makeRoute();check(executor.submit(route,[]{},[&](auto){++otherCompletions;}),"Another module occupies shared capacity");executor.waitIdle();
    queue.start();check(!queue.status().loaded&&!queue.status().busy&&executor.stats().accepted==1,"Queue-full load is retained without a retry timer");
    executor.drain();queue.queueCapacityAvailable();executor.waitIdle();executor.drain();
    check(loaded==1&&changed==1&&queue.status().loaded&&!queue.status().dirty&&otherCompletions==1,"Explicit shared drain admits the initial load once");
    check(!std::filesystem::exists(t.root),"Loading defaults creates no data file or directory");
    auto setting=initial;setting.set("futureSetting",data::Json::Object{{"unchanged","opaque"}});setting.set("closeOnFocusLost",false);
    check(executor.submit(route,[]{},[](auto){}),"Shared capacity can be occupied after loading");executor.waitIdle();
    for(unsigned n=0;n<128;++n){setting.set("hudScale",.5+double(n)/100);queue.save(setting);}
    check(queue.status().dirty&&!queue.status().busy&&queue.status().revision==128,"One latest immutable value coalesces an entire saturated preference burst");
    check(queue.flush()&&!queue.status().dirty&&queue.status().savedRevision==128,"Shutdown barrier admits and saves the latest value");
    check(executor.stats().accepted==4,"Burst uses exactly one accepted save, without per-change jobs");
    data::SettingsStore stored(t.root);check(stored.value()==setting,"Latest full settings and unknown data are persisted exactly");
    const auto accepted=executor.stats().accepted;queue.save(setting);queue.queueCapacityAvailable();check(executor.stats().accepted==accepted,"Unchanged preferences admit no work");
    rejects([&]{app::SettingsSaveQueue invalid(std::filesystem::path("relative"),executor);});
    bool wrongThread{};std::thread outsider([&]{try{queue.status();}catch(const std::logic_error&){wrongThread=true;}});outsider.join();check(wrongThread,"Owner-only queue access rejects cross-thread use");
}
void failureAndPreservation(){
    Temporary t;check(std::filesystem::create_directory(t.root),"Create synthetic saved-settings root");
    auto setting=data::Settings::defaults();setting.set("futurePrivatePreference",data::Json::Object{{"value",42}});
    data::SettingsStore seeded(t.root);seeded.update(setting);
    app::UtilityExecutor executor([]{},2);unsigned loaded{};app::SettingsSaveQueue queue(t.root,executor,{[&](const auto&s){setting=s;++loaded;},{}});
    check(queue.flush()&&loaded==1&&setting.fields["futurePrivatePreference"]["value"].integer()==42,"Initial worker load preserves additive unknown preferences");
    const auto original=data::detail::readFile(t.root/"settings.json",4*1024*1024);check(bool(original),"Retain exact initial synthetic bytes");
    write(t.root/"settings.json","external fixture modification");setting.set("closeOnFocusLost",false);queue.save(setting);executor.waitIdle();executor.drain();
    check(queue.status().failed&&queue.status().dirty&&queue.status().error&&!queue.status().busy,"Changed-on-disk failure retains the dirty latest record");
    const auto attempts=executor.stats().accepted;for(unsigned n=0;n<100;++n)queue.queueCapacityAvailable();check(executor.stats().accepted==attempts,"Shared completion notifications never auto-retry failed IO");
    check(!queue.flush()&&executor.stats().accepted==attempts+1,"Explicit failed flush attempts once and returns, without a retry loop");
    check(data::detail::readFile(t.root/"settings.json",4*1024*1024)==std::optional<std::string>("external fixture modification"),"Conflicting external contents remain byte-for-byte untouched");
    write(t.root/"settings.json",*original);queue.retry();executor.waitIdle();executor.drain();
    check(!queue.status().failed&&!queue.status().dirty&&!queue.status().error&&data::SettingsStore(t.root).value()==setting,"Explicit retry saves retained data after the caller resolves the conflict");
    auto invalid=setting;invalid.set("closeOnFocusLost","wrong type");queue.save(invalid);executor.waitIdle();executor.drain();check(queue.status().failed&&queue.status().dirty,"Invalid record is retained and reported, never silently normalized");
    setting.set("ambientAnimation",false);queue.save(setting);check(queue.flush()&&data::SettingsStore(t.root).value()==setting,"A changed valid preference supersedes a failed pending value");
    Temporary corrupt;check(std::filesystem::create_directory(corrupt.root),"Create isolated corrupt archive root");write(corrupt.root/"settings.json","{broken");
    unsigned corruptLoaded{};app::SettingsSaveQueue broken(corrupt.root,executor,{[&](const auto&){++corruptLoaded;},{}});check(!broken.flush()&&broken.status().failed&&!broken.status().loaded&&!corruptLoaded,"Failed initial load never enables a default overwrite");rejects([&]{broken.save(data::Settings::defaults());});
    std::filesystem::remove(corrupt.root/"settings.json");broken.retry();executor.waitIdle();executor.drain();check(broken.status().loaded&&corruptLoaded==1,"Explicit repaired-load retry delivers the record once");
}
void acceptedLifetimeAndReentrancy(){
    Temporary t;app::UtilityExecutor executor([]{},2);unsigned callbacks{};
    auto queue=std::make_unique<app::SettingsSaveQueue>(t.root,executor,app::SettingsSaveCallbacks{{},[&]{++callbacks;}});check(queue->flush(),"Load lifetime fixture");
    auto setting=data::Settings::defaults();setting.set("closeOnFocusLost",false);
    auto gate=std::make_shared<std::promise<void>>();const auto wait=gate->get_future().share();const auto route=executor.makeRoute();
    check(executor.submit(route,[wait]{wait.wait();},[](auto){}),"Owned deterministic gate holds the existing worker");
    struct OpenGate{std::shared_ptr<std::promise<void>>value;~OpenGate(){if(value)try{value->set_value();}catch(...){}}}open{gate};
    queue->save(setting);check(queue->status().busy,"Latest settings write is accepted behind shared work");const auto before=callbacks;queue.reset();gate->set_value();open.value.reset();executor.waitIdle();executor.drain();
    check(callbacks==before&&data::SettingsStore(t.root).value()==setting,"Accepted write outlives UI binding without calling a dead observer");
    Temporary reentrant;std::unique_ptr<app::SettingsSaveQueue>destroyed;
    destroyed=std::make_unique<app::SettingsSaveQueue>(reentrant.root,executor,app::SettingsSaveCallbacks{[&](const auto&){destroyed.reset();},{}});destroyed->start();executor.waitIdle();executor.drain();check(!destroyed,"Load callback can destroy its binding safely");
    Temporary savedDuringLoad;std::unique_ptr<app::SettingsSaveQueue>reentered;
    reentered=std::make_unique<app::SettingsSaveQueue>(savedDuringLoad.root,executor,app::SettingsSaveCallbacks{[&](const auto&loaded){auto next=loaded;next.set("closeOnFocusLost",false);reentered->save(next);},{}});
    check(reentered->flush()&&!data::SettingsStore(savedDuringLoad.root).value().boolean("closeOnFocusLost"),"Loaded callback can enqueue an explicit preference without losing its revision");
}
void inFlightCoalescing(){
    Temporary t;app::UtilityExecutor executor([]{},2);app::SettingsSaveQueue queue(t.root,executor);check(queue.flush(),"Load in-flight fixture");
    const auto route=executor.makeRoute();auto gate=std::make_shared<std::promise<void>>();const auto wait=gate->get_future().share();
    check(executor.submit(route,[wait]{wait.wait();},[](auto){}),"Deterministic shared-worker gate");
    struct Release{std::shared_ptr<std::promise<void>>p;~Release(){if(p)try{p->set_value();}catch(...){}}}release{gate};
    auto value=data::Settings::defaults();value.set("closeOnFocusLost","invalid first value");queue.save(value);const auto firstRevision=queue.status().revision;
    for(unsigned n=0;n<80;++n){value.set("closeOnFocusLost",false);value.set("hudScale",.5+double(n)/100);queue.save(value);}
    check(queue.status().busy&&queue.status().revision==firstRevision+80&&executor.stats().accepted==3,"One accepted value plus one latest value bounds an in-flight burst");
    gate->set_value();release.p.reset();check(queue.flush(),"Newer valid value proceeds after stale accepted failure");
    check(!queue.status().failed&&!queue.status().error&&executor.stats().accepted==4&&data::SettingsStore(t.root).value()==value,"Stale failure does not poison the latest saved revision or enqueue intermediate values");
    auto restored=data::Settings::defaults();queue.save(restored);check(queue.flush()&&data::SettingsStore(t.root).value()==restored,"Returning to a previous value remains a real ordered save");
}
}
int main(){try{readAndCoalesce();failureAndPreservation();acceptedLifetimeAndReentrancy();inFlightCoalescing();std::cout<<"Settings save queue: "<<checks<<" checks passed; temporary data only\n";return 0;}catch(const std::exception&e){std::cerr<<"Settings save queue after "<<checks<<": "<<e.what()<<'\n';return 1;}}
