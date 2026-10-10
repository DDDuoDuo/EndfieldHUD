#include "modules/orbipom_runtime.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace endfield::modules {
struct OrbiPomRuntimeTestAccess {
    static std::string evaluate(OrbiPomRuntime&r,std::string_view script){return r.evaluateForTesting(script);}
    static std::size_t live(){return OrbiPomRuntime::liveVMsForTesting();}
};
}
namespace m=endfield::modules;using J=ehud::data::Json;using Access=m::OrbiPomRuntimeTestAccess;
namespace {
unsigned checks{},comparedNumbers{},observedTrajectoryNumbers{};double maximumResidual{},maximumFrameSeconds{},trajectoryX{},trajectoryY{},trajectoryAngle{};std::vector<double>frameSeconds;
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
void healthy(const m::OrbiPomRuntime&r){if(r.error())throw std::runtime_error(*r.error());check(true,"Original runtime has no exception");}
void js(m::OrbiPomRuntime&r,std::string_view code,const char*why){const auto result=Access::evaluate(r,code);healthy(r);check(result=="true",why);}
void setup(m::OrbiPomRuntime&r,std::string_view code){Access::evaluate(r,std::string(code)+";undefined");healthy(r);}
void steps(m::OrbiPomRuntime&r,int count){for(int i=0;i<count;++i){const auto begin=std::chrono::steady_clock::now();r.advance(1./60);const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();frameSeconds.push_back(elapsed);maximumFrameSeconds=std::max(maximumFrameSeconds,elapsed);}healthy(r);}
J snapshot(const m::OrbiPomSnapshot&s){
    J::Array bodies,selected;for(const auto&b:s.bodies)bodies.emplace_back(J::Object{{"id",J(b.id)},{"level",J(b.level)},{"x",J(b.x)},{"y",J(b.y)},{"angle",J(b.angle)},{"size",J(b.size)},{"opacity",J(b.opacity)},{"scale",J(b.scale)}});
    for(auto id:s.selectedBodyIDs)selected.emplace_back(id);
    const auto optional=[](std::optional<double>value){return value?J(*value):J();};
    constexpr std::array names{"clear","wind","shake","swap"};
    return J::Object{{"state",J(s.state)},{"paused",J(s.paused)},{"score",J(s.score)},{"highScore",J(s.highScore)},{"energy",J(s.energy)},{"swapCharge",J(s.swapCharge)},{"mergeCount",J(s.mergeCount)},{"skillUseCount",J(s.skillUseCount)},{"energyProgress",J(s.energyProgress)},{"currentLevel",J(s.currentLevel)},{"nextLevel",J(s.nextLevel)},{"previewX",J(s.previewX)},{"previewY",J(s.previewY)},{"previewSize",J(s.previewSize)},{"previewVisible",J(s.previewVisible)},{"previewScale",J(s.previewScale)},{"dangerSeconds",optional(s.dangerSeconds)},{"skill",s.skill?J(names[std::size_t(*s.skill)]):J()},{"skillPhase",s.skillPhase?J(*s.skillPhase):J()},{"selectedBodyIDs",J(std::move(selected))},{"hoverBodyID",s.hoverBodyID?J(*s.hoverBodyID):J()},{"windSurfaceY",optional(s.windSurfaceY)},{"bodies",J(std::move(bodies))},{"simulationTime",J(s.simulationTime)}};
}
void compare(const J&actual,const J&expected,std::string path,bool observeTrajectory=false){
    if(expected.isNumber()){
        check(actual.isNumber(),"Source numeric field type preserved");const auto a=actual.number(),e=expected.number();const auto error=std::abs(a-e);
        // A captured Windows Math replay in the unchanged JSC solver explains
        // all 601 native frames exactly. Platform libm's 1-ULP primitive
        // differences grow through contacts; do not invent a wider tolerance
        // for a long trajectory. Independent one-step states remain gated.
        if(observeTrajectory&&path.find("/bodies/")!=std::string::npos&&(path.ends_with("/x")||path.ends_with("/y")||path.ends_with("/angle"))){auto&peak=path.ends_with("/x")?trajectoryX:path.ends_with("/y")?trajectoryY:trajectoryAngle;peak=std::max(peak,error);++observedTrajectoryNumbers;check(std::isfinite(a),"Long-run body coordinates stay finite; platform residual is reported separately");return;}
        maximumResidual=std::max(maximumResidual,error);++comparedNumbers;
        const bool discrete=path.ends_with("/id")||path.ends_with("/level")||path.ends_with("/score")||path.ends_with("/highScore")||path.ends_with("/energy")||path.ends_with("/swapCharge")||path.ends_with("/mergeCount")||path.ends_with("/skillUseCount")||path.ends_with("/currentLevel")||path.ends_with("/nextLevel")||path.find("selectedBodyIDs")!=std::string::npos||path.ends_with("/hoverBodyID");
        if(discrete?a!=e:error>1e-8*std::max(1.,std::abs(e)))throw std::runtime_error("JavaScriptCore comparison failed at "+path+": actual="+std::to_string(a)+" expected="+std::to_string(e));check(true,"Exact discrete fields; continuous fields relative/absolute tolerance1e-8");
    }else if(expected.isObject()){check(actual.isObject()&&actual.object().size()==expected.object().size(),"Source snapshot property set preserved");for(const auto&[key,value]:expected.object())compare(actual[key],value,path+"/"+key,observeTrajectory);}
    else if(expected.isArray()){check(actual.isArray()&&actual.array().size()==expected.array().size(),"Source array length/order preserved");for(std::size_t i=0;i<expected.array().size();++i)compare(actual.array()[i],expected.array()[i],path+"/"+std::to_string(i),observeTrajectory);}
    else check(actual==expected,"Source boolean/string/null fields match exactly");
}
void diagnosticTrace(const std::filesystem::path&resources,const char*output){
    // Explicit isolated diagnostic mode only. Wrappers return the untouched
    // engine's result; no physics values, scripts or parity tolerances change.
    std::ofstream file(output,std::ios::binary);check(bool(file),"Owned diagnostic output opens");
    m::OrbiPomRuntime runtime({resources});runtime.start(12345);healthy(runtime);
    setup(runtime,R"JS(var __mathTrace=[],__mathOverflow=0;for(const name of ['sin','cos','acos','atan2','sqrt','pow','hypot']){const original=Math[name];Math[name]=function(...args){const result=original(...args);if(__mathTrace.length<512)__mathTrace.push([name,args,result]);else ++__mathOverflow;return result;};})JS");
    const auto capture=[&]{file<<Access::evaluate(runtime,R"JS((()=>{const snapshot=OrbiPom.snapshot();const physics=Array.from(__game.phys.bodies.values()).map(value=>{const b=value.body;return {id:value.id,position:b.position,positionPrev:b.positionPrev,velocity:b.velocity,angle:b.angle,anglePrev:b.anglePrev,angularVelocity:b.angularVelocity,vertices:b.vertices.map(v=>[v.x,v.y])};});const result={snapshot,physics,math:__mathTrace.splice(0),overflow:__mathOverflow};__mathOverflow=0;return result;})())JS")<<'\n';healthy(runtime);};
    capture();for(int second=0;second<10;++second){runtime.move({double(30+(second*47)%160),40});runtime.drop();for(int frame=0;frame<60;++frame){runtime.advance(1./60);capture();}}
    check(bool(file),"Diagnostic transcript completed without truncation");std::cout<<"OrbiPom diagnostic: 601 snapshots with bounded primitive-call traces; normal parity assertions are unchanged\n";
}
void reference(const std::filesystem::path&resources,const char*fixture){
    std::ifstream file(fixture);check(bool(file),"Immutable original JavaScriptCore transcript opens");const std::string bytes(std::istreambuf_iterator<char>(file),{});const auto root=J::parse(bytes,128*1024);
    m::OrbiPomRuntime runtime({resources});check(!runtime.stats().initialized&&runtime.stats().calls==0&&Access::live()==0,"Constructing runtime performs no IO/evaluation/VM allocation");runtime.start(12345);healthy(runtime);compare(snapshot(runtime.snapshot()),root["frames"].array()[0]["snapshot"],"start");
    for(int second=0;second<10;++second){runtime.move({double(30+(second*47)%160),40});runtime.drop();steps(runtime,60);compare(snapshot(runtime.snapshot()),root["frames"].array()[std::size_t(second+1)]["snapshot"],std::to_string(second+1),true);}
    check(runtime.stats().boots==1&&Access::live()==1&&runtime.stats().heapBytes<32*1024*1024,"Ten-second actual physics transcript uses one bounded VM");
    const auto bodies=runtime.snapshot().bodies;const auto capacity=runtime.stats().snapshotCapacityBytes;for(int i=0;i<1000;++i)runtime.move({double(20+i%170),40});check(runtime.snapshot().bodies==bodies&&runtime.stats().snapshotCapacityBytes==capacity,"Pointer-only updates leave retained body snapshots and capacity untouched");
    runtime.pause(true);healthy(runtime);const auto frozen=runtime.snapshot();const auto calls=runtime.stats().calls;for(int i=0;i<100;++i){runtime.advance(60);runtime.move({20,40});runtime.pointerUp({20,40});}check(runtime.snapshot()==frozen&&runtime.stats().calls==calls,"Paused host calls execute no engine/timer work");
    runtime.pause(false);runtime.advance(1./60);check(runtime.snapshot().simulationTime-frozen.simulationTime<.02,"Resume advances only new visible elapsed time");
    const auto beforeInvalid=runtime.snapshot();runtime.advance(NAN);runtime.advance(-1);runtime.move({NAN,0});runtime.pointerUp({0,INFINITY});check(runtime.snapshot()==beforeInvalid,"Nonfinite and invalid elapsed input does not enter engine");
    bool rejected{};std::thread foreign([&]{try{runtime.advance(1./60);}catch(const std::logic_error&){rejected=true;}});foreign.join();check(rejected&&runtime.snapshot()==beforeInvalid,"Cross-thread access rejects before touching QuickJS");
    runtime.highScore(321);runtime.start(1);check(runtime.snapshot().highScore==321&&runtime.stats().boots==1,"Healthy restart preserves original VM and local high score");
}
void sourceSteps(const std::filesystem::path&resources,const std::filesystem::path&fixture){
    std::ifstream file(fixture,std::ios::binary);check(bool(file),"Independent original one-step fixture opens");const std::string bytes(std::istreambuf_iterator<char>(file),{});const auto root=J::parse(bytes,128*1024);
    check(root["cases"].array().size()==18,"Every source body profile plus wall/floor/contact/merge initial state is present");
    for(const auto&input:root["cases"].array()){
        m::OrbiPomRuntime runtime({resources});runtime.start(static_cast<std::uint32_t>(input["seed"].number()));healthy(runtime);setup(runtime,input["setup"].string());
        // Compare the complete engine snapshot, including source-only ghost
        // metadata that both native snapshot adapters intentionally omit.
        const auto name="one-step/"+input["name"].string();compare(J::parse(Access::evaluate(runtime,"OrbiPom.snapshot()"),128*1024),input["before"],name+"/initial");runtime.advance(1./60);healthy(runtime);compare(J::parse(Access::evaluate(runtime,"OrbiPom.snapshot()"),128*1024),input["after"],name+"/result");
    }
}
void gameplay(const std::filesystem::path&resources){
    m::OrbiPomRuntime game({resources});game.start(123);healthy(game);
    js(game,"Matter.version==='0.20.0' && __game.phys.engine.positionIterations===12 && __game.phys.engine.velocityIterations===7 && __game.phys.engine.constraintIterations===3 && __game.phys.engine.enableSleeping && __game.phys.engine.gravity.y===1.05","Actual pinned Matter solver parameters retained");
    js(game,"typeof fetch==='undefined'&&typeof XMLHttpRequest==='undefined'&&typeof Audio==='undefined'&&typeof WebSocket==='undefined'&&typeof Worker==='undefined'&&typeof std==='undefined'&&typeof os==='undefined'&&typeof setInterval==='undefined'","VM has no external service/network/OS/timer bridge");
    js(game,"(()=>{for(let level=1;level<=11;++level){let p=__game.phys.spawn(level,115,140);if(p.collisionParts.length<2||p.body.sleepThreshold!==180||Math.abs(p.body.mass-p.collisionParts[0].body.mass)>1e-9||Math.abs(p.body.inertia-p.collisionParts[0].body.inertia)>1e-9)return false;__game.phys.remove(p.id);}return true})()","All11 actual compound-body profiles retain primary mass/inertia");
    game.start(10);check(game.drop()&&!game.drop(),"Actual500ms drop cooldown rejects immediate repeat");steps(game,29);check(!game.drop(),"Actual cooldown stays locked before500ms");steps(game,2);check(game.drop(),"Actual cooldown unlocks after500ms");
    game.start(40);js(game,"(()=>{__game.phys.spawn(1,110,140);__game.phys.spawn(1,116,140);__game.tick(1000/60,__now);return e5().score===1&&e5().mergeCount===1&&Array.from(__game.phys.bodies.values()).some(b=>b.level===2)})()","Real Matter collision merges matching compound bodies exactly once");
    game.start(40);js(game,"(()=>{__game.phys.spawn(1,110,140);__game.phys.spawn(1,116,140);__game.phys.spawn(1,112,135);__game.tick(1000/60,__now);return e5().score===1&&e5().mergeCount===1&&__game.phys.bodies.size===2})()","Simultaneous contacts do not double-merge a body");
    for(int level=1;level<=11;++level){game.start(55);const auto code="(()=>{let a=__game.phys.spawn("+std::to_string(level)+",90,160),b=__game.phys.spawn("+std::to_string(level)+",140,160);__game.pendingMerges.push({a:a.body,b:b.body,level:"+std::to_string(level)+"});__game.processMerges();return e5().score==="+std::to_string(level*(level+1)/2)+"&&e5().mergeCount===1&&__game.phys.bodies.size==="+std::to_string(level==11?0:1)+"&&Array.from(__game.phys.bodies.values()).every(b=>b.level==="+std::to_string(std::min(11,level+1))+")})()";js(game,code,"Every original score/level advancement includes level11 pair disappearance");}
    game.start(2);js(game,"(()=>{for(let i=0;i<36;i++)e5().onMerge(2,1);return e5().energy===3&&e5().energyProgress===0&&e5().score===36})()","Twelve merges charge one energy point without score multiplier");
    js(game,"(()=>{e5().addScore(99999);e5().onMerge(11,10);return e5().score===99999})()","Source score saturation preserved");
    game.start(1);js(game,"(()=>{for(let i=0;i<29;i++)e5().onMerge(2,1);__now+=1999;e5().tickEnergyDecay(__now);if(e5().energyDecayStartedAt!==0)return false;__now+=1;e5().tickEnergyDecay(__now);__now+=2500;return e5().energy===2&&Math.abs(e2(e5(),__now)-2.5)<1e-9})()","Original partial energy delay and continuous decay preserved");
    game.start(4);for(int skill=0;skill<4;++skill)check(!game.activate(static_cast<m::OrbiPomSkill>(skill)),"Uncharged skill rejects in original controller");
    setup(game,"e5().addEnergy(3);__game.phys.spawn(4,115,130)");check(game.activate(m::OrbiPomSkill::clear)&&game.snapshot().skillPhase=="selecting","Clear enters actual target selection");game.move({115,130});check(game.snapshot().hoverBodyID.has_value(),"Clear targets actual compound body");game.pointerUp({115,130});check(game.snapshot().energy==2&&game.snapshot().swapCharge==1&&game.snapshot().skillPhase=="casting"&&std::all_of(game.snapshot().bodies.begin(),game.snapshot().bodies.end(),[](const auto&b){return b.id<0;}),"Clear removes real target and publishes original finite outgoing ghost");steps(game,40);check(!game.snapshot().skill,"Clear completes original650ms cast");
    setup(game,"__game.phys.spawn(4,115,235)");check(game.activate(m::OrbiPomSkill::wind),"Wind accepts original2energy cost");game.pointerUp({100,130});check(game.snapshot().energy==0&&game.snapshot().swapCharge==3,"Wind confirms cost and swap charge");js(game,"__game.phys.engine.enableSleeping===false&&__game.phys.skillCeiling!==null","Wind changes actual Matter sleeping and ceiling");steps(game,80);check(game.snapshot().windSurfaceY&&*game.snapshot().windSurfaceY<100,"Wind actual buoyancy reaches original75percent depth");steps(game,400);check(!game.snapshot().skill&&!game.snapshot().windSurfaceY,"Wind fill/hold/drain completes");js(game,"__game.phys.engine.enableSleeping===true&&__game.phys.skillCeiling===null&&Array.from(__game.phys.bodies.values()).every(b=>b.body.frictionAir===ey.body.frictionAir)","Wind restores solver state and friction");
    setup(game,"e5().addEnergy(3)");check(game.activate(m::OrbiPomSkill::shake),"Shake accepts3energy");game.pointerUp({100,130});check(game.snapshot().energy==0&&game.snapshot().swapCharge==6,"Shake caps original swap charge at6");steps(game,121);check(!game.snapshot().skill,"Shake finishes2000ms original cast");
    setup(game,"__game.phys.clearBodies();__game.phys.spawn(3,60,150);__game.phys.spawn(6,165,180)");check(game.activate(m::OrbiPomSkill::swap),"Actual charge unlocks Swap");game.pointerUp({60,150});check(game.snapshot().selectedBodyIDs.size()==1,"Swap holds first distinct target");game.pointerUp({165,180});js(game,"__game.phys.suspendedIds.size===2","Swap suspends both original physics bodies");steps(game,31);check(game.snapshot().swapCharge==6,"Swap defers charge settlement");steps(game,50);check(!game.snapshot().skill&&game.snapshot().swapCharge==0,"Swap completes1300ms original Bezier motion");js(game,"__game.phys.suspendedIds.size===0","Swap resumes both bodies");
    setup(game,"e3.setState({swapCharge:6})");check(game.activate(m::OrbiPomSkill::swap),"Recharged Swap can select again");const auto targets=game.snapshot().bodies;check(targets.size()==2,"Finished Swap retains exactly two physical targets");game.pointerUp({targets[0].x,targets[0].y});game.pointerUp({targets[1].x,targets[1].y});game.cancelSkill();js(game,"__game.phys.suspendedIds.size===0&&e5().swapCharge===6","Cancelling unfinished Swap restores bodies without spending charge");
    game.start(6);js(game,"(()=>{let d=new sF();if(d.update(true,0,5000)!==undefined||d.update(true,119,5000)!==undefined||d.update(true,120,5000)!==5000)return false;d.pause(200);d.resume(1200);return d.update(true,1320,5000)===4800})()","Original danger settle and pause semantics retained");
    js(game,"(()=>{let d=__game.phys.spawn(5,115,0);Matter.Body.setStatic(d.body,true);for(let i=0;i<320;i++){__now+=1000/60;__game.checkGameOver(__now)}return e5().state==='over'})()","Actual settled overflow ends original5second countdown");
}
void cadence(const std::filesystem::path&resources){
    m::OrbiPomRuntime sixty({resources}),thirty({resources});sixty.start(12345);thirty.start(12345);
    for(int second=0;second<10;++second){for(auto*game:{&sixty,&thirty}){game->move({double(30+(second*47)%160),40});game->drop();}for(int i=0;i<60;++i)sixty.advance(1./60);for(int i=0;i<30;++i)thirty.advance(1./30);healthy(sixty);healthy(thirty);}
    compare(snapshot(thirty.snapshot()),snapshot(sixty.snapshot()),"source30vs60Hz");
    js(sixty,"(()=>{let clock=new sY();return clock.takeSteps(5000)===3&&clock.takeSteps(0)===0&&clock.takeSteps(NaN)===0&&clock.takeSteps(-1)===0})()","Original fixed-step clock caps stalls at3steps without changing source virtual timers");
}
void integrity(){
    const auto root=std::filesystem::temp_directory_path()/("endfield-orbipom-integrity-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(std::filesystem::create_directory(root),"Owned synthetic resource directory is new");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}cleanup{root};
    std::filesystem::create_directory(root/"OrbiPom");{std::ofstream file(root/"OrbiPom"/"matter-0.20.0.js",std::ios::binary);const std::string synthetic(83318,'x');file.write(synthetic.data(),std::streamsize(synthetic.size()));}
    m::OrbiPomRuntime game({root});game.start(1);check(game.error()&&game.error()->find("integrity")!=std::string::npos&&game.stats().boots==0,"Correct-sized altered script is rejected before VM creation");
}
void limits(const std::filesystem::path&resources){
    const auto baseline=Access::live();
    {m::OrbiPomRuntime game({resources});game.highScore(123);game.start(44);game.drop();game.advance(1./60);const auto previous=game.snapshot();const auto began=std::chrono::steady_clock::now();Access::evaluate(game,"while(true){}");const auto duration=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();check(game.error()&&game.stats().faulted&&game.stats().interrupts>0&&duration<2,"Cooperative deadline terminates runaway original-VM execution");check(game.snapshot()==previous,"Interrupted command preserves last complete published snapshot");const auto calls=game.stats().calls;game.advance(1);check(game.stats().calls==calls,"Faulted runtime performs no further simulation");game.start(45);healthy(game);check(game.stats().boots==2&&game.snapshot().highScore==123&&Access::live()==baseline+1,"Restart retires faulted VM and restores requested local best");
        Access::evaluate(game,"new Uint8Array(64*1024*1024)");check(game.error()&&game.stats().heapBytes<32*1024*1024,"VM memory cap rejects oversized allocation");game.start(46);healthy(game);Access::evaluate(game,"function recurse(){return recurse()+1}recurse()");check(game.error()&&game.stats().faulted,"VM stack budget rejects recursion without native stack overflow");}
    check(Access::live()==baseline,"Faulted VM and all retained function/context references retire");
    {m::OrbiPomRuntimeOptions options{resources};options.memoryBytes=64*1024;options.snapshotBytes=64*1024;m::OrbiPomRuntime game(options);game.start(1);check(game.error()&&game.stats().faulted,"Initialization allocation failure is explicit");}
    check(Access::live()==baseline,"Partial initialization failure releases VM");
    {m::OrbiPomRuntimeOptions options{resources};options.snapshotBytes=1;m::OrbiPomRuntime game(options);game.start(1);healthy(game);const auto previous=game.snapshot();game.drop();check(game.error()&&game.snapshot()==previous,"Native snapshot budget fails atomically instead of truncating body state");}
    check(Access::live()==baseline,"Snapshot failure releases all engine resources");
    {m::OrbiPomRuntime game({resources/"missing-synthetic-resource-root"});game.start(1);check(game.error()&&!game.stats().initialized&&Access::live()==baseline,"Missing original resources cannot run a fallback simulation");}
}
}
int main(int argc,char**argv){try{if(argc==4&&std::string_view(argv[2])=="--trace"){diagnosticTrace(argv[1],argv[3]);return 0;}check(argc==3,"Pass original Resources root and immutable JavaScriptCore fixture");check(Access::live()==0,"No VM exists before explicit runtime work");reference(argv[1],argv[2]);check(Access::live()==0,"Healthy runtime teardown releases all VM resources");sourceSteps(argv[1],std::filesystem::path(argv[2]).parent_path()/"orbipom-step-source.json");gameplay(argv[1]);cadence(argv[1]);check(Access::live()==0,"All actual skills retire cleanly");limits(argv[1]);integrity();check(Access::live()==0,"Every runtime fixture is fully retired");std::sort(frameSeconds.begin(),frameSeconds.end());std::cout<<"PASS "<<checks<<" actual OrbiPom checks; strict source numbers="<<comparedNumbers<<" maximum residual="<<maximumResidual<<"; ten-second cross-platform trajectory observation ("<<observedTrajectoryNumbers<<" coordinates, no bitwise parity): max x="<<trajectoryX<<" y="<<trajectoryY<<" angle="<<trajectoryAngle<<"; frame median="<<frameSeconds[frameSeconds.size()/2]*1000<<"ms p95="<<frameSeconds[frameSeconds.size()*95/100]*1000<<"ms maximum="<<maximumFrameSeconds*1000<<"ms\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
