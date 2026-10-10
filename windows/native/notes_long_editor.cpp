#include "native/notes_long_editor.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include <dwrite.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace endfield::native {
namespace {
namespace text=core::text;using Json=ehud::data::Json;
void need(bool value,const char* reason){if(!value)throw std::invalid_argument(reason);}
void checked(HRESULT hr,const char* reason){if(FAILED(hr))throw std::runtime_error(reason);}
bool high(char16_t c){return c>=0xd800&&c<=0xdbff;}bool low(char16_t c){return c>=0xdc00&&c<=0xdfff;}
std::string utf8(std::u16string_view value){if(value.empty())return {};need(text::Buffer::validUTF16(value),"Editor document contains invalid UTF16");
    const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(value.data()),int(value.size()),nullptr,0,nullptr,nullptr);need(n>0,"Cannot encode editor UTF16");std::string result(std::size_t(n),'\0');need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(value.data()),int(value.size()),result.data(),n,nullptr,nullptr)==n,"Cannot encode editor UTF16");return result;}
Json rgba(const std::array<double,4>& c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
Json pathCommand(const char* op,double x,double y){Json::Array points;points.emplace_back(Json::Array{x,y});return Json::Object{{"op",op},{"points",std::move(points)}};}
void rectanglePath(Json::Array& commands,core::Rect r){if(r.width<=0||r.height<=0)return;commands.push_back(pathCommand("move",r.x,r.y));commands.push_back(pathCommand("line",r.x+r.width,r.y));commands.push_back(pathCommand("line",r.x+r.width,r.y+r.height));commands.push_back(pathCommand("line",r.x,r.y+r.height));commands.push_back(Json::Object{{"op","close"},{"points",Json::Array{}}});}
// A chunk holds whole paragraphs. Chunks end only after LF or CR (CRLF kept
// together): DirectWrite ends a line there and starts a new bidi paragraph, so
// every chunk wraps exactly like the same paragraphs inside one layout.
bool lineBreak(char16_t c){return c==u'\n'||c==u'\r';}
bool chunkBoundary(std::u16string_view t,std::size_t p){if(p==0||p>=t.size())return true;const auto c=t[p-1];return c==u'\n'||(c==u'\r'&&t[p]!=u'\n');}
// The painted leaf limit is 65,536 UTF-16 units; one paragraph plus its CRLF must fit.
constexpr std::uint32_t paragraphLimit=65534,chunkLimit=65536;
void validate(const ProjectedEditorStyle& s,const LayerRasterOptions& o){
    for(double n:{s.width,s.height,s.fontSize})need(std::isfinite(n)&&n>0,"Invalid long editor dimensions/font size");
    need(std::isfinite(s.lineHeight)&&std::isfinite(s.baseline)&&((s.lineHeight==0&&s.baseline==0)||(s.lineHeight>0&&s.lineHeight<=32768&&s.baseline>0&&s.baseline<=s.lineHeight)),"Invalid explicit long editor line height/baseline");
    switch(s.alignment){case ProjectedEditorAlignment::natural:case ProjectedEditorAlignment::left:case ProjectedEditorAlignment::center:case ProjectedEditorAlignment::right:break;default:need(false,"Invalid long editor alignment");}
    need(!s.sourceSingleLineField&&s.wrapped,"The long editor is a wrapped multiline document");
    need(!s.naturalParagraphSpacingOne||(s.lineHeight==0&&s.baseline==0),"Natural paragraph spacing cannot use uniform line metrics");
    need(std::isfinite(s.cornerRadius)&&s.cornerRadius>=0&&s.cornerRadius<=std::min(s.width,s.height)*.5,"Invalid long editor rounded viewport radius");
    need(s.fontSize>=1&&s.fontSize<=2048&&Json::validUtf8(s.fontFamily)&&!s.fontFamily.empty()&&Json::validUtf8(s.fontFace),"Invalid long editor font");
    for(const auto* c:{&s.textColor,&s.caretColor,&s.selectionColor,&s.compositionColor})for(double n:*c)need(std::isfinite(n)&&n>=0&&n<=1,"Invalid long editor straight-sRGB color");
    need(std::isfinite(o.pixelsPerPoint)&&o.pixelsPerPoint>=.25&&o.pixelsPerPoint<=4,"Invalid long editor raster scale");
    const auto w=std::ceil(s.width*o.pixelsPerPoint),h=std::ceil(s.height*o.pixelsPerPoint);
    need(w<=8192&&h<=8192&&w*h<=LayerRasterizer::maximumPixels,"Long editor viewport exceeds local raster capacity");
}
// Read-only view of one chunk's text, only to bind its painted layout.
class ChunkText final:public text::Document {
    std::u16string_view value_;std::uint64_t revision_;
public:
    ChunkText(std::u16string_view value,std::uint64_t revision):value_(value),revision_(revision){}
    std::u16string_view text()const noexcept override{return value_;}
    text::Selection selection()const noexcept override{return {};}
    std::uint64_t revision()const noexcept override{return revision_;}
    std::uint32_t maximumUnits()const noexcept override{return static_cast<std::uint32_t>(value_.size());}
    bool readOnly()const noexcept override{return true;}
    void beginInputTransaction()override{throw std::logic_error("Chunk text is read-only");}
    void endInputTransaction()noexcept override{}
    void setSelection(text::Selection)override{throw std::logic_error("Chunk text is read-only");}
    text::Change replace(text::Range,std::u16string_view)override{throw std::logic_error("Chunk text is read-only");}
    void beginComposition(text::Range)override{throw std::logic_error("Chunk text is read-only");}
    void updateComposition(text::Range)override{throw std::logic_error("Chunk text is read-only");}
    std::optional<text::Range> composition()const noexcept override{return {};}
    std::optional<text::Change> endComposition(bool)override{throw std::logic_error("Chunk text is read-only");}
};
// Every TSF/host edit goes through the owner's RichDocument; this guard only
// declines an edit that would make one paragraph exceed the painted leaf limit
// (no text is truncated), exactly like the short editor's capacity guard.
class ParagraphBoundedDocument final:public text::Document {
    core::notes::RichDocument* owner_;
public:
    explicit ParagraphBoundedDocument(core::notes::RichDocument& d):owner_(&d){}
    std::u16string_view text()const noexcept override{return owner_->text();}
    text::Selection selection()const noexcept override{return owner_->selection();}
    std::uint64_t revision()const noexcept override{return owner_->revision();}
    std::uint32_t maximumUnits()const noexcept override{return owner_->maximumUnits();}
    bool readOnly()const noexcept override{return owner_->readOnly();}
    void beginInputTransaction()override{owner_->beginInputTransaction();}void endInputTransaction()noexcept override{owner_->endInputTransaction();}
    void setSelection(text::Selection s)override{owner_->setSelection(s);}
    text::Change replace(text::Range r,std::u16string_view s)override{
        const auto value=text();need(r.start<=r.end&&r.end<=value.size(),"Invalid long editor replacement range");
        std::size_t left{},right{};for(auto p=r.start;p>0&&!lineBreak(value[p-1])&&left<=paragraphLimit;--p)++left;for(auto p=r.end;p<value.size()&&!lineBreak(value[p])&&right<=paragraphLimit;++p)++right;
        std::size_t first{};while(first<s.size()&&!lineBreak(s[first]))++first;
        if(first==s.size())need(left+s.size()+right<=paragraphLimit,"Paragraph exceeds the long editor's layout capacity; no text was truncated");
        else{need(left+first<=paragraphLimit,"Paragraph exceeds the long editor's layout capacity; no text was truncated");std::size_t run{};
            for(auto p=first;p<s.size();++p){if(lineBreak(s[p])){run=0;continue;}need(++run<=paragraphLimit,"Paragraph exceeds the long editor's layout capacity; no text was truncated");}need(run+right<=paragraphLimit,"Paragraph exceeds the long editor's layout capacity; no text was truncated");}
        return owner_->replace(r,s);
    }
    void beginComposition(text::Range r)override{owner_->beginComposition(r);}void updateComposition(text::Range r)override{owner_->updateComposition(r);}
    std::optional<text::Range>composition()const noexcept override{return owner_->composition();}std::optional<text::Change>endComposition(bool cancel)override{return owner_->endComposition(cancel);}
};
} // namespace

