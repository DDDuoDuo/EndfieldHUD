#include "native/event_log_names.hpp"
#include "modules/event_log.hpp"
#include "core/data/json.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
namespace native=endfield::native;namespace model=endfield::modules;using Json=ehud::data::Json;
namespace {
std::size_t checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&&f,const char*why){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
}
int main(int argc,char**argv){try{
    check(argc==2,"Pass the original Swift event-log-names.json fixture");std::ifstream input(argv[1],std::ios::binary);check(bool(input),"Open explicit Unicode oracle");
    const std::string bytes((std::istreambuf_iterator<char>(input)),{});check(bytes.size()<=2*1024*1024,"Unicode oracle is bounded");const auto reference=Json::parse(bytes,2*1024*1024);
    check(reference["provenance"]["source"].string()=="Sources/SystemEventLog.swift"&&reference["provenance"]["sha256"].string()=="10085570f86bca00eaa011f1dda4c3436b41c2252d0d18ff3c6650d3bff64b9e","Fixture carries the exact original source provenance used by the scalar tables");
    native::NativeEventNameCompactor names;std::size_t caseNumber{};
    for(const auto&row:reference["cases"].array()){
        const auto value=names.compact(row["input"].string());
        if(value!=row["expected"].string())throw std::runtime_error("Original Swift event name mismatch at synthetic case "+std::to_string(caseNumber));
        check(value.size()<=160&&Json::validUtf8(value),"Original compaction preserves whole valid UTF-8 output within byte bound");++caseNumber;
    }
    check(caseNumber>=1000,"Oracle covers controls, trimming, combining sequences and long clusters");
    rejects([&]{names.compact(std::string(4097,'x'));},"Oversized raw metadata rejects before segmentation");
    for(const auto&value:{std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),std::string("\xf0\x80\x80\xaf",4),std::string("\xff",1)})rejects([&]{names.compact(value);},"Malformed UTF-8 rejects without partial output");
    bool otherThreadRejected{};std::thread other([&]{try{names.compact("synthetic");}catch(const std::invalid_argument&){otherThreadRejected=true;}});other.join();check(otherThreadRejected,"Mutable ICU iterator cannot escape its creating thread");
    model::EventNameCompactor callback=[&](std::string_view value){return names.compact(value);};
    std::string longName;for(unsigned n=0;n<80;++n)longName+="扬声器";
    const auto safe=model::sanitizedEventMetadata(model::EventKind::audioDeviceConnected,{{"device",longName},{"path","PRIVATE"}},callback);
    check(safe.size()==1&&safe.at("device")==names.compact(longName),"Real metadata injector accepts bounded whole Chinese characters");
    const auto file=model::sanitizedEventMetadata(model::EventKind::shelfAdded,{{"filename","C:\\private\\"+longName}},callback);
    check(file.size()==1&&file.at("filename")==names.compact(longName),"Native compaction occurs only after private filename path removal");
    const auto blocked=model::sanitizedEventMetadata(model::EventKind::appShortcutOpened,{{"app","file:///PRIVATE"}},callback);check(blocked.empty(),"Unicode implementation does not weaken URL/path privacy rules");
    const auto version=native::NativeEventNameCompactor::unicodeVersion();std::cout<<"Event name Unicode version "<<unsigned(version[0])<<'.'<<unsigned(version[1])<<'.'<<unsigned(version[2])<<": "<<checks<<" checks / "<<caseNumber<<" original Swift cases passed\n";return 0;
}catch(const std::exception&error){std::cerr<<"Event name contracts failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
