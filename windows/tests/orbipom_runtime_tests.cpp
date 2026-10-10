#include "modules/orbipom_runtime.hpp"
#include "core/data/json.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <array>
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
unsigned checks{},comparedNumbers{},observedTrajectoryNumbers{},substitutedTrajectoryNumbers{};double maximumResidual{},maximumFrameSeconds{},trajectoryX{},trajectoryY{},trajectoryAngle{};std::vector<double>frameSeconds;
std::string primitiveSummary,auditSummary;
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
        // Only reached for production-libm snapshots taken AFTER this run has
        // executed a Math primitive whose MSVC UCRT result differs (by the
        // gated 1 ULP) from Apple libm's result for the same input; see
        // nativeLibmAudit(). Before that point, and in the Apple-primitive
        // substitution run, body coordinates use the strict tolerance below.
        if(observeTrajectory&&path.find("/bodies/")!=std::string::npos&&(path.ends_with("/x")||path.ends_with("/y")||path.ends_with("/angle"))){auto&peak=path.ends_with("/x")?trajectoryX:path.ends_with("/y")?trajectoryY:trajectoryAngle;peak=std::max(peak,error);++observedTrajectoryNumbers;check(std::isfinite(a),"Long-run body coordinates stay finite after a gated 1-ULP platform primitive difference");return;}
        if(path.find("/bodies/")!=std::string::npos&&(path.ends_with("/x")||path.ends_with("/y")||path.ends_with("/angle"))&&path.starts_with("apple-libm/"))++substitutedTrajectoryNumbers;
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
std::string bytesOf(const std::filesystem::path&path,const char*why){std::ifstream file(path,std::ios::binary);check(bool(file),why);return std::string(std::istreambuf_iterator<char>(file),{});}
void transcriptSecond(m::OrbiPomRuntime&r,int second,bool timed){r.move({double(30+(second*47)%160),40});r.drop();if(timed){steps(r,60);return;}for(int i=0;i<60;++i)r.advance(1./60);healthy(r);}
// ECMA-262 21.3.2 leaves every one of these Math results
// "implementation-approximated". orbipom-libm-source.json records, as IEEE-754
// bits, every distinct call the unchanged Mac runtime (JavaScriptCore -> Apple
// libm) makes while reproducing orbipom-source.json (tools/orbipom_libm_reference.py).
// This table is test-only evidence: production keeps the platform libm.
constexpr std::string_view appleTableInstaller=R"JS(var __apple=(function(){
 const names=['acos','acosh','asin','asinh','atan','atanh','atan2','cbrt','cos','cosh','exp','expm1','hypot','log','log1p','log10','log2','pow','sin','sinh','tan','tanh'];
 const view=new DataView(new ArrayBuffer(8)),natives=new Map(names.map(n=>[n,Math[n]])),tables=new Map(),rows=new Map();
 const bitsKey=v=>{view.setFloat64(0,v);return view.getUint32(0)+':'+view.getUint32(4);};
 const key=args=>args.map(a=>bitsKey(Number(a))).join(',');
 const fromHex=h=>{view.setUint32(0,parseInt(h.slice(0,8),16));view.setUint32(4,parseInt(h.slice(8,16),16));return view.getFloat64(0);};
 const ordered=v=>{view.setFloat64(0,v);const i=view.getBigInt64(0);return i<0n?-(1n<<63n)-i:i;};
 const stats={calls:0,served:0,missing:0,agree:0,oneUlp:0,wider:0,unchecked:0,first:null};
 function compare(name,args,actual,expected){if(Object.is(actual,expected)){++stats.agree;return;}let d=ordered(actual)-ordered(expected);if(d<0n)d=-d;if(d===1n)++stats.oneUlp;else ++stats.wider;
  if(stats.first===null)stats.first={name:name,args:args.map(Number),actual:actual,expected:expected,ulps:Number(d),call:stats.calls};}
 return {
  add:function(name,list){let table=tables.get(name),all=rows.get(name);if(!table){table=new Map();all=[];tables.set(name,table);rows.set(name,all);}
   for(const row of list){const args=row.slice(0,-1).map(fromHex),expected=fromHex(row[row.length-1]);table.set(key(args),expected);all.push([args,expected]);}return true;},
  install:function(substitute){for(const name of names){const original=natives.get(name),table=tables.get(name);
   Math[name]=function(...args){++stats.calls;const expected=table===undefined?undefined:table.get(key(args));
    if(substitute){if(expected!==undefined){++stats.served;return expected;}++stats.missing;if(stats.first===null)stats.first={name:name,args:args.map(Number),call:stats.calls};return original.apply(Math,args);}
    const actual=original.apply(Math,args);if(expected===undefined)++stats.unchecked;else compare(name,args,actual,expected);return actual;};}return true;},
  verify:function(name,begin,end){const all=rows.get(name),original=natives.get(name);for(let i=begin;i<end&&i<all.length;++i){++stats.calls;compare(name,all[i][0],original.apply(Math,all[i][0]),all[i][1]);}return true;},
  stats:function(){return stats;}
 };
})();true)JS";
J libmTable(const std::filesystem::path&fixture,const std::string&transcriptBytes){
    const auto root=J::parse(bytesOf(fixture,"Original JavaScriptCore Math primitive fixture opens"),1024*1024);
    check(root["transcriptSHA256"].string()==endfield::core::packet::sha256({reinterpret_cast<const std::uint8_t*>(transcriptBytes.data()),transcriptBytes.size()}),"Math primitive fixture was recorded from this exact JavaScriptCore transcript");
    check(root["sourcePins"]["Resources/OrbiPom/matter-0.20.0.js"].string()=="928d059868201b2c4c270818fda44e5d26c099e4df407043f018f9bbe2c598cf"&&root["sourcePins"]["Resources/OrbiPom/orbipom.js"].string()=="1c68da4d2165f3d6d45e8842ab4d7d6e766bb3d738e3ff19f32ca2dce64e4054","Math primitive fixture belongs to the runtime's pinned original scripts");
    check(root["moduleLoadCalls"].integer()==0,"Unchanged scripts make no approximated Math call before a table can be installed");
    check(root["seed"].integer()==12345&&!root["functions"].object().empty(),"Math primitive fixture covers the seeded transcript");
    return root;
}
void loadAppleTable(m::OrbiPomRuntime&r,const J&libm){
    js(r,appleTableInstaller,"Apple primitive table installs as isolated test script");
    for(const auto&[name,rows]:libm["functions"].object()){const auto&all=rows.array();check(!all.empty(),"Recorded primitive has rows");
        for(std::size_t begin=0;begin<all.size();begin+=256){J::Array chunk(all.begin()+std::ptrdiff_t(begin),all.begin()+std::ptrdiff_t(std::min(all.size(),begin+256)));js(r,"__apple.add("+J(name).encode()+","+J(std::move(chunk)).encode()+")","Apple primitive rows load exactly through IEEE-754 bit patterns");}}
}
J appleStats(m::OrbiPomRuntime&r){const auto text=Access::evaluate(r,"__apple.stats()");healthy(r);return J::parse(text,128*1024);}
std::string describe(const J&first){return first.isNull()?std::string("none"):first.encode();}
// Why the long-run body trajectory cannot be gated bit-for-bit against the Mac
// (the inherent tolerance, proved by the three runs below):
//  * Matter/orbipom.js call only Math.sin/cos/atan2 among the ECMA-262
//    "implementation-approximated" functions (fixture: 17928/17928/3588 calls,
//    no ** operator, none while loading). JavaScriptCore forwards them to
//    Apple libm; QuickJS-NG to the MSVC UCRT. All other arithmetic is IEEE.
//  * Apple's results are correctly rounded on 4856 of the 4918 distinct inputs
//    (plus the exact sin(0)/cos(0)) and the other faithful neighbour on 60
//    (fixture appleVersusCorrectlyRounded, 80-digit reference), so even a
//    correctly rounded libm would differ from the Mac on those 60.
//    The UCRT measured on the Windows test laptop (2026-10-10) equals Apple on
//    4870 inputs and differs by exactly one ULP on 48. Two faithfully rounded libms
//    may differ by one ULP and never more: that is gated below on every input.
//  * appleSubstitution(): the unchanged QuickJS runtime, solver and bridge fed
//    Apple's primitive results reproduce all 11 JavaScriptCore snapshots,
//    body x/y/angle included, within the strict 1e-8 tolerance.
//  * reference(): the production run is strict until its first differing
//    primitive (sin(5.6286868376817125), call 44: Apple ...13ec, UCRT ...13ed).
//    Matter's contact solver then amplifies that ULP (about 1 pt of x after
//    ten seconds), but every discrete field (body IDs, levels, merges, score,
//    energy, skills) still matches JavaScriptCore exactly, and the production
//    trajectory is deterministic (equal to the audited run bit for bit).
void primitiveAccuracy(const std::filesystem::path&resources,const J&libm){
    m::OrbiPomRuntime runtime({resources});runtime.start(1);healthy(runtime);loadAppleTable(runtime,libm);
    std::int64_t rows{};for(const auto&[name,list]:libm["functions"].object()){rows+=std::int64_t(list.array().size());for(std::size_t begin=0;begin<list.array().size();begin+=512)js(runtime,"__apple.verify("+J(name).encode()+","+std::to_string(begin)+","+std::to_string(begin+512)+")","Platform primitive verification chunk completes");}
    const auto stats=appleStats(runtime);
    check(stats["calls"].integer()==rows&&stats["agree"].integer()+stats["oneUlp"].integer()+stats["wider"].integer()==rows,"Every recorded original input is evaluated by this platform's libm");
    if(stats["wider"].integer()!=0)throw std::runtime_error("Platform Math differs from Apple libm by more than one ULP: "+describe(stats["first"]));
    check(true,"Every platform Math result is within one ULP of Apple libm on every recorded original input");
    primitiveSummary="platform libm vs Apple on "+std::to_string(rows)+" recorded inputs: equal="+std::to_string(stats["agree"].integer())+" one-ULP="+std::to_string(stats["oneUlp"].integer())+" wider=0";
}
// Restored strict long-run parity: the unchanged QuickJS runtime, solver and
// bridge, given Apple's primitive results, must reproduce the complete
// ten-second JavaScriptCore transcript (body x/y/angle included) under the same
// strict 1e-8 relative tolerance; any input missing from the table fails.
void appleSubstitution(const std::filesystem::path&resources,const J&transcript,const J&libm){
    m::OrbiPomRuntime runtime({resources});loadAppleTable(runtime,libm);js(runtime,"__apple.install(true)","Apple primitive substitution installs before start");
    runtime.start(12345);healthy(runtime);compare(snapshot(runtime.snapshot()),transcript["frames"].array()[0]["snapshot"],"apple-libm/start");
    for(int second=0;second<10;++second){transcriptSecond(runtime,second,false);compare(snapshot(runtime.snapshot()),transcript["frames"].array()[std::size_t(second+1)]["snapshot"],"apple-libm/"+std::to_string(second+1));}
    const auto stats=appleStats(runtime);
    if(stats["missing"].integer()!=0)throw std::runtime_error("Apple primitive table misses an input of the reproduced transcript: "+describe(stats["first"]));
    check(stats["calls"].integer()>0&&stats["served"].integer()==stats["calls"].integer(),"Every approximated Math call of the ten-second transcript is served from Apple's recorded results");
    check(runtime.stats().boots==1&&!runtime.error(),"Apple-primitive transcript uses one healthy VM");
}
struct NativeAudit{std::array<bool,11>agreed{};std::vector<m::OrbiPomSnapshot>frames;};
// Production-libm run with checking wrappers that return the native result
// unchanged. agreed[n] is true while every primitive executed so far, on inputs
// the original also used, returned Apple's exact bits; a body trajectory is
// then required to match JavaScriptCore strictly. The first differing
// primitive must be exactly one ULP (gated) and is reported.
NativeAudit nativeLibmAudit(const std::filesystem::path&resources,const J&libm){
    NativeAudit audit;m::OrbiPomRuntime runtime({resources});loadAppleTable(runtime,libm);js(runtime,"__apple.install(false)","Native primitive audit installs before start");
    const auto differences=[&]{const auto stats=appleStats(runtime);if(stats["wider"].integer()!=0)throw std::runtime_error("Platform primitive on the transcript differs from Apple libm by more than one ULP: "+describe(stats["first"]));return stats["oneUlp"].integer();};
    runtime.start(12345);healthy(runtime);audit.frames.push_back(runtime.snapshot());audit.agreed[0]=differences()==0;
    for(int second=0;second<10;++second){transcriptSecond(runtime,second,false);audit.frames.push_back(runtime.snapshot());audit.agreed[std::size_t(second+1)]=differences()==0;}
    const auto stats=appleStats(runtime);std::size_t strict{};while(strict<audit.agreed.size()&&audit.agreed[strict])++strict;
    check(std::is_sorted(audit.agreed.rbegin(),audit.agreed.rend()),"Primitive agreement is a prefix of the transcript");
    auditSummary="native transcript primitives: checked="+std::to_string(stats["agree"].integer()+stats["oneUlp"].integer())+" one-ULP="+std::to_string(stats["oneUlp"].integer())+" unchecked(after divergence)="+std::to_string(stats["unchecked"].integer())+" strict snapshots="+std::to_string(strict)+"/11 first difference="+describe(stats["first"]);
    return audit;
}
void reference(const std::filesystem::path&resources,const J&root,const NativeAudit&audit){
    m::OrbiPomRuntime runtime({resources});check(!runtime.stats().initialized&&runtime.stats().calls==0&&Access::live()==0,"Constructing runtime performs no IO/evaluation/VM allocation");runtime.start(12345);healthy(runtime);check(runtime.snapshot()==audit.frames[0],"Audit wrappers return native primitive results unchanged");compare(snapshot(runtime.snapshot()),root["frames"].array()[0]["snapshot"],"start");
    for(int second=0;second<10;++second){transcriptSecond(runtime,second,true);const auto index=std::size_t(second+1);check(runtime.snapshot()==audit.frames[index],"Production-libm trajectory equals the audited native trajectory bit for bit");compare(snapshot(runtime.snapshot()),root["frames"].array()[index]["snapshot"],std::to_string(second+1),!audit.agreed[index]);}
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
int main(int argc,char**argv){try{if(argc==4&&std::string_view(argv[2])=="--trace"){diagnosticTrace(argv[1],argv[3]);return 0;}check(argc==3,"Pass original Resources root and immutable JavaScriptCore fixture");check(Access::live()==0,"No VM exists before explicit runtime work");const auto transcriptBytes=bytesOf(argv[2],"Immutable original JavaScriptCore transcript opens");const auto transcript=J::parse(transcriptBytes,128*1024);const auto libm=libmTable(std::filesystem::path(argv[2]).parent_path()/"orbipom-libm-source.json",transcriptBytes);primitiveAccuracy(argv[1],libm);appleSubstitution(argv[1],transcript,libm);const auto audit=nativeLibmAudit(argv[1],libm);check(Access::live()==0,"Primitive audit runtimes retire");reference(argv[1],transcript,audit);check(Access::live()==0,"Healthy runtime teardown releases all VM resources");sourceSteps(argv[1],std::filesystem::path(argv[2]).parent_path()/"orbipom-step-source.json");gameplay(argv[1]);cadence(argv[1]);check(Access::live()==0,"All actual skills retire cleanly");limits(argv[1]);integrity();check(Access::live()==0,"Every runtime fixture is fully retired");std::sort(frameSeconds.begin(),frameSeconds.end());std::cout<<"PASS "<<checks<<" actual OrbiPom checks; strict source numbers="<<comparedNumbers<<" maximum residual="<<maximumResidual<<"; Apple-primitive ten-second body coordinates strict="<<substitutedTrajectoryNumbers<<"; "<<primitiveSummary<<"; "<<auditSummary<<"; production-libm coordinates after the first gated 1-ULP primitive difference ("<<observedTrajectoryNumbers<<"): max x="<<trajectoryX<<" y="<<trajectoryY<<" angle="<<trajectoryAngle<<"; frame median="<<frameSeconds[frameSeconds.size()/2]*1000<<"ms p95="<<frameSeconds[frameSeconds.size()*95/100]*1000<<"ms maximum="<<maximumFrameSeconds*1000<<"ms\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