struct NativeNotesLongEditor::Impl {
    struct Chunk {
        std::uint32_t start{},length{};
        double top{},inner{},extent{};   // inner: next chunk's top offset; extent: natural bottom when last
        std::vector<core::notes::TextRun> runs;std::vector<text::Range> spacing; // chunk-local, as painted
        std::uint64_t styleGeneration{};bool dirty{true};
        Json descriptor;bool described{};
        std::shared_ptr<const PaintedTextLayout> painted;std::unique_ptr<LayerTextLayout> layout;
        std::uint64_t used{};int slot{-1};double paintedOffset{};
        std::uint32_t end()const noexcept{return start+length;}
    };
    class Geometry final:public text::Layout {
    public:
        Impl* owner{};
        std::uint64_t textRevision()const noexcept override{return owner->layoutRevision;}
        std::optional<text::RangeBounds> bounds(text::Range r)const override{return owner->bounds(r);}
        std::optional<std::uint32_t> hit(core::Point p,bool nearest,bool round)const override{return owner->hit(p,nearest,round);}
    };
    DWORD thread{GetCurrentThreadId()};bool alive{true},stopped{},dragging{},poseReady{},poseSet{},styleDirty{},viewportDirty{},revealPending{},recheck{};
    core::notes::RichDocument& owner;ParagraphBoundedDocument document;core::notes::RichDocument* rich{};core::notes::RichDocument* history{};
    bool richLayout{},normalizedParagraphs{};std::vector<text::Range> spacedParagraphs;
    LayerScene* scene;LayerRasterizer* raster;ProjectedEditorStyle style;LayerRasterOptions options;NativeNotesLongEditorOptions limits;
    Geometry geometry;std::unique_ptr<ProjectedTextInput> input;
    std::vector<Chunk> chunks;std::u16string synced;
    std::uint64_t layoutRevision{},styleGeneration{1},paintedFontRevision{},useClock{},paintRevision{},structureRevision{},shapeRevision{},adornedDocument{},chunkTextRevision{};
    text::Selection adornedSelection;std::optional<text::Range> adornedComposition;std::optional<char16_t> highUnit;
    std::vector<std::string> ids;std::vector<std::size_t> surfaces;std::vector<DrawObject> originals;std::vector<LayerPlacement> placements;
    std::vector<std::array<PlaneMask,8>> masks;std::vector<std::size_t> maskCounts;
    std::vector<std::uint64_t> slotRevision;std::vector<double> slotTranslate;std::vector<std::shared_ptr<const PaintedTextLayout>> slotLayout;std::vector<bool> slotEmpty;
    text::Placement placement;ProjectedEditorPose pose;double scroll{},wanted{},height{};core::Rect caretRect;std::array<Json,3> shapeContent;
    std::string measureID;std::uint64_t measureRevision{};std::size_t visibleBegin{},visibleEnd{};std::vector<char> owned;
    mutable std::vector<core::Rect> rectangles;
    NativeNotesLongEditorStats counts;
    std::size_t slotCount()const noexcept{return limits.glyphSlots;}
    std::size_t selectionLeaf()const noexcept{return 0;}std::size_t glyphLeaf(std::size_t k)const noexcept{return 1+k;}
    std::size_t compositionLeaf()const noexcept{return 1+slotCount();}std::size_t caretLeaf()const noexcept{return 2+slotCount();}

