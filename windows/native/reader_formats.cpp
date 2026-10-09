#include "native/reader_formats.hpp"
#include "modules/reader_gb18030.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xmllite.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <icu.h>
#include <algorithm>
#include <regex>
#include <limits>
namespace endfield::native {
namespace {
using namespace modules;using Microsoft::WRL::ComPtr;
void need(bool value){if(!value)throw ReaderError(ReaderErrorCode::invalidBook);}void hr(HRESULT value){need(SUCCEEDED(value));}
std::u16string chars(const wchar_t*value,UINT length){return value?std::u16string(reinterpret_cast<const char16_t*>(value),length):std::u16string{};}
std::u16string decode(std::span<const std::uint8_t>b){if(b.size()>=2&&((b[0]==0xff&&b[1]==0xfe)||(b[0]==0xfe&&b[1]==0xff)))return readerUTF16(b);return readerUTF8(std::string_view(reinterpret_cast<const char*>(b.data()),b.size()));}
std::string lowerASCII(std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(c>='A'&&c<='Z'?c+32:c);});return value;}
}
std::u16string decodeReaderTXT(std::span<const std::uint8_t>b){
    if(b.size()>32*1024*1024)throw ReaderError(ReaderErrorCode::tooLarge);std::u16string text;
    if(b.size()>=2&&((b[0]==0xff&&b[1]==0xfe)||(b[0]==0xfe&&b[1]==0xff)))text=readerUTF16(b);
    else {const std::string_view raw(reinterpret_cast<const char*>(b.data()),b.size());if(ReaderJson::validUtf8(raw))text=readerUTF8(raw);else{
        // Foundation's pinned GB18030 table differs from CP54936 on supported
        // Windows builds. Use the complete source-generated immutable mapping,
        // retaining strict validation and the original UTF8-first precedence.
        text=modules::readerGB18030(b);
    }}need(!text.empty()&&text.find(u'\0')==text.npos&&readerValidUTF16(text));return text;
}
std::string readerGraphemePrefix(std::u16string_view text,std::size_t maximum){need(readerValidUTF16(text)&&text.size()<=static_cast<std::size_t>(INT32_MAX));UErrorCode error=U_ZERO_ERROR;auto*raw=ubrk_open(UBRK_CHARACTER,"",reinterpret_cast<const UChar*>(text.data()),static_cast<int32_t>(text.size()),&error);need(U_SUCCESS(error)&&raw);struct End{UBreakIterator*p;~End(){ubrk_close(p);}}end{raw};std::size_t count{};auto bound=ubrk_first(raw);while(count<maximum){const auto next=ubrk_next(raw);if(next==UBRK_DONE){bound=static_cast<int32_t>(text.size());break;}bound=next;++count;}return readerUTF8(text.substr(0,static_cast<std::size_t>(bound)));}
ReaderXMLDocument parseReaderXML(std::span<const std::uint8_t>b,const ReaderCancel&cancelled){
    checkReaderCancelled(cancelled);need(b.size()<=8*1024*1024);auto text=decode(b);need(text.find(u'\0')==text.npos);auto ascii=readerUTF8(text),uppercase=ascii;std::transform(uppercase.begin(),uppercase.end(),uppercase.begin(),[](unsigned char c){return char(c>='a'&&c<='z'?c-32:c);});need(uppercase.find("<!ENTITY")==uppercase.npos);
    for(std::size_t n=0;(n=ascii.find("&nbsp;",n))!=ascii.npos;){ascii.replace(n,6,"&#160;");n+=6;}if(ascii.starts_with("\xef\xbb\xbf"))ascii.erase(0,3);
    if(ascii.starts_with("<?xml")){const auto end=ascii.find("?>");if(end!=ascii.npos&&readerUTF8(ascii.substr(0,end+2)).size()<=1024){const std::regex encoding(R"(\bencoding\s*=\s*(["'])[^"']*\1)",std::regex_constants::icase);ascii.replace(0,end+2,std::regex_replace(ascii.substr(0,end+2),encoding,"encoding=\"UTF-8\""));}}
    // UTF16 entries may normalize to more bytes. Bound follows the source
    // input size, with at most3 UTF8 bytes per decoded UTF16 unit.
    need(ascii.size()<=24*1024*1024);ComPtr<IStream>stream;stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(ascii.data()),static_cast<UINT>(ascii.size())));need(bool(stream));
    ComPtr<IXmlReader>reader;hr(CreateXmlReader(__uuidof(IXmlReader),reinterpret_cast<void**>(reader.GetAddressOf()),nullptr));hr(reader->SetProperty(XmlReaderProperty_XmlResolver,0));hr(reader->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Parse));hr(reader->SetInput(stream.Get()));
    ReaderXMLDocument out;std::u16string body;unsigned depth{},nodes{};std::optional<unsigned>ignoredDepth,bodyDepth,titleDepth;
    auto flush=[&]{auto value=readerTrim(body);if(!value.empty())out.blocks.push_back({ReaderBlock::Kind::text,std::u16string(value),{}});body.clear();};
    auto endElement=[&]{if(ignoredDepth==depth)ignoredDepth.reset();if(titleDepth==depth)titleDepth.reset();if(bodyDepth==depth){flush();bodyDepth.reset();}need(depth>0);--depth;};
    XmlNodeType type{};HRESULT status{};while((status=reader->Read(&type))==S_OK){checkReaderCancelled(cancelled);
        if(type==XmlNodeType_Element){++depth;++nodes;need(depth<=128&&nodes<=100000&&out.blocks.size()<=8192);const wchar_t*name{};UINT size{};hr(reader->GetLocalName(&name,&size));const auto tag=lowerASCII(readerUTF8(chars(name,size)));std::map<std::string,std::string,std::less<>>attributes;
            for(auto a=reader->MoveToFirstAttribute();a==S_OK;a=reader->MoveToNextAttribute()){const wchar_t*key{},*value{};UINT kl{},vl{};hr(reader->GetQualifiedName(&key,&kl));hr(reader->GetValue(&value,&vl));attributes.emplace(readerUTF8(chars(key,kl)),readerUTF8(chars(value,vl)));}hr(reader->MoveToElement());
            const auto get=[&](std::string_view key)->std::optional<std::string>{auto i=attributes.find(key);return i==attributes.end()?std::nullopt:std::optional<std::string>{i->second};};
            if(tag=="rootfile"&&!out.rootFile)out.rootFile=get("full-path");if(tag=="item"){const auto id=get("id"),href=get("href"),mime=get("media-type");if(id&&href&&mime){need(!out.manifest.contains(*id));out.manifest.emplace(*id,ReaderXMLDocument::Item{*href,*mime});}}
            if(tag=="itemref"){const auto id=get("idref");if(id&&get("linear")!=std::optional<std::string>{"no"})out.spine.push_back(*id);}if(tag=="title"&&!bodyDepth)titleDepth=depth;if(tag=="body")bodyDepth=depth;
            if(!ignoredDepth&&(tag=="script"||tag=="style"||tag=="iframe"||tag=="object"||tag=="audio"||tag=="video"))ignoredDepth=depth;
            if(bodyDepth&&!ignoredDepth){if(!body.empty()&&(tag=="p"||tag=="div"||tag=="br"||tag=="h1"||tag=="h2"||tag=="h3"||tag=="li"||tag=="section"))body+=u'\n';if(tag=="img"||tag=="image"){auto href=get("src");if(!href)href=get("href");if(!href)href=get("xlink:href");if(href){flush();out.blocks.push_back({ReaderBlock::Kind::image,{},*href});}}}
            if(reader->IsEmptyElement())endElement();
        }else if(type==XmlNodeType_EndElement)endElement();else if(type==XmlNodeType_Text||type==XmlNodeType_Whitespace||type==XmlNodeType_CDATA){const wchar_t*value{};UINT size{};hr(reader->GetValue(&value,&size));auto s=chars(value,size);if(titleDepth)out.title+=s;if(bodyDepth&&!ignoredDepth)body+=s;}
    }need(status==S_FALSE&&depth==0);flush();return out;
}
} // namespace endfield::native
#endif
