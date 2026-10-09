#pragma once
#include "modules/reader_text.hpp"
namespace endfield::native {
// Windows native codecs only. XmlLite resolver is explicitly null; no remote
// source, CSS/HTML renderer, entity expansion, script or disk extraction.
modules::ReaderXMLDocument parseReaderXML(std::span<const std::uint8_t>,const modules::ReaderCancel& cancelled={});
std::u16string decodeReaderTXT(std::span<const std::uint8_t>);
std::string readerGraphemePrefix(std::u16string_view,std::size_t maximum);
} // namespace endfield::native