    Impl(core::notes::RichDocument& d,LayerScene& s,LayerRasterizer& r,ProjectedEditorStyle styleValue,LayerRasterOptions opts,NativeNotesLongEditorOptions l,core::notes::RichDocument* richValue,UINT_PTR generation)
        :owner(d),document(d),rich(richValue),history(&d),scene(&s),raster(&r),style(std::move(styleValue)),options(std::move(opts)),limits(l){
        need(limits.targetChunkUnits>=64&&limits.targetChunkUnits<=limits.maximumChunkUnits&&limits.maximumChunkUnits<=chunkLimit&&limits.minimumChunkUnits<=limits.targetChunkUnits&&limits.retainedLayouts>=1&&limits.retainedLayouts<=4096&&limits.glyphSlots>=2&&limits.glyphSlots<=64,"Invalid long editor chunk options");
        richLayout=rich&&rich->richText().has_value();options.paddingPoints=0;options.retainEmptyTextLayout=true;options.richTextDocument=rich!=nullptr||style.naturalParagraphSpacingOne;options.plainTextDocument=!options.richTextDocument;
        options.textDocumentOffset={};options.retainedPlainText.reset();options.revealPlainTextPosition.reset();validate(style,options);
        need(s.contentRevision()==0&&s.draws().empty(),"Long editor needs its dedicated initially empty borrowed scene");
        geometry.owner=this;static std::atomic<std::uint64_t> editors{1};measureID="notes-long-editor-measure-"+std::to_string(editors.fetch_add(1))+"-"+std::to_string(generation);
        const auto leaves=slotCount()+3;ids.reserve(leaves);ids.emplace_back("notes-long-editor-selection");for(std::size_t k=0;k<slotCount();++k)ids.push_back("notes-long-editor-glyphs-"+std::to_string(k));ids.emplace_back("notes-long-editor-composition");ids.emplace_back("notes-long-editor-caret");
        surfaces.resize(leaves);originals.resize(leaves);placements.resize(leaves);masks.resize(leaves);maskCounts.resize(leaves);
        slotRevision.assign(slotCount(),0);slotTranslate.assign(slotCount(),0);slotLayout.resize(slotCount());slotEmpty.assign(slotCount(),true);owned.assign(slotCount(),0);
    }
    void onThread()const{if(GetCurrentThreadId()!=thread)throw std::logic_error("Long editor used outside its creating thread");need(alive&&!stopped,"Long editor is stopped");}
    void current()const{onThread();need(!chunks.empty()&&layoutRevision==owner.revision(),"Synchronize long editor text before geometry/navigation");}
    std::u16string_view value()const noexcept{return owner.text();}
    // ---- descriptors --------------------------------------------------------
    Json leaf(std::size_t index,const char* kind)const{return Json::Object{{"id",ids[index]},{"class",std::string(kind)=="text"?"CATextLayer":"CAShapeLayer"},{"kind",kind},{"bounds",Json::Array{0,0,style.width,style.height}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"opacity",1},{"children",Json::Array{}}};}
    Json emptyShape(std::size_t index,const std::array<double,4>& color)const{auto out=leaf(index,"shape");out["bounds"]=Json::Array{0,0,0,0};out["shape"]=Json::Object{{"path",Json::Array{}},{"fillColor",rgba(color)},{"strokeColor",nullptr},{"fillRule","non-zero"}};return out;}
    const char* alignment()const noexcept{switch(style.alignment){case ProjectedEditorAlignment::left:return "left";case ProjectedEditorAlignment::center:return "center";case ProjectedEditorAlignment::right:return "right";default:return "natural";}}
    std::vector<text::Range> spacingRanges()const{
        if(!(rich||style.naturalParagraphSpacingOne))return {};
        if(style.naturalParagraphSpacingOne||normalizedParagraphs)return {text::Range{0,static_cast<std::uint32_t>(value().size())}};return spacedParagraphs;
    }
    // Slices (chunk-local) of the document runs/spacing exactly as the single
    // layout editor would apply them to these UTF-16 units.
    void sliceRuns(std::uint32_t start,std::uint32_t end,std::vector<core::notes::TextRun>& out)const{
        out.clear();if(!rich)return;const auto runs=rich->runs();
        auto run=std::lower_bound(runs.begin(),runs.end(),start,[](const auto& r,std::uint32_t p){return std::uint64_t(r.location)+r.length<=p;});
        for(;run!=runs.end()&&run->location<end;++run){const std::uint64_t b=std::max(start,run->location),e=std::min<std::uint64_t>(end,std::uint64_t(run->location)+run->length);if(b<e){auto part=*run;part.location=static_cast<std::uint32_t>(b-start);part.length=static_cast<std::uint32_t>(e-b);out.push_back(std::move(part));}}
    }
    bool sameRuns(const Chunk& c)const{
        if(!rich)return c.runs.empty();const auto runs=rich->runs();const auto start=c.start,end=c.end();
        auto run=std::lower_bound(runs.begin(),runs.end(),start,[](const auto& r,std::uint32_t p){return std::uint64_t(r.location)+r.length<=p;});std::size_t n{};
        for(;run!=runs.end()&&run->location<end;++run){const std::uint64_t b=std::max(start,run->location),e=std::min<std::uint64_t>(end,std::uint64_t(run->location)+run->length);if(b>=e)continue;
            if(n>=c.runs.size())return false;const auto& old=c.runs[n++];if(old.location!=b-start||old.length!=e-b||old.style!=run->style||old.extra!=run->extra)return false;}
        return n==c.runs.size();
    }
    void sliceSpacing(const std::vector<text::Range>& all,std::uint32_t start,std::uint32_t end,std::vector<text::Range>& out)const{
        out.clear();for(const auto r:all){if(r.end<start||r.start>end)continue;out.push_back({std::max(r.start,start)-start,std::min(r.end,end)-start});}
    }
    Json descriptor(const Chunk& c)const{
        const auto part=value().substr(c.start,c.length);
        Json textValue=Json::Object{{"string",utf8(part)},{"fontSize",style.fontSize},{"font",Json::Object{{"familyName",style.fontFamily},{"postScriptName",style.fontFace},{"pointSize",style.fontSize}}},{"foregroundColor",rgba(style.textColor)},{"alignment",alignment()},{"wrapped",style.wrapped},{"truncation","none"},{"runs",Json::Array{}}};
        if(style.lineHeight>0&&!richLayout){textValue["font"]["ascender"]=style.baseline;textValue["font"]["descender"]=style.baseline-style.lineHeight;textValue["font"]["leading"]=0;}
        if(rich){Json::Array runs;runs.reserve(c.runs.size());
            for(const auto& r:c.runs){const auto& st=r.style;const auto name=st.fontName&&!st.fontName->starts_with(".")?*st.fontName:style.fontFamily;
                Json font=Json::Object{{"familyName",name},{"postScriptName",st.fontName&&!st.fontName->starts_with(".")?*st.fontName:style.fontFace},{"pointSize",st.fontSize},{"symbolicTraits",(st.bold?2:0)|(st.italic?1:0)},{"preserveUserFont",st.fontName&&!st.fontName->starts_with(".")}};
                const auto color=st.color?std::array<double,4>{st.color->red,st.color->green,st.color->blue,st.color->alpha}:style.textColor;
                runs.emplace_back(Json::Object{{"utf16Range",Json::Array{double(r.location),double(r.length)}},{"attributes",Json::Object{{"NSFont",std::move(font)},{"NSColor",rgba(color)},{"NSUnderline",st.underline?1:0},{"NSStrikethrough",st.strikethrough?1:0}}}});}
            textValue["runs"]=std::move(runs);}
        if(rich||style.naturalParagraphSpacingOne){Json::Array spacing;spacing.reserve(c.spacing.size());for(const auto r:c.spacing)spacing.emplace_back(Json::Object{{"start",double(r.start)},{"end",double(r.end)},{"points",1}});textValue["paragraphLineSpacing"]=std::move(spacing);}
        return textValue;
    }
    void describe(Chunk& c){if(c.described&&!c.dirty&&c.styleGeneration==styleGeneration)return;sliceRuns(c.start,c.end(),c.runs);sliceSpacing(spacingRanges(),c.start,c.end(),c.spacing);c.descriptor=descriptor(c);c.described=true;c.styleGeneration=styleGeneration;}
    Json node(std::size_t leafIndex,const Chunk& c,double boundsHeight)const{auto out=leaf(leafIndex,"text");out["bounds"]=Json::Array{0,0,style.width,boundsHeight};out["text"]=c.descriptor;return out;}
    Json measureNode(const Chunk& c)const{auto out=node(0,c,1);out["id"]=measureID;return out;}
    // ---- chunk layout -------------------------------------------------------
    // Paragraph spacing of one painted line, exactly as DocumentTextLines
    // assigns it from the chunk-local ranges (a zero-length line is the final
    // empty line after a break).
    double lineSpacing(const Chunk& c,std::uint32_t at,bool zeroLength)const{
        if(!(rich||style.naturalParagraphSpacingOne))return 0;
        for(const auto r:c.spacing){if(r.end<=at&&!(zeroLength&&r.end==at))continue;return r.start<=at&&at<=r.end?1.:0.;}return 0;
    }
    std::vector<DWRITE_LINE_METRICS> lineMetrics;
    void bind(Chunk& c){
        need(c.painted&&c.painted->isDocumentLayout()&&c.painted->text().size()==c.length,"Long editor chunk layout does not match its text");
        const ChunkText view(value().substr(c.start,c.length),++chunkTextRevision);
        if(c.layout)c.layout->bind(c.painted,view);else c.layout=std::make_unique<LayerTextLayout>(c.painted,view);
        // Exact line extents of the SAME painted DirectWrite object (its identity
        // is the retained object's address; the handle keeps it alive on this
        // thread). Caret metrics are ink-based and cannot give line heights.
        auto& native=*reinterpret_cast<IDWriteTextLayout*>(c.painted->layoutIdentity());
        DWRITE_TEXT_METRICS metrics{};checked(native.GetMetrics(&metrics),"Read long editor chunk extent");
        UINT32 count{};const auto probe=native.GetLineMetrics(nullptr,0,&count);need((SUCCEEDED(probe)||probe==HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))&&count>0&&count<=c.length+1,"Cannot index long editor chunk lines");
        lineMetrics.resize(count);checked(native.GetLineMetrics(lineMetrics.data(),count,&count),"Read long editor chunk lines");
        double top=metrics.top,spacing{};std::uint32_t at{};
        for(UINT32 i=0;i<count;++i){const auto& m=lineMetrics[i];need(std::isfinite(m.height)&&m.height>0&&m.length<=c.length-at,"Invalid long editor chunk line");
            if(i+1==count)c.inner=top+spacing; // top of the final line in document coordinates
            spacing+=lineSpacing(c,at,m.length==0);top+=m.height;at+=m.length;}
        need(at==c.length,"Long editor chunk lines do not cover its text");
        // A chunk ending in a break owns a final empty line where the next chunk's
        // first line is painted; the next chunk starts at that line's top.
        if(c.length&&lineBreak(value()[c.end()-1]))need(lineMetrics.back().length==0,"Long editor chunk break has no following line");
        c.extent=double(metrics.top)+metrics.height+spacing;c.used=++useClock;
    }
    void layoutOffscreen(Chunk& c){
        describe(c);auto opts=options;opts.textDocumentOffset={0,0};
        // A one-point viewport: same wrap width, no visible glyph pixels.
        raster->rasterize(measureID,++measureRevision,measureNode(c),opts);
        c.painted=raster->textLayout(measureID,measureRevision);need(c.painted!=nullptr,"Long editor chunk layout was not retained");bind(c);++counts.chunkLayouts;++counts.offscreenLayouts;
    }
    // Scroll offsets and glyph leaves stay on the bitmap pixel grid, so a
    // chunk leaf moves by whole bitmap pixels: in uniform-line mode its glyphs
    // rasterize at the same sub-pixel phase as one whole-document layout.
    double snap(double v)const noexcept{return std::round(v*options.pixelsPerPoint)/options.pixelsPerPoint;}
    double leafHeight()const noexcept{return std::ceil(style.height*options.pixelsPerPoint)/options.pixelsPerPoint;}
    double effectiveOffset(const Chunk& c)const{const auto requested=std::max(0.,scroll-c.top);return std::min(requested,std::max(0.,c.painted->documentHeight()-leafHeight()));}
    void paintSlot(std::size_t k,Chunk& c,bool retained){
        describe(c);auto opts=options;opts.textDocumentOffset={0,std::max(0.,scroll-c.top)};if(retained)opts.retainedPlainText=c.painted;
        scene->updateLocalContent(ids[glyphLeaf(k)],++slotRevision[k],node(glyphLeaf(k),c,leafHeight()),opts);
        if(!retained){c.painted=scene->paintedTextLayout(ids[glyphLeaf(k)]);need(c.painted!=nullptr,"Long editor slot did not retain its painted layout");bind(c);++counts.chunkLayouts;}else ++counts.retainedRepaints;
        c.slot=static_cast<int>(k);c.paintedOffset=effectiveOffset(c);slotLayout[k]=c.painted;slotEmpty[k]=false;slotTranslate[k]=c.top+c.paintedOffset-scroll;++paintRevision;++counts.slotPaints;
    }
    void emptySlot(std::size_t k){if(slotEmpty[k])return;scene->updateLocalContent(ids[glyphLeaf(k)],++slotRevision[k],emptyShape(glyphLeaf(k),style.textColor),options);slotLayout[k].reset();slotEmpty[k]=true;++paintRevision;}
    void evict(Chunk& c){c.painted.reset();c.layout.reset();c.descriptor=Json{};c.described=false;++counts.evictions;}
    void ensureLayout(std::size_t i){auto& c=chunks[i];if(!c.layout)layoutOffscreen(c);c.used=++useClock;}
    double chunkHeight(std::size_t i)const noexcept{return i+1==chunks.size()?chunks[i].extent:chunks[i].inner;}
    void restack(){double top{};for(std::size_t i=0;i<chunks.size();++i){chunks[i].top=top;top+=chunkHeight(i);}
        const auto& last=chunks.back();height=std::max(style.height,std::ceil(last.top+last.extent)+1);}
    double maximumScroll()const noexcept{return std::ceil(std::max(0.,height-style.height)*options.pixelsPerPoint)/options.pixelsPerPoint;}
    // ---- partition ----------------------------------------------------------
    std::size_t chunkAt(std::uint32_t pos)const{auto it=std::upper_bound(chunks.begin(),chunks.end(),pos,[](std::uint32_t p,const Chunk& c){return p<c.start;});return it==chunks.begin()?0:std::size_t(it-chunks.begin())-1;}
    std::size_t chunkAtY(double y)const{auto it=std::upper_bound(chunks.begin(),chunks.end(),y,[](double v,const Chunk& c){return v<c.top;});return it==chunks.begin()?0:std::size_t(it-chunks.begin())-1;}
    void checkParagraphs(std::u16string_view t,std::size_t a,std::size_t b)const{std::size_t run{};for(auto p=a;p<b;++p){if(lineBreak(t[p])){run=0;continue;}if(++run>paragraphLimit)throw NativeNotesLongParagraphError{};}}
    // Greedy whole-paragraph groups of about target units over [a,b).
    void split(std::u16string_view t,std::uint32_t a,std::uint32_t b,std::uint32_t target,std::vector<Chunk>& out){
        if(a==b){Chunk c;c.start=a;out.push_back(std::move(c));return;}
        auto begin=a;
        while(begin<b){auto at=std::min<std::uint32_t>(b,begin+target);while(at<b&&!chunkBoundary(t,at))++at; // extend to the paragraph end
            if(at-begin>chunkLimit){at=begin+1;while(at<b&&!chunkBoundary(t,at))++at;}
            Chunk c;c.start=begin;c.length=at-begin;out.push_back(std::move(c));begin=at;}
    }
    void partitionAll(){const auto t=value();checkParagraphs(t,0,t.size());chunks.clear();split(t,0,static_cast<std::uint32_t>(t.size()),limits.targetChunkUnits,chunks);synced.assign(t);counts.splits+=chunks.size();}
    void reconcileParagraphs(std::size_t prefix,std::size_t oldEnd,std::size_t newEnd){
        if(!rich||spacedParagraphs.empty())return;std::vector<text::Range> shifted;shifted.reserve(spacedParagraphs.size()+1);
        for(const auto r:spacedParagraphs){if(r.end<=prefix)shifted.push_back(r);else if(r.start>=oldEnd)shifted.push_back({static_cast<std::uint32_t>(r.start-oldEnd+newEnd),static_cast<std::uint32_t>(r.end-oldEnd+newEnd)});
            else shifted.push_back({static_cast<std::uint32_t>(std::min<std::size_t>(r.start,prefix)),static_cast<std::uint32_t>(r.end<=oldEnd?newEnd:r.end-oldEnd+newEnd)});}
        spacedParagraphs=std::move(shifted);
    }
    void repartition(){
        const auto now=value();const auto common=std::min(synced.size(),now.size());
        const auto prefix=static_cast<std::size_t>(std::mismatch(synced.begin(),synced.begin()+std::ptrdiff_t(common),now.begin()).first-synced.begin());
        std::size_t suffix{};while(suffix<common-prefix&&synced[synced.size()-1-suffix]==now[now.size()-1-suffix])++suffix;
        const auto oldEnd=synced.size()-suffix,newEnd=now.size()-suffix;const auto delta=static_cast<std::int64_t>(newEnd)-static_cast<std::int64_t>(oldEnd);
        reconcileParagraphs(prefix,oldEnd,newEnd);
        auto i0=chunkAt(static_cast<std::uint32_t>(prefix)),i1=chunkAt(static_cast<std::uint32_t>(oldEnd>prefix?oldEnd-1:prefix));
        const auto shifted=[&](std::size_t i){return static_cast<std::size_t>(std::int64_t(chunks[i].end())+delta);};
        std::size_t a=chunks[i0].start,b=shifted(i1);
        for(;;){if(!chunkBoundary(now,a)&&i0>0){a=chunks[--i0].start;continue;}if(!chunkBoundary(now,b)&&i1+1<chunks.size()){b=shifted(++i1);continue;}break;}
        // A tiny edited region joins its smaller neighbour (bounded re-layout).
        if(b-a<limits.minimumChunkUnits&&chunks.size()>1){const auto left=i0>0?chunks[i0-1].length:UINT32_MAX,right=i1+1<chunks.size()?chunks[i1+1].length:UINT32_MAX;
            if(std::min(left,right)!=UINT32_MAX&&b-a+std::min(left,right)<=limits.maximumChunkUnits){if(left<=right)a=chunks[--i0].start;else b=shifted(++i1);++counts.merges;}}
        checkParagraphs(now,a,b);
        std::vector<Chunk> replacement;if(b-a<=limits.maximumChunkUnits){Chunk c;c.start=static_cast<std::uint32_t>(a);c.length=static_cast<std::uint32_t>(b-a);replacement.push_back(std::move(c));}
        else{split(now,static_cast<std::uint32_t>(a),static_cast<std::uint32_t>(b),limits.targetChunkUnits,replacement);++counts.splits;}
        // Inherit the first replaced chunk's slot: the edited text usually stays visible there.
        const int inherited=chunks[i0].slot;for(std::size_t i=i0;i<=i1;++i)chunks[i].slot=-1;
        for(auto& c:replacement){c.top=chunks[i0].top;c.dirty=true;}if(inherited>=0)replacement.front().slot=inherited;
        for(std::size_t i=i1+1;i<chunks.size();++i)chunks[i].start=static_cast<std::uint32_t>(std::int64_t(chunks[i].start)+delta);
        chunks.erase(chunks.begin()+std::ptrdiff_t(i0),chunks.begin()+std::ptrdiff_t(i1+1));chunks.insert(chunks.begin()+std::ptrdiff_t(i0),std::make_move_iterator(replacement.begin()),std::make_move_iterator(replacement.end()));
        synced.assign(now);
    }
    // Rebuild every chunk whose text, runs, spacing or global style changed,
    // in document order so each dirty chunk knows its exact top.
    void rebuild(){
        const auto spacing=spacingRanges();std::vector<text::Range> local;
        for(auto& c:chunks)if(!c.dirty&&(c.styleGeneration!=styleGeneration||!sameRuns(c)||(sliceSpacing(spacing,c.start,c.end(),local),local!=c.spacing)))c.dirty=true;
        double top{};
        for(std::size_t i=0;i<chunks.size();++i){auto& c=chunks[i];c.top=top;
            if(c.dirty){c.described=false;describe(c);c.dirty=false;
                const bool visible=c.top<scroll+style.height&&c.top+std::max(1.,c.inner)>scroll;
                if(visible&&c.slot>=0)paintSlot(static_cast<std::size_t>(c.slot),c,false);else{c.slot=-1;layoutOffscreen(c);}}
            top+=chunkHeight(i);}
        restack();
    }
    // ---- viewport -----------------------------------------------------------
    void visibleRange(){
        visibleBegin=chunkAtY(scroll);visibleEnd=visibleBegin;
        while(visibleEnd<chunks.size()&&(visibleEnd==visibleBegin||chunks[visibleEnd].top<scroll+style.height))++visibleEnd;
    }
    bool mergeVisible(){
        // More visible chunks than glyph slots: join the smallest visible pair.
        std::size_t best=visibleBegin;std::uint64_t smallest=UINT64_MAX;for(auto i=visibleBegin;i+1<visibleEnd;++i){const auto n=std::uint64_t(chunks[i].length)+chunks[i+1].length;if(n<smallest){smallest=n;best=i;}}
        if(smallest>chunkLimit)return false;auto& a=chunks[best];a.length+=chunks[best+1].length;a.dirty=true;a.slot=-1;chunks.erase(chunks.begin()+std::ptrdiff_t(best+1));++counts.merges;return true;
    }
    bool paintVisible(){
        bool changed{};visibleRange();
        while(visibleEnd-visibleBegin>slotCount()){need(mergeVisible(),"Visible paragraphs exceed the long editor's glyph slots");rebuild();visibleRange();changed=true;}
        std::fill(owned.begin(),owned.end(),char(0));
        for(std::size_t i=0;i<chunks.size();++i){auto& c=chunks[i];if(c.slot<0)continue;if(i<visibleBegin||i>=visibleEnd||owned[std::size_t(c.slot)])c.slot=-1;else owned[std::size_t(c.slot)]=true;}
        for(auto i=visibleBegin;i<visibleEnd;++i){auto& c=chunks[i];c.used=++useClock;
            if(c.slot>=0&&c.layout&&slotLayout[std::size_t(c.slot)]==c.painted){const auto k=std::size_t(c.slot);
                if(effectiveOffset(c)!=c.paintedOffset){paintSlot(k,c,true);changed=true;}
                const auto translate=c.top+c.paintedOffset-scroll;if(translate!=slotTranslate[k]){slotTranslate[k]=translate;changed=true;}}
            else{std::size_t k=c.slot>=0?std::size_t(c.slot):slotCount();if(k==slotCount())for(std::size_t n=0;n<slotCount();++n)if(!owned[n]){k=n;break;}
                need(k<slotCount(),"No free long editor glyph slot");owned[k]=true;const auto before=std::pair(c.inner,c.extent);paintSlot(k,c,false);changed=true;
                if(before!=std::pair(c.inner,c.extent)){restack();return paintVisible()||true;}}}
        for(std::size_t k=0;k<slotCount();++k)if(!owned[k]&&!slotEmpty[k]){emptySlot(k);changed=true;}
        // Bounded retention of off-screen chunk layouts (least recently used).
        std::size_t laidOut{};for(const auto& c:chunks)if(c.layout)++laidOut;
        while(laidOut>limits.retainedLayouts+(visibleEnd-visibleBegin)){std::size_t oldest=chunks.size();for(std::size_t i=0;i<chunks.size();++i){if(!chunks[i].layout||(i>=visibleBegin&&i<visibleEnd))continue;if(oldest==chunks.size()||chunks[i].used<chunks[oldest].used)oldest=i;}
            if(oldest==chunks.size())break;evict(chunks[oldest]);--laidOut;}
        return changed;
    }
    // ---- geometry (document coordinates) -------------------------------------
    std::optional<text::RangeBounds> bounds(text::Range r){
        need(r.start<=r.end&&r.end<=value().size(),"Long editor ACP range exceeds its document");
        if(r.start==r.end){const auto i=chunkAt(r.start);ensureLayout(i);const auto& c=chunks[i];auto b=c.layout->bounds({r.start-c.start,r.start-c.start});if(b&&b->bounds!=core::Rect{})b->bounds.y+=c.top;return b;}
        const auto i0=chunkAt(r.start),i1=chunkAt(r.end-1);std::optional<core::Rect> all;bool clipped{};
        for(auto i=i0;i<=i1;++i){auto& c=chunks[i];core::Rect rect;
            if(i1-i0>8&&!c.layout){rect={0,c.top,style.width,chunkHeight(i)};clipped=true;}
            else{ensureLayout(i);const auto b=c.layout->bounds({std::max(r.start,c.start)-c.start,std::min(r.end,c.end())-c.start});if(!b||b->bounds==core::Rect{}){clipped=true;continue;}rect=b->bounds;rect.y+=c.top;clipped|=b->clipped;}
            if(!all)all=rect;else{const double l=std::min(all->x,rect.x),t=std::min(all->y,rect.y),right=std::max(all->x+all->width,rect.x+rect.width),bottom=std::max(all->y+all->height,rect.y+rect.height);all=core::Rect{l,t,right-l,bottom-t};}}
        if(!all)return text::RangeBounds{{},true};return text::RangeBounds{*all,clipped};
    }
    std::optional<std::uint32_t> hit(core::Point p,bool nearest,bool round){
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.y)>std::numeric_limits<float>::max())return {};
        // DirectWrite hit-tests the document point in float. Round it once, as
        // one whole-document layout would, then pick its chunk: a point on a
        // chunk's top belongs to that chunk's first line, never to the previous
        // chunk's empty line after its final break.
        const double y=static_cast<float>(p.y);auto i=chunkAtY(y);if(i+1<chunks.size()&&y-chunks[i].top>=chunks[i].inner)++i;
        ensureLayout(i);const auto& c=chunks[i];
        const auto at=c.layout->hit({p.x,y-c.top},nearest,round);return at?std::optional<std::uint32_t>(c.start+*at):std::nullopt;
    }
    std::span<const core::Rect> rectanglesFor(text::Range r,std::size_t first,std::size_t last){
        rectangles.clear();
        // A collapsed range is its caret, exactly as one painted layout reports it.
        if(r.start>=r.end){const auto caret=bounds({r.start,r.start});if(caret&&caret->bounds.width>0&&caret->bounds.height>0)rectangles.push_back(caret->bounds);return rectangles;}
        for(auto i=std::max(first,chunkAt(r.start));i<last&&i<chunks.size()&&chunks[i].start<r.end;++i){auto& c=chunks[i];if(c.end()<=r.start)continue;ensureLayout(i);
            for(auto rect:c.layout->selectionRectangles({std::max(r.start,c.start)-c.start,std::min(r.end,c.end())-c.start})){rect.y+=c.top;rectangles.push_back(rect);}}
        return rectangles;
    }
    std::uint32_t previousCluster(std::uint32_t acp){need(acp<=value().size(),"Cluster ACP exceeds document");if(acp==0)return 0;auto i=chunkAt(acp);if(chunks[i].start==acp&&i>0)--i;ensureLayout(i);const auto& c=chunks[i];return c.start+c.layout->previousCluster(acp-c.start);}
    std::uint32_t nextCluster(std::uint32_t acp){need(acp<=value().size(),"Cluster ACP exceeds document");if(acp==value().size())return acp;const auto i=chunkAt(acp);ensureLayout(i);const auto& c=chunks[i];return c.start+c.layout->nextCluster(acp-c.start);}
    // ---- structure, adornments, sync ----------------------------------------
    void load(){
        Json::Array children;children.push_back(emptyShape(selectionLeaf(),style.selectionColor));for(std::size_t k=0;k<slotCount();++k)children.push_back(emptyShape(glyphLeaf(k),style.textColor));
        children.push_back(emptyShape(compositionLeaf(),style.compositionColor));children.push_back(emptyShape(caretLeaf(),style.caretColor));
        scene->load(Json::Object{{"bounds",Json::Array{0,0,style.width,style.height}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"masksToBounds",true},{"children",std::move(children)}},options);
        need(scene->report().unsupported.empty(),"Long editor scene contains unsupported local effects");structureRevision=scene->contentRevision();shapeRevision=structureRevision;
        for(auto& r:slotRevision)r=structureRevision;
        for(std::size_t k=0;k<surfaces.size();++k){const auto surface=scene->surfaceIndex(ids[k]);need(surface.has_value(),"Long editor surface is missing");surfaces[k]=*surface;originals[k]=scene->draws()[*surface];
            need(originals[k].masks.size()==1&&originals[k].masks[0].bounds==core::Rect{0,0,style.width,style.height}&&originals[k].masks[0].worldToLocal==core::Matrix4{},"Long editor leaves must share their one root viewport mask");maskCounts[k]=originals[k].masks.size();}
        shapeContent={emptyShape(selectionLeaf(),style.selectionColor),emptyShape(compositionLeaf(),style.compositionColor),emptyShape(caretLeaf(),style.caretColor)};
    }
    Json adornment(unsigned kind,text::Range range){
        const auto index=kind==0?selectionLeaf():kind==1?compositionLeaf():caretLeaf();auto out=leaf(index,"shape");out["bounds"]=Json::Array{0,0,0,0};Json::Array commands;
        if(kind==2){if(caretRect.height>0)rectanglePath(commands,{0,0,1,caretRect.height});}
        else if(range.start!=range.end)for(auto r:rectanglesFor(range,visibleBegin,visibleEnd)){
            if(kind==1){r.y+=std::max(0.,r.height-1);r.height=1;}
            r.y-=scroll;const auto x=std::max(0.,r.x),y=std::max(0.,r.y),right=std::min(style.width,r.x+r.width),bottom=std::min(style.height,r.y+r.height);
            if(right>x&&bottom>y)rectanglePath(commands,{x,y,right-x,bottom-y});}
        const auto& c=kind==0?style.selectionColor:kind==1?style.compositionColor:style.caretColor;
        out["shape"]=Json::Object{{"path",std::move(commands)},{"fillColor",rgba(c)},{"strokeColor",nullptr},{"fillRule","non-zero"}};return out;
    }
    bool sync(){
        onThread();bool changed{},layoutChanged{};
        if(input){const auto ready=input->setPlacement(placement);if(ready==TS_E_NOLOCK||!alive)return false;checked(ready,"Check long editor layout transaction");}
        const bool fontChanged=paintedFontRevision!=raster->fontRevision();
        if(chunks.empty()){partitionAll();load();styleGeneration+=1;}
        else if(layoutRevision!=owner.revision()){revealPending=true;if(value()!=synced)repartition();}
        if(fontChanged||styleDirty){++styleGeneration;styleDirty=false;}
        if(recheck||layoutRevision!=owner.revision()||fontChanged||std::any_of(chunks.begin(),chunks.end(),[&](const Chunk& c){return c.dirty||c.styleGeneration!=styleGeneration;})){recheck=false;
            if(history&&!rich)need(!history->richText(),"Plain history field cannot flatten externally formatted text");
            rebuild();layoutRevision=owner.revision();paintedFontRevision=raster->fontRevision();adornedDocument=0;changed=layoutChanged=true;
            const auto next=std::clamp(scroll,0.,maximumScroll());viewportDirty|=next!=scroll;scroll=next;}
        if(revealPending){const auto selected=owner.selection();const auto at=selected.activeEnd==text::ActiveEnd::start?selected.range.start:selected.range.end;const auto box=bounds({at,at});
            if(box&&box->bounds.height>0){const auto& r=box->bounds;const auto ppp=options.pixelsPerPoint;auto next=scroll;if(r.y<next)next=std::floor(r.y*ppp)/ppp;else if(r.y+r.height>next+style.height)next=std::ceil((r.y+r.height-style.height)*ppp)/ppp;next=std::clamp(next,0.,maximumScroll());viewportDirty|=next!=scroll;scroll=next;}revealPending=false;}
        if(snap(wanted)!=scroll)wanted=scroll;
        const bool scrolled=paintVisible()||viewportDirty;viewportDirty=false;changed|=scrolled;
        const auto selection=owner.selection();const auto composition=owner.composition();
        if(scrolled||adornedDocument!=owner.revision()||selection!=adornedSelection||composition!=adornedComposition){
            const auto at=selection.activeEnd==text::ActiveEnd::start?selection.range.start:selection.range.end;
            const auto box=bounds({at,at});caretRect=box?box->bounds:core::Rect{};
            for(unsigned kind:{0u,1u,2u}){auto content=adornment(kind,kind==0?selection.range:composition.value_or(text::Range{}));
                if(content!=shapeContent[kind]){const auto index=kind==0?selectionLeaf():kind==1?compositionLeaf():caretLeaf();scene->updateLocalContent(ids[index],++shapeRevision,content,options);shapeContent[kind]=std::move(content);++paintRevision;}}
            adornedDocument=owner.revision();adornedSelection=selection;adornedComposition=composition;changed=true;
        }
        if(changed)poseReady=false;
        if(input){bool placementNotified{};if(placement.scroll!=core::Point{0,scroll}){auto next=placement;next.scroll={0,scroll};const auto hr=input->setPlacement(next);if(!alive)return changed;checked(hr,"Update scrolled long editor candidate geometry");placement=next;placementNotified=hr==S_OK;}
            if(layoutChanged&&!placementNotified)checked(input->layoutChanged(),"Notify current long editor layout");}
        return changed;
    }
    std::vector<text::Range> spacedSelection()const{
        if(normalizedParagraphs)return spacedParagraphs;const auto t=value();auto selected=owner.selection().range;if(selected.start==selected.end)return spacedParagraphs;
        const auto delimiter=[](char16_t c){return (c>=u'\n'&&c<=u'\r')||c==0x85||c==0x2028||c==0x2029;};
        while(selected.start&&!delimiter(t[selected.start-1]))--selected.start;
        --selected.end;while(selected.end<t.size()&&!delimiter(t[selected.end]))++selected.end;
        if(selected.end<t.size()){const auto c=t[selected.end++];if(c==u'\r'&&selected.end<t.size()&&t[selected.end]==u'\n')++selected.end;}
        std::vector<text::Range> joined;joined.reserve(spacedParagraphs.size()+1);bool inserted{};
        for(const auto r:spacedParagraphs){if(r.end<selected.start)joined.push_back(r);else if(selected.end<r.start){if(!inserted){joined.push_back(selected);inserted=true;}joined.push_back(r);}else{selected.start=std::min(selected.start,r.start);selected.end=std::max(selected.end,r.end);}}
        if(!inserted)joined.push_back(selected);return joined;
    }
    ProjectedEditorResult select(std::uint32_t target,bool extend,bool reveal=true){
        if(owner.composition())return {};const auto old=owner.selection();const auto anchor=extend?(old.activeEnd==text::ActiveEnd::start?old.range.end:old.range.start):target;
        const text::Selection next{{std::min(anchor,target),std::max(anchor,target)},target<anchor?text::ActiveEnd::start:text::ActiveEnd::end,false};
        const auto hr=input->selectFromHost(next);if(hr==TS_E_NOLOCK)return {true,false,false};checked(hr,"Select long editor text");revealPending|=reveal;return {true,next!=old,false};
    }
    ProjectedEditorResult replace(text::Range range,std::u16string_view replacement){const auto hr=input->replaceFromHost(range,replacement);if(hr==E_INVALIDARG||hr==TS_E_NOLOCK||hr==TS_E_READONLY)return {true,false,false};checked(hr,"Edit long editor document");revealPending=true;return {true,true,false};}
};

