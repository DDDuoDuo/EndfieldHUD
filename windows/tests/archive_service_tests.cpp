#include "app/archive_service.hpp"
#include "native/archive_text_rules.hpp"
#include "core/data/file_io.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <condition_variable>
#include <mutex>
#ifdef _WIN32
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#endif
namespace fs=std::filesystem;namespace app=endfield::app;namespace mod=endfield::modules;namespace data=ehud::data;
namespace {unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}struct Temp{fs::path root=fs::temp_directory_path()/("EndfieldHUD-Archive-Service-"+data::makeUUID());Temp(){fs::create_directory(root);}~Temp(){std::error_code ec;fs::remove_all(root,ec);}};
void drain(app::UtilityExecutor&e,app::ArchiveService&s){for(unsigned n=0;n<20;++n){e.waitIdle();e.drain();s.queueCapacityAvailable();if(!s.state().busy())return;}throw std::runtime_error("Shared Archive queue failed to settle");}
void run(){Temp temporary;const auto root=fs::canonical(temporary.root);app::UtilityExecutor executor([]{});unsigned changes{};const auto rules=endfield::native::nativeArchiveTextRules();const auto id=data::makeUUID();
 {app::ArchiveService service(root/"Archive",executor,rules,[&]{++changes;});check(!fs::exists(root/"Archive")&&!executor.stats().started,"Construction performs no I/O or worker start");service.state().activate();drain(executor,service);check(!service.state().error()&&service.state().entries().empty()&&changes>0,"Existing FIFO loads an isolated empty Archive");check(service.state().create(id,812000000.),"Create through retained source state");drain(executor,service);auto entry=*service.state().selected();entry.title="中文 Archive";entry.body="Retained body";check(service.state().update(entry,812000001.,1),"Update keeps source draft");check(service.state().hasUnsavedChanges(),"Debounced change stays pending before flush");check(service.flush()&&!service.state().hasUnsavedChanges(),"Shutdown boundary saves dirty state on shared worker");check(fs::exists(root/"Archive"/"archive.sqlite"),"Only explicit root contains database");
 // A separate connection owns only this fixture database. Its write lock
 // simulates transient unavailability without bypassing Windows file sharing.
 const auto database=root/"Archive"/"archive.sqlite";sqlite3*locked{};const auto path=database.u8string();check(sqlite3_open(reinterpret_cast<const char*>(path.c_str()),&locked)==SQLITE_OK,"Open isolated conflicting writer");struct Lock{sqlite3*db;~Lock(){if(db){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(db);}}}lock{locked};check(sqlite3_exec(locked,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)==SQLITE_OK,"Hold fixture writer lock");entry=*service.state().selected();entry.title="Unsaved new title";service.state().update(entry,812000002.,2);check(!service.flush()&&service.state().hasUnsavedChanges()&&service.state().error(),"Failure retains complete draft and reports blocked shutdown");check(sqlite3_exec(locked,"ROLLBACK",nullptr,nullptr,nullptr)==SQLITE_OK,"Release only owned fixture transaction");sqlite3_close(locked);lock.db=nullptr;check(service.flush()&&!service.state().hasUnsavedChanges(),"Explicit retry saves retained latest value after storage restored");service.state().deactivate();drain(executor,service);
 }
 {app::ArchiveService reopened(root/"Archive",executor,rules);reopened.state().activate();drain(executor,reopened);check(reopened.state().entries().size()==1,"Reopen reads committed source-compatible summary");reopened.state().select(id);drain(executor,reopened);check(reopened.state().selected()&&reopened.state().selected()->title=="Unsaved new title"&&reopened.state().selected()->body=="Retained body","Complete record survives save failure/retry/reopen");check(reopened.flush(),"Unchanged flush does not fabricate failure");}
 executor.waitIdle();executor.drain();check(executor.stats().pending==0&&executor.stats().running==0,"No service job remains after owned lifetime");
}
}
int main(){try{run();std::cout<<"PASS "<<checks<<" shared Archive service checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
