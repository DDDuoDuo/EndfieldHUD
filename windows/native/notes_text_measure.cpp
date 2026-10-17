#include "native/notes_text_measure.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <limits>
#include <stdexcept>
#include <thread>

namespace endfield::native {
namespace {
[[noreturn]]void invalid(const char*why){throw std::invalid_argument(why);}
struct Scalar{std::uint32_t value{};std::size_t end{};unsigned units{};};
Scalar scalar(std::string_view text,std::size_t at){
    if(at>=text.size())invalid("Missing UTF-8 scalar");
    const auto first=static_cast<unsigned char>(text[at]);unsigned length{};std::uint32_t code{};
    if(first<0x80){length=1;code=first;}else if(first>=0xc2&&first<=0xdf){length=2;code=first&0x1f;}
    else if(first>=0xe0&&first<=0xef){length=3;code=first&0x0f;}else if(first>=0xf0&&first<=0xf4){length=4;code=first&7;}
    else invalid("Invalid UTF-8 note text");
    if(length>text.size()-at)invalid("Truncated UTF-8 note text");
    for(unsigned i=1;i<length;++i){const auto next=static_cast<unsigned char>(text[at+i]);if((next&0xc0)!=0x80)invalid("Invalid UTF-8 note continuation");code=(code<<6)|(next&0x3f);}
    if((length==2&&code<0x80)||(length==3&&code<0x800)||(length==4&&code<0x10000)||code>0x10ffff||(code>=0xd800&&code<=0xdfff))invalid("Invalid UTF-8 note scalar");
    return{code,at+length,code>=0x10000?2u:1u};
}
bool newline(std::uint32_t c){return(c>=0x0a&&c<=0x0d)||c==0x85||c==0x2028||c==0x2029;}
void decode(std::string_view text,std::size_t begin,std::size_t end,std::u16string&out){
    out.clear();
    for(auto at=begin;at<end;){const auto s=scalar(text,at);if(s.end>end)invalid("Paragraph split is not a UTF-8 boundary");
        if(s.units==1)out.push_back(static_cast<char16_t>(s.value));else{const auto value=s.value-0x10000;out.push_back(static_cast<char16_t>(0xd800+(value>>10)));out.push_back(static_cast<char16_t>(0xdc00+(value&0x3ff)));}at=s.end;}
}
std::uint32_t utf16Length(std::string_view text){
    std::size_t units{};for(const char c:text){const auto b=static_cast<unsigned char>(c);if((b&0xc0)!=0x80)units+=b>=0xf0?2:1;}
    if(units>UINT32_MAX)invalid("Note text exceeds UTF-16 ACP range");return static_cast<std::uint32_t>(units);
}
// Largest X such that both sparse sorted run lists describe identical styles on
// [0,X): equal runs, equal gaps. UINT32_MAX when the lists are identical.
std::uint32_t commonRunPrefix(std::span<const core::notes::TextRun>a,std::span<const core::notes::TextRun>b){
    for(std::size_t i=0;;++i){
        if(i==a.size()||i==b.size())return i==a.size()&&i==b.size()?UINT32_MAX:(i==a.size()?b[i].location:a[i].location);
        const auto&x=a[i];const auto&y=b[i];
        if(x.location!=y.location)return std::min(x.location,y.location);
        if(x.style!=y.style||x.extra!=y.extra)return x.location;
        if(x.length!=y.length)return x.location+std::min(x.length,y.length);
    }
}
// Largest Y such that [aUnits-Y,aUnits) of a equals [bUnits-Y,bUnits) of b.
std::uint32_t commonRunSuffix(std::span<const core::notes::TextRun>a,std::uint32_t aUnits,std::span<const core::notes::TextRun>b,std::uint32_t bUnits){
    for(std::size_t i=0;;++i){
        if(i==a.size()||i==b.size()){if(i==a.size()&&i==b.size())return UINT32_MAX;const auto&r=i==a.size()?b[b.size()-1-i]:a[a.size()-1-i];return (i==a.size()?bUnits:aUnits)-(r.location+r.length);}
        const auto&x=a[a.size()-1-i];const auto&y=b[b.size()-1-i];
        const auto xe=aUnits-(x.location+x.length),ye=bUnits-(y.location+y.length);
        if(xe!=ye)return std::min(xe,ye);
        if(x.style!=y.style||x.extra!=y.extra)return xe;
        if(x.length!=y.length)return xe+std::min(x.length,y.length);
    }
}
std::size_t bytes(const NativeNotesTextMeasurement&m){
    std::size_t result=sizeof(m)+m.measured.text.capacity()+m.measured.lines.capacity()*sizeof(modules::NotesMeasuredLine)+m.font.selectedFamily.capacity();
    if(m.measured.sourceRichPayload)result+=m.measured.sourceRichPayload->capacity();
    if(m.measured.richText){result+=m.measured.richText->runs.capacity()*sizeof(core::notes::TextRun);for(const auto&r:m.measured.richText->runs){if(r.style.fontName)result+=r.style.fontName->capacity();result+=r.extra.size()*sizeof(ehud::data::Json::Object::value_type)+r.style.extra.size()*sizeof(ehud::data::Json::Object::value_type);}}
    result+=m.font.fontSubstitutions.capacity()*sizeof(LayerFontSubstitution);
    for(const auto&f:m.font.fontSubstitutions)result+=f.node.capacity()+f.requestedFamily.capacity()+f.requestedFace.capacity()+f.selectedFamily.capacity();
    return result;
}
}

std::pair<std::size_t,std::size_t>NativeNotesTextMeasurement::visibleLines(double minimum,double maximum)const{
    if(!std::isfinite(minimum)||!std::isfinite(maximum))invalid("Visible note range must be finite");
    std::size_t low{},high=measured.lines.size();while(low<high){const auto middle=(low+high)/2;const auto&line=measured.lines[middle];if(line.y+line.height<=minimum)low=middle+1;else high=middle;}
    const auto first=low;high=measured.lines.size();while(low<high){const auto middle=(low+high)/2;if(measured.lines[middle].y<maximum)low=middle+1;else high=middle;}return{first,low};
}
std::shared_ptr<const modules::NotesMeasuredText>NativeNotesTextMeasurement::presentationText(const std::shared_ptr<const NativeNotesTextMeasurement>&owner)const{
    if(owner.get()!=this)invalid("Note measurement alias needs its actual owning handle");
    return{owner,&measured};
}
struct NativeNotesTextMeasurer::Impl {
    struct Entry{std::string id,fallback,monospace;std::uint64_t revision{};double width{},fontSize{};std::shared_ptr<const NativeNotesTextMeasurement>result;std::size_t bytes{};};
    LayerRasterizer*raster;std::thread::id thread{std::this_thread::get_id()};
    std::vector<Entry>entries;NativeNotesTextMeasureStats counts;
    explicit Impl(LayerRasterizer&r):raster(&r){entries.reserve(maximumEntries);}
    void onThread()const{if(std::this_thread::get_id()!=thread)throw std::logic_error("Note measurement used outside its creating thread");}
    void erase(std::size_t index){counts.retainedBytes-=entries[index].bytes;entries.erase(entries.begin()+static_cast<std::ptrdiff_t>(index));++counts.evictions;}
};
NativeNotesTextMeasurer::NativeNotesTextMeasurer(LayerRasterizer&r):impl_(std::make_unique<Impl>(r)){}
NativeNotesTextMeasurer::~NativeNotesTextMeasurer()=default;
std::shared_ptr<const NativeNotesTextMeasurement>NativeNotesTextMeasurer::measure(std::string_view id,std::uint64_t revision,std::string_view value,double width,double size,const LayerRasterOptions&options,const std::optional<std::string>&richPayload){
    auto&r=*impl_;r.onThread();
    if(id.empty()||id.size()>4096||!ehud::data::Json::validUtf8(id)||!std::isfinite(width)||std::abs(width)>32768||!std::isfinite(size)||size<=0||size>2048)invalid("Invalid note measurement identity/geometry/font");
    for(std::size_t i=0;i<r.entries.size();++i){const auto&e=r.entries[i];if(e.result->fontRevision==r.raster->fontRevision()&&e.id==id&&e.revision==revision&&e.width==width&&e.fontSize==size&&e.fallback==options.fallbackFontFamily&&e.monospace==options.monospaceFallbackFontFamily&&e.result->measured.sourceRichPayload==richPayload){
        auto result=e.result;std::rotate(r.entries.begin()+static_cast<std::ptrdiff_t>(i),r.entries.begin()+static_cast<std::ptrdiff_t>(i+1),r.entries.end());++r.counts.cacheHits;return result;}}
    if(value.size()>maximumTextBytes||!ehud::data::Json::validUtf8(value))invalid("Note measurement exceeds native payload bound or has invalid UTF-8; input is not truncated");
    auto analysis=r.raster->plainSystemTextAnalysis(std::string(id),size,options);
    struct AnalysisCount{Impl&r;LayerPlainTextAnalysis&a;~AnalysisCount(){r.counts.analysisLayoutsCreated+=a.layoutsCreated();}}count{r,*analysis};
    auto result=std::make_shared<NativeNotesTextMeasurement>();result->fontRevision=r.raster->fontRevision();result->font=analysis->metrics();auto&measured=result->measured;
    measured.text=value;measured.width=width;measured.fontSize=size;measured.sourceRichPayload=richPayload;if(richPayload)measured.richText=modules::decodeNotesRichText(value,richPayload);
    constexpr auto maximumLines=maximumIndexBytes/sizeof(modules::NotesMeasuredLine);
    // Incremental index. An earlier index of the SAME source, width, size,
    // fallbacks, font revision and text kind keeps every whole paragraph outside
    // the edited bytes (rich: and outside the changed UTF-16 style runs).
    // Paragraphs wrap independently, so their native line lengths and heights
    // equal a complete re-measurement; only origins and the byte/UTF-16 offsets
    // after the edit move. Everything else is measured exactly as before.
    std::shared_ptr<const NativeNotesTextMeasurement>previous;
    for(const auto&e:r.entries)if(e.id==id&&e.result->fontRevision==r.raster->fontRevision()&&e.width==width&&e.fontSize==size&&e.fallback==options.fallbackFontFamily&&e.monospace==options.monospaceFallbackFontFamily&&e.result->measured.richText.has_value()==measured.richText.has_value()){previous=e.result;break;}
    const bool rich=measured.richText.has_value();
    std::size_t lead{},tailFirst{},tailEnd{},leadBytes{},stop=value.size();std::uint32_t leadUnits{},oldUnits{},newUnits{};
    if(previous){
        const std::string_view old=previous->measured.text;const auto&lines=previous->measured.lines;
        const auto common=std::min(old.size(),value.size());
        const auto prefix=static_cast<std::size_t>(std::mismatch(old.begin(),old.begin()+static_cast<std::ptrdiff_t>(common),value.begin()).first-old.begin());
        std::size_t suffix{};while(suffix<common-prefix&&old[old.size()-1-suffix]==value[value.size()-1-suffix])++suffix;
        // Identical text (a style-only rich change): every byte is common, so
        // paragraphs on either side of the changed runs keep their lines.
        if(old.size()==value.size()&&prefix==old.size())suffix=old.size();
        const bool oldBlank=old.empty()||old.back()=='\n'||old.back()=='\r';const auto content=lines.size()-(oldBlank?1:0);
        std::uint32_t runPrefix=UINT32_MAX,runSuffix=UINT32_MAX;
        if(rich){oldUnits=utf16Length(old);newUnits=utf16Length(value);runPrefix=commonRunPrefix(previous->measured.richText->runs,measured.richText->runs);runSuffix=commonRunSuffix(previous->measured.richText->runs,oldUnits,measured.richText->runs,newUnits);}
        const auto unitAfter=[&](std::size_t line){return line+1<lines.size()?lines[line+1].utf16Begin:oldUnits;};
        for(std::size_t i=0;i<content;++i){const auto&l=lines[i];if(l.end>prefix)break;if(l.end==l.visibleTextEnd)continue;
            // A lone CR ending exactly at the edit could become CRLF.
            if(l.end==prefix&&old[l.end-1]=='\r')break;if(rich&&unitAfter(i)>runPrefix)break;lead=i+1;}
        tailFirst=tailEnd=content;const auto suffixStart=old.size()-suffix;
        for(std::size_t i=content;i>lead;--i){const auto&l=lines[i-1];if(i-1>0&&lines[i-2].end==lines[i-2].visibleTextEnd)continue;
            // The whole delimiter before this paragraph (at most three bytes) is unchanged.
            if(l.begin<suffixStart+3)break;if(rich&&l.utf16Begin<oldUnits-std::min(oldUnits,runSuffix))break;tailFirst=i-1;}
        if(lead){leadBytes=lines[lead-1].end;leadUnits=rich?unitAfter(lead-1):0;}
        if(tailFirst<tailEnd)stop=lines[tailFirst].begin+value.size()-old.size();
        if(lead||tailFirst<tailEnd){++r.counts.incrementalMeasurements;r.counts.reusedLines+=lead+(tailEnd-tailFirst);}
    }
    std::uint32_t unitPosition{};
    auto append=[&](std::size_t begin,std::size_t end,std::size_t visibleEnd,double height){
        if(measured.lines.size()>=maximumLines)invalid("Note line index exceeds its explicit memory bound; input is not truncated");
        if(measured.lines.size()==measured.lines.capacity()){const auto cap=measured.lines.capacity();measured.lines.reserve(std::min(maximumLines,std::max(std::size_t(64),cap+cap/2)));}
        const auto unitBegin=unitPosition;if(measured.richText)for(auto at=begin;at<visibleEnd;){const auto s=scalar(value,at);unitPosition+=s.units;at=s.end;}const auto unitVisibleEnd=unitPosition;if(measured.richText)for(auto at=visibleEnd;at<end;){const auto s=scalar(value,at);unitPosition+=s.units;at=s.end;}
        measured.lines.push_back({begin,end,visibleEnd,measured.height,height,unitBegin,unitVisibleEnd});measured.height+=height;
    };
    // Reused lines keep their exact ranges and heights; origins accumulate in
    // the same order as a complete measurement, so every y is bit-identical.
    auto reuse=[&](const modules::NotesMeasuredLine&line,std::ptrdiff_t byteShift,std::int64_t unitShift){
        if(measured.lines.size()>=maximumLines)invalid("Note line index exceeds its explicit memory bound; input is not truncated");
        auto next=line;next.begin=static_cast<std::size_t>(static_cast<std::ptrdiff_t>(line.begin)+byteShift);next.end=static_cast<std::size_t>(static_cast<std::ptrdiff_t>(line.end)+byteShift);
        next.visibleTextEnd=static_cast<std::size_t>(static_cast<std::ptrdiff_t>(line.visibleTextEnd)+byteShift);
        if(rich){next.utf16Begin=static_cast<std::uint32_t>(line.utf16Begin+unitShift);next.utf16VisibleEnd=static_cast<std::uint32_t>(line.utf16VisibleEnd+unitShift);}
        next.y=measured.height;measured.lines.push_back(next);measured.height+=next.height;
    };
    if(previous)measured.lines.reserve(std::min(maximumLines,lead+(tailEnd-tailFirst)+64));
    if(lead){for(std::size_t i=0;i<lead;++i)reuse(previous->measured.lines[i],0,0);unitPosition=leadUnits;}
    std::u16string paragraph;std::size_t start=leadBytes;
    while(start<stop){
        auto paragraphEnd=start;std::size_t delimiterEnd{};
        while(paragraphEnd<value.size()){const auto s=scalar(value,paragraphEnd);if(newline(s.value)){delimiterEnd=s.end;if(s.value==0x0d&&s.end<value.size()&&scalar(value,s.end).value==0x0a)delimiterEnd=scalar(value,s.end).end;break;}paragraphEnd=s.end;}
        if(delimiterEnd==0)delimiterEnd=paragraphEnd;
        if(start==paragraphEnd&&!measured.richText)append(start,delimiterEnd,start,result->font.lineHeight);
        else{
            decode(value,start,paragraphEnd,paragraph);
            std::vector<core::notes::TextRun> localRuns;
            std::span<const LayerStyledTextLine> styled;std::span<const std::uint32_t> plainLengths;
            if(measured.richText){
                // Include a canonical newline solely in the attributed metric
                // query so an empty styled source line retains its font. Source
                // hard-break bytes/UTF16 ranges are kept independently below.
                const bool delimiter=delimiterEnd>paragraphEnd;if(delimiter){if(value[paragraphEnd]=='\r'&&delimiterEnd-paragraphEnd==2)paragraph.push_back(u'\r');paragraph.push_back(u'\n');}
                const auto unitEnd=unitPosition+static_cast<std::uint32_t>(paragraph.size());
                auto run=std::lower_bound(measured.richText->runs.begin(),measured.richText->runs.end(),unitPosition,[](const auto&r,std::uint32_t p){return std::uint64_t(r.location)+r.length<=p;});
                for(;run!=measured.richText->runs.end()&&run->location<unitEnd;++run){auto part=*run;const auto begin=std::max(unitPosition,run->location),end=std::min(unitEnd,run->location+run->length);if(begin<end){part.location=begin-unitPosition;part.length=end-begin;localRuns.push_back(std::move(part));}}
                styled=analysis->richLines(paragraph,width,localRuns,maximumLines-measured.lines.size()+1);
                if(delimiter&&styled.size()>1&&styled.back().length==0)styled=styled.first(styled.size()-1);
            }else plainLengths=analysis->lineLengths(paragraph,width,maximumLines-measured.lines.size());
            const auto lineCount=measured.richText?styled.size():plainLengths.size();std::size_t at=start;
            for(std::size_t i=0;i<lineCount;++i){const auto begin=at;auto length=measured.richText?styled[i].length:plainLengths[i];
                if(measured.richText&&i+1==lineCount&&delimiterEnd>paragraphEnd){const unsigned delimiterUnits=value[paragraphEnd]=='\r'&&delimiterEnd-paragraphEnd==2?2u:1u;if(length<delimiterUnits)invalid("Missing attributed source delimiter");length-=delimiterUnits;}
                std::uint32_t units{};while(units<length){const auto s=scalar(value,at);if(s.end>paragraphEnd||s.units>length-units)invalid("Native line splits a UTF-16 surrogate/UTF-8 scalar");units+=s.units;at=s.end;}
                if((length==0&&start!=paragraphEnd)||at>paragraphEnd)invalid("Native wrapping returned an empty interior line");
                if(i+1==lineCount&&at!=paragraphEnd)invalid("Native wrapping did not cover complete paragraph");
                append(begin,i+1==lineCount?delimiterEnd:at,at,measured.richText?styled[i].height:result->font.lineHeight);
            }
        }
        start=delimiterEnd;
    }
    if(start!=stop)invalid("Incremental note index did not end at a reused paragraph");
    if(tailFirst<tailEnd){
        const auto&lines=previous->measured.lines;const auto byteShift=static_cast<std::ptrdiff_t>(value.size())-static_cast<std::ptrdiff_t>(previous->measured.text.size());
        const auto unitShift=static_cast<std::int64_t>(newUnits)-static_cast<std::int64_t>(oldUnits);
        if(rich&&std::int64_t(unitPosition)!=std::int64_t(lines[tailFirst].utf16Begin)+unitShift)invalid("Incremental rich note index lost its UTF-16 alignment");
        for(std::size_t i=tailFirst;i<tailEnd;++i)reuse(lines[i],byteShift,unitShift);if(rich)unitPosition=newUnits;
    }
    if(value.empty()||value.back()=='\n'||value.back()=='\r')append(value.size(),value.size(),value.size(),result->font.lineHeight);
    if(measured.lines.empty())invalid("Note measurement did not produce a complete line index");
    result->font=analysis->metrics();
    // Commit only after all UTF boundaries/native layouts/line budgets validate.
    // Source bounds its cache and keeps one unusually large current index; held
    // shared results belong to their callers and remain valid after eviction.
    const std::size_t footprint=bytes(*result)+sizeof(Impl::Entry)+id.size()+options.fallbackFontFamily.size()+options.monospaceFallbackFontFamily.size();
    Impl::Entry entry{std::string(id),options.fallbackFontFamily,options.monospaceFallbackFontFamily,revision,width,size,result,footprint};
    for(std::size_t i=r.entries.size();i>0;--i)if(r.entries[i-1].id==id)r.erase(i-1);
    while(!r.entries.empty()&&(r.entries.size()>=maximumEntries||footprint>maximumCacheBytes||r.counts.retainedBytes>maximumCacheBytes-footprint))r.erase(0);
    r.entries.push_back(std::move(entry));r.counts.retainedBytes+=footprint;++r.counts.measurements;return result;
}
bool NativeNotesTextMeasurer::remove(std::string_view id){auto&r=*impl_;r.onThread();bool found{};for(std::size_t i=r.entries.size();i>0;--i)if(r.entries[i-1].id==id){r.erase(i-1);found=true;}return found;}
void NativeNotesTextMeasurer::clear(){auto&r=*impl_;r.onThread();r.entries.clear();r.counts.retainedBytes=0;}
NativeNotesTextMeasureStats NativeNotesTextMeasurer::stats()const{const auto&r=*impl_;r.onThread();auto result=r.counts;result.entries=r.entries.size();return result;}
} // namespace endfield::native