NativeNotesLongEditor::NativeNotesLongEditor(HWND hwnd,core::notes::RichDocument& doc,LayerScene& scene,LayerRasterizer& raster,ProjectedEditorStyle style,LayerRasterOptions options,UINT message,UINT_PTR generation,ProjectedEditorTextMode mode,NativeNotesLongEditorOptions limits)
    :impl_([&]{need(mode==ProjectedEditorTextMode::rich||mode==ProjectedEditorTextMode::plainHistory,"Invalid long editor text mode");need(mode!=ProjectedEditorTextMode::plainHistory||!doc.richText(),"Plain history mode cannot flatten imported rich text");
        return std::make_shared<Impl>(doc,scene,raster,std::move(style),std::move(options),limits,mode==ProjectedEditorTextMode::rich?&doc:nullptr,generation);}()){
    auto& i=*impl_;i.sync();i.input=std::make_unique<ProjectedTextInput>(hwnd,i.document,i.geometry,message,generation);
    i.placement={core::Projection{},{0,0,i.style.width,i.style.height},{0,i.scroll},false,i.style.cornerRadius};
    checked(i.input->setPlacement(i.placement),"Initialize concealed long editor placement");
}
NativeNotesLongEditor::~NativeNotesLongEditor(){auto i=impl_;if(!i)return;if(GetCurrentThreadId()!=i->thread)std::terminate();i->alive=false;i->input.reset();try{i->raster->remove(i->measureID);}catch(...){}}
HRESULT NativeNotesLongEditor::connect(ITfThreadMgr& manager,TfClientId client)noexcept{auto i=impl_;if(GetCurrentThreadId()!=i->thread)return RPC_E_WRONG_THREAD;return i->input->connect(manager,client);}
HRESULT NativeNotesLongEditor::focus(bool focused)noexcept{auto i=impl_;if(GetCurrentThreadId()!=i->thread)return RPC_E_WRONG_THREAD;i->highUnit.reset();i->dragging=false;return focused?i->input->focus():i->input->blur(true);}
HRESULT NativeNotesLongEditor::stop()noexcept{auto i=impl_;if(GetCurrentThreadId()!=i->thread)return RPC_E_WRONG_THREAD;i->highUnit.reset();i->dragging=false;const auto hr=i->input->stop();if(i->alive&&SUCCEEDED(hr))i->stopped=true;return hr;}
HRESULT NativeNotesLongEditor::commitComposition()noexcept{auto i=impl_;if(GetCurrentThreadId()!=i->thread)return RPC_E_WRONG_THREAD;i->highUnit.reset();return i->input->commitComposition();}
bool NativeNotesLongEditor::filterKeyMessage(UINT message,WPARAM key,LPARAM data)noexcept{auto i=impl_;return i->input->filterKeyMessage(message,key,data);}
unsigned NativeNotesLongEditor::takeChanges(UINT_PTR generation)noexcept{auto i=impl_;return i->input->takeChanges(generation);}
ITextStoreACP* NativeNotesLongEditor::textStore()const noexcept{return impl_->input->textStore();}
bool NativeNotesLongEditor::syncContent(){auto i=impl_;return i->sync();}
bool NativeNotesLongEditor::setStyle(const ProjectedEditorStyle& style){
    auto i=impl_;i->onThread();auto options=i->options;options.richTextDocument=i->rich!=nullptr||style.naturalParagraphSpacingOne;validate(style,options);
    need(style.width==i->style.width&&style.height==i->style.height,"Long editor viewport resize requires owner replacement");if(style==i->style)return false;
    auto painted=style;painted.cornerRadius=i->style.cornerRadius;i->styleDirty|=painted!=i->style;i->style=style;i->poseReady=false;return true;
}
bool NativeNotesLongEditor::setPose(const ProjectedEditorPose& pose){
    auto i=impl_;i->current();need(i->scene->contentRevision()==i->structureRevision,"Long editor scene structure was replaced");need(pose.localToScreen.finite()&&pose.screenToClip.finite()&&pose.pixelWidth>0&&pose.pixelHeight>0&&std::isfinite(pose.opacity)&&pose.opacity>=0&&pose.opacity<=1,"Invalid long editor pose");
    const auto inverse=core::source::inverseSourceMatrix(pose.localToScreen);
    const text::Placement placement{core::Projection::viewport(pose.screenToClip*pose.localToScreen,pose.pixelWidth,pose.pixelHeight),{0,0,i->style.width,i->style.height},{0,i->scroll},pose.visible,i->style.cornerRadius};
    if(i->poseReady&&i->pose.localToScreen==pose.localToScreen&&i->pose.screenToClip==pose.screenToClip&&i->pose.pixelWidth==pose.pixelWidth&&i->pose.pixelHeight==pose.pixelHeight&&i->pose.opacity==pose.opacity&&i->pose.visible==pose.visible&&i->pose.ownerFocused==pose.ownerFocused&&i->pose.caretVisible==pose.caretVisible)return false;
    const auto placementResult=i->input->setPlacement(placement);if(!i->alive)return false;checked(placementResult,"Update long editor candidate plane");
    const auto selection=i->owner.selection();const bool selectionVisible=selection.range.start!=selection.range.end;
    for(std::size_t k=0;k<i->surfaces.size();++k){auto& out=i->placements[k];out.surface=i->surfaces[k];out.world=pose.localToScreen*i->originals[k].world;out.opacity=pose.visible?pose.opacity:0;
        if(k==i->caretLeaf())out.world=out.world*core::Matrix4::translation(i->caretRect.x,i->caretRect.y-i->scroll);
        else if(k>=1&&k<=i->slotCount()){const auto slot=k-1;out.world=out.world*core::Matrix4::translation(0,i->slotTranslate[slot]);if(i->slotEmpty[slot])out.opacity=0;}
        if((k==i->selectionLeaf()&&!selectionVisible)||(k==i->compositionLeaf()&&!i->owner.composition())||(k==i->caretLeaf()&&(!pose.ownerFocused||!pose.caretVisible||selectionVisible)))out.opacity=0;
        for(std::size_t m=0;m<i->maskCounts[k];++m){i->masks[k][m]=i->originals[k].masks[m];i->masks[k][m].cornerRadius=i->style.cornerRadius;i->masks[k][m].worldToLocal=i->masks[k][m].worldToLocal*inverse;}out.masks={i->masks[k].data(),i->maskCounts[k]};}
    i->scene->setPlacements(i->placements);i->placement=placement;i->pose=pose;i->poseReady=i->poseSet=true;return true;
}
bool NativeNotesLongEditor::setScrollOffset(double offset){
    // Wheel deltas accumulate exactly; only the painted offset is snapped.
    auto i=impl_;i->current();need(std::isfinite(offset)&&std::abs(offset)<1e9,"Invalid long editor document offset");i->revealPending=false;i->wanted=std::clamp(offset,0.,i->maximumScroll());const auto next=std::clamp(i->snap(i->wanted),0.,i->maximumScroll());if(next==i->scroll)return false;
    const auto oldPlacement=i->placement;const auto oldScroll=i->scroll;const bool hadPose=i->poseSet;
    {auto nextPlacement=oldPlacement;nextPlacement.scroll={0,next};const auto hr=i->input->setPlacement(nextPlacement);if(hr==TS_E_NOLOCK||!i->alive)return false;checked(hr,"Scroll long editor candidate plane");i->placement=nextPlacement;}
    i->scroll=next;i->viewportDirty=true;
    try{i->sync();if(hadPose&&i->alive)setPose(i->pose);}catch(...){i->scroll=oldScroll;i->viewportDirty=true;i->poseReady=false;if(i->alive){i->input->setPlacement(oldPlacement);i->placement=oldPlacement;}throw;}return true;
}
bool NativeNotesLongEditor::scrollBy(double delta){need(std::isfinite(delta),"Invalid long editor scroll delta");return setScrollOffset((impl_->snap(impl_->wanted)==impl_->scroll?impl_->wanted:impl_->scroll)+delta);}
double NativeNotesLongEditor::scrollOffset()const noexcept{return impl_->scroll;}
double NativeNotesLongEditor::maximumScrollOffset()const noexcept{return impl_->maximumScroll();}
bool NativeNotesLongEditor::revealCaret(){auto i=impl_;i->onThread();i->revealPending=true;const bool changed=i->sync();if(changed&&i->poseSet&&i->alive)setPose(i->pose);return changed;}
ProjectedEditorResult NativeNotesLongEditor::applyFormat(const core::notes::FormatChange& change){
    auto i=impl_;i->current();if(!i->rich)return {};bool changed{};
    const auto hr=i->input->performHostEdit([&]()->std::optional<text::Change>{
        const auto oldLayout=i->richLayout;const auto revision=i->rich->revision();
        auto paragraphs=oldLayout?i->spacedSelection():std::vector<text::Range>{};
        const bool spacingChanged=oldLayout&&paragraphs!=i->spacedParagraphs;
        changed=i->rich->applyFormat(change);
        // Plain metrics become natural metrics in every chunk; otherwise the
        // per-chunk run/spacing comparison re-lays out only what changed.
        if(!oldLayout){i->richLayout=true;i->normalizedParagraphs=true;++i->styleGeneration;changed=true;}
        else if(spacingChanged){i->spacedParagraphs.swap(paragraphs);changed=true;}
        if(!oldLayout||spacingChanged||revision!=i->rich->revision()){i->revealPending=true;i->recheck=true;}
        return {};});
    if(hr==TS_E_NOLOCK||hr==TS_E_READONLY)return {true,false,false};checked(hr,"Format long rich document");return {true,changed,false};
}
ProjectedEditorResult NativeNotesLongEditor::undo(){
    auto i=impl_;i->current();if(!i->history)return {};bool changed{};
    const auto hr=i->input->performHostEdit([&]()->std::optional<text::Change>{const auto before=std::u16string(i->owner.text());changed=i->history->undo();if(!changed)return {};i->revealPending=true;
        if(before==i->owner.text())return {};return text::Change{0,static_cast<std::uint32_t>(before.size()),static_cast<std::uint32_t>(i->owner.text().size())};});
    if(hr==TS_E_NOLOCK||hr==TS_E_READONLY)return {true,false,false};checked(hr,"Undo long rich document");return {true,changed,false};
}
ProjectedEditorResult NativeNotesLongEditor::redo(){
    auto i=impl_;i->current();if(!i->history)return {};bool changed{};
    const auto hr=i->input->performHostEdit([&]()->std::optional<text::Change>{const auto before=std::u16string(i->owner.text());changed=i->history->redo();if(!changed)return {};i->revealPending=true;
        if(before==i->owner.text())return {};return text::Change{0,static_cast<std::uint32_t>(before.size()),static_cast<std::uint32_t>(i->owner.text().size())};});
    if(hr==TS_E_NOLOCK||hr==TS_E_READONLY)return {true,false,false};checked(hr,"Redo long rich document");return {true,changed,false};
}
std::optional<core::notes::TextStyle> NativeNotesLongEditor::selectionStyle()const{auto i=impl_;i->onThread();return i->rich?std::optional(i->rich->selectionStyle()):std::nullopt;}
bool NativeNotesLongEditor::richLayoutEnabled()const noexcept{return impl_->richLayout;}
ProjectedEditorResult NativeNotesLongEditor::command(ProjectedEditorCommand command,bool extend){
    auto i=impl_;i->onThread();i->highUnit.reset();
    if(command==ProjectedEditorCommand::finish){if(i->owner.composition()){const auto hr=i->input->cancelComposition();if(hr==TS_E_NOLOCK)return {true,false,false};checked(hr,"Cancel unconsumed IME composition");return {true,true,false};}return {true,false,true};}
    if(i->owner.composition())return {};i->current();const auto selection=i->owner.selection();auto range=selection.range;const auto at=selection.activeEnd==text::ActiveEnd::start?range.start:range.end;
    switch(command){
    case ProjectedEditorCommand::selectAll:{const auto n=std::uint32_t(i->value().size());const auto hr=i->input->selectFromHost({{0,n},text::ActiveEnd::end,false});if(hr==TS_E_NOLOCK)return {true,false,false};checked(hr,"Select entire long document");i->revealPending=true;return {true,true,false};}
    case ProjectedEditorCommand::left:case ProjectedEditorCommand::right:{const bool left=command==ProjectedEditorCommand::left;const auto target=!extend&&range.start!=range.end?(left?range.start:range.end):(left?i->previousCluster(at):i->nextCluster(at));return i->select(target,extend);}
    case ProjectedEditorCommand::documentStart:return i->select(0,extend);
    case ProjectedEditorCommand::documentEnd:return i->select(std::uint32_t(i->value().size()),extend);
    case ProjectedEditorCommand::backspace:case ProjectedEditorCommand::deleteForward:
        if(range.start==range.end){if(command==ProjectedEditorCommand::backspace)range.start=i->previousCluster(at);else range.end=i->nextCluster(at);}return range.start==range.end?ProjectedEditorResult{true,false,false}:i->replace(range,{});
    case ProjectedEditorCommand::up:case ProjectedEditorCommand::down:{const auto box=i->bounds({at,at});if(!box||box->bounds.height<=0)return {true,false,false};const auto& r=box->bounds;const auto target=i->hit({r.x,r.y+(command==ProjectedEditorCommand::up?-.5:1.5)*r.height},true,true);return target?i->select(*target,extend):ProjectedEditorResult{true,false,false};}
    case ProjectedEditorCommand::finish:break;
    }return {};
}
ProjectedEditorResult NativeNotesLongEditor::character(std::uint32_t value,bool scalar){auto i=impl_;i->onThread();if(i->owner.composition()){i->highUnit.reset();return {true,false,false};}if(value<32&&value!='\r'&&value!='\n'){i->highUnit.reset();return {true,false,false};}std::array<char16_t,2> units{};std::size_t size{};
    if(scalar){i->highUnit.reset();if(value>0x10ffff||(value>=0xd800&&value<=0xdfff))return {true,false,false};if(value>0xffff){const auto v=value-0x10000;units={char16_t(0xd800+(v>>10)),char16_t(0xdc00+(v&1023))};size=2;}else{units[0]=value=='\r'?u'\n':char16_t(value);size=1;}}
    else{if(value>0xffff)return {true,false,false};const auto unit=char16_t(value);if(high(unit)){i->highUnit=unit;return {true,false,false};}if(low(unit)){if(!i->highUnit)return {true,false,false};units={*i->highUnit,unit};size=2;i->highUnit.reset();}else{i->highUnit.reset();units[0]=unit=='\r'?u'\n':unit;size=1;}}
    return i->replace(i->owner.selection().range,{units.data(),size});}
