#include "modules/hud_clock.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <iostream>

namespace m=endfield::modules;using ehud::data::Json;
namespace {
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
void oracle(const Json&source){
    check(source["source"].string()=="Sources/HUDClock.swift"&&source["provenance"]["modifications"].array().empty(),"Unchanged Mac clock oracle");
    m::LocalClockFields fields{};unsigned reads{},notices{};double time{};
    m::HUDClock clock([&]{++reads;return fields;});
    for(const auto&row:source["rows"].array()){
        const auto&f=row["fields"].array();fields={unsigned(f[0].integer()),unsigned(f[1].integer()),unsigned(f[2].integer()),unsigned(f[3].integer()),unsigned(f[4].integer()),unsigned(f[5].integer())};
        time+=1;const auto op=row["op"].string();bool changed{};
        if(op=="active")changed=clock.setActive(row["value"].integer()!=0,time);
        else if(op=="format")changed=clock.setFormat(row["value"].integer()?m::HUDClockFormat::twelveHour:m::HUDClockFormat::twentyFourHour,time);
        else if(op=="wake")changed=clock.wake(time);
        if(changed)++notices;
        check(reads==row["reads"].integer(),"Source sample count: no reads for hidden or unchanged settings");
        check(notices==row["notices"].integer(),"Only changed strings publish new clock artwork");
        check(clock.active()==row["active"].boolean()&&bool(clock.nextDeadline())==clock.active(),"Only active clock owns a deadline");
        check(bool(clock.reading())!=row["time"].isNull(),"Source reading lifecycle");
        if(clock.reading())check(clock.reading()->time==row["time"].string()&&clock.reading()->date==row["date"].string(),"Source POSIX date and 12/24-hour strings");
    }
}
void deadlines(){
    unsigned reads{};m::HUDClock clock([&]{++reads;return m::LocalClockFields{10,8,4,12,30,0};});
    check(!clock.nextDeadline()&&reads==0,"Construction owns no timer and reads nothing");clock.setActive(true,10.25);
    for(unsigned n=0;n<1000;++n){(void)clock.reading();(void)clock.nextDeadline();clock.wake(10.5);}
    check(reads==1&&clock.nextDeadline()==11.25,"Early shared wakes and pointer reads never resample");
    check(!clock.wake(100.8)&&reads==2&&clock.nextDeadline()==101.25,"Late wake samples once and skips missed ticks while retaining phase");
    clock.setActive(false,101);clock.wake(200);check(reads==2&&!clock.nextDeadline(),"Hidden clock cancels all deadlines");
    clock.setActive(true,201);clock.setActive(true,201.5);check(reads==3&&clock.nextDeadline()==202,"Duplicate activation cannot add a second cadence");
    bool rejected{};try{clock.wake(std::nan(""));}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Nonfinite deadline rejects instead of spinning");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass explicit source clock fixture");auto bytes=ehud::data::detail::readFile(argv[1],1024*1024);check(bytes.has_value(),"Read bounded fixture");oracle(Json::parse(*bytes));deadlines();std::cout<<"HUD clock: "<<checks<<" source/lifecycle checks passed\n";}catch(const std::exception&e){std::cerr<<"HUD clock failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
