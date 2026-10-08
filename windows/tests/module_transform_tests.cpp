#include "core/module_transform.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {bool countAllocations{};std::size_t allocations{},checks{};}
void* operator new(std::size_t size){if(countAllocations)++allocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
using namespace endfield::core;
using ehud::data::Json;
namespace {
void check(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
void checkNear(double a,double b,double tolerance,const char* message){check(std::isfinite(a)&&std::abs(a-b)<=tolerance,message);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,"Unsupported or nonfinite transform rejected explicitly");}
ModulePresentationTransform track(MotionPoint direction,bool incoming,double progress){
    return incoming?ModulePresentationTransform{ModuleTransitionStyle::incomingOffset(direction),{},progress,true}:
                    ModulePresentationTransform{{},ModuleTransitionStyle::outgoingOffset(direction),progress,true};
}
void synthetic(){
    const auto m=sampleModuleTransform(track({1,0},true,.5));
    // These values come from genuine CA's paused linear half-way presentation,
    // rather than from a second implementation of the sampler.
    const std::array<double,16> observed{.9968017063026193,0,-.07991469396917338,.00006659557830764448,
        0,1,0,0,.07991469396917339,0,.9968017063026193,-.0008306680885855161,16,0,-27,1.0225};
    for(std::size_t i=0;i<16;++i)checkNear(m.values[i],observed[i],1e-12,"Original half-way perspective/rotation sample");
    check(std::abs(m.values[15]-1.045)>.02,"Perspective-depth term must not use matrix-entry interpolation");
    checkNear(moduleCoreAnimationProgress(.1),.373760461807251,0,"Source CA Float timing sample");
    checkNear(moduleCoreAnimationProgress(.33),.8127523064613342,0,"Float input and control points retained");
    check(moduleCoreAnimationProgress(-3)==0&&moduleCoreAnimationProgress(3)==1,"Finite time clamps at exact endpoints");
    for(const auto direction:{MotionPoint{-1,0},MotionPoint{1,0},MotionPoint{0,-1},MotionPoint{0,1}}){
        const auto incoming=sampleModuleTransform(track(direction,true,1));
        const auto outgoing=sampleModuleTransform(track(direction,false,0));
        check(incoming==Matrix4{}&&outgoing==Matrix4{},"Source identity endpoints remain exact");
        auto stopped=track(direction,false,.7);stopped.animated=false;
        check(sampleModuleTransform(stopped)==sampleModuleTransform(track(direction,false,1)),"Nonanimated tracks use their model endpoint");
    }
    check(sampleModuleTransform({})==Matrix4{},"Settled module has an exact identity transform");
    for(const auto invalid:{-1.,2.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        rejects([&]{(void)sampleModuleTransform(track({1,0},true,invalid));});
    rejects([]{(void)moduleCoreAnimationProgress(std::numeric_limits<double>::infinity());});
    rejects([]{(void)moduleCoreAnimationProgress(std::numeric_limits<double>::quiet_NaN());});
    auto bad=track({1,0},true,.5);bad.to=ModuleTransitionStyle::outgoingOffset({1,0});rejects([&]{(void)sampleModuleTransform(bad);});
    bad=track({1,1},true,.5);rejects([&]{(void)sampleModuleTransform(bad);});
    bad=track({1,0},true,.5);bad.from.perspective=-1./500;rejects([&]{(void)sampleModuleTransform(bad);});
    bad=track({1,0},true,.5);bad.from.x=std::numeric_limits<double>::quiet_NaN();rejects([&]{(void)sampleModuleTransform(bad);});
    // Caller sampling owns no timer/cache and does not allocate per frame.
    countAllocations=true;
    double sum=0;
    for(unsigned i=0;i<=100000;++i){
        const auto progress=moduleCoreAnimationProgress(static_cast<double>(i)/100000);
        check(progress>=0&&progress<=1,"Source timing is bounded across dense finite input");
        const auto value=sampleModuleTransform(track({0,-1},true,progress));sum+=value.values[15];
    }
    countAllocations=false;
    check(sum>100000&&allocations==0,"Dense sampled module transforms retain zero heap allocations");
}
void oracle(const char* path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);check(bool(file),"Explicit original CA oracle opens");
    const auto size=file.tellg();check(size>0&&size<=2*1024*1024,"Oracle JSON remains bounded");
    std::string bytes(static_cast<std::size_t>(size),'\0');file.seekg(0);file.read(bytes.data(),size);check(bool(file),"Complete oracle bytes read");
    const auto data=Json::parse(bytes,2*1024*1024);
    check(data["schemaVersion"].integer()==1&&data["kind"].string()=="actual-core-animation-presentation","Actual presentation sample schema");
    check(data["pausedLayerClock"].boolean()&&data["ownOffscreenNonactivatingPanel"].boolean()&&!data["screenCaptured"].boolean(),"Oracle isolation metadata");
    check(data["maximumRepeatDifference"].number()==0,"Repeated paused actual presentation samples are identical");
    const auto& rows=data["rows"].array();check(rows.size()>=208,"All directions and source phases sampled");
    std::array<unsigned,8> groups{};unsigned sourceRows{},linearRows{};double matrixError{},timingError{},existingTimingError{};
    for(const auto& row:rows){
        const MotionPoint direction{row["direction"].array().at(0).number(),row["direction"].array().at(1).number()};
        const bool incoming=row["incoming"].boolean();const auto phase=row["phase"].number();
        const auto& observed=row["presentation"].array();check(observed.size()==16,"Actual CA matrix has sixteen scalars");
        const auto group=direction.x<0?0u:direction.x>0?1u:direction.y<0?2u:3u;++groups[group*2+(incoming?0:1)];
        // Translation exposes CA's actual eased progress independently of the
        // matrix reconstruction and of any inferred timing-function algorithm.
        const auto actualProgress=incoming?1-observed[14].number()/-54:observed[14].number()/-55;
        auto input=track(direction,incoming,actualProgress);const auto reconstructed=sampleModuleTransform(input);
        for(std::size_t i=0;i<16;++i){
            matrixError=std::max(matrixError,std::abs(reconstructed.values[i]-observed[i].number()));
            checkNear(reconstructed.values[i],observed[i].number(),1e-11,"Decomposed source transform matches genuine CA presentation");
            checkNear(sampleModuleTransform(track(direction,incoming,0)).values[i],row["from"].array()[i].number(),1e-12,"Source from endpoint matches original CATransform3D construction");
            checkNear(sampleModuleTransform(track(direction,incoming,1)).values[i],row["to"].array()[i].number(),1e-12,"Source to endpoint matches original CATransform3D construction");
        }
        if(row["timing"].string()=="source"){
            ++sourceRows;const auto progress=moduleCoreAnimationProgress(phase);
            timingError=std::max(timingError,std::abs(progress-actualProgress));
            existingTimingError=std::max(existingTimingError,std::abs(ModuleTransitionStyle::timing.value(phase)-actualProgress));
            checkNear(progress,actualProgress,1e-14,"Observed source CA Float progress matches sampler");
            const auto final=sampleModuleTransform(track(direction,incoming,progress));
            for(std::size_t i=0;i<16;++i)checkNear(final.values[i],observed[i].number(),1e-11,"End-to-end sampled source timing and transform match CA");
        }else{check(row["timing"].string()=="linear","Only declared control timing used");++linearRows;}
    }
    for(auto n:groups)check(n>=26,"All incoming/outgoing cardinal directions covered");
    check(sourceRows==linearRows,"Source and linear timing independently exercise transform reconstruction");
    std::cout<<"CA oracle rows="<<rows.size()<<" matrix_max_error="<<matrixError<<" timing_max_error="<<timingError
             <<" existing_cubic_progress_max_error="<<existingTimingError<<'\n';
}
}
int main(int argc,char** argv){try{synthetic();if(argc==2)oracle(argv[1]);else check(argc==1,"Pass only one explicit oracle JSON");
    std::cout<<"PASS "<<checks<<" module transform checks\n";return 0;
}catch(const std::exception& e){countAllocations=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