ProjectedEditorResult NativeNotesLongEditor::replaceTextFromHost(text::Range range,std::u16string_view value){auto i=impl_;i->onThread();i->highUnit.reset();return i->replace(range,value);}
ProjectedEditorResult NativeNotesLongEditor::setSelectionFromHost(text::Selection selection){auto i=impl_;i->onThread();i->highUnit.reset();const auto previous=i->owner.selection();const auto hr=i->input->selectFromHost(selection);if(hr==E_INVALIDARG||hr==TS_E_NOLOCK)return {true,false,false};checked(hr,"Restore long editor selection");if(i->alive)i->revealPending=true;return {true,selection!=previous,false};}
ProjectedEditorResult NativeNotesLongEditor::pointerDown(core::Point point,bool extend){auto i=impl_;i->current();need(i->poseReady,"Set long editor pose before pointer input");if(i->owner.composition())return {};const auto at=text::projectedHit(i->document,i->geometry,point,i->placement,false,true);if(!at)return {};i->dragging=true;return i->select(*at,extend,false);}
ProjectedEditorResult NativeNotesLongEditor::pointerDrag(core::Point point){auto i=impl_;i->current();if(!i->dragging||i->owner.composition()||!i->poseReady||!i->placement.visible)return {};
    const auto local=i->placement.projection.unproject(point);if(!local)return {};
    const auto at=i->hit({local->x-i->placement.viewport.x,local->y-i->placement.viewport.y+i->scroll},true,true);return at?i->select(*at,true):ProjectedEditorResult{};}
