#pragma once
#include "modules/archive_model.hpp"
#include <array>
namespace endfield::native {
// Windows 10 1903+ installed ICU, shared with Event Log's Unicode dependency.
// Stateless bounded calls may execute on the caller's UI/file-executor thread;
// there is no worker, native window, font lookup or retained document buffer.
// Foundation's pinned whitespace/newline scalar set is explicit. Folded keys
// use NFC to reproduce Swift String's canonical-equivalent equality. Installed
// Unicode-version differences for newly introduced graphemes remain explicit.
modules::ArchiveTextRules nativeArchiveTextRules();
std::array<std::uint8_t,4>archiveUnicodeVersion()noexcept;
}
