#include "modules/activity_state.hpp"
#include "modules/telemetry_format.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
namespace m=endfield::modules;using J=ehud::data::Json;
static std::atomic<std::size_t>allocations{};
void*operator new(std::size_t n){++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
namespace {
unsigned checks{};void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}void near(double a,double b,const char*m){check(std::abs(a-b)<1e-10,m);}
template<class F>void rejects(F f,const char*m){bool threw{};try{f();}catch(const std::invalid_argument&){threw=true;}check(threw,m);}
int names(std::string_view a,std::string_view b){return a.compare(b);}
std::optional<double>optional(const J&j){return j.isNull()?std::nullopt:std::optional<double>(j.number());}
// The oracle rounds its numbers to 1e-9 (tools/hud_design_reference_support.swift sanitize).
void same(std::optional<double>a,const J&b){check(bool(a)==!b.isNull(),"Source optional state matches");if(a)check(std::abs(*a-b.number())<=1e-9*std::max(1.,std::abs(*a)),"Source numeric value matches");}
m::ActivityCPU cpu(const J&v){const auto&a=v.array();return {std::uint64_t(a[0].integer()),std::uint64_t(a[1].integer()),std::uint64_t(a[2].integer()),std::uint64_t(a[3].integer())};}
// The Mac 1.3.0 oracle (tests/fixtures/telemetry-source.json, tools/telemetry_reference.*): the unchanged
// System/App derivation cases and the TelemetryFormat byte, rate and percent strings.
void oracle(const char*path){std::ifstream file(path);std::string bytes{std::istreambuf_iterator<char>(file),{}};const auto root=J::parse(bytes,32*1024*1024);const auto&data=root["derivations"];for(const auto&c:data["cpu"].array()){m::ActivityRawSample previous;previous.uptime=10;previous.cpu=cpu(c["old"]);auto current=previous;current.uptime=11;current.cpu=cpu(c["now"]);same(m::deriveActivity(current,&previous).cpuPercent,c["percent"]);}
 for(const auto&c:data["memory"].array()){const auto&a=c["args"].array();const auto value=m::activityMacMemory(a[0].integer(),a[1].integer(),a[2].integer(),a[3].integer(),a[4].integer(),a[5].integer());check(bool(value)==!c["result"].isNull(),"Source memory availability");if(value){const auto&r=c["result"].array();check(value->used==std::uint64_t(r[0].integer())&&value->total==std::uint64_t(r[1].integer())&&value->compressed==std::uint64_t(r[2].integer()),"Source resident physical footprint excludes purgeable cache");}}
 for(const auto&c:data["rates"].array()){m::ActivityRawSample old;old.uptime=100;old.cpu=m::ActivityCPU{1,2,3,4};old.network=m::ActivityCounters{{"en0",{10,20}}};old.disk=m::ActivityCounters{};auto now=old;now.uptime+=c["elapsed"].number();now.cpu=m::ActivityCPU{2,4,6,8};now.network=m::ActivityCounters{{c["topology"].boolean()?"en1":"en0",{30,70}}};const auto got=m::deriveActivity(now,&old);const auto&e=c["result"];same(got.cpuPercent,e["cpu"]);same(got.upload,e["upload"]);same(got.download,e["download"]);same(got.diskRead,e["read"]);same(got.diskWrite,e["write"]);check(got.statusNotes.size()==e["notes"].array().size(),"Original status notes");for(std::size_t n=0;n<got.statusNotes.size();++n)check(got.statusNotes[n]==e["notes"].array()[n].string(),"Original note text/order");}
 for(const auto&c:root["formats"].array()){if(c["value"].isString())continue;const auto value=optional(c["value"]);check(m::telemetry::bytes(value)==c["bytes"].string(),"Source decimal byte formatter");check(m::telemetry::rate(value)==c["rate"].string(),"Source rate formatter");check(m::telemetry::percent(value)==c["percent"].string(),"Source machine percent formatter");}
 for(const auto&c:data["apps"].array()){const auto mode=c["mode"].integer();m::ActivityAppsRaw old;old.uptime=10;old.secondsPerCPUTick=.01;old.apps.push_back({"app","Example","",{8,7,7}});old.members["app"]={7,8};old.processes[7]={7,1,100,1000,1,100,100};old.processes[8]={8,2,200,2000,1,200,200};auto now=old;now.uptime=mode==6?15:mode==7?15.0001:11;now.processes[7].cpuTicks=250;now.processes[7].diskRead=150;now.processes[8].cpuTicks=300;now.processes[8].diskWrite=300;if(mode==1)now.processes.erase(8);if(mode==2)now.processes[8].startID=3;if(mode==3)now.members["app"]={7};now.network=m::ActivityAppNetwork{mode==5?6:now.uptime,5,{{7,{10,20}}},mode==4?std::set<std::uint32_t>{8}:std::set<std::uint32_t>{}};const auto value=m::deriveActivityApps(now,&old).items[0];same(value.cpuPercent,c["cpu"]);same(value.memoryBytes?std::optional<double>(double(*value.memoryBytes)):std::nullopt,c["memory"]);same(value.diskRead,c["read"]);same(value.diskWrite,c["write"]);same(value.upload,c["upload"]);same(value.download,c["download"]);check(value.statusNotes.size()==c["notes"].array().size(),"Source app notes count");for(std::size_t n=0;n<value.statusNotes.size();++n)check(value.statusNotes[n]==c["notes"].array()[n].string(),"Source app note ordering");}}
void derivation(){const auto max=std::numeric_limits<std::uint64_t>::max();check(!m::activityMacMemory(100,4096,max,0,2,0),"Resident sum overflow is unavailable");check(!m::activityMacMemory(max,max,2,0,0,0),"Resident multiply overflow is unavailable");m::ActivityRawSample old;old.uptime=1;old.cpu=m::ActivityCPU{0,0,0,0};auto now=old;now.uptime=2;now.cpu=m::ActivityCPU{max,1,0,0};check(!m::deriveActivity(now,&old).cpuPercent,"CPU delta overflow is unavailable");old.network=m::ActivityCounters{{"a",{4,5}}};now.network=m::ActivityCounters{{"a",{3,9}}};check(!m::deriveActivity(now,&old).download,"Counter reset cannot cause spike");
 m::ActivityAppsRaw a;a.timestamp=1;a.uptime=10;a.secondsPerCPUTick=.01;a.apps.push_back({"app","Example","",{8,7,7}});a.members["app"]={7,8};a.processes[7]={7,1,100,1000,1,100,100};a.processes[8]={8,2,200,2000,1,200,200};auto b=a;b.uptime=11;b.processes[7].cpuTicks+=150;b.processes[8].cpuTicks+=100;b.processes[7].diskRead=150;b.processes[8].diskWrite=300;auto result=m::deriveActivityApps(b,&a);check(result.items[0].cpuPercent==250&&result.items[0].memoryBytes==3000&&result.items[0].diskRead==50&&result.items[0].diskWrite==100,"App sums complete membership;100% is one CPU");b.processes[8].startID=3;check(!m::deriveActivityApps(b,&a).items[0].cpuPercent,"Reused PID cannot inherit CPU baseline");b=a;b.uptime=16;check(!m::deriveActivityApps(b,&a).items[0].cpuPercent,"Delayed app samples over5s leave a gap");b=a;b.uptime=11;b.processes.erase(8);result=m::deriveActivityApps(b,&a);check(!result.items[0].memoryBytes&&!result.items[0].cpuPercent,"Protected missing member makes whole app unavailable");b=a;b.uptime=11;b.processes[7].diskRead.reset();result=m::deriveActivityApps(b,&a);check(result.items[0].cpuPercent==0&&!result.items[0].diskRead,"Unsupported Windows disk does not become fake0 or block known CPU");b.network=m::ActivityAppNetwork{11,5,{{7,{10,20}}},{}};result=m::deriveActivityApps(b,&a);check(result.items[0].download==10&&result.items[0].upload==20,"Complete absent network PID means source0");b.network->unknownPIDs.insert(8);check(!m::deriveActivityApps(b,&a).items[0].download,"Unknown network owner stays unknown");
 m::ActivityApp x,y,z;x.identity={"a","A","",{}};y.identity={"b","B","",{}};z.identity={"c","C","",{}};x.cpuPercent=1;z.cpuPercent=2;std::vector items{x,y,z};m::sortActivityApps(items,m::ActivitySort::cpu,false,names);check(items[0].identity.id=="a"&&items[2].identity.id=="b","Unknown sorts last ascending");m::sortActivityApps(items,m::ActivitySort::cpu,true,names);check(items[0].identity.id=="c"&&items[2].identity.id=="b","Unknown sorts last descending");}
// GPU (Windows addition): the total load between two collections of
// cumulative 100 ns running times, per process or per app: on each physical
// adapter its engine types summed (each type its busiest engine), at most
// 100%, and of two adapters the busier one.
std::vector<m::ActivityGPUUsage>ordered(std::vector<m::ActivityGPUUsage>v){std::sort(v.begin(),v.end(),[](const auto&x,const auto&y){return x.engine<y.engine||(x.engine==y.engine&&x.pid<y.pid);});return v;}
m::ActivityGPUUsage&reading(m::ActivityGPUSample&s,std::uint32_t pid,const m::ActivityGPUEngine&engine){for(auto&v:s.usage)if(v.pid==pid&&v.engine==engine)return v;throw std::runtime_error("No such GPU reading");}
void gpu(){
 check(m::activityGPUEngineType("")==2166136261u&&m::activityGPUEngineType("a")==0xE40C292Cu,"Engine type key: 32-bit FNV-1a of the engtype_ label");
 const auto d3=m::activityGPUEngineType("3D"),copyType=m::activityGPUEngineType("Copy"),decode=m::activityGPUEngineType("VideoDecode");
 check(d3!=copyType&&d3!=decode&&copyType!=decode&&m::activityGPUEngineType("3D")==d3,"Distinct labels, distinct types; one label, one type");
 // A discrete adapter (3D, two Copy engines, Video Decode) and an integrated one (3D, Copy).
 const m::ActivityGPUEngine render{0x1234,0,d3,0},copy1{0x1234,0,copyType,1},copy2{0x1234,0,copyType,2},video{0x1234,0,decode,3},igpu{0x9999,0,d3,0},icopy{0x9999,0,copyType,1};
 m::ActivityGPUSample a,b;a.time=1'000'000;b.time=11'000'000; // one second apart: 1% is 100'000
 a.usage=ordered({{7,render,0},{8,render,1000},{7,copy1,0},{8,copy2,1000},{7,video,0},{10,igpu,0},{9,igpu,500},{10,icopy,0}});
 b.usage=ordered({{7,render,4'000'000},{8,render,1'001'000},{7,copy1,2'000'000},{8,copy2,3'001'000},{7,video,500'000},{10,igpu,6'000'000},{9,igpu,500},{10,icopy,1'000'000}});
 // Discrete: 3D 40% + 10% = 50%, Copy the busier of its engines (20%, 30%), Video Decode 5%: 85%.
 // Integrated: 3D 60% + Copy 10% = 70%. The busiest single engine is the integrated 3D at 60%.
 near(*m::activityGPUPercent(b,a),85,"Total load: the busier adapter's engine types summed (3D 50% + Copy 30% + Video Decode 5%)");
 check(*m::activityGPUPercent(b,a)>60+1e-9,"Total load, not the busiest single engine (60%)");
 const std::array<std::uint32_t,1>eight{8};near(*m::activityGPUPercent(b,a,eight),40,"One process: its own engine types summed (3D 10% + Copy 30%)");
 const std::array<std::uint32_t,1>seven{7};near(*m::activityGPUPercent(b,a,seven),65,"A type counts the selection's busiest engine of it (3D 40% + Copy 20% + Video Decode 5%)");
 const std::array<std::uint32_t,2>both{7,8};near(*m::activityGPUPercent(b,a,both),85,"An app sums its processes per engine, takes each type's busiest engine, then sums the types");
 const std::array<std::uint32_t,1>ten{10};near(*m::activityGPUPercent(b,a,ten),70,"The integrated adapter's own load (3D 60% + Copy 10%)");
 const std::array<std::uint32_t,2>spread{7,10};near(*m::activityGPUPercent(b,a,spread),70,"An app on both adapters: the busier adapter, neither the sum (135%) nor the average");
 const std::array<std::uint32_t,1>idle{9};near(*m::activityGPUPercent(b,a,idle),0,"An unchanged counter is an idle 0%");
 const std::array<std::uint32_t,1>absent{42};near(*m::activityGPUPercent(b,a,absent),0,"A process without GPU use is 0%");
 auto c=b;reading(c,8,copy2).runningTime=500;near(*m::activityGPUPercent(c,a,eight),10,"A counter that went backwards (restarted instance) adds nothing");
 c=b;c.usage.push_back({3,{0xFFFF,0,d3,0},5'000'000});near(*m::activityGPUPercent(c,a),85,"A process only in the newer collection adds nothing");
 c=b;reading(c,7,render).runningTime=7'000'000;near(*m::activityGPUPercent(c,a),100,"An adapter's engine types sum to at most 100% (3D 80% + Copy 30% + Video Decode 5%)");
 c=b;reading(c,10,igpu).runningTime=30'000'000;near(*m::activityGPUPercent(c,a,ten),100,"A runaway engine reading still reads at most 100%");
 {// A linked adapter's second physical node is an adapter of its own (merged, its 3D would make the first node 100%).
  auto a2=a,b2=b;const m::ActivityGPUEngine node{0x1234,1,d3,0};a2.usage.push_back({7,node,0});b2.usage.push_back({7,node,9'000'000});a2.usage=ordered(a2.usage);b2.usage=ordered(b2.usage);
  near(*m::activityGPUPercent(b2,a2),90,"Each physical node is its own adapter");}
 c=b;std::swap(c.usage[0],c.usage[1]);check(!m::activityGPUPercent(c,a),"An unordered collection is unavailable");
 c=b;c.usage.push_back(c.usage.back());check(!m::activityGPUPercent(c,a),"A duplicated instance is unavailable");
 c=b;c.time=a.time;check(!m::activityGPUPercent(c,a),"No elapsed time base is unavailable");
 const std::array<std::uint32_t,2>unsorted{8,7};check(!m::activityGPUPercent(b,a,unsorted),"Process selection must be sorted");
 m::ActivityGPUSample empty1,empty2;empty2.time=10;near(*m::activityGPUPercent(empty2,empty1),0,"No engine instance at all is an idle GPU");
 {const auto before=allocations.load();double sum{};for(unsigned n=0;n<1000;++n)sum+=*m::activityGPUPercent(b,a,both);check(allocations.load()==before&&std::abs(sum-85'000)<1e-6,"Total load is computed without allocating");}
 // The system snapshot: both readings needed, no status note (the Mac notes stay exact).
 m::ActivityRawSample old;old.uptime=10;old.cpu=m::ActivityCPU{1,2,3,4};old.gpu=a;auto now=old;now.uptime=11;now.cpu=m::ActivityCPU{2,4,6,8};now.gpu=b;
 auto value=m::deriveActivity(now,&old);near(*value.gpuPercent,85,"System GPU total load from the two readings");const auto notes=value.statusNotes;
 now.gpu.reset();value=m::deriveActivity(now,&old);check(!value.gpuPercent&&value.statusNotes==notes,"No GPU reading: unknown, and no extra note");
 now.gpu=b;old.gpu.reset();check(!m::deriveActivity(now,&old).gpuPercent,"The first GPU reading is a baseline");
 for(const auto&note:value.statusNotes)check(note.find("GPU")==std::string::npos,"GPU never adds a status note");
 // Apps: the member processes, a stable membership, no reused PID.
 m::ActivityAppsRaw x;x.uptime=10;x.secondsPerCPUTick=.01;x.apps.push_back({"app","Example","",{8,7}});x.apps.push_back({"idle","Idle","",{9}});x.members["app"]={7,8};x.members["idle"]={9};
 x.processes[7]={7,1,100,1000,1,100,100};x.processes[8]={8,2,200,2000,1,200,200};x.processes[9]={9,3,0,10,1,0,0};x.gpu=a;auto y=x;y.uptime=11;y.gpu=b;
 auto apps=m::deriveActivityApps(y,&x);near(*apps.items[0].gpuPercent,85,"App GPU total load over its processes");near(*apps.items[1].gpuPercent,0,"Idle app 0%");
 y.processes[8].startID=99;check(!m::deriveActivityApps(y,&x).items[0].gpuPercent,"A reused PID leaves the app's GPU unknown");y.processes[8].startID=2;
 // Mac 1.3.0 (AppActivityMonitor.swift:124-130): GPU for a complete app only.
 y.processes.erase(8);check(!m::deriveActivityApps(y,&x).items[0].gpuPercent,"An app with an unreadable (protected) process has no GPU reading");y.processes[8]=x.processes[8];
 y.members["app"]={7};check(!m::deriveActivityApps(y,&x).items[0].gpuPercent,"Changed membership is a new baseline");y.members["app"]={7,8};
 y.uptime=16;check(!m::deriveActivityApps(y,&x).items[0].gpuPercent,"A delayed reading over 5 s leaves a gap");y.uptime=11;
 y.gpu.reset();check(!m::deriveActivityApps(y,&x).items[0].gpuPercent,"No GPU reading: unknown");
 // Sorting by GPU: busiest first, unknown last either way.
 m::ActivityApp p,q,r;p.identity={"p","P","",{}};q.identity={"q","Q","",{}};r.identity={"r","R","",{}};p.gpuPercent=5;r.gpuPercent=50;std::vector items{p,q,r};
 m::sortActivityApps(items,m::ActivitySort::gpu,true,names);check(items[0].identity.id=="r"&&items[1].identity.id=="p"&&items[2].identity.id=="q","GPU sorts busiest first, unknown last");
 m::sortActivityApps(items,m::ActivitySort::gpu,false,names);check(items[0].identity.id=="p"&&items[2].identity.id=="q","Ascending GPU keeps unknown last");check(m::activitySortKey(m::ActivitySort::gpu)=="gpu","GPU sort action key");
}
void state(){m::ActivityState state(names);for(unsigned n=0;n<90;++n){m::ActivitySnapshot v;v.timestamp=n;v.uptime=n;v.cpuPercent=n;state.receive(v);}check(state.history().size()==60&&state.history().front().uptime==30,"History bounded60 and retained in session");
 unsigned systemNotices{},appNotices{};state.onSystem=[&]{++systemNotices;};state.onApps=[&]{++appNotices;};
 m::ActivityAppsSnapshot apps;apps.uptime=100;for(unsigned n=0;n<256;++n){m::ActivityApp item;item.identity={std::to_string(n),"App "+std::to_string(n),"",{}};item.cpuPercent=n;apps.items.push_back(item);}state.receiveApps(apps);
 check(appNotices==1&&!systemNotices&&state.appSnapshot().items.front().identity.id=="255","A sample notifies its observer and keeps the current sort (CPU, busiest first)");
 const auto memory=allocations.load();for(unsigned n=0;n<10000;++n)(void)state.history();check(allocations==memory,"History queries allocate nothing");
 state.setSort(m::ActivitySort::name,false);check(state.sortKey()==m::ActivitySort::name&&!state.descending()&&state.appSnapshot().items.front().identity.id=="0","setSort re-sorts the current items");
 const auto revision=state.revision();auto bad=apps;bad.items[0].cpuPercent=std::numeric_limits<double>::quiet_NaN();rejects([&]{state.receiveApps(bad);},"NaN metrics rejected before sorting");check(state.revision()==revision&&appNotices==1,"Rejected app update preserves state");
 m::ActivitySamplingPlan plan;check(!plan.nextWakeTime(),"No sampler until explicit session start");plan.start(10);auto demand=plan.takeDue(10);check(demand.system&&demand.resetSystem&&!demand.apps&&plan.nextWakeTime()==15,"Source background5s starts with baseline");plan.setVisible(true,false,false,11);check(plan.nextWakeTime()==12,"Activity visible changes same deadline to1Hz");plan.setVisible(false,true,false,11);check(plan.nextWakeTime()==12,"Charge alert shares same demand");plan.setVisible(true,false,true,12);demand=plan.takeDue(12);check(demand.system&&demand.apps&&demand.resetApps,"Apps first request uses independent baseline");plan.setVisible(false,false,true,12.1);check(!plan.appsActive()&&plan.nextWakeTime()==17.1,"Hidden Apps off while system history continues5s");plan.stop();check(!plan.nextWakeTime(),"Shutdown removes all deadlines");
 // setAlertActive: the alert's demand is separate from Activity visibility.
 plan.setAlert(false,20);check(!plan.running()&&!plan.nextWakeTime(),"Clearing an absent alert starts nothing");plan.setAlert(true,20);demand=plan.takeDue(20);check(plan.running()&&plan.alertActive()&&!plan.visibleActive()&&demand.system&&demand.resetSystem&&plan.nextWakeTime()==21,"An alert starts the stopped session at 1 Hz with a fresh baseline");
 plan.setVisible(true,true,true,20.5);plan.setAlert(false,20.6);check(plan.visibleActive()&&plan.appsActive()&&!plan.alertActive()&&plan.nextWakeTime()==20.5,"Clearing the alert keeps Activity's own visible Apps demand at 1 Hz");
 plan.setVisible(false,false,false,21);plan.setAlert(true,21.5);check(plan.alertActive()&&plan.nextWakeTime()==22.5&&!plan.appsActive(),"Alert alone resumes 1 Hz system sampling without app enumeration");const auto generation=plan.generation();plan.setAlert(true,21.6);check(plan.nextWakeTime()==22.5&&plan.generation()==generation,"An unchanged alert is a no-op");plan.setAlert(false,22);check(plan.nextWakeTime()==27&&plan.running(),"Without either demand the session history continues at 5 s");plan.stop();
 // reconcileChargeMetricTelemetry over the same plan and state.
 {m::ActivityState shared(names);m::ActivitySamplingPlan sampling;m::ActivityTelemetryLink link(sampling,shared);std::vector<std::optional<double>>delivered;unsigned clears{};
  const auto deliver=[&](const m::ActivitySnapshot*value){if(value)delivered.push_back(value->cpuPercent);else ++clears;};
  check(!link.synchronize(false,1,deliver)&&delivered.empty()&&!clears&&!sampling.running(),"No demand: nothing attaches or starts");
  sampling.start(1);(void)sampling.takeDue(1);check(sampling.nextWakeTime()==6,"Background session at 5 s");
  check(link.synchronize(true,2,deliver)&&sampling.alertActive()&&sampling.nextWakeTime()==3&&delivered.size()==1&&!delivered[0],"Attaching sets 1 Hz demand and presents the current snapshot at once");
  check(!link.synchronize(true,2.5,deliver)&&delivered.size()==1,"Unchanged samples are not delivered again");
  m::ActivitySnapshot sample;sample.timestamp=3;sample.uptime=3;sample.cpuPercent=42;shared.receive(sample);check(!link.synchronize(true,3,deliver)&&delivered.size()==2&&delivered[1]==42,"Each new system sample is delivered once");
  sampling.stop();sampling.start(4);check(!sampling.alertActive(),"A sleep cycle clears the alert demand");link.synchronize(true,4,deliver);check(sampling.alertActive()&&delivered.size()==2,"The attached link restores its demand after a stop/start cycle");
  check(link.synchronize(false,5,deliver)&&!sampling.alertActive()&&clears==1&&sampling.nextWakeTime()==10,"Detaching clears the demand once and returns to the 5 s history");
  check(!link.synchronize(false,6,deliver)&&clears==1,"A detached link stays silent");}rejects([&]{plan.start(std::numeric_limits<double>::infinity());},"Nonfinite clock rejected");
 }
}
int main(int argc,char**argv){try{derivation();gpu();state();if(argc==2)oracle(argv[1]);std::cout<<"PASS "<<checks<<" Activity source state/presentation checks\n";}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
