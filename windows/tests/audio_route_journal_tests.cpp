#include "native/audio_route_journal.hpp"
#include "core/data/data_store.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

// Isolated temporary directories only: never the user's data root.
namespace {
namespace n=endfield::native;namespace fs=std::filesystem;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
struct Temp {
    fs::path root;
    Temp(){root=fs::temp_directory_path()/("endfield-audio-journal-"+ehud::data::makeUUID());fs::create_directories(root);root=fs::canonical(root);}
    ~Temp(){std::error_code e;fs::remove_all(root,e);}
};
std::string read(const fs::path&p){std::ifstream f(p,std::ios::binary);std::stringstream s;s<<f.rdbuf();return s.str();}
void write(const fs::path&p,const std::string&bytes){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary|std::ios::trunc);f<<bytes;}
n::AudioRouteJournalEntry entry(std::string session,float original,float written,std::wstring endpoint=L"{0.0.0.00000000}.{speaker}"){
    return {std::move(endpoint),std::move(session),"{0.0.0.00000000}.{speaker}|\\Device\\HarddiskVolume3\\Synthetic\\player.exe%b{00000000-0000-0000-0000-000000000000}","4242:133000000000000000",original,written};
}

void codec(){
    const std::vector<n::AudioRouteJournalEntry>entries{entry("{session-a}",.8f,.25f),entry("{session-b}",1,.1f,L"{0.0.0.00000000}.{headphones-一}")};
    const auto bytes=n::AudioRouteJournal::encode(entries);const auto decoded=n::AudioRouteJournal::decode(bytes);
    check(decoded&&*decoded==entries,"Journal codec round-trips identities and exact float scalars");
    for(const float value:{0.f,1e-7f,.1f,1.f/3,.999999f,1.f}){auto e=entries;e[0].written=value;const auto d=n::AudioRouteJournal::decode(n::AudioRouteJournal::encode(e));check(d&&(*d)[0].written==value,"Every float scalar survives the JSON round trip bit-exactly");}
    check(!n::AudioRouteJournal::decode("{}")&&!n::AudioRouteJournal::decode("not json")&&!n::AudioRouteJournal::decode(""),"Malformed journals are rejected");
    auto text=bytes;text.replace(text.find("\"schema\":1"),10,"\"schema\":2");check(!n::AudioRouteJournal::decode(text),"Other schema versions are rejected");
    text=bytes;text.replace(text.find("\"format\":"),9,"\"extra\":0,\"format\":");check(!n::AudioRouteJournal::decode(text),"Unknown top-level keys are rejected");
    std::vector<n::AudioRouteJournalEntry>duplicate{entries[0],entries[0]};check(!n::AudioRouteJournal::decode(n::AudioRouteJournal::encode(duplicate)),"A session instance appears once");
    auto invalid=entries;invalid[0].written=1.5f;bool threw{};try{(void)n::AudioRouteJournal::encode(invalid);}catch(const std::invalid_argument&){threw=true;}check(threw,"Out-of-range scalars are never encoded");
    invalid=entries;invalid[0].session.clear();threw=false;try{(void)n::AudioRouteJournal::encode(invalid);}catch(const std::invalid_argument&){threw=true;}check(threw,"An entry needs its session lease identity");
}
void lifecycle(){
    Temp temp;const auto path=temp.root/"Audio"/"RouteJournal.json";
    {
        n::AudioRouteJournal journal(path);check(journal.available()&&journal.recovered().empty()&&journal.live().empty()&&!fs::exists(path),"A missing journal is empty and creates nothing");
        check(journal.flush()==0&&!fs::exists(path),"Flushing an empty journal writes nothing");
        check(journal.record(entry("{session-a}",.8f,.4f))>=0,"First attenuation is recorded");
        auto disk=n::AudioRouteJournal::decode(read(path));check(disk&&disk->size()==1&&(*disk)[0].written==.4f,"The record is durable on disk BEFORE record() returns");
        check(journal.written("{session-a}",.3f)&&journal.dirty(),"Later levels are memory-only until a flush");
        disk=n::AudioRouteJournal::decode(read(path));check((*disk)[0].written==.4f,"No disk write per level change");
        check(journal.flush()==0&&!journal.dirty()&&(*n::AudioRouteJournal::decode(read(path)))[0].written==.3f,"Coalesced flush persists the latest level");
        check(!journal.written("{unknown}",.2f)&&!journal.written("{session-a}",2.f),"Unknown sessions and invalid levels are ignored");
        check(journal.record(entry("{session-b}",1,.5f))>=0&&journal.live().size()==2,"A second session is recorded");
        check(journal.release("{session-a}")&&journal.live().size()==1&&journal.liveEntry("{session-b}"),"Restoring a session releases its entry");
        check(journal.flush()==0&&n::AudioRouteJournal::decode(read(path))->size()==1,"Release reaches disk with the next flush");
    }
    {
        n::AudioRouteJournal next(path);
        check(next.available()&&next.recovered().size()==1&&next.recovered()[0].session=="{session-b}"&&next.live().empty(),"A later run loads the unrestored entry for recovery");
        check(next.record(entry("{session-c}",.9f,.45f))>=0,"New live entries coexist with recovered ones");
        auto disk=n::AudioRouteJournal::decode(read(path));check(disk&&disk->size()==2,"Recovered entries stay durable until settled");
        check(next.settle(0)&&next.recovered().empty()&&next.flush()==0&&n::AudioRouteJournal::decode(read(path))->size()==1,"Settling removes a recovered entry");
        check(!next.settle(0),"Settling an absent index is refused");
        check(next.demote(L"{0.0.0.00000000}.{speaker}")==1&&next.live().empty()&&next.recovered().size()==1,"Unreachable endpoint entries become recoverable");
        check(next.flush()==0,"Demotion persists");
        check(next.record(entry("{session-d}",.7f,.35f))>=0&&next.demoteSession("{session-d}")&&next.live().empty()&&next.recovered().size()==2&&next.recovered()[1].written==.35f,"A vanished live session becomes a recovery entry with its last written level");
        check(!next.demoteSession("{session-d}")&&!next.demoteSession("{unknown}")&&next.dirty(),"Only a live session can be demoted");
        check(next.flush()==0&&n::AudioRouteJournal::decode(read(path))->size()==2,"Session demotion persists with the next flush");
    }
}
void protection(){
    Temp temp;const auto path=temp.root/"RouteJournal.json";
    write(path,"{corrupt");
    {
        n::AudioRouteJournal journal(path);
        check(!journal.available()&&journal.status()==static_cast<std::int32_t>(0x80070570u),"A corrupt journal is unavailable");
        check(journal.record(entry("{a}",.8f,.4f))<0&&read(path)=="{corrupt","No attenuation is recorded over a corrupt journal; the file is preserved");
    }
    write(path,"{\"entries\":[],\"format\":\"EndfieldHUD.Windows.AudioRouteJournal\",\"schema\":9}\n");
    {
        n::AudioRouteJournal journal(path);check(!journal.available()&&journal.status()==static_cast<std::int32_t>(0x8007051Au),"A newer journal is reported as a revision mismatch and preserved");
    }
    std::error_code e;fs::remove(path,e);
    {
        n::AudioRouteJournal journal(path);check(journal.record(entry("{a}",.8f,.4f))>=0,"Fresh journal records");
        write(path,"{\"entries\":[],\"format\":\"EndfieldHUD.Windows.AudioRouteJournal\",\"schema\":1}\n"); // another writer
        check(journal.record(entry("{b}",.8f,.4f))<0&&!journal.available(),"An external change stops this journal instead of overwriting it");
        check(journal.live().size()==1&&journal.liveEntry("{a}")&&!journal.liveEntry("{b}"),"A refused record is rolled back in memory");
    }
    {
        n::AudioRouteJournal relative(fs::path("relative.json"));check(!relative.available(),"Only an explicit absolute journal path is accepted");
    }
    fs::remove(path,e);
    {
        n::AudioRouteJournal journal(path);std::int32_t last{};
        for(std::size_t i=0;i<n::AudioRouteJournal::maximumEntries&&last>=0;++i)last=journal.record(entry("{s"+std::to_string(i)+"}",.8f,.4f));
        check(last>=0&&journal.live().size()==n::AudioRouteJournal::maximumEntries,"The journal holds its bounded capacity");
        check(journal.record(entry("{overflow}",.8f,.4f))==static_cast<std::int32_t>(0x8007006Fu),"Beyond capacity, attenuation is refused rather than unrecorded");
        check(journal.record(entry("{s0}",.8f,.2f))>=0,"Updating an existing entry is still allowed when full");
    }
}
}
int main(){
    try{codec();lifecycle();protection();std::cout<<"PASS "<<checks<<" audio route journal checks (temporary directories only)\n";return 0;}
    catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
