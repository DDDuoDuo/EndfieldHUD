#include "modules/orbipom_runtime.hpp"
#include "core/shell_packet.hpp"
#include "third_party/quickjs-ng/quickjs.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

namespace endfield::modules {
namespace {
using Clock=std::chrono::steady_clock;
std::atomic<std::size_t>liveVMs{};
struct Value {
    JSContext*ctx{};JSValue value{JS_UNDEFINED};
    Value(JSContext*c,JSValue v):ctx(c),value(v){}
    ~Value(){if(ctx)JS_FreeValue(ctx,value);}
    Value(const Value&)=delete;Value&operator=(const Value&)=delete;
    Value(Value&&other)noexcept:ctx(other.ctx),value(std::exchange(other.value,JS_UNDEFINED)){}
};
struct CString {
    JSContext*context;std::size_t size{};const char*bytes{};
    CString(JSContext*ctx,JSValueConst value):context(ctx),bytes(JS_ToCStringLen(ctx,&size,value)){}
    ~CString(){if(bytes)JS_FreeCString(context,bytes);}
    CString(const CString&)=delete;CString&operator=(const CString&)=delete;
};
enum class Method:std::size_t {start,advance,move,pointerUp,drop,activate,cancelSkill,pause,highScore,snapshot,destroy,count};
constexpr std::array methodNames{"start","advance","move","pointerUp","drop","activate","cancelSkill","pause","highScore","snapshot","destroy"};
constexpr std::array skillNames{"clear","wind","shake","swap"};
bool finite(core::Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
std::string sourceFile(const std::filesystem::path&root,const char*name,std::size_t size,std::string_view hash){
    std::ifstream stream(root/"OrbiPom"/name,std::ios::binary|std::ios::ate);
    if(!stream||stream.tellg()!=std::streamoff(size))throw std::runtime_error(std::string("Missing or invalid original minigame resource: ")+name);
    stream.seekg(0);std::string result(size,'\0');stream.read(result.data(),std::streamsize(size));
    if(stream.gcount()!=std::streamsize(size)||stream.peek()!=std::char_traits<char>::eof()||core::packet::sha256({reinterpret_cast<const std::uint8_t*>(result.data()),result.size()})!=hash)
        throw std::runtime_error(std::string("Original minigame resource integrity check failed: ")+name);
    return result;
}
}
struct OrbiPomRuntime::Impl {
    OrbiPomRuntimeOptions options;std::thread::id owner{std::this_thread::get_id()};
    JSRuntime*runtime{};JSContext*context{};JSValue bridge{JS_UNDEFINED};
    std::array<JSValue,std::size_t(Method::count)>methods;
    OrbiPomSnapshot published,scratch;std::optional<std::string>failure;
    OrbiPomRuntimeStats counters;std::int64_t requestedHighScore{};
    Clock::time_point deadline{};bool inCall{},interrupted{},faulted{};
    explicit Impl(OrbiPomRuntimeOptions value):options(std::move(value)){
        methods.fill(JS_UNDEFINED);
        if(options.resourceRoot.empty()||options.memoryBytes<64*1024||options.memoryBytes>256*1024*1024||!options.snapshotBytes||options.snapshotBytes>options.memoryBytes||options.stackBytes<16*1024||options.stackBytes>256*1024||
           !std::isfinite(options.commandSeconds)||options.commandSeconds<=0||options.commandSeconds>60||!std::isfinite(options.initializationSeconds)||options.initializationSeconds<=0||options.initializationSeconds>60)
            throw std::invalid_argument("Invalid minigame runtime resource limits");
    }
    void thread()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("Minigame runtime belongs to its creating thread");}
    static int interrupt(JSRuntime*,void*opaque){auto&self=*static_cast<Impl*>(opaque);if(self.inCall&&Clock::now()>=self.deadline){self.interrupted=true;++self.counters.interrupts;return 1;}return 0;}
    struct Budget {
        Impl&self;Clock::time_point began{Clock::now()};
        Budget(Impl&s,double seconds):self(s){if(self.inCall)throw std::logic_error("Reentrant minigame command");self.inCall=true;self.interrupted=false;self.deadline=began+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));}
        ~Budget(){const auto seconds=std::chrono::duration<double>(Clock::now()-began).count();self.counters.totalCommandSeconds+=seconds;self.counters.maximumCommandSeconds=std::max(self.counters.maximumCommandSeconds,seconds);self.inCall=false;}
    };
    std::string exception(){
        Value error(context,JS_GetException(context));
        if(interrupted)return "Original minigame simulation exceeded its command time budget.";
        CString text(context,error.value);return text.bytes?std::string(text.bytes,std::min<std::size_t>(text.size,4096)):"Original minigame simulation failed (memory or stack limit).";
    }
    void checked(JSValueConst value){if(JS_IsException(value))throw std::runtime_error(exception());}
    Value property(JSValueConst object,const char*key){Value result(context,JS_GetPropertyStr(context,object,key));checked(result.value);return result;}
    double number(JSValueConst object,const char*key,double fallback=0){
        auto value=property(object,key);if(JS_IsUndefined(value.value)||JS_IsNull(value.value))return fallback;
        double result{};if(!JS_IsNumber(value.value)||JS_ToFloat64(context,&result,value.value)<0||!std::isfinite(result))throw std::runtime_error(std::string("Invalid original minigame number: ")+key);return result;
    }
    std::int64_t integer(JSValueConst object,const char*key,std::int64_t fallback=0){const auto value=number(object,key,double(fallback));if(std::abs(value)>9007199254740991.||std::trunc(value)!=value)throw std::runtime_error("Invalid original minigame integer");return std::int64_t(value);}
    int smallInteger(JSValueConst object,const char*key,int fallback=0){const auto value=integer(object,key,fallback);if(value<std::numeric_limits<int>::min()||value>std::numeric_limits<int>::max())throw std::runtime_error("Invalid original minigame small integer");return int(value);}
    bool boolean(JSValueConst object,const char*key){auto value=property(object,key);if(JS_IsUndefined(value.value)||JS_IsNull(value.value))return false;if(!JS_IsBool(value.value))throw std::runtime_error("Invalid original minigame boolean");return JS_ToBool(context,value.value)!=0;}
    std::optional<double>optionalNumber(JSValueConst object,const char*key){auto value=property(object,key);if(JS_IsNull(value.value)||JS_IsUndefined(value.value))return {};double result{};if(!JS_IsNumber(value.value)||JS_ToFloat64(context,&result,value.value)<0||!std::isfinite(result))throw std::runtime_error("Invalid original minigame optional number");return result;}
    std::optional<std::int64_t>optionalID(JSValueConst object,const char*key){const auto value=optionalNumber(object,key);if(!value)return {};if(std::abs(*value)>9007199254740991.||std::trunc(*value)!=*value)throw std::runtime_error("Invalid original minigame body ID");return std::int64_t(*value);}
    std::optional<std::string>string(JSValueConst object,const char*key){
        auto value=property(object,key);if(JS_IsNull(value.value)||JS_IsUndefined(value.value))return {};
        if(!JS_IsString(value.value))throw std::runtime_error("Invalid original minigame text");
        CString text(context,value.value);if(!text.bytes)throw std::runtime_error(exception());
        if(text.size>64)throw std::runtime_error("Invalid original minigame state text size");
        return std::string(text.bytes,text.size);
    }
    std::size_t arraySize(JSValueConst value,std::size_t limit){if(!JS_IsArray(value))throw std::runtime_error("Invalid original minigame array");const auto size=integer(value,"length");if(size<0||std::uint64_t(size)>limit)throw std::runtime_error("Original minigame snapshot exceeds its resource budget.");return std::size_t(size);}
    void interaction(JSValueConst value){
        const auto x=number(value,"previewX",published.previewX),y=number(value,"previewY",published.previewY),size=number(value,"previewSize",published.previewSize),scale=number(value,"previewScale",published.previewScale);
        const auto visible=boolean(value,"previewVisible");const auto hover=optionalID(value,"hoverBodyID");
        published.previewX=x;published.previewY=y;published.previewSize=size;published.previewScale=scale;published.previewVisible=visible;published.hoverBodyID=hover;
    }
    void snapshot(JSValueConst value){
        if(!JS_IsObject(value))throw std::runtime_error("Original minigame snapshot is unavailable");
        auto&s=scratch;s.state=string(value,"state").value_or("idle");s.paused=boolean(value,"paused");
        if(s.state!="idle"&&s.state!="playing"&&s.state!="over")throw std::runtime_error("Unknown original minigame state");
        s.score=integer(value,"score");s.highScore=integer(value,"highScore");s.energy=smallInteger(value,"energy");s.swapCharge=smallInteger(value,"swapCharge");
        s.mergeCount=integer(value,"mergeCount");s.skillUseCount=integer(value,"skillUseCount");s.energyProgress=number(value,"energyProgress");
        s.currentLevel=smallInteger(value,"currentLevel",1);s.nextLevel=smallInteger(value,"nextLevel",1);
        s.previewX=number(value,"previewX");s.previewY=number(value,"previewY");s.previewSize=number(value,"previewSize");s.previewScale=number(value,"previewScale",1);s.previewVisible=boolean(value,"previewVisible");
        s.dangerSeconds=optionalNumber(value,"dangerSeconds");s.skill.reset();
        if(const auto name=string(value,"skill")){const auto found=std::find(skillNames.begin(),skillNames.end(),*name);if(found==skillNames.end())throw std::runtime_error("Unknown original minigame skill");s.skill=static_cast<OrbiPomSkill>(found-skillNames.begin());}
        s.skillPhase=string(value,"skillPhase");s.hoverBodyID=optionalID(value,"hoverBodyID");s.windSurfaceY=optionalNumber(value,"windSurfaceY");s.simulationTime=number(value,"simulationTime");
        auto selected=property(value,"selectedBodyIDs");const auto selectedCount=arraySize(selected.value,2);s.selectedBodyIDs.clear();
        for(std::size_t index=0;index<selectedCount;++index){Value item(context,JS_GetPropertyUint32(context,selected.value,std::uint32_t(index)));checked(item.value);double id{};if(!JS_IsNumber(item.value)||JS_ToFloat64(context,&id,item.value)<0||!std::isfinite(id)||std::abs(id)>9007199254740991.||std::trunc(id)!=id)throw std::runtime_error("Invalid selected minigame body");s.selectedBodyIDs.push_back(std::int64_t(id));}
        auto bodies=property(value,"bodies");const auto count=arraySize(bodies.value,options.snapshotBytes/sizeof(OrbiPomBody));if(count>s.bodies.capacity())s.bodies.reserve(count);s.bodies.resize(count);
        for(std::size_t index=0;index<count;++index){if(index%64==0&&interrupt(runtime,this))throw std::runtime_error("Original minigame snapshot exceeded its command time budget.");Value item(context,JS_GetPropertyUint32(context,bodies.value,std::uint32_t(index)));checked(item.value);auto&b=s.bodies[index];
            b={integer(item.value,"id"),smallInteger(item.value,"level"),number(item.value,"x"),number(item.value,"y"),number(item.value,"angle"),number(item.value,"size"),number(item.value,"opacity",1),number(item.value,"scale",1)};
            if(b.level<1||b.level>11||b.size<=0||b.scale<0||b.opacity<0||b.opacity>1)throw std::runtime_error("Invalid original minigame body metadata");
        }
        std::swap(published,scratch);
    }
    Value call(Method method,std::span<const JSValue>arguments={}){if(arguments.size()>2)throw std::logic_error("Invalid minigame command arity");std::array<JSValue,2>argv{};std::copy(arguments.begin(),arguments.end(),argv.begin());++counters.calls;Value result(context,JS_Call(context,methods[std::size_t(method)],bridge,int(arguments.size()),argv.data()));checked(result.value);return result;}
    void refresh(){auto value=call(Method::snapshot);snapshot(value.value);}
    bool boot(){
        if(runtime)return false;
        Budget budget(*this,options.initializationSeconds);
        const auto matter=sourceFile(options.resourceRoot,"matter-0.20.0.js",83318,"928d059868201b2c4c270818fda44e5d26c099e4df407043f018f9bbe2c598cf");
        const auto game=sourceFile(options.resourceRoot,"orbipom.js",94724,"1c68da4d2165f3d6d45e8842ab4d7d6e766bb3d738e3ff19f32ca2dce64e4054");
        runtime=JS_NewRuntime();if(!runtime)throw std::runtime_error("Could not create the original minigame VM");++liveVMs;++counters.boots;
        JS_SetMemoryLimit(runtime,options.memoryBytes);JS_SetMaxStackSize(runtime,options.stackBytes);JS_SetCanBlock(runtime,false);JS_SetInterruptHandler(runtime,interrupt,this);
        context=JS_NewContext(runtime);if(!context)throw std::runtime_error("Original minigame VM exceeded its initialization memory budget.");
        for(const auto&item:std::array<std::pair<std::string_view,const char*>,2>{{{matter,"matter-0.20.0.js"},{game,"orbipom.js"}}}){Value result(context,JS_Eval(context,item.first.data(),item.first.size(),item.second,JS_EVAL_TYPE_GLOBAL));checked(result.value);}
        Value global(context,JS_GetGlobalObject(context));bridge=JS_GetPropertyStr(context,global.value,"OrbiPom");checked(bridge);if(!JS_IsObject(bridge))throw std::runtime_error("Missing original offline minigame bridge");
        for(std::size_t i=0;i<methods.size();++i){methods[i]=JS_GetPropertyStr(context,bridge,methodNames[i]);checked(methods[i]);if(!JS_IsFunction(context,methods[i]))throw std::runtime_error("Incomplete original offline minigame bridge");}
        return true;
    }
    template<class F>bool command(F&&fn){thread();if(faulted)return false;try{boot();Budget budget(*this,options.commandSeconds);fn();return true;}catch(const std::exception&e){failure=e.what();faulted=true;return false;}}
    void close()noexcept{
        if(!runtime)return;
        if(context){
            if(!faulted&&JS_IsFunction(context,methods[std::size_t(Method::destroy)])){try{Budget budget(*this,options.commandSeconds);auto result=call(Method::destroy);}catch(...) {}}
            for(auto&method:methods){JS_FreeValue(context,method);method=JS_UNDEFINED;}JS_FreeValue(context,bridge);bridge=JS_UNDEFINED;JS_FreeContext(context);context=nullptr;
        }
        JS_FreeRuntime(runtime);runtime=nullptr;--liveVMs;
    }
    ~Impl(){close();}
};
OrbiPomRuntime::OrbiPomRuntime(OrbiPomRuntimeOptions options):impl_(std::make_unique<Impl>(std::move(options))){}
OrbiPomRuntime::~OrbiPomRuntime()=default;
const OrbiPomSnapshot&OrbiPomRuntime::snapshot()const noexcept{return impl_->published;}
const std::optional<std::string>&OrbiPomRuntime::error()const noexcept{return impl_->failure;}
void OrbiPomRuntime::start(std::optional<std::uint32_t>seed){auto&i=*impl_;i.thread();if(i.faulted){i.close();i.faulted=false;}i.failure.reset();i.command([&]{const std::array high{JS_NewInt64(i.context,i.requestedHighScore)};{auto result=i.call(Method::highScore,high);}const std::array args{seed?JS_NewUint32(i.context,*seed):JS_NULL};auto result=i.call(Method::start,args);i.snapshot(result.value);});}
void OrbiPomRuntime::advance(double seconds){auto&i=*impl_;i.thread();if(!i.published.canAdvance()||!std::isfinite(seconds)||seconds<=0||i.faulted)return;i.command([&]{++i.counters.advances;const std::array args{JS_NewFloat64(i.context,seconds)};auto result=i.call(Method::advance,args);i.snapshot(result.value);});}
void OrbiPomRuntime::move(core::Point p){auto&i=*impl_;i.thread();if(!finite(p)||!i.published.canAdvance()||i.faulted)return;i.command([&]{const std::array args{JS_NewFloat64(i.context,p.x),JS_NewFloat64(i.context,p.y)};auto result=i.call(Method::move,args);i.interaction(result.value);});}
void OrbiPomRuntime::pointerUp(core::Point p){auto&i=*impl_;i.thread();if(!finite(p)||!i.published.canAdvance()||i.faulted)return;i.command([&]{const std::array args{JS_NewFloat64(i.context,p.x),JS_NewFloat64(i.context,p.y)};{auto result=i.call(Method::pointerUp,args);}i.refresh();});}
bool OrbiPomRuntime::drop(){auto&i=*impl_;bool accepted{};i.command([&]{{auto result=i.call(Method::drop);accepted=JS_ToBool(i.context,result.value)>0;}i.refresh();});return accepted&&!i.faulted;}
bool OrbiPomRuntime::activate(OrbiPomSkill skill){auto&i=*impl_;const auto index=std::size_t(skill);if(index>=skillNames.size())return false;bool accepted{};i.command([&]{Value name(i.context,JS_NewString(i.context,skillNames[index]));i.checked(name.value);const std::array args{name.value};{auto result=i.call(Method::activate,args);accepted=JS_ToBool(i.context,result.value)>0;}i.refresh();});return accepted&&!i.faulted;}
void OrbiPomRuntime::cancelSkill(){auto&i=*impl_;i.command([&]{{auto result=i.call(Method::cancelSkill);}i.refresh();});}
void OrbiPomRuntime::pause(bool paused){auto&i=*impl_;i.command([&]{const std::array args{JS_NewBool(i.context,paused)};{auto result=i.call(Method::pause,args);}i.refresh();});}
void OrbiPomRuntime::highScore(std::int64_t score){auto&i=*impl_;i.thread();i.requestedHighScore=score;i.command([&]{const std::array args{JS_NewInt64(i.context,score)};{auto result=i.call(Method::highScore,args);}i.refresh();});}
OrbiPomRuntimeStats OrbiPomRuntime::stats()const{auto&i=*impl_;i.thread();auto result=i.counters;result.initialized=i.context!=nullptr;result.faulted=i.faulted;result.snapshotCapacityBytes=(i.published.bodies.capacity()+i.scratch.bodies.capacity())*sizeof(OrbiPomBody)+(i.published.selectedBodyIDs.capacity()+i.scratch.selectedBodyIDs.capacity())*sizeof(std::int64_t);if(i.runtime){JSMemoryUsage memory{};JS_ComputeMemoryUsage(i.runtime,&memory);result.heapBytes=std::size_t(std::max<std::int64_t>(0,memory.malloc_size));}return result;}
std::string OrbiPomRuntime::evaluateForTesting(std::string_view script){auto&i=*impl_;std::string output;i.command([&]{Value result(i.context,JS_Eval(i.context,std::string(script).c_str(),script.size(),"isolated-orbipom-test",JS_EVAL_TYPE_GLOBAL));i.checked(result.value);Value json(i.context,JS_JSONStringify(i.context,result.value,JS_UNDEFINED,JS_UNDEFINED));i.checked(json.value);if(!JS_IsUndefined(json.value)){CString text(i.context,json.value);if(!text.bytes)throw std::runtime_error(i.exception());if(text.size>128*1024)throw std::runtime_error("Test result exceeds fixture budget");output.assign(text.bytes,text.size);}i.refresh();});return output;}
std::size_t OrbiPomRuntime::liveVMsForTesting()noexcept{return liveVMs.load();}
}
