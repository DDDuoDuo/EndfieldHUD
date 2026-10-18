#include "modules/projection_interaction.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
bool finite(core::Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
bool contains(core::Rect r,core::Point p){return r.width>0&&r.height>0&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
}
ProjectionInteraction::ProjectionInteraction(ProjectionModel&m,core::Point size):model_(m){resize(size);}
void ProjectionInteraction::resize(core::Point size){if(!finite(size)||size.x<=0||size.y<=0)throw std::invalid_argument("Invalid Projection canvas size");size_=size;}
void ProjectionInteraction::cancelGesture()noexcept{held_=false;pending_.reset();gesture_.reset();lastEraser_.reset();eraseChanged_=false;}
ProjectionInputResult ProjectionInteraction::setActive(bool value){
    ProjectionInputResult result;if(value==active_)return result;active_=value;result.changed=true;
    if(!value){result.brushChanged=pendingBrush_;pendingBrush_=false;result.closeMenu=menuOpen_;menuOpen_=false;cancelGesture();}return result;
}
core::Point ProjectionInteraction::normalized(core::Point p)const{return {std::clamp(p.x/std::max(1.,size_.x),0.,1.),std::clamp(p.y/std::max(1.,size_.y),0.,1.)};}
ProjectionInputResult ProjectionInteraction::seek(std::uint64_t id,core::Point p)const{
    ProjectionInputResult result;result.handled=true;const auto*item=model_.find(id);
    if(item&&item->reference->duration()){
        const auto box=projectionMediaGeometry({item->frame.width,item->frame.height},item->reference->kind()).seek;
        result.seek=ProjectionInputResult::Seek{id,std::clamp((p.x-item->frame.x-box.x)/std::max(1.,box.width),0.,1.) * item->reference->duration().value()};
    }return result;
}
ProjectionInputResult ProjectionInteraction::down(core::Point p){
    ProjectionInputResult result;if(!active_||!finite(p))return result;result.handled=true;
    if(menuOpen_){menuOpen_=false;result.closeMenu=true;return result;}
    cancelGesture();result.brushChanged=pendingBrush_;pendingBrush_=false;
    const auto items=model_.media();
    for(auto it=items.rbegin();it!=items.rend();++it){const auto id=it->id;const auto frame=it->frame;const auto kind=it->reference->kind();
        const auto box=projectionMediaGeometry({frame.width,frame.height},kind);const core::Point local{p.x-frame.x,p.y-frame.y};
        if(contains(box.close,local)){result.changed=model_.removeMedia(id);result.mediaRemoved=result.changed;return result;}
        if(kind!=NotesMediaKind::image&&contains(box.play,local)){result.togglePlayback=id;return result;}
        auto rail=box.seek;rail.y-=6;rail.height+=12;
        if(kind==NotesMediaKind::video&&contains(rail,local)){held_=true;gesture_=Gesture{Kind::seek,id,p,frame};auto next=seek(id,p);next.brushChanged=result.brushChanged;return next;}
        const bool resize=contains(box.resize,local);
        if(resize||contains(box.header,local)){held_=true;gesture_=Gesture{resize?Kind::resize:Kind::move,id,p,frame};result.changed=model_.bringForward(id);return result;}
    }
    held_=true;
    if(model_.erasing()){lastEraser_=p;eraseChanged_=model_.erase(p,size_);result.changed=eraseChanged_;}
    else{pending_=DrawingStroke{{normalized(p)},model_.brushWidth(),model_.color()};result.changed=true;}return result;
}
ProjectionInputResult ProjectionInteraction::drag(core::Point p){
    ProjectionInputResult result;if(!active_||!held_||!finite(p))return result;result.handled=true;
    if(gesture_){const auto&g=*gesture_;if(g.kind==Kind::seek)return seek(g.id,p);
        auto frame=g.frame;if(g.kind==Kind::move){frame.x+=p.x-g.start.x;frame.y+=p.y-g.start.y;}else{frame.width+=p.x-g.start.x;frame.height+=p.y-g.start.y;}
        result.changed=model_.setFrame(frame,g.id,{0,0,size_.x,size_.y});return result;
    }
    if(model_.erasing()){
        if(!lastEraser_||std::hypot(p.x-lastEraser_->x,p.y-lastEraser_->y)>=std::max(1.,model_.brushWidth()/4)){
            lastEraser_=p;result.changed=model_.erase(p,size_);eraseChanged_|=result.changed;
        }
    }else if(pending_&&pending_->points.size()<NotesDrawing::maximumPointsPerStroke){const auto next=normalized(p),last=pending_->points.back();
        if(std::hypot((next.x-last.x)*size_.x,(next.y-last.y)*size_.y)>=1){pending_->points.push_back(next);result.changed=true;}
    }return result;
}
ProjectionInputResult ProjectionInteraction::up(){
    ProjectionInputResult result;if(!active_||!held_)return result;result.handled=true;
    if(pending_){result.strokeCompleted=model_.append(std::move(*pending_));result.drawingLimit=!result.strokeCompleted;result.changed=true;}
    result.erased=eraseChanged_;cancelGesture();return result;
}
ProjectionInputResult ProjectionInteraction::rightDown(){ProjectionInputResult result;if(!active_)return result;result.handled=true;result.changed=model_.setErasing(!model_.erasing());result.brushChanged=true;return result;}
ProjectionInputResult ProjectionInteraction::wheel(double delta,bool precise,bool ended){
    ProjectionInputResult result;if(!active_||!std::isfinite(delta))return result;result.handled=true;
    result.changed=model_.setBrushWidth(model_.brushWidth()+delta*(precise?.12:1));pendingBrush_=true;
    if(ended){result.brushChanged=true;pendingBrush_=false;}return result;
}
ProjectionInputResult ProjectionInteraction::escape(){ProjectionInputResult result;if(!active_)return result;result.handled=true;if(menuOpen_){menuOpen_=false;result.closeMenu=true;}else result.returnToHUD=true;return result;}
}
