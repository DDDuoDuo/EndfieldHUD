#include "core/notes_rich_text.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <stdexcept>
#include <utility>

namespace endfield::core::notes {
namespace {
void need(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
bool validRange(std::u16string_view value,text::Range r)noexcept{return r.start<=r.end&&r.end<=value.size();}
bool boundary(std::u16string_view value,std::uint32_t at)noexcept{
    return at==0||at==value.size()||!(value[at-1]>=0xd800&&value[at-1]<=0xdbff&&value[at]>=0xdc00&&value[at]<=0xdfff);
}
bool defaultStyle(const TextStyle&s){return s==TextStyle{};}
std::uint32_t integer(const Json&j,const char*message){need(j.isNumber(),message);const auto n=j.integer();need(n>=0&&n<=std::numeric_limits<std::uint32_t>::max(),message);return static_cast<std::uint32_t>(n);}
Json::Object extras(const Json&j,std::initializer_list<const char*>known){
    auto result=j.object();for(const auto*key:known)result.erase(key);return result;
}
RGBA decodeColor(const Json&j){
    need(j.isObject(),"Notes color must be an sRGB object");
    RGBA c;for(const auto*key:{"red","green","blue","alpha"})need(j[key].isNumber(),"Missing Notes color channel");
    c={j["red"].number(),j["green"].number(),j["blue"].number(),j["alpha"].number()};need(c.valid(),"Invalid Notes color");return c;
}
TextStyle decodeStyle(const Json&j){
    need(j.isObject(),"Notes style must be an object");TextStyle s;
    need(j["fontSize"].isNumber(),"Missing Notes font size");s.fontSize=j["fontSize"].number();
    if(!j["fontName"].isNull()){need(j["fontName"].isString(),"Invalid Notes font name");s.fontName=j["fontName"].string();}
    if(!j["color"].isNull())s.color=decodeColor(j["color"]);
    for(const auto*key:{"bold","italic","underline","strikethrough"})need(j[key].isBool(),"Missing Notes text trait");
    s.bold=j["bold"].boolean();s.italic=j["italic"].boolean();s.underline=j["underline"].boolean();s.strikethrough=j["strikethrough"].boolean();
    s.extra=extras(j,{"fontName","fontSize","color","bold","italic","underline","strikethrough"});
    need(s.valid(),"Invalid Notes text style");return s;
}
Json encodeStyle(const TextStyle&s){
    Json j{s.extra};j["fontSize"]=s.fontSize;j["bold"]=s.bold;j["italic"]=s.italic;j["underline"]=s.underline;j["strikethrough"]=s.strikethrough;
    if(s.fontName)j["fontName"]=*s.fontName;else j.erase("fontName");
    if(s.color)j["color"]=Json::Object{{"red",s.color->red},{"green",s.color->green},{"blue",s.color->blue},{"alpha",s.color->alpha}};else j.erase("color");
    return j;
}
void append(std::vector<TextRun>&out,TextRun run){
    if(!run.length||(defaultStyle(run.style)&&run.extra.empty()))return;
    if(!out.empty()&&std::uint64_t(out.back().location)+out.back().length==run.location&&out.back().style==run.style&&out.back().extra==run.extra){
        out.back().length+=run.length;return;
    }
    out.push_back(std::move(run));
}
std::vector<TextRun>normalized(std::span<const TextRun>runs){std::vector<TextRun>out;out.reserve(runs.size());for(const auto&r:runs)append(out,r);return out;}
std::vector<TextRun>clipped(std::span<const TextRun>runs,text::Range range){
    std::vector<TextRun>out;
    for(const auto&r:runs){const auto end=r.location+r.length;if(end<=range.start)continue;if(r.location>=range.end)break;
        auto part=r;part.location=std::max(r.location,range.start)-range.start;part.length=std::min(end,range.end)-std::max(r.location,range.start);append(out,std::move(part));}
    return out;
}
std::vector<TextRun>replaced(std::span<const TextRun>runs,text::Range range,std::uint32_t count,std::span<const TextRun>inserted){
    std::vector<TextRun>out;out.reserve(runs.size()+inserted.size()+2);
    for(const auto&r:runs){if(r.location>=range.start)break;auto part=r;part.length=std::min(r.location+r.length,range.start)-r.location;append(out,std::move(part));}
    for(const auto&r:inserted){auto part=r;part.location+=range.start;append(out,std::move(part));}
    for(const auto&r:runs){const auto end=r.location+r.length;if(end<=range.end)continue;auto part=r;
        const auto from=std::max(r.location,range.end);part.location=range.start+count+(from-range.end);part.length=end-from;append(out,std::move(part));}
    need(out.size()<=RichText::maximumRuns,"Edit exceeds source Notes formatting run limit; unchanged input is preserved");return out;
}
TextStyle at(std::span<const TextRun>runs,std::uint32_t p){
    auto found=std::upper_bound(runs.begin(),runs.end(),p,[](auto n,const auto&r){return n<r.location;});
    if(found!=runs.begin()){--found;if(p-found->location<found->length)return found->style;}return {};
}
// Enumerate effective styles, including default gaps, without copying text.
template<class F>void segments(std::span<const TextRun>runs,text::Range range,F visitor){
    auto p=range.start;
    for(const auto&r:runs){const auto end=r.location+r.length;if(end<=p)continue;if(r.location>=range.end)break;
        if(p<r.location){visitor(p,std::min(r.location,range.end)-p,TextStyle{},Json::Object{});p=std::min(r.location,range.end);}
        if(p>=range.end)break;const auto next=std::min(end,range.end);visitor(p,next-p,r.style,r.extra);p=next;
    }
    if(p<range.end)visitor(p,range.end-p,TextStyle{},Json::Object{});
}
bool trait(const TextStyle&s,FormatKind kind){switch(kind){case FormatKind::bold:return s.bold;case FormatKind::italic:return s.italic;case FormatKind::underline:return s.underline;case FormatKind::strikethrough:return s.strikethrough;default:return false;}}
void validate(const FormatChange&change){
    switch(change.kind){
    case FormatKind::font:need(!change.fontName.empty()&&change.fontName.size()<=256&&Json::validUtf8(change.fontName),"Invalid formatting font name");break;
    case FormatKind::size:need(std::isfinite(change.fontSize),"Invalid formatting size");break;
    case FormatKind::color:need(change.color.valid(),"Invalid formatting color");break;
    case FormatKind::bold:case FormatKind::italic:case FormatKind::underline:case FormatKind::strikethrough:break;
    default:need(false,"Unknown Notes formatting command");
    }
}
TextStyle changed(TextStyle style,const FormatChange&change,bool enable){
    switch(change.kind){
    case FormatKind::font:style.fontName=change.fontName;break;
    case FormatKind::size:style.fontSize=std::clamp(change.fontSize,6.,144.);break;
    case FormatKind::color:style.color=change.color;break;
    case FormatKind::bold:style.bold=enable;break;case FormatKind::italic:style.italic=enable;break;
    case FormatKind::underline:style.underline=enable;break;case FormatKind::strikethrough:style.strikethrough=enable;break;
    }
    return style;
}
std::size_t extraBytes(const Json::Object&extra){return extra.empty()?0:Json{extra}.encode(std::numeric_limits<std::size_t>::max()).size();}
std::size_t styleBytes(const TextStyle&s){return sizeof(TextStyle)+(s.fontName?s.fontName->size():0)+extraBytes(s.extra);}
std::size_t runBytes(std::span<const TextRun>runs){std::size_t n{};for(const auto&r:runs)n+=sizeof(TextRun)+styleBytes(r.style)-sizeof(TextStyle)+extraBytes(r.extra);return n;}
}
bool RGBA::valid()const noexcept{return std::isfinite(red)&&std::isfinite(green)&&std::isfinite(blue)&&std::isfinite(alpha)&&red>=0&&red<=1&&green>=0&&green<=1&&blue>=0&&blue<=1&&alpha>=0&&alpha<=1;}
bool TextStyle::valid()const noexcept{return std::isfinite(fontSize)&&fontSize>=6&&fontSize<=144&&(!fontName||(!fontName->empty()&&fontName->size()<=256&&Json::validUtf8(*fontName)))&&(!color||color->valid());}
bool RichText::validFor(std::u16string_view value)const noexcept{
    if(version!=1||runs.size()>maximumRuns||!text::Buffer::validUTF16(value))return false;
    std::uint64_t previous{};for(const auto&r:runs){if(r.location<previous||r.length==0||r.location>value.size()||r.length>value.size()-r.location||!r.style.valid())return false;previous=std::uint64_t(r.location)+r.length;}return true;
}
RichText decodeRichText(const Json&j,std::u16string_view value){
    need(j.isObject(),"Notes rich text must be an object");RichText rich;rich.version=integer(j["version"],"Missing Notes rich-text version");
    need(rich.version==1,"Notes rich-text version is unsupported; original payload must be preserved");need(j["runs"].isArray()&&j["runs"].array().size()<=RichText::maximumRuns,"Notes rich text exceeds source run limit");
    rich.extra=extras(j,{"version","runs"});rich.runs.reserve(j["runs"].array().size());
    for(const auto&r:j["runs"].array()){need(r.isObject(),"Notes text run must be an object");rich.runs.push_back({integer(r["location"],"Invalid Notes run location"),integer(r["length"],"Invalid Notes run length"),decodeStyle(r["style"]),extras(r,{"location","length","style"})});}
    need(rich.validFor(value),"Notes rich-text ranges/styles do not match UTF-16 text");return rich;
}
Json encodeRichText(const RichText&rich,std::u16string_view value){
    need(rich.validFor(value),"Cannot encode invalid Notes rich text");Json j{rich.extra};j["version"]=1;Json::Array runs;runs.reserve(rich.runs.size());
    for(const auto&r:rich.runs){Json run{r.extra};run["location"]=static_cast<std::int64_t>(r.location);run["length"]=static_cast<std::int64_t>(r.length);run["style"]=encodeStyle(r.style);runs.push_back(std::move(run));}j["runs"]=std::move(runs);return j;
}

struct RichDocument::Impl {
    struct Delta {std::uint32_t start{},beforeLength{},afterLength{};bool textEdit{};std::u16string beforeText,afterText;std::vector<TextRun>beforeRuns,afterRuns;};
    struct Group {std::vector<Delta>changes;text::Selection before,after;TextStyle beforeTyping,afterTyping;std::size_t bytes{};};
    struct Snapshot {std::u16string value;std::vector<TextRun>runs;text::Selection selection;TextStyle typing;};
    using Groups=std::list<Group>;
    std::u16string value;std::vector<TextRun>styled;Json::Object rootExtra;bool initiallyRich{},readonly{};
    std::uint32_t maximum;std::uint64_t revision{1};text::Selection selection;TextStyle typing;
    HistoryBudget budget;Groups undo,redo,pending,composing;std::size_t historyBytes{};
    std::optional<text::Selection>inputSelection;std::optional<TextStyle>inputTyping;
    std::optional<Snapshot>preparedSnapshot,snapshot;std::optional<text::Range>composition;
    Impl(std::u16string textValue,std::optional<RichText>rich,std::uint32_t limit,HistoryBudget b):value(std::move(textValue)),initiallyRich(rich.has_value()),maximum(limit),budget(b){
        need(limit>0&&limit<=static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())&&value.size()<=limit&&text::Buffer::validUTF16(value),"Invalid explicit rich-document text capacity/UTF-16");
        need(b.maximumGroups>0&&b.maximumBytes>0,"Undo budget must retain at least the current group");
        if(rich){need(rich->validFor(value),"Invalid imported Notes rich payload");styled=normalized(rich->runs);rootExtra=std::move(rich->extra);}
        typing=at(styled,0);
    }
    void writable()const{need(!readonly,"Rich document is read only");}
    void hostEdit()const{writable();need(!inputSelection&&!composition,"Host formatting/undo cannot run inside TSF input transaction or composition");}
    Snapshot copySnapshot()const{return {value,styled,inputSelection.value_or(selection),inputTyping.value_or(typing)};}
    TextStyle selectionTyping(text::Selection s)const{if(s.range.start!=s.range.end)return at(styled,s.range.start);return at(styled,s.range.start?s.range.start-1:0);}
    static std::size_t deltaBytes(const Delta&d){return sizeof(Delta)+(d.beforeText.size()+d.afterText.size())*sizeof(char16_t)+runBytes(d.beforeRuns)+runBytes(d.afterRuns);}
    void trim()noexcept{while(undo.size()>1&&(undo.size()>budget.maximumGroups||historyBytes>budget.maximumBytes)){historyBytes-=undo.front().bytes;undo.pop_front();}}
    void commit(Groups&groups)noexcept{if(groups.empty())return;for(const auto&g:redo)historyBytes-=g.bytes;redo.clear();historyBytes+=groups.front().bytes;undo.splice(undo.end(),groups);trim();}
    // Allocate complete history/style state before text mutation. The final
    // transaction commit only splices already-owned list nodes, including the
    // noexcept TSF endInputTransaction path.
    Groups* record(Delta delta,Groups&local,const text::Selection&after,const TextStyle&afterTyping){
        auto*groups=composition?&composing:inputSelection?&pending:&local;
        if(groups->empty()){Group group;group.before=inputSelection.value_or(selection);group.beforeTyping=inputTyping.value_or(typing);group.after=after;group.afterTyping=afterTyping;
            group.bytes=sizeof(Group)+styleBytes(group.beforeTyping)+styleBytes(group.afterTyping);group.changes.push_back(std::move(delta));group.bytes+=deltaBytes(group.changes.back());groups->push_back(std::move(group));}
        else {auto nextTyping=afterTyping;auto&g=groups->front();const auto beforeBytes=styleBytes(g.afterTyping);const auto afterBytes=styleBytes(nextTyping);const auto bytes=deltaBytes(delta);
            g.changes.push_back(std::move(delta));g.after=after;g.afterTyping=std::move(nextTyping);g.bytes=g.bytes-beforeBytes+afterBytes+bytes;}
        return groups;
    }
    bool history(bool reverse){
        hostEdit();auto&from=reverse?undo:redo;auto&to=reverse?redo:undo;if(from.empty())return false;
        const auto&group=from.back();auto nextValue=value;auto nextRuns=styled;auto nextTyping=reverse?group.beforeTyping:group.afterTyping;
        auto apply=[&](const Delta&d){const auto count=reverse?d.afterLength:d.beforeLength;const text::Range range{d.start,d.start+count};need(validRange(nextValue,range),"Undo range does not match document");
            const auto length=reverse?d.beforeLength:d.afterLength;const auto&runs=reverse?d.beforeRuns:d.afterRuns;nextRuns=replaced(nextRuns,range,length,runs);
            if(d.textEdit)nextValue.replace(range.start,count,reverse?d.beforeText:d.afterText);
        };
        if(reverse)for(auto i=group.changes.rbegin();i!=group.changes.rend();++i)apply(*i);else for(const auto&d:group.changes)apply(d);
        need(nextValue.size()<=maximum&&text::Buffer::validUTF16(nextValue),"Undo exceeds document capacity");
        value.swap(nextValue);styled.swap(nextRuns);typing=std::move(nextTyping);selection=reverse?group.before:group.after;++revision;
        to.splice(to.end(),from,std::prev(from.end()));return true;
    }
};
RichDocument::RichDocument(std::u16string value,std::optional<RichText>rich,std::uint32_t maximum,HistoryBudget budget):impl_(std::make_unique<Impl>(std::move(value),std::move(rich),maximum,budget)){}
RichDocument::~RichDocument()=default;
std::u16string_view RichDocument::text()const noexcept{return impl_->value;}
text::Selection RichDocument::selection()const noexcept{return impl_->selection;}
std::uint64_t RichDocument::revision()const noexcept{return impl_->revision;}
std::uint32_t RichDocument::maximumUnits()const noexcept{return impl_->maximum;}
bool RichDocument::readOnly()const noexcept{return impl_->readonly;}
void RichDocument::setReadOnly(bool v)noexcept{impl_->readonly=v;}
void RichDocument::beginInputTransaction(){auto&i=*impl_;need(!i.inputSelection,"Input transaction is already active");i.inputSelection=i.selection;}
void RichDocument::endInputTransaction()noexcept{auto&i=*impl_;if(!i.composition)i.commit(i.pending);i.inputSelection.reset();i.inputTyping.reset();i.preparedSnapshot.reset();}
void RichDocument::setSelection(text::Selection s){
    auto&i=*impl_;need((s.activeEnd==text::ActiveEnd::none||s.activeEnd==text::ActiveEnd::start||s.activeEnd==text::ActiveEnd::end)&&validRange(i.value,s.range)&&(!s.interim||s.activeEnd==text::ActiveEnd::none),"Invalid rich-document UTF-16 selection");
    if(s==i.selection)return;auto style=i.selectionTyping(s);
    if(i.inputSelection&&!i.inputTyping)i.inputTyping=i.typing;
    auto&groups=i.composition?i.composing:i.pending;
    if(!groups.empty()){auto afterStyle=style;auto&g=groups.front();const auto bytes=styleBytes(g.afterTyping),nextBytes=styleBytes(afterStyle);
        g.afterTyping=std::move(afterStyle);g.after=s;g.bytes=g.bytes-bytes+nextBytes;}
    i.selection=s;i.typing=std::move(style);
}
text::Change RichDocument::replace(text::Range range,std::u16string_view replacement){
    auto&i=*impl_;i.writable();need(validRange(i.value,range)&&boundary(i.value,range.start)&&boundary(i.value,range.end)&&text::Buffer::validUTF16(replacement),"Invalid rich-document UTF-16 replacement");
    need(replacement.size()<=i.maximum-(i.value.size()-(range.end-range.start)),"Replacement exceeds explicit rich-document capacity; input is not truncated");
    const auto count=static_cast<std::uint32_t>(replacement.size());const auto newEnd=range.start+count;text::Change change{range.start,range.end,newEnd};
    std::vector<TextRun>inserted;if(count)append(inserted,{0,count,i.typing,{}});
    auto nextRuns=replaced(i.styled,range,count,inserted);const text::Selection nextSelection{{newEnd,newEnd},text::ActiveEnd::end,false};
    if(std::u16string_view(i.value).substr(range.start,range.end-range.start)==replacement&&nextRuns==i.styled){i.selection=nextSelection;return change;}
    auto nextTyping=i.typing;Impl::Delta delta{range.start,range.end-range.start,count,true,std::u16string(i.value.data()+range.start,range.end-range.start),std::u16string(replacement),clipped(i.styled,range),inserted};
    if(i.inputSelection&&!i.composition&&!i.preparedSnapshot)i.preparedSnapshot=i.copySnapshot();
    Impl::Groups local;auto&existing=i.composition?i.composing:i.pending;
    std::optional<TextStyle>previousTyping;std::optional<text::Selection>previousSelection;std::size_t previousBytes{};
    if((i.composition||i.inputSelection)&&!existing.empty()){previousTyping=existing.front().afterTyping;previousSelection=existing.front().after;previousBytes=existing.front().bytes;}
    auto*recorded=i.record(std::move(delta),local,nextSelection,nextTyping);
    // The undo record owns replacement bytes even when the caller's view
    // aliases this document. std::string::replace has strong failure semantics.
    try{i.value.replace(range.start,range.end-range.start,recorded->front().changes.back().afterText);}catch(...){
        // History staging must not describe a text edit that failed. Restore
        // its appended delta without touching prior successful lock edits.
        auto&g=recorded->front();g.changes.pop_back();if(g.changes.empty())recorded->clear();else{g.afterTyping=std::move(*previousTyping);g.after=*previousSelection;g.bytes=previousBytes;}throw;
    }
    i.styled=std::move(nextRuns);i.selection=nextSelection;i.typing=std::move(nextTyping);++i.revision;
    if(i.composition){const auto map=[&](std::uint32_t p,bool right){if(p<range.start)return p;if(p>range.end)return p-(range.end-range.start)+count;return right?newEnd:range.start;};i.composition=text::Range{map(i.composition->start,false),map(i.composition->end,true)};}
    if(recorded==&local)i.commit(local);return change;
}
void RichDocument::beginComposition(text::Range range){
    auto&i=*impl_;i.writable();need(!i.composition&&validRange(i.value,range),"Invalid rich-document composition start");
    if(i.preparedSnapshot){i.snapshot=std::move(i.preparedSnapshot);i.preparedSnapshot.reset();}else i.snapshot=i.copySnapshot();
    if(!i.pending.empty())i.composing.splice(i.composing.end(),i.pending);i.composition=range;
}
void RichDocument::updateComposition(text::Range range){auto&i=*impl_;need(i.composition&&validRange(i.value,range),"Invalid rich-document composition update");i.composition=range;}
std::optional<text::Range>RichDocument::composition()const noexcept{return impl_->composition;}
std::optional<text::Change>RichDocument::endComposition(bool cancel){
    auto&i=*impl_;if(!i.composition)return {};need(i.snapshot.has_value(),"Rich-document composition snapshot is missing");std::optional<text::Change>change;
    if(cancel){change=text::Change{0,static_cast<std::uint32_t>(i.value.size()),static_cast<std::uint32_t>(i.snapshot->value.size())};i.value=std::move(i.snapshot->value);i.styled=std::move(i.snapshot->runs);i.selection=i.snapshot->selection;i.typing=std::move(i.snapshot->typing);i.composing.clear();++i.revision;}
    else {if(!i.composing.empty()){auto afterTyping=i.typing;auto&g=i.composing.front();g.bytes=g.bytes-styleBytes(g.afterTyping)+styleBytes(afterTyping);g.after=i.selection;g.afterTyping=std::move(afterTyping);}i.commit(i.composing);}
    i.snapshot.reset();i.preparedSnapshot.reset();i.composition.reset();return change;
}
std::span<const TextRun>RichDocument::runs()const noexcept{return impl_->styled;}
const TextStyle&RichDocument::typingStyle()const noexcept{return impl_->typing;}
TextStyle RichDocument::selectionStyle()const{
    const auto&i=*impl_;const auto range=i.selection.range;if(range.start==range.end)return i.typing;auto result=at(i.styled,range.start);
    result.bold=result.italic=result.underline=result.strikethrough=true;
    segments(i.styled,range,[&](auto,auto,const auto&s,const auto&){result.bold&=s.bold;result.italic&=s.italic;result.underline&=s.underline;result.strikethrough&=s.strikethrough;});return result;
}
bool RichDocument::applyFormat(const FormatChange&change){
    auto&i=*impl_;i.hostEdit();validate(change);const auto range=i.selection.range;const auto current=selectionStyle();const bool enable=!trait(current,change.kind);
    if(range.start==range.end){auto next=changed(i.typing,change,enable);if(next==i.typing)return false;i.typing=std::move(next);return true;}
    std::vector<TextRun>inserted;segments(i.styled,range,[&](auto start,auto length,const auto&style,const auto&extra){append(inserted,{start-range.start,length,changed(style,change,enable),extra});});
    auto nextRuns=replaced(i.styled,range,range.end-range.start,inserted);if(nextRuns==i.styled)return false;
    auto nextTyping=changed(i.typing,change,enable);Impl::Delta delta{range.start,range.end-range.start,range.end-range.start,false,{},{},clipped(i.styled,range),inserted};Impl::Groups local;i.record(std::move(delta),local,i.selection,nextTyping);
    i.styled=std::move(nextRuns);i.typing=std::move(nextTyping);++i.revision;i.commit(local);return true;
}
bool RichDocument::canUndo()const noexcept{return !impl_->readonly&&!impl_->inputSelection&&!impl_->composition&&!impl_->undo.empty();}
bool RichDocument::canRedo()const noexcept{return !impl_->readonly&&!impl_->inputSelection&&!impl_->composition&&!impl_->redo.empty();}
bool RichDocument::undo(){return impl_->history(true);}bool RichDocument::redo(){return impl_->history(false);}
void RichDocument::clearHistory(){auto&i=*impl_;need(!i.inputSelection&&!i.composition,"Cannot clear undo during input transaction/composition");i.undo.clear();i.redo.clear();i.historyBytes=0;}
std::optional<RichText>RichDocument::richText()const{
    const auto&i=*impl_;RichText rich;rich.extra=i.rootExtra;rich.runs.reserve(i.styled.size());
    for(auto r:i.styled){if(r.style.fontName&&r.style.fontName->starts_with('.'))r.style.fontName.reset();append(rich.runs,std::move(r));}
    if(!i.initiallyRich&&rich.runs.empty())return {};need(rich.validFor(i.value),"Captured Notes formatting is invalid; input is not truncated");return rich;
}
RichDocumentStats RichDocument::stats()const noexcept{const auto&i=*impl_;std::size_t changes{};for(const auto&g:i.pending)changes+=g.changes.size();for(const auto&g:i.composing)changes+=g.changes.size();return {i.undo.size(),i.redo.size(),i.historyBytes,changes,i.snapshot.has_value()||i.preparedSnapshot.has_value()};}
} // namespace endfield::core::notes
