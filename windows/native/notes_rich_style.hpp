#pragma once
#include "core/notes_rich_text.hpp"
#include <array>
namespace endfield::native {
// One descriptor mapping for settled Notes shaping/paint. Public names remain
// explicit; source-private system fonts use the configured native fallback.
inline ehud::data::Json notesRunFont(const core::notes::TextStyle&s){
    using J=ehud::data::Json;const bool named=s.fontName&&!s.fontName->starts_with(".");
    return J::Object{{"familyName",named?*s.fontName:".AppleSystemUIFont"},{"postScriptName",named?*s.fontName:".SFNS-Regular"},
        {"pointSize",s.fontSize},{"symbolicTraits",(s.bold?2:0)|(s.italic?1:0)},{"preserveUserFont",named}};
}
inline ehud::data::Json notesRunDescriptor(const core::notes::TextRun&r,const std::array<double,4>&theme){
    using J=ehud::data::Json;const auto&s=r.style;const auto c=s.color?std::array<double,4>{s.color->red,s.color->green,s.color->blue,s.color->alpha}:theme;
    return J::Object{{"utf16Range",J::Array{double(r.location),double(r.length)}},{"attributes",J::Object{
        {"NSFont",notesRunFont(s)},{"NSColor",J::Object{{"sRGB",J::Array{c[0],c[1],c[2],c[3]}}}},
        {"NSUnderline",s.underline?1:0},{"NSStrikethrough",s.strikethrough?1:0}}}};
}
}
