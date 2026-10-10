#include "core/data/map_store.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace ehud::data {
namespace {
namespace m=endfield::modules;
constexpr const char*invalid="The saved map could not be read. The original data has been preserved.";
void need(bool condition){if(!condition)throw StoreError(StoreErrorCode::invalid,invalid);}
std::string canonical(std::string value){for(auto&c:value)if(c>='a'&&c<='f')c=char(c-'a'+'A');return value;}
double number(const Json&j){need(j.isNumber());const auto value=j.number();need(std::isfinite(value));return value;}
m::MapPinStyle style(const Json&j){if(j.isNull())return m::MapPinStyle::yellow;need(j.isString());const auto key=j.string();if(key=="yellow")return m::MapPinStyle::yellow;if(key=="green")return m::MapPinStyle::green;if(key=="player")return m::MapPinStyle::player;throw StoreError(StoreErrorCode::invalid,invalid);}
const char*styleName(m::MapPinStyle value){switch(value){case m::MapPinStyle::yellow:return "yellow";case m::MapPinStyle::green:return "green";case m::MapPinStyle::player:return "player";}throw StoreError(StoreErrorCode::invalid,invalid);}
m::MapSnapshot canonicalSnapshot(m::MapSnapshot value){try{return m::migrateMapSnapshot(std::move(value),4);}catch(const StoreError&){throw;}catch(...){throw StoreError(StoreErrorCode::invalid,invalid);}}
void setNumber(Json&j,const char*key,double value){if(!j[key].isNumber()||j[key].number()!=value)j[key]=value;}
Json document(const m::MapSnapshot&value,const Json&preserved){
    need(preserved.isObject());if(preserved.contains("version")){need(preserved["version"].isNumber());const auto v=preserved["version"].integer();if(v>4)throw StoreError(StoreErrorCode::newerVersion,"This map was saved by a newer version of EndfieldHUD.");need(v>=1);}
    Json root=preserved;root["version"]=4;
    Json viewport=root["viewport"].isObject()?root["viewport"]:Json::Object{};setNumber(viewport,"centerX",value.viewport.centerX);setNumber(viewport,"centerY",value.viewport.centerY);setNumber(viewport,"zoom",value.viewport.zoom);root["viewport"]=std::move(viewport);
    std::map<std::string,const Json*,std::less<>>old;
    if(preserved["pins"].isArray())for(const auto&row:preserved["pins"].array())if(row.isObject()&&row["id"].isString())old.emplace(canonical(row["id"].string()),&row);
    Json::Array pins;pins.reserve(value.pins.size());for(const auto&p:value.pins){const auto found=old.find(p.id);Json row=found==old.end()?Json::Object{}:*found->second;row["id"]=p.id;setNumber(row,"x",p.x);setNumber(row,"y",p.y);setNumber(row,"createdAt",p.createdAt);row["style"]=styleName(p.style);pins.push_back(std::move(row));}root["pins"]=std::move(pins);return root;
}
}
MapArchive decodeMapArchive(std::string_view bytes){try{
    auto raw=Json::parse(bytes,MapStore::maximumArchiveBytes);need(raw.isObject()&&raw["version"].isNumber());const auto version=raw["version"].integer();if(version>4)throw StoreError(StoreErrorCode::newerVersion,"This map was saved by a newer version of EndfieldHUD.");need(version>=1&&raw["pins"].isArray()&&raw["pins"].array().size()<=m::MapState::maximumPins&&raw["viewport"].isObject());
    m::MapSnapshot value;const auto&view=raw["viewport"];value.viewport={number(view["centerX"]),number(view["centerY"]),number(view["zoom"])};value.pins.reserve(raw["pins"].array().size());
    for(const auto&row:raw["pins"].array()){need(row.isObject()&&row["id"].isString());value.pins.push_back({row["id"].string(),number(row["x"]),number(row["y"]),number(row["createdAt"]),style(row["style"])});}
    value=m::migrateMapSnapshot(std::move(value),static_cast<unsigned>(version));return {std::move(value),std::move(raw),static_cast<unsigned>(version)};
}catch(const StoreError&){throw;}catch(...){throw StoreError(StoreErrorCode::invalid,invalid);}}
std::string encodeMapArchive(const m::MapSnapshot&value,const Json&preserved){return document(canonicalSnapshot(value),preserved).encode(MapStore::maximumArchiveBytes);}
MapStore::MapStore(std::filesystem::path root){detail::validateRoot(root);path_=std::move(root)/"WorldMap"/"map.json";persisted_=detail::readFile(path_,maximumArchiveBytes);if(!persisted_)return;auto archive=decodeMapArchive(*persisted_);document_=std::move(archive.preserved);value_=std::move(archive.snapshot);if(archive.sourceVersion<4)commit(value_);}
void MapStore::commit(m::MapSnapshot next){auto nextDocument=document(next,document_);auto bytes=nextDocument.encode(maximumArchiveBytes);std::optional<std::string>expected(bytes);detail::replaceFile(path_,persisted_,bytes,maximumArchiveBytes);value_=std::move(next);document_=std::move(nextDocument);persisted_.swap(expected);}
bool MapStore::replace(const m::MapSnapshot&value){auto next=canonicalSnapshot(value);if(next==value_)return false;commit(std::move(next));return true;}
}
