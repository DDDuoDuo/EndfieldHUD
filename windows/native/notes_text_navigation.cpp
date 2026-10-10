#include "native/notes_text_navigation.hpp"
#include <algorithm>
#include <cmath>
#include <atomic>
#include <type_traits>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::native {
namespace {
namespace text=core::text;
bool lineBreak(char16_t c){return c==u'\n'||c==u'\r'||c==u'\x2029';}
bool high(char16_t c){return c>=0xd800&&c<=0xdbff;}
bool low(char16_t c){return c>=0xdc00&&c<=0xdfff;}
std::atomic<bool> forceFallback{};

// The OS ICU C API (icu.dll since Windows 10 1903; icuuc.dll before), loaded
// once from System32 only. Unversioned exports; UChar is UTF-16.
struct Icu {
    using Open=void*(*)(int,const char*,const char16_t*,std::int32_t,int*);
    using Close=void(*)(void*);using First=std::int32_t(*)(void*);using Move=std::int32_t(*)(void*,std::int32_t);using Next=std::int32_t(*)(void*);using Status=std::int32_t(*)(void*);
    Open open{};Close close{};First first{};Next next{};Move following{},preceding{};Status status{};
    bool ready()const noexcept{return open&&close&&first&&next&&following&&preceding&&status;}
};
const Icu& icu(){
    static const Icu value=[]{Icu v;HMODULE module=LoadLibraryExW(L"icu.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)module=LoadLibraryExW(L"icuuc.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)return v;
        auto load=[&](auto& target,const char* name){target=reinterpret_cast<std::remove_reference_t<decltype(target)>>(reinterpret_cast<void*>(GetProcAddress(module,name)));};
        load(v.open,"ubrk_open");load(v.close,"ubrk_close");load(v.first,"ubrk_first");load(v.next,"ubrk_next");load(v.following,"ubrk_following");load(v.preceding,"ubrk_preceding");load(v.status,"ubrk_getRuleStatus");
        if(!v.ready())v=Icu{};return v;}();
    return value;
}
bool useIcu(){return !forceFallback.load()&&icu().ready();}
struct Breaker {
    void* handle{};
    Breaker(int type,std::u16string_view value){int error{};handle=icu().open(type,"",value.data(),static_cast<std::int32_t>(value.size()),&error);if(error>0&&handle){icu().close(handle);handle=nullptr;}}
    ~Breaker(){if(handle)icu().close(handle);}
    Breaker(const Breaker&)=delete;Breaker&operator=(const Breaker&)=delete;
};
constexpr int characterBreak=0,wordBreak=1;
constexpr std::int32_t done=-1;

enum class Kind {space,word,other};
struct Segment {std::uint32_t start,end;Kind kind;};
// Fallback classes: whitespace, letters/digits (a run), one ideograph per
// segment, kana runs, other symbols/punctuation runs. Marks join their base.
enum class Class {space,alnum,ideograph,hiragana,katakana,other,join};
Class classify(std::u16string_view t,std::size_t i,std::size_t& width){
    std::uint32_t c=t[i];width=1;
    if(high(t[i])&&i+1<t.size()&&low(t[i+1])){c=0x10000+((c-0xd800)<<10)+(t[i+1]-0xdc00);width=2;}
    if(c==0x200d||(c>=0xfe00&&c<=0xfe0f)||(c>=0x1f3fb&&c<=0x1f3ff)||(c>=0xe0100&&c<=0xe01ef))return Class::join;
    if((c>=0x4e00&&c<=0x9fff)||(c>=0x3400&&c<=0x4dbf)||(c>=0xf900&&c<=0xfaff)||(c>=0x20000&&c<=0x3134f))return Class::ideograph;
    if(c>=0x3041&&c<=0x309f)return Class::hiragana;if((c>=0x30a0&&c<=0x30ff)||(c>=0x31f0&&c<=0x31ff))return Class::katakana;
    if(width==2)return Class::other;
    WORD type1{},type3{};const wchar_t unit=static_cast<wchar_t>(c);GetStringTypeW(CT_CTYPE1,&unit,1,&type1);GetStringTypeW(CT_CTYPE3,&unit,1,&type3);
    if(type3&(C3_NONSPACING|C3_DIACRITIC|C3_VOWELMARK))return Class::join;
    if(type1&(C1_SPACE|C1_BLANK))return Class::space;
    if((type1&(C1_ALPHA|C1_DIGIT))||c==u'_')return Class::alnum;
    return Class::other;
}
std::vector<Segment> fallbackSegments(std::u16string_view t){
    std::vector<Segment> out;Class previous{Class::space};
    for(std::size_t i=0;i<t.size();){std::size_t width{};auto c=classify(t,i,width);
        const bool joins=!out.empty()&&(c==Class::join||(c==previous&&c!=Class::ideograph));
        if(joins)out.back().end=static_cast<std::uint32_t>(i+width);
        else{if(c==Class::join)c=Class::other;out.push_back({static_cast<std::uint32_t>(i),static_cast<std::uint32_t>(i+width),c==Class::space?Kind::space:c==Class::other?Kind::other:Kind::word});previous=c;}
        i+=width;}
    return out;
}
bool allSpace(std::u16string_view t){for(const auto c:t){WORD type{};const wchar_t unit=static_cast<wchar_t>(c);GetStringTypeW(CT_CTYPE1,&unit,1,&type);if(!(type&(C1_SPACE|C1_BLANK)))return false;}return !t.empty();}
// Segments of one paragraph (no line breaks inside).
std::vector<Segment> segments(std::u16string_view t){
    if(t.empty())return {};if(!useIcu())return fallbackSegments(t);
    Breaker b(wordBreak,t);if(!b.handle)return fallbackSegments(t);std::vector<Segment> out;
    std::int32_t start=icu().first(b.handle);for(std::int32_t end=icu().next(b.handle);end!=done;start=end,end=icu().next(b.handle)){
        const auto status=icu().status(b.handle);const auto piece=t.substr(static_cast<std::size_t>(start),static_cast<std::size_t>(end-start));
        out.push_back({static_cast<std::uint32_t>(start),static_cast<std::uint32_t>(end),status>=100?Kind::word:allSpace(piece)?Kind::space:Kind::other});}
    return out;
}
struct Paragraph {std::uint32_t start,end;}; // [start,end) excludes the break
Paragraph paragraph(std::u16string_view t,std::uint32_t at){
    std::uint32_t start=at,end=at;while(start>0&&!lineBreak(t[start-1]))--start;while(end<t.size()&&!lineBreak(t[end]))++end;return {start,end};
}
std::uint32_t breakLength(std::u16string_view t,std::uint32_t at){return at<t.size()&&lineBreak(t[at])?(t[at]==u'\r'&&at+1<t.size()&&t[at+1]==u'\n'?2u:1u):0u;}
const Segment* containing(const std::vector<Segment>& s,std::uint32_t local){for(const auto& v:s)if(local>=v.start&&local<v.end)return &v;return nullptr;}
text::Selection moved(const text::Document& d,std::uint32_t target,bool extend){
    const auto s=d.selection();const auto anchor=s.activeEnd==text::ActiveEnd::start?s.range.end:s.range.start;
    if(!extend)return {{target,target},text::ActiveEnd::end,false};
    return target<anchor?text::Selection{{target,anchor},text::ActiveEnd::start,false}:text::Selection{{anchor,target},text::ActiveEnd::end,false};
}
std::uint32_t activePosition(const text::Document& d){const auto s=d.selection();return s.activeEnd==text::ActiveEnd::start?s.range.start:s.range.end;}
} // namespace

