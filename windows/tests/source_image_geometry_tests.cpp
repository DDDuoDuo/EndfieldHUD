#include "core/source_image_geometry.hpp"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {bool countAllocations=false;std::size_t allocations=0;}
void* operator new(std::size_t n){if(countAllocations)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

using namespace endfield::core::source;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,message);}
SourceSprite sprite(){SourceSprite s;s.size={100,100};s.border={10,10,10,10};s.outer={0,0,1,1};s.inner={.1,.1,.9,.9};s.textureID="synthetic.texture-long-enough-to-exercise-retained-string-storage";return s;}
double area(const SourceImageMesh& m){double total=0;for(std::size_t i=0;i<m.indices.size();i+=3){const auto&a=m.positions[m.indices[i]],&b=m.positions[m.indices[i+1]],&c=m.positions[m.indices[i+2]];total+=std::abs(double((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])))*.5;}return total;}
void synthetic(){
    ImageGeometryBuilder builder;SourceImageParameters parameters;auto s=sprite();SourceRect rect{{0,0},{100,100}};
    const auto& simple=builder.build(parameters,&s,rect,{.5,.5});check(simple.positions.size()==4&&simple.indices==std::vector<std::uint32_t>{0,1,2,2,3,0},"Simple image preserves source negative-Z triangle order");
    check(simple.positions[2]==std::array<float,4>{100,100,0,1},"Source positions retain Float4 z/w convention");
    parameters.type=1;parameters.fillCenter=false;const auto& border=builder.build(parameters,&s,rect,{.5,.5});check(border.positions.size()==32&&area(border)==3600,"Nine-slice omits only the center and keeps its exact border area");
    parameters.type=3;parameters.fillMethod=4;
    for(int origin=0;origin<4;++origin)for(bool clockwise:{false,true}){parameters.fillOrigin=origin;parameters.fillClockwise=clockwise;const auto&m=builder.build(parameters,&s,rect,{.5,.5},100,.25);check(area(m)==2500,"All radial360 directions/origins fill one quarter at25percent");}
    check(builder.build(parameters,&s,rect,{.5,.5},100,.00099).positions.empty(),"Fill below the authored threshold emits nothing");
    check(!builder.build(parameters,&s,rect,{.5,.5},100,.001).positions.empty(),"Exact fill threshold still emits authored radial geometry");
    parameters={};parameters.preserveAspect=true;s.size={200,100};const auto&m=builder.build(parameters,&s,rect,{0,1});
    check(m.positions[0][1]==50&&m.positions[2][1]==100,"Preserve-aspect placement uses the authored pivot");
    parameters.useSpriteMesh=true;check(builder.build(parameters,&s,{{0,0},{0,100}},{.5,.5}).indices.empty(),"Empty rect returns before the source sprite-mesh flag check");
    rejects([&]{builder.build(parameters,&s,rect,{.5,.5});},"Unexpected tight sprite-mesh flag is rejected");
    check(builder.mesh().positions.empty()&&builder.mesh().uv.empty()&&builder.mesh().indices.empty(),"Failed build exposes no partial mesh");
    parameters={};parameters.type=2;s=sprite();s.size={1,1};s.border={};s.inner=s.outer;
    check(builder.build(parameters,&s,{{0,0},{125,130}},{0,0}).positions.size()==65000,"Authored tile cell limit admits exactly65000 vertices");
    rejects([&]{builder.build(parameters,&s,{{0,0},{1,16251}},{0,0});},"One additional tile rejects before unbounded growth");
    rejects([&]{builder.build(parameters,&s,{{0,0},{1e300,1e300}},{0,0});},"Overflowing tile counts are checked before integer conversion");
    parameters={};rect={{-10.25,-20.5},{70.75,90.125}};s=sprite();builder.build(parameters,&s,rect,{.5,.5});
    auto before=builder.stats();const auto* positionBuffer=builder.mesh().positions.data();allocations=0;countAllocations=true;
    for(unsigned i=0;i<120;++i)builder.build(parameters,&s,rect,{.5,.5});countAllocations=false;
    check(allocations==0&&builder.stats().builds==before.builds&&builder.stats().reuses==before.reuses+120,"Identical geometry inputs reuse buffers without allocation/rebuild");
    check(builder.mesh().positions.data()==positionBuffer,"Camera-only frames keep the same local image vertices");
    parameters.type=3;parameters.fillMethod=4;builder.build(parameters,&s,rect,{.5,.5},100,.99); // warm maximum four-quadrant capacity
    allocations=0;countAllocations=true;for(unsigned i=0;i<120;++i)builder.build(parameters,&s,rect,{.5,.5},100,.01+i*.008);countAllocations=false;
    check(allocations==0,"Changing fill geometry retains both vector capacity and sprite-key storage");
    before=builder.stats();builder.reset();check(builder.mesh().positions.empty()&&builder.stats().retainedBytes==before.retainedBytes,"Reset clears geometry while retaining scratch capacity");
    builder.release();check(builder.stats().retainedBytes==0,"Explicit release drops retained geometry capacity");
    parameters={};auto invalid=s;invalid.size[0]=std::numeric_limits<double>::infinity();rejects([&]{builder.build(parameters,&invalid,rect,{0,0});},"Nonfinite sprite rejected");
    rejects([&]{builder.build(parameters,&s,rect,{std::numeric_limits<double>::quiet_NaN(),0});},"Nonfinite pivot rejected");
    rejects([&]{builder.build(parameters,&s,rect,{0,0},100,std::numeric_limits<double>::infinity());},"Nonfinite fill override rejected");
    rejects([&]{builder.build(parameters,&s,{{1e100,0},{100,100}},{0,0});},"Finite Double outside Float range cannot enter the GPU mesh");
    parameters.type=1;rejects([&]{builder.build(parameters,&s,rect,{0,0},0);},"Infinite derived pixels-per-unit rejected");
    parameters.type=3;parameters.fillMethod=4;parameters.fillOrigin=-1;rejects([&]{builder.build(parameters,&s,rect,{0,0},100,.5);},"Negative radial origin cannot index outside the quad");
}
void oracle(std::istream& inputStream){
    // Each case is independently bounded; the65000-vertex limit fixture must
    // not require weakening the production JSON parser's1M-node limit.
    std::string line;check(bool(std::getline(inputStream,line)),"Oracle header exists");const auto fixture=Json::parse(line);
    check(fixture["sourceSHA256"].string()=="47b0f620846abb6afb0d2cb6bece0b38009a3c5ca85b9700e0bccb4912f58683","Oracle records unchanged authoritative Swift geometry source");
    check(fixture["actualSourceCases"].integer()>=469,"Oracle covers mounted source image metadata, including profile images");
    ImageGeometryBuilder builder;std::size_t cases=0,errorCases=0;
    while(std::getline(inputStream,line)){
        const auto row=Json::parse(line,16*1024*1024);
        ++cases;const auto& input=row["input"];const auto name=row["name"].string();
        try{
            WatchComponent component{"synthetic.image","MonoBehaviour","UIImage",input["image"]};const auto parameters=SourceImageParameters::fromComponent(component);
            std::optional<SourceSprite> sprite;if(!input["sprite"].isNull())sprite=SourceSprite::fromSource(input["sprite"],input["texture"]);
            const auto& r=input["rect"];const SourceRect rect{{r["origin"].array()[0].number(),r["origin"].array()[1].number()},{r["size"].array()[0].number(),r["size"].array()[1].number()}};
            const auto& p=input["pivot"].array();std::optional<double> fill;if(!input["fillAmount"].isNull())fill=input["fillAmount"].number();
            const auto& mesh=builder.build(parameters,sprite?&*sprite:nullptr,rect,{p[0].number(),p[1].number()},input["canvasReferencePPU"].number(),fill);
            if(!row["error"].isNull())throw std::runtime_error("C++ unexpectedly accepted Swift error case "+name);
            const auto& expected=row["expected"];check(mesh.positions.size()==expected["positions"].array().size()&&mesh.uv.size()==expected["uv"].array().size()&&mesh.indices.size()==expected["indices"].array().size(),"Original Swift topology count matches");
            for(std::size_t i=0;i<mesh.positions.size();++i)for(unsigned c=0;c<4;++c){++checks;const auto bits=std::bit_cast<std::uint32_t>(mesh.positions[i][c]),wanted=static_cast<std::uint32_t>(expected["positions"].array()[i].array()[c].integer());if(bits!=wanted)throw std::runtime_error("Float position mismatch "+name+" vertex "+std::to_string(i)+" channel "+std::to_string(c)+" bits "+std::to_string(bits)+" expected "+std::to_string(wanted));}
            for(std::size_t i=0;i<mesh.uv.size();++i)for(unsigned c=0;c<2;++c){++checks;const auto bits=std::bit_cast<std::uint32_t>(mesh.uv[i][c]),wanted=static_cast<std::uint32_t>(expected["uv"].array()[i].array()[c].integer());if(bits!=wanted)throw std::runtime_error("Float UV mismatch "+name+" vertex "+std::to_string(i)+" channel "+std::to_string(c)+" bits "+std::to_string(bits)+" expected "+std::to_string(wanted));}
            for(std::size_t i=0;i<mesh.indices.size();++i)check(mesh.indices[i]==expected["indices"].array()[i].integer(),"Original Swift triangle indices match exactly");
            if(sprite){const auto& s=expected["sprite"];for(unsigned i=0;i<2;++i)check(sprite->size[i]==s["size"].array()[i].number(),"Sprite design size matches Swift");
                for(const auto& pair:{std::pair{"padding",sprite->padding},std::pair{"border",sprite->border},std::pair{"outer",sprite->outer},std::pair{"inner",sprite->inner}})
                    for(unsigned i=0;i<4;++i)check(pair.second[i]==s[pair.first].array()[i].number(),"Sprite trim/border/UV metadata matches original Swift Double values");
                check(sprite->pixelsPerUnit==s["pixelsPerUnit"].number()&&sprite->textureID==s["textureID"].string(),"Sprite PPU and original texture identity match Swift");}
        }catch(const std::invalid_argument& e){if(row["error"].isNull())throw std::runtime_error("C++ rejected valid Swift case "+name+": "+e.what());++errorCases;++checks;}
    }
    check(cases>=1800&&errorCases>=5,"Oracle includes all source cases, fill directions and explicit errors");
    std::cout<<"Compared "<<cases<<" original Swift image cases; "<<errorCases<<" matching errors\n";
}
}
int main(int argc,char**argv){try{synthetic();if(argc==2){std::ifstream file(argv[1],std::ios::binary);check(bool(file),"Explicit owned oracle opens");oracle(file);}
    else check(argc==1,"Pass at most one explicit Swift oracle JSON");std::cout<<"PASS "<<checks<<" source image geometry checks\n";return 0;
}catch(const std::exception& e){countAllocations=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
