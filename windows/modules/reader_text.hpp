#pragma once
#include "modules/reader_epub.hpp"
namespace endfield::modules {
// Strict scalar conversion used by XML and TXT. BOM UTF16 is endian-aware;
// no replacement/truncation of invalid sequences. GB18030 fallback is native.
std::u16string readerUTF8(std::string_view);
std::string readerUTF8(std::u16string_view);
std::u16string readerUTF16(std::span<const std::uint8_t>);
bool readerValidUTF16(std::u16string_view)noexcept;
std::u16string_view readerChunk(std::u16string_view,std::size_t from);
using ReaderBlocks=std::function<std::shared_ptr<const std::vector<ReaderBlock>>(std::size_t)>;
ReaderLocation readerNormalized(ReaderLocation,std::size_t sectionCount,const ReaderBlocks&);
ReaderLocation readerLocationAt(double,std::size_t sectionCount,const ReaderBlocks&);
std::optional<ReaderLocation>readerAdvance(ReaderLocation,std::size_t blockCount,std::size_t sectionCount);
// Callback shapes the exact suffix at the current width/font/paragraph settings
// and returns measured height. Original binary-search predecessor semantics.
std::optional<ReaderLocation>readerPrevious(ReaderLocation,std::size_t sectionCount,
    const ReaderBlocks&,double availableHeight,const std::function<double(std::u16string_view)>&measure);
} // namespace endfield::modules
