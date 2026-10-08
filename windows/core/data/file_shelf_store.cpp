#include "core/data/file_shelf_store.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace ehud::data {
namespace {
void require(bool value,const char* message){if(!value)throw StoreError(StoreErrorCode::invalid,message);}
struct Operation {bool& busy;explicit Operation(bool& value):busy(value){require(!value,"Reentrant shelf store operation");value=true;}~Operation(){busy=false;}};
char upper(char c)noexcept{return c>='a'&&c<='z'?char(c-'a'+'A'):c;}
bool equalUUID(std::string_view a,std::string_view b)noexcept{return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](char x,char y){return upper(x)==upper(y);});}
std::string canonicalUUID(std::string value){require(validUUID(value),"Invalid shelf UUID");for(auto& c:value)c=upper(c);return value;}
void text(std::string_view value){require(Json::validUtf8(value)&&value.find('\0')==std::string_view::npos,"Invalid shelf UTF-8 metadata");}
void identity(const ShelfFileIdentity& value){if(value.volumeUUID)require(validUUID(*value.volumeUUID),"Invalid shelf volume UUID");}
void metadata(const ShelfFileMetadata& value){
    require(validWindowsFilePath(value.windowsPath),"Invalid native shelf file path");text(value.name);text(value.typeDescription);require(!value.name.empty(),"Empty shelf file name");
    require(!value.byteCount||*value.byteCount>=0,"Invalid shelf byte count");identity(value.identity);
    require(value.kind==ShelfFileKind::regular||value.kind==ShelfFileKind::directory||value.kind==ShelfFileKind::symbolicLink,"Unsupported shelf object kind");
    require(value.kind==ShelfFileKind::symbolicLink||(value.kind==ShelfFileKind::directory)==value.isDirectory,"Inconsistent shelf directory metadata");
}
void record(const ShelfRecord& value){
    require(validUUID(value.id)&&std::isfinite(value.createdAt),"Invalid shelf record identity/time");
    metadata({value.windowsPath,value.name,value.typeDescription,value.byteCount,value.isDirectory,value.isDirectory?ShelfFileKind::directory:ShelfFileKind::regular,value.identity});
    require(value.originalFields.isObject(),"Invalid retained shelf fields");if(value.availabilityError)text(*value.availabilityError);
}
constexpr char hexDigits[]="0123456789abcdef";
std::string hex(std::span<const std::uint8_t> bytes){std::string out;out.reserve(bytes.size()*2);for(auto b:bytes){out.push_back(hexDigits[b>>4]);out.push_back(hexDigits[b&15]);}return out;}
unsigned nibble(char c){if(c>='0'&&c<='9')return unsigned(c-'0');if(c>='a'&&c<='f')return unsigned(c-'a'+10);throw StoreError(StoreErrorCode::invalid,"Noncanonical shelf identity hex");}
template<std::size_t N>std::array<std::uint8_t,N> unhex(std::string_view value){require(value.size()==N*2,"Invalid shelf identity width");std::array<std::uint8_t,N> out{};for(std::size_t i=0;i<N;++i)out[i]=std::uint8_t((nibble(value[2*i])<<4)|nibble(value[2*i+1]));return out;}
std::string serialHex(std::uint64_t serial){std::array<std::uint8_t,8> bytes{};for(std::size_t i=0;i<8;++i)bytes[7-i]=std::uint8_t(serial>>(i*8));return hex(bytes);}
std::uint64_t serialNumber(std::string_view hexValue){const auto bytes=unhex<8>(hexValue);std::uint64_t value{};for(auto b:bytes)value=(value<<8)|b;return value;}
std::string requiredText(const Json& object,const char* key){require(object[key].isString(),"Missing shelf text field");return object[key].string();}
void setOptional(Json& object,const char* key,const std::optional<std::string>& value){if(value)object[key]=*value;else object.erase(key);}
Json encoded(const ShelfRecord& value){
    record(value);auto out=value.originalFields;
    for(const char* key:{"bookmark","isSecurityScoped","lastKnownPath","identity"})require(!out.contains(key),"Mixed Mac/Windows shelf locators are unsupported");
    out["id"]=value.id;out["referencePlatform"]="windows";out["windowsPath"]=value.windowsPath;
    out["name"]=value.name;out["typeDescription"]=value.typeDescription;out["isDirectory"]=value.isDirectory;
    if(value.byteCount)out["byteCount"]=*value.byteCount;else out.erase("byteCount");
    if(!out["createdAt"].isNumber()||out["createdAt"].number()!=value.createdAt)out["createdAt"]=value.createdAt;
    auto native=out["windowsIdentity"].isObject()?out["windowsIdentity"]:Json(Json::Object{});
    native["objectID"]=hex(value.identity.objectID);native["volumeSerial"]=serialHex(value.identity.volumeSerial);
    setOptional(native,"volumeUUID",value.identity.volumeUUID);out["windowsIdentity"]=std::move(native);
    out.erase("availabilityError");return out;
}
ShelfRecord decoded(const Json& value){
    require(value.isObject(),"Invalid shelf item object");
    require(value["referencePlatform"].isString()&&value["referencePlatform"].string()=="windows","Mac/unknown shelf locators require an explicit migration adapter; original data preserved");
    for(const char* key:{"bookmark","isSecurityScoped","lastKnownPath","identity"})require(!value.contains(key),"Mixed Mac/Windows shelf locators are unsupported");
    ShelfRecord out;out.originalFields=value;out.id=canonicalUUID(requiredText(value,"id"));out.windowsPath=requiredText(value,"windowsPath");out.name=requiredText(value,"name");out.typeDescription=requiredText(value,"typeDescription");
    require(value["isDirectory"].isBool()&&value["createdAt"].isNumber(),"Missing shelf metadata");out.isDirectory=value["isDirectory"].boolean();out.createdAt=value["createdAt"].number();
    if(!value["byteCount"].isNull()){require(value["byteCount"].isNumber(),"Invalid shelf byte count");out.byteCount=value["byteCount"].integer();}
    const auto& native=value["windowsIdentity"];require(native.isObject(),"Missing native shelf identity");
    out.identity.objectID=unhex<16>(requiredText(native,"objectID"));out.identity.volumeSerial=serialNumber(requiredText(native,"volumeSerial"));
    if(!native["volumeUUID"].isNull())out.identity.volumeUUID=canonicalUUID(requiredText(native,"volumeUUID"));
    record(out);return out;
}
void apply(ShelfRecord& record,const ShelfFileMetadata& value){
    record.windowsPath=value.windowsPath;record.name=value.name;record.typeDescription=value.typeDescription;record.byteCount=value.byteCount;
    record.isDirectory=value.isDirectory;record.identity=value.identity;if(record.identity.volumeUUID)record.identity.volumeUUID=canonicalUUID(*record.identity.volumeUUID);record.availabilityError.reset();
}
}
bool ShelfFileIdentity::matches(const ShelfFileIdentity& other)const noexcept{
    if(objectID!=other.objectID)return false;
    if(volumeUUID&&other.volumeUUID)return equalUUID(*volumeUUID,*other.volumeUUID);
    return volumeSerial==other.volumeSerial;
}
ShelfFileAccess::ShelfFileAccess(ShelfFileMetadata value,std::function<void()> cleanup):metadata_(std::move(value)),cleanup_(std::move(cleanup)){
    // Validation here would leak a successfully acquired scope if construction
    // throws. The store validates only after the fully formed RAII lease exists.
    require(bool(cleanup_),"A shelf access lease needs an explicit cleanup owner");
}
ShelfFileAccess::~ShelfFileAccess(){close();}
ShelfFileAccess::ShelfFileAccess(ShelfFileAccess&& other)noexcept:metadata_(std::move(other.metadata_)),cleanup_(std::exchange(other.cleanup_,{})){}
ShelfFileAccess& ShelfFileAccess::operator=(ShelfFileAccess&& other)noexcept{if(this!=&other){close();metadata_=std::move(other.metadata_);cleanup_=std::exchange(other.cleanup_,{});}return *this;}
void ShelfFileAccess::close()noexcept{auto cleanup=std::exchange(cleanup_,{});if(cleanup)try{cleanup();}catch(...){/* native release callbacks must be nonthrowing */}}
FileShelfStore::FileShelfStore(const std::filesystem::path& root,Platform platform):FileShelfStore(root,std::move(platform),Creation{}){}
FileShelfStore::FileShelfStore(const std::filesystem::path& root,Platform platform,Creation creation):platform_(std::move(platform)),creation_(std::move(creation)){
    require(bool(platform_.acquireImport)&&bool(platform_.resolve)&&bool(creation_.id)&&bool(creation_.timestamp),"Incomplete shelf platform/creation adapter");
    detail::validateRoot(root);path_=root/"FileShelf"/"shelf.json";persisted_=detail::readFile(path_,maximumArchiveBytes);
    if(!persisted_)return;
    try{
        auto envelope=Json::parse(*persisted_,maximumArchiveBytes);require(envelope.isObject()&&envelope["version"].isNumber(),"Missing shelf version");
        const auto version=envelope["version"].integer();if(version>1)throw StoreError(StoreErrorCode::newerVersion,"Shelf was saved by a newer version; original data preserved");
        require(version==1&&envelope["items"].isArray(),"Invalid shelf archive");std::vector<ShelfRecord> items;std::set<std::string,std::less<>> ids;
        items.reserve(envelope["items"].array().size());for(const auto& value:envelope["items"].array()){auto next=decoded(value);require(ids.insert(next.id).second,"Duplicate shelf UUID");items.push_back(std::move(next));}
        items_=std::move(items);envelope_=std::move(envelope);
    }catch(const StoreError&){throw;}catch(const std::exception&){throw StoreError(StoreErrorCode::invalid,"Shelf archive is invalid; original data preserved");}
}
void FileShelfStore::commit(std::vector<ShelfRecord> next){
    auto envelope=envelope_;envelope["version"]=1;Json::Array rows;rows.reserve(next.size());std::set<std::string,std::less<>> ids;
    for(const auto& row:next){require(ids.insert(canonicalUUID(row.id)).second,"Duplicate shelf UUID");rows.push_back(encoded(row));}envelope["items"]=std::move(rows);
    // Prepare all owned state before disk replacement. Success publication uses
    // only moves/swaps, so a later allocation cannot split disk and memory state.
    std::optional<std::string> bytes;
    try{bytes=envelope.encode(maximumArchiveBytes);}catch(const std::exception&){throw StoreError(StoreErrorCode::tooLarge,"Shelf metadata exceeds the explicit archive byte bound");}
    detail::replaceFile(path_,persisted_,*bytes,maximumArchiveBytes);
    items_.swap(next);envelope_=std::move(envelope);persisted_.swap(bytes);
}
std::size_t FileShelfStore::add(std::span<const std::string> tokens){
    Operation operation(busy_);if(tokens.empty())return 0;auto next=items_;std::set<std::string,std::less<>> ids;for(const auto& item:next)ids.insert(item.id);
    for(const auto& token:tokens){
        auto access=platform_.acquireImport(token);require(access.open(),"Native import returned a closed shelf lease");const auto& value=access.metadata();metadata(value);
        if(std::any_of(next.begin(),next.end(),[&](const auto& item){return item.identity.matches(value.identity);}))continue;
        ShelfRecord item;item.id=canonicalUUID(creation_.id());item.createdAt=creation_.timestamp();apply(item,value);record(item);
        require(ids.insert(item.id).second,"Shelf UUID provider reused an existing identity");next.push_back(std::move(item));
    }
    const auto added=next.size()-items_.size();if(added)commit(std::move(next));return added;
}
bool FileShelfStore::remove(std::string_view id){Operation operation(busy_);const auto found=std::find_if(items_.begin(),items_.end(),[&](const auto& row){return equalUUID(row.id,id);});if(found==items_.end())return false;
    auto next=items_;next.erase(next.begin()+std::distance(items_.begin(),found));commit(std::move(next));return true;
}
bool FileShelfStore::clear(){Operation operation(busy_);if(items_.empty())return false;commit({});return true;}
ShelfFileAccess FileShelfStore::resolve(const ShelfRecord& record){auto lease=platform_.resolve(record);require(lease.open(),"Native resolver returned a closed shelf lease");metadata(lease.metadata());
    if(!record.identity.matches(lease.metadata().identity))throw StoreError(StoreErrorCode::unavailable,"Original shelf item is unavailable; a different item occupies its former location");return lease;
}
ShelfFileAccess FileShelfStore::access(std::string_view id){Operation operation(busy_);const auto found=std::find_if(items_.begin(),items_.end(),[&](const auto& row){return equalUUID(row.id,id);});
    if(found==items_.end())throw StoreError(StoreErrorCode::unavailable,"Item is no longer on the shelf");return resolve(*found);
}
void FileShelfStore::refresh(){Operation operation(busy_);auto next=items_;bool changed=false;
    for(auto& row:next){try{auto lease=resolve(row);auto refreshed=row;apply(refreshed,lease.metadata());if(encoded(refreshed)!=encoded(row))changed=true;row=std::move(refreshed);}
        catch(const std::exception& error){row.availabilityError=error.what();}}
    if(changed)commit(std::move(next));else items_.swap(next);
}
ShelfCopyBundle FileShelfStore::prepareCopy(std::span<const std::string> ids){Operation operation(busy_);std::set<std::string,std::less<>> wanted;
    for(const auto& id:ids){const auto canonical=canonicalUUID(id);require(std::any_of(items_.begin(),items_.end(),[&](const auto& row){return row.id==canonical;}),"Copy requested a reference no longer on the shelf");wanted.insert(canonical);}
    ShelfCopyBundle bundle;bundle.ids.reserve(wanted.size());bundle.accesses.reserve(wanted.size());
    // Resolve the entire selected group before a receiver sees any URL/path.
    // If one fails, RAII releases all earlier leases; never export a subset.
    for(const auto& row:items_)if(wanted.contains(row.id)){auto lease=resolve(row);bundle.ids.push_back(row.id);bundle.accesses.push_back(std::move(lease));}return bundle;
}
void FileShelfStore::copy(std::span<const std::string> ids,std::function<void(ShelfCopyBundle)> receiver){require(bool(receiver),"Missing copy-only shelf receiver");auto bundle=prepareCopy(ids);if(bundle.accesses.empty())return;
    // No store state or guard is touched after handoff. A native receiver may
    // close the shelf/store during its nested loop while keeping these leases.
    receiver(std::move(bundle));}
} // namespace ehud::data
