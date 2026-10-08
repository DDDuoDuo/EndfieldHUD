#include "core/notes_rich_text.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>
using namespace endfield::core;
using namespace endfield::core::notes;
namespace {
std::atomic<std::size_t>allocations{};bool countAllocations{};long failAllocationAfter{-1};unsigned checks{};
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F fn,const char*why){bool caught{};try{fn();}catch(const std::exception&){caught=true;}check(caught,why);}
void select(RichDocument&d,std::uint32_t start,std::uint32_t end){d.setSelection({{start,end},text::ActiveEnd::end,false});}
TextStyle styleAt(const RichDocument&d,std::size_t p){for(const auto&r:d.runs())if(p>=r.location&&p-r.location<r.length)return r.style;return {};}
FormatChange command(FormatKind kind){FormatChange c;c.kind=kind;return c;}
FormatChange size(double value){auto c=command(FormatKind::size);c.fontSize=value;return c;}
FormatChange font(std::string value){auto c=command(FormatKind::font);c.fontName=std::move(value);return c;}
FormatChange color(RGBA value){auto c=command(FormatKind::color);c.color=value;return c;}
void codec(){
    const std::u16string value=u"A😀日本語";RichText rich;TextStyle s;s.fontName="Example Font";s.fontSize=144;s.color=RGBA{.2,.3,.4,.5};s.bold=s.italic=s.underline=s.strikethrough=true;s.extra["futureStyle"]="kept";
    rich.runs={{1,2,s,{{"futureRun",17}}}};rich.extra["futureRoot"]=Json::Array{true,"unchanged"};
    check(rich.validFor(value),"UTF16 emoji range is two units, not UTF8 bytes or a grapheme count");
    const auto json=encodeRichText(rich,value);check(decodeRichText(json,value)==rich,"All source fields and additive root/run/style metadata roundtrip");
    check(json["runs"].array()[0]["length"].integer()==2,"Canonical schema persists UTF16 length");
    check(!json["runs"].array()[0]["style"]["fontName"].isNull(),"Public font identity is preserved without native lookup");
    RichText plain;plain.runs={{0,static_cast<std::uint32_t>(value.size()),{}, {}}};auto normal=encodeRichText(plain,value);
    check(normal["runs"].array()[0]["style"]["fontName"].isNull()&&normal["runs"].array()[0]["style"]["color"].isNull(),"Default font and theme color stay optional");
    check(decodeRichText(normal,value)==plain,"Known defaults are12pt and no traits");
    auto bad=json;bad["version"]=2;rejects([&]{decodeRichText(bad,value);},"Future version is rejected without modifying supplied JSON");check(json["version"].integer()==1,"Failed decode leaves original payload intact");
    bad=json;bad["runs"]=Json::Array{Json::Object{{"location",0},{"length",0},{"style",normal["runs"].array()[0]["style"]}}};rejects([&]{decodeRichText(bad,value);},"Zero-length runs are invalid on Mac");
    rich.runs.push_back({2,1,{}, {}});check(!rich.validFor(value),"Overlapping source ranges reject");rich.runs.pop_back();rich.runs[0].length=100;check(!rich.validFor(value),"Out-of-bounds ranges reject without truncation");
    for(double n:{5.9,144.1,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){TextStyle t;t.fontSize=n;check(!t.valid(),"Saved font size source bounds enforce6–144 finite");}
    TextStyle t;t.fontName=std::string(257,'a');check(!t.valid(),"Font name source UTF8 byte bound enforces256");
    check(!RGBA{-1,0,0,1}.valid()&&!RGBA{0,0,0,1.1}.valid(),"Colors reject out-of-range channels");
    RichText many;many.runs.reserve(RichText::maximumRuns);TextStyle bold;bold.bold=true;const std::u16string large(RichText::maximumRuns*2,u'x');
    for(std::uint32_t n=0;n<RichText::maximumRuns;++n)many.runs.push_back({n*2,1,bold,{}});
    check(many.validFor(large),"Full50000 sparse source runs remain valid, including default gaps");
    const auto packed=encodeRichText(many,large).encode(16*1024*1024);check(packed.size()>4*1024*1024,"Max-run fixture exercises beyond generic4MiB JSON default");
    check(decodeRichText(Json::parse(packed,packed.size()),large)==many,"Caller-bounded JSON roundtrip does not reduce50000-run source contract");
    RichDocument doc(large,many,static_cast<std::uint32_t>(large.size()+1));check(doc.runs().size()==RichText::maximumRuns,"Document keeps all50000 sparse runs without densifying gaps");
    select(doc,1,2);const auto before=doc.revision();rejects([&]{doc.applyFormat(command(FormatKind::italic));},"A50001st source run cannot be silently dropped");
    check(doc.revision()==before&&doc.runs().size()==RichText::maximumRuns&&!doc.canUndo(),"Run-budget rejection preserves original full styles/history");
    many.runs.push_back({static_cast<std::uint32_t>(large.size()),1,bold,{}});check(!many.validFor(large+u'x'),"50001 runs reject explicitly");
}
void formatting(){
    RichDocument doc(u"one TWO three",{},100);select(doc,4,7);const auto originalSelection=doc.selection();const auto before=doc.revision();
    check(doc.applyFormat(command(FormatKind::bold)),"Selected text gains bold");check(doc.selection()==originalSelection&&doc.revision()>before,"Format preserves exact active selection and advances painted revision");
    check(styleAt(doc,4).bold&&!styleAt(doc,0).bold&&!styleAt(doc,8).bold,"Unselected prefixes and suffixes keep their styles");
    select(doc,0,7);check(!doc.selectionStyle().bold,"Menu reports mixed selected trait as false");doc.applyFormat(command(FormatKind::bold));
    check(styleAt(doc,0).bold&&styleAt(doc,6).bold,"Mixed bold selection enables all characters");doc.applyFormat(command(FormatKind::bold));check(!styleAt(doc,0).bold&&!styleAt(doc,6).bold,"All-bold selected range toggles off");
    doc.applyFormat(command(FormatKind::italic));doc.applyFormat(command(FormatKind::underline));doc.applyFormat(command(FormatKind::strikethrough));doc.applyFormat(font("Example Public Family"));
    auto s=styleAt(doc,0);check(s.fontName=="Example Public Family"&&s.italic&&s.underline&&s.strikethrough,"Changing family retains unrelated traits");
    doc.applyFormat(size(1000));check(styleAt(doc,0).fontSize==144,"Menu size clamps upper source bound");doc.applyFormat(size(-10));check(styleAt(doc,0).fontSize==6,"Menu size clamps lower source bound");
    const auto revision=doc.revision();const auto payload=doc.richText();rejects([&]{doc.applyFormat(size(std::numeric_limits<double>::infinity()));},"Nonfinite format request rejects before mutation");check(doc.revision()==revision&&doc.richText()==payload,"Rejected format leaves document/history unchanged");
    select(doc,7,7);const auto original=std::u16string(doc.text());const auto n=doc.stats().undoGroups;const auto rev=doc.revision();
    doc.applyFormat(color({.7,.2,.1,1}));check(doc.text()==original&&doc.revision()==rev&&doc.stats().undoGroups==n,"Collapsed formatting affects pending typing only, not glyphs or undo");
    doc.replace({7,7},u"!");check(styleAt(doc,7).color==RGBA{.7,.2,.1,1},"Inserted characters inherit pending typing style");check(doc.undo()&&doc.text()==original,"Typing undo restores original text");check(doc.typingStyle().color==RGBA{.7,.2,.1,1},"Typing undo retains its original separate pending style");
    RichDocument theme(u"Theme",{},100);select(theme,0,5);theme.applyFormat(color({1,1,1,1}));
    // Source AppKit check: NSColor.white/black.isEqual(NSColor(srgbRed:...))
    // is false, so capture must not erase an explicitly selected sRGB ink.
    check(theme.richText()->runs[0].style.color==RGBA{1,1,1,1},"Explicit sRGB white remains explicit rather than becoming theme-relative");
    theme.applyFormat(color({0,0,0,1}));check(theme.richText()->runs[0].style.color==RGBA{0,0,0,1},"Explicit sRGB black remains explicit when theme ink has identical channels");theme.undo();
    RichText early;TextStyle privateFont;privateFont.fontName=".SFNS-Bold";early.runs={{0,5,privateFont,{}}};RichDocument legacy(u"Theme",early,100);
    check(legacy.richText()->runs.empty(),"Private AppKit system font names normalize through logical system role on capture");
    check(theme.undo()&&!theme.richText(),"Undo to default style preserves originally plain payload absence");
    RichText originallyRich;RichDocument empty(u"",originallyRich,100);check(empty.richText().has_value(),"Originally rich empty note stays a v1 rich note");
}
void metadataAndUndo(){
    TextStyle named;named.fontName="PublicFont";named.bold=true;named.extra["unknownStyle"]="safe";RichText rich;rich.extra["unknownRoot"]=true;rich.runs={{1,3,named,{{"unknownRun",19}}}};
    RichDocument doc(u"ABCDE",rich,100);select(doc,2,3);doc.applyFormat(command(FormatKind::italic));auto formatted=doc.richText();
    check(formatted->extra==rich.extra&&styleAt(doc,1)==named,"Unrelated metadata and styles survive partial format");
    check(styleAt(doc,2).extra==named.extra,"Edited known field preserves additive style metadata");
    for(const auto&r:formatted->runs)check(r.extra.at("unknownRun").integer()==19,"Split fragments retain imported run metadata");
    check(doc.undo()&&doc.richText()==rich,"Formatting undo restores exact original substring/range metadata");check(doc.selection().range==text::Range{2,3},"Formatting undo restores selected range");
    check(doc.redo()&&doc.richText()==formatted,"Formatting redo restores complete known/unknown payload");
    select(doc,1,4);const auto before=std::u16string(doc.text());const auto beforeRich=doc.richText();doc.replace({1,4},u"日本😀");const auto edited=std::u16string(doc.text());const auto editedRich=doc.richText();
    check(doc.undo()&&doc.text()==before&&doc.richText()==beforeRich,"Text undo restores rich UTF16 substring and unrelated metadata");check(doc.redo()&&doc.text()==edited&&doc.richText()==editedRich,"Text redo restores emoji plus inserted styles");
    doc.undo();doc.replace({1,1},u"X");check(!doc.canRedo(),"A fresh accepted edit drops the abandoned redo branch");
    doc.clearHistory();check(!doc.canUndo()&&doc.stats().historyBytes==0,"Explicit history retirement releases payloads");
    RichDocument aliased(u"abcdef",{},100);aliased.replace({2,4},aliased.text().substr(0,3));check(aliased.text()==u"ababcef","Replacement view may alias caller-owned text");check(aliased.undo()&&aliased.text()==u"abcdef","Aliased replacement remains correctly undoable");
}
void transactions(){
    TextStyle bold;bold.bold=true;RichText rich;rich.runs={{0,4,bold,{}}};RichDocument doc(u"ABCD",rich,1000);select(doc,1,3);doc.applyFormat(color({.2,.4,.6,1}));
    const auto oldText=std::u16string(doc.text());const auto oldRich=doc.richText();const auto oldSelection=doc.selection();const auto oldTyping=doc.typingStyle();const auto prior=doc.stats().undoGroups;
    doc.beginInputTransaction();doc.replace({1,3},u"中");doc.beginComposition({1,2});doc.endInputTransaction();
    check(!doc.canUndo()&&doc.stats().undoGroups==prior,"Provisional TSF insertion is not published as an undo group");
    doc.beginInputTransaction();doc.replace({1,2},u"中文😀");doc.updateComposition({1,5});doc.endInputTransaction();
    rejects([&]{doc.applyFormat(command(FormatKind::italic));},"Toolbar cannot mutate a marked-text composition");
    check(doc.endComposition(true).has_value(),"IME cancellation reports reverted change");
    check(doc.text()==oldText&&doc.richText()==oldRich&&doc.selection()==oldSelection&&doc.typingStyle()==oldTyping,"Cancel after insert-before-start restores text/styles/selection/typing snapshot");
    check(doc.stats().undoGroups==prior&&!doc.stats().compositionSnapshot,"Canceled IME adds no undo and releases snapshot");
    doc.beginInputTransaction();doc.replace({1,3},u"中");doc.beginComposition({1,2});doc.endInputTransaction();
    for(unsigned n=0;n<20;++n){doc.beginInputTransaction();doc.replace({1,2},n%2?u"文":u"中");doc.endInputTransaction();}
    doc.endComposition(false);const auto committed=std::u16string(doc.text());const auto committedRich=doc.richText();check(doc.stats().undoGroups==prior+1,"All provisional IME updates commit as one undo group");
    check(doc.undo()&&doc.text()==oldText&&doc.richText()==oldRich&&doc.selection()==oldSelection,"One undo reverses the complete IME transaction");
    check(doc.redo()&&doc.text()==committed&&doc.richText()==committedRich,"One redo reapplies the committed IME transaction");
    RichDocument normal(u"abcd",{},100);normal.beginInputTransaction();select(normal,1,3);normal.replace({1,3},u"12");normal.replace({2,3},u"X");select(normal,0,2);normal.endInputTransaction();
    check(normal.stats().undoGroups==1&&normal.text()==u"a1Xd","Multiple writes in one TSF lock form one undo unit");
    check(normal.undo()&&normal.text()==u"abcd"&&normal.selection().range==text::Range{0,0},"Transaction undo restores selection at lock entry");
    check(normal.redo()&&normal.selection().range==text::Range{0,2},"Transaction redo preserves final post-edit TSF selection");
    normal.beginInputTransaction();select(normal,1,1);normal.beginComposition({1,1});normal.endInputTransaction();normal.endComposition(true);
    check(normal.selection().range==text::Range{0,2},"Composition cancellation restores selection changed before composition announcement");
    RichDocument composing(u"A",{},100);composing.beginComposition({1,1});composing.beginInputTransaction();composing.replace({1,1},u"한");composing.endComposition(false);composing.endInputTransaction();
    check(composing.text()==u"A한"&&composing.undo()&&composing.text()==u"A","Composition commit inside its final TSF lock is safe and reversible");
}
void boundsAndHistory(){
    rejects([]{RichDocument d(u"A",{},0);},"Explicit capacity cannot be zero");rejects([]{RichDocument d(u"ABCDE",{},4);},"Initial oversize text rejects without truncation");
    rejects([]{RichDocument d(u"A",{},std::numeric_limits<std::uint32_t>::max());},"Document capacity must fit signed TSF ACP");
    RichDocument d(u"A😀B",{},5);const auto old=std::u16string(d.text());rejects([&]{d.replace({2,3},u"x");},"Replace cannot split UTF16 surrogate pair");check(d.text()==old,"Surrogate rejection keeps full original text");
    rejects([&]{d.replace({0,0},u"123");},"Capacity failure does not trim inserted text");check(d.text()==old&&!d.canUndo(),"Capacity rejection keeps history untouched");
    d.setReadOnly(true);rejects([&]{d.replace({0,1},u"X");},"Read-only document rejects characters");rejects([&]{d.applyFormat(command(FormatKind::bold));},"Read-only document rejects typing attributes");d.setReadOnly(false);
    RichDocument history(u"",{},1000,{2,1});for(unsigned n=0;n<5;++n)history.replace({static_cast<std::uint32_t>(history.text().size()),static_cast<std::uint32_t>(history.text().size())},u"X");
    check(history.stats().undoGroups==1&&history.stats().historyBytes>1,"One oversized latest group stays undoable instead of truncating input or silently losing that undo");check(history.undo()&&history.text()==u"XXXX","Soft memory budget retains the most recent exact undo");
    RichDocument count(u"",{},1000,{2,100000});for(unsigned n=0;n<5;++n)count.replace({static_cast<std::uint32_t>(count.text().size()),static_cast<std::uint32_t>(count.text().size())},u"X");
    check(count.stats().undoGroups==2&&count.undo()&&count.undo()&&!count.undo()&&count.text()==u"XXX","Configured group retention evicts only oldest committed groups");
    const std::u16string longText(100000,u'x');RichDocument longDoc(longText,{},200000);longDoc.replace({90000,90001},u"日本");check(longDoc.text().size()==100001&&longDoc.undo()&&longDoc.text()==longText,"Portable document has no65536-unit leaf truncation");
    RichDocument noop(u"abc",{},100);noop.replace({1,2},u"b");check(!noop.canUndo()&&noop.revision()==1,"Exact no-op replacement does not allocate a useless undo unit");
}
void independentModel(){
    RichDocument doc(u"abcdefghij",{},4096);std::u16string expected(doc.text());std::vector<TextStyle>styles(expected.size());std::mt19937 rng(8476);
    for(unsigned iteration=0;iteration<500;++iteration){
        const auto start=static_cast<std::uint32_t>(rng()%(expected.size()+1));const auto end=static_cast<std::uint32_t>(start+rng()%(expected.size()-start+1));select(doc,start,end);
        if(rng()%3==0){const std::u16string inserted(rng()%5,char16_t(u'A'+rng()%20));const auto typing=doc.typingStyle();doc.replace({start,end},inserted);
            expected.replace(start,end-start,inserted);styles.erase(styles.begin()+start,styles.begin()+end);styles.insert(styles.begin()+start,inserted.size(),typing);
        }else if(start!=end){const auto kind=static_cast<FormatKind>(rng()%7);auto change=command(kind);change.fontName="Independent family";change.fontSize=(rng()%2)?6:144;change.color={.3,.5,.7,1};
            bool all=true;for(auto n=start;n<end;++n){const auto&s=styles[n];all&=kind==FormatKind::bold?s.bold:kind==FormatKind::italic?s.italic:kind==FormatKind::underline?s.underline:kind==FormatKind::strikethrough?s.strikethrough:false;}
            doc.applyFormat(change);for(auto n=start;n<end;++n){auto&s=styles[n];switch(kind){case FormatKind::font:s.fontName=change.fontName;break;case FormatKind::size:s.fontSize=change.fontSize;break;case FormatKind::color:s.color=change.color;break;case FormatKind::bold:s.bold=!all;break;case FormatKind::italic:s.italic=!all;break;case FormatKind::underline:s.underline=!all;break;case FormatKind::strikethrough:s.strikethrough=!all;break;}}
        }
        check(doc.text()==expected,"Interval edits match independent per-unit reference text");for(std::size_t n=0;n<styles.size();++n)check(styleAt(doc,n)==styles[n],"Sparse UTF16 run transforms match independent per-unit style oracle");
        const auto payload=doc.richText();check(!payload||payload->validFor(doc.text()),"Every accepted randomized edit remains valid source schema");
    }
}
void noReadAllocations(){
    RichDocument doc(u"Plain read-only TSF query",{},1000);select(doc,3,7);doc.applyFormat(font("A public face requiring owned name storage"));const auto rev=doc.revision();const auto before=allocations.load();countAllocations=true;
    for(unsigned n=0;n<50000;++n){doc.beginInputTransaction();const auto value=doc.text();const auto selection=doc.selection();const auto runs=doc.runs();const auto&typing=doc.typingStyle();const auto stats=doc.stats();if(value.empty()||selection.range.end!=7||runs.empty()||!typing.fontName||stats.compositionSnapshot)std::abort();doc.endInputTransaction();}
    countAllocations=false;check(allocations.load()==before&&doc.revision()==rev,"Repeated read-only TSF transactions do not copy text/styles, allocate, or advance layout");
}
void allocationFailures(){
    TextStyle styled;styled.fontName="Public family with a name larger than a short string";styled.bold=true;styled.extra["metadata"]=Json::Array{"preserved","through failure"};
    RichText rich;rich.runs={{0,40,styled,{{"identity","preserved"}}}};const std::u16string initial(80,u'x'),replacement(400,u'y');
    unsigned observed{};
    for(unsigned scenario=0;scenario<4;++scenario){
        bool completed{};
        for(long failure=0;failure<200&&!completed;++failure){
            RichDocument doc(initial,rich,1000);select(doc,5,20);
            if(scenario==2){doc.beginInputTransaction();doc.replace({5,6},u"A");}
            if(scenario==3)doc.applyFormat(command(FormatKind::italic));
            const auto beforeText=std::u16string(doc.text());const auto beforePayload=doc.richText();const auto beforeSelection=doc.selection();const auto beforeRevision=doc.revision();const auto beforeStats=doc.stats();
            failAllocationAfter=failure;bool failed{};
            try{if(scenario==0||scenario==2)doc.replace({5,20},replacement);else if(scenario==1)doc.applyFormat(size(72));else doc.undo();}
            catch(const std::bad_alloc&){failed=true;}
            failAllocationAfter=-1;
            if(failed){++observed;check(doc.text()==beforeText&&doc.richText()==beforePayload&&doc.selection()==beforeSelection&&doc.revision()==beforeRevision,"Allocation failure preserves complete document state");
                const auto after=doc.stats();check(after.undoGroups==beforeStats.undoGroups&&after.redoGroups==beforeStats.redoGroups&&after.historyBytes==beforeStats.historyBytes&&after.pendingChanges==beforeStats.pendingChanges,"Allocation failure does not publish or corrupt staged undo history");
            }else completed=true;
            if(scenario==2){doc.endInputTransaction();if(failed)check(doc.undo()&&doc.text()==initial,"A failed second TSF edit still commits/undoes its successful first edit exactly");}
        }
        check(completed,"Every allocation-fault scenario eventually completes normally");
    }
    check(observed>20,"Fault injection reaches rich style/history and text-growth staging allocations");
}
}
void*operator new(std::size_t n){if(countAllocations)++allocations;if(failAllocationAfter==0){failAllocationAfter=-1;throw std::bad_alloc();}if(failAllocationAfter>0)--failAllocationAfter;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
int main(){try{codec();formatting();metadataAndUndo();transactions();boundsAndHistory();independentModel();noReadAllocations();allocationFailures();std::cout<<"Notes rich-text contracts passed "<<checks<<" checks\n";return 0;}catch(const std::exception&e){countAllocations=false;failAllocationAfter=-1;std::cerr<<"Notes rich-text failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
