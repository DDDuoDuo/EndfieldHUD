#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#ifdef _WIN32
#include <winsqlite3.h>
#else
#include <sqlite3.h>
#endif
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace ehud::data {
namespace {
constexpr std::size_t payloadLimit=16*1024*1024, totalLimit=64*1024*1024;
void require(bool condition,const char* message) {if(!condition) throw StoreError(StoreErrorCode::invalid,message);}
[[noreturn]] void unavailable(sqlite3* db) {
    throw StoreError(StoreErrorCode::unavailable,db?sqlite3_errmsg(db):"Notes database unavailable");
}
void check(int code,sqlite3* db) {if(code!=SQLITE_OK) unavailable(db);}
struct Statement {
    sqlite3_stmt* value{};sqlite3* db{};
    Statement(sqlite3* database,const char* sql):db(database) {
        const int code=sqlite3_prepare_v2(db,sql,-1,&value,nullptr);
        if(code!=SQLITE_OK) {if(value) sqlite3_finalize(value);value=nullptr;unavailable(db);}
    }
    ~Statement(){if(value) sqlite3_finalize(value);}
    Statement(const Statement&)=delete;
    void text(int column,const std::string& value) {
        require(value.size()<=static_cast<std::size_t>(std::numeric_limits<int>::max()),"Note string too large");
        check(sqlite3_bind_text(this->value,column,value.data(),static_cast<int>(value.size()),SQLITE_TRANSIENT),db);
    }
    void optional(int column,const std::optional<std::string>& value) {
        if(value) text(column,*value);else check(sqlite3_bind_null(this->value,column),db);
    }
    void real(int column,double value) {check(sqlite3_bind_double(this->value,column,value),db);}
    void integer(int column,std::int64_t value) {check(sqlite3_bind_int64(this->value,column,value),db);}
    void done() {if(sqlite3_step(value)!=SQLITE_DONE) unavailable(db);}
};
void execute(sqlite3* db,const char* sql) {check(sqlite3_exec(db,sql,nullptr,nullptr,nullptr),db);}
std::int64_t scalar(sqlite3* db,const char* sql) {
    Statement statement(db,sql);if(sqlite3_step(statement.value)!=SQLITE_ROW) unavailable(db);
    require(sqlite3_column_type(statement.value,0)==SQLITE_INTEGER,"Invalid Notes database integer");return sqlite3_column_int64(statement.value,0);
}
std::string text(sqlite3_stmt* row,int column,std::size_t& total) {
    require(sqlite3_column_type(row,column)==SQLITE_TEXT,"Invalid Notes database text");
    const int count=sqlite3_column_bytes(row,column);require(count>=0 && static_cast<std::size_t>(count)<=payloadLimit,"Notes payload exceeds safety limit");
    require(static_cast<std::size_t>(count)<=totalLimit-total,"Notes data exceeds memory safety limit");total+=static_cast<std::size_t>(count);
    const auto* raw=sqlite3_column_text(row,column);require(raw!=nullptr,"Unreadable Notes database text");
    std::string result(reinterpret_cast<const char*>(raw),static_cast<std::size_t>(count));require(Json::validUtf8(result),"Invalid Notes UTF-8 text");return result;
}
std::optional<std::string> optional(sqlite3_stmt* row,int column,std::size_t& total) {
    if(sqlite3_column_type(row,column)==SQLITE_NULL) return {};return text(row,column,total);
}
double real(sqlite3_stmt* row,int column) {
    const int type=sqlite3_column_type(row,column);require(type==SQLITE_FLOAT||type==SQLITE_INTEGER,"Invalid Notes database coordinate");
    const double value=sqlite3_column_double(row,column);require(std::isfinite(value),"Nonfinite Notes database coordinate");return value;
}
std::int64_t integer(sqlite3_stmt* row,int column) {
    require(sqlite3_column_type(row,column)==SQLITE_INTEGER,"Invalid Notes database integer");return sqlite3_column_int64(row,column);
}
Json parse(std::string_view value) {
    try {return Json::parse(value,payloadLimit);}catch(const std::exception&) {throw StoreError(StoreErrorCode::invalid,"Invalid note JSON payload; original data preserved");}
}
bool imageName(std::string_view value) {
    return value.size()==40 && value.substr(36)==".png" && validUUID(value.substr(0,36));
}
std::size_t utf16Length(std::string_view text) {
    require(Json::validUtf8(text),"Invalid note UTF-8");std::size_t length{};
    for(const unsigned char c:text) if((c&0xc0)!=0x80) length+=c>=0xf0?2:1;
    return length;
}
double requiredNumber(const Json& object,const char* key) {require(object[key].isNumber(),"Missing note payload number");return object[key].number();}
std::int64_t requiredInteger(const Json& object,const char* key) {require(object[key].isNumber(),"Missing note payload integer");return object[key].integer();}
void payloadVersion(const Json& value) {
    require(value.isObject(),"Invalid note payload object");const auto version=requiredInteger(value,"version");
    if(version>1) throw StoreError(StoreErrorCode::newerVersion,"Note payload requires a newer app version");
    require(version==1,"Invalid note payload version");
}
void color(const Json& value) {
    require(value.isObject(),"Invalid drawing/text color");
    for(const char* key:{"red","green","blue","alpha"}) {const auto c=requiredNumber(value,key);require(c>=0&&c<=1,"Invalid drawing/text color channel");}
}
void style(const Json& value) {
    require(value.isObject(),"Invalid note text style");const auto size=requiredNumber(value,"fontSize");require(size>=6&&size<=144,"Invalid note font size");
    if(!value["fontName"].isNull()) require(value["fontName"].isString()&&!value["fontName"].string().empty()&&value["fontName"].string().size()<=256,"Invalid note font name");
    if(!value["color"].isNull()) color(value["color"]);
    for(const char* key:{"bold","italic","underline","strikethrough"}) require(value[key].isBool(),"Invalid note text trait");
}
void validateRichText(const std::string& raw,const std::string& text) {
    const auto value=parse(raw);payloadVersion(value);require(value["runs"].isArray()&&value["runs"].array().size()<=50'000,"Invalid note formatting runs");
    const auto length=utf16Length(text);std::size_t previousEnd{};
    for(const auto& run:value["runs"].array()) {
        const auto offset=requiredInteger(run,"location"),count=requiredInteger(run,"length");
        require(offset>=0&&count>0&&static_cast<std::uint64_t>(offset)>=previousEnd&&static_cast<std::uint64_t>(offset)<=length&&
            static_cast<std::uint64_t>(count)<=length-static_cast<std::size_t>(offset),"Invalid note UTF-16 formatting range");
        previousEnd=static_cast<std::size_t>(offset+count);style(run["style"]);
    }
}
void validateMedia(const std::string& raw) {
    const auto value=parse(raw);payloadVersion(value);require(value["kind"].isString(),"Invalid note media kind");
    const auto kind=value["kind"].string();require(kind=="image"||kind=="gif"||kind=="video","Invalid note media kind");
    if(value.contains("referencePlatform") || value.contains("windowsPath")) {
        require(value["referencePlatform"].isString()&&value["referencePlatform"].string()=="windows"&&value["windowsPath"].isString()&&
            validWindowsFilePath(value["windowsPath"].string()),"Invalid native Windows media locator");
        require(!value.contains("bookmark")&&!value.contains("isSecurityScoped")&&!value.contains("lastKnownPath"),"Ambiguous mixed-platform media reference");
    } else {
        require(value["bookmark"].isString(),"Invalid opaque Mac media bookmark");
        const auto bookmark=value["bookmark"].string();
        require(!bookmark.empty()&&bookmark.size()<=1'398'104&&bookmark.size()%4==0,"Invalid opaque Mac media bookmark");
        const auto padding=bookmark.ends_with("==")?2u:bookmark.ends_with("=")?1u:0u;
        require(bookmark.size()/4*3-padding<=1024*1024,"Opaque Mac bookmark exceeds byte limit");
        for(std::size_t i=0;i<bookmark.size()-padding;++i) {const char c=bookmark[i];require((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/',"Invalid Mac bookmark base64");}
        require(value["isSecurityScoped"].isBool(),"Invalid media security scope flag");
        require(value["lastKnownPath"].isString()&&!value["lastKnownPath"].string().empty()&&value["lastKnownPath"].string().size()<=32768&&value["lastKnownPath"].string().front()=='/'&&value["lastKnownPath"].string().find('\0')==std::string::npos,"Invalid Mac media path");
    }
    require(value["displayName"].isString()&&!value["displayName"].string().empty()&&value["displayName"].string().size()<=4096&&value["displayName"].string().find('\0')==std::string::npos,"Invalid media display name");
    for(const char* key:{"pixelWidth","pixelHeight"}) {const auto n=requiredInteger(value,key);require(n>=1&&n<=65536,"Invalid media dimensions");}
    const auto frames=requiredInteger(value,"frameCount");require(frames>=1&&frames<=2000&&(kind!="image"||frames==1),"Invalid media frame count");
    require(kind!="video"||!value["duration"].isNull(),"Missing video duration");
    if(!value["duration"].isNull()) require(value["duration"].isNumber()&&value["duration"].number()>0&&value["duration"].number()<=31'536'000,"Invalid media duration");
}
void validateDrawing(const std::string& raw) {
    const auto value=parse(raw);payloadVersion(value);require(value["strokes"].isArray()&&value["strokes"].array().size()<=2000,"Invalid drawing strokes");std::size_t total{};
    for(const auto& stroke:value["strokes"].array()) {
        const auto width=requiredNumber(stroke,"width");require(width>=1&&width<=80,"Invalid drawing stroke width");color(stroke["color"]);
        require(stroke["points"].isArray()&&!stroke["points"].array().empty()&&stroke["points"].array().size()<=4096,"Invalid drawing stroke points");
        total+=stroke["points"].array().size();require(total<=100'000,"Drawing exceeds point limit");
        for(const auto& point:stroke["points"].array()) {const auto x=requiredNumber(point,"x"),y=requiredNumber(point,"y");require(x>=0&&x<=1&&y>=0&&y<=1,"Invalid normalized drawing point");}
    }
}
void validate(const Note& value) {
    require(validUUID(value.id)&&Json::validUtf8(value.text)&&value.text.size()<=payloadLimit,"Invalid note identity/text");
    require(std::isfinite(value.createdAt)&&std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.width)&&std::isfinite(value.height),"Invalid note geometry/date");
    std::set<std::string> identities;
    for(const auto& item:value.items) require(validUUID(item.id)&&identities.insert(item.id).second&&Json::validUtf8(item.text)&&item.text.size()<=payloadLimit&&item.originalFields.isObject(),"Invalid checklist item");
    if(value.imageName) require(imageName(*value.imageName),"Invalid note managed image reference");
    require(value.kind!=NoteKind::image || value.imageName || value.media,"Missing note image reference");
    require(!value.richText || value.kind==NoteKind::text,"Formatting belongs only to text notes");
    require(!value.media || value.kind==NoteKind::image,"Media belongs only to image notes");
    require(!value.drawing || value.kind==NoteKind::drawing,"Drawing belongs only to drawing notes");
    require(value.kind!=NoteKind::drawing || value.drawing,"Missing drawing payload");
    if(value.richText) validateRichText(*value.richText,value.text);
    if(value.media) validateMedia(*value.media);
    if(value.drawing) validateDrawing(*value.drawing);
}
Note constrained(Note value) {
    const double minimumWidth=value.kind==NoteKind::todo?180:110,minimumHeight=value.kind==NoteKind::todo?105:70;
    value.width=std::clamp(std::isfinite(value.width)?value.width:160.,minimumWidth,32768.);
    value.height=std::clamp(std::isfinite(value.height)?value.height:110.,minimumHeight,32768.);
    value.x=std::clamp(std::isfinite(value.x)?value.x:24.,-1'000'000.,1'000'000.);
    value.y=std::clamp(std::isfinite(value.y)?value.y:50.,-1'000'000.,1'000'000.);return value;
}
std::vector<ChecklistItem> checklist(std::string_view raw) {
    const auto value=parse(raw);require(value.isArray(),"Invalid note checklist");std::vector<ChecklistItem> result;result.reserve(value.array().size());
    for(const auto& record:value.array()) {
        require(record.isObject()&&record["id"].isString()&&record["text"].isString()&&record["isChecked"].isBool(),"Invalid checklist record");
        auto unknown=record;unknown.erase("id");unknown.erase("text");unknown.erase("isChecked");
        result.push_back(ChecklistItem{record["id"].string(),record["text"].string(),record["isChecked"].boolean(),std::move(unknown)});
    }return result;
}
std::string checklist(const std::vector<ChecklistItem>& items) {
    Json::Array result;result.reserve(items.size());for(const auto& item:items) {
        auto record=item.originalFields;record["id"]=item.id;record["text"]=item.text;record["isChecked"]=item.isChecked;result.push_back(std::move(record));
    }return Json(std::move(result)).encode(payloadLimit);
}
const char* kind(NoteKind value) {
    switch(value) {case NoteKind::text:case NoteKind::drawing:return "text";case NoteKind::todo:return "todo";case NoteKind::image:return "image";}
    throw StoreError(StoreErrorCode::invalid,"Invalid note kind");
}
std::size_t footprint(const Note& value) {
    std::size_t size=value.id.size()+std::char_traits<char>::length(kind(value.kind))+value.text.size()+checklist(value.items).size();
    for(const auto& field:{value.imageName,value.richText,value.media,value.drawing}) if(field) size+=field->size();
    return size;
}
std::vector<Note> readNotes(sqlite3* db,bool payloads=true) {
    Statement statement(db,payloads?
        "SELECT id,kind,text,checklist,image_name,x,y,width,height,z_index,created_at,is_pinned,rich_text,media,drawing FROM notes ORDER BY z_index,created_at,id":
        "SELECT id,kind,text,checklist,image_name,x,y,width,height,z_index,created_at,is_pinned FROM notes ORDER BY z_index,created_at,id");
    std::vector<Note> result;std::size_t bytes{};std::set<std::string> ids;
    while(true) {
        const int code=sqlite3_step(statement.value);if(code==SQLITE_DONE) break;if(code!=SQLITE_ROW) unavailable(db);
        require(result.size()<10'000,"Notes record count exceeds safety limit");Note note{.id=text(statement.value,0,bytes),.createdAt=0};const auto storedKind=text(statement.value,1,bytes);
        require(storedKind=="text"||storedKind=="todo"||storedKind=="image","Invalid stored note kind");
        note.kind=storedKind=="todo"?NoteKind::todo:storedKind=="image"?NoteKind::image:NoteKind::text;
        note.text=text(statement.value,2,bytes);note.items=checklist(text(statement.value,3,bytes));note.imageName=optional(statement.value,4,bytes);
        note.x=real(statement.value,5);note.y=real(statement.value,6);note.width=real(statement.value,7);note.height=real(statement.value,8);
        note.zIndex=integer(statement.value,9);note.createdAt=real(statement.value,10);const auto pinned=integer(statement.value,11);require(pinned==0||pinned==1,"Invalid stored note pin");note.isPinned=pinned!=0;
        if(payloads) {note.richText=optional(statement.value,12,bytes);note.media=optional(statement.value,13,bytes);note.drawing=optional(statement.value,14,bytes);
            if(note.drawing) {require(storedKind=="text"&&!note.richText,"Invalid stored drawing base kind");note.kind=NoteKind::drawing;}}
        require(ids.insert(note.id).second,"Duplicate note UUID");validate(note);result.push_back(constrained(std::move(note)));
    }return result;
}
void sort(std::vector<Note>& values) {std::sort(values.begin(),values.end(),[](const Note& a,const Note& b){if(a.zIndex!=b.zIndex) return a.zIndex<b.zIndex;if(a.createdAt!=b.createdAt) return a.createdAt<b.createdAt;return a.id<b.id;});}
}
struct NotesStore::Impl {
    std::filesystem::path path;
    sqlite3* db{};std::vector<Note> notes;std::int64_t dataVersion{};std::size_t payloadBytes{};
    ~Impl(){if(db) sqlite3_close_v2(db);}
    void begin() {
        execute(db,"BEGIN IMMEDIATE");
        try {if(scalar(db,"PRAGMA data_version")!=dataVersion) throw StoreError(StoreErrorCode::changedOnDisk,"Notes changed in another instance; reopen before editing");}
        catch(...) {try {execute(db,"ROLLBACK");}catch(...){}throw;}
    }
};
NotesStore::NotesStore(const std::filesystem::path& root):impl_(std::make_unique<Impl>()) {
    detail::validateRoot(root);impl_->path=root/"Notes"/"notes.sqlite3";
    detail::validateRoot(impl_->path.parent_path());
    detail::validateDataFile(impl_->path);std::error_code error;
    std::filesystem::create_directories(impl_->path.parent_path(),error);if(error) throw StoreError(StoreErrorCode::unavailable,"Notes directory could not be created");
    const auto encoded=impl_->path.u8string();const std::string path(reinterpret_cast<const char*>(encoded.data()),encoded.size());
    if(sqlite3_open_v2(path.c_str(),&impl_->db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK) unavailable(impl_->db);
    if(sqlite3_libversion_number()<3024000) throw StoreError(StoreErrorCode::unavailable,"The OS SQLite version does not support Notes transactions");
    check(sqlite3_busy_timeout(impl_->db,250),impl_->db);sqlite3_limit(impl_->db,SQLITE_LIMIT_LENGTH,static_cast<int>(payloadLimit));
#ifdef _WIN32
    check(sqlite3_enable_load_extension(impl_->db,0),impl_->db);
#endif
    {
        Statement integrity(impl_->db,"PRAGMA quick_check(1)");
        if(sqlite3_step(integrity.value)!=SQLITE_ROW) unavailable(impl_->db);
        std::size_t bytes{};require(text(integrity.value,0,bytes)=="ok","Invalid Notes database; original data preserved");
    }
    auto version=scalar(impl_->db,"PRAGMA user_version");
    if(version>2) throw StoreError(StoreErrorCode::newerVersion,"Notes require a newer app version");require(version>=0,"Invalid Notes schema version");
    require(scalar(impl_->db,"SELECT count(*) FROM sqlite_master WHERE type='trigger'")==0,"Unexpected Notes database triggers");
    execute(impl_->db,"BEGIN IMMEDIATE");
    try {
        const auto tables=scalar(impl_->db,"SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'");
        if(version==0&&tables==0) {
            execute(impl_->db,"CREATE TABLE notes(id TEXT PRIMARY KEY NOT NULL,kind TEXT NOT NULL CHECK(kind IN ('text','todo','image')),text TEXT NOT NULL,checklist TEXT NOT NULL,image_name TEXT,x REAL NOT NULL,y REAL NOT NULL,width REAL NOT NULL,height REAL NOT NULL,z_index INTEGER NOT NULL,created_at REAL NOT NULL,is_pinned INTEGER NOT NULL CHECK(is_pinned IN (0,1)),rich_text TEXT,media TEXT,drawing TEXT)");
            execute(impl_->db,"PRAGMA user_version=2");version=2;
        }
        if(version<2) {
            require(tables==1&&scalar(impl_->db,"SELECT count(*) FROM sqlite_master WHERE type='table' AND name='notes'")==1,
                "Unexpected legacy Notes database tables");(void)readNotes(impl_->db,false);
            execute(impl_->db,"ALTER TABLE notes ADD COLUMN rich_text TEXT");execute(impl_->db,"ALTER TABLE notes ADD COLUMN media TEXT");execute(impl_->db,"ALTER TABLE notes ADD COLUMN drawing TEXT");execute(impl_->db,"PRAGMA user_version=2");
        }
        impl_->notes=readNotes(impl_->db);
        for(const auto& note:impl_->notes) {const auto size=footprint(note);require(size<=totalLimit-impl_->payloadBytes,"Notes data exceeds memory safety limit");impl_->payloadBytes+=size;}
        impl_->dataVersion=scalar(impl_->db,"PRAGMA data_version");execute(impl_->db,"COMMIT");
    } catch(const StoreError&) {try {execute(impl_->db,"ROLLBACK");}catch(...){}throw;}
      catch(const std::exception&) {try {execute(impl_->db,"ROLLBACK");}catch(...){}throw StoreError(StoreErrorCode::invalid,"Invalid Notes payload; original data preserved");}
}
NotesStore::~NotesStore()=default;
const std::vector<Note>& NotesStore::notes() const noexcept {return impl_->notes;}
const std::filesystem::path& NotesStore::path() const noexcept {return impl_->path;}
bool NotesStore::upsert(const Note& raw) {
    Note note=constrained(raw);try {validate(note);}catch(const StoreError&) {throw;}catch(const std::exception&) {throw StoreError(StoreErrorCode::invalid,"Invalid note payload");}
    auto found=std::find_if(impl_->notes.begin(),impl_->notes.end(),[&](const Note& value){return value.id==note.id;});
    if(found!=impl_->notes.end()&&*found==note) return false;
    const bool reorder=found==impl_->notes.end()||found->zIndex!=note.zIndex||found->createdAt!=note.createdAt;
    const auto size=footprint(note);require(size<=payloadLimit,"Note row exceeds size safety limit");
    const auto remaining=impl_->payloadBytes-(found==impl_->notes.end()?0:footprint(*found));
    require(size<=totalLimit-remaining,"Notes data exceeds memory safety limit");const auto bytes=remaining+size;
    if(found==impl_->notes.end()) {require(impl_->notes.size()<10'000,"Notes record count exceeds safety limit");impl_->notes.reserve(impl_->notes.size()+1);}
    const auto checklistText=checklist(note.items);impl_->begin();
    try {
        Statement statement(impl_->db,"INSERT INTO notes(id,kind,text,checklist,image_name,x,y,width,height,z_index,created_at,is_pinned,rich_text,media,drawing) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET kind=excluded.kind,text=excluded.text,checklist=excluded.checklist,image_name=excluded.image_name,x=excluded.x,y=excluded.y,width=excluded.width,height=excluded.height,z_index=excluded.z_index,created_at=excluded.created_at,is_pinned=excluded.is_pinned,rich_text=excluded.rich_text,media=excluded.media,drawing=excluded.drawing");
        statement.text(1,note.id);statement.text(2,kind(note.kind));statement.text(3,note.text);statement.text(4,checklistText);statement.optional(5,note.imageName);
        statement.real(6,note.x);statement.real(7,note.y);statement.real(8,note.width);statement.real(9,note.height);statement.integer(10,note.zIndex);statement.real(11,note.createdAt);statement.integer(12,note.isPinned?1:0);
        statement.optional(13,note.richText);statement.optional(14,note.media);statement.optional(15,note.drawing);statement.done();execute(impl_->db,"COMMIT");
    } catch(...) {try {execute(impl_->db,"ROLLBACK");}catch(...){}throw;}
    // Locate again because reserving may have invalidated the previous iterator.
    found=std::find_if(impl_->notes.begin(),impl_->notes.end(),[&](const Note& value){return value.id==note.id;});
    if(found==impl_->notes.end()) impl_->notes.push_back(std::move(note));else *found=std::move(note);impl_->payloadBytes=bytes;if(reorder) sort(impl_->notes);return true;
}
bool NotesStore::remove(std::string_view id) {
    require(validUUID(id),"Invalid note UUID");const auto found=std::find_if(impl_->notes.begin(),impl_->notes.end(),[&](const Note& value){return value.id==id;});
    if(found==impl_->notes.end()) return false;const auto bytes=footprint(*found);impl_->begin();
    try {Statement statement(impl_->db,"DELETE FROM notes WHERE id=?");statement.text(1,std::string(id));statement.done();execute(impl_->db,"COMMIT");}
    catch(...) {try {execute(impl_->db,"ROLLBACK");}catch(...){}throw;}
    impl_->payloadBytes-=bytes;impl_->notes.erase(found);return true;
}
bool validWindowsFilePath(std::string_view path) noexcept {
    if(path.empty()||path.size()>32768||!Json::validUtf8(path)) return false;
    auto separator=[](char c){return c=='\\'||c=='/';};
    std::size_t start{};bool unc{};
    if(path.size()>=3&&((path[0]>='A'&&path[0]<='Z')||(path[0]>='a'&&path[0]<='z'))&&path[1]==':'&&separator(path[2])) start=3;
    else if(path.size()>=3&&separator(path[0])&&separator(path[1])) {start=2;unc=true;}
    else return false;
    if(start==path.size()||separator(path.back())) return false;
    std::size_t components{};
    while(start<path.size()) {
        auto end=path.find_first_of("\\/",start);if(end==std::string_view::npos) end=path.size();
        const auto part=path.substr(start,end-start);
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ') return false;
        for(const unsigned char c:part) if(c<32||c==127||c=='<'||c=='>'||c==':'||c=='"'||c=='|'||c=='?'||c=='*') return false;
        const auto base=part.substr(0,part.find('.'));
        auto equals=[&](std::string_view reserved) {if(base.size()!=reserved.size()) return false;for(std::size_t i=0;i<base.size();++i) {char c=base[i];if(c>='a'&&c<='z') c-=32;if(c!=reserved[i]) return false;}return true;};
        if(equals("CON")||equals("PRN")||equals("AUX")||equals("NUL")) return false;
        if(base.size()==4&&(base[3]>='1'&&base[3]<='9')) {
            auto first=base.substr(0,3);bool com=true,lpt=true;
            for(std::size_t i=0;i<3;++i) {char c=first[i];if(c>='a'&&c<='z') c-=32;com&=c=="COM"[i];lpt&=c=="LPT"[i];}
            if(com||lpt) return false;
        }
        ++components;start=end+1;
    }
    return !unc||components>=3;
}
std::string makeWindowsMediaReference(std::string path,std::string displayName,int width,int height,
    std::string kind,std::optional<double> duration,int frames) {
    Json record=Json::Object{{"version",1},{"kind",std::move(kind)},{"referencePlatform","windows"},
        {"windowsPath",std::move(path)},{"displayName",std::move(displayName)},{"pixelWidth",width},
        {"pixelHeight",height},{"frameCount",frames}};
    if(duration) record["duration"]=*duration;
    const auto raw=record.encode(payloadLimit);validateMedia(raw);return raw;
}
}
