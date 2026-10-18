#include "native/audio_route_journal.hpp"
#include "native/audio_endpoint_model.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;
constexpr std::int32_t corrupt=static_cast<std::int32_t>(0x80070570u);   // ERROR_FILE_CORRUPT
constexpr std::int32_t newer=static_cast<std::int32_t>(0x8007051Au);     // ERROR_REVISION_MISMATCH
constexpr std::int32_t full=static_cast<std::int32_t>(0x8007006Fu);      // ERROR_BUFFER_OVERFLOW
constexpr std::int32_t changed=static_cast<std::int32_t>(0x80070021u);   // ERROR_LOCK_VIOLATION
constexpr std::int32_t failure=static_cast<std::int32_t>(0x80004005u);   // E_FAIL
constexpr std::int32_t invalid=static_cast<std::int32_t>(0x80070057u);   // E_INVALIDARG
bool unit(float v)noexcept{return std::isfinite(v)&&v>=0&&v<=1;}
bool token(std::string_view v,bool allowEmpty)noexcept{return (allowEmpty||!v.empty())&&v.size()<=4096&&v.find('\0')==v.npos&&Json::validUtf8(v);}
bool valid(const AudioRouteJournalEntry&e)noexcept{
    return !e.endpoint.empty()&&e.endpoint.size()<=32767&&audioUtf8(e.endpoint).has_value()&&token(e.session,false)&&token(e.persistent,true)&&token(e.processKey,false)&&unit(e.original)&&unit(e.written);
}
std::int32_t storeStatus(const ehud::data::StoreError&e)noexcept{
    switch(e.code()){
    case ehud::data::StoreErrorCode::newerVersion:return newer;
    case ehud::data::StoreErrorCode::changedOnDisk:return changed;
    case ehud::data::StoreErrorCode::invalid:case ehud::data::StoreErrorCode::tooLarge:return corrupt;
    case ehud::data::StoreErrorCode::unavailable:return failure;
    }
    return failure;
}
}

std::optional<std::vector<AudioRouteJournalEntry>>AudioRouteJournal::decode(std::string_view text){
    try{
        const auto root=Json::parse(text,maximumBytes);
        if(!root.isObject()||root["format"].string()!=format||!root["schema"].isNumber()||root["schema"].integer()!=schema||!root["entries"].isArray())return std::nullopt;
        for(const auto&[key,_]:root.object())if(key!="format"&&key!="schema"&&key!="entries")return std::nullopt;
        const auto&list=root["entries"].array();if(list.size()>maximumEntries)return std::nullopt;
        std::vector<AudioRouteJournalEntry>out;out.reserve(list.size());std::set<std::string>sessions;
        for(const auto&item:list){
            if(!item.isObject()||item.object().size()!=6)return std::nullopt;
            const auto endpoint=audioWide(item["endpoint"].string());if(!endpoint)return std::nullopt;
            const auto original=item["original"].number(),written=item["written"].number();
            AudioRouteJournalEntry e{*endpoint,item["session"].string(),item["persistent"].string(),item["process"].string(),static_cast<float>(original),static_cast<float>(written)};
            if(static_cast<double>(e.original)!=original||static_cast<double>(e.written)!=written||!valid(e)||!sessions.insert(e.session).second)return std::nullopt;
            out.push_back(std::move(e));
        }
        return out;
    }catch(...){return std::nullopt;}
}
std::string AudioRouteJournal::encode(std::span<const AudioRouteJournalEntry>entries){
    Json::Array list;list.reserve(entries.size());
    for(const auto&e:entries){
        if(!valid(e))throw std::invalid_argument("Invalid audio route journal entry");
        list.emplace_back(Json::Object{{"endpoint",*audioUtf8(e.endpoint)},{"session",e.session},{"persistent",e.persistent},{"process",e.processKey},
            {"original",static_cast<double>(e.original)},{"written",static_cast<double>(e.written)}});
    }
    return Json(Json::Object{{"format",std::string(format)},{"schema",schema},{"entries",std::move(list)}}).encode(maximumBytes)+"\n";
}