NotesWordEngine notesWordEngine()noexcept{return useIcu()?NotesWordEngine::icu:NotesWordEngine::fallback;}
void notesUseFallbackWords(bool value)noexcept{forceFallback=value;}

std::uint32_t notesNextWordStart(std::u16string_view t,std::uint32_t at){
    at=std::min<std::uint32_t>(at,static_cast<std::uint32_t>(t.size()));if(at==t.size())return at;
    const auto p=paragraph(t,at);if(at>=p.end)return at+breakLength(t,at); // a paragraph end moves to the next paragraph
    const auto s=segments(t.substr(p.start,p.end-p.start));const auto* current=containing(s,at-p.start);if(!current)return p.end;
    auto target=current->end;for(const auto& v:s)if(v.start==target&&v.kind==Kind::space)target=v.end;
    return p.start+target;
}
std::uint32_t notesPreviousWordStart(std::u16string_view t,std::uint32_t at){
    at=std::min<std::uint32_t>(at,static_cast<std::uint32_t>(t.size()));if(at==0)return 0;
    const auto p=paragraph(t,at);if(at==p.start){const auto before=at-1;return before>0&&t[before]==u'\n'&&t[before-1]==u'\r'?before-1:before;} // to the previous paragraph's end
    const auto s=segments(t.substr(p.start,p.end-p.start));const auto local=at-p.start;
    std::size_t index=s.size();for(std::size_t i=0;i<s.size();++i)if(local-1>=s[i].start&&local-1<s[i].end){index=i;break;}if(index==s.size())return p.start;
    if(s[index].kind==Kind::space&&index>0)--index;return p.start+s[index].start;
}
text::Range notesWordAt(std::u16string_view t,std::uint32_t at){
    if(t.empty())return {0,0};at=std::min<std::uint32_t>(at,static_cast<std::uint32_t>(t.size()));const auto p=paragraph(t,at);
    if(at>=p.end){if(at>p.start)at=p.end-1;else return {at,at};} // the click landed on a line end: the word before it
    const auto s=segments(t.substr(p.start,p.end-p.start));const auto* v=containing(s,at-p.start);if(!v)return {at,at};return {p.start+v->start,p.start+v->end};
}
text::Range notesParagraphAt(std::u16string_view t,std::uint32_t at){
    at=std::min<std::uint32_t>(at,static_cast<std::uint32_t>(t.size()));const auto p=paragraph(t,at);return {p.start,p.end+breakLength(t,p.end)};
}
std::uint32_t notesNextGrapheme(std::u16string_view t,std::uint32_t at){
    if(at>=t.size())return static_cast<std::uint32_t>(t.size());
    if(t[at]==u'\r'&&at+1<t.size()&&t[at+1]==u'\n')return at+2;if(lineBreak(t[at]))return at+1;
    const auto p=paragraph(t,at);const auto piece=t.substr(p.start,p.end-p.start);
    if(useIcu()){Breaker b(characterBreak,piece);if(b.handle){const auto next=icu().following(b.handle,static_cast<std::int32_t>(at-p.start));if(next!=done)return p.start+static_cast<std::uint32_t>(next);}}
    std::size_t width{};classify(piece,at-p.start,width);auto next=at-p.start+static_cast<std::uint32_t>(width);
    while(next<piece.size()){std::size_t w{};if(classify(piece,next,w)!=Class::join)break;next+=static_cast<std::uint32_t>(w);}return p.start+next;
}
std::uint32_t notesPreviousGrapheme(std::u16string_view t,std::uint32_t at){
    at=std::min<std::uint32_t>(at,static_cast<std::uint32_t>(t.size()));if(at==0)return 0;
    if(t[at-1]==u'\n'&&at>=2&&t[at-2]==u'\r')return at-2;if(lineBreak(t[at-1]))return at-1;
    const auto p=paragraph(t,at-1);const auto piece=t.substr(p.start,p.end-p.start);const auto local=at-p.start;
    if(useIcu()){Breaker b(characterBreak,piece);if(b.handle){const auto previous=icu().preceding(b.handle,static_cast<std::int32_t>(local));if(previous!=done)return p.start+static_cast<std::uint32_t>(previous);}}
    std::uint32_t start{};for(std::uint32_t i=0;i<local;){std::size_t w{};const auto c=classify(piece,i,w);if(c!=Class::join)start=i;i+=static_cast<std::uint32_t>(w);}return p.start+start;
}

