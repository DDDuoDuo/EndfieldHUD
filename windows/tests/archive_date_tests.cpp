#include "native/archive_dates.hpp"
#include "core/data/file_io.hpp"
#include "core/data/json.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
void run(const std::filesystem::path&path){
    using endfield::native::ArchiveDateFormatter;
    auto bytes=ehud::data::detail::readFile(path,128*1024);check(bytes.has_value(),"Source date fixture exists");
    const auto fixture=ehud::data::Json::parse(*bytes,128*1024);std::string zone;std::unique_ptr<ArchiveDateFormatter>formatter;
    for(const auto&row:fixture["rows"].array()){
        if(row["zone"].string()!=zone){zone=row["zone"].string();formatter=std::make_unique<ArchiveDateFormatter>(std::u16string(zone.begin(),zone.end()));}
        if(row.object().contains("formatted")){
            const auto actual=formatter->format(row["seconds"].number());
            if(actual!=row["formatted"].string())std::cerr<<zone<<" date "<<row["seconds"].number()<<": "<<actual<<" != "<<row["formatted"].string()<<'\n';
            check(actual==row["formatted"].string(),"Date matches unchanged ArchiveCanvas formatter");
        }else{
            const auto actual=formatter->parse(row["text"].string());const bool expected=!row["seconds"].isNull();
            if(actual.has_value()!=expected||(actual&&std::abs(*actual-row["seconds"].number())>.001))std::cerr<<zone<<" input "<<row["text"].string()<<": "<<(actual?std::to_string(*actual):"invalid")<<" != "<<row["seconds"].encode()<<'\n';
            check(actual.has_value()==expected,"Strict date acceptance matches original source");
            if(actual)check(std::abs(*actual-row["seconds"].number())<.001,"Parsed local midnight preserves Foundation epoch");
        }
    }
    check(!formatter->refreshSystemTimeZone(),"Explicit fixture zone does not consult or alter system settings");
    check(!formatter->parse(std::string(4096,'9')),"Oversized input is bounded");
    bool rejected{};try{formatter->format(std::numeric_limits<double>::quiet_NaN());}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Nonfinite date cannot enter ICU");
    ArchiveDateFormatter system;check(!system.format(0).empty(),"System zone resolves without changing it");
    check(!system.refreshSystemTimeZone(),"Unchanged OS zone retains the formatter");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass original date fixture");run(argv[1]);std::cout<<"PASS "<<checks<<" source Archive date checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
