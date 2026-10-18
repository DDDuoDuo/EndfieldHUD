#include "modules/archive_repository.hpp"
#include "core/data/file_io.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#include <sys/stat.h>
#endif
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <iterator>
#include <limits>
#include <set>
#include <thread>

namespace endfield::modules {
namespace {
using namespace ehud::data;using J=ArchiveJson;
constexpr std::size_t thumbnailLimit=2*1024*1024;
void need(bool value,const char*m){if(!value)throw StoreError(StoreErrorCode::invalid,m);}
[[noreturn]]void unavailable(sqlite3*db){throw StoreError(StoreErrorCode::unavailable,db?sqlite3_errmsg(db):"The archive could not be saved.");}
void checked(int value,sqlite3*db){if(value!=SQLITE_OK)unavailable(db);}
struct Query {
    sqlite3*db{};sqlite3_stmt*statement{};
    Query(sqlite3*d,const char*sql):db(d){if(sqlite3_prepare_v2(d,sql,-1,&statement,nullptr)!=SQLITE_OK){if(statement)sqlite3_finalize(statement);statement=nullptr;unavailable(d);}}
    ~Query(){if(statement)sqlite3_finalize(statement);}Query(const Query&)=delete;
    void text(int at,std::string_view value){need(value.size()<=std::size_t(INT_MAX),"Archive SQL text exceeds safety bound");checked(sqlite3_bind_text(statement,at,value.data(),static_cast<int>(value.size()),SQLITE_TRANSIENT),db);}
    void optional(int at,const std::optional<std::string>&value){if(value)text(at,*value);else checked(sqlite3_bind_null(statement,at),db);}
    void real(int at,double value){need(std::isfinite(value),"Nonfinite Archive SQL date");checked(sqlite3_bind_double(statement,at,value),db);}
    void integer(int at,std::int64_t value){checked(sqlite3_bind_int64(statement,at,value),db);}
    void blob(int at,const std::optional<std::string>&value){if(value){need(value->size()<=std::size_t(INT_MAX),"Archive SQL payload exceeds safety bound");checked(sqlite3_bind_blob(statement,at,value->data(),static_cast<int>(value->size()),SQLITE_TRANSIENT),db);}else checked(sqlite3_bind_null(statement,at),db);}
    bool row(){const auto result=sqlite3_step(statement);if(result==SQLITE_ROW)return true;if(result!=SQLITE_DONE)unavailable(db);return false;}
    void done(){need(!row(),"Unexpected Archive write result");}
};
void execute(sqlite3*db,const char*sql){checked(sqlite3_exec(db,sql,nullptr,nullptr,nullptr),db);}
std::int64_t integer(sqlite3_stmt*q,int at){need(sqlite3_column_type(q,at)==SQLITE_INTEGER,"Invalid Archive SQL integer");return sqlite3_column_int64(q,at);}
std::int64_t scalar(sqlite3*db,const char*sql){Query q(db,sql);need(q.row(),"Missing Archive SQL scalar");return integer(q.statement,0);}
double real(sqlite3_stmt*q,int at){const auto type=sqlite3_column_type(q,at);need(type==SQLITE_INTEGER||type==SQLITE_FLOAT,"Invalid Archive SQL date");const auto value=sqlite3_column_double(q,at);need(std::isfinite(value),"Nonfinite Archive SQL date");return value;}
std::string bytes(sqlite3_stmt*q,int at,std::size_t maximum,bool blob=false){need(sqlite3_column_type(q,at)==(blob?SQLITE_BLOB:SQLITE_TEXT),"Invalid Archive SQL payload type");const auto n=sqlite3_column_bytes(q,at);need(n>=0&&std::size_t(n)<=maximum,"Archive SQL payload exceeds source limit");const auto*raw=blob?sqlite3_column_blob(q,at):sqlite3_column_text(q,at);need(raw||n==0,"Unreadable Archive SQL payload");const std::string value(n?static_cast<const char*>(raw):"",std::size_t(n));need(J::validUtf8(value),"Invalid Archive SQL UTF-8");return value;}
std::optional<std::string>optional(sqlite3_stmt*q,int at){if(sqlite3_column_type(q,at)==SQLITE_NULL)return {};auto value=bytes(q,at,36);need(validUUID(value),"Invalid Archive category UUID");return value;}
std::optional<J>thumbnail(sqlite3_stmt*q,int at){if(sqlite3_column_type(q,at)==SQLITE_NULL)return {};auto value=J::parse(bytes(q,at,thumbnailLimit,true),thumbnailLimit);validateArchiveMedia(value);return value;}
ArchiveTemplate type(std::string_view value){need(value=="journal"||value=="research","Unknown Archive SQL template");return value=="journal"?ArchiveTemplate::journal:ArchiveTemplate::research;}
const char*type(ArchiveTemplate value){need(value==ArchiveTemplate::journal||value==ArchiveTemplate::research,"Unknown Archive template");return value==ArchiveTemplate::journal?"journal":"research";}
std::optional<std::string>thumbnailBytes(const ArchiveEntry&e){return e.media.empty()?std::nullopt:std::optional(e.media.front().encode(thumbnailLimit));}
struct FileIdentity {
#ifdef _WIN32
    ULONGLONG volume{};std::array<std::uint8_t,16>id{};
#else
    dev_t device{};ino_t inode{};
#endif
    bool operator==(const FileIdentity&)const=default;
};
FileIdentity identity(const std::filesystem::path&path){
    ehud::data::detail::validateDataFile(path);
#ifdef _WIN32
    const auto handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE)throw StoreError(StoreErrorCode::unavailable,"Archive file identity is unavailable");
    struct Close{HANDLE value;~Close(){CloseHandle(value);}}close{handle};FILE_ID_INFO info{};BY_HANDLE_FILE_INFORMATION basic{};
    need(GetFileInformationByHandle(handle,&basic)&&!(basic.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),"Archive path is not its original regular file");
    if(!GetFileInformationByHandleEx(handle,FileIdInfo,&info,sizeof(info)))throw StoreError(StoreErrorCode::unavailable,"Archive file identity is unavailable");FileIdentity result;result.volume=info.VolumeSerialNumber;std::copy(std::begin(info.FileId.Identifier),std::end(info.FileId.Identifier),result.id.begin());return result;
#else
    struct stat info{};if(lstat(path.c_str(),&info)!=0)throw StoreError(StoreErrorCode::unavailable,"Archive file identity is unavailable");need(S_ISREG(info.st_mode),"Archive path is not a regular file");return {info.st_dev,info.st_ino};
#endif
}
}
struct ArchiveSQLiteRepository::Impl {
    std::filesystem::path directory,path;ArchiveTextRules rules;sqlite3*db{};std::optional<std::thread::id>thread;FileIdentity fileIdentity;std::int64_t dataVersion{};
    Impl(std::filesystem::path d,ArchiveTextRules r):directory(std::move(d)),path(directory/"archive.sqlite"),rules(std::move(r)){need(directory.is_absolute()&&rules.characters&&rules.trimmed&&rules.categoryNameKey,"Archive needs an explicit absolute directory and Unicode rules");}
    ~Impl(){if(db)sqlite3_close_v2(db);}
    void owner(){if(thread)need(*thread==std::this_thread::get_id(),"Archive repository belongs to one FIFO file executor");else thread=std::this_thread::get_id();}
    void unchanged(){if(identity(path)!=fileIdentity)throw StoreError(StoreErrorCode::changedOnDisk,"Archive file was replaced; reopen before editing");}
    void begin(){unchanged();execute(db,"BEGIN IMMEDIATE");try{if(scalar(db,"PRAGMA data_version")!=dataVersion)throw StoreError(StoreErrorCode::changedOnDisk,"Archive changed in another instance; reopen before editing");}catch(...){try{execute(db,"ROLLBACK");}catch(...){}throw;}}
    template<class F>void transaction(F&&f){begin();try{f();execute(db,"COMMIT");}catch(...){try{execute(db,"ROLLBACK");}catch(...){}throw;}}
    std::optional<std::string>selection(){Query q(db,"SELECT value FROM state WHERE key='selection'");if(!q.row())return {};auto value=bytes(q.statement,0,36);if(value.empty())return {};need(ehud::data::validUUID(value),"Invalid Archive selected UUID");return value;}
    ArchiveEntry decoded(sqlite3_stmt*q,int payload,int metadata,bool v1=false){auto e=decodeArchiveEntry(J::parse(bytes(q,payload,archiveMaximumPayloadBytes,true),archiveMaximumPayloadBytes),rules);
        need(e.id==bytes(q,metadata,36)&&type(bytes(q,metadata+1,16))==e.type&&bytes(q,metadata+2,archiveMaximumPayloadBytes)==e.title&&std::abs(real(q,metadata+3)-(e.date+archiveFoundationToUnix))<.00001&&std::abs(real(q,metadata+4)-(e.modified+archiveFoundationToUnix))<.00001&&integer(q,metadata+5)==static_cast<std::int64_t>(e.media.size()),"Archive document/index metadata disagrees; original preserved");
        if(v1){need(!e.categoryID,"Unexpected legacy Archive category");return e;}
        const auto stored=optional(q,metadata+6);if(e.categoryID!=stored){need(!e.categoryID&&stored==archiveLegacyCategory(e.type).id,"Archive category metadata disagrees");e.categoryID=stored;}
        const auto thumb=modules::thumbnail(q,metadata+7);need(thumb==(e.media.empty()?std::nullopt:std::optional(e.media.front())),"Archive thumbnail/index metadata disagrees");if(stored){Query category(db,"SELECT id FROM categories WHERE id=?");category.text(1,*stored);need(category.row(),"Archive document refers to missing category");}return e;
    }
    void insertCategory(const ArchiveCategory&c){Query q(db,"INSERT INTO categories(id,name,created) VALUES(?,?,?)");q.text(1,c.id);q.text(2,c.name);q.real(3,c.created+archiveFoundationToUnix);q.done();}
    void migrate(){execute(db,"BEGIN IMMEDIATE");try{
        std::set<ArchiveTemplate>templates;std::size_t count{};{Query q(db,"SELECT payload,id,template,title,date,modified,mediaCount FROM entries");while(q.row()){need(++count<=archiveMaximumEntries,"Legacy Archive is full");templates.insert(decoded(q.statement,0,1,true).type);}}(void)selection();
        execute(db,"CREATE TABLE categories(id TEXT PRIMARY KEY,name TEXT NOT NULL,created REAL NOT NULL)");execute(db,"ALTER TABLE entries ADD COLUMN categoryID TEXT REFERENCES categories(id)");execute(db,"ALTER TABLE entries ADD COLUMN thumbnail BLOB");for(const auto t:templates)insertCategory(archiveLegacyCategory(t));
        Query rows(db,"SELECT payload,id,template,title,date,modified,mediaCount FROM entries");while(rows.row()){const auto e=decoded(rows.statement,0,1,true);Query q(db,"UPDATE entries SET categoryID=?,thumbnail=? WHERE id=?");q.text(1,archiveLegacyCategory(e.type).id);q.blob(2,thumbnailBytes(e));q.text(3,e.id);q.done();}
        execute(db,"PRAGMA user_version=2");execute(db,"COMMIT");
    }catch(...){try{execute(db,"ROLLBACK");}catch(...){}throw;}}
    void open(){owner();if(db){unchanged();return;}detail::validateRoot(directory);detail::validateDataFile(path);std::error_code error;std::filesystem::create_directories(directory,error);if(error)throw StoreError(StoreErrorCode::unavailable,"Archive directory could not be created");
        const auto encoded=path.u8string();const std::string filename(reinterpret_cast<const char*>(encoded.data()),encoded.size());sqlite3*opened{};if(sqlite3_open_v2(filename.c_str(),&opened,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){if(opened)sqlite3_close_v2(opened);unavailable(nullptr);}db=opened;
        try{checked(sqlite3_busy_timeout(db,1200),db);sqlite3_limit(db,SQLITE_LIMIT_LENGTH,static_cast<int>(2*archiveMaximumPayloadBytes+thumbnailLimit+65536));
#ifdef _WIN32
            checked(sqlite3_enable_load_extension(db,0),db);
#endif
            {Query integrity(db,"PRAGMA quick_check(1)");need(integrity.row()&&bytes(integrity.statement,0,1024)=="ok","Invalid Archive SQLite; original preserved");}need(scalar(db,"SELECT count(*) FROM sqlite_master WHERE type='trigger'")==0,"Unexpected Archive SQL triggers");const auto version=scalar(db,"PRAGMA user_version");if(version>2)throw StoreError(StoreErrorCode::newerVersion,"Archive needs a newer app version");need(version>=0,"Invalid Archive schema version");
            if(version==0){need(scalar(db,"SELECT count(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'")==0,"Foreign SQLite database preserved");execute(db,"BEGIN IMMEDIATE");try{execute(db,"CREATE TABLE categories(id TEXT PRIMARY KEY,name TEXT NOT NULL,created REAL NOT NULL)");execute(db,"CREATE TABLE entries(id TEXT PRIMARY KEY,template TEXT NOT NULL,title TEXT NOT NULL,date REAL NOT NULL,modified REAL NOT NULL,mediaCount INTEGER NOT NULL,payload BLOB NOT NULL,categoryID TEXT REFERENCES categories(id),thumbnail BLOB)");execute(db,"CREATE TABLE state(key TEXT PRIMARY KEY,value TEXT NOT NULL)");execute(db,"PRAGMA user_version=2");execute(db,"COMMIT");}catch(...){try{execute(db,"ROLLBACK");}catch(...){}throw;}}
            else{{Query entries(db,"SELECT id,template,title,date,modified,mediaCount,payload FROM entries LIMIT 0");Query state(db,"SELECT key,value FROM state LIMIT 0");}if(version==1)migrate();else{Query categories(db,"SELECT id,name,created FROM categories LIMIT 0");Query columns(db,"SELECT categoryID,thumbnail FROM entries LIMIT 0");}}
            execute(db,"PRAGMA foreign_keys=ON");fileIdentity=identity(path);dataVersion=scalar(db,"PRAGMA data_version");
        }catch(...){sqlite3_close_v2(db);db=nullptr;throw;}
    }
    std::vector<ArchiveCategory>categories(){std::vector<ArchiveCategory>out;std::set<std::string,std::less<>>names,ids;std::size_t extraBytes{};Query q(db,"SELECT id,name,created FROM categories ORDER BY created,id LIMIT 101");while(q.row()){need(out.size()<archiveMaximumCategories,"Archive category limit exceeded");ArchiveCategory c{bytes(q.statement,0,36),bytes(q.statement,1,1024),real(q.statement,2)-archiveFoundationToUnix,{}};validateArchiveCategory(c,rules);need(ids.insert(c.id).second&&names.insert(rules.categoryNameKey(c.name)).second,"Repeated Archive category");
            // Additive category-only payloads fit the source state table without
            // inventing columns, changing version2 or affecting Mac selection.
            Query extras(db,"SELECT value FROM state WHERE key=?");extras.text(1,"category.extra/"+c.id);if(extras.row()){const auto raw=bytes(extras.statement,0,archiveMaximumSummaryThumbnailBytes);if(raw.size()>archiveMaximumSummaryThumbnailBytes-extraBytes)throw StoreError(StoreErrorCode::tooLarge,"Additive Archive category metadata exceeds bounded cache");extraBytes+=raw.size();auto j=J::parse(raw,archiveMaximumSummaryThumbnailBytes);need(j.isObject(),"Invalid additive Archive category payload");c.extra=j.object();}out.push_back(std::move(c));}return out;}
    void write(const ArchiveEntry&e){validateArchiveEntry(e,rules);if(e.categoryID){Query category(db,"SELECT id FROM categories WHERE id=?");category.text(1,*e.categoryID);need(category.row(),"Unknown Archive category");}{Query count(db,"SELECT count(*) FROM entries WHERE id!=?");count.text(1,e.id);need(count.row()&&integer(count.statement,0)<static_cast<std::int64_t>(archiveMaximumEntries),"Archive document limit exceeded");}
        const auto payload=encodeArchiveEntry(e,rules).encode(archiveMaximumPayloadBytes);Query q(db,"INSERT INTO entries(id,template,title,date,modified,mediaCount,payload,categoryID,thumbnail) VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET template=excluded.template,title=excluded.title,date=excluded.date,modified=excluded.modified,mediaCount=excluded.mediaCount,payload=excluded.payload,categoryID=excluded.categoryID,thumbnail=excluded.thumbnail");q.text(1,e.id);q.text(2,type(e.type));q.text(3,e.title);q.real(4,e.date+archiveFoundationToUnix);q.real(5,e.modified+archiveFoundationToUnix);q.integer(6,static_cast<std::int64_t>(e.media.size()));q.blob(7,payload);q.optional(8,e.categoryID);q.blob(9,thumbnailBytes(e));q.done();}
};
ArchiveSQLiteRepository::ArchiveSQLiteRepository(std::filesystem::path directory,ArchiveTextRules rules):impl_(std::make_unique<Impl>(std::move(directory),std::move(rules))){}
ArchiveSQLiteRepository::~ArchiveSQLiteRepository()=default;
const std::filesystem::path&ArchiveSQLiteRepository::path()const noexcept{return impl_->path;}
void ArchiveSQLiteRepository::reopen(){auto&i=*impl_;i.owner();if(i.db){checked(sqlite3_close(i.db),i.db);i.db=nullptr;}i.open();}
std::vector<ArchiveCategory>ArchiveSQLiteRepository::categories(){impl_->open();return impl_->categories();}
std::vector<ArchiveSummary>ArchiveSQLiteRepository::summaries(){auto&i=*impl_;i.open();std::set<std::string,std::less<>>categories;for(const auto&c:i.categories())categories.insert(c.id);std::vector<ArchiveSummary>out;std::size_t thumbBytes{};Query q(i.db,"SELECT id,template,title,date,mediaCount,categoryID,thumbnail FROM entries ORDER BY modified DESC LIMIT 2001");while(q.row()){need(out.size()<archiveMaximumEntries,"Archive document limit exceeded");const auto n=integer(q.statement,4);need(n>=0&&n<=16,"Invalid Archive summary attachment count");ArchiveSummary s{bytes(q.statement,0,36),type(bytes(q.statement,1,16)),bytes(q.statement,2,archiveMaximumPayloadBytes),real(q.statement,3)-archiveFoundationToUnix,static_cast<std::size_t>(n),optional(q.statement,5),modules::thumbnail(q.statement,6)};validateArchiveSummary(s,i.rules);need((s.mediaCount==0)==!s.thumbnail&&(!s.categoryID||categories.contains(*s.categoryID)),"Archive summary metadata disagrees");const auto cost=static_cast<std::size_t>(sqlite3_column_bytes(q.statement,6));if(cost>archiveMaximumSummaryThumbnailBytes-thumbBytes)s.thumbnail.reset();else thumbBytes+=cost;out.push_back(std::move(s));}return out;}
std::optional<ArchiveEntry>ArchiveSQLiteRepository::entry(std::string_view id){auto&i=*impl_;need(ehud::data::validUUID(id),"Invalid Archive entry UUID");i.open();Query q(i.db,"SELECT payload,id,template,title,date,modified,mediaCount,categoryID,thumbnail FROM entries WHERE id=?");q.text(1,id);if(!q.row())return {};return i.decoded(q.statement,0,1);}
std::optional<std::string>ArchiveSQLiteRepository::selection(){impl_->open();return impl_->selection();}
void ArchiveSQLiteRepository::select(const std::optional<std::string>&id){need(!id||ehud::data::validUUID(*id),"Invalid Archive selected UUID");auto&i=*impl_;i.open();i.transaction([&]{Query q(i.db,"INSERT OR REPLACE INTO state(key,value) VALUES('selection',?)");q.text(1,id.value_or(""));q.done();});}
void ArchiveSQLiteRepository::save(const ArchiveEntry&e){auto&i=*impl_;validateArchiveEntry(e,i.rules);i.open();i.transaction([&]{i.write(e);});}
void ArchiveSQLiteRepository::saveCategory(const ArchiveCategory&c){auto&i=*impl_;validateArchiveCategory(c,i.rules);const auto extra=c.extra.empty()?std::nullopt:std::optional(J(c.extra).encode(archiveMaximumSummaryThumbnailBytes));i.open();i.transaction([&]{const auto categories=i.categories();const auto found=std::find_if(categories.begin(),categories.end(),[&](const auto&v){return v.id==c.id;});if(found!=categories.end())need(found->name==c.name&&std::abs(found->created-c.created)<.00001,"Existing Archive category is immutable");else{need(categories.size()<archiveMaximumCategories,"Archive category limit exceeded");need(std::none_of(categories.begin(),categories.end(),[&](const auto&v){return i.rules.categoryNameKey(v.name)==i.rules.categoryNameKey(c.name);}),"Repeated Archive category name");i.insertCategory(c);}if(extra){Query total(i.db,"SELECT coalesce(sum(length(CAST(value AS BLOB))),0) FROM state WHERE key LIKE 'category.extra/%' AND key!=?");total.text(1,"category.extra/"+c.id);need(total.row(),"Missing additive Archive metadata total");const auto count=integer(total.statement,0);need(count>=0&&static_cast<std::uint64_t>(count)<=archiveMaximumSummaryThumbnailBytes-extra->size(),"Additive Archive category metadata exceeds bounded cache");Query q(i.db,"INSERT OR REPLACE INTO state(key,value) VALUES(?,?)");q.text(1,"category.extra/"+c.id);q.text(2,*extra);q.done();}});}
void ArchiveSQLiteRepository::deleteCategory(std::string_view id,std::span<const ArchiveEntry>drafts){need(ehud::data::validUUID(id)&&drafts.size()<=archiveMaximumEntries,"Invalid Archive category removal");auto&i=*impl_;std::set<std::string,std::less<>>ids;for(const auto&e:drafts){validateArchiveEntry(e,i.rules);need(e.categoryID!=id&&ids.insert(e.id).second,"Ambiguous category-removal draft");}i.open();i.transaction([&]{std::vector<std::string>affected;{Query names(i.db,"SELECT id FROM entries WHERE categoryID=? LIMIT 2001");names.text(1,id);while(names.row()){need(affected.size()<archiveMaximumEntries,"Archive category references exceed limit");affected.push_back(bytes(names.statement,0,36));}}for(const auto&document:affected){const auto preserved=std::find_if(drafts.begin(),drafts.end(),[&](const auto&e){return e.id==document;});if(preserved!=drafts.end())continue;ArchiveEntry entry;{Query q(i.db,"SELECT payload,id,template,title,date,modified,mediaCount,categoryID,thumbnail FROM entries WHERE id=?");q.text(1,document);need(q.row(),"Missing category-removal document");entry=i.decoded(q.statement,0,1);}entry.categoryID.reset();i.write(entry);}for(const auto&e:drafts)i.write(e);Query q(i.db,"DELETE FROM categories WHERE id=?");q.text(1,id);q.done();Query extra(i.db,"DELETE FROM state WHERE key=?");extra.text(1,"category.extra/"+std::string(id));extra.done();});}
void ArchiveSQLiteRepository::remove(std::string_view id){need(ehud::data::validUUID(id),"Invalid Archive removal UUID");auto&i=*impl_;i.open();i.transaction([&]{Query q(i.db,"DELETE FROM entries WHERE id=?");q.text(1,id);q.done();Query state(i.db,"DELETE FROM state WHERE key='selection' AND value=?");state.text(1,id);state.done();});}
std::optional<J>ArchiveSQLiteRepository::thumbnail(std::string_view id){need(ehud::data::validUUID(id),"Invalid Archive thumbnail UUID");auto&i=*impl_;i.open();Query q(i.db,"SELECT thumbnail FROM entries WHERE id=?");q.text(1,id);return q.row()?modules::thumbnail(q.statement,0):std::nullopt;}
} // namespace endfield::modules
