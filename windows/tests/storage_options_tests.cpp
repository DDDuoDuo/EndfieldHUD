#include "tools/storage_options.hpp"
#include "core/data/file_io.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <atomic>
#endif
namespace {
namespace t=endfield::tools;namespace m=endfield::modules;namespace d=ehud::data;using J=d::Json;
#ifdef _WIN32
namespace n=endfield::native;
#endif
unsigned checks{};void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>void rejects(F f){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,"Missing or modified packaged source data rejects");}
void equal(const J&a,const J&b){
 if(a.isNumber()&&b.isNumber()){check(a.number()==b.number(),"Original coordinate retained bit-for-bit as double");return;}
 if(a.isArray()&&b.isArray()){check(a.array().size()==b.array().size(),"Original command array size");for(std::size_t i=0;i<a.array().size();++i)equal(a.array()[i],b.array()[i]);return;}
 if(a.isObject()&&b.isObject()){check(a.object().size()==b.object().size(),"Original command object shape");for(const auto&[key,v]:a.object())equal(v,b[key]);return;}
 check(a==b,"Original command/metadata identity");
}
const J*named(const J&node,std::string_view name){if(node["name"].isString()&&node["name"].string()==name)return &node;for(const auto&child:node["children"].array())if(const auto*p=named(child,name))return p;return nullptr;}
void source(const m::StorageSourcePaths&paths,const std::filesystem::path&oracle){
 const auto bytes=d::detail::readFile(oracle,4*1024*1024);check(bytes.has_value(),"Explicit detached source oracle exists");const auto doc=J::parse(*bytes);
 check(!doc["filesystemQueries"].boolean()&&!doc["windowsCreated"].boolean()&&doc["cases"].array().size()==10,"Original oracle created no windows and queried no storage");
 for(const auto&row:doc["cases"].array()){
  const auto&root=row["root"];const auto*arrow=named(root,"storage.refresh.arrow"),*settings=named(root,"storage.settings.button"),*refresh=named(root,"storage.refresh.button");check(arrow&&settings&&refresh,"All exact source controls found");
  equal(paths.refreshArrow,(*arrow)["shape"]["path"]);equal(paths.settingsFeedback,(*settings)["children"].array()[1]["children"].array()[0]["shape"]["path"]);equal(paths.refreshFeedback,(*refresh)["children"].array()[1]["children"].array()[0]["shape"]["path"]);
 }
}
void resource(const std::filesystem::path&root){
 const auto paths=t::loadStorageSourcePaths(root);check(paths.refreshArrow.array().size()==16&&paths.settingsFeedback.array().size()==10&&paths.refreshFeedback.array().size()==10,"Only compact original arrow and two feedback paths are installed");
 m::StoragePresentation presentation;const auto artwork=m::prepareStorageArtwork(presentation,paths);
 check(presentation.actions().size()==2&&presentation.actions()[0].id=="storage:settings"&&presentation.actions()[1].id=="storage:refresh","Production paths add no details or scanner control");
 for(const auto&node:artwork.layers["children"].array())if(node["id"].string()=="storage.refresh.arrow")equal(node["shape"]["path"],paths.refreshArrow);
 const auto temporary=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-Storage-Options-"+d::makeUUID());
 struct Remove {std::filesystem::path path;~Remove(){std::error_code e;std::filesystem::remove_all(path,e);}}cleanup{temporary};std::filesystem::create_directory(temporary);
 rejects([&]{t::loadStorageSourcePaths(temporary);});std::filesystem::create_directory(temporary/"storage");std::filesystem::copy_file(root/"storage/source-paths.json",temporary/"storage/source-paths.json");
 equal(t::loadStorageSourcePaths(temporary).refreshArrow,paths.refreshArrow);
 {std::ofstream file(temporary/"storage/source-paths.json",std::ios::app);file.put(' ');}rejects([&]{t::loadStorageSourcePaths(temporary);});rejects([&]{t::loadStorageSourcePaths("relative/resources");});
}
#ifdef _WIN32
void wiring(const std::filesystem::path&root){
 std::atomic<unsigned>notices{},reads{},scans{};endfield::app::UtilityExecutor queue([&]{++notices;});unsigned opened{};
 auto options=t::makeStoragePreviewOptions(root,queue,[&]{++opened;});check(options.utility==&queue&&options.readCapacity&&options.scanDetails&&options.completionClock,"Factory binds existing queue and deferred real Windows operations");
 check(queue.stats().accepted==0&&!queue.stats().started&&opened==0,"Constructing production options opens no UI and starts no worker/query/scan");
 check(options.appearance.availableColor==std::optional(std::array<double,4>{.9886131882667542,.9012837409973145,.5504170060157776,1}),"Default available color comes from original detached source");
 options.openSettings();check(opened==1,"Only explicit settings action invokes caller");
 auto appearance=options.appearance;appearance.accent={.1,.2,.3,1};appearance.availableColor.reset();rejects([&]{t::makeStoragePreviewOptions(root,queue,[]{},appearance);});
 appearance.availableColor=std::array<double,4>{.4,.5,.6,1};check(t::makeStoragePreviewOptions(root,queue,[]{},appearance).appearance==appearance,"Explicit calibrated custom appearance is preserved");
 // Real provider callbacks are deliberately NEVER invoked by this fixture.
 // Exercise the resulting owner seam through synthetic worker payloads only.
 double now{};options.readCapacity=[&]()->std::optional<m::StorageCapacity>{++reads;return m::StorageCapacity{"Owned synthetic volume",1000,400,0};};
 options.scanDetails=[&](const m::StorageScanCancellation&){++scans;return m::StorageDetailsSnapshot{{{"fixture","Fixture",42,false}},false,now,false,{}};};options.completionClock=[&]{return now;};
 m::StorageController state;n::StorageCapacityProbe capacity(state,queue,options.readCapacity);n::StorageDetailsProbe details(state,queue,options.scanDetails,options.completionClock);
 const auto drain=[&]{queue.waitIdle();queue.drain();};
 state.activate(now);check(capacity.submitPending()&&!details.submitPending(),"Visible activation requests only capacity");drain();
 check(reads==1&&scans==0&&state.nextWakeTime()==60,"Initial load preserves source60second deadline and no scan");state.wake(59.9);check(!capacity.submitPending(),"No early capacity polling");now=60;state.wake(now);check(capacity.submitPending(),"One visible source deadline requests capacity");drain();check(reads==2&&scans==0,"Timed capacity completion still cannot start details");
 state.deactivate();now=180;state.wake(now);check(!capacity.submitPending()&&!details.submitPending()&&!state.nextWakeTime(),"Hidden owner has no capacity request or deadline");
 state.activate(now);capacity.submitPending();drain();state.requestDetails(now);check(details.submitPending(),"Only explicit model request starts details");drain();check(scans==1&&state.details().updatedAt==now,"Details completion uses the same owner-clock epoch");state.requestDetails(now+1);check(!details.submitPending(),"Source details cache suppresses repeat explicit scans");
}
#endif
}
int main(int argc,char**argv){try{check(argc==2||argc==3,"Pass packaged resources and optional detached Storage oracle");const auto root=std::filesystem::absolute(argv[1]).lexically_normal();resource(root);if(argc==3)source(t::loadStorageSourcePaths(root),argv[2]);
#ifdef _WIN32
 wiring(root);
#endif
 std::cout<<"PASS "<<checks<<" Storage production option checks\n";
 }catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
