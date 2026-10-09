#include "modules/reader_viewport.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace endfield::modules {
namespace {
bool finite(core::Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
bool contains(core::Rect r,core::Point p){return finite(p)&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
void clock(double t){if(!std::isfinite(t)||t<0)throw std::invalid_argument("Reader requires a finite owner clock");}
}
void ReaderViewport::setActive(bool value,double t){clock(t);if(active_==value)return;active_=value;if(value)releaseAt_.reset();else{cancelInteraction();detailAt_.reset();offset_=0;view_={};releaseAt_=t+.35;animatedTurn_=0;animatedScroll_=false;}}
void ReaderViewport::setPreferences(const ReaderPreferences&p){if(!p.valid())throw std::invalid_argument("Invalid Reader preferences");vertical_=p.vertical();if(vertical_)animatedTurn_=0;}
void ReaderViewport::setPage(std::optional<ReaderPageAnchor>p,double t,bool reduceMotion){clock(t);if(p&&(!p->location.valid()||p->bookID.empty()||!std::isfinite(p->progress)||p->progress<0||p->progress>1))throw std::invalid_argument("Invalid Reader page anchor");if(!active_&&page_)return;
    // ReaderController publishes current=nil before opening a different book.
    // If the owner's revision notification coalesces those two snapshots,
    // perform the exact clear/new-book end state here too (never carry a zoom
    // or pending page-turn into another document at the same location).
    const bool differentBook=p&&page_&&p->bookID!=page_->bookID;
    if(differentBook){pendingOffset_.reset();pendingView_.reset();turnDirection_=0;offset_=0;view_={};}
    const bool changed=differentBook||(!p)!=(!page_)||(p&&page_&&p->location!=page_->location);
    const bool animate=changed&&page_&&p&&turnDirection_!=0;
    if(changed){const bool same=page_&&p&&page_->bookID==p->bookID;offset_=pendingOffset_.value_or(0);pendingOffset_.reset();view_=same&&p->illustration?pendingView_.value_or(view_):ReaderImageView{};pendingView_.reset();panStart_.reset();}
    page_=std::move(p);if(changed)scheduleDetail(t);animatedTurn_=animate&&active_&&!reduceMotion?turnDirection_:0;
    if(animatedTurn_){if(pageTurnSequence_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Reader turn sequence exhausted");++pageTurnSequence_;}if(changed)turnDirection_=0;
}
core::Rect ReaderViewport::progressRect()const noexcept{return canZoom()?core::Rect{165,416,171,22}:core::Rect{55,389,290,22};}
double ReaderViewport::fraction(core::Point p)const noexcept{const auto r=progressRect();return std::clamp((p.x-r.x)/r.width,0.,1.);}
double ReaderViewport::displayedProgress()const noexcept{return dragProgress_.value_or(page_?page_->progress:0);}
bool ReaderViewport::pointerDown(core::Point p){if(page_&&contains(progressRect(),p)){dragProgress_=fraction(p);return true;}if(contains(viewport,p)&&canZoom()&&view_.zoom>1)panStart_=Pan{p,view_.pan};return contains(viewport,p);}
void ReaderViewport::pointerDragged(core::Point p,double t){clock(t);if(!finite(p))return;if(dragProgress_)dragProgress_=fraction(p);else if(panStart_){auto v=view_;v.pan={panStart_->pan.x+p.x-panStart_->point.x,panStart_->pan.y+p.y-panStart_->point.y};setView(v,t);}}
void ReaderViewport::pointerUp(double t){clock(t);if(dragProgress_){const auto value=*dragProgress_;dragProgress_.reset();turnDirection_=0;setView({},t);navigation_=ReaderNavigationIntent{0,value};}panStart_.reset();}
void ReaderViewport::cancelInteraction()noexcept{dragProgress_.reset();panStart_.reset();navigation_.reset();}
void ReaderViewport::scheduleDetail(double t){detailAt_.reset();if(view_.zoom>1)detailAt_=t+.12;}
void ReaderViewport::setView(ReaderImageView v,double t,double offset){v=v.clamped({viewport.width,viewport.height});if(v==view_&&offset==offset_)return;const bool changed=v!=view_;view_=v;offset_=offset;pendingOffset_.reset();pendingView_.reset();if(changed)scheduleDetail(t);animatedScroll_=false;}
void ReaderViewport::zoomBy(double factor,core::Point p,double t){clock(t);if(!canZoom()||!std::isfinite(factor)||!finite(p))return;const auto old=view_.zoom,next=std::clamp(old*factor,1.,12.),ratio=next/old;p.x-=viewport.x+viewport.width/2;p.y-=viewport.y+viewport.height/2;auto v=view_;v.zoom=next;v.pan={p.x-(p.x-v.pan.x)*ratio,p.y-(p.y-v.pan.y)*ratio};setView(v,t);}
void ReaderViewport::resetZoom(double t){clock(t);setView({},t);}
bool ReaderViewport::magnify(core::Point p,double amount,double t){clock(t);if(!contains(viewport,p)||!canZoom()||!std::isfinite(amount))return false;zoomBy(std::max(.05,1+amount),p,t);return true;}
bool ReaderViewport::turnPage(int direction){if(!page_||(direction>0?!page_->next:!page_->previous))return false;turnDirection_=vertical_?0:direction;pendingOffset_.reset();pendingView_.reset();if(canZoom()&&view_.zoom>1){auto v=view_;if(vertical_)v.pan.y=viewport.height*(v.zoom-1)/2*(direction>0?1:-1);pendingView_=v;}navigation_=ReaderNavigationIntent{direction>0?1:-1,{}};return true;}
void ReaderViewport::scrollZoomed(double dx,double dy,double t){const auto h=viewport.height*view_.zoom,extent=(h-viewport.height)/2;double position=extent-view_.pan.y+offset_+std::clamp(dy,-viewport.height*2,viewport.height*2);auto target=view_;target.pan.x-=dx;int direction=0;if(position>=h&&page_->next){position-=h;direction=1;}else if(position<0&&page_->previous){position+=h;direction=-1;}else if(!page_->next)position=std::min(h-viewport.height,position);position=std::clamp(position,0.,h-.001);const auto inside=std::min(h-viewport.height,position);target.pan.y=extent-inside;if(direction){pendingView_=target.clamped({viewport.width,viewport.height});pendingOffset_=position-inside;turnDirection_=0;navigation_=ReaderNavigationIntent{direction,{}};}else setView(target,t,position-inside);}
bool ReaderViewport::scroll(core::Point p,double dx,double dy,double t,ReaderGesturePhase phase,bool momentum,bool precision,bool modifier){clock(t);if(!contains(viewport,p)||!page_||!std::isfinite(dx)||!std::isfinite(dy))return false;animatedScroll_=false;if(modifier&&canZoom()){zoomBy(std::exp(-dy*.012),p,t);return true;}if(!vertical_&&view_.zoom>1&&std::abs(dy)>=std::abs(dx)/1.1){auto v=view_;v.pan.x-=dx;v.pan.y-=dy;setView(v,t);return true;}
    if(vertical_){if(view_.zoom>1){scrollZoomed(dx,dy,t);return true;}offset_=std::clamp(offset_+dy,-viewport.height,viewport.height*2);if(offset_>=viewport.height&&page_->next){pendingOffset_=offset_-viewport.height;turnDirection_=0;navigation_=ReaderNavigationIntent{1,{}};}else if(offset_<0&&page_->previous){pendingOffset_=viewport.height+offset_;turnDirection_=0;navigation_=ReaderNavigationIntent{-1,{}};}else{offset_=std::clamp(offset_,0.,page_->next?viewport.height:0.);animatedScroll_=!precision;}}
    else{if(phase==ReaderGesturePhase::began){horizontalRemainder_=0;turnConsumed_=false;}if(momentum)return true;if(phase==ReaderGesturePhase::ended||phase==ReaderGesturePhase::cancelled){horizontalRemainder_=0;return true;}if(std::abs(dx)<=std::abs(dy)*1.1)return true;if(phase==ReaderGesturePhase::none&&t-lastWheelTurn_>=.24)turnConsumed_=false;if(turnConsumed_)return true;horizontalRemainder_+=dx;if(std::abs(horizontalRemainder_)>=(precision?60:24)){turnConsumed_=true;lastWheelTurn_=t;turnPage(horizontalRemainder_>0?1:-1);horizontalRemainder_=0;}}return true;
}
std::optional<ReaderNavigationIntent>ReaderViewport::takeNavigation()noexcept{auto n=navigation_;navigation_.reset();return n;}
std::optional<ReaderImageView>ReaderViewport::takeDetailRequest(double t){clock(t);if(!detailAt_||t<*detailAt_)return {};detailAt_.reset();if(!active_||!canZoom()||view_.zoom<=1)return {};return view_;}
std::optional<double>ReaderViewport::nextWakeTime()const noexcept{if(!detailAt_)return releaseAt_;if(!releaseAt_)return detailAt_;return std::min(*detailAt_,*releaseAt_);}
bool ReaderViewport::artworkReleaseDue(double t){clock(t);if(!releaseAt_||t<*releaseAt_)return false;releaseAt_.reset();return !active_;}
std::array<ReaderPagePlacement,3>ReaderViewport::placements(bool detail)const noexcept{std::array<ReaderPagePlacement,3>result;for(std::size_t n=0;n<3;++n){const double neighbor=n==0?0:n==1?1:-1;auto&r=result[n];r.hidden=n!=0&&!vertical_;if(view_.zoom>1){r.rect=n==0&&detail?core::Rect{0,-offset_,viewport.width,viewport.height}:core::Rect{viewport.width*(1-view_.zoom)/2+view_.pan.x,viewport.height*(1-view_.zoom)/2+view_.pan.y+neighbor*viewport.height*view_.zoom-offset_,viewport.width*view_.zoom,viewport.height*view_.zoom};}else r.rect={0,neighbor*viewport.height-offset_,viewport.width,viewport.height};}return result;}
core::Rect readerMenuRect(ReaderMenuKind k,std::optional<core::Rect>a){const double w=k==ReaderMenuKind::library?350:k==ReaderMenuKind::bookmarks?240:248,h=k==ReaderMenuKind::settings?222:294;return {a?std::clamp(a->x,8.,392-w):(400-w)/2,a?a->y+a->height+6:46,w,h};}
void ReaderListScroll::setCount(std::size_t n){count_=n;offset_=std::min(offset_,n>6?n-6:0);remainder_=0;}
bool ReaderListScroll::scroll(double delta){if(confirming_||!std::isfinite(delta))return false;remainder_+=std::clamp(delta,-350.,350.);const auto rows=std::trunc(remainder_/35);if(rows==0)return true;remainder_-=rows*35;const auto maximum=count_>6?count_-6:0;const auto amount=std::size_t(std::abs(rows));if(rows>0)offset_+=std::min(amount,maximum-offset_);else offset_-=std::min(amount,offset_);return true;}
std::optional<core::Rect>ReaderListScroll::scrollbar(double w)const noexcept{if(count_<=6||!std::isfinite(w)||w<6)return {};const auto height=std::max(12.,204.*6/double(count_)),y=39+(204-height)*double(offset_)/double(count_-6);return core::Rect{w-6,y,2,height};}
} // namespace endfield::modules
