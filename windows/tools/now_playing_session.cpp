#include "tools/now_playing_session.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::tools {namespace {
std::optional<double>earlier(std::optional<double>a,std::optional<double>b){return a&&b?std::min(*a,*b):a?a:b;}
}
struct NowPlayingSession::Impl:std::enable_shared_from_this<Impl>{
 struct Seek{std::uint64_t token{},session{},readAtCompletion{};modules::NowPlayingTrack track;double value{},sampledAt{};std::optional<double>rollback;};
 native::NativeNowPlayingService&service;modules::NowPlayingPresentation view;std::function<void()>changed;std::unique_ptr<native::NativeNowPlayingArtwork>images;
 std::shared_ptr<const native::NotesImageFrame>image;std::shared_ptr<const modules::NowPlayingLyrics>lyrics;std::optional<std::string>lyricsSource;std::optional<Seek>seek;std::optional<double>displayDeadline;
 // One source warm presentation, populated only on conceal from actual
 // observed metadata. No provider/session is kept alive while hidden.
 std::optional<modules::NowPlayingViewInput>warm;std::string warmApplication;bool warmCoverHeld{};
 std::uint64_t serviceRevision{},imageRevision{},completion{},event{},coverRevision{};bool alive{true},active{},imageDirty{};double time{};
 Impl(native::NativeNowPlayingService&s,modules::NowPlayingAppearance a,std::function<void()>c):service(s),view(std::move(a)),changed(std::move(c)){}
 void initialize(app::UtilityExecutor&q,native::NativeNowPlayingArtwork::Decoder d){const auto weak=weak_from_this();images=std::make_unique<native::NativeNowPlayingArtwork>(q,[weak]{if(auto self=weak.lock();self&&self->alive&&self->active){self->imageDirty=true;auto callback=self->changed;if(callback)callback();}},std::move(d));}
 std::uint64_t begin(double t){if(!std::isfinite(t)||t<0)throw std::invalid_argument("Now Playing owner needs finite nonnegative time");time=std::max(time,t);return ++event;}
 bool current(std::uint64_t e)const{return alive&&event==e;}
 void publish(){if(!active)return;const auto&s=service.snapshot();
  if(!s.fresh&&warm){auto input=*warm;input.failed=true;input.coverAvailable=bool(image);input.coverRevision=coverRevision;serviceRevision=s.revision;imageDirty=false;imageRevision=images->revision();view.sync(std::move(input),time);displayDeadline=view.nextDisplayDeadline(time);return;}

  if(s.commandCompleted&&s.commandCompleted->request.token!=completion){const auto&done=*s.commandCompleted;completion=done.request.token;if(seek&&done.failure!=native::NowPlayingServiceFailure::none&&done.request.token>=seek->token)seek.reset();else if(seek&&seek->token==done.request.token){seek->rollback=done.completedAt+2.5;seek->readAtCompletion=done.mediaReadRevision;}}
  if(s.failure==native::NowPlayingServiceFailure::timedOut&&!s.busy)seek.reset();
  if(seek){if(!s.media.session||s.media.session->token!=seek->session||!s.media.track||!s.media.track->sameIdentity(seek->track))seek.reset();else if(seek->rollback&&s.mediaReadRevision>seek->readAtCompletion&&modules::nowPlayingAcknowledgesSeek(*s.media.track,seek->value,seek->sampledAt))seek.reset();}
  modules::NowPlayingViewInput input;input.session=s.media.session?s.media.session->token:0;input.track=s.media.track;input.capabilities=s.media.capabilities;input.failed=s.failure!=native::NowPlayingServiceFailure::none||!s.fresh;
  // GSMTC has no player-volume or timed-lyrics command. Preserve the original
  // unavailable rail. Only actual supplied timed lyrics may populate its rows.
  const auto nextLyrics=input.track?input.track->timedLyrics:std::nullopt;if(lyricsSource!=nextLyrics){lyricsSource=nextLyrics;lyrics.reset();if(nextLyrics)if(auto parsed=modules::NowPlayingLyrics::parse(*nextLyrics))lyrics=std::make_shared<const modules::NowPlayingLyrics>(std::move(*parsed));}input.lyrics=lyrics;
  if(seek&&input.track){input.track->position=seek->value;input.track->sampledAt=seek->sampledAt;}
  if(s.media.timeline)input.playbackRate=std::isfinite(s.media.timeline->rate)&&s.media.timeline->rate>0?s.media.timeline->rate:0;
  images->request(input.track?s.media.artwork:nullptr);imageDirty=false;imageRevision=images->revision();auto next=images->result().image;
  const auto imageWork=images->stats();const bool keepWarm=warmCoverHeld&&!next&&s.media.artwork&&(imageWork.inFlight||imageWork.pending)&&warm&&warm->track&&input.track&&input.track->sameIdentity(*warm->track)&&s.media.session&&s.media.session->appUserModelID==warmApplication;
  if(keepWarm)next=image;else{warmCoverHeld=false;warm.reset();warmApplication.clear();}
  if(image!=next){image=std::move(next);++coverRevision;}input.coverAvailable=bool(image);input.coverRevision=coverRevision;
  serviceRevision=s.revision;view.sync(std::move(input),time);displayDeadline=view.nextDisplayDeadline(time);
 }
};
NowPlayingSession::NowPlayingSession(native::NativeNowPlayingService&s,app::UtilityExecutor&q,modules::NowPlayingAppearance a,std::function<void()>c,native::NativeNowPlayingArtwork::Decoder d):impl_(std::make_shared<Impl>(s,std::move(a),std::move(c))){impl_->initialize(q,std::move(d));}
NowPlayingSession::~NowPlayingSession(){auto i=impl_;i->alive=false;++i->event;i->changed={};i->images.reset();if(i->active){i->active=false;i->service.setActive(false);}}
void NowPlayingSession::setActive(bool active,double t){auto i=impl_;const auto e=i->begin(t);if(i->active==active)return;
 if(!active){const auto&s=i->service.snapshot();if(s.fresh){if(s.media.session&&s.media.track&&(s.failure==native::NowPlayingServiceFailure::none||s.failure==native::NowPlayingServiceFailure::timedOut||s.failure==native::NowPlayingServiceFailure::unavailable)){i->warm=i->view.input();i->warm->track=s.media.track;i->warm->failed=false;i->warmApplication=s.media.session->appUserModelID;}else{i->warm.reset();i->warmApplication.clear();}}}
 i->active=active;i->seek.reset();i->completion=0;i->view.setActive(active,i->time);if(!active){i->displayDeadline.reset();i->images->cancel();i->imageDirty=false;i->service.setActive(false);return;}i->warmCoverHeld=i->warm&&bool(i->image);i->service.setActive(true);if(i->current(e)&&i->active)i->publish();}
