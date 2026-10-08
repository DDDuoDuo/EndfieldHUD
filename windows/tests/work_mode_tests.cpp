#include "modules/work_mode.hpp"
#include "core/data/json.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>

namespace m=endfield::modules;using Json=ehud::data::Json;
namespace {std::size_t checks{};std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t size){if(counting)++allocations;if(auto*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}void*operator new[](std::size_t size){return ::operator new(size);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace {
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
void near(double a,double b,const char*why){check(std::abs(a-b)<=1e-9*std::max({1.,std::abs(a),std::abs(b)}),why);}
template<class F>void rejects(F&&fn){bool failed{};try{fn();}catch(const std::invalid_argument&){failed=true;}check(failed,"Nonfinite caller clock must reject");}
void optional(std::optional<double>a,const Json&b,const char*why){check(a.has_value()!=b.isNull(),why);if(a)near(*a,b.number(),why);}
void oracle(const Json&reference){
    check(reference["provenance"]["source"].string()=="Sources/WorkModeController.swift"&&reference["provenance"]["modifications"].array().empty(),"Oracle compiles the unchanged original controller");
    unsigned notices{};std::vector<double>checkpoints;checkpoints.reserve(8);m::WorkModeController c({[&]{++notices;},[&](double value){checkpoints.push_back(value);}});std::size_t index{};
    for(const auto&row:reference["rows"].array()){try{const auto op=row["op"].string();const auto now=row["now"].number(),value=row["value"].number();std::optional<bool>result;
        if(op=="restore")result=c.restoreTrackedWorkSeconds(value);else if(op=="countdown")result=c.chooseCountdown(value,now);else if(op=="stopwatch")c.chooseStopwatch(now);else if(op=="start")c.start(now);else if(op=="pause")c.pause(now);else if(op=="resume")c.resume(now);else if(op=="stop")c.stop(now);else if(op=="reset")c.reset(now);else if(op=="visible")c.setVisible(value!=0,now);else if(op=="suspended")c.setSuspended(value!=0,now);else if(op=="refresh")c.refresh(now);else if(op=="wake")c.wake(now);else if(op=="shutdown")c.shutdown(now);else check(op=="sample","Known original controller operation");
        if(result)check(*result==row["result"].boolean(),"Source command return value");const auto s=c.snapshot(now);
        check(m::workModeKindKey(s.kind)==row["kind"].string()&&m::workModePhaseKey(s.phase)==row["phase"].string(),"Source timer kind/phase");near(s.duration,row["duration"].number(),"Source duration");near(s.elapsed,row["elapsed"].number(),"Source continuous elapsed");near(s.remaining(),row["remaining"].number(),"Source remaining");near(s.progress(),row["progress"].number(),"Source ring fraction");check(s.displayedSeconds()==row["seconds"].integer()&&s.timeText()==row["text"].string(),"Source epsilon/display formatting");check(s.active()==row["active"].boolean()&&c.suspended()==row["suspended"].boolean(),"Source session/suspension state");check(c.revision()==static_cast<std::uint64_t>(row["revision"].integer())&&notices==row["notifications"].integer(),"Source mutation/notification generations");near(c.trackedWorkSeconds(now),row["tracked"].number(),"Source absolute accumulated work time");check(checkpoints.size()==row["checkpoints"].array().size(),"Source lifetime checkpoint count");for(std::size_t n=0;n<checkpoints.size();++n)near(checkpoints[n],row["checkpoints"].array()[n].number(),"Source lifetime checkpoint value");checkpoints.clear();optional(c.displayDeadline(),row["displayDeadline"],"Source visible-only one-second deadline");optional(c.completionDeadline(),row["completionDeadline"],"Source hidden countdown completion deadline");++index;
    }catch(const std::exception&e){throw std::runtime_error("Controller oracle row "+std::to_string(index)+" ("+row["op"].string()+"): "+e.what());}}
    check(index>=700,"Original controller oracle exercises transitions, hidden/sleep, late delivery and accounting");
    for(const auto&row:reference["parse"].array()){const auto result=m::parseWorkModeDuration(row["input"].string());if(result.has_value()==row["seconds"].isNull())throw std::runtime_error("Duration parse disagreement for '"+row["input"].string()+"'");check(true,"Original parser acceptance");if(result)near(*result,row["seconds"].number(),"Original parsed duration");}
    for(const auto&row:reference["edit"].array())check(m::workModeDurationEditText(row["value"].number())==row["text"].string(),"Source canonical duration editor value");
}
void guards(){m::WorkModeController c;check(c.restoreTrackedWorkSeconds(123)&&!c.restoreTrackedWorkSeconds(123),"Lifetime work restores only once");c.chooseCountdown(1,0);c.start(0);c.wake(.2);check(c.snapshot(.2).phase==m::WorkModePhase::running&&c.nextDeadline()==1,"Early aggregate wake cannot complete timer early");c.wake(1.1);check(c.snapshot(1.1).phase==m::WorkModePhase::completed&&c.trackedWorkSeconds(1.1)==124&&!c.nextDeadline(),"Countdown completion accounts exactly configured duration");c.chooseStopwatch(2);c.start(2);check(!c.nextDeadline(),"Hidden stopwatch has no periodic work");allocations=0;counting=true;for(unsigned n=0;n<1000;++n){(void)c.snapshot(2+n*.1);(void)c.trackedWorkSeconds(2+n*.1);(void)c.nextDeadline();c.wake(2+n*.1);}counting=false;check(allocations==0,"Steady hidden stopwatch evaluation allocates nothing");const auto before=c.snapshot(102);for(const auto value:{0.,-1.,86401.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})check(!c.chooseCountdown(value,102)&&c.snapshot(102)==before,"Invalid duration preserves running session");rejects([&]{c.refresh(std::numeric_limits<double>::quiet_NaN());});rejects([&]{c.snapshot(std::numeric_limits<double>::infinity());});
    m::WorkModeController large;check(large.restoreTrackedWorkSeconds(std::numeric_limits<double>::max()),"Finite original lifetime boundary restores");large.chooseStopwatch(0);large.start(0);large.stop(100);check(large.trackedWorkSeconds(100)==std::numeric_limits<double>::max(),"Absolute lifetime overflow saturates without a duplicate delta");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass original work-mode.json fixture");std::ifstream in(argv[1],std::ios::binary);check(bool(in),"Open explicit Work Mode source fixture");std::string bytes((std::istreambuf_iterator<char>(in)),{});check(bytes.size()<2*1024*1024,"Bounded original source fixture");oracle(Json::parse(bytes,2*1024*1024));guards();std::cout<<"Work Mode: "<<checks<<" source/controller checks passed\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"Work Mode failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