void NativeNotesLongEditor::pointerUp()noexcept{if(GetCurrentThreadId()==impl_->thread)impl_->dragging=false;}
const core::text::Layout& NativeNotesLongEditor::geometry()const noexcept{return impl_->geometry;}
std::span<const core::Rect> NativeNotesLongEditor::selectionRectangles(text::Range range)const{auto i=impl_;i->current();need(range.start<=range.end&&range.end<=i->value().size(),"Long editor ACP range exceeds its document");return i->rectanglesFor(range,0,i->chunks.size());}
std::uint32_t NativeNotesLongEditor::previousCluster(std::uint32_t acp)const{auto i=impl_;i->current();return i->previousCluster(acp);}
std::uint32_t NativeNotesLongEditor::nextCluster(std::uint32_t acp)const{auto i=impl_;i->current();return i->nextCluster(acp);}
double NativeNotesLongEditor::documentHeight()const noexcept{return impl_->height;}
const text::Placement& NativeNotesLongEditor::placement()const noexcept{return impl_->placement;}
LayerScene& NativeNotesLongEditor::scene()const noexcept{return *impl_->scene;}
std::uint64_t NativeNotesLongEditor::paintRevision()const noexcept{return impl_->paintRevision;}
NativeNotesLongEditorStats NativeNotesLongEditor::stats()const noexcept{const auto& i=*impl_;auto out=i.counts;out.chunks=i.chunks.size();for(const auto& c:i.chunks)if(c.layout)++out.laidOutChunks;out.visibleChunks=i.visibleEnd-i.visibleBegin;out.documentHeight=i.height;return out;}

