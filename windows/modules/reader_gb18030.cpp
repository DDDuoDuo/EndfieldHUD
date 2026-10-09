#include "modules/reader_gb18030.hpp"
#include "modules/reader_gb18030_table.hpp"
#include <algorithm>
namespace endfield::modules {
std::u16string readerGB18030(std::span<const std::uint8_t> bytes){
    if(bytes.size()>32*1024*1024)throw ReaderError(ReaderErrorCode::tooLarge);
    std::u16string out;out.reserve(bytes.size());
    const auto scalar=[&](std::uint32_t value){if(value>0x10ffff||(value>=0xd800&&value<=0xdfff))throw ReaderError(ReaderErrorCode::invalidBook);if(value<0x10000)out.push_back(static_cast<char16_t>(value));else{value-=0x10000;out.push_back(static_cast<char16_t>(0xd800+(value>>10)));out.push_back(static_cast<char16_t>(0xdc00+(value&1023)));}};
    for(std::size_t n=0;n<bytes.size();){const unsigned a=bytes[n++];if(a>=0x81&&a<=0xfe){if(n>=bytes.size())throw ReaderError(ReaderErrorCode::invalidBook);const unsigned b=bytes[n++];if(b>=0x30&&b<=0x39){if(bytes.size()-n<2)throw ReaderError(ReaderErrorCode::invalidBook);const unsigned c=bytes[n++],d=bytes[n++];if(c<0x81||c>0xfe||d<0x30||d>0x39)throw ReaderError(ReaderErrorCode::invalidBook);const auto index=static_cast<std::uint32_t>((((a-0x81)*10+b-0x30)*126+c-0x81)*10+d-0x30);const auto&table=readerGB18030Data::ranges;const auto range=std::upper_bound(table.begin(),table.end(),index,[](auto i,const auto&r){return i<r.first;});if(range==table.begin()||index>std::prev(range)->last)throw ReaderError(ReaderErrorCode::invalidBook);const auto&r=*std::prev(range);scalar(r.scalar+index-r.first);
            }else{if(b<0x40||b>0xfe||b==0x7f)throw ReaderError(ReaderErrorCode::invalidBook);scalar(readerGB18030Data::pairs[(a-0x81)*190+b-0x40-(b>0x7f?1:0)]);}
        }else scalar(readerGB18030Data::singles[a]);}
    return out;
}
}