bool NowPlayingSession::active()const noexcept{return impl_->active;}
bool NowPlayingSession::utilityCompleted(double t){auto i=impl_;const auto e=i->begin(t);if(!i->active)return false;const bool updated=i->service.drain();if(!i->current(e)||!i->active)return updated;i->images->submitPending();const bool changed=updated||i->serviceRevision!=i->service.snapshot().revision||i->imageDirty||i->imageRevision!=i->images->revision();if(changed)i->publish();return changed;}
bool NowPlayingSession::deadline(double t){auto i=impl_;const auto e=i->begin(t);if(!i->active)return false;bool changed{};if(const auto due=i->service.nextWakeTime();due&&*due<=i->time){changed=i->service.drain();if(!i->current(e)||!i->active)return changed;}
 if(i->seek&&i->seek->rollback&&*i->seek->rollback<=i->time){i->seek.reset();changed=true;}if(changed||i->serviceRevision!=i->service.snapshot().revision)i->publish();
 if(i->displayDeadline&&*i->displayDeadline<=i->time){if(i->view.tick(i->time)){i->service.refresh();if(!i->current(e)||!i->active)return true;}changed=true;i->displayDeadline=i->view.nextDisplayDeadline(i->time);}return changed;
}
bool NowPlayingSession::dispatch(modules::NowPlayingIntent intent,double t){auto i=impl_;const auto e=i->begin(t);if(!i->active)return false;const auto&s=i->service.snapshot();if(!s.media.session||s.media.session->token!=intent.session||!s.media.track)return false;
 modules::NowPlayingCommand command;switch(intent.kind){case modules::NowPlayingIntentKind::playPause:command.kind=modules::NowPlayingCommandKind::playPause;break;case modules::NowPlayingIntentKind::previous:command.kind=modules::NowPlayingCommandKind::previous;break;case modules::NowPlayingIntentKind::next:command.kind=modules::NowPlayingCommandKind::next;break;case modules::NowPlayingIntentKind::seek:command={modules::NowPlayingCommandKind::seek,intent.value};break;case modules::NowPlayingIntentKind::volume:return false;}
 if(!i->service.perform(command)||!i->current(e)||!i->active)return false;const auto&receipt=*i->service.snapshot().commandAccepted;if(command.kind==modules::NowPlayingCommandKind::seek)i->seek=Impl::Seek{receipt.token,receipt.session,0,*s.media.track,receipt.command.seconds,i->time,{}};else if(i->seek&&receipt.replacedPending==i->seek->token)i->seek.reset();i->publish();return true;
}
void NowPlayingSession::presentationChanged(double t){auto i=impl_;i->begin(t);i->displayDeadline=i->active?i->view.nextDisplayDeadline(i->time):std::nullopt;}
std::optional<double>NowPlayingSession::nextWakeTime(double)const{auto&i=*impl_;if(!i.active)return {};auto due=earlier(i.service.nextWakeTime(),i.displayDeadline);return earlier(due,i.seek?i.seek->rollback:std::nullopt);}
modules::NowPlayingPresentation&NowPlayingSession::presentation()noexcept{return impl_->view;}
const modules::NowPlayingPresentation&NowPlayingSession::presentation()const noexcept{return impl_->view;}
std::shared_ptr<const native::NotesImageFrame>NowPlayingSession::artwork()const noexcept{return impl_->image;}
native::NativeNowPlayingArtwork::Stats NowPlayingSession::artworkStats()const{return impl_->images->stats();}
bool NowPlayingSession::optimisticSeek()const noexcept{return impl_->seek.has_value();}
}
