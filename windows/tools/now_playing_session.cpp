#include "tools/now_playing_session.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::tools {namespace {
std::optional<double>earlier(std::optional<double>a,std::optional<double>b){return a&&b?std::min(*a,*b):a?a:b;}
std::string_view actionKey(modules::NowPlayingCommandKind kind){
    switch(kind){case modules::NowPlayingCommandKind::playPause:return "playPause";case modules::NowPlayingCommandKind::previous:return "previous";
    case modules::NowPlayingCommandKind::next:return "next";case modules::NowPlayingCommandKind::seek:return "seek";}
    return "playPause";
}
}
struct NowPlayingSession::Impl:std::enable_shared_from_this<Impl>{
 struct Seek{std::uint64_t token{},session{},readAtCompletion{};modules::NowPlayingTrack track;double value{},sampledAt{};std::optional<double>rollback;};
 // Catalog cover supplement for a player that supplied no thumbnail. Only the
 // current cover's compressed bytes are retained (<= 8 MiB, one entry).
 // A window-title player's own cover (the URL its player bar shows) comes
 // first, the catalog's as supplement (source NowPlayingArtworkLoader: the
 // player's AX image URL, then the catalog); a track keeps the cover it
 // already has, and a failed address gives way to the other one.
 struct Cover{std::optional<modules::NowPlayingLyricsKey>key;std::string url;std::vector<std::string>tried;modules::NowPlayingBody bytes;std::function<void()>cancel;bool inFlight{},done{};};
 // Recently downloaded catalog covers by URL (bounded, kept while hidden):
 // returning to one of the last tracks shows its cover at once, and the
 // decoded thumbnails (keyed by these exact bytes) stay valid for them too.
 std::vector<std::pair<std::string,modules::NowPlayingBody>>recentCovers;std::size_t recentCoverBytes{};
 void remember(const std::string&url,modules::NowPlayingBody bytes){
  if(!bytes||bytes->size()>maximumRecentCoverBytes)return;
  forget(url);recentCoverBytes+=bytes->size();recentCovers.emplace_back(url,std::move(bytes));
  while(recentCovers.size()>maximumRecentCovers||recentCoverBytes>maximumRecentCoverBytes){recentCoverBytes-=recentCovers.front().second->size();recentCovers.erase(recentCovers.begin());}
 }
 void forget(const std::string&url){const auto found=std::find_if(recentCovers.begin(),recentCovers.end(),[&](const auto&e){return e.first==url;});if(found==recentCovers.end())return;recentCoverBytes-=found->second->size();recentCovers.erase(found);}
 modules::NowPlayingBody recalled(const std::string&url){
  const auto found=std::find_if(recentCovers.begin(),recentCovers.end(),[&](const auto&e){return e.first==url;});if(found==recentCovers.end())return nullptr;
  auto entry=std::move(*found);recentCovers.erase(found);recentCovers.push_back(std::move(entry));return recentCovers.back().second;
 }
 native::NativeNowPlayingService&service;modules::NowPlayingPresentation view;std::function<void()>changed;std::unique_ptr<native::NativeNowPlayingArtwork>images;
 std::shared_ptr<native::NowPlayingWeb>web;std::unique_ptr<modules::NowPlayingLyricsSources>sources;Cover cover;
 std::shared_ptr<const modules::NowPlayingAppVolume>volume;std::function<void(const modules::NowPlayingPlaybackEvent&)>playback;
 std::shared_ptr<const native::NotesImageFrame>image;std::optional<Seek>seek;std::optional<double>displayDeadline;
 // One source warm presentation, populated only on conceal from actual
 // observed metadata. No provider/session is kept alive while hidden.
 std::optional<modules::NowPlayingViewInput>warm;std::string warmApplication;bool warmCoverHeld{};
 // Event Log source per accepted command (bounded; commands are serialized).
 std::vector<std::pair<std::uint64_t,modules::NowPlayingSource>>commandSources;
 std::string volumeRouteID;std::optional<std::pair<std::string,double>>pendingVolume;std::uint64_t volumeRevision{};
 std::vector<std::pair<std::string,std::optional<std::string>>>identities;
 std::uint64_t serviceRevision{},imageRevision{},completion{},event{},coverRevision{},refreshRead{};bool alive{true},active{},imageDirty{},sourcesDirty{},retryMissing{};double time{};
 unsigned coverPixels{600};
 // A window-title player's progress bar: the position last presented for the
 // track. The bar is drawn a little behind the player's clock (it updates in
 // steps), so a reading at most progressLag seconds behind the running time
 // keeps the running time; anything else (a seek, a pause) resynchronizes.
 struct Progress{std::optional<modules::NowPlayingLyricsKey>key;double position{},sampledAt{};bool playing{};};
 std::optional<Progress>progress;std::optional<double>controlsRead;
 static constexpr double progressLag=1.5,controlsInterval=.9;
 // Downloaded lyric/catalog JSON is decoded on the shared utility worker (a
 // 1 MiB LRCLIB search can take tens of milliseconds); results return through
 // the host's utility drain like decoded artwork. Hide discards queued work.
 app::UtilityExecutor*utility{};app::UtilityExecutor::Route decodeRoute{};
 Impl(native::NativeNowPlayingService&s,modules::NowPlayingAppearance a,std::function<void()>c):service(s),view(std::move(a)),changed(std::move(c)){}
 void initialize(app::UtilityExecutor&q,NowPlayingSessionOptions options){
  const auto weak=weak_from_this();
  images=std::make_unique<native::NativeNowPlayingArtwork>(q,[weak]{if(auto self=weak.lock();self&&self->alive&&self->active){self->imageDirty=true;auto callback=self->changed;if(callback)callback();}},std::move(options.decode));
  web=std::move(options.web);volume=std::move(options.volume);playback=std::move(options.playback);coverPixels=options.coverPixels;
  utility=&q;if(web)decodeRoute=q.makeRoute();
  modules::NowPlayingDecoder decoder=modules::nowPlayingInlineDecoder();
  if(web)decoder=[weak](std::function<void()>work,std::function<void()>done){
   const auto self=weak.lock();if(!self||!self->alive)return;
   bool queued{};
   if(self->decodeRoute)try{queued=self->utility->submit(self->decodeRoute,work,[weak,done](std::exception_ptr){
    const auto owner=weak.lock();if(!owner||!owner->alive)return;done();
    if(owner->alive&&owner->active&&owner->sourcesDirty){auto callback=owner->changed;if(callback)callback();}});}catch(const std::logic_error&){queued=false;}
   // A full (or stopped) shared queue decodes inline, bounded, rather than polling or dropping.
   if(!queued){work();done();}
  };
  sources=std::make_unique<modules::NowPlayingLyricsSources>(web?web->lyricsFetch():modules::nowPlayingOfflineFetch(),bool(web),std::move(decoder));
  if(options.neteasePlayingList)sources->setPlayingList(std::move(options.neteasePlayingList));
  Impl*self=this;sources->onChange=[self]{self->sourcesDirty=true;};
  commandSources.reserve(9);
 }
 void discardDecodes()noexcept{if(!utility||!decodeRoute)return;try{utility->invalidate(decodeRoute);}catch(...){}decodeRoute=0;try{decodeRoute=utility->makeRoute();}catch(...){decodeRoute=0;}}
 std::uint64_t begin(double t){if(!std::isfinite(t)||t<0)throw std::invalid_argument("Now Playing owner needs finite nonnegative time");time=std::max(time,t);return ++event;}
 bool current(std::uint64_t e)const{return alive&&event==e;}
 void stopCover(){auto cancel=std::move(cover.cancel);cover.cancel=nullptr;if(cover.inFlight){cover.inFlight=false;cover.done=false;cover.key.reset();cover.url.clear();}if(cancel)cancel();}
 // Source NowPlayingArtworkLoader: the player's embedded thumbnail first, then
 // an allow-listed catalog supplement downloaded once per track and URL.
 modules::NowPlayingBody remoteCover(const std::optional<modules::NowPlayingLyricsKey>&key,const std::optional<std::string>&own){
  if(!web||!key)return nullptr;
  std::vector<std::string>addresses;
  if(own)if(auto url=modules::nowPlayingCatalogCoverURL(*own,coverPixels))addresses.push_back(std::move(*url));
  if(const auto catalog=sources->catalogArtwork())if(auto url=modules::nowPlayingCatalogCoverURL(*catalog,coverPixels);url&&std::find(addresses.begin(),addresses.end(),*url)==addresses.end())addresses.push_back(std::move(*url));
  const bool same=cover.key&&*cover.key==*key;
  if(same&&(cover.inFlight||cover.bytes))return cover.bytes;
  std::optional<std::string>url;
  for(const auto&address:addresses)if(!same||std::find(cover.tried.begin(),cover.tried.end(),address)==cover.tried.end()){url=address;break;}
  if(!url){if(!same)stopCover();return same?cover.bytes:nullptr;}
  if(!same){stopCover();cover.tried.clear();}
  cover.key=key;cover.url=*url;cover.tried.push_back(*url);cover.bytes.reset();cover.done=false;
  if(auto bytes=recalled(*url)){cover.bytes=std::move(bytes);cover.done=true;return cover.bytes;}
  cover.inFlight=true;
  const auto weak=weak_from_this();const auto expected=cover.url;
  auto cancel=web->fetch({*url,native::NowPlayingWeb::Kind::artwork},[weak,expected](modules::NowPlayingBody body){
   const auto self=weak.lock();if(!self||self->cover.url!=expected||!self->cover.inFlight)return;
   self->cover.inFlight=false;self->cover.done=true;self->cover.cancel=nullptr;self->cover.bytes=std::move(body);self->sourcesDirty=true;
   if(self->cover.bytes)self->remember(expected,self->cover.bytes);
  });
  if(cover.inFlight)cover.cancel=std::move(cancel);
  return cover.bytes;
 }
 const modules::NowPlayingAudioRoute*volumeTarget(const std::vector<modules::NowPlayingAudioRoute>&routes,std::string_view application){
  if(!volume||!volume->routes||application.empty())return nullptr;
  std::function<std::optional<std::string>(const modules::NowPlayingAudioRoute&)>identity;
  if(volume->processAppUserModelID&&!modules::nowPlayingWin32Identity(application))identity=[this](const modules::NowPlayingAudioRoute&route)->std::optional<std::string>{
   for(const auto&[id,value]:identities)if(id==route.id)return value;
   auto value=volume->processAppUserModelID(route);
   if(identities.size()>=64)identities.erase(identities.begin());
   identities.emplace_back(route.id,value);return value;
  };
  const auto index=modules::nowPlayingAudioRouteFor(application,routes,identity);
  return index?&routes[*index]:nullptr;
 }
 // Returns whether the resolved process route renders audio (when captured).
 std::optional<bool>applyVolume(modules::NowPlayingViewInput&input,std::string_view application){
  std::vector<modules::NowPlayingAudioRoute>routes;if(volume&&volume->routes&&input.track)routes=volume->routes();
  if(volume&&volume->revision)volumeRevision=volume->revision();
  const auto*route=input.track?volumeTarget(routes,application):nullptr;
  const std::string id=route?route->id:std::string();
  // A drag never continues onto a different process route.
  if(id!=volumeRouteID&&view.capturesPointer()&&view.dragging())view.cancelDrag();
  volumeRouteID=id;
  const auto face=modules::nowPlayingVolumeFace(route);
  input.volumeAvailable=face.available;input.capabilities.volume=face.available;input.volume=face.value;
  if(pendingVolume){
   const bool sameRoute=route&&pendingVolume->first==route->id&&route->state!=modules::NowPlayingAudioRouteState::failed;
   const bool confirmed=sameRoute&&face.value&&std::abs(*face.value-pendingVolume->second)<0.0005;
   if(!sameRoute||confirmed)pendingVolume.reset();else input.volume=pendingVolume->second;
  }
  return route?route->rendering:std::nullopt;
 }
 void publish(){if(!active)return;const auto&s=service.snapshot();sourcesDirty=false;
  if(!s.fresh&&warm){auto input=*warm;input.failed=true;
   // Source activate(): the warm snapshot's lyrics come from the same bounded caches.
   std::optional<modules::NowPlayingLyricsKey>key;if(input.track&&!warmApplication.empty())key=modules::NowPlayingLyricsKey::make(warmApplication,*input.track);
   sources->update(key,input.track);sourcesDirty=false;input.lyrics=sources->lyrics();
   // An unconfirmed cached session never targets a live audio route.
   input.volumeAvailable=false;input.capabilities.volume=false;input.volume.reset();
   input.coverAvailable=bool(image);input.coverRevision=coverRevision;serviceRevision=s.revision;imageDirty=false;imageRevision=images->revision();view.sync(std::move(input),time);displayDeadline=view.nextDisplayDeadline(time);return;}

  if(s.commandCompleted&&s.commandCompleted->request.token!=completion){const auto&done=*s.commandCompleted;completion=done.request.token;
   if(seek&&done.failure!=native::NowPlayingServiceFailure::none&&done.request.token>=seek->token)seek.reset();else if(seek&&seek->token==done.request.token){seek->rollback=done.completedAt+2.5;seek->readAtCompletion=done.mediaReadRevision;}
   const auto found=std::find_if(commandSources.begin(),commandSources.end(),[&](const auto&p){return p.first==done.request.token;});
   if(found!=commandSources.end()){const auto source=found->second;commandSources.erase(commandSources.begin(),found+1);
    if(done.failure==native::NowPlayingServiceFailure::none&&playback){auto callback=playback;callback({actionKey(done.request.command.kind),source});if(!alive||!active)return;}}}
  if(s.failure==native::NowPlayingServiceFailure::timedOut&&!s.busy)seek.reset();
  if(seek){if(!s.media.session||s.media.session->token!=seek->session||!s.media.track||!s.media.track->sameIdentity(seek->track))seek.reset();else if(seek->rollback&&s.mediaReadRevision>seek->readAtCompletion&&modules::nowPlayingAcknowledgesSeek(*s.media.track,seek->value,seek->sampledAt))seek.reset();}
  modules::NowPlayingViewInput input;input.session=s.media.session?s.media.session->token:0;input.track=s.media.track;input.capabilities=s.media.capabilities;input.failed=s.failure!=native::NowPlayingServiceFailure::none||!s.fresh;
  const std::string application=s.media.session?s.media.session->appUserModelID:std::string();
  // Source changed(): embedded timed lyrics, the NetEase catalog for NetEase,
  // then LRCLIB. GSMTC itself never supplies timed lyrics or player volume.
  std::optional<modules::NowPlayingLyricsKey>key;if(input.track&&s.media.session)key=modules::NowPlayingLyricsKey::make(application,*input.track);
  if(retryMissing&&s.mediaReadRevision!=refreshRead){retryMissing=false;if(s.failure==native::NowPlayingServiceFailure::none&&key){sources->update(key,input.track);sources->invalidateMissing();if(cover.key&&*cover.key==*key&&cover.done&&!cover.bytes){cover.key.reset();cover.url.clear();cover.tried.clear();cover.done=false;}}}
  sources->update(key,input.track);
  if(seek&&input.track){input.track->position=seek->value;input.track->sampledAt=seek->sampledAt;}
  if(s.media.timeline)input.playbackRate=std::isfinite(s.media.timeline->rate)&&s.media.timeline->rate>0?s.media.timeline->rate:0;
  auto artwork=input.track?s.media.artwork:nullptr;bool remotePending{};
  // A player thumbnail always wins; an in-flight supplement is then retired.
  if(input.track&&!artwork){artwork=remoteCover(key,s.media.artworkURL);remotePending=cover.inFlight;}else stopCover();
  sourcesDirty=false;input.lyrics=sources->lyrics();
  const auto rendering=applyVolume(input,application);
  const bool playerWindow=s.media.session&&s.media.session->origin==modules::NowPlayingSessionOrigin::playerWindow;
  // The time comes from the player's own window: a window-title session, or a
  // media session of the same player whose timeline is unusable (NetEase's
  // own SMTC leaves it all zero; nowPlayingMergeReads supplies the window's).
  const bool playerTime=playerWindow||s.media.playerTime;
  // A window title carries no length: the matched catalog recording's does
  // (NetEase: its own queue file's exact length), so the elapsed/total times
  // and the progress can show.
  if(input.track&&playerTime&&!input.track->duration)if(const auto length=sources->catalogDuration())input.track->duration=*length;
  // The player's own progress bar: its played fraction of that length.
  if(input.track&&playerTime&&s.media.progress&&input.track->duration)input.track->position=progressPosition(key,*input.track,*s.media.progress);
  else if(!s.media.progress)progress.reset();
  // A window-title player's title does not report pausing: its single
  // process route's audio activity (AudioSessionStateActive) does, and it is
  // published event-driven by the shared audio worker. The estimated elapsed
  // time is re-based at this moment, so it neither jumps back to the last
  // read nor keeps running until the provider's next read. A player whose
  // own controls were read states its play state itself (its audio fades out
  // for a second after it pauses).
  if(rendering&&input.track&&playerWindow&&!s.media.playerControls&&input.track->isPlaying!=*rendering){
   if(input.track->position)input.track->position=input.track->elapsed(time);
   input.track->sampledAt=time;input.track->isPlaying=*rendering;
  }
  images->request(artwork);imageDirty=false;imageRevision=images->revision();auto next=images->result().image;
  const auto imageWork=images->stats();const bool decoding=artwork&&(imageWork.inFlight||imageWork.pending);
  const bool keepWarm=warmCoverHeld&&!next&&(decoding||remotePending)&&warm&&warm->track&&input.track&&input.track->sameIdentity(*warm->track)&&s.media.session&&s.media.session->appUserModelID==warmApplication;
  if(keepWarm)next=image;else{warmCoverHeld=false;warm.reset();warmApplication.clear();}
  if(image!=next){image=std::move(next);++coverRevision;}input.coverAvailable=bool(image);input.coverRevision=coverRevision;
  serviceRevision=s.revision;view.sync(std::move(input),time);displayDeadline=view.nextDisplayDeadline(time);
 }
 double progressPosition(const std::optional<modules::NowPlayingLyricsKey>&key,const modules::NowPlayingTrack&track,const native::NowPlayingMediaSnapshot::Progress&bar){
  const double duration=*track.duration;
  double position=std::clamp(bar.fraction*duration+bar.played,0.,duration);
  if(progress&&key&&progress->key&&*progress->key==*key&&progress->playing&&track.isPlaying){
   const double running=std::min(duration,progress->position+std::max(0.,track.sampledAt-progress->sampledAt));
   if(position<running&&running-position<=progressLag)position=running;
  }
  progress=Progress{key,position,track.sampledAt,track.isPlaying};
  return position;
 }
 // Once per displayed second while a window-title player's time runs (the
 // Mac re-reads its player each time the elapsed label changes): its own
 // controls are read anew, never more often, never while paused or hidden.
 void readControls(){
  const auto&s=service.snapshot();
  if(!active||!s.media.playerControls||!s.media.track||!s.media.track->isPlaying)return;
  if(controlsRead&&time-*controlsRead<controlsInterval)return;
  controlsRead=time;service.resynchronize();
 }
 bool dispatchVolume(double value){
  if(!volume||volumeRouteID.empty()||!volume->routes)return false;
  const auto routes=volume->routes();const auto found=std::find_if(routes.begin(),routes.end(),[&](const auto&r){return r.id==volumeRouteID;});
  if(found==routes.end())return false;value=std::clamp(value,0.,1.);
  switch(modules::nowPlayingVolumeCommand(*found,value)){
  case modules::NowPlayingVolumeCommand::rejected:return false;
  case modules::NowPlayingVolumeCommand::unchanged:return true;
  case modules::NowPlayingVolumeCommand::setGain:if(!volume->setGain||!volume->setGain(found->id,value))return false;pendingVolume=std::pair(found->id,value);break;
  case modules::NowPlayingVolumeCommand::stop:if(!volume->stop||!volume->stop(found->id))return false;pendingVolume=std::pair(found->id,1.);break;
  }
  publish();return true;
 }
};
NowPlayingSession::NowPlayingSession(native::NativeNowPlayingService&s,app::UtilityExecutor&q,modules::NowPlayingAppearance a,std::function<void()>c,native::NativeNowPlayingArtwork::Decoder d)
 :NowPlayingSession(s,q,std::move(a),std::move(c),NowPlayingSessionOptions{std::move(d),{},{},{}}){}
