#include "modules/calendar_editor_text.hpp"
#include "native/calendar_civil.hpp"
#include <iostream>
#include <stdexcept>
namespace {namespace m=endfield::modules;unsigned checks{};
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&&f,const char*message){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
void unicode(){
 const auto rules=endfield::native::nativeCalendarTextRules();
 const std::string sample="Latin 中文 한국어 日本語 😀 e\xCC\x81 👩‍👩‍👧‍👦\n\r\t";
 const auto encoded=m::calendarEditorUTF16(sample);check(m::calendarEditorUTF8(encoded)==sample,"Calendar UTF8/UTF16 bridge preserves CJK, emoji, combining clusters and source whitespace");
 std::string clusters;for(unsigned n=0;n<121;++n)clusters+="e\xCC\x81";const auto title=m::calendarEditorText(clusters,m::CalendarEditorField::title,rules,false);check(rules.characters(title)==120&&m::calendarEditorUTF16(title).size()==240,"Title prefix counts source Characters rather than UTF16 code units");
 check(m::calendarEditorText(clusters,m::CalendarEditorField::title,rules,true)==clusters,"Marked text remains complete until the same native composition commits");
 check(m::calendarEditorText("one\ntwo\rthree\t",m::CalendarEditorField::title,rules,false)=="one two\rthree\t","Source single-line conversion changes only LF and preserves CR/tab");
 check(m::calendarEditorText("2026-10-04extra",m::CalendarEditorField::date,rules,false)=="2026-10-04","Date uses its exact ten-Character source prefix");
 const std::string mixed="中文😀\nline\r\nnext";check(m::calendarEditorText(mixed,m::CalendarEditorField::details,rules,false)==mixed,"Details preserves all source line separators");
 std::string details;for(unsigned n=0;n<2001;++n)details+="😀";check(rules.characters(m::calendarEditorText(details,m::CalendarEditorField::details,rules,false))==2000,"Details uses the source two-thousand-Character bound without splitting emoji");
 check(m::calendarEditorUTF8(m::calendarEditorUTF16("" )).empty(),"Empty field text round trips exactly");
 rejects([]{(void)m::calendarEditorUTF16(std::string("\xC0\xAF",2));},"Invalid UTF8 cannot enter the source edit transaction");
 rejects([]{(void)m::calendarEditorUTF8(std::u16string{char16_t(0xd800)});},"An isolated surrogate cannot leave the source document");
 rejects([]{(void)m::calendarEditorUTF8(std::u16string{char16_t(0xdc00),char16_t(0xd800)});},"Reversed surrogate code units are rejected atomically");
 rejects([&]{(void)m::calendarEditorText("x",static_cast<m::CalendarEditorField>(99),rules,false);},"Unknown Calendar field cannot silently choose another normalization contract");
 for(const auto scalar:{0u,0x7fu,0x80u,0x7ffu,0x800u,0xd7ffu,0xe000u,0xffffu,0x10000u,0x10ffffu}){std::u16string value;if(scalar<=0xffff)value+=char16_t(scalar);else{const auto n=scalar-0x10000;value+=char16_t(0xd800+(n>>10));value+=char16_t(0xdc00+(n&1023));}check(m::calendarEditorUTF16(m::calendarEditorUTF8(value))==value,"Every Unicode encoding boundary preserves its original UTF16 units");}
}
}
int main(){try{unicode();std::cout<<"PASS "<<checks<<" Calendar editor Unicode checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
