#include "modules/reader_gb18030.hpp"
#include "modules/reader_gb18030_table.hpp"
#include "modules/reader_text.hpp"
#include "core/data/file_io.hpp"
#include <array>
#include <iostream>
using namespace endfield::modules;
namespace {unsigned checks{};void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}template<class F>void rejects(F f){bool failed{};try{f();}catch(const ReaderError&){failed=true;}check(failed,"Malformed GB18030 sequence rejected without replacements");}
std::u16string scalar(std::uint32_t cp){if(cp<0x10000)return {static_cast<char16_t>(cp)};cp-=0x10000;return {static_cast<char16_t>(0xd800+(cp>>10)),static_cast<char16_t>(0xdc00+(cp&1023))};}
std::array<std::uint8_t,4>bytes(std::uint32_t i){const auto d=i%10;i/=10;const auto c=i%126;i/=126;const auto b=i%10;i/=10;return {static_cast<std::uint8_t>(i+0x81),static_cast<std::uint8_t>(b+0x30),static_cast<std::uint8_t>(c+0x81),static_cast<std::uint8_t>(d+0x30)};}
void run(){check(readerGB18030(std::array<std::uint8_t,4>{0xd6,0xd0,0xce,0xc4})==u"中文","Unchanged source GB18030 original TXT fixture");check(readerGB18030(std::array<std::uint8_t,4>{0xa8,0xbc,0xa6,0xd9})==u"ḿ︐","Unchanged source Foundation compatibility characters");
    // Exhaustive byte/parser indexing against the generated API oracle, not an
    // OS codec or a copied pagination algorithm. The immutable table pin is
    // separately recorded by the build-only Foundation extractor provenance.
    for(unsigned a=0x81;a<=0xfe;++a)for(unsigned b=0x40;b<=0xfe;++b)if(b!=0x7f){const std::array<std::uint8_t,2>input{static_cast<std::uint8_t>(a),static_cast<std::uint8_t>(b)};const auto cp=readerGB18030Data::pairs[(a-0x81)*190+b-0x40-(b>0x7f?1:0)];if(cp==UINT32_MAX)rejects([&]{readerGB18030(input);});else check(readerGB18030(input)==scalar(cp),"Every pair decoder index matches generated Foundation output");}
    for(const auto&r:readerGB18030Data::ranges)for(auto i:{r.first,r.last})check(readerGB18030(bytes(i))==scalar(r.scalar+i-r.first),"Every four-byte range boundary retains exact Unicode scalars");
    for(unsigned b=0;b<=255;++b){const std::array<std::uint8_t,1>input{static_cast<std::uint8_t>(b)};const auto cp=readerGB18030Data::singles[b];if((b>=0x81&&b<=0xfe)||cp==UINT32_MAX)rejects([&]{readerGB18030(input);});else check(readerGB18030(input)==scalar(cp),"Single-byte table boundaries retain original API facts");}
    for(const auto&input:std::array<std::vector<std::uint8_t>,7>{{{0x81},{0x81,0x7f},{0x81,0x30},{0x81,0x30,0x81},{0x81,0x30,0x80,0x30},{0x81,0x30,0x81,0x2f},{0xff}}})rejects([&]{readerGB18030(input);});
    check(readerGB18030(bytes(189000))==u"\U00010000"&&readerGB18030(bytes(1237575))==u"\U0010ffff","Supplementary endpoints are surrogate pairs without truncation");
    rejects([&]{readerGB18030(bytes(1237576));});check(readerGB18030({}).empty(),"Codec leaves original owner empty-document policy intact");
}}
int main(){try{run();std::cout<<"Foundation GB18030 passed "<<checks<<" checks\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