NowPlayingSession::NowPlayingSession(native::NativeNowPlayingService&s,app::UtilityExecutor&q,modules::NowPlayingAppearance a,std::function<void()>c,NowPlayingSessionOptions o):impl_(std::make_shared<Impl>(s,std::move(a),std::move(c))){impl_->initialize(q,std::move(o));}
NowPlayingSession::~NowPlayingSession(){auto i=impl_;i->alive=false;++i->event;i->changed={};i->playback={};i->stopCover();i->sources->hide();if(i->utility&&i->decodeRoute)try{i->utility->invalidate(i->decodeRoute);}catch(...){}i->decodeRoute=0;i->images.reset();
 if(i->active){i->active=false;i->service.setActive(false);if(i->volume&&i->volume->setActive)i->volume->setActive(false);}}
void NowPlayingSession::setActive(bool active,double t){auto i=impl_;const auto e=i->begin(t);if(i->active==active)return;
 if(!active){const auto&s=i->service.snapshot();if(s.fresh){if(s.media.session&&s.media.track&&(s.failure==native::NowPlayingServiceFailure::none||s.failure==native::NowPlayingServiceFailure::timedOut||s.failure==native::NowPlayingServiceFailure::unavailable)){i->warm=i->view.input();i->warm->track=s.media.track;i->warm->failed=false;i->warmApplication=s.media.session->appUserModelID;}else{i->warm.reset();i->warmApplication.clear();}}}
 i->active=active;i->seek.reset();i->completion=0;i->commandSources.clear();i->pendingVolume.reset();i->retryMissing=false;i->progress.reset();i->controlsRead.reset();i->view.setActive(active,i->time);
 if(!active){i->displayDeadline.reset();i->images->cancel();i->imageDirty=false;i->stopCover();i->sources->hide();i->discardDecodes();i->sourcesDirty=false;i->identities.clear();i->volumeRouteID.clear();i->service.setActive(false);if(i->volume&&i->volume->setActive)i->volume->setActive(false);return;}
 i->warmCoverHeld=i->warm&&bool(i->image);if(i->web)i->web->prepare();if(i->volume&&i->volume->setActive)i->volume->setActive(true);if(!i->current(e))return;i->service.setActive(true);if(i->current(e)&&i->active)i->publish();}
