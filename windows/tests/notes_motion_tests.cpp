#include "modules/notes_motion.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
namespace {std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
using namespace endfield::modules;
namespace {
unsigned checks{};void check(bool ok,const char*m){++checks;if(!ok)throw std::runtime_error(m);}
bool near(double a,double b){return std::abs(a-b)<1e-9;}
template<class F>void rejects(F&&f){bool bad=false;try{f();}catch(const std::invalid_argument&){bad=true;}check(bad,"Invalid animation input rejects explicitly");}
void run(){
    const auto first=notesSectionMotion(true,{1,-1},0);check(first.x==18&&first.y==-18&&first.opacity==0&&first.active,"Incoming Notes starts displaced and hidden before its turn");
    const auto out=notesSectionMotion(false,{1,-1},.3);check(out.x==-14&&out.y==14&&out.opacity==0&&!out.active,"Outgoing Notes retires only after source section duration");
    const auto mid=notesSectionMotion(true,{1,0},.15);check(mid.opacity>.5&&near(mid.x,18*(1-mid.opacity)),"Source easing moves and fades on the same progress");
    check(near(notesToolbarMotion(.18*.3).y,2)&&near(notesMutationMotion(.2*.32).y,-3),"Source keyframe peaks preserve press and pin lift timing");
    check(!notesToolbarMotion(.18).active&&notesToolbarMotion(.18).y==0&&!notesMutationMotion(.2).active&&notesMutationMotion(.2).y==0,"Finished pulses return to their own layer positions");
    const auto created=notesCardMotion(true,0);const auto removed=notesCardMotion(false,.2);check(created.scale==.94&&created.y==12&&created.opacity==0&&near(removed.scale,.94)&&removed.y==-8&&!removed.active,"Create/delete use source center scale, travel and fade");
    check(notesDeletionMenuMotion(0).y==-6&&notesDeletionMenuMotion(.16).y==0,"Confirmation slides from above its source location");
    check(notesMenuMotion(true,0).y==6&&notesMenuMotion(true,0).opacity==0,"Secondary menu reveal starts below its source location");
    const auto dismiss=notesMenuMotion(false,0,.31);check(dismiss.opacity==.31&&dismiss.y==0&&dismiss.active,"Dismiss captures in-progress opacity without flashing opaque");
    check(notesMenuMotion(false,.12,.31).opacity==0&&!notesMenuMotion(false,.12,.31).active,"Dismiss artwork can retire after its finite duration");
    const auto reduced=notesSectionMotion(true,{0,1},0,true);check(!reduced.active&&reduced.opacity==1&&reduced.y==0,"Reduce motion settles without starting extra frame work");
    check(!notesCardMotion(false,0,true).active&&notesCardMotion(false,0,true).opacity==0&&!notesMenuMotion(false,0,.5,true).active,"Reduced removal releases hidden artwork immediately");
    const auto before=allocations.load();double checksum{};for(unsigned i=0;i<1000;++i){const auto t=i/1000.;checksum+=notesSectionMotion(true,{0,1},t).y+notesCardMotion(true,t).scale+notesMutationMotion(t).y+notesToolbarMotion(t).y+notesMenuMotion(false,t,.5).opacity;}
    check(allocations==before&&std::isfinite(checksum),"Sampled Notes tracks allocate no storage or own any frame scheduler");
    rejects([]{notesSectionMotion(true,{0,std::numeric_limits<double>::infinity()},0);});rejects([]{notesMenuMotion(false,0,-1);});rejects([]{notesToolbarMotion(std::numeric_limits<double>::quiet_NaN());});
}
}
int main(){try{run();std::cout<<"PASS "<<checks<<" Notes motion checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
