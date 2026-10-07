#include "core/source_camera.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
using namespace endfield::core;
using namespace endfield::core::source;
namespace {
unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
void close(double a,double b,double tolerance,const char*message){check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance,message);}
template<class F>void rejects(F f,const char*message){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
void matrix(const Matrix4&a,const Json&b,double tolerance){check(b.array().size()==4,"Matrix columns");for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)close(a.values[c*4+r],b.array()[c].array()[r].number(),tolerance,"Matrix equals independently exported Mac camera");}
void run(const Json&fixture){
    SourceCamera model(fixture["runtimeRoot"]);
    for(const auto&row:fixture["frames"].array()){
        const auto&v=row["viewport"].array();const Vec2 size{v[0].number(),v[1].number()};
        const auto frame=model.frame(size);const auto gpu=model.gpu(frame);
        matrix(frame.view,row["camera"]["view"],1e-10);matrix(frame.projection,row["camera"]["projection"],1e-10);matrix(frame.worldRoot,row["camera"]["worldRoot"],1e-10);
        for(unsigned i=0;i<2;++i)close(frame.layout.canvasSize[i],row["camera"]["canvasSize"].array()[i].number(),1e-8,"World canvas matches Mac float boundaries");
        matrix(gpu.projection,row["gpuCamera"]["projection"],1e-6);matrix(gpu.viewProjection,row["gpuCamera"]["viewProjection"],7e-5);matrix(gpu.viewNoTranslationProjection,row["gpuCamera"]["viewNoTranslationProjection"],1e-6);matrix(gpu.inverseView,row["gpuCamera"]["inverseView"],1e-6);
        for(unsigned i=0;i<4;++i)close(gpu.uiProjectionParameters[i],row["gpuCamera"]["uiProjectionParameters"].array()[i].number(),1e-7,"GPU near/far and Y policy match Mac source");
        const auto identity=inverseSourceMatrix(frame.view)*frame.view;for(unsigned i=0;i<16;++i)close(identity.values[i],i%5==0?1:0,1e-10,"Inverse camera roundtrip");
    }
    const auto&g=model.gyro();const Vec2 size{1920,1080};
    const auto a=g.targetEuler({0,0},size),b=g.targetEuler(size,size);
    close(a[0],-g.maxPitch,1e-7,"Pitch uses source signed max angle");close(b[1],g.maxYaw,1e-7,"Yaw uses source signed max angle");
    check(g.targetEuler({-500,-500},size)==a,"Pointer clamps outside overlay");check(g.targetEuler({0,0},size,false)==Vec3{},"Disabled pointer detection is neutral");
    const auto desktop=g.desktopTarget({0,1080},size);for(unsigned i=0;i<3;++i)close(desktop[i],a[i]*1.25,1e-9,"Desktop tilt preserves Mac intensity factor and one Y flip");
    check(g.desktopTarget({0,1080},size,0,1)==Vec3{},"Zero parallax removes tilt");
    GyroMotion motion;check(!motion.isAnimating(),"Initial gyro is idle");check(motion.retarget({0,20,0},10,.5),"Changed pointer starts the source tween");
    const auto halfway=motion.rotation(10.25);const auto expected=SourceCamera::eulerQuaternion({0,15,0});for(unsigned i=0;i<4;++i)close(halfway[i],expected[i],1e-12,"Gyro preserves OutQuad spherical interpolation");
    const auto current=motion.rotation(10.3);motion.retarget({0,-20,0},10.3,.5);for(unsigned i=0;i<4;++i)close(motion.rotation(10.3)[i],current[i],1e-12,"Retarget does not jump at interruption");
    check(!motion.retarget({0,-20,0},10.4,.5),"Same pointer does not continually restart easing");motion.finishIfNeeded(10.8);check(!motion.isAnimating(),"Gyro settles without extra polling");
    motion.retarget({5,6,0},11,.5,true);check(!motion.isAnimating(),"Reduced motion commits synchronously");
    motion.retarget({1,2,0},12,.5);const auto stopped=motion.rotation(12.1);motion.stop(12.1);check(!motion.isAnimating()&&motion.rotation(30)==stopped,"Stop retains its last sampled angle");
    rejects([&]{model.frame({0,100});},"Invalid display size rejected");rejects([&]{g.targetEuler({std::numeric_limits<double>::infinity(),0},size);},"Invalid pointer rejected");
    rejects([&]{motion.retarget({0,0,0},0,0);},"Zero gyro duration rejected");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass independent camera fixture");std::ifstream stream(argv[1],std::ios::binary);check(bool(stream),"Camera fixture opens");std::string bytes((std::istreambuf_iterator<char>(stream)),{});run(Json::parse(bytes));std::cout<<"Passed "<<checks<<" camera/gyro contracts\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
