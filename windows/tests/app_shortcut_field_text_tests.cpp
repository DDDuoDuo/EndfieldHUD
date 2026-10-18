#include "native/app_shortcut_field_text.hpp"
#include "core/data/json.hpp"
#include <fstream>
#ifdef _WIN32
#include <icu.h>
#else
#include <unicode/ustring.h>
#endif
#include <iostream>
namespace {unsigned checks{};void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
std::u16string utf16(const std::string&s){std::u16string out(s.size(),u' ');UErrorCode error=U_ZERO_ERROR;int32_t count{};u_strFromUTF8(reinterpret_cast<UChar*>(out.data()),static_cast<int32_t>(out.size()),&count,s.data(),static_cast<int32_t>(s.size()),&error);if(U_FAILURE(error))throw std::runtime_error("Invalid fixture UTF8");out.resize(static_cast<std::size_t>(count));return out;}
}
int main(int argc,char**argv){try{
    using namespace endfield;check(argc==2,"Pass original Shortcut field fixture");
    std::ifstream stream(argv[1],std::ios::binary);check(bool(stream),"Explicit original field fixture opens");const auto fixture=ehud::data::Json::parse(std::string(std::istreambuf_iterator<char>(stream),{}),1024*1024);
    check(!fixture["usesAppOrWindow"].boolean()&&fixture["rows"].array().size()>=20,"Original Foundation fixture is isolated and varied");
    for(const auto&r:fixture["rows"].array()){const auto value=native::normalizeShortcutField(utf16(r["input"].string()));check(value==utf16(r["normalized"].string()),"Name newline/grapheme boundary equals original Foundation expression");check(value.size()==std::size_t(r["utf16"].integer()),"Name retains original UTF16 length");check(native::normalizeShortcutField(value)==value,"Name normalization is idempotent");}
    for(const auto&bad:{std::u16string(1,0xd800),std::u16string(1,0xdc00),std::u16string{0xd800,u'A'}}){bool rejected{};try{(void)native::normalizeShortcutField(bad);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Malformed UTF16 never reaches ICU");}
    check(native::normalizeShortcutField(u"A\r\nB")==u"A  B","CR and LF are separate Foundation newline separators");
    std::cout<<"PASS "<<checks<<" Shortcut name source checks\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
