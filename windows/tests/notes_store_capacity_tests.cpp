#include "core/data/data_store.hpp"
#include <algorithm>
#include <filesystem>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <string>

// A Notes store larger than the former 64 MiB in-memory bound (many large
// notes, as a real Mac store with drawings can be) opens and reloads without
// loss. Owned temporary directory only; removed afterwards.
namespace {
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
}
int main(){
    const auto root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-Notes-Capacity-"+ehud::data::makeUUID());
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}}cleanup{root};
    try{
        check(!std::filesystem::exists(root)&&std::filesystem::create_directory(root),"New owned temporary root");
        constexpr int count=60;constexpr std::size_t size=1200*1024;std::vector<std::string> ids;
        {ehud::data::NotesStore store(root);for(int n=0;n<count;++n){ehud::data::Note note;note.text=std::string(size,static_cast<char>('a'+n%26));note.text[0]='#';note.zIndex=n;ids.push_back(note.id);check(store.upsert(note),"A large note saves");}
            check(store.notes().size()==count,"Every large note is in memory");}
        {ehud::data::NotesStore reopened(root);const auto& notes=reopened.notes();check(notes.size()==count,"A 70 MiB store reopens");
            std::size_t total{};for(const auto& n:notes){total+=n.text.size();check(n.text.size()==size&&n.text[0]=='#',"Each note keeps its complete text");}
            check(total>64u*1024*1024,"The store exceeds the former in-memory bound");
            for(int n=0;n<count;++n)check(std::any_of(notes.begin(),notes.end(),[&](const ehud::data::Note& v){return v.id==ids[n]&&v.text[1]==static_cast<char>('a'+n%26);}),"Every note keeps its identity and content");}
        std::cout<<"PASS "<<checks<<" Notes store capacity contracts\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
