// Windows DPAPI vault for classified Archive documents (D5): the Mac Keychain
// item semantics (absent = no item, duplicate add refused, remove of an absent
// item succeeds, enumeration for the per-document sweep) over current-user
// DPAPI files with an owner-only DACL, and the repository's classify/verify/
// declassify/missing-verifier path through it. Temporary folders only; no
// Credential Manager, no user data root.
#include "native/archive_password_vault.hpp"
#include "native/hypergryph_account_vault.hpp"
#include "native/archive_text_rules.hpp"
#include "modules/archive_repository.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>

namespace {
namespace mod=endfield::modules;namespace nat=endfield::native;
unsigned checks{};void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>bool throws(F&&f){try{f();}catch(const std::exception&){return true;}return false;}
struct Temporary {std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-Archive-Vault-"+ehud::data::makeUUID());Temporary(){std::filesystem::create_directories(root);}~Temporary(){std::error_code e;std::filesystem::remove_all(root,e);}};
const std::string doc="B0000000-0000-4000-8000-000000000001",key="C0000000-0000-4000-8000-0000000000AA";
void vault(){Temporary t;nat::DpapiArchivePasswordVault v(t.root/"Archive"/"Classified");const auto account=mod::archivePasswordAccount(doc,key);
    check(!v.read(account)&&v.accounts().empty(),"No item reads as absent (password missing), nothing listed");
    const auto record=mod::ArchivePasswordVerifier::make("correct horse",600000).encoded();v.add(account,record);
    check(v.read(account)==std::optional<std::string>(record)&&v.accounts()==std::vector<std::string>{account},"The record round-trips and is listed by account");
    check(v.path(account).filename()==std::filesystem::path(doc+"."+key+".verifier")&&nat::ownerOnly(v.path(account))&&nat::ownerOnly(v.path(account).parent_path()),"One owner-only DPAPI file per account");
    std::string raw;{std::ifstream in(v.path(account),std::ios::binary);raw.assign(std::istreambuf_iterator<char>(in),{});}check(raw.find("PBKDF2")==std::string::npos,"The record is not stored in plaintext");
    check(throws([&]{v.add(account,record);})&&v.read(account)==std::optional<std::string>(record),"A duplicate add is refused and never overwrites");
    {std::ofstream out(v.path(account),std::ios::binary|std::ios::trunc);out<<"EHUDARV1garbage";}check(throws([&]{(void)v.read(account);}),"An undecryptable item throws (vault unavailable), unlike an absent one");
    v.remove(account);v.remove(account);check(!v.read(account),"Remove succeeds, also when absent");
    check(throws([&]{(void)v.path("b0000000-0000-4000-8000-000000000001/"+key);})&&throws([&]{(void)v.path("../x");}),"Only canonical uppercase accounts name files");
    check(throws([&]{nat::DpapiArchivePasswordVault relative("relative");}),"No implicit directory");
}
void repository(){Temporary t;const auto rules=nat::nativeArchiveTextRules();auto v=std::make_shared<nat::DpapiArchivePasswordVault>(t.root/"Archive"/"Classified");
    mod::ArchiveSQLiteRepository repo(t.root/"Archive",rules,v);mod::ArchiveEntry e;e.id=doc;e.title="Secret";e.body="Plaintext";repo.save(e);
    repo.classify(doc,"correct horse");check(repo.verify(doc,"correct horse")&&!repo.verify(doc,"wrong horse")&&repo.classificationStatus(doc)==mod::ArchiveClassificationStatus::protectedByPassword,"Classify and verify through DPAPI");
    for(const auto&account:v->accounts())v->remove(account);check(repo.classificationStatus(doc)==mod::ArchiveClassificationStatus::passwordMissing,"A removed verifier is passwordMissing (the Mac-import path)");
    repo.recoverClassification(doc,std::string("new password"));check(repo.verify(doc,"new password")&&v->accounts().size()==1,"Recovery writes one new DPAPI item");
    repo.declassify(doc);check(v->accounts().empty()&&!repo.isClassified(doc),"Declassify removes the item");
}
}
int main(){try{vault();repository();std::cout<<"PASS "<<checks<<" Archive DPAPI vault contracts\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