std::vector<NativeNotesLongEditorChunk> NativeNotesLongEditor::chunks()const{const auto& i=*impl_;std::vector<NativeNotesLongEditorChunk> out;out.reserve(i.chunks.size());
    for(std::size_t n=0;n<i.chunks.size();++n){const auto& c=i.chunks[n];out.push_back({c.start,c.length,c.top,i.chunkHeight(n),c.layout!=nullptr,c.slot});}return out;}
// ---- NativeNotesEditor -------------------------------------------------------
NativeNotesEditor::NativeNotesEditor(std::unique_ptr<NativeProjectedEditor> e):editor_(std::move(e)){need(std::get<0>(editor_)!=nullptr,"Notes editor requires an adapter");}
NativeNotesEditor::NativeNotesEditor(std::unique_ptr<NativeNotesLongEditor> e):editor_(std::move(e)){need(std::get<1>(editor_)!=nullptr,"Notes editor requires an adapter");}
NativeNotesEditor::~NativeNotesEditor()=default;
bool NativeNotesEditor::chunked()const noexcept{return editor_.index()==1;}
NativeProjectedEditor* NativeNotesEditor::projected()noexcept{return editor_.index()==0?std::get<0>(editor_).get():nullptr;}
NativeNotesLongEditor* NativeNotesEditor::longEditor()noexcept{return editor_.index()==1?std::get<1>(editor_).get():nullptr;}
#define EHUD_NOTES_EDITOR(call) return std::visit([&](auto& e)->decltype(auto){return e->call;},editor_)
HRESULT NativeNotesEditor::connect(ITfThreadMgr& m,TfClientId c)noexcept{EHUD_NOTES_EDITOR(connect(m,c));}
HRESULT NativeNotesEditor::focus(bool f)noexcept{EHUD_NOTES_EDITOR(focus(f));}
HRESULT NativeNotesEditor::stop()noexcept{EHUD_NOTES_EDITOR(stop());}
HRESULT NativeNotesEditor::commitComposition()noexcept{EHUD_NOTES_EDITOR(commitComposition());}
bool NativeNotesEditor::filterKeyMessage(UINT m,WPARAM w,LPARAM l)noexcept{EHUD_NOTES_EDITOR(filterKeyMessage(m,w,l));}
unsigned NativeNotesEditor::takeChanges(UINT_PTR g)noexcept{EHUD_NOTES_EDITOR(takeChanges(g));}
ITextStoreACP* NativeNotesEditor::textStore()const noexcept{EHUD_NOTES_EDITOR(textStore());}
bool NativeNotesEditor::syncContent(){EHUD_NOTES_EDITOR(syncContent());}
bool NativeNotesEditor::setStyle(const ProjectedEditorStyle& s){EHUD_NOTES_EDITOR(setStyle(s));}
bool NativeNotesEditor::setPose(const ProjectedEditorPose& p){EHUD_NOTES_EDITOR(setPose(p));}
bool NativeNotesEditor::setScrollOffset(double v){EHUD_NOTES_EDITOR(setScrollOffset(v));}
bool NativeNotesEditor::scrollBy(double v){EHUD_NOTES_EDITOR(scrollBy(v));}
double NativeNotesEditor::scrollOffset()const noexcept{EHUD_NOTES_EDITOR(scrollOffset());}
double NativeNotesEditor::maximumScrollOffset()const noexcept{EHUD_NOTES_EDITOR(maximumScrollOffset());}
bool NativeNotesEditor::revealCaret(){EHUD_NOTES_EDITOR(revealCaret());}
ProjectedEditorResult NativeNotesEditor::applyFormat(const core::notes::FormatChange& c){EHUD_NOTES_EDITOR(applyFormat(c));}
ProjectedEditorResult NativeNotesEditor::undo(){EHUD_NOTES_EDITOR(undo());}
ProjectedEditorResult NativeNotesEditor::redo(){EHUD_NOTES_EDITOR(redo());}
std::optional<core::notes::TextStyle> NativeNotesEditor::selectionStyle()const{EHUD_NOTES_EDITOR(selectionStyle());}
bool NativeNotesEditor::richLayoutEnabled()const noexcept{EHUD_NOTES_EDITOR(richLayoutEnabled());}
ProjectedEditorResult NativeNotesEditor::command(ProjectedEditorCommand c,bool extend){EHUD_NOTES_EDITOR(command(c,extend));}
ProjectedEditorResult NativeNotesEditor::character(std::uint32_t v,bool scalar){EHUD_NOTES_EDITOR(character(v,scalar));}
ProjectedEditorResult NativeNotesEditor::replaceTextFromHost(core::text::Range r,std::u16string_view v){EHUD_NOTES_EDITOR(replaceTextFromHost(r,v));}
ProjectedEditorResult NativeNotesEditor::setSelectionFromHost(core::text::Selection s){EHUD_NOTES_EDITOR(setSelectionFromHost(s));}
ProjectedEditorResult NativeNotesEditor::pointerDown(core::Point p,bool extend){EHUD_NOTES_EDITOR(pointerDown(p,extend));}
ProjectedEditorResult NativeNotesEditor::pointerDrag(core::Point p){EHUD_NOTES_EDITOR(pointerDrag(p));}
void NativeNotesEditor::pointerUp()noexcept{std::visit([](auto& e){e->pointerUp();},editor_);}
const core::text::Placement& NativeNotesEditor::placement()const noexcept{EHUD_NOTES_EDITOR(placement());}
LayerScene& NativeNotesEditor::scene()const noexcept{EHUD_NOTES_EDITOR(scene());}
std::uint64_t NativeNotesEditor::paintRevision()const noexcept{EHUD_NOTES_EDITOR(paintRevision());}
#undef EHUD_NOTES_EDITOR
const LayerTextLayout& NativeNotesEditor::layout()const{if(editor_.index()!=0)throw std::logic_error("A chunked Notes field has one layout per paragraph chunk; use geometry()");return std::get<0>(editor_)->layout();}
const core::text::Layout& NativeNotesEditor::geometry()const noexcept{if(editor_.index()==0)return std::get<0>(editor_)->layout();return std::get<1>(editor_)->geometry();}
std::span<const core::Rect> NativeNotesEditor::selectionRectangles(core::text::Range r)const{if(editor_.index()==0)return std::get<0>(editor_)->layout().selectionRectangles(r);return std::get<1>(editor_)->selectionRectangles(r);}
} // namespace endfield::native
#endif
