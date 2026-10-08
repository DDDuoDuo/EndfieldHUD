#include "core/subsection_mask.hpp"
#include "core/data/json.hpp"
#include "core/shell_packet.hpp"
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};unsigned checks{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace c=endfield::core;using Json=ehud::data::Json;
void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}template<class F>void rejects(F f){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,"Malformed source mask must reject");}
int main(int argc,char**argv){try{
    check(argc==3||(argc==4&&std::string_view(argv[3])=="--explicit-viewport"),"Pass pinned mask binary and original Mac evidence JSON [--explicit-viewport]");std::ifstream file(argv[1],std::ios::binary);std::vector<std::uint8_t>bytes((std::istreambuf_iterator<char>(file)),{});
    std::ifstream evidenceFile(argv[2]);const std::string text((std::istreambuf_iterator<char>(evidenceFile)),{});const auto evidence=Json::parse(text);
    const bool explicitViewport=argc==4;const auto pin=explicitViewport?evidence["candidateSHA256"].string():std::string(c::SubsectionMaskSampler::assetSHA256);
    c::Rect viewport=c::SubsectionMaskSampler::viewport;if(explicitViewport){const auto&a=evidence["viewport"].array();check(a.size()==4,"Source fixture viewport shape");viewport={a[0].number(),a[1].number(),a[2].number(),a[3].number()};}
    check(bytes.size()==(explicitViewport?std::size_t(evidence["candidateBytes"].integer()):98856),"Exact bounded original candidate bytes");
    auto owned=explicitViewport?std::make_unique<c::SubsectionMaskSampler>(bytes,pin,viewport):std::make_unique<c::SubsectionMaskSampler>(bytes,pin);const auto&sampler=*owned;check(sampler.sourceViewport()==viewport,"Retain caller-verified source viewport");
    auto wrongViewport=viewport;wrongViewport.width+=1;rejects([&]{c::SubsectionMaskSampler bad(bytes,pin,wrongViewport);});
    if(explicitViewport)rejects([&]{c::SubsectionMaskSampler bad(bytes,pin);});
    const auto malformed=[&](std::vector<std::uint8_t>copy){const auto digest=c::packet::sha256(copy);rejects([&]{c::SubsectionMaskSampler bad(copy,digest,viewport);});};
    {auto copy=bytes;copy[0]='X';malformed(std::move(copy));}
    {auto copy=bytes;copy[56]=255;malformed(std::move(copy));}
    {auto copy=bytes;for(unsigned n=60;n<64;++n)copy[n]=0;malformed(std::move(copy));}
    {auto copy=bytes;copy[64]=4;malformed(std::move(copy));}
    {auto copy=bytes;copy.push_back(0);malformed(std::move(copy));}
    auto corrupt=bytes;corrupt.back()^=1;rejects([&]{c::SubsectionMaskSampler bad(corrupt,pin,viewport);});rejects([&]{c::SubsectionMaskSampler bad(bytes,"untrusted",viewport);});bytes.pop_back();rejects([&]{c::SubsectionMaskSampler bad(bytes,pin,viewport);});
    const auto&w=evidence["independentWorst"];const auto oracle=sampler.sample(w["direction"].number(),w["phase"].number());
    check(oracle.opcodeCount==w["opcodes"].array().size()&&oracle.coordinateCount==w["candidateCoordinates"].array().size(),"Actual original-CA holdout topology retained");
    for(std::size_t n=0;n<oracle.opcodeCount;++n)check(oracle.opcodes[n]==w["opcodes"].array()[n].integer(),"Exact original holdout opcode");
    for(std::size_t n=0;n<oracle.coordinateCount;++n)check(oracle.coordinates[n]==w["candidateCoordinates"].array()[n].number(),"Bitwise Float32 original measured candidate coordinate");
    for(const auto&gap:evidence["discontinuityBrackets"].array()){const auto sample=sampler.sample(gap["direction"].number(),(gap["lo"].number()+gap["hi"].number())*.5);check(sample.topologyGap,"Sub-picosecond phase gap is explicitly flagged");}
    for(double bad:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){rejects([&]{sampler.sample(1,bad);});rejects([&]{sampler.sample(bad,.5);});}
    allocations=0;counting=true;for(int direction:{-1,1})for(unsigned n=0;n<=10000;++n){const auto p=sampler.sample(direction,double(n)/10000);check(p.opcodeCount<=40&&p.coordinateCount<=192,"Fixed source path budget");for(std::size_t k=0;k<p.coordinateCount;++k)check(std::isfinite(p.coordinates[k]),"Finite sampled source coordinate");}counting=false;check(allocations==0,"20002 finite samples allocate no memory");
    check(sampler.sample(1,-1)==sampler.sample(1,0)&&sampler.sample(-1,2)==sampler.sample(-1,1),"Finite phases clamp at source endpoints");
    std::cout<<"PASS "<<checks<<" source subsection mask checks\n";return 0;
}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
