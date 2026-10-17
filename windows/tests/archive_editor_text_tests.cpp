#include "native/archive_editor_text.hpp"
#include "native/archive_text_rules.hpp"
#include <iostream>
#include <stdexcept>

namespace {unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}template<class F>void rejects(F&&f,const char*why){bool failure{};try{f();}catch(const std::exception&){failure=true;}check(failure,why);}}
int main(){try{namespace n=endfield::native;namespace m=endfield::modules;namespace c=endfield::core;using K=n::ArchiveEditorTextKind;
 for(auto kind:{K::title,K::body,K::category}){auto empty=n::clampArchiveEditorText({},kind);check(empty.text.empty()&&empty.removed.empty(),"Empty source field is preserved");auto unchanged=n::clampArchiveEditorText(u"中文 / 한국어 / 日本語 🐱 é",kind);check(unchanged.removed.empty(),"CJK, surrogate and combining input below source limit remains byte-identical");}
 std::u16string grapheme=u"👩🏽‍🚀";std::u16string longText;for(unsigned i=0;i<201;++i)longText+=grapheme;auto title=n::clampArchiveEditorText(longText,K::title);check(n::nativeArchiveTextRules().characters(m::archiveUTF8(title.text))==200&&title.removed.size()==1&&title.removed[0].start==200*grapheme.size(),"Title uses source 200 extended graphemes rather than UTF16 units");
 auto category=n::clampArchiveEditorText(longText,K::category);check(n::nativeArchiveTextRules().characters(m::archiveUTF8(category.text))==40,"Category editor uses source 40 graphemes separately from storage's 80");
 std::u16string withNUL=u"A";withNUL.push_back(0);withNUL+=u"中";withNUL.push_back(0);withNUL+=u"B";auto stripped=n::clampArchiveEditorText(withNUL,K::title);check(stripped.text==u"A中B"&&stripped.removed==std::vector<c::text::Range>{{3,4},{1,2}},"NUL removals report descending original UTF16 ranges");
 c::notes::RichText payload;payload.runs.push_back({0,5,c::notes::TextStyle{.fontSize=48,.bold=true},{}});c::notes::RichDocument document(withNUL,payload,65536);for(const auto range:stripped.removed)document.replace(range,{});check(document.text()==stripped.text&&document.runs().size()==1&&document.runs()[0].length==3&&document.runs()[0].style.bold,"Clamp edits preserve unaffected original rich style instead of flattening");
 auto bodyNUL=n::clampArchiveEditorText(withNUL,K::body);check(bodyNUL.text==withNUL&&bodyNUL.removed.empty(),"Body preserves NUL because the source only strips title/category");
 std::u16string body(m::archiveMaximumBodyBytes-2,u'a');body+=u"🐱後";auto prefix=n::clampArchiveEditorText(body,K::body);check(prefix.text.size()==m::archiveMaximumBodyBytes-2&&m::archiveUTF8(prefix.text).size()<=m::archiveMaximumBodyBytes&&prefix.removed.size()==1,"Body byte prefix never splits a multi-byte scalar/surrogate");
 auto invalid=std::u16string(1,char16_t(0xd800));rejects([&]{n::clampArchiveEditorText(invalid,K::title);},"Invalid UTF16 fails without fabricating replacement glyphs");
 std::cout<<"PASS "<<checks<<" Archive source edit-clamp checks\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
