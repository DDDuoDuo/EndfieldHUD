#include "core/media_request_catalog.hpp"
#include "core/data/data_store.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
namespace endfield::core {namespace {
void need(bool ok,const char*why){if(!ok)throw std::invalid_argument(why);}
void validate(const ScopedMediaRequest&r,MediaRequestChannel channel){
 need(!r.key.empty()&&r.key.size()<=MediaRequestCatalog::maximumLocalKeyBytes&&r.key.find('\0')==std::string::npos&&ehud::data::Json::validUtf8(r.key)&&r.revision,"Invalid scoped media identity");
 need(ehud::data::validWindowsFilePath(r.path)&&r.maximumDimension>0&&r.maximumDimension<=65536,"Invalid explicit scoped media reference");
 using K=modules::NotesMediaKind;need(r.kind==K::image||r.kind==K::gif||r.kind==K::video,"Invalid scoped media kind");
 if(r.duration)need(std::isfinite(*r.duration)&&*r.duration>0&&*r.duration<=31536000,"Invalid scoped movie duration");
 if(channel==MediaRequestChannel::image)need(r.kind!=K::video&&!r.allowVideoInspection&&!r.workerPoster&&!r.duration&&(!r.videoPoster||r.kind==K::image),"Invalid image channel flags");
 else if(channel==MediaRequestChannel::video)need(r.kind==K::video&&!r.firstFrameOnly&&!r.videoPoster&&!r.allowVideoInspection,"Invalid video channel flags");
 else need(!r.videoPoster&&!r.workerPoster&&!r.duration,"Invalid inspection channel flags");
}
char channelCode(MediaRequestChannel c){switch(c){case MediaRequestChannel::image:return 'i';case MediaRequestChannel::video:return 'v';case MediaRequestChannel::inspection:return 'p';}throw std::invalid_argument("Invalid scoped media channel");}
std::size_t metadataBytes(const ScopedMediaBinding&r){return sizeof(ScopedMediaBinding)+64+r.request.key.size()*2+r.request.path.size()+r.globalKey.size();}
}
MediaRequestCatalog::Transaction::Transaction(Transaction&&other)noexcept:next_(std::move(other.next_)),aggregate_(std::move(other.aggregate_)),owner_(std::exchange(other.owner_,nullptr)),revision_(other.revision_),changed_(other.changed_),stale_(other.stale_){}
MediaRequestCatalog::Transaction&MediaRequestCatalog::Transaction::operator=(Transaction&&other)noexcept{if(this!=&other){next_=std::move(other.next_);aggregate_=std::move(other.aggregate_);owner_=std::exchange(other.owner_,nullptr);revision_=other.revision_;changed_=other.changed_;stale_=other.stale_;}return *this;}
MediaClient MediaRequestCatalog::attach(){need(clients_.size()<maximumClients&&next_<std::numeric_limits<MediaClient>::max()&&revision_<std::numeric_limits<std::uint64_t>::max(),"Shared media client capacity exhausted");const auto id=next_+1;clients_.emplace(id,true);next_=id;++revision_;return id;}
MediaRequestCatalog::Transaction MediaRequestCatalog::prepare(MediaClient client,MediaRequestChannel channel,std::uint64_t generation,std::span<const ScopedMediaRequest>requests)const{
 need(clients_.contains(client)&&generation,"Unknown shared media client or zero generation");const auto code=channelCode(channel);need(requests.size()<=maximumRequests,"Scoped media metadata count exceeds bound");Transaction result;result.owner_=this;result.revision_=revision_;const Key key{client,channel};const auto old=batches_.find(key);
 if(old!=batches_.end()&&generation<old->second->generation){result.stale_=true;return result;}
 auto batch=std::make_shared<Batch>();batch->generation=generation;batch->bindings.reserve(requests.size());std::size_t ownBytes{};
 for(const auto&r:requests){validate(r,channel);need(batch->index.emplace(r.key,batch->bindings.size()).second,"Repeated scoped media identity");
  if(old!=batches_.end()&&generation==old->second->generation){const auto prior=old->second->index.find(r.key);if(prior!=old->second->index.end()){const auto&before=old->second->bindings[prior->second].request;need(before.revision!=r.revision||before==r,"A changed reference requires a new explicit revision or owner generation");}}
  auto global="media."+std::to_string(client)+"."+code+"."+std::to_string(generation)+"."+std::to_string(r.revision)+"."+r.key;need(global.size()<=400,"Scoped key exceeds native video identity bound");
  ScopedMediaBinding binding{client,generation,channel,r,std::move(global)};const auto bytes=metadataBytes(binding);need(bytes<=maximumMetadataBytes-ownBytes,"Scoped media metadata bytes exceed bound");ownBytes+=bytes;batch->bindings.push_back(std::move(binding));
 }
 bool same=old!=batches_.end()&&generation==old->second->generation&&old->second->bindings.size()==batch->bindings.size();if(same)for(std::size_t n=0;n<batch->bindings.size();++n)if(old->second->bindings[n].request!=batch->bindings[n].request){same=false;break;}
 result.changed_=!same;result.next_=batches_;if(result.changed_)result.next_[key]=std::move(batch);
 std::size_t count{},bytes{};for(const auto&[k,b]:result.next_){if(k.second==channel){need(b->bindings.size()<=maximumRequests-count,"Aggregate active media request count exceeds bound");count+=b->bindings.size();}for(const auto&r:b->bindings){const auto amount=metadataBytes(r);need(amount<=maximumMetadataBytes-bytes,"Aggregate active media metadata bytes exceed bound");bytes+=amount;}}
 result.aggregate_.reserve(count);for(const auto&[k,b]:result.next_)if(k.second==channel)result.aggregate_.insert(result.aggregate_.end(),b->bindings.begin(),b->bindings.end());return result;
}
void MediaRequestCatalog::commit(Transaction&&t){need(t.owner_==this&&!t.stale_&&t.revision_==revision_,"Stale, foreign or consumed shared media transaction");if(t.changed_){need(revision_<std::numeric_limits<std::uint64_t>::max(),"Shared media catalog revision exhausted");batches_.swap(t.next_);++revision_;}t.owner_=nullptr;t.next_.clear();t.aggregate_.clear();}
std::span<const ScopedMediaBinding>MediaRequestCatalog::bindings(MediaClient client,MediaRequestChannel channel)const{need(clients_.contains(client),"Unknown shared media client");const auto found=batches_.find({client,channel});return found==batches_.end()?std::span<const ScopedMediaBinding>{}:std::span(found->second->bindings);}
const ScopedMediaBinding*MediaRequestCatalog::find(MediaClient c,MediaRequestChannel channel,std::string_view key)const noexcept{const auto b=batches_.find({c,channel});if(b==batches_.end())return nullptr;const auto found=b->second->index.find(key);return found==b->second->index.end()?nullptr:&b->second->bindings[found->second];}
std::uint64_t MediaRequestCatalog::generation(MediaClient c,MediaRequestChannel channel)const noexcept{const auto b=batches_.find({c,channel});return b==batches_.end()?0:b->second->generation;}
bool MediaRequestCatalog::detach(MediaClient client){need(clients_.contains(client),"Unknown shared media client");for(const auto&[key,b]:batches_)if(key.first==client&&!b->bindings.empty())return false;need(revision_<std::numeric_limits<std::uint64_t>::max(),"Shared media catalog revision exhausted");for(auto it=batches_.begin();it!=batches_.end();)if(it->first.first==client)it=batches_.erase(it);else ++it;clients_.erase(client);++revision_;return true;}
}
