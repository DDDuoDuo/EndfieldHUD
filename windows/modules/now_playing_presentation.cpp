#include "modules/now_playing_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace endfield::modules {
namespace {
void need(bool ok,const char*why){if(!ok)throw std::invalid_argument(why);}
bool contains(core::Rect r,core::Point p)noexcept{return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
void valid(const NowPlayingAppearance&a){need(static_cast<unsigned>(a.language)<=static_cast<unsigned>(core::Language::korean),"Invalid Now Playing language");for(double c:a.accent)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid Now Playing color");}
bool equal(const std::optional<NowPlayingTrack>&a,const std::optional<NowPlayingTrack>&b){if(bool(a)!=bool(b))return false;if(!a)return true;return std::tie(a->title,a->artist,a->album,a->duration,a->position,a->isPlaying,a->sampledAt,a->identifier,a->timedLyrics,a->artworkRevision,a->supportsSeeking)==std::tie(b->title,b->artist,b->album,b->duration,b->position,b->isPlaying,b->sampledAt,b->identifier,b->timedLyrics,b->artworkRevision,b->supportsSeeking);}
constexpr std::array<core::Rect,5>rects{{{404,406,30,30},{2,406,30,30},{38,403,36,36},{78,406,30,30},{368,406,30,30}}};
}
std::string_view nowPlayingActionID(NowPlayingAction a)noexcept{constexpr std::array ids{"lyrics","previous","playPause","next","volume"};const auto index=static_cast<std::size_t>(a);return index<ids.size()?ids[index]:"";}
NowPlayingProgress nowPlayingProgress(std::optional<double>value,std::optional<double>duration,bool playing,bool dragging,bool animated,bool reduced,double advance)noexcept{
    const auto length=duration&&std::isfinite(*duration)&&*duration>0?*duration:0.;const double fraction=length>0&&value&&std::isfinite(*value)?std::clamp(*value/length,0.,1.):0.;NowPlayingProgress result;result.width=330*fraction;result.interpolates=animated&&!dragging&&!reduced&&playing&&length>0&&std::isfinite(advance)&&advance>0;result.destination=result.interpolates?std::min(330.,result.width+330*advance/length):result.width;result.handleX=55+result.width;result.destinationHandleX=55+result.destination;result.duration=result.interpolates?1:0;return result;
}
NowPlayingVolumeGeometry nowPlayingVolumeGeometry(double value,bool available)noexcept{value=std::isfinite(value)?std::clamp(value,0.,1.):0;const double y=369-117*value;return {{354,222,53,180},NowPlayingGeometry::volumeMenu,{382.5,252,2,117},{382.5,y,2,117*value},{379.5,y-3.5,8,7},{360,228,47,18},available?1.:.35};}
double NowPlayingPresentation::Scalar::sample(double time)const noexcept{if(duration<=0||!std::isfinite(time))return to;const auto p=std::clamp((time-began)/duration,0.,1.);const auto t=eased?core::CubicTiming{.42,0,.58,1}.value(p):p;return from+(to-from)*t;}
bool NowPlayingPresentation::Scalar::live(double time)const noexcept{return duration>0&&from!=to&&std::isfinite(time)&&time<began+duration;}
void NowPlayingPresentation::Scalar::set(double target,double time,double seconds,bool ease){if(target==to)return;from=sample(time);to=target;began=time;duration=seconds;eased=ease;if(seconds<=0)from=to;}
void NowPlayingPresentation::Scalar::settle()noexcept{from=to;duration=0;}
NowPlayingPresentation::NowPlayingPresentation(NowPlayingAppearance appearance):appearance_(appearance){valid(appearance_);updateActions();}
bool NowPlayingPresentation::setAppearance(NowPlayingAppearance appearance,double time){need(std::isfinite(time),"Now Playing requires a finite host clock");valid(appearance);if(appearance_==appearance)return false;appearance_=appearance;repaint(time);if(appearance.reducedMotion)settle();return true;}
void NowPlayingPresentation::setActive(bool value,double time){need(std::isfinite(time),"Now Playing requires a finite host clock");if(active_==value)return;active_=value;if(value)repaint(time);else{cancelDrag();showVolume_=false;volumeVisibility_.set(0,time,0,false);lastLyricPosition_.reset();settle();++revision_;}}
void NowPlayingPresentation::sync(NowPlayingViewInput next,double time){
    need(std::isfinite(time)&&std::isfinite(next.playbackRate)&&next.playbackRate>=0,"Invalid Now Playing clock/rate");need(!next.track||next.session,"Now Playing track requires its native session");need(!next.volume||(std::isfinite(*next.volume)&&*next.volume>=0&&*next.volume<=1),"Invalid Now Playing volume");need(!next.volumeAvailable||next.capabilities.volume,"An adjustable volume requires an explicit provider capability");
    if(next.track)next.track=NowPlayingTrack::bounded(std::move(*next.track));
    if(input_.session==next.session&&equal(input_.track,next.track)&&input_.capabilities==next.capabilities&&input_.failed==next.failed&&input_.lyrics==next.lyrics&&input_.coverAvailable==next.coverAvailable&&input_.coverRevision==next.coverRevision&&input_.volume==next.volume&&input_.volumeAvailable==next.volumeAvailable&&input_.playbackRate==next.playbackRate){updateLyrics(time);updateProgress(time,false);return;}
    if(drag_&&(drag_->session!=next.session||(drag_->kind==NowPlayingSliderKind::seek&&(!next.track||!drag_->track||!next.track->sameIdentity(*drag_->track)))))cancelDrag();
    const auto title=[](const auto&i)->std::string_view{return i.track?i.track->title:std::string_view{};};const auto artist=[](const auto&i)->std::string_view{return i.track?i.track->artist:std::string_view{};};
    const bool animate=active_&&rendered_&&!appearance_.reducedMotion;
    if(animate&&!title(input_).empty()&&title(input_)!=title(next))++titleTransition_;
    if(animate&&!artist(input_).empty()&&artist(input_)!=artist(next))++artistTransition_;
    if(animate&&(input_.coverAvailable!=next.coverAvailable||input_.coverRevision!=next.coverRevision))++coverTransition_;
    if(input_.session!=next.session||title(input_)!=title(next)||(input_.track?input_.track->album:"")!=(next.track?next.track->album:""))requestedEndRefresh_=false;
    input_=std::move(next);if(input_.track&&input_.track->duration){const auto current=playbackElapsed(time);if(current&&*current<*input_.track->duration-1)requestedEndRefresh_=false;}
    repaint(time);
}
std::optional<double>NowPlayingPresentation::elapsed(double time)const noexcept{if(drag_&&drag_->kind==NowPlayingSliderKind::seek)return dragValue_;return playbackElapsed(time);}
std::optional<double>NowPlayingPresentation::playbackElapsed(double time)const noexcept{if(!input_.track||!input_.track->position)return {};const auto&t=*input_.track;const auto delta=t.isPlaying&&std::isfinite(time)?std::max(0.,time-t.sampledAt)*input_.playbackRate:0.;return std::min(t.duration.value_or(nowPlayingMaximumSeconds),*t.position+delta);}
bool NowPlayingPresentation::lyricsVisible()const noexcept{return showLyrics_&&input_.lyrics&&!input_.lyrics->lines().empty();}
void NowPlayingPresentation::updateActions(){
    const bool track=bool(input_.track),transport=track&&!input_.failed,playing=track&&input_.track->isPlaying;const auto&c=input_.capabilities;
    const std::array enabled{track,transport&&c.previous,transport&&(c.toggle||(playing?c.pause:c.play)),transport&&c.next,track};
    const std::array<std::pair<const char*,const char*>,5>labels{{{"Lyrics","歌词"},{"Previous track","上一首"},{playing?"Pause":"Play",playing?"暂停":"播放"},{"Next track","下一首"},{"App volume","应用音量"}}};
    for(std::size_t k=0;k<5;++k)actions_[k]={static_cast<NowPlayingAction>(k),rects[k],core::localized(labels[k].first,labels[k].second,appearance_.language),enabled[k],k==0?showLyrics_:k==4&&showVolume_};
}
void NowPlayingPresentation::repaint(double time){
    lyricsVisibility_.set(lyricsVisible()?1:0,time,rendered_&&active_&&!appearance_.reducedMotion?lyricsFadeDuration:0,true);
    volumeVisibility_.set(showVolume_?1:0,time,active_&&!appearance_.reducedMotion?volumeFadeDuration:0,false);
    updateActions();durationText_=nowPlayingTime(input_.track?input_.track->duration:std::optional<double>{});
    volumeText_=input_.volumeAvailable?std::to_string(static_cast<int>(std::round(input_.volume.value_or(1)*100)))+"%":"—";
    updateLyrics(time);updateProgress(time,false);rendered_=true;++revision_;
}
void NowPlayingPresentation::updateProgress(double time,bool animated){const auto value=elapsed(time);const auto p=nowPlayingProgress(value,input_.track?input_.track->duration:std::optional<double>{},input_.track&&input_.track->isPlaying,dragging(),animated,appearance_.reducedMotion,input_.playbackRate);progress_={p.width,p.destination,time,p.duration,false};auto text=nowPlayingTime(value);if(elapsedText_!=text){elapsedText_=std::move(text);++revision_;}}
void NowPlayingPresentation::updateLyrics(double time){
    const auto position=elapsed(time);const auto next=input_.lyrics?input_.lyrics->window(position.value_or(0)):std::array<std::string_view,3>{};bool changed{},previous{};for(std::size_t k=0;k<3;++k){changed|=rows_[k]!=next[k];previous|=!rows_[k].empty();}
    const bool animate=active_&&rendered_&&lyricsVisible()&&!appearance_.reducedMotion&&changed&&previous;
    if(changed){for(std::size_t k=0;k<3;++k)rows_[k]=next[k];++revision_;}
    if(animate){lyricMovement_={position.value_or(0)>=lastLyricPosition_.value_or(0)?7.:-7.,0,time,lyricChangeDuration,true};lyricOpacity_={.88,1,time,lyricChangeDuration,true};}
    else if(!lyricsVisible()||appearance_.reducedMotion){lyricMovement_.settle();lyricOpacity_.settle();}
    lastLyricPosition_=position;
}
bool NowPlayingPresentation::tick(double time){need(std::isfinite(time),"Now Playing requires a finite host clock");if(!active_)return false;updateProgress(time,true);updateLyrics(time);const auto value=playbackElapsed(time);if(!requestedEndRefresh_&&input_.track&&input_.track->duration&&value&&*value>=*input_.track->duration){requestedEndRefresh_=true;return true;}return false;}
std::optional<double>NowPlayingPresentation::nextDisplayDeadline(double time)const noexcept{
    if(!active_||!input_.track||!input_.track->isPlaying||input_.playbackRate<=0||!std::isfinite(time))return {};const auto value=playbackElapsed(time);if(!value||!std::isfinite(*value)||(input_.track->duration&&*value>=*input_.track->duration))return {};
    double delay=(std::floor(*value)+1-*value)/input_.playbackRate;if(lyricsVisible()&&(!drag_||drag_->kind!=NowPlayingSliderKind::seek))if(auto boundary=input_.lyrics->nextBoundary(*value))delay=std::min(delay,(*boundary-*value)/input_.playbackRate);if(input_.track->duration)delay=std::min(delay,(*input_.track->duration-*value)/input_.playbackRate);return time+std::max(1./30,delay);
}
NowPlayingViewPose NowPlayingPresentation::sample(double time)const noexcept{const auto width=progress_.sample(time);return {width,55+width,lyricsVisibility_.sample(time),lyricMovement_.sample(time),lyricOpacity_.sample(time),volumeVisibility_.sample(time)};}
bool NowPlayingPresentation::requiresFrames(double time)const noexcept{return active_&&(progress_.live(time)||lyricsVisibility_.live(time)||lyricMovement_.live(time)||lyricOpacity_.live(time)||volumeVisibility_.live(time));}
void NowPlayingPresentation::settle()noexcept{progress_.settle();lyricsVisibility_.settle();lyricMovement_.settle();lyricOpacity_.settle();volumeVisibility_.settle();}
std::array<NowPlayingSliderFace,2>NowPlayingPresentation::sliders(double time)const noexcept{const auto&t=input_.track;return {{{NowPlayingSliderKind::seek,NowPlayingGeometry::seek,elapsed(time),0,t?t->duration.value_or(1):1,t&&t->supportsSeeking&&t->duration&&t->position&&!input_.failed&&input_.capabilities.seek},{NowPlayingSliderKind::volume,NowPlayingGeometry::volumeSlider,input_.volume?input_.volume:input_.volumeAvailable?std::optional<double>{1}:std::nullopt,0,1,input_.volumeAvailable}}};}
bool NowPlayingPresentation::containsControl(core::Point p,double time)const noexcept{if(!active_)return false;for(const auto&a:actions_)if(contains(a.rect,p))return true;const auto values=sliders(time);for(std::size_t k=0;k<sliderCount();++k)if(contains(values[k].rect,p))return true;return false;}
NowPlayingPointerResult NowPlayingPresentation::pointerDown(core::Point p,double time){need(std::isfinite(time),"Now Playing requires a finite host clock");if(!active_||!contains(NowPlayingGeometry::canvas,p))return {};if(showVolume_&&!contains(NowPlayingGeometry::volumeMenu,p)&&!contains(rects[4],p)){dismissVolume(time);return {true,{}};}const auto values=sliders(time);for(std::size_t k=0;k<sliderCount();++k)if(contains(values[k].rect,p)){if(values[k].enabled){drag_=Drag{values[k].kind,input_.session,input_.track};return {true,pointerMove(p,time)};}return {true,{}};}for(const auto&a:actions_)if(a.enabled&&contains(a.rect,p))return {true,perform(a.action,time)};return {true,{}};}
std::optional<NowPlayingIntent>NowPlayingPresentation::pointerMove(core::Point p,double time){need(std::isfinite(time),"Now Playing requires a finite host clock");if(!active_||!drag_||!std::isfinite(p.x)||!std::isfinite(p.y)||input_.session!=drag_->session)return {};const auto slider=sliders(time)[drag_->kind==NowPlayingSliderKind::seek?0:1];if(!slider.enabled)return {};const auto fraction=std::clamp(drag_->kind==NowPlayingSliderKind::seek?(p.x-slider.rect.x)/slider.rect.width:(slider.rect.y+slider.rect.height-p.y)/slider.rect.height,0.,1.);dragValue_=slider.minimum+fraction*(slider.maximum-slider.minimum);if(drag_->kind==NowPlayingSliderKind::seek){updateProgress(time,false);return {};}return setSlider(NowPlayingSliderKind::volume,*dragValue_,time);}
std::optional<NowPlayingIntent>NowPlayingPresentation::pointerUp(double time){need(std::isfinite(time),"Now Playing requires a finite host clock");if(!drag_)return {};const bool seek=drag_->kind==NowPlayingSliderKind::seek;const bool same=input_.session==drag_->session&&input_.track&&drag_->track&&input_.track->sameIdentity(*drag_->track);const auto value=dragValue_;cancelDrag();if(seek&&same&&value)return NowPlayingIntent{NowPlayingIntentKind::seek,input_.session,*value};return {};}
void NowPlayingPresentation::cancelDrag()noexcept{drag_.reset();dragValue_.reset();}
std::optional<NowPlayingIntent>NowPlayingPresentation::perform(NowPlayingAction action,double time){need(std::isfinite(time),"Now Playing requires a finite host clock");const auto index=static_cast<std::size_t>(action);if(!active_||index>=actions_.size()||!actions_[index].enabled)return {};cancelDrag();switch(action){case NowPlayingAction::lyrics:showLyrics_=!showLyrics_;repaint(time);return {};case NowPlayingAction::volume:showVolume_=!showVolume_;repaint(time);return {};case NowPlayingAction::previous:return NowPlayingIntent{NowPlayingIntentKind::previous,input_.session};case NowPlayingAction::playPause:return NowPlayingIntent{NowPlayingIntentKind::playPause,input_.session};case NowPlayingAction::next:return NowPlayingIntent{NowPlayingIntentKind::next,input_.session};}return {};}
std::optional<NowPlayingIntent>NowPlayingPresentation::setSlider(NowPlayingSliderKind kind,double value,double time){need(std::isfinite(time),"Now Playing requires a finite host clock");if(!active_||!std::isfinite(value)||(kind==NowPlayingSliderKind::volume&&!showVolume_))return {};const auto slider=sliders(time)[kind==NowPlayingSliderKind::seek?0:1];if(!slider.enabled)return {};return NowPlayingIntent{kind==NowPlayingSliderKind::seek?NowPlayingIntentKind::seek:NowPlayingIntentKind::volume,input_.session,std::clamp(value,slider.minimum,slider.maximum)};}
void NowPlayingPresentation::dismissVolume(double time){need(std::isfinite(time),"Now Playing requires a finite host clock");if(!showVolume_)return;showVolume_=false;repaint(time);}
}