AudioRouteJournal::AudioRouteJournal(std::filesystem::path path):path_(std::move(path)){
    try{
        if(path_.empty()||!path_.is_absolute()){status_=invalid;return;}
        disk_=ehud::data::detail::readFile(path_,maximumBytes);
        if(!disk_)return;
        auto entries=decode(*disk_);
        if(!entries){
            // Distinguish a newer writer from damage; neither is overwritten.
            try{const auto root=Json::parse(*disk_,maximumBytes);status_=root.isObject()&&root["format"].isString()&&root["format"].string()==format&&root["schema"].isNumber()&&root["schema"].number()>schema?newer:corrupt;}
            catch(...){status_=corrupt;}
            return;
        }
        recovered_=std::move(*entries);
    }catch(const ehud::data::StoreError&e){status_=storeStatus(e);}
    catch(...){status_=failure;}
}
const AudioRouteJournalEntry*AudioRouteJournal::liveEntry(std::string_view session)const noexcept{
    const auto it=std::find_if(live_.begin(),live_.end(),[&](const auto&e){return e.session==session;});return it==live_.end()?nullptr:&*it;
}
std::int32_t AudioRouteJournal::record(AudioRouteJournalEntry entry){
    if(!available())return status_;
    if(!valid(entry))return invalid;
    const auto existing=std::find_if(live_.begin(),live_.end(),[&](const auto&e){return e.session==entry.session;});
    if(existing==live_.end()&&live_.size()+recovered_.size()>=maximumEntries)return full;
    std::optional<AudioRouteJournalEntry>previous;
    if(existing!=live_.end()){previous=*existing;*existing=std::move(entry);}else live_.push_back(std::move(entry));
    const auto status=persist();
    if(status<0){ // Not durable: the caller must not write the mixer.
        if(previous)*std::find_if(live_.begin(),live_.end(),[&](const auto&e){return e.session==previous->session;})=*previous;
        else live_.pop_back();
    }
    return status;
}
bool AudioRouteJournal::written(std::string_view session,float value)noexcept{
    if(!unit(value))return false;
    for(auto&e:live_)if(e.session==session){if(e.written!=value){e.written=value;dirty_=true;}return true;}
    return false;
}
bool AudioRouteJournal::release(std::string_view session)noexcept{
    const auto it=std::find_if(live_.begin(),live_.end(),[&](const auto&e){return e.session==session;});
    if(it==live_.end())return false;live_.erase(it);dirty_=true;return true;
}
bool AudioRouteJournal::settle(std::size_t index)noexcept{
    if(index>=recovered_.size())return false;recovered_.erase(recovered_.begin()+static_cast<std::ptrdiff_t>(index));dirty_=true;return true;
}
std::size_t AudioRouteJournal::demote(std::wstring_view endpoint)noexcept{
    try{
        std::size_t moved{};
        for(auto it=live_.begin();it!=live_.end();){if(it->endpoint==endpoint){recovered_.push_back(std::move(*it));it=live_.erase(it);++moved;}else ++it;}
        if(moved)dirty_=true;return moved;
    }catch(...){return 0;}
}
bool AudioRouteJournal::demoteSession(std::string_view session)noexcept{
    try{
        const auto it=std::find_if(live_.begin(),live_.end(),[&](const auto&e){return e.session==session;});if(it==live_.end())return false;
        recovered_.push_back(std::move(*it));live_.erase(it);dirty_=true;return true;
    }catch(...){return false;}
}
std::int32_t AudioRouteJournal::flush(){if(!dirty_)return 0;if(!available())return status_;return persist();}
std::int32_t AudioRouteJournal::persist(){
    try{
        std::vector<AudioRouteJournalEntry>all;all.reserve(recovered_.size()+live_.size());
        all.insert(all.end(),recovered_.begin(),recovered_.end());all.insert(all.end(),live_.begin(),live_.end());
        auto bytes=encode(all);
        if(disk_==bytes){dirty_=false;return 0;}
        // An empty journal with no file is already the durable empty state.
        if(!disk_&&all.empty()){dirty_=false;return 0;}
        ehud::data::detail::replaceFile(path_,disk_,bytes,maximumBytes);
        disk_=std::move(bytes);dirty_=false;return 0;
    }catch(const ehud::data::StoreError&e){
        const auto status=storeStatus(e);
        // Another writer changed the file: stop using it rather than guess.
        if(status==changed||status==corrupt||status==newer)status_=status;
        return status;
    }catch(...){return failure;}
}
} // namespace endfield::native
