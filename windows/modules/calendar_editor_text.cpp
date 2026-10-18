#include "modules/calendar_editor_text.hpp"
#include "core/text_input.hpp"
#include <algorithm>
namespace endfield::modules {
namespace {void need(bool b){if(!b)throw std::invalid_argument("Invalid Calendar editor text");}}
std::string calendarEditorText(std::string_view text,CalendarEditorField field,const CalendarTextRules&rules,bool marked){
    need(CalendarJson::validUtf8(text)&&text.size()<=calendarMaximumBytes&&bool(rules.prefix));
    need(field==CalendarEditorField::title||field==CalendarEditorField::date||field==CalendarEditorField::details);
    if(marked)return std::string(text);
    auto value=rules.prefix(text,field==CalendarEditorField::title?120:field==CalendarEditorField::date?10:2000);
    if(field!=CalendarEditorField::details)std::replace(value.begin(),value.end(),'\n',' ');
    return value;
}
std::u16string calendarEditorUTF16(std::string_view text){
    need(CalendarJson::validUtf8(text)&&text.size()<=calendarMaximumBytes);std::u16string value;value.reserve(text.size());
    for(std::size_t n=0;n<text.size();){const auto first=static_cast<unsigned char>(text[n++]);std::uint32_t scalar=first;unsigned continuation{};
        if(first>=0xf0){scalar=first&7;continuation=3;}else if(first>=0xe0){scalar=first&15;continuation=2;}else if(first>=0xc0){scalar=first&31;continuation=1;}
        while(continuation--)scalar=(scalar<<6)|(static_cast<unsigned char>(text[n++])&63);
        if(scalar<=0xffff)value.push_back(static_cast<char16_t>(scalar));else{scalar-=0x10000;value.push_back(static_cast<char16_t>(0xd800+(scalar>>10)));value.push_back(static_cast<char16_t>(0xdc00+(scalar&1023)));}
    }return value;
}
std::string calendarEditorUTF8(std::u16string_view text){
    need(text.size()<=calendarMaximumBytes&&core::text::Buffer::validUTF16(text));std::string value;value.reserve(text.size());
    for(std::size_t n=0;n<text.size();++n){std::uint32_t scalar=text[n];if(scalar>=0xd800&&scalar<=0xdbff)scalar=0x10000+((scalar-0xd800)<<10)+(text[++n]-0xdc00);
        if(scalar<0x80)value.push_back(static_cast<char>(scalar));else if(scalar<0x800){value.push_back(static_cast<char>(0xc0|(scalar>>6)));value.push_back(static_cast<char>(0x80|(scalar&63)));}
        else if(scalar<0x10000){value.push_back(static_cast<char>(0xe0|(scalar>>12)));value.push_back(static_cast<char>(0x80|((scalar>>6)&63)));value.push_back(static_cast<char>(0x80|(scalar&63)));}
        else{value.push_back(static_cast<char>(0xf0|(scalar>>18)));value.push_back(static_cast<char>(0x80|((scalar>>12)&63)));value.push_back(static_cast<char>(0x80|((scalar>>6)&63)));value.push_back(static_cast<char>(0x80|(scalar&63)));}
    }need(value.size()<=calendarMaximumBytes);return value;
}
}
