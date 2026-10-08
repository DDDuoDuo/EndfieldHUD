#include "native/module_registration.hpp"
#include "core/data/json.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#endif
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {std::atomic<bool> counting{};std::atomic<std::size_t> allocations{};unsigned checks{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using namespace endfield::core;
using namespace endfield::native;
using ehud::data::Json;
namespace {
void check(bool v,const char*message){++checks;if(!v)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char*message){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,message);}
ModuleRegistrationPlacement placement(){ModuleRegistrationPlacement p;p.local.path=ModuleTransitionStyle::registrationKeyframe(.5,{-1,0});p.local.opacity=1;p.local.white=.77;
    p.hostClip={{},{0,0,440,440}};p.shutter=PlaneShutter{{},ModuleTransitionStyle::shutterKeyframe(.5,{-1,0})};return p;}
double cross(Point a,Point b,Point p){return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);}
unsigned coverage(const ModuleRegistrationMesh& mesh,Point p,std::size_t indexCount,bool& boundary){
    unsigned count=0;for(std::size_t i=0;i<indexCount;i+=3){const auto point=[&](std::size_t j){const auto&v=mesh.vertices[mesh.indices[i+j]].position;return Point{v[0],v[1]};};
        const auto a=point(0),b=point(1),c=point(2);const auto area=cross(a,b,c);if(std::abs(area)<1e-12)continue;
        const auto sign=area>0?1.:-1.;const auto x=cross(a,b,p)*sign,y=cross(b,c,p)*sign,z=cross(c,a,p)*sign;
        if(x>=-1e-5&&y>=-1e-5&&z>=-1e-5){if(std::min({std::abs(x),std::abs(y),std::abs(z)})<1e-5)boundary=true;else ++count;}
    }return count;
}
void synthetic(){
    NativeModuleRegistration seam("module.fixture.registration");const auto*address=seam.draws().data();const auto id=seam.draws()[0].sourceID;
    check(seam.draws().size()==1&&seam.draws()[0].opacity==0&&seam.mesh().vertices.size()==120&&seam.mesh().indices.size()==180,"Inactive seam has one stable retained object and bounded topology");
    auto p=placement();check(seam.update(p),"First original path builds its finite mesh");
    check(seam.draws()[0].linearTint[0]==.55422168970108032f&&seam.draws()[0].linearTint[3]==.38f,"Original dark generic-gray stroke converts once to sampled linear sRGB");
    const auto geometry=seam.stats().geometryRevision;
    p.local.white=.22;p.parentOpacity=.75f;p.local.opacity=.36;
    check(!seam.update(p)&&seam.stats().geometryRevision==geometry&&seam.draws()[0].linearTint[0]==.039681904017925262f,"Theme and opacity changes retain path geometry");
    check(std::abs(seam.draws()[0].opacity-.27f)<1e-7,"Parent and local seam opacity multiply independently of color alpha");
    allocations=0;counting=true;
    try{for(unsigned i=0;i<120;++i){p.world=Matrix4::translation(static_cast<double>(i)*.1,0);p.hostClip.worldToLocal=Matrix4::translation(-static_cast<double>(i)*.1,0);
        p.shutter->worldToLocal=p.hostClip.worldToLocal;check(!seam.update(p),"Pointer-only seam update leaves geometry unchanged");}}
    catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&seam.stats().geometryRevision==geometry,"Pointer seam updates allocate and rebuild nothing");
    allocations=0;counting=true;
    try{for(unsigned i=0;i<120;++i){p.local.path=ModuleTransitionStyle::registrationAt(static_cast<double>(i)*.3/120,{-1,0});seam.update(p);}}
    catch(...){counting=false;throw;}counting=false;
    check(allocations==0&&seam.stats().geometryRevision>geometry,"Finite changing paths reuse fixed CPU mesh storage");
    const auto before=seam.stats().geometryRevision;const auto world=seam.draws()[0].world;
    auto invalid=p;invalid.local.path[5][1].y+=1;
    rejects([&]{seam.update(invalid);},"Unsupported late diagonal stroke rejects before replacing live geometry");
    invalid=p;invalid.shutter->strips[5][1].x=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{seam.update(invalid);},"Invalid late shutter preserves pending stroke geometry");
    check(seam.stats().geometryRevision==before&&seam.draws()[0].world==world,"Rejected seam update preserves revision and constants");
    check(!seam.update({})&&seam.draws()[0].opacity==0&&seam.stats().geometryRevision==before,"Deactivation retains the last mesh at zero opacity");
    check(seam.draws().data()==address&&seam.draws()[0].sourceID==id,"Activation and animation never invalidate borrowed composition storage");
    check(!seam.update(p),"Reactivating unchanged seam reuses existing mesh");
    auto path=p.local.path;path[0][0].x=std::numeric_limits<double>::infinity();rejects([&]{(void)moduleRegistrationMesh(path,.7);},"Nonfinite stroke point rejected");
    rejects([&]{(void)moduleRegistrationMesh(p.local.path,0);},"Empty stroke width rejected");
}
void oracle(const char*path){
    std::ifstream stream(path,std::ios::binary|std::ios::ate);check(bool(stream),"Explicit Core Graphics stroke oracle opens");const auto size=stream.tellg();check(size>0&&size<4*1024*1024,"Stroke oracle remains bounded");
    std::string bytes(static_cast<std::size_t>(size),'\0');stream.seekg(0);stream.read(bytes.data(),size);check(bool(stream),"Complete stroke oracle read");const auto data=Json::parse(bytes);
    check(data["kind"].string()=="actual-core-graphics-stroke-coverage"&&data["lineCap"].string()=="butt"&&data["lineJoin"].string()=="miter"&&data["noWindow"].boolean()&&!data["screenCapture"].boolean(),"Oracle is actual source-style Core Graphics geometric coverage");
    unsigned tested=0,skipped=0;for(const auto&row:data["cases"].array()){
        RegistrationPath input{};for(auto&strip:input)for(unsigned i=0;i<4;++i)strip[i]={row["points"].array()[i].array()[0].number(),row["points"].array()[i].array()[1].number()};
        const auto mesh=moduleRegistrationMesh(input,data["width"].number());
        for(const auto&s:row["samples"].array()){const Point p{s.array()[0].number(),s.array()[1].number()};bool boundary=false;const auto count=coverage(mesh,p,30,boundary);
            if(boundary){++skipped;continue;}check(count<=1,"Disjoint miter mesh has no doubled-alpha triangle overlap");
            check((count==1)==s.array()[2].boolean(),"Triangulated source bracket matches actual Core Graphics stroked path");++tested;}
    }
    check(tested>1000,"Oracle covers four orientations, short/zero caps and intermediate corners");std::cout<<"Core Graphics coverage samples="<<tested<<" boundarySkipped="<<skipped<<'\n';
}
#ifdef _WIN32
void gpu(const wchar_t*shader){
    WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"EndfieldModuleRegistrationFixture";
    const auto atom=RegisterClassW(&type);check(atom!=0,"Own hidden test window class registers");
    const auto window=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,type.lpszClassName,L"Synthetic seam fixture",WS_POPUP,0,0,440,440,nullptr,nullptr,type.hInstance,nullptr);
    check(window!=nullptr,"Own hidden test window creates");
    try{
        Renderer renderer;renderer.initialize(window,440,440,{Driver::warpForTests,shader,RenderTarget::offscreenForTests});renderer.setCamera(layerViewportProjection(440,440));
        LayerRasterizer raster;LayerScene empty(raster);empty.load(Json::Object{{"bounds",Json::Array{0,0,0,0}},{"children",Json::Array{}}},{});
        NativeModuleRegistration seam("module.fixture.registration");auto p=placement();seam.update(p);check(seam.uploadGeometry(renderer),"First retained seam mesh uploads");
        LayerComposition composition;const LayerCompositionEntry entry{&empty,seam.draws()};composition.setEntries(renderer,std::span(&entry,1));composition.present(renderer);renderer.draw(false);
        auto image=renderer.readback();auto alpha=[&](unsigned x,unsigned y){return image.pixels.at(std::size_t(y)*image.rowBytes+x*4+3);};
        check(std::abs(int(alpha(219,180))-97)<=1&&std::abs(int(alpha(219,158))-97)<=1,"Miter corner and line interior have the same single-layer alpha");
        check(alpha(220,180)==0,"Source stroke width does not expand into a rectangle");
        const auto before=renderer.stats();for(unsigned i=0;i<120;++i){p.world=Matrix4::translation(double(i)*.001,0);check(!seam.update(p),"Pointer-only GPU seam has no mesh change");composition.present(renderer);}
        const auto after=renderer.stats();check(after.meshUploads==before.meshUploads&&after.textureUploads==before.textureUploads&&after.objectBufferAllocations==before.objectBufferAllocations,"Pointer frames retain all seam GPU resources");
        for(unsigned i=0;i<18;++i){p.local.path=ModuleTransitionStyle::registrationAt(double(i)*.3/18,{-1,0});if(seam.update(p))seam.uploadGeometry(renderer);composition.present(renderer);}
        check(renderer.stats().meshes==1&&renderer.stats().textures==0&&renderer.stats().resourceBytes==before.resourceBytes,"Finite stroke morph keeps exactly one bounded mesh and no texture");
        seam.update({});composition.present(renderer);renderer.draw(false);image=renderer.readback();check(std::all_of(image.pixels.begin(),image.pixels.end(),[](auto c){return c==0;}),"Inactive retained seam draws fully transparent");
        check(!seam.releaseResources(renderer),"Published seam cannot retire its mesh prematurely");composition.detach(renderer);check(seam.releaseResources(renderer)&&renderer.stats().meshes==0,"Explicit seam retirement follows composition detach");
        check(!IsWindowVisible(window),"Native seam fixture never shows or captures desktop content");
    }catch(...){DestroyWindow(window);UnregisterClassW(MAKEINTATOM(atom),type.hInstance);throw;}
    DestroyWindow(window);UnregisterClassW(MAKEINTATOM(atom),type.hInstance);
}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t**argv){const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 1;int result=0;
    try{synthetic();check(argc==2,"Pass native shader path for hidden seam GPU fixture");gpu(argv[1]);std::cout<<"PASS "<<checks<<" module registration checks\n";}
    catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#else
int main(int argc,char**argv){try{synthetic();if(argc==2)oracle(argv[1]);else check(argc==1,"Pass at most one explicit stroke oracle");std::cout<<"PASS "<<checks<<" module registration checks\n";return 0;}
    catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
#endif
