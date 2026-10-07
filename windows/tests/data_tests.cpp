#include "core/data/data_store.hpp"
#ifdef _WIN32
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#endif
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>

using namespace ehud::data;
namespace {
int checks{};
void check(bool condition,const char* label) {++checks;if(!condition) throw std::runtime_error(label);}
void rejects(StoreErrorCode code,const std::function<void()>& operation,const char* label) {
    try {operation();} catch(const StoreError& error) {check(error.code()==code,label);return;}throw std::runtime_error(label);
}
void rejectsJson(const std::function<void()>& operation,const char* label) {
    try {operation();}catch(const std::invalid_argument&) {++checks;return;}throw std::runtime_error(label);
}
struct Temporary {
    std::filesystem::path path=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-data-fixture-"+makeUUID());
    Temporary(){std::filesystem::create_directories(path);}
    ~Temporary(){std::error_code error;std::filesystem::remove_all(path,error);}
};
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path,const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));if(!file) throw std::runtime_error("Fixture write failed");
}
struct Database {
    sqlite3* value{};
    explicit Database(const std::filesystem::path& path) {const auto p=path.u8string();if(sqlite3_open(reinterpret_cast<const char*>(p.c_str()),&value)!=SQLITE_OK) throw std::runtime_error("Fixture database failed");}
    ~Database(){sqlite3_close(value);}
    void sql(const char* sql) {if(sqlite3_exec(value,sql,nullptr,nullptr,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(value));}
    std::string field(const char* sql) {
        sqlite3_stmt* statement{};if(sqlite3_prepare_v2(value,sql,-1,&statement,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(value));
        if(sqlite3_step(statement)!=SQLITE_ROW) {sqlite3_finalize(statement);throw std::runtime_error("Fixture row missing");}
        const auto* bytes=sqlite3_column_text(statement,0);std::string result(reinterpret_cast<const char*>(bytes),static_cast<std::size_t>(sqlite3_column_bytes(statement,0)));sqlite3_finalize(statement);return result;
    }
};
void jsonTests() {
    const std::string fixture=R"({"large":18446744073709551615,"date":812345678.12500001,"text":"\uD83D\uDE00简体繁體日本한국","null":null,"array":[true,false,1.0]})";
    const auto value=Json::parse(fixture);const auto encoded=value.encode();
    check(encoded.find("18446744073709551615")!=std::string::npos,"Large raw integer token remains lossless");
    check(encoded.find("812345678.12500001")!=std::string::npos,"Foundation epoch token remains lossless");
    check(value["text"].string()=="😀简体繁體日本한국","Surrogate pair and international text decode");
    check(Json::parse(encoded)==value,"Lossless JSON value roundtrip");
    check(Json::parse("9223372036854775807").integer()==std::numeric_limits<std::int64_t>::max(),"Signed integer boundary exact");
    rejectsJson([]{(void)Json::parse("{\"id\":1,\"id\":2}");},"Duplicate keys rejected");
    rejectsJson([]{(void)Json::parse("\"\\uD800\"");},"Unpaired surrogate rejected");
    rejectsJson([]{(void)Json::parse("[01]");},"Leading numeric zero rejected");
    rejectsJson([]{(void)Json::parse("1e999");},"Nonfinite numeric token rejected");
    rejectsJson([]{(void)Json::parse("null trailing");},"Trailing content rejected");
    rejectsJson([]{(void)Json::parse(std::string(66,'[')+"0"+std::string(66,']'));},"Excessive nesting rejected");
    rejectsJson([]{(void)Json::parse("\"oversized\"",2);},"Read budget enforced");
    rejectsJson([]{(void)Json("oversized").encode(2);},"Write budget enforced");
    check(!Json::validUtf8(std::string("\xc0\xaf",2)),"Overlong UTF-8 rejected");
    check(!Json::validUtf8(std::string("\xed\xa0\x80",3)),"UTF-8 surrogate rejected");
    rejectsJson([]{(void)Json(std::numeric_limits<double>::quiet_NaN());},"NaN cannot be encoded");
}
void settingsTests() {
    Temporary temporary;SettingsStore store(temporary.path);
    check(store.value().number("blurAmount")==.75,"Mac blur default");check(store.value().number("backgroundDarkness")==.63,"Mac brightness 37 percent default");
    check(store.value().string("accentHex")=="FAD41F","Mac accent default");
    auto settings=store.value();settings.set("language","korean");settings.set("futureSetting",Json::parse("18446744073709551615"));
    check(store.update(settings),"Settings update persists");const auto stable=read(store.path());
    check(!store.update(store.value())&&read(store.path())==stable,"Unchanged settings avoid writes");
    auto envelope=Json::parse(stable);envelope["futureEnvelope"]="preserve";write(store.path(),envelope.encode());
    SettingsStore imported(temporary.path);auto changed=imported.value();changed.set("theme","light");imported.update(changed);
    const auto roundtrip=Json::parse(read(store.path()));check(roundtrip["futureEnvelope"].string()=="preserve","Unknown settings envelope field preserved");
    check(roundtrip["settings"]["futureSetting"].encode()=="18446744073709551615","Unknown settings numeric field preserved");
    SettingsStore other(temporary.path);changed=imported.value();changed.set("theme","dark");imported.update(changed);
    auto stale=other.value();stale.set("language","english");rejects(StoreErrorCode::changedOnDisk,[&]{other.update(stale);},"Concurrent settings edit rejected");
    check(Json::parse(read(store.path()))["settings"]["theme"].string()=="dark","Concurrent failure preserves latest settings");
    changed=imported.value();changed.set("blurAmount",2);rejects(StoreErrorCode::invalid,[&]{imported.update(changed);},"Invalid settings mutation rejected before save");
    envelope=Json::parse(read(store.path()));envelope["version"]=2;const auto future=envelope.encode();write(store.path(),future);
    rejects(StoreErrorCode::newerVersion,[&]{SettingsStore futureStore(temporary.path);},"Future settings version rejected");check(read(store.path())==future,"Future settings preserved");
    rejects(StoreErrorCode::invalid,[]{SettingsStore relative("relative-root");},"Relative default data root rejected");
}
void profileTests() {
    Temporary temporary;ProfileStore store(temporary.path);const auto uid=store.value().uid;
    check(uid.size()==10&&std::filesystem::exists(store.path()),"Local identity saved at first creation");
    check(!store.update(store.value()),"Unchanged profile update skips write");
    auto profile=store.value();profile.name="管理員😀";profile.tag="1234";profile.introduction="First line\nSecond line";
    profile.avatarZoom=20;profile.themeColorHex="A1B2C3";profile.gamePlayerID="9007199254740993";store.update(profile);
    ProfileStore restored(temporary.path);check(restored.value().uid==uid&&restored.value().displayedUID()=="9007199254740993","Original and canonical game UID remain distinct strings");
    check(restored.value().avatarZoom==20&&restored.value().name==profile.name,"Profile appearance and Unicode persist");
    auto bytes=Json::parse(read(store.path()));bytes["futureEnvelope"]="opaque";bytes["profile"]["futureField"]=Json::parse("18446744073709551615");
    bytes["profile"]["awakeningDate"]=Json::parse("812345678.12500001");write(store.path(),bytes.encode());
    ProfileStore imported(temporary.path);profile=imported.value();profile.introduction="Edited";imported.update(profile);const auto edited=Json::parse(read(store.path()));
    check(edited["profile"]["awakeningDate"].encode()=="812345678.12500001","Unchanged imported date token retains original precision");
    check(edited["profile"]["futureField"].encode()=="18446744073709551615"&&edited["futureEnvelope"].string()=="opaque","Unknown profile/envelope fields preserved");
    profile=imported.value();profile.uid="1234567890";rejects(StoreErrorCode::invalid,[&]{imported.update(profile);},"Original local UID cannot be replaced");
    imported.setProfileSyncLocked(true);profile=imported.value();profile.name="Manual";
    rejects(StoreErrorCode::invalid,[&]{imported.update(profile);},"Synced profile name locked");
    profile=imported.value();profile.playerIDOverride="custom";rejects(StoreErrorCode::invalid,[&]{imported.update(profile);},"Synced UID override locked");
    profile=imported.value();profile.awakeningDate+=86400;rejects(StoreErrorCode::invalid,[&]{imported.update(profile);},"Synced awakening date locked");
    profile=imported.value();profile.birthdayMonth=2;profile.birthdayDay=29;profile.introduction="Still editable";
    check(imported.update(profile),"Birthday and introduction remain editable while synced");
    profile=imported.value();profile.name="Official";profile.tag="5678";profile.gamePlayerID="1234567890123456789";profile.playerIDOverride.reset();
    check(imported.updateFromGame(profile),"Official profile sync bypasses local field lock");check(imported.value().uid==uid,"Official sync preserves original local UID");
    imported.setProfileSyncLocked(false);profile=imported.value();profile.name="Local again";check(imported.update(profile),"Disabling sync unlocks without restoring old fields");
    ProfileStore concurrent(temporary.path);profile=imported.value();profile.introduction="Latest";imported.update(profile);
    auto stale=concurrent.value();stale.introduction="Stale";rejects(StoreErrorCode::changedOnDisk,[&]{concurrent.update(stale);},"Concurrent profile edit rejected");
    profile=imported.value();profile.avatarFilename="../../escape.png";rejects(StoreErrorCode::invalid,[&]{imported.update(profile);},"Managed image path traversal rejected");
    const auto avatarName=makeUUID()+".image";check(imported.imagePath(avatarName)==temporary.path/"Profile"/"Images"/avatarName,"Managed image path scoped to profile Images");
}
void notesTests() {
    Temporary temporary;NotesStore store(temporary.path);check(store.notes().empty(),"Fresh notes start empty");
    Note note=Note::textNote("A😀Z");note.createdAt=812345678.12500001;note.isPinned=true;note.x=140;note.zIndex=5;
    note.richText=R"({ "version":1,"future":18446744073709551615,"runs":[{"location":1,"length":2,"style":{"fontSize":12,"bold":true,"italic":false,"underline":false,"strikethrough":false}}]})";
    check(store.upsert(note),"Create formatted text note");check(!store.upsert(store.notes().front()),"Unchanged note skips transaction");
    Note todo=Note::todoNote();todo.zIndex=1;ChecklistItem item;item.text="Task";item.isChecked=true;item.originalFields["futureItem"]="retain";todo.items.push_back(item);store.upsert(todo);
    check(store.notes().front().id==todo.id,"Notes sorted by persisted stacking order");
    Note drawing;drawing.kind=NoteKind::drawing;drawing.zIndex=2;
    drawing.drawing=R"({"version":1,"future":"keep","strokes":[{"width":5,"color":{"red":1,"green":0.5,"blue":0,"alpha":1},"points":[{"x":0.2,"y":0.4}]}]})";store.upsert(drawing);
    Note media;media.kind=NoteKind::image;media.zIndex=3;
    media.media=R"({"version":1,"kind":"image","bookmark":"QUJD","isSecurityScoped":true,"lastKnownPath":"/unavailable/copied.png","displayName":"Copied","pixelWidth":65536,"pixelHeight":1,"duration":null,"frameCount":1,"future":"keep"})";store.upsert(media);
    {
        NotesStore loaded(temporary.path);check(loaded.notes().size()==4,"All four note types reload");
        const auto& recovered=loaded.notes().back();check(recovered.createdAt==note.createdAt,"Foundation SQL epoch preserves exact double");
        check(recovered.richText==note.richText&&recovered.isPinned,"Raw formatting payload and pin roundtrip");
        check(loaded.notes().front().items.front().originalFields["futureItem"].string()=="retain","Unknown checklist fields roundtrip");
        auto changed=loaded.notes()[1];check(changed.kind==NoteKind::drawing,"Drawing decoded from legacy text SQL base");changed.x+=10;loaded.upsert(changed);
        Database fixture(store.path());check(fixture.field("SELECT kind FROM notes WHERE drawing IS NOT NULL")=="text","Drawing persists with exact original SQL kind");
        check(fixture.field("SELECT drawing FROM notes WHERE drawing IS NOT NULL")==*drawing.drawing,"Geometry edit preserves drawing payload byte-for-byte");
        check(fixture.field("PRAGMA user_version")=="2","Exact Notes schema version");
    }
    auto stale=note;stale.text="stale";stale.richText.reset();rejects(StoreErrorCode::changedOnDisk,[&]{store.upsert(stale);},"Concurrent Notes edit refused");
    {
        NotesStore loaded(temporary.path);auto changed=loaded.notes().front();changed.items.front().isChecked=false;loaded.upsert(changed);
        check(loaded.remove(media.id)&&!loaded.remove(media.id),"Delete note idempotently");
        changed=loaded.notes().back();changed.richText=R"({"version":1,"runs":[{"location":10,"length":2,"style":{"fontSize":12,"bold":false,"italic":false,"underline":false,"strikethrough":false}}]})";
        rejects(StoreErrorCode::invalid,[&]{loaded.upsert(changed);},"Out-of-range UTF-16 formatting rejected");
        changed=loaded.notes().back();changed.width=-1;changed.x=std::numeric_limits<double>::quiet_NaN();loaded.upsert(changed);
        check(loaded.notes().back().width==110&&loaded.notes().back().x==24,"Mac geometry normalization reproduced");
    }
    {
        Database fixture(store.path());fixture.sql("ALTER TABLE notes ADD COLUMN future_column TEXT");fixture.sql("UPDATE notes SET future_column='opaque'");
    }
    {
        NotesStore loaded(temporary.path);auto changed=loaded.notes().front();changed.y+=1;loaded.upsert(changed);
        Database fixture(store.path());check(fixture.field("SELECT future_column FROM notes LIMIT 1")=="opaque","Unknown additive SQLite column survives edits");
    }
    {
        Database fixture(store.path());fixture.sql("PRAGMA user_version=3");
    }
    const auto futureBytes=read(store.path());rejects(StoreErrorCode::newerVersion,[&]{NotesStore future(temporary.path);},"Future Notes schema rejected");
    check(read(store.path())==futureBytes,"Future Notes database preserved byte-for-byte");
}
void migrationTests() {
    Temporary temporary;const auto path=temporary.path/"Notes"/"notes.sqlite3";std::filesystem::create_directories(path.parent_path());
    {
        Database fixture(path);fixture.sql("CREATE TABLE notes(id TEXT PRIMARY KEY NOT NULL,kind TEXT NOT NULL CHECK(kind IN ('text','todo','image')),text TEXT NOT NULL,checklist TEXT NOT NULL,image_name TEXT,x REAL NOT NULL,y REAL NOT NULL,width REAL NOT NULL,height REAL NOT NULL,z_index INTEGER NOT NULL,created_at REAL NOT NULL,is_pinned INTEGER NOT NULL CHECK(is_pinned IN (0,1)))");
        fixture.sql("PRAGMA user_version=1");fixture.sql("INSERT INTO notes VALUES('00000000-0000-4000-8000-000000000001','text','Original','[]',NULL,24,50,160,110,0,812345678.125,1)");
    }
    {NotesStore migrated(temporary.path);check(migrated.notes().size()==1&&migrated.notes().front().text=="Original"&&migrated.notes().front().isPinned,"Validated v1 migration preserves data");}
    {Database fixture(path);check(fixture.field("PRAGMA user_version")=="2","Successful migration stamps v2 only after transaction");}
    Temporary invalid;const auto foreign=invalid.path/"Notes"/"notes.sqlite3";std::filesystem::create_directories(foreign.parent_path());
    {Database fixture(foreign);fixture.sql("CREATE TABLE unrelated(secret TEXT)");fixture.sql("INSERT INTO unrelated VALUES('preserve')");}
    const auto before=read(foreign);rejects(StoreErrorCode::invalid,[&]{NotesStore rejected(invalid.path);},"Foreign v0 database rejected");check(read(foreign)==before,"Foreign database remains unchanged");
    Temporary symlink;Temporary outside;
    std::error_code error;std::filesystem::create_directory_symlink(outside.path,symlink.path/"Profile",error);
    if(!error) {rejects(StoreErrorCode::invalid,[&]{ProfileStore rejected(symlink.path);},"Profile symlink root rejected");check(!std::filesystem::exists(outside.path/"profile.json"),"No writes through a symlink");}
}
void nativeReferenceTests() {
    check(validWindowsFilePath(R"(C:\Users\Synthetic\照片.png)"),"Absolute Unicode Windows drive path accepted without file access");
    check(validWindowsFilePath(R"(\\synthetic-server\share\video.mp4)"),"Absolute UNC file reference accepted");
    check(!validWindowsFilePath("C:relative.png")&&!validWindowsFilePath("relative.png"),"Relative Windows paths rejected");
    check(!validWindowsFilePath(R"(\\.\PhysicalDrive0)")&&!validWindowsFilePath(R"(C:\NUL.png)"),"Windows device paths rejected");
    check(!validWindowsFilePath(R"(C:\folder\..\file.png)")&&!validWindowsFilePath(R"(\\server\share)"),"Ambiguous dot and share-only references rejected");
    const auto native=makeWindowsMediaReference(R"(C:\Users\Synthetic\照片.png)","照片",640,480);
    const auto descriptor=Json::parse(native);check(descriptor["version"].integer()==1&&descriptor["referencePlatform"].string()=="windows","Windows reference is explicit additive v1");
    check(!descriptor.contains("bookmark")&&!descriptor.contains("lastKnownPath")&&!descriptor.contains("isSecurityScoped"),"No fabricated Mac bookmark or scope");
    Temporary temporary;NotesStore store(temporary.path);Note image;image.kind=NoteKind::image;image.media=native;store.upsert(image);
    {NotesStore loaded(temporary.path);check(loaded.notes().front().media==native,"Native Windows image reference persists byte-for-byte");}
    auto ambiguous=descriptor;ambiguous["bookmark"]="QUJD";image.media=ambiguous.encode();
    rejects(StoreErrorCode::invalid,[&]{store.upsert(image);},"Mixed Windows and Mac locators rejected");
    Note video;video.kind=NoteKind::image;video.media=makeWindowsMediaReference(R"(\\synthetic-server\share\video.mp4)","Video",1280,720,"video",60);store.upsert(video);
    check(store.notes().size()==2,"Native video reference supported without reading media");
    rejects(StoreErrorCode::invalid,[]{(void)makeWindowsMediaReference("relative.png","Bad",640,480);},"Factory validates absolute path");
    rejects(StoreErrorCode::invalid,[]{(void)makeWindowsMediaReference(R"(C:\Synthetic\video.mp4)","Video",1280,720,"video");},"Native video requires source-compatible duration");
}
}
int main() {
    try {jsonTests();settingsTests();profileTests();notesTests();migrationTests();nativeReferenceTests();std::cout<<checks<<" data assertions passed; temporary fixtures only\n";return 0;}
    catch(const std::exception& error) {std::cerr<<"Data fixture failure: "<<error.what()<<"\n";return 1;}
}
