#pragma once
#include "modules/reader_model.hpp"
namespace endfield::modules {
// Exact generated Foundation codec table selected by unchanged Mac Reader.
// Stateless byte sequences, no locale/installed-codec dependence or replacement
// characters. UTF8/UTF16 precedence and NUL/empty checks remain the TXT owner's.
std::u16string readerGB18030(std::span<const std::uint8_t>);
}
