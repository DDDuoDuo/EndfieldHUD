#include "core/data/event_log_store.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>

namespace ehud::data {
namespace {
namespace model=endfield::modules;
void need(bool value,const char*why){if(!value)throw StoreError(StoreErrorCode::invalid,why);}
std::string canonicalID(std::string value){need(validUUID(value),"Invalid saved event UUID");for(auto&c:value)if(c>='a'&&c<='f')c=char(c-'a'+'A');return value;}
model::EventKind kind(std::string_view key){
    for(unsigned n=0;n<=static_cast<unsigned>(model::EventKind::accountAction);++n){const auto value=static_cast<model::EventKind>(n);if(model::eventKindKey(value)==key)return value;}
    throw StoreError(StoreErrorCode::invalid,"Unknown saved event kind");
}
std::vector<model::SystemEvent>decode(const Json&document,std::size_t capacity){
    need(document["events"].isArray(),"Saved event list is missing");
    std::vector<model::SystemEvent>events;std::set<std::string,std::less<>>seen;
    for(const auto&row:document["events"].array()){
        need(row.isObject()&&row["id"].isString()&&row["kind"].isString()&&row["createdAt"].isNumber()&&row["metadata"].isObject(),"Malformed saved event");
        auto id=canonicalID(row["id"].string());const auto type=kind(row["kind"].string());model::EventMetadata metadata;
        for(const auto&[key,value]:row["metadata"].object()){need(value.isString(),"Malformed saved event metadata");metadata.emplace(key,value.string());}
        const auto date=row["createdAt"].number();if(!std::isfinite(date)||!seen.insert(id).second)continue;
        events.push_back({std::move(id),type,date,std::move(metadata)});
    }
    std::stable_sort(events.begin(),events.end(),[](const auto&a,const auto&b){return a.createdAt>b.createdAt;});
    if(events.size()>capacity)events.resize(capacity);return events;
}
std::string encode(std::span<const model::SystemEvent>events){
    Json::Array rows;rows.reserve(events.size());
    for(const auto&event:events){Json::Object metadata;for(const auto&[key,value]:event.metadata)metadata.emplace(key,value);
        Json::Object row;row.emplace("id",event.id);row.emplace("kind",std::string(model::eventKindKey(event.kind)));row.emplace("createdAt",event.createdAt);row.emplace("metadata",std::move(metadata));rows.emplace_back(std::move(row));}
    return Json(Json::Object{{"version",1},{"events",std::move(rows)}}).encode(EventLogStore::maximumArchiveBytes);
}
}
struct EventLogWrite::Writer {
    std::filesystem::path path;
    std::optional<std::string>persisted;
    std::optional<std::uint64_t>savedRevision;
    bool blocked{};std::mutex mutex;
    Writer(std::filesystem::path p,std::optional<std::string>bytes,bool b):path(std::move(p)),persisted(std::move(bytes)),blocked(b){}
    bool write(std::uint64_t revision,const std::string&bytes){
        std::lock_guard lock(mutex);if(blocked)return false;if(savedRevision&&revision<=*savedRevision)return true;
        // Allocate the replacement expectation before publishing disk bytes.
        // A later allocation failure must not make our own successful write
        // look like a change by another process on the next attempt.
        std::optional<std::string>next(bytes);
        detail::replaceFile(path,persisted,bytes,EventLogStore::maximumArchiveBytes);persisted.swap(next);savedRevision=revision;return true;
    }
};
EventLogWrite::EventLogWrite(std::shared_ptr<Writer>writer,std::uint64_t revision,std::string bytes):writer_(std::move(writer)),revision_(revision),bytes_(std::move(bytes)){}
EventLogSaveResult EventLogWrite::execute()const noexcept{try{return {revision_,writer_&&writer_->write(revision_,bytes_)};}catch(...){return {revision_,false};}}
EventLogStore::EventLogStore(std::optional<std::filesystem::path>root,std::size_t capacity,model::EventNameCompactor compactor):model_(capacity,std::move(compactor)){
    if(!root)return;detail::validateRoot(*root);path_=*root/"EventLog"/"events.json";std::optional<std::string>persisted;
    try{
        persisted=detail::readFile(*path_,maximumArchiveBytes);
        if(persisted){const auto document=Json::parse(*persisted,maximumArchiveBytes);need(document.isObject()&&document["version"].isNumber(),"Saved event version is missing");
            const auto version=document["version"].integer();if(version>1)failure_=Failure::version;
            else{need(version==1,"Invalid saved event version");const auto events=decode(document,model_.capacity());model_.replace(events);}}
    }catch(...){failure_=Failure::load;}
    writer_=std::make_shared<EventLogWrite::Writer>(*path_,std::move(persisted),failure_.has_value());
}
std::optional<std::string_view>EventLogStore::statusMessage()const noexcept{
    if(!failure_)return {};switch(*failure_){
    case Failure::load:return "Saved log could not be read; the original file is preserved.";
    case Failure::version:return "This log uses a newer format; the original file is preserved.";
    case Failure::save:return "The log could not be saved. New events are kept in this session.";
    }return {};
}
void EventLogStore::validateTime(double value)const{need(std::isfinite(value)&&value<=std::numeric_limits<double>::max()-saveDelay&&(!lastTime_||value>=*lastTime_),"Event persistence requires finite monotonic owner time");}
void EventLogStore::changed(double time){++revision_;lastTime_=time;if(writer_)deadline_=time+saveDelay;}
void EventLogStore::record(model::SystemEvent event,double time){validateTime(time);model_.record(std::move(event.id),event.kind,event.createdAt,event.metadata);changed(time);}
void EventLogStore::clear(double time){validateTime(time);model_.clear();changed(time);}
std::optional<EventLogWrite>EventLogStore::takeSave(double time,bool force){
    validateTime(time);if(!writer_){lastTime_=time;return {};}
    if(!force&&(!deadline_||time<*deadline_)){lastTime_=time;return {};}
    auto bytes=encode(model_.events());EventLogWrite request(writer_,revision_,std::move(bytes));deadline_.reset();lastTime_=time;return request;
}
bool EventLogStore::complete(EventLogSaveResult result){
    if(result.revision!=revision_||failure_==Failure::load||failure_==Failure::version)return false;
    const auto before=failure_;failure_=result.success?std::nullopt:std::optional(Failure::save);return before!=failure_;
}
}
