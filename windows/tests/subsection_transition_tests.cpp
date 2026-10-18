#include "core/subsection_transition.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {bool countAllocations{};std::size_t allocations{},checks{};}
void* operator new(std::size_t n){if(countAllocations)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
using namespace endfield::core;using ehud::data::Json;
namespace {
void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
void near(double a,double b,double tolerance,const char* text){check(std::isfinite(a)&&std::abs(a-b)<=tolerance,text);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,"Invalid subsection inputs reject explicitly");}
Rect rectangle(const Json& json){const auto& a=json.array();check(a.size()==4,"Oracle viewport has four components");return {a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
void synthetic(){
    const auto start=sampleSubsectionTransform(-1,0);check(start.active,"Reveal begins immediately with authored offset");
    near(start.sublayerTransform.values[12],-18,0,"Source negative direction translation");near(start.sublayerTransform.values[14],-16,0,"Source depth");
    near(start.sublayerTransform.values[11],-1./720,0,"Source perspective");near(start.sublayerTransform.values[15],1+16./720,1e-15,"Source composed perspective/depth");
    check(sampleSubsectionTransform(0,0).sublayerTransform==sampleSubsectionTransform(99,0).sublayerTransform,"Zero direction uses positive source sign");
    check(sampleSubsectionTransform(1,-5).sublayerTransform==sampleSubsectionTransform(1,0).sublayerTransform,"Finite negative time clamps to first pose");
    for(double time:{0.,.1,.26,20.})check(sampleSubsectionTransform(1,time,false).sublayerTransform==Matrix4{}&&!sampleSubsectionTransform(1,time,false).active,"Nonanimated source reveal settles to model identity");
    check(!sampleSubsectionTransform(1,.26).active&&sampleSubsectionTransform(1,.26).sublayerTransform==Matrix4{},"Finite completion removes source motion");
    // Read from the real source oracle; this is not a second timing formula.
    near(subsectionCoreAnimationProgress(.1),.4262858033180237,0,"Genuine CA Float progress at one tenth");
    const auto key=subsectionMaskKeyframe({9,40,382,248},-1,1);
    near(key[0][0].x,12.1,0,"Original source cut at second authored keyframe");near(key[0][1].x,160.272,1e-12,"Original lagged strip width");
    for(unsigned k=0;k<4;++k){const auto left=subsectionMaskKeyframe({9,40,382,248},-1,k),right=subsectionMaskKeyframe({9,40,382,248},1,k);
        for(unsigned strip=0;strip<4;++strip)near(left[strip][1].x-left[strip][4].x,right[strip][1].x-right[strip][4].x,1e-12,"Source direction changes edge without changing strip width");}
    const auto end=subsectionMaskKeyframe({9,40,382,248},1,3);near(end[0][0].x,9,0,"Final authored mask spans full viewport");near(end[3][3].y,288,0,"Final authored mask covers all four bands");
    for(double invalid:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){
        rejects([&]{(void)subsectionCoreAnimationProgress(invalid);});rejects([&]{(void)sampleSubsectionTransform(invalid,0);});rejects([&]{(void)sampleSubsectionTransform(1,invalid);});rejects([&]{(void)subsectionMaskKeyframe({0,0,400,334},invalid,0);});}
    rejects([]{(void)subsectionMaskKeyframe({0,0,0,334},1,0);});rejects([]{(void)subsectionMaskKeyframe({0,0,400,334},1,4);});
    rejects([]{(void)subsectionMaskKeyframe({std::numeric_limits<double>::max(),0,std::numeric_limits<double>::max(),334},1,0);});
    countAllocations=true;double sum{};
    for(unsigned i=0;i<=100000;++i){const auto time=double(i)/100000;const auto p=subsectionCoreAnimationProgress(time);check(p>=0&&p<=1,"Dense finite source timing remains bounded");
        sum+=sampleSubsectionTransform(-1,time*.26).sublayerTransform.values[15];sum+=subsectionMaskKeyframe({9,40,382,248},1,i%4)[0][0].x;}
    countAllocations=false;check(sum>100000&&allocations==0,"Subsection transform/keyframe sampling performs no heap allocations");
}
void oracle(const char* path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);check(bool(file),"Explicit source oracle opens");const auto size=file.tellg();check(size>0&&size<=4*1024*1024,"Bounded source oracle");
    std::string bytes(static_cast<std::size_t>(size),'\0');file.seekg(0);file.read(bytes.data(),size);check(bool(file),"Complete source oracle read");const auto data=Json::parse(bytes,4*1024*1024);
    check(data["schemaVersion"].integer()==1&&data["kind"].string()=="actual-subsection-core-animation-presentation","Actual CA subsection schema");
    check(data["pausedLayerClock"].boolean()&&data["ownOffscreenNonactivatingPanel"].boolean()&&!data["screenCaptured"].boolean()&&!data["handoffInstantiated"].boolean(),"Actual source oracle remains isolated");
    check(data["maximumRepeatDifference"].number()==0,"Paused presentation is deterministic on repeat");near(data["sourceDuration"].number(),.26,0,"Original source duration");
    const auto& frames=data["keyframes"].array();check(frames.size()==6,"Three viewports in both source directions");double geometryError{},matrixError{},timingError{};
    for(const auto& frame:frames){const auto viewport=rectangle(frame["viewport"]);const auto direction=frame["direction"].number();
        check(!frame["maskHasGlobalTiming"].boolean(),"Mask uses per-segment timing without a global easing curve");
        for(std::size_t k=0;k<4;++k){near(frame["keyTimes"].array()[k].number(),SubsectionTransitionStyle::maskKeyTimes[k],0,"Exact authored mask key times");const auto expected=subsectionMaskKeyframe(viewport,direction,k);const auto& points=frame["paths"].array()[k].array();check(points.size()==24,"Authored source keyframe has four hexagons");
            for(std::size_t strip=0;strip<4;++strip)for(std::size_t vertex=0;vertex<6;++vertex){const auto& p=points[strip*6+vertex].array();const auto& value=expected[strip][vertex];geometryError=std::max({geometryError,std::abs(value.x-p[0].number()),std::abs(value.y-p[1].number())});near(value.x,p[0].number(),1e-12,"Authored mask X matches unchanged original Swift");near(value.y,p[1].number(),1e-12,"Authored mask Y matches unchanged original Swift");}}
    }
    const auto& rows=data["rows"].array();check(rows.size()==666,"Dense and key-boundary source sample matrix is complete");std::size_t curvedRows{};
    for(const auto& row:rows){const auto direction=row["direction"].number(),phase=row["phase"].number();const auto actualProgress=1-row["presentation"].array()[14].number()/-16;
        const auto progress=subsectionCoreAnimationProgress(phase);timingError=std::max(timingError,std::abs(progress-actualProgress));near(progress,actualProgress,1e-14,"Source Float timing matches genuine CA");
        const auto value=sampleSubsectionTransform(direction,phase*.26).sublayerTransform;const auto& actual=row["presentation"].array();check(actual.size()==16,"Source matrix preserves sixteen components");
        for(std::size_t i=0;i<16;++i){matrixError=std::max(matrixError,std::abs(value.values[i]-actual[i].number()));near(value.values[i],actual[i].number(),1e-11,"Source decomposed transform matches genuine CA");}
        bool curved=false;for(const auto& command:row["pathCommands"].array()){const auto& c=command.array();check(!c.empty(),"Raw CA mask command retained");const auto type=c[0].integer();const auto expected=type==0||type==1?3u:type==2?5u:type==3?7u:type==4?1u:0u;check(expected&&c.size()==expected,"Raw CA mask preserves full control point topology");for(const auto& x:c)check(std::isfinite(x.number()),"Raw CA mask coordinates remain finite");curved|=type==2||type==3;}curvedRows+=curved;
    }
    check(curvedRows>500,"Oracle retains genuine curved interiors instead of claiming polygon interpolation parity");
    std::cout<<"Original CA rows="<<rows.size()<<" authored_mask_error="<<geometryError<<" matrix_error="<<matrixError<<" timing_error="<<timingError<<" curved_rows="<<curvedRows<<'\n';
}
}
int main(int argc,char** argv){try{synthetic();if(argc==2)oracle(argv[1]);else check(argc==1,"Pass at most one explicit original oracle JSON");std::cout<<"PASS "<<checks<<" subsection checks\n";return 0;}catch(const std::exception& e){countAllocations=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
