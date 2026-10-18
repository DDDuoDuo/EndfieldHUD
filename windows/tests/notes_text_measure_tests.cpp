#include "native/notes_text_measure.hpp"
#include "core/notes_rich_text.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace endfield::native;
namespace core=endfield::core;
namespace {
std::size_t checks{};
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char*why){bool failed{};try{f();}catch(const std::exception&){failed=true;}check(failed,why);}
bool boundary(std::string_view value,std::size_t at){return at==value.size()||(at<value.size()&&(static_cast<unsigned char>(value[at])&0xc0)!=0x80);}
void complete(const NativeNotesTextMeasurement&m,std::string_view original){
    check(m.measured.text==original,"Complete input text retained exactly");check(!m.measured.lines.empty(),"Source index has at least one line");
    std::size_t cursor{};double y{};
    for(const auto&line:m.measured.lines){check(line.begin==cursor&&line.end>=line.begin&&line.visibleTextEnd>=line.begin&&line.visibleTextEnd<=line.end,"Line ranges are contiguous and bounded");
        check(boundary(original,line.begin)&&boundary(original,line.end)&&boundary(original,line.visibleTextEnd),"Line endpoints are actual UTF-8 scalar boundaries");
        check(line.y==y&&line.height==m.font.lineHeight&&line.height>0,"Source plain line height and origins are consistent");cursor=line.end;y+=line.height;}
    check(cursor==original.size()&&y==m.measured.height,"Line index covers full original bytes and height");
}
bool sameIndex(const NativeNotesTextMeasurement&a,const NativeNotesTextMeasurement&b){
    if(a.measured.text!=b.measured.text||a.measured.height!=b.measured.height||a.measured.lines.size()!=b.measured.lines.size()||a.font.lineHeight!=b.font.lineHeight)return false;
    for(std::size_t i=0;i<a.measured.lines.size();++i){const auto&x=a.measured.lines[i];const auto&y=b.measured.lines[i];
        if(x.begin!=y.begin||x.end!=y.end||x.visibleTextEnd!=y.visibleTextEnd||x.y!=y.y||x.height!=y.height||x.utf16Begin!=y.utf16Begin||x.utf16VisibleEnd!=y.utf16VisibleEnd)return false;}
    return true;
}
std::string utf8(std::u16string_view value){
    if(value.empty())return {};const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(value.data()),int(value.size()),nullptr,0,nullptr,nullptr);
    std::string out(std::size_t(n),'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(value.data()),int(value.size()),out.data(),n,nullptr,nullptr);return out;
}
// A new revision of a long note re-measures only the paragraphs whose bytes
// (or rich UTF-16 runs) changed. Every incremental index must equal a complete
// measurement of the same text by an independent measurer, line for line.
void incremental(){
    LayerRasterizer raster;NativeNotesTextMeasurer measure(raster);
    std::string text;for(unsigned i=0;i<900;++i){text+=std::to_string(i)+" 终末地 Endfield 日本語 한국어 😀 e\xCC\x81 wraps across this narrow note viewport";text+=i%7==3?"\r\n":i%11==5?"\r":"\n";if(i%13==0)text+="\n";}
    check(text.size()>65536,"Incremental fixture is a long multilingual note");
    std::uint64_t revision=1;(void)measure.measure("inc",revision,text,190);
    auto expect=[&](std::string next,const char*why,bool reuse){
        const auto before=measure.stats();const auto incrementalIndex=measure.measure("inc",++revision,next,190);const auto after=measure.stats();
        NativeNotesTextMeasurer fresh(raster);const auto full=fresh.measure("full",1,next,190);
        check(sameIndex(*incrementalIndex,*full),why);complete(*incrementalIndex,next);check(after.measurements==before.measurements+1,"Incremental index is one counted measurement");
        if(reuse&&!(after.incrementalMeasurements==before.incrementalMeasurements+1&&after.reusedLines>before.reusedLines+900&&after.analysisLayoutsCreated-before.analysisLayoutsCreated<=4&&fresh.stats().analysisLayoutsCreated>800))
            std::cerr<<why<<": incremental "<<after.incrementalMeasurements-before.incrementalMeasurements<<", reused "<<after.reusedLines-before.reusedLines<<" of "<<full->measured.lines.size()<<", layouts "<<after.analysisLayoutsCreated-before.analysisLayoutsCreated<<" vs "<<fresh.stats().analysisLayoutsCreated<<'\n';
        if(reuse)check(after.incrementalMeasurements==before.incrementalMeasurements+1&&after.reusedLines>before.reusedLines+900&&after.analysisLayoutsCreated-before.analysisLayoutsCreated<=4&&fresh.stats().analysisLayoutsCreated>800,"Edit re-measures only its own paragraphs");
        text=std::move(next);
    };
    auto edited=[&](std::size_t at,std::size_t erase,std::string_view insert){auto next=text;next.replace(at,erase,insert);return next;};
    expect(edited(text.find(" Endfield",text.size()/2),0,"X"),"Typing inside a middle paragraph equals a complete index",true);
    expect(edited(text.find(" 日本語",text.size()/3),0,"\xF0\x9F\x98\x80"),"Supplementary scalar typing equals a complete index",true);
    expect(edited(text.find('\n',text.size()/3),1,""),"Joining two paragraphs equals a complete index",true);
    expect(edited(text.find(" wraps",text.size()/4),0,"\n"),"Splitting a paragraph equals a complete index",true);
    {std::size_t cr=text.find('\r');while(cr!=std::string::npos&&cr+1<text.size()&&text[cr+1]=='\n')cr=text.find('\r',cr+1);check(cr!=std::string::npos,"Fixture has a lone CR");
     expect(edited(cr+1,0,"\n"),"Lone CR becoming CRLF re-measures its paragraph",true);}
    {const auto crlf=text.find("\r\n",text.size()/2);expect(edited(crlf+1,1,""),"CRLF becoming a lone CR re-measures its paragraph",true);}
    expect(edited(0,0,"Start "),"Typing at the start equals a complete index",true);
    expect(text+" end","Appending to the last paragraph equals a complete index",true);
    expect(text+"\n","A new final blank line equals a complete index",true);
    expect(text.substr(0,text.size()-1),"Removing the final newline equals a complete index",true);
    {const auto a=text.find('\n',text.size()/5),b=text.find('\n',text.size()/5+4000);expect(edited(a,b-a,"\xE2\x80\xA8replaced"),"Replacing many paragraphs with a Unicode separator equals a complete index",true);}
    expect(edited(text.find('\n',text.size()/2),0,"\xC2\x85"),"Inserting NEL before a newline equals a complete index",true);
    expect("Entirely different","Unrelated text equals a complete index",false);
    // Rich: runs shift with the edit; a style-only change re-measures only the
    // paragraphs its runs touch. Equality with a complete measurement holds.
    std::u16string units;for(unsigned i=0;i<500;++i){units+=std::u16string(u"段落 ")+char16_t(u'A'+i%26)+u" 富文本 Endfield rich paragraph 😀 wraps in the viewport";units+=i%9==4?u"\r\n":u"\n";}
    core::notes::RichText rich;unsigned occurrence{};for(auto at=units.find(u"Endfield rich");at!=std::u16string::npos;at=units.find(u"Endfield rich",at+1),++occurrence){if(occurrence%4)continue;
        core::notes::TextRun run;run.location=static_cast<std::uint32_t>(at);run.length=13;run.style.bold=occurrence%8==0;run.style.fontSize=occurrence%12==0?20:12;rich.runs.push_back(run);}
    core::notes::RichDocument document(units,rich,1u<<22);
    auto payload=[&]{return std::optional<std::string>(core::notes::encodeRichText(*document.richText(),document.text()).encode());};
    std::uint64_t richRevision=1;(void)measure.measure("rich",richRevision,utf8(document.text()),190,12,{},payload());
    auto expectRich=[&](const char*why,bool reuse){
        const auto value=utf8(document.text());const auto before=measure.stats();const auto incrementalIndex=measure.measure("rich",++richRevision,value,190,12,{},payload());const auto after=measure.stats();
        NativeNotesTextMeasurer fresh(raster);const auto full=fresh.measure("full",1,value,190,12,{},payload());
        check(incrementalIndex->measured.richText.has_value()&&sameIndex(*incrementalIndex,*full),why);
        if(reuse&&!(after.incrementalMeasurements==before.incrementalMeasurements+1&&after.reusedLines>before.reusedLines+400&&after.analysisLayoutsCreated-before.analysisLayoutsCreated<=6))
            std::cerr<<why<<": incremental "<<after.incrementalMeasurements-before.incrementalMeasurements<<", reused "<<after.reusedLines-before.reusedLines<<" of "<<full->measured.lines.size()<<", layouts "<<after.analysisLayoutsCreated-before.analysisLayoutsCreated<<'\n';
        if(reuse)check(after.incrementalMeasurements==before.incrementalMeasurements+1&&after.reusedLines>before.reusedLines+400&&after.analysisLayoutsCreated-before.analysisLayoutsCreated<=6,"Rich edit re-measures only the paragraphs it touched");
    };
    const auto middle=static_cast<std::uint32_t>(document.text().find(u"富文本",document.text().size()/2));
    document.replace({middle,middle},u"插入");expectRich("Rich typing shifts later runs and equals a complete index",true);
    const auto join=static_cast<std::uint32_t>(document.text().find(u'\n',document.text().size()/3));document.replace({join,join+1},u"");expectRich("Rich paragraph join equals a complete index",true);
    const auto styled=static_cast<std::uint32_t>(document.text().find(u"Endfield",document.text().size()/4));document.setSelection({{styled,styled+8},core::text::ActiveEnd::end,false});
    core::notes::FormatChange bigger;bigger.kind=core::notes::FormatKind::size;bigger.fontSize=24;check(document.applyFormat(bigger),"Style-only fixture changes runs");expectRich("Style-only run change re-measures its paragraph and equals a complete index",true);
    const auto first=static_cast<std::uint32_t>(document.text().find(u'\n'));document.replace({first,first},u"\n新段落");expectRich("Rich paragraph insertion near the start equals a complete index",true);
}
void run(){
    LayerRasterizer raster;NativeNotesTextMeasurer measure(raster);LayerRasterOptions options;
    const auto empty=measure.measure("empty",1,"",190);complete(*empty,"");check(empty->measured.lines.size()==1&&empty->measured.lines[0].begin==0&&empty->measured.lines[0].end==0,"Empty source gets one real-font-height blank line");
    check(empty->font.selectedFamily=="Noto Sans SC"&&!empty->font.fontSubstitutions.empty(),"Unavailable source system font fallback is explicit");
    check(empty->font.lineHeight==std::ceil(empty->font.ascent+empty->font.descent+std::max(0.,empty->font.leading))+1,"Source ceil(font metrics)+1 contract uses selected font, not invented character dimensions");
    auto alias=empty->presentationText(empty);check(alias.get()==&empty->measured,"Presentation borrows same text/line index without copy");
    const auto before=raster.stats();const auto stats=measure.stats();
    for(unsigned frame=0;frame<120;++frame)check(measure.measure("empty",1,"",190)==empty,"Equal revision/geometry reuses actual index handle");
    check(measure.stats().cacheHits==stats.cacheHits+120&&raster.stats().textAnalysisFormatsCreated==before.textAnalysisFormatsCreated&&raster.stats().rasterizations==before.rasterizations,"Idle/pointer reuse creates no text format, raster, or new analysis layout");
    const std::string breaks="Alpha  \r\n\nBeta\vGamma\fDelta\xC2\x85" "Epsilon\xE2\x80\xA8" "Zeta\xE2\x80\xA9" "Eta\r";
    const auto hard=measure.measure("hard",1,breaks,32768);complete(*hard,breaks);
    check(hard->measured.lines.size()==9,"Foundation newline set with CRLF pair and final CR blank line");
    const auto&first=hard->measured.lines[0];check(first.begin==0&&first.visibleTextEnd==7&&first.end==9,"CRLF joins preceding range while trailing spaces remain visible");
    check(hard->measured.text.substr(first.begin,first.visibleTextEnd-first.begin)=="Alpha  ","Only source newlines trim, never ordinary spaces");
    const auto newlineOnly=measure.measure("hard2",1,"\n\r\n\r",100);complete(*newlineOnly,"\n\r\n\r");check(newlineOnly->measured.lines.size()==4,"Consecutive hard breaks preserve source blank lines without double counting CRLF");
    for(const auto&value:std::vector<std::string>{"X\xC2\x85","X\xE2\x80\xA8","X\xE2\x80\xA9","X\v","X\f"}){
        const auto m=measure.measure("unicode-end",std::uint64_t(checks),value,100);complete(*m,value);check(m->measured.lines.size()==1,"Final Unicode newline is trimmed but does not invent source LF/CR-only trailing blank");
    }
    const std::string spaced="one   two     three  ";const auto wide=measure.measure("spaces",1,spaced,190);complete(*wide,spaced);check(wide->measured.lines.back().visibleTextEnd==spaced.size(),"Terminal whitespace stays in the last visible line");
    const std::string multilingual="终末地 日本語 한국어 😀 e\xCC\x81 👍🏽 👨‍👩‍👧‍👦 ";
    for(const double width:{1.,0.,-8.,40.,190.}){const auto m=measure.measure("unicode",std::uint64_t(checks),multilingual,width);complete(*m,multilingual);check(m->measured.width==width,"Tiny source viewport width retained while typesetter uses max(1,width)");}
    for(const auto&cluster:std::vector<std::string>{"😀","e\xCC\x81","👍🏽","👨‍👩‍👧‍👦"}){const auto m=measure.measure("cluster",std::uint64_t(checks),cluster,1);complete(*m,cluster);check(m->measured.lines.size()==1,"Very narrow wrapping preserves full supplementary/combining/emoji clusters");}
    auto analysis=raster.plainSystemTextAnalysis("generic",12,options);const auto generic=analysis->lineLengths(u"终末地 日本語 한국어 😀 e\u0301",40,64);std::uint64_t covered{};for(auto n:generic)covered+=n;check(covered==std::u16string_view(u"终末地 日本語 한국어 😀 e\u0301").size(),"Shared real DWrite line lengths count UTF-16 including full surrogate pairs");
    rejects([&]{analysis->lineLengths(std::u16string{char16_t(0xd800)},100,8);},"Generic analyzer explicitly rejects unpaired high surrogate");
    rejects([&]{analysis->lineLengths(std::u16string{char16_t(0xdc00)},100,8);},"Generic analyzer explicitly rejects unpaired low surrogate");
    rejects([&]{analysis->lineLengths(u"ABCDE",1,1);},"Line count probe rejects explicit line budget before allocating native metrics array");
    check(analysis->lineLengths({},100,1).size()==1,"Generic empty input is safe even when string_view data is null");
    std::string longText;longText.reserve(multilingual.size()*4000);for(unsigned i=0;i<4000;++i)longText+=multilingual;
    const auto longIndex=measure.measure("long",1,longText,190);complete(*longIndex,longText);check(longText.size()>65536&&longIndex->measured.lines.size()>1000,"Long multilingual note has actual complete line index beyond old leaf limit");
    const auto [visibleBegin,visibleEnd]=longIndex->visibleLines(50*longIndex->font.lineHeight,53*longIndex->font.lineHeight);check(visibleBegin==50&&visibleEnd==53,"Visible line binary search preserves end>minimum and origin<maximum");
    check(longIndex->visibleLines(-100,0)==std::pair<std::size_t,std::size_t>{0,0},"Viewport above note returns empty line interval");
    check(longIndex->visibleLines(longIndex->measured.height,longIndex->measured.height+100)==std::pair{longIndex->measured.lines.size(),longIndex->measured.lines.size()},"Viewport beyond note returns empty end interval");
    const auto longWide=measure.measure("long",1,longText,700);check(longWide!=longIndex&&longWide->measured.lines.size()<longIndex->measured.lines.size(),"Viewport width invalidates real wrapping while old caller handle survives");
    const auto larger=measure.measure("long",1,longText,700,24);check(larger!=longWide&&larger->font.lineHeight>longWide->font.lineHeight,"Font size invalidates selected font metrics and line index");
    const auto revision=measure.measure("long",2,"Changed",700,24);check(revision!=larger&&revision->measured.text=="Changed","Document revision invalidates content without modifying old caller handles");
    LayerRasterOptions mono;mono.fallbackFontFamily="Consolas";const auto alternate=measure.measure("long",2,"Changed",700,24,mono);check(alternate!=revision&&alternate->font.selectedFamily=="Consolas","Explicit family configuration invalidates measured format and reports actual fallback");
    const auto nul=measure.measure("nul",1,std::string("A\0B",3),100);complete(*nul,std::string("A\0B",3));check(nul->measured.lines[0].end==3,"Embedded NUL is retained through length-based native text analysis");
    const auto protectedResult=measure.measure("protected",1,"Old complete text",190);const auto protectedCount=measure.stats().entries;
    rejects([&]{measure.measure("protected",2,std::string("\xed\xa0\x80",3),190);},"Invalid UTF-8 surrogate input explicitly rejects without clearing prior cache");
    rejects([&]{measure.measure("protected",2,std::string(NativeNotesTextMeasurer::maximumTextBytes+1,'a'),190);},"Native payload limit rejects complete oversized input without truncation");
    rejects([&]{measure.measure("protected",2,"new",std::numeric_limits<double>::quiet_NaN());},"Nonfinite viewport rejected transactionally");
    check(measure.stats().entries==protectedCount&&measure.measure("protected",1,"Old complete text",190)==protectedResult,"Failed measurement preserves all prior retained cache results");
    const auto limit=NativeNotesTextMeasurer::maximumIndexBytes/sizeof(endfield::modules::NotesMeasuredLine);
    rejects([&]{measure.measure("protected",2,std::string(limit+1,'\n'),1);},"Pathological tiny-width line index rejects its explicit memory budget without silently dropping lines");
    check(measure.measure("protected",1,"Old complete text",190)==protectedResult,"Failed large line index keeps previous complete note usable");
    measure.clear();check(measure.stats().entries==0&&measure.stats().retainedBytes==0,"Explicit clear releases only owned cache handles");
    for(unsigned i=0;i<80;++i)measure.measure("entry-"+std::to_string(i),1,"short",190);
    check(measure.stats().entries==NativeNotesTextMeasurer::maximumEntries&&measure.stats().retainedBytes<=NativeNotesTextMeasurer::maximumCacheBytes,"Source64-entry cache bound evicts oldest handles");
    const auto latest=measure.measure("entry-79",1,"short",190);const auto hits=measure.stats().cacheHits;for(unsigned i=0;i<120;++i)check(measure.measure("entry-79",1,"short",190)==latest,"Warm measurement object retained across frames");check(measure.stats().cacheHits==hits+120,"Warm frames account only cache hits");
    measure.clear();const std::string newlineBatch(50000,'\n');for(unsigned i=0;i<12;++i)measure.measure("bulk-"+std::to_string(i),1,newlineBatch,190);
    check(measure.stats().entries<12&&measure.stats().retainedBytes<=NativeNotesTextMeasurer::maximumCacheBytes,"Source16MiB index cache budget evicts large-note indices rather than growing unbounded");
    const std::string uncommon(NativeNotesTextMeasurer::maximumCacheBytes/sizeof(endfield::modules::NotesMeasuredLine)+1000,'\n');
    const auto big=measure.measure("uncommon",1,uncommon,190);check(measure.stats().entries==1&&measure.stats().retainedBytes>NativeNotesTextMeasurer::maximumCacheBytes,"Source retains one unusually large complete current index to prevent recomputation thrash");
    check(measure.measure("uncommon",1,uncommon,190)==big,"One large index still gets immediate repeated cache hit");measure.measure("small",1,"small",190);check(measure.stats().entries==1&&measure.stats().retainedBytes<NativeNotesTextMeasurer::maximumCacheBytes,"Small replacement evicts previous unusually large cache entry");
    check(big->measured.text==uncommon&&longIndex->measured.text==longText,"Caller-owned complete handles survive cache eviction and clear");
    check(measure.remove("small")&&!measure.remove("small"),"Explicit note removal releases its owned cached index once");
    bool foreign{};std::thread worker([&]{try{measure.stats();}catch(const std::logic_error&){foreign=true;}});worker.join();check(foreign,"Shared DWrite measurement/cache use rejected on another thread");
    check(raster.stats().rasterizations==0&&raster.stats().resourceBytes==0&&raster.stats().entries==0,"All measurements allocate no glyph bitmap, GPU texture or raster cache entry");
    incremental();
}
}
int main(){const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr)){std::cerr<<"Cannot initialize owned measurement COM apartment\n";return 1;}int result{};try{run();std::cout<<"Passed "<<checks<<" native Notes text measurement contracts\n";}catch(const std::exception&e){std::cerr<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#endif
