#include "modules/projection_interaction.hpp"
#include "core/data/data_store.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace m=endfield::modules;namespace c=endfield::core;
namespace {
unsigned checks{};
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
void run(){
    m::ProjectionModel model({1,.8,.1,1},.63,.75);m::ProjectionInteraction input(model,{1000,700});
    check(!input.down({10,10}).handled&&!input.escape().handled,"Hidden Projection has no canvas input");
    input.setActive(true);check(input.down({10,10}).handled&&input.ownsPointer(),"Canvas owns its single gesture");
    check(!input.drag({10.6,10}).changed&&input.liveStroke()->points.size()==1,"Projection uses its own one-point spacing, not Notes spacing");
    check(input.drag({12,10}).changed&&input.liveStroke()->points.size()==2,"Whole-point travel appends a normalized vector");
    check(model.drawing().strokes().empty(),"Live stroke is not committed before mouse release");
    check(input.up().strokeCompleted&&model.drawing().strokes().size()==1&&!input.ownsPointer(),"Release commits once");
    check(!input.up().handled&&model.drawing().strokes().size()==1,"Duplicate release never duplicates a stroke");
    check(input.wheel(10,true,false).changed&&std::abs(model.brushWidth()-6.2)<1e-9,"Precise wheel preserves source 0.12 scaling");
    check(input.down({30,30}).brushChanged,"Pending brush event coalesces at next gesture");
    input.drag({2000,-100});check(input.liveStroke()->points.back()==c::Point{1,0},"Drawing samples clamp to canvas bounds");
    input.cancelGesture();check(model.drawing().strokes().size()==1&&!input.liveStroke(),"Lost capture discards only incomplete work");
    check(input.rightDown().brushChanged&&model.erasing(),"Right click toggles original eraser");
    check(input.down({11,10}).changed&&model.drawing().strokes().empty(),"Eraser removes the complete intersecting source stroke");
    check(input.up().erased,"Eraser gesture emits one completed event");input.rightDown();
    input.setMenuOpen(true);const auto dismissed=input.down({50,50});
    check(dismissed.closeMenu&&!input.liveStroke()&&!input.ownsPointer(),"Click dismissing a secondary menu does not also draw behind it");
    input.setMenuOpen(true);check(input.escape().closeMenu&&!input.escape().closeMenu,"Escape first closes a secondary menu");
    check(input.escape().returnToHUD,"Bare Escape requests caller-owned return animation");
    input.down({4,5});input.wheel(1,false,false);const auto hidden=input.setActive(false);
    check(hidden.brushChanged&&!input.liveStroke()&&!input.ownsPointer(),"Hide coalesces pending preference and cancels gesture");
    input.setActive(true);
    const auto media=m::ProjectionMediaReference::fromWindowsReference(ehud::data::makeWindowsMediaReference("C:\\fixture\\movie.mp4","Fixture.mp4",640,480,"video",20.,1));
    const auto id=model.addMedia(media,{500,350},{0,0,1000,700}).value();const auto initial=model.find(id)->frame;
    input.down({initial.x+10,initial.y+10});input.drag({initial.x+60,initial.y+40});input.up();
    check(model.find(id)->frame.x==initial.x+50&&model.find(id)->frame.y==initial.y+30,"Media header drag preserves original pointer offset");
    auto frame=model.find(id)->frame;input.down({frame.x+frame.width-5,frame.y+frame.height-5});input.drag({frame.x+frame.width+35,frame.y+frame.height+25});input.up();
    check(model.find(id)->frame.width==frame.width+40&&model.find(id)->frame.height==frame.height+30,"Corner gesture resizes without a duplicate slider");
    frame=model.find(id)->frame;const auto geometry=m::projectionMediaGeometry({frame.width,frame.height},m::NotesMediaKind::video);
    const auto seek=input.down({frame.x+geometry.seek.x+geometry.seek.width*.5,frame.y+geometry.seek.y+3});
    check(seek.seek&&seek.seek->id==id&&std::abs(seek.seek->seconds-10)<1e-9,"Video rail delegates exact midpoint seek to shared player");
    const auto end=input.drag({frame.x+geometry.seek.x+geometry.seek.width+100,frame.y+geometry.seek.y});
    check(end.seek&&end.seek->seconds==20,"Captured video seek clamps to duration");input.up();
    check(input.down({frame.x+12,frame.y+frame.height-15}).togglePlayback==id,"Source media button delegates playback to existing broker");
    check(input.down({frame.x+frame.width-10,frame.y+10}).mediaRemoved&&!model.find(id),"Close removes only owned session reference");
    check(!input.drag({100,100}).handled&&!input.ownsPointer(),"Removed media leaves no dangling pointer capture");
}
}
int main(){try{run();std::cout<<"PASS "<<checks<<" Projection interaction checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
