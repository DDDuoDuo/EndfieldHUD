#pragma once
#include "modules/reader_zip.hpp"
#include <deque>

namespace endfield::modules {
struct ReaderBlock {
    enum class Kind {text,image}kind{Kind::text};
    std::u16string text;std::string image;
    bool operator==(const ReaderBlock&)const=default;
};
struct ReaderXMLDocument {
    struct Item {std::string href,type;};
    std::optional<std::string>rootFile;std::map<std::string,Item,std::less<>>manifest;
    std::vector<std::string>spine;std::u16string title;std::vector<ReaderBlock>blocks;
};
// Platform XML reader must implement the original bounded XML-only contract:
// no external entities/network, ENTITY declarations rejected, depth128/nodes
//100,000/blocks8192, script/media subtree exclusion, UTF8 or BOM UTF16 input.
using ReaderXMLParser=std::function<ReaderXMLDocument(std::span<const std::uint8_t>,const ReaderCancel&)>;
class ReaderEPUB final {
public:
    ReaderEPUB(std::shared_ptr<ReaderBytes>,ReaderXMLParser,ReaderCancel={});
    const std::u16string&title()const noexcept{return title_;}
    std::span<const std::string>spine()const noexcept{return spine_;}
    std::shared_ptr<const std::vector<ReaderBlock>>blocks(std::size_t,const ReaderCancel& cancelled={});
    const ReaderZIP&zip()const noexcept{return zip_;}
private:
    ReaderZIP zip_;ReaderXMLParser parse_;std::u16string title_;std::vector<std::string>spine_;
    std::deque<std::pair<std::size_t,std::shared_ptr<const std::vector<ReaderBlock>>>>cache_;
};
// Foundation's whitespace/newline scalar set; deliberately does not strip
// arbitrary spaces inside text or use the current C locale.
bool readerWhitespace(char16_t)noexcept;
std::u16string_view readerTrim(std::u16string_view)noexcept;
} // namespace endfield::modules
