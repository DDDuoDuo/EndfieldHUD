#include "modules/reader_epub.hpp"
#include "modules/reader_text.hpp"
#include <algorithm>

namespace endfield::modules {
namespace {void need(bool value){if(!value)throw ReaderError(ReaderErrorCode::invalidBook);}bool imagePath(std::string_view path){auto dot=path.rfind('.');if(dot==path.npos)return false;std::string ext(path.substr(dot+1));std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(c>='A'&&c<='Z'?c+32:c);});return ext=="jpg"||ext=="jpeg"||ext=="png"||ext=="gif"||ext=="webp";}}
bool readerWhitespace(char16_t c)noexcept{return (c>=9&&c<=13)||c==0x20||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200b)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000;}
std::u16string_view readerTrim(std::u16string_view text)noexcept{while(!text.empty()&&readerWhitespace(text.front()))text.remove_prefix(1);while(!text.empty()&&readerWhitespace(text.back()))text.remove_suffix(1);return text;}
ReaderEPUB::ReaderEPUB(std::shared_ptr<ReaderBytes>file,ReaderXMLParser parse,ReaderCancel cancelled):zip_(std::move(file),cancelled),parse_(std::move(parse)){
    need(bool(parse_));const auto mime=zip_.data("mimetype",100,cancelled);const auto decodedMime=readerUTF8(std::string_view(reinterpret_cast<const char*>(mime.data()),mime.size()));need(readerTrim(decodedMime)==u"application/epub+zip");
    const auto container=parse_(zip_.data("META-INF/container.xml",256*1024,cancelled),cancelled);need(container.rootFile&&ReaderZIP::safePath(*container.rootFile));
    const auto package=parse_(zip_.data(*container.rootFile,2*1024*1024,cancelled),cancelled);need(!package.spine.empty()&&package.spine.size()<=4096);spine_.reserve(package.spine.size());
    for(const auto&id:package.spine){checkReaderCancelled(cancelled);const auto i=package.manifest.find(id);need(i!=package.manifest.end());const auto&type=i->second.type;need(type=="application/xhtml+xml"||type=="text/html"||type=="image/jpeg"||type=="image/png"||type=="image/webp"||type=="image/gif");auto path=ReaderZIP::resolve(i->second.href,*container.rootFile);need(zip_.entries().contains(path));spine_.push_back(std::move(path));}title_=readerTrim(package.title);
}
std::shared_ptr<const std::vector<ReaderBlock>>ReaderEPUB::blocks(std::size_t section,const ReaderCancel&cancelled){
    need(section<spine_.size());checkReaderCancelled(cancelled);for(const auto&cached:cache_)if(cached.first==section)return cached.second;
    const auto&path=spine_[section];std::vector<ReaderBlock>result;
    if(imagePath(path))result.push_back({ReaderBlock::Kind::image,{},path});else{auto parsed=parse_(zip_.data(path,8*1024*1024,cancelled),cancelled);result=std::move(parsed.blocks);for(auto&block:result)if(block.kind==ReaderBlock::Kind::image)block.image=ReaderZIP::resolve(block.image,path);}
    need(!result.empty());auto owned=std::make_shared<const std::vector<ReaderBlock>>(std::move(result));cache_.emplace_back(section,owned);if(cache_.size()>2)cache_.pop_front();return owned;
}
} // namespace endfield::modules
