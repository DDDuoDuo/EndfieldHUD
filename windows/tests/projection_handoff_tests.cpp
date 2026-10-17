#include "app/projection_handoff.hpp"
#include <iostream>
#include <stdexcept>
using H=endfield::app::ProjectionHandoff;
namespace {
unsigned checks{};
void check(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
void run(){
    H h;const H::Context context{72,91};
    const H::OpeningConditions gates[]={{false,false,false,false},{true,true,false,false},{true,false,true,false},{true,false,false,true}};
    for(const auto blocked:gates)
        check(!h.open(blocked,context)&&!h.active(),"Source open guards leave the current HUD untouched");
    const auto request=h.open({true,false,false,false},context);
    check(request&&request->action==H::Action::closeHUD&&request->context==context,"Request retains display and foreground identity for serialized exit");
    check(!h.acceptsInput()&&!h.open({true,false,false,false},context)&&!h.returnToHUD(),"Repeated input cannot create a second surface during HUD exit");
    check(!h.hudClosed(request->generation+1)&&!h.projectionClosed(request->generation),"Wrong or out-of-order completion cannot open a surface");
    const auto shown=h.hudClosed(request->generation);
    check(shown&&shown->action==H::Action::showProjection&&h.acceptsInput(),"Only completed HUD exit presents Projection");
    check(!h.hudClosed(request->generation),"Duplicate HUD completion is harmless");
    const auto returning=h.returnToHUD();
    check(returning&&returning->action==H::Action::closeProjection&&!h.acceptsInput(),"Return disables input before beginning source fade");
    check(!h.returnToHUD(),"Repeated Escape cannot enqueue extra HUD openings");
    const auto reopened=h.projectionClosed(returning->generation);
    check(reopened&&reopened->action==H::Action::showHUD&&reopened->context==context&&!h.active(),"HUD reopens only after Projection exit and retains its source display");
    check(!h.projectionClosed(returning->generation),"Duplicate fade completion does not reopen twice");

    const auto old=h.open({true,false,false,false},context).value();h.cancel();
    const auto next=h.open({true,false,false,false},{11,12}).value();
    check(!h.hudClosed(old.generation)&&next.generation!=old.generation,"Cancelled HUD completion cannot affect a newer handoff");
    h.hudClosed(next.generation);const auto close=h.cancel();
    check(close&&close->action==H::Action::closeImmediately&&!close->externalID&&!h.active(),"Quit immediately releases Projection without a return callback");
    check(!h.projectionClosed(next.generation),"Fade callback after shutdown cannot reopen the app");

    auto cycle=h.open({true,false,false,false},context).value();
    check(!h.external(20)&&!h.external(21),"External UI waits for already-running HUD exit");
    auto result=h.hudClosed(cycle.generation);
    check(result&&result->action==H::Action::external&&result->externalID==21&&!h.active(),"Only latest deferred external UI appears, without flashing Projection");
    cycle=h.open({true,false,false,false},context).value();h.hudClosed(cycle.generation);
    result=h.external(22);
    check(result&&result->action==H::Action::closeProjection,"External request dismisses visible Projection before presentation");
    check(!h.external(23),"External replacement does not start another fade");
    result=h.projectionClosed(cycle.generation);
    check(result&&result->action==H::Action::external&&result->externalID==23,"External completion bypasses ordinary HUD return");
    check(!h.external(24),"Owner handles external presentation outside Projection");
    cycle=h.open({true,false,false,false},context).value();h.external(25);h.cancel();
    check(!h.hudClosed(cycle.generation),"Cancellation drops pending external actions too");
    cycle=h.open({true,false,false,false},context).value();
    check(!h.hudClosed(cycle.generation,false)&&!h.active(),"Unavailable source and fallback display cancels rather than stranding a handoff");
    cycle=h.open({true,false,false,false},context).value();result=h.hudClosed(cycle.generation,true,101);
    check(result&&result->context.display==101,"A fallback monitor selected after HUD exit becomes the Projection display");
    check(h.reposition(102)&&!h.reposition(102),"Display changes update only visible Projection context");
    h.returnToHUD();check(!h.reposition(103),"Monitor reposition cannot alter a running dismissal");result=h.projectionClosed(cycle.generation);
    check(result&&result->context.display==102&&result->context.previousApplication==context.previousApplication,"Return preserves the latest display and original foreground application separately");
}
}
int main(){try{run();std::cout<<"PASS "<<checks<<" Projection handoff checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
