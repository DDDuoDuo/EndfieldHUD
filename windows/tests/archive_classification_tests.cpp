// Mac 1.3.0 Archive data contracts: the lazy classification/viewed schema and
// user_version 2 <-> 3 rule (Sources/ArchiveStore.swift:62-95, 361-485),
// editable category names and the NEW badge (:9-45, 166-224), the verifier
// record and password rules against the unchanged ArchiveClassification.swift
// (tests/fixtures/archive-classification-source.json from
// windows/tools/archive_classification_reference.py), the "password missing"
// path of an imported classified document, and the ArchiveState lock model
// (:643-987). Synthetic SQLite archives in temporary folders only; only the
// in-memory vault is used (no DPAPI file, no Keychain, no user data).
#include "modules/archive_repository.hpp"
#include "native/archive_text_rules.hpp"
#include "core/data/file_io.hpp"
#ifdef _WIN32
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#endif
#include <algorithm>
#include <cstdio>
#include <deque>
#include <iostream>

namespace {
namespace mod=endfield::modules;namespace data=ehud::data;using J=data::Json;
unsigned checks{};void check(bool v,const std::string&m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F&&f,const std::string&m,std::optional<data::StoreErrorCode>code={}){bool failure{};try{f();}catch(const data::StoreError&e){failure=!code||*code==e.code();}catch(const std::exception&){failure=!code;}check(failure,m);}
template<class F>std::optional<mod::ArchiveClassificationErrorCode>classificationError(F&&f){try{f();}catch(const mod::ArchiveClassificationError&e){return e.code();}return {};}
std::string id(char prefix,unsigned n){char text[64];std::snprintf(text,sizeof(text),"%c0000000-0000-4000-8000-%012u",prefix,n);return text;}
struct Temporary {std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-Archive-Classification-"+data::makeUUID());Temporary(){std::filesystem::create_directories(root);}~Temporary(){std::error_code e;std::filesystem::remove_all(root,e);}};
struct Database {
    sqlite3*db{};explicit Database(const std::filesystem::path&path){const auto u=path.u8string();const std::string s(reinterpret_cast<const char*>(u.data()),u.size());if(sqlite3_open_v2(s.c_str(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK)throw std::runtime_error("Synthetic SQLite open failed");}
    ~Database(){if(db)sqlite3_close(db);}Database(const Database&)=delete;
    void sql(const std::string&s){char*error{};const auto status=sqlite3_exec(db,s.c_str(),nullptr,nullptr,&error);std::string text=error?error:"";sqlite3_free(error);if(status!=SQLITE_OK)throw std::runtime_error("Synthetic SQL failed: "+text);}
    std::int64_t number(const std::string&s){sqlite3_stmt*q{};if(sqlite3_prepare_v2(db,s.c_str(),-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("Synthetic query failed: "+s);const bool row=sqlite3_step(q)==SQLITE_ROW;const auto value=row?sqlite3_column_int64(q,0):0;sqlite3_finalize(q);if(!row)throw std::runtime_error("Synthetic scalar missing");return value;}
    bool has(const std::string&s){sqlite3_stmt*q{};const bool ok=sqlite3_prepare_v2(db,s.c_str(),-1,&q,nullptr)==SQLITE_OK;if(q)sqlite3_finalize(q);return ok;}
};
mod::ArchiveEntry document(unsigned n){mod::ArchiveEntry e;e.id=id('B',n);e.title="档案 "+std::to_string(n);e.body="Plaintext body stays readable";e.date=1.5;e.modified=2.25;return e;}
// Windows 1.2.0 / Mac v1.2.0 version-2 schema (no classification, no viewed).
constexpr const char*v120Schema="CREATE TABLE categories(id TEXT PRIMARY KEY,name TEXT NOT NULL,created REAL NOT NULL);CREATE TABLE entries(id TEXT PRIMARY KEY,template TEXT NOT NULL,title TEXT NOT NULL,date REAL NOT NULL,modified REAL NOT NULL,mediaCount INTEGER NOT NULL,payload BLOB NOT NULL,categoryID TEXT REFERENCES categories(id),thumbnail BLOB);CREATE TABLE state(key TEXT PRIMARY KEY,value TEXT NOT NULL);PRAGMA user_version=2";
// Fixed-cost passwords keep the suite fast: production calibrates (>= 600000).
struct Vault:mod::ArchivePasswordVault {mod::ArchiveMemoryPasswordVault items;bool failRead{},failAdd{};unsigned adds{};
    std::optional<std::string>read(std::string_view a)override{if(failRead)throw std::runtime_error("Synthetic locked vault");return items.read(a);}
    void add(std::string_view a,std::string_view d)override{++adds;if(failAdd)throw std::runtime_error("Synthetic vault write failure");items.add(a,d);}
    void remove(std::string_view a)override{items.remove(a);}std::vector<std::string>accounts()override{return items.accounts();}};

void oracle(const std::filesystem::path&path){const auto rules=endfield::native::nativeArchiveTextRules();const auto fixture=J::parse(*data::detail::readFile(path,4*1024*1024),4*1024*1024);
    check(fixture["account"].string()==mod::archivePasswordAccount("b0000000-0000-4000-8000-000000000001","c0000000-0000-4000-8000-0000000000aa"),"Vault account is <DOC-UUID>/<KEY-UUID> uppercase");
    const auto&limits=fixture["limits"];check(limits["minimumIterations"].integer()==mod::ArchivePasswordVerifier::minimumIterations&&limits["maximumIterations"].integer()==mod::ArchivePasswordVerifier::maximumIterations&&
        limits["minimumLength"].integer()==long(mod::archivePasswordMinimumLength)&&limits["maximumBytes"].integer()==long(mod::archivePasswordMaximumBytes),"Verifier and password limits match the Mac");
    const auto issueName=[](std::optional<mod::ArchivePasswordIssue>v)->J{if(!v)return nullptr;return *v==mod::ArchivePasswordIssue::tooShort?"tooShort":*v==mod::ArchivePasswordIssue::tooLong?"tooLong":"mismatch";};
    for(const auto&row:fixture["issues"].array()){const auto confirmation=row["confirmation"].isString()?std::optional(row["confirmation"].string()):std::nullopt;
        check(issueName(mod::archivePasswordIssue(row["password"].string(),confirmation,rules))==row["issue"],"ArchivePassword.issue matches for "+row["password"].string().substr(0,16));}
    const auto salt=*mod::archiveBase64Decoded(fixture["salt"].string());
    for(const auto&row:fixture["derived"].array()){const auto record=mod::ArchivePasswordVerifier::decode(row["record"].string());check(record&&record->salt==salt&&record->iterations==600000,"The Mac verifier record decodes (Foundation escapes '/')");
        check(record->matches(rules.canonical(row["password"].string())),"The Mac record matches the NFC password (PBKDF2-HMAC-SHA256)");check(!record->matches(rules.canonical(row["password"].string()+"!")),"A different password does not match");
        check(mod::ArchivePasswordVerifier::decode(record->encoded())==record,"Windows re-encodes the same record");
        const auto key=mod::archivePBKDF2(rules.canonical(row["password"].string()),salt,600000,32);check(key&&*key==record->key,"Platform PBKDF2 derives the Mac key");}
    for(const auto&row:fixture["records"].array())check(mod::ArchivePasswordVerifier::decode(row["record"].string()).has_value()==row["accepted"].boolean(),"init?(data:) acceptance matches: "+row["record"].string().substr(0,90));
    check(fixture["duplicateRejected"].boolean(),"The Mac memory vault refuses a duplicate item");
    mod::ArchiveMemoryPasswordVault vault;vault.add("a","x");rejects([&]{vault.add("a","y");},"A duplicate add is an error like errSecDuplicateItem");vault.remove("absent");check(vault.read("a")==std::optional<std::string>("x"),"Remove of an absent item succeeds");
}
void vectors(){
    // RFC 7914 / widely published PBKDF2-HMAC-SHA256 vectors (P="password", S="salt").
    const auto hex=[](const std::string&bytes){static constexpr char d[]="0123456789abcdef";std::string out;for(const unsigned char c:bytes){out.push_back(d[c>>4]);out.push_back(d[c&15]);}return out;};
    check(hex(*mod::archivePBKDF2("password","salt",1,32))=="120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b","PBKDF2-HMAC-SHA256 c=1");
    check(hex(*mod::archivePBKDF2("password","salt",4096,32))=="c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a","PBKDF2-HMAC-SHA256 c=4096");
    const auto calibrated=mod::ArchivePasswordVerifier::calibratedIterations();check(calibrated>=600000&&calibrated<=10000000&&calibrated==mod::ArchivePasswordVerifier::calibratedIterations(),"Iterations are calibrated once and clamped to the OWASP floor");
    const auto made=mod::ArchivePasswordVerifier::make("correct horse",600000);check(made.valid()&&made.salt.size()==16&&made.key.size()==32&&made.matches("correct horse")&&!made.matches("correct horsf"),"A fresh verifier has a random 16-byte salt and a 32-byte key");
    check(mod::ArchivePasswordVerifier::make("correct horse",600000).salt!=made.salt,"Every verifier draws a new salt");
    const auto text=made.encoded();check(text.starts_with("{\"algorithm\":\"PBKDF2-HMAC-SHA256\",\"iterations\":600000,\"key\":")&&text.ends_with(",\"version\":1}"),"Sorted-key record layout like JSONEncoder .sortedKeys");
    rejects([&]{(void)mod::ArchivePasswordVerifier::make("pw",599999);},"Iterations below the floor are refused",data::StoreErrorCode::invalid);
}
void schema(){Temporary t;const auto rules=endfield::native::nativeArchiveTextRules();auto vault=std::make_shared<Vault>();
    // A Windows 1.2.0 / Mac v1.2.0 archive widens lazily and stays version 2.
    const auto folder=t.root/"Archive";std::filesystem::create_directories(folder);
    {Database db(folder/"archive.sqlite");db.sql(v120Schema);db.sql("INSERT INTO categories VALUES('"+id('A',1)+"','Field notes',5)");}
    {mod::ArchiveSQLiteRepository repo(folder,rules,vault);const auto cats=repo.categories();check(cats.size()==1&&!cats[0].isNew,"Existing categories read as seen (viewed DEFAULT 1)");
        auto a=document(1),b=document(2);repo.save(a);repo.save(b);
        {Database db(repo.path());check(db.number("PRAGMA user_version")==2&&db.has("SELECT classification FROM entries")&&db.has("SELECT viewed FROM categories"),"Opening adds both columns, version stays 2 (no classified document)");}
        // A v1.2.0-equivalent reader keeps working at version 2 (named columns).
        {Database db(repo.path());db.sql("INSERT INTO categories(id,name,created) VALUES('"+id('A',2)+"','v1.2.0 insert',6)");check(db.number("SELECT viewed FROM categories WHERE id='"+id('A',2)+"'")==1,"v1.2.0's three-column insert gets viewed=1");}
        repo.reopen();
        check(repo.classificationStatus(a.id)==mod::ArchiveClassificationStatus::unclassified&&repo.verify(a.id,"anything"),"An unclassified document verifies any password and never reads the vault");
        repo.classify(a.id,"correct horse");
        {Database db(repo.path());check(db.number("PRAGMA user_version")==3,"The first classified document sets version 3 in the same transaction");check(db.number("SELECT count(*) FROM entries WHERE classification IS NOT NULL")==1,"Only the key UUID is stored in the row");}
        const auto summaries=repo.summaries();check(std::count_if(summaries.begin(),summaries.end(),[](const auto&s){return s.isClassified;})==1,"Summaries carry the classified flag");
        check(repo.entry(a.id)->body==a.body&&repo.entry(a.id)->title==a.title,"Classified means locked, not encrypted: text unchanged");
        check(repo.classificationStatus(a.id)==mod::ArchiveClassificationStatus::protectedByPassword&&repo.verify(a.id,"correct horse")&&!repo.verify(a.id,"correct horsf"),"The right password verifies, a wrong one does not");
        check(repo.verify(a.id,"corre\xCC\x81"+std::string("ct horse"))==false,"Different canonical text is a different password");
        rejects([&]{repo.classify(a.id,"another one");},"Classifying an already classified document is refused",data::StoreErrorCode::invalid);
        rejects([&]{repo.classify(b.id,"abc");},"A three-Character password is refused",data::StoreErrorCode::invalid);
        // Document saves never touch the flag (upsert, not replace).
        auto edited=a;edited.body="Edited while unlocked";repo.save(edited);check(repo.isClassified(a.id),"A document save keeps the classification");
        repo.classify(b.id,"second password");repo.declassify(a.id);
        {Database db(repo.path());check(db.number("PRAGMA user_version")==3,"Still version 3 while another document is classified");}
        check(vault->accounts().size()==1,"Declassify removes the document's verifier after the commit");
        repo.declassify(a.id);check(!repo.isClassified(a.id),"Declassify is idempotent");
        repo.remove(b.id);
        {Database db(repo.path());check(db.number("PRAGMA user_version")==2,"Deleting the last classified document returns the file to version 2");}
        check(vault->accounts().empty(),"Deleting a classified document removes its verifier");
    }
    // Outside edits are repaired on open: version 3 without a classified row, version 2 with one.
    {Database db(folder/"archive.sqlite");db.sql("PRAGMA user_version=3");}
    {mod::ArchiveSQLiteRepository repo(folder,rules,vault);(void)repo.categories();Database db(repo.path());check(db.number("PRAGMA user_version")==2,"A stray version 3 without classified documents is repaired to 2");}
    {Database db(folder/"archive.sqlite");db.sql("UPDATE entries SET classification='"+id('C',9)+"' WHERE id='"+id('B',1)+"'");}
    {mod::ArchiveSQLiteRepository repo(folder,rules,vault);(void)repo.categories();Database db(repo.path());check(db.number("PRAGMA user_version")==3,"A classified row written at version 2 is repaired to 3");
        // The Mac "password missing" path (an imported archive: vault items never transfer).
        check(repo.classificationStatus(id('B',1))==mod::ArchiveClassificationStatus::passwordMissing,"A classified row without a verifier is passwordMissing");
        check(classificationError([&]{(void)repo.verify(id('B',1),"anything");})==mod::ArchiveClassificationErrorCode::passwordMissing,"Verification reports the missing verifier, never a wrong password");
        repo.declassify(id('B',9));check(!repo.isClassified(id('B',9)),"Declassifying an unknown document is a no-op, as on the Mac");
        repo.recoverClassification(id('B',1),std::string("new password"));check(repo.classificationStatus(id('B',1))==mod::ArchiveClassificationStatus::protectedByPassword&&repo.verify(id('B',1),"new password"),"Recovery with a new password keeps it classified under a new key");
        rejects([&]{repo.recoverClassification(id('B',1),std::nullopt);},"Recovery is refused while a readable verifier exists",data::StoreErrorCode::invalid);
        vault->failRead=true;check(repo.classificationStatus(id('B',1))==mod::ArchiveClassificationStatus::vaultUnavailable,"An unreadable vault is vaultUnavailable");vault->failRead=false;
        vault->items.remove(vault->accounts().front());repo.recoverClassification(id('B',1),std::nullopt);check(!repo.isClassified(id('B',1)),"Recovery without a password declassifies");Database after(repo.path());check(after.number("PRAGMA user_version")==2,"and returns the file to version 2");
        auto a=document(1);vault->failAdd=true;check(classificationError([&]{repo.classify(a.id,"correct horse");})==mod::ArchiveClassificationErrorCode::vaultUnavailable,"A failed vault write fails the classification");vault->failAdd=false;
        check(!repo.isClassified(a.id)&&Database(repo.path()).number("PRAGMA user_version")==2,"A failed classification rolls back the flag and the version");}
    {mod::ArchiveSQLiteRepository noVault(folder,rules);check(classificationError([&]{noVault.classify(id('B',1),"correct horse");})==mod::ArchiveClassificationErrorCode::vaultUnavailable,"Without a vault nothing can be classified");}
    // Malformed new columns invalidate the archive (the Mac's strict reads).
    {Database db(folder/"archive.sqlite");db.sql("UPDATE entries SET classification='not-a-uuid' WHERE id='"+id('B',1)+"'");}
    {mod::ArchiveSQLiteRepository repo(folder,rules,vault);rejects([&]{(void)repo.summaries();},"A non-UUID classification key is invalid",data::StoreErrorCode::invalid);repo.remove(id('B',1));check(repo.summaries().empty(),"A malformed flag never blocks deletion");}
    {Database db(folder/"archive.sqlite");db.sql("UPDATE categories SET viewed=2");}
    {mod::ArchiveSQLiteRepository repo(folder,rules,vault);rejects([&]{(void)repo.categories();},"A viewed value other than 0/1 is invalid",data::StoreErrorCode::invalid);}
    // Version 3 requires both columns; version 4 is newer.
    const auto broken=t.root/"broken";std::filesystem::create_directories(broken);{Database db(broken/"archive.sqlite");db.sql(v120Schema);db.sql("PRAGMA user_version=3");}
    {mod::ArchiveSQLiteRepository repo(broken,rules,vault);rejects([&]{(void)repo.categories();},"Version 3 without the 1.3.0 columns is refused");Database db(broken/"archive.sqlite");check(db.number("PRAGMA user_version")==3&&!db.has("SELECT viewed FROM categories"),"and left untouched");}
}
void categories(){Temporary t;const auto rules=endfield::native::nativeArchiveTextRules();mod::ArchiveSQLiteRepository repo(t.root/"Archive",rules);
    mod::ArchiveCategory fresh{id('A',1),"Category 1",1,{},true},seen{id('A',2),"Old name that is longer than twenty-four characters",2,{},false};repo.saveCategory(fresh);repo.saveCategory(seen);
    auto cats=repo.categories();check(cats[0].isNew&&!cats[1].isNew&&cats[1].name.size()>24,"The NEW badge is stored as viewed=0; names up to 80 Characters still load");
    check(!mod::encodeArchiveCategory(cats[0],rules).contains("isNew"),"The badge never enters a category's JSON form");
    repo.markCategoryViewed(fresh.id);repo.markCategoryViewed(id('A',9));check(!repo.categories()[0].isNew,"Opening clears the badge; unknown ids are ignored");
    using I=mod::ArchiveCategoryNameIssue;const auto issue=[&](std::string_view n,std::optional<std::string>ex={}){return mod::archiveCategoryNameIssue(n,repo.categories(),rules,ex);};
    check(issue("  ")==I::empty&&issue(std::string(25,'x'))==I::tooLong&&!issue(std::string(24,'x'))&&issue("Tab\tname")==I::invalidCharacters&&issue("Line\xE2\x80\xA8sep")==I::invalidCharacters,"Empty, over 24 Characters, control and separator characters are refused");
    check(issue("全部")==I::reserved&&issue(" uncategorized ")==I::reserved&&issue("미분류")==I::reserved&&issue("すべて")==I::reserved&&issue("未分類")==I::reserved,"All and Uncategorized are reserved in every language");
    check(issue("category 1")==I::duplicate&&!issue("CATEGORY 1",fresh.id),"Duplicates by name key, except the category being renamed");
    check(issue("👩🏽‍💻👩🏽‍💻👩🏽‍💻👩🏽‍💻👩🏽‍💻👩🏽‍💻👩🏽‍💻👩🏽‍💻👩🏽‍💻👩🏽‍💻👩🏽‍💻👩🏽‍💻")==std::nullopt,"Length counts Characters, not bytes or UTF-16");
    repo.renameCategory(fresh.id,"  Field notes  ");check(repo.categories()[0].name=="Field notes","Renames are trimmed and stored in place");repo.renameCategory(fresh.id,"Field notes");
    rejects([&]{repo.renameCategory(fresh.id,"全部");},"A reserved rename is refused",data::StoreErrorCode::invalid);rejects([&]{repo.renameCategory(id('A',7),"Ghost");},"Unknown categories are refused",data::StoreErrorCode::invalid);
    repo.renameCategory(seen.id,seen.name);check(repo.categories()[1].name==seen.name,"Repeating the unchanged 80-Character name is a no-op");
    rejects([&]{repo.saveCategory({fresh.id,"Changed by save",1,{},false});},"saveCategory still never renames",data::StoreErrorCode::invalid);
}
struct Queue {
    struct Job{std::function<void()>work;std::function<void(std::exception_ptr)>done;};std::deque<Job>jobs;
    mod::ArchiveExecutor executor(){return {[this](auto work,auto done){jobs.push_back({std::move(work),std::move(done)});return true;}};}
    void drain(){while(!jobs.empty()){auto job=std::move(jobs.front());jobs.pop_front();std::exception_ptr error;try{job.work();}catch(...){error=std::current_exception();}job.done(error);}}
};
void lockModel(){Temporary t;const auto rules=endfield::native::nativeArchiveTextRules();auto vault=std::make_shared<Vault>();auto repo=std::make_shared<mod::ArchiveSQLiteRepository>(t.root/"Archive",rules,vault);
    Queue queue;mod::ArchiveState state(repo,queue.executor(),rules);state.activate();queue.drain();
    check(state.create(id('B',1),10),"Create a document");queue.drain();auto e=*state.selected();e.body="Secret plans";check(state.update(e,11,0),"Edit it");
    std::exception_ptr outcome=std::make_exception_ptr(std::runtime_error("pending"));state.classifySelected("correct horse",[&](auto f){outcome=f;});queue.drain();
    check(!outcome&&state.isClassified(e.id)&&state.isSelectedLocked()&&repo->entry(e.id)->body=="Secret plans","Classify saves the latest text first, then seals the document");
    check(state.entries().front().isClassified&&!state.entries().front().thumbnail,"The classified tile withholds its thumbnail");
    auto locked=*state.selected();locked.body="Typed while locked";check(!state.update(locked,12,1)&&!state.moveSelected(std::nullopt,12,1)&&!state.deleteSelected(),"Locked documents reject edits, moves and deletion");
    mod::ArchiveUnlockResult result{};state.unlockSelected("wrong password",[&](auto r,auto){result=r;});queue.drain();check(result==mod::ArchiveUnlockResult::wrongPassword&&state.isSelectedLocked(),"A wrong password keeps it locked");
    state.unlockSelected("correct horse",[&](auto r,auto){result=r;});queue.drain();check(result==mod::ArchiveUnlockResult::unlocked&&!state.isSelectedLocked()&&state.update(locked,12,1),"The right password unlocks this visit");
    state.lockSelected();check(state.isSelectedLocked(),"lockSelected relocks in place");
    state.unlockSelected("correct horse",[&](auto r,auto){result=r;});queue.drain();state.select(std::nullopt);queue.drain();state.select(e.id);queue.drain();check(state.isSelectedLocked(),"Leaving the document locks it again");
    state.unlockSelected("correct horse",[&](auto r,auto){result=r;});queue.drain();state.deactivate();state.activate();queue.drain();state.select(e.id);queue.drain();check(state.isSelectedLocked(),"Hiding the Archive locks it again");
    state.declassifySelected([&](auto f){outcome=f;});check(bool(outcome),"Declassify is refused while locked");
    state.unlockSelected("correct horse",[&](auto r,auto){result=r;});queue.drain();state.declassifySelected([&](auto f){outcome=f;});queue.drain();check(!outcome&&!state.isClassified(e.id),"Declassify while unlocked");
    auto events=state.takeEvents();check(std::any_of(events.begin(),events.end(),[](const auto&v){return v.name=="classified";})&&std::any_of(events.begin(),events.end(),[](const auto&v){return v.name=="declassified";}),"classified/declassified events");
    // An imported classified document (no verifier here): passwordMissing, owner-authenticated recovery.
    state.classifySelected("correct horse",[&](auto f){outcome=f;});queue.drain();for(const auto&account:vault->accounts())vault->items.remove(account);
    state.deactivate();state.activate();queue.drain();state.select(e.id);queue.drain();
    check(state.isSelectedLocked()&&state.selectedClassification()==mod::ArchiveClassificationStatus::passwordMissing,"A missing verifier opens locked as passwordMissing");
    state.unlockSelected("correct horse",[&](auto r,auto){result=r;});queue.drain();check(result==mod::ArchiveUnlockResult::passwordMissing&&state.isSelectedLocked(),"Unlocking reports passwordMissing and stays classified");
    state.recoverSelectedClassification(std::string("new password"),"restore access to a classified document",[&](auto f){outcome=f;});check(bool(outcome),"Recovery needs an owner authentication hook");
    bool approve=false;std::string reason;state.setOwnerAuthentication([&](std::string r,std::function<void(bool)>done){reason=std::move(r);done(approve);});
    state.recoverSelectedClassification(std::string("new password"),"restore access to a classified document",[&](auto f){outcome=f;});queue.drain();
    check(classificationError([&]{if(outcome)std::rethrow_exception(outcome);})==mod::ArchiveClassificationErrorCode::cancelled&&reason=="restore access to a classified document","A denied owner proof cancels the recovery");
    approve=true;state.recoverSelectedClassification(std::string("new password"),"restore access to a classified document",[&](auto f){outcome=f;});queue.drain();
    check(!outcome&&!state.isSelectedLocked()&&state.selectedClassification()==mod::ArchiveClassificationStatus::protectedByPassword&&repo->verify(e.id,"new password"),"Recovery with a new password unlocks this visit");
    // Categories: "+" creates Category N with the NEW badge; renames and badge clears persist in order.
    const auto added=state.addCategory(id('A',1),20,endfield::core::Language::english);const auto zh=state.nextCategoryName(endfield::core::Language::simplifiedChinese);queue.drain();
    check(added&&state.categories().front().name=="Category 1"&&state.categories().front().isNew&&repo->categories().front().isNew&&zh=="分类 2","The + action makes Category N with the NEW badge");
    check(state.renameCategory(*added,"全部").result==mod::ArchiveCategoryRenameResult::rejected&&state.renameCategory(*added,"Category 1").result==mod::ArchiveCategoryRenameResult::unchanged,"Rename validation in memory");
    check(state.renameCategory(*added,"Research").result==mod::ArchiveCategoryRenameResult::renamed&&state.categories().front().name=="Research"&&state.hasUnsavedChanges(),"A rename applies at once and is pending");
    state.markCategoryViewed(*added);queue.drain();check(repo->categories().front().name=="Research"&&!repo->categories().front().isNew&&!state.hasUnsavedChanges(),"Rename and badge clear reach the archive");
    events=state.takeEvents();check(std::any_of(events.begin(),events.end(),[](const auto&v){return v.name=="categoryRenamed";}),"categoryRenamed event");
}
}
int main(int argc,char**argv){try{if(argc!=2)throw std::runtime_error("Usage: archive_classification_tests archive-classification-source.json");oracle(argv[1]);vectors();schema();categories();lockModel();std::cout<<"PASS "<<checks<<" Archive 1.3.0 classification/schema/category contracts\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
