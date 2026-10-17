#include "modules/reader_zip.hpp"
#include "third_party/zlib/zlib.h"
#include <algorithm>
#include <array>
#include <limits>

namespace endfield::modules {
namespace {
void need(bool value,ReaderErrorCode code=ReaderErrorCode::invalidArchive){if(!value)throw ReaderError(code);}
std::uint16_t u16(std::span<const std::uint8_t>b,std::size_t n){need(n<=b.size()&&b.size()-n>=2);return std::uint16_t(b[n])|std::uint16_t(std::uint16_t(b[n+1])<<8);}
std::uint32_t u32(std::span<const std::uint8_t>b,std::size_t n){need(n<=b.size()&&b.size()-n>=4);return std::uint32_t(b[n])|(std::uint32_t(b[n+1])<<8)|(std::uint32_t(b[n+2])<<16)|(std::uint32_t(b[n+3])<<24);}
std::string string(std::span<const std::uint8_t>b,std::size_t offset,std::size_t size){need(offset<=b.size()&&size<=b.size()-offset);std::string value(reinterpret_cast<const char*>(b.data()+offset),size);need(ReaderJson::validUtf8(value));return value;}
int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
}
void checkReaderCancelled(const ReaderCancel&c){if(c&&c())throw ReaderCancelled();}
std::vector<std::uint8_t>ReaderZIP::read(std::uint64_t offset,std::size_t count)const{need(offset<=file_->size()&&count<=file_->size()-offset&&count<=maximumEntryBytes);std::vector<std::uint8_t>b(count);if(count)file_->read(offset,b);return b;}
ReaderZIP::ReaderZIP(std::shared_ptr<ReaderBytes>file,ReaderCancel cancelled):file_(std::move(file)){
    need(bool(file_));checkReaderCancelled(cancelled);const auto length=file_->size();need(length>=22&&length<=maximumInflatedBytes,ReaderErrorCode::tooLarge);
    const auto start=length-std::min<std::uint64_t>(length,65557);const auto tail=read(start,std::size_t(length-start));std::optional<std::size_t>footer;
    for(std::size_t index=tail.size()-22;;--index){if(u32(tail,index)==0x06054b50&&index+22+u16(tail,index+20)==tail.size()){footer=index;break;}if(index==0)break;}
    need(bool(footer));const auto end=*footer;need(u16(tail,end+4)==0&&u16(tail,end+6)==0&&u16(tail,end+8)==u16(tail,end+10));const auto count=u16(tail,end+10);const auto directorySize=u32(tail,end+12);directoryOffset_=u32(tail,end+16);
    need(count>0&&count<=maximumEntries&&directorySize<=16*1024*1024&&directoryOffset_+directorySize==start+end);const auto table=read(directoryOffset_,directorySize);std::size_t position{};std::uint64_t inflated{};
    for(unsigned n=0;n<count;++n){checkReaderCancelled(cancelled);need(position<=table.size()&&table.size()-position>=46&&u32(table,position)==0x02014b50);const auto nameSize=u16(table,position+28),extra=u16(table,position+30),comment=u16(table,position+32);const auto stop=position+46+nameSize+extra+comment;need(stop<=table.size()&&nameSize>0&&nameSize<=4096&&u16(table,position+34)==0);auto name=string(table,position+46,nameSize);need(safePath(name)&&!entries_.contains(name));Entry e{name,u16(table,position+10),u16(table,position+8),u32(table,position+16),u32(table,position+20),u32(table,position+24),u32(table,position+42)};
        need((e.flags&0x2041)==0,ReaderErrorCode::encrypted);need((e.method==0||e.method==8)&&((u32(table,position+38)>>16)&0xf000)!=0xa000&&e.size<=maximumEntryBytes&&e.compressed<=maximumEntryBytes&&e.offset+30+e.compressed<=directoryOffset_&&std::uint64_t(e.size)<=std::uint64_t(std::max(1u,e.compressed))*2000);inflated+=e.size;need(inflated<=maximumInflatedBytes,ReaderErrorCode::tooLarge);entries_.emplace(name,std::move(e));position=stop;
    }need(position==table.size());
}
std::vector<std::uint8_t>ReaderZIP::data(std::string_view name,std::size_t limit,const ReaderCancel&cancelled)const{
    checkReaderCancelled(cancelled);const auto found=entries_.find(name);need(found!=entries_.end()&&found->second.size<=limit);const auto&e=found->second;const auto header=read(e.offset,30);need(u32(header,0)==0x04034b50&&u16(header,6)==e.flags&&u16(header,8)==e.method);const auto nameSize=u16(header,26),extra=u16(header,28);const auto body=e.offset+30+nameSize+extra;need(body+e.compressed<=directoryOffset_);const auto local=read(e.offset+30,nameSize);need(string(local,0,local.size())==e.name);if(!(e.flags&8))need(u32(header,14)==e.crc&&u32(header,18)==e.compressed&&u32(header,22)==e.size);auto source=read(body,e.compressed);checkReaderCancelled(cancelled);std::vector<std::uint8_t>out;
    if(e.method==0){need(source.size()==e.size);out=std::move(source);}else{
        out.resize(std::size_t(e.size)+1);z_stream stream{};need(inflateInit2(&stream,-MAX_WBITS)==Z_OK);struct End{z_stream*stream;~End(){inflateEnd(stream);}}end{&stream};stream.next_in=source.data();stream.avail_in=static_cast<uInt>(source.size());int status=Z_OK;
        do{checkReaderCancelled(cancelled);const auto before=stream.total_out;const auto available=out.size()-std::size_t(stream.total_out);need(available>0);stream.next_out=out.data()+stream.total_out;stream.avail_out=static_cast<uInt>(std::min<std::size_t>(available,65536));status=inflate(&stream,Z_NO_FLUSH);need(status==Z_OK||status==Z_STREAM_END);if(status==Z_OK)need(stream.total_out!=before||stream.avail_in>0);}while(status!=Z_STREAM_END);
        need(stream.total_in==source.size()&&stream.total_out==e.size);out.resize(e.size);
    }
    uLong crc=crc32(0,nullptr,0);for(std::size_t n=0;n<out.size();){checkReaderCancelled(cancelled);const auto count=std::min<std::size_t>(65536,out.size()-n);crc=crc32(crc,out.data()+n,static_cast<uInt>(count));n+=count;}need(std::uint32_t(crc)==e.crc);return out;
}
bool ReaderZIP::safePath(std::string_view name)noexcept{if(name.empty()||name.starts_with('/')||name.find_first_of("\\:\0",0,3)!=std::string_view::npos)return false;std::size_t start{};for(;;){const auto end=name.find('/',start);const auto part=name.substr(start,end==name.npos?name.size()-start:end-start);if(part==".."||part==".")return false;if(end==name.npos)return true;start=end+1;}}
std::string ReaderZIP::resolve(std::string_view href,std::string_view base){const auto raw=href.substr(0,href.find('#'));std::string decoded;decoded.reserve(raw.size());for(std::size_t n=0;n<raw.size();++n){if(raw[n]=='%'){need(raw.size()-n>=3&&hex(raw[n+1])>=0&&hex(raw[n+2])>=0,ReaderErrorCode::invalidBook);decoded+=char(hex(raw[n+1])*16+hex(raw[n+2]));n+=2;}else decoded+=raw[n];}need(!decoded.empty()&&ReaderJson::validUtf8(decoded)&&!decoded.starts_with('/')&&decoded.find_first_of(":\\\0?",0,4)==decoded.npos,ReaderErrorCode::invalidBook);std::vector<std::string_view>pieces;auto append=[&](std::string_view value,bool resolving){std::size_t start{};while(start<value.size()){const auto end=value.find('/',start);const auto part=value.substr(start,end==value.npos?value.size()-start:end-start);if(!part.empty()){if(resolving&&part==".."){need(!pieces.empty(),ReaderErrorCode::invalidBook);pieces.pop_back();}else if(!resolving||part!=".")pieces.push_back(part);}if(end==value.npos)break;start=end+1;}};const auto slash=base.rfind('/');if(slash!=base.npos)append(base.substr(0,slash),false);append(decoded,true);std::string result;for(auto part:pieces){if(!result.empty())result+='/';result+=part;}need(safePath(result),ReaderErrorCode::invalidBook);return result;}
} // namespace endfield::modules
