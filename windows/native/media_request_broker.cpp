#include "native/media_request_broker.hpp"
#ifdef _WIN32
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>
namespace endfield::native {namespace {
using Channel=core::MediaRequestChannel;using Client=core::MediaClient;using Request=core::ScopedMediaRequest;using Binding=core::ScopedMediaBinding;
void need(bool ok,const char*why){if(!ok)throw std::invalid_argument(why);}
unsigned index(Channel c){return static_cast<unsigned>(c);}
Request imageRequest(const NotesImagePlaybackRequest&r){return {r.image.key,r.image.path,r.image.revision,r.image.maximumDimension,r.kind,r.image.firstFrameOnly,r.image.videoPoster,r.image.allowVideoInspection,false,{},r.image.accessLease};}
Request videoRequest(const NotesVideoRequest&r){return {r.key,r.path,r.revision,r.maximumDimension,modules::NotesMediaKind::video,false,false,false,r.workerPoster,r.duration,r.accessLease};}
Request inspectionRequest(const NotesImageRequest&r){return {r.key,r.path,r.revision,64,modules::NotesMediaKind::image,false,r.videoPoster,r.allowVideoInspection,false,{},r.accessLease};}
NotesImageRequest decodeRequest(const Binding&r){const auto&v=r.request;return {r.globalKey,v.path,v.revision,v.maximumDimension,v.firstFrameOnly,v.accessLease,v.allowVideoInspection,v.videoPoster};}
NotesVideoRequest movieRequest(const Binding&r){const auto&v=r.request;return {r.globalKey,v.path,v.revision,v.maximumDimension,v.duration,v.accessLease,v.workerPoster};}
struct ProviderOwners {std::mutex mutex;std::set<const NativeNotesImageDecoder*>decoders;};
ProviderOwners&providerOwners(){static ProviderOwners value;return value;}
}
struct NativeMediaRequestBroker::Impl {
 struct Known {std::string global;std::uint64_t revision{};};using KnownMap=std::map<std::string,Known,std::less<>>;
 struct Owner {std::array<KnownMap,2>known;};struct Retired {Client client{};Channel channel{};std::string global;};
 struct Inspection {Client client{};std::uint64_t generation{},revision{};std::string local;};
 struct Ready {Client client{};std::uint64_t generation{};NotesImageCompletion value;std::size_t bytes{};};
 NativeNotesImageDecoder&decoder;NativeNotesImagePlayback&images;NativeNotesVideoPlayback*videos{};NotesImageRoute route;DWORD thread{GetCurrentThreadId()};
 core::MediaRequestCatalog catalog;std::map<Client,Owner>owners;std::vector<Retired>retired;std::map<std::string,Inspection,std::less<>>inspections;
 std::set<std::string,std::less<>>fulfilled;std::vector<Ready>ready;std::size_t readyBytes{};double time{};unsigned eventDepth{};
 Impl(NativeNotesImageDecoder&d,NativeNotesImagePlayback&i,NotesImageRoute r,NativeNotesVideoPlayback*v):decoder(d),images(i),videos(v),route(r){
  need(d.stats().requests==0&&!d.stats().stopped,"Shared broker must take provider ownership before module requests");need(!v||(!v->stats().visible&&!v->stats().engines),"Shared broker requires a fresh video owner");retired.reserve(32);ready.reserve(16);
  auto&registry=providerOwners();std::lock_guard lock(registry.mutex);need(registry.decoders.insert(&decoder).second,"Existing decoder already has its one shared broker");try{decoder.setRoute(route);if(videos)videos->setRoute({route.owner,route.message,route.generation});}catch(...){registry.decoders.erase(&decoder);throw;}
 }
 ~Impl(){auto&registry=providerOwners();std::lock_guard lock(registry.mutex);registry.decoders.erase(&decoder);}
 void check()const{need(thread==GetCurrentThreadId(),"Shared media broker belongs to its owner thread");}
 struct Event {Impl&i;Event(Impl&v,double t):i(v){i.check();need(std::isfinite(t),"Shared media needs finite owner time");if(!i.eventDepth)i.time=std::max(i.time,t);++i.eventDepth;}~Event(){--i.eventDepth;}};
 Owner&owner(Client c){const auto it=owners.find(c);need(it!=owners.end(),"Unknown shared media client");return it->second;}
 const Known*known(Client c,Channel channel,std::string_view key)const noexcept{const auto it=owners.find(c);if(it==owners.end())return nullptr;const auto&map=it->second.known[index(channel)];const auto found=map.find(key);return found==map.end()?nullptr:&found->second;}
 struct KnownStage {KnownMap next;std::vector<Retired>retired;};
 KnownStage stage(Client c,Channel channel,std::span<const Binding>aggregate){KnownStage out{owner(c).known[index(channel)],retired};for(const auto&binding:aggregate)if(binding.client==c){const auto old=out.next.find(binding.request.key);if(old!=out.next.end()&&old->second.global!=binding.globalKey)out.retired.push_back({c,channel,old->second.global});out.next.insert_or_assign(binding.request.key,Known{binding.globalKey,binding.request.revision});}
  std::size_t count=out.next.size();for(const auto&[id,client]:owners)if(id!=c)count+=client.known[index(channel)].size();for(const auto&r:out.retired)count+=r.channel==channel;need(count<=maximumRetainedIdentities,"Publish and retire old module media before replacing more identities");return out;
 }
 void publishKnown(Client c,Channel channel,KnownStage&&stage){owner(c).known[index(channel)].swap(stage.next);retired.swap(stage.retired);}
 bool drainInfo(){bool changed{};for(auto&completion:decoder.drainInspections(route.generation)){completion.frame.reset();const auto found=inspections.find(completion.key);if(found==inspections.end()||found->second.revision!=completion.revision||fulfilled.contains(completion.key))continue;const auto&t=found->second;std::size_t bytes=sizeof(Ready)+t.local.size();if(completion.info)bytes+=sizeof(NotesImageInfo)+completion.info->frameDelays.size()*sizeof(double);
   if(bytes>maximumInspectionMetadataBytes-readyBytes){completion.info.reset();completion.result=E_OUTOFMEMORY;bytes=sizeof(Ready)+t.local.size();}
   need(bytes<=maximumInspectionMetadataBytes-readyBytes&&ready.size()<core::MediaRequestCatalog::maximumRequests,"Drain shared inspection metadata before accepting more results");
   Ready result{t.client,t.generation,{},bytes};result.value=std::move(completion);result.value.key=t.local;ready.push_back(std::move(result));fulfilled.insert(found->first);readyBytes+=bytes;changed=true;
  }return changed;
 }
 void discardReady(Client c,std::uint64_t generation,const std::map<std::string,Inspection,std::less<>>&next){for(auto it=ready.begin();it!=ready.end();){bool keep=it->client!=c;if(!keep&&it->generation==generation)for(const auto&[key,b]:next){(void)key;if(b.client==c&&b.generation==generation&&b.local==it->value.key&&b.revision==it->value.revision){keep=true;break;}}if(keep)++it;else{readyBytes-=it->bytes;it=ready.erase(it);}}}
 bool retire(Channel channel,std::string_view global){if(channel==Channel::image){const auto*r=images.find(global);if(!r)return true;if(r->visible)return false;return images.retire(global);}if(!videos)return true;const auto*r=videos->find(global);if(!r)return true;if(r->visible)return false;return videos->retire(global);}
};
NativeMediaRequestBroker::NativeMediaRequestBroker(NativeNotesImageDecoder&d,NativeNotesImagePlayback&i,NotesImageRoute r,NativeNotesVideoPlayback*v):impl_(std::make_unique<Impl>(d,i,r,v)){}
NativeMediaRequestBroker::~NativeMediaRequestBroker(){if(!impl_)return;auto&i=*impl_;try{i.check();i.images.hide(i.time);i.decoder.setInspections({});if(i.videos)i.videos->hide(i.time);}catch(...){} }
NativeMediaRequestBroker::Client NativeMediaRequestBroker::attachClient(){auto&i=*impl_;i.check();const auto client=i.catalog.attach();try{i.owners.emplace(client,Impl::Owner{});}catch(...){i.catalog.detach(client);throw;}return client;}
void NativeMediaRequestBroker::connectVideoPlayback(NativeNotesVideoPlayback&v){auto&i=*impl_;i.check();need(!i.videos||i.videos==&v,"Shared broker already has its one video owner");need(!v.stats().visible&&!v.stats().engines,"Connect video owner before visible requests");v.setRoute({i.route.owner,i.route.message,i.route.generation});i.videos=&v;}
bool NativeMediaRequestBroker::setImages(Client client,std::uint64_t generation,std::span<const NotesImagePlaybackRequest>incoming,double now,bool preserve){auto&i=*impl_;const Impl::Event event(i,now);i.owner(client);std::vector<Request>requests;requests.reserve(incoming.size());for(const auto&r:incoming)requests.push_back(imageRequest(r));auto transaction=i.catalog.prepare(client,Channel::image,generation,requests);if(transaction.stale()||!transaction.changed())return false;auto known=i.stage(client,Channel::image,transaction.aggregate());std::vector<NotesImagePlaybackRequest>unionRequests;unionRequests.reserve(transaction.aggregate().size());for(const auto&r:transaction.aggregate())unionRequests.push_back({decodeRequest(r),r.request.kind});i.images.setVisible(unionRequests,i.time,preserve);i.catalog.commit(std::move(transaction));i.publishKnown(client,Channel::image,std::move(known));return true;}
bool NativeMediaRequestBroker::setVideos(Client client,std::uint64_t generation,std::span<const NotesVideoRequest>incoming,double now,bool preserve){auto&i=*impl_;const Impl::Event event(i,now);i.owner(client);need(i.videos||incoming.empty(),"Connect existing video playback before movie requests");std::vector<Request>requests;requests.reserve(incoming.size());for(const auto&r:incoming)requests.push_back(videoRequest(r));auto transaction=i.catalog.prepare(client,Channel::video,generation,requests);if(transaction.stale()||!transaction.changed())return false;auto known=i.stage(client,Channel::video,transaction.aggregate());std::vector<NotesVideoRequest>unionRequests;unionRequests.reserve(transaction.aggregate().size());for(const auto&r:transaction.aggregate())unionRequests.push_back(movieRequest(r));if(i.videos)i.videos->setVisible(unionRequests,i.time,preserve);i.catalog.commit(std::move(transaction));i.publishKnown(client,Channel::video,std::move(known));return true;}
bool NativeMediaRequestBroker::setInspections(Client client,std::uint64_t generation,std::span<const NotesImageRequest>incoming){auto&i=*impl_;i.check();i.owner(client);i.drainInfo();std::vector<Request>requests;requests.reserve(incoming.size());for(const auto&r:incoming)requests.push_back(inspectionRequest(r));auto transaction=i.catalog.prepare(client,Channel::inspection,generation,requests);if(transaction.stale()||!transaction.changed())return false;std::map<std::string,Impl::Inspection,std::less<>>next;std::set<std::string,std::less<>>complete;std::vector<NotesImageRequest>unionRequests;unionRequests.reserve(transaction.aggregate().size());for(const auto&r:transaction.aggregate()){next.emplace(r.globalKey,Impl::Inspection{r.client,r.generation,r.request.revision,r.request.key});if(i.fulfilled.contains(r.globalKey))complete.insert(r.globalKey);else unionRequests.push_back(decodeRequest(r));}i.decoder.setInspections(unionRequests);i.catalog.commit(std::move(transaction));i.discardReady(client,generation,next);i.inspections.swap(next);i.fulfilled.swap(complete);return true;}
bool NativeMediaRequestBroker::hideImages(Client client,double now,bool preserve){auto&i=*impl_;const auto generation=std::max<std::uint64_t>(1,i.catalog.generation(client,Channel::image));return setImages(client,generation,{},now,preserve);}
bool NativeMediaRequestBroker::hideVideos(Client client,double now,bool preserve){auto&i=*impl_;const auto generation=std::max<std::uint64_t>(1,i.catalog.generation(client,Channel::video));return setVideos(client,generation,{},now,preserve);}
bool NativeMediaRequestBroker::cancelInspections(Client client,std::uint64_t generation){return setInspections(client,generation,{});}
bool NativeMediaRequestBroker::accept(UINT_PTR generation,double now){auto&i=*impl_;const Impl::Event event(i,now);if(generation!=i.route.generation)return false;bool changed=i.images.accept(generation,i.time);if(i.videos){changed=i.videos->accept(generation,i.time)||changed;changed=i.videos->sample(i.time)||changed;}changed=i.drainInfo()||changed;return changed||!i.ready.empty();}
bool NativeMediaRequestBroker::sample(double now){auto&i=*impl_;const Impl::Event event(i,now);bool changed=i.images.sample(i.time);if(i.videos)changed=i.videos->sample(i.time)||changed;return changed;}
bool NativeMediaRequestBroker::requiresFrames()const{auto&i=*impl_;i.check();return i.videos&&i.videos->requiresFrames();}
std::optional<double>NativeMediaRequestBroker::nextWakeTime()const{auto&i=*impl_;i.check();auto result=i.images.nextWakeTime();if(i.videos){const auto t=i.videos->nextWakeTime();if(t&&(!result||*t<*result))result=t;}return result;}
const NotesImagePlaybackRecord*NativeMediaRequestBroker::findImage(Client c,std::string_view key)const noexcept{const auto*r=impl_->known(c,Channel::image,key);return r?impl_->images.find(r->global):nullptr;}
const NotesVideoRecord*NativeMediaRequestBroker::findVideo(Client c,std::string_view key)const noexcept{const auto&r=*impl_;const auto*id=r.known(c,Channel::video,key);return id&&r.videos?r.videos->find(id->global):nullptr;}
bool NativeMediaRequestBroker::toggleImage(Client c,std::string_view key,double now){auto&i=*impl_;const Impl::Event event(i,now);const auto*r=i.known(c,Channel::image,key);return r&&i.images.toggle(r->global,i.time);}
bool NativeMediaRequestBroker::playImage(Client c,std::string_view key,double now){auto&i=*impl_;const Impl::Event event(i,now);const auto*r=i.known(c,Channel::image,key);return r&&i.images.play(r->global,i.time);}
bool NativeMediaRequestBroker::pauseImage(Client c,std::string_view key){auto&i=*impl_;i.check();const auto*r=i.known(c,Channel::image,key);return r&&i.images.pause(r->global);}
bool NativeMediaRequestBroker::toggleVideo(Client c,std::string_view key,double now){auto&i=*impl_;const Impl::Event event(i,now);const auto*r=i.known(c,Channel::video,key);return r&&i.videos&&i.videos->toggle(r->global,i.time);}
bool NativeMediaRequestBroker::seekVideo(Client c,std::string_view key,double seconds){auto&i=*impl_;i.check();const auto*r=i.known(c,Channel::video,key);return r&&i.videos&&i.videos->seek(r->global,seconds);}
bool NativeMediaRequestBroker::setVideoPoster(Client c,std::string_view key,std::uint64_t requestRevision,std::uint64_t posterRevision,const NotesImageFrame*frame,HRESULT result){auto&i=*impl_;i.check();const auto*r=i.known(c,Channel::video,key);return r&&r->revision==requestRevision&&i.videos&&i.videos->setPoster(r->global,requestRevision,posterRevision,frame,result);}
std::vector<NotesImageCompletion>NativeMediaRequestBroker::drainInspections(Client c,std::uint64_t generation){auto&i=*impl_;i.check();i.owner(c);if(generation!=i.catalog.generation(c,Channel::inspection))return {};std::vector<NotesImageCompletion>result;result.reserve(std::count_if(i.ready.begin(),i.ready.end(),[&](const auto&r){return r.client==c&&r.generation==generation;}));for(auto it=i.ready.begin();it!=i.ready.end();)if(it->client==c&&it->generation==generation){i.readyBytes-=it->bytes;result.push_back(std::move(it->value));it=i.ready.erase(it);}else++it;return result;}
bool NativeMediaRequestBroker::retireImage(Client c,std::string_view key){auto&i=*impl_;i.check();auto&known=i.owner(c).known[0];const auto r=known.find(key);if(r==known.end())return false;if(!i.retire(Channel::image,r->second.global))return false;known.erase(r);return true;}
bool NativeMediaRequestBroker::retireVideo(Client c,std::string_view key){auto&i=*impl_;i.check();auto&known=i.owner(c).known[1];const auto r=known.find(key);if(r==known.end())return false;if(!i.retire(Channel::video,r->second.global))return false;known.erase(r);return true;}
bool NativeMediaRequestBroker::collectRetired(){auto&i=*impl_;i.check();bool complete=true;for(auto it=i.retired.begin();it!=i.retired.end();)if(i.retire(it->channel,it->global))it=i.retired.erase(it);else{complete=false;++it;}if(i.videos)complete=i.videos->collectRetired()&&complete;return complete;}
bool NativeMediaRequestBroker::detachClient(Client c){auto&i=*impl_;i.check();auto&owner=i.owner(c);for(auto channel:{Channel::image,Channel::video,Channel::inspection})if(!i.catalog.bindings(c,channel).empty())return false;for(auto channel:{Channel::image,Channel::video})for(const auto&[key,r]:owner.known[index(channel)]){(void)key;if(!i.retire(channel,r.global))return false;}for(auto it=i.retired.begin();it!=i.retired.end();)if(it->client==c){if(!i.retire(it->channel,it->global))return false;it=i.retired.erase(it);}else++it;i.discardReady(c,0,{});need(i.catalog.detach(c),"Cleared shared media client remained active");i.owners.erase(c);return true;}
NotesImageDecoderStats NativeMediaRequestBroker::decoderStats()const{impl_->check();return impl_->decoder.stats();}
NotesVideoStats NativeMediaRequestBroker::videoStats()const{impl_->check();return impl_->videos?impl_->videos->stats():NotesVideoStats{};}
bool NativeMediaRequestBroker::hasVideoPlayback()const{impl_->check();return impl_->videos!=nullptr;}
} // namespace endfield::native
#endif