bool NowPlayingSession::active()const noexcept{return impl_->active;}
bool NowPlayingSession::utilityCompleted(double t){auto i=impl_;const auto e=i->begin(t);if(!i->active)return false;const bool updated=i->service.drain();if(!i->current(e)||!i->active)return updated;
 const bool downloaded=i->web&&i->web->drain();if(!i->current(e)||!i->active)return updated||downloaded;
 i->images->submitPending();const bool changed=updated||downloaded||i->sourcesDirty||i->serviceRevision!=i->service.snapshot().revision||i->imageDirty||i->imageRevision!=i->images->revision();if(changed)i->publish();return changed;}
bool NowPlayingSession::deadline(double t){auto i=impl_;const auto e=i->begin(t);if(!i->active)return false;bool changed{};if(const auto due=i->service.nextWakeTime();due&&*due<=i->time){changed=i->service.drain();if(!i->current(e)||!i->active)return changed;}
 if(i->web)if(const auto due=i->web->nextWakeTime();due&&*due<=i->time){changed=i->web->drain()||changed;if(!i->current(e)||!i->active)return changed;}
 if(i->seek&&i->seek->rollback&&*i->seek->rollback<=i->time){i->seek.reset();changed=true;}if(changed||i->sourcesDirty||i->serviceRevision!=i->service.snapshot().revision)i->publish();
 if(i->displayDeadline&&*i->displayDeadline<=i->time){if(i->view.tick(i->time)){i->service.refresh();if(!i->current(e)||!i->active)return true;}else{i->readControls();if(!i->current(e)||!i->active)return true;}changed=true;i->displayDeadline=i->view.nextDisplayDeadline(i->time);}return changed;
}
bool NowPlayingSession::audioChanged(double t){auto i=impl_;i->begin(t);if(!i->active||!i->volume)return false;const auto revision=i->volume->revision?i->volume->revision():i->volumeRevision+1;if(revision==i->volumeRevision)return false;i->publish();return true;}
void NowPlayingSession::refreshManually(double t){auto i=impl_;i->begin(t);if(!i->active)return;i->retryMissing=true;i->refreshRead=i->service.snapshot().mediaReadRevision;i->service.refresh();}
bool NowPlayingSession::dispatch(modules::NowPlayingIntent intent,double t){auto i=impl_;const auto e=i->begin(t);if(!i->active)return false;const auto&s=i->service.snapshot();if(!s.media.session||s.media.session->token!=intent.session||!s.media.track)return false;
 modules::NowPlayingCommand command;switch(intent.kind){case modules::NowPlayingIntentKind::playPause:command.kind=modules::NowPlayingCommandKind::playPause;break;case modules::NowPlayingIntentKind::previous:command.kind=modules::NowPlayingCommandKind::previous;break;case modules::NowPlayingIntentKind::next:command.kind=modules::NowPlayingCommandKind::next;break;case modules::NowPlayingIntentKind::seek:command={modules::NowPlayingCommandKind::seek,intent.value};break;
 case modules::NowPlayingIntentKind::volume:return s.fresh&&i->dispatchVolume(intent.value);}
 const auto source=modules::nowPlayingSourceForAppUserModelID(s.media.session->appUserModelID);
 if(!i->service.perform(command)||!i->current(e)||!i->active)return false;const auto&receipt=*i->service.snapshot().commandAccepted;
 if(i->commandSources.size()>=8)i->commandSources.erase(i->commandSources.begin());i->commandSources.emplace_back(receipt.token,source);
 if(command.kind==modules::NowPlayingCommandKind::seek)i->seek=Impl::Seek{receipt.token,receipt.session,0,*s.media.track,receipt.command.seconds,i->time,{}};else if(i->seek&&receipt.replacedPending==i->seek->token)i->seek.reset();i->publish();return true;
}
void NowPlayingSession::presentationChanged(double t){auto i=impl_;i->begin(t);i->displayDeadline=i->active?i->view.nextDisplayDeadline(i->time):std::nullopt;}
std::optional<double>NowPlayingSession::nextWakeTime(double)const{auto&i=*impl_;if(!i.active)return {};auto due=earlier(i.service.nextWakeTime(),i.displayDeadline);if(i.web)due=earlier(due,i.web->nextWakeTime());return earlier(due,i.seek?i.seek->rollback:std::nullopt);}
modules::NowPlayingPresentation&NowPlayingSession::presentation()noexcept{return impl_->view;}
const modules::NowPlayingPresentation&NowPlayingSession::presentation()const noexcept{return impl_->view;}
std::shared_ptr<const native::NotesImageFrame>NowPlayingSession::artwork()const noexcept{return impl_->image;}
native::NativeNowPlayingArtwork::Stats NowPlayingSession::artworkStats()const{return impl_->images->stats();}
bool NowPlayingSession::optimisticSeek()const noexcept{return impl_->seek.has_value();}
const std::string&NowPlayingSession::volumeRoute()const noexcept{return impl_->volumeRouteID;}
const modules::NowPlayingLyricsSources&NowPlayingSession::lyricsSources()const noexcept{return *impl_->sources;}
void NowPlayingSession::setCoverPixels(unsigned value){impl_->coverPixels=value;}
bool NowPlayingSession::park(){auto&i=*impl_;if(i.active)return false;const auto before=i.images->stats().residentBytes;const bool covers=!i.recentCovers.empty();
 i.images->trim(i.image);i.recentCovers.clear();i.recentCovers.shrink_to_fit();i.recentCoverBytes=0;
 if(!i.cover.inFlight&&i.cover.bytes&&!(i.warm&&i.warm->track)){i.cover={};}
 return covers||i.images->stats().residentBytes<before;}
NowPlayingSessionMemory NowPlayingSession::memory()const{
 const auto&i=*impl_;NowPlayingSessionMemory m;m.recentCovers=i.recentCovers.size();m.recentCoverBytes=i.recentCoverBytes;
 if(i.cover.bytes&&std::none_of(i.recentCovers.begin(),i.recentCovers.end(),[&](const auto&e){return e.second==i.cover.bytes;}))m.coverBytes=i.cover.bytes->size();
 m.artworkBytes=i.images->stats().residentBytes;if(i.image&&!m.artworkBytes)m.artworkBytes=i.image->straightRGBA.size();
 return m;}
}