ProjectedEditorResult notesNavigate(NativeNotesEditor& editor,const text::Document& d,NotesNavigation kind,bool extend){
    if(d.composition())return {true,false,false};const auto t=d.text();const auto from=activePosition(d);
    const auto target=kind==NotesNavigation::nextWord?notesNextWordStart(t,from):notesPreviousWordStart(t,from);
    const auto next=moved(d,target,extend);if(next==d.selection())return {true,false,false};return editor.setSelectionFromHost(next);
}
ProjectedEditorResult notesDeleteWord(NativeNotesEditor& editor,text::Document& d,bool forward){
    if(d.composition()||d.readOnly())return {true,false,false};auto range=d.selection().range;
    if(range.start==range.end){const auto t=d.text();range=forward?text::Range{range.start,notesNextWordStart(t,range.start)}:text::Range{notesPreviousWordStart(t,range.start),range.start};}
    if(range.start==range.end)return {true,false,false};return editor.replaceTextFromHost(range,u"");
}
ProjectedEditorResult notesSelectRange(NativeNotesEditor& editor,const text::Document& d,text::Range range){
    if(d.composition()||range.start>range.end||range.end>d.text().size())return {true,false,false};const text::Selection next{range,text::ActiveEnd::end,false};
    if(next==d.selection())return {true,false,false};return editor.setSelectionFromHost(next);
}
ProjectedEditorResult notesNavigationKey(NativeNotesEditor& editor,text::Document& d,unsigned key,bool control,bool shift){
    if(!control)return {};
    if(key==VK_LEFT||key==VK_RIGHT)return notesNavigate(editor,d,key==VK_LEFT?NotesNavigation::previousWord:NotesNavigation::nextWord,shift);
    if((key==VK_BACK||key==VK_DELETE)&&!shift)return notesDeleteWord(editor,d,key==VK_DELETE);
    return {};
}
unsigned NotesClickCount::press(bool doubleClick,core::Point p,double seconds)noexcept{
    if(doubleClick){armed_=true;time_=seconds;point_=p;return 2;}
    const bool triple=armed_&&seconds>=time_&&seconds-time_<=GetDoubleClickTime()/1000.&&std::abs(p.x-point_.x)*2<=GetSystemMetrics(SM_CXDOUBLECLK)&&std::abs(p.y-point_.y)*2<=GetSystemMetrics(SM_CYDOUBLECLK);
    armed_=false;return triple?3:1;
}
} // namespace endfield::native
