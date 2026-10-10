#include "native/projected_editor.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
namespace text=core::text;using Json=ehud::data::Json;
void need(bool value,const char* reason){if(!value)throw std::invalid_argument(reason);}
void checked(HRESULT hr,const char* reason){if(FAILED(hr))throw std::runtime_error(reason);}
bool high(char16_t c){return c>=0xd800&&c<=0xdbff;}bool low(char16_t c){return c>=0xdc00&&c<=0xdfff;}
std::string utf8(std::u16string_view value){if(value.empty())return {};need(text::Buffer::validUTF16(value),"Editor document contains invalid UTF16");
    const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(value.data()),int(value.size()),nullptr,0,nullptr,nullptr);need(n>0,"Cannot encode editor UTF16");std::string result(std::size_t(n),'\0');need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(value.data()),int(value.size()),result.data(),n,nullptr,nullptr)==n,"Cannot encode editor UTF16");return result;}
Json rgba(const std::array<double,4>& c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
Json command(const char* op,double x,double y){Json::Array points;points.emplace_back(Json::Array{x,y});return Json::Object{{"op",op},{"points",std::move(points)}};}
void rectanglePath(Json::Array& commands,core::Rect r){if(r.width<=0||r.height<=0)return;commands.push_back(command("move",r.x,r.y));commands.push_back(command("line",r.x+r.width,r.y));commands.push_back(command("line",r.x+r.width,r.y+r.height));commands.push_back(command("line",r.x,r.y+r.height));commands.push_back(Json::Object{{"op","close"},{"points",Json::Array{}}});}
void validate(const ProjectedEditorStyle& s,const LayerRasterOptions& o){
    for(double n:{s.width,s.height,s.fontSize})need(std::isfinite(n)&&n>0,"Invalid short editor dimensions/font size");
    need(std::isfinite(s.lineHeight)&&std::isfinite(s.baseline)&&((s.lineHeight==0&&s.baseline==0)||(s.lineHeight>0&&s.lineHeight<=32768&&s.baseline>0&&s.baseline<=s.lineHeight)),"Invalid explicit editor line height/baseline");
    switch(s.alignment){case ProjectedEditorAlignment::natural:case ProjectedEditorAlignment::left:case ProjectedEditorAlignment::center:case ProjectedEditorAlignment::right:break;default:need(false,"Invalid projected editor alignment");}
    need(!s.sourceSingleLineField||(!s.wrapped&&!s.naturalParagraphSpacingOne&&s.lineHeight==0&&s.baseline==0&&s.width>6&&!o.richTextDocument),"Single-line field requires natural plain no-wrap style");
    need(!s.naturalParagraphSpacingOne||(s.lineHeight==0&&s.baseline==0),"Natural paragraph spacing cannot use uniform line metrics");
    need(std::isfinite(s.cornerRadius)&&s.cornerRadius>=0&&s.cornerRadius<=std::min(s.width,s.height)*.5,"Invalid editor rounded viewport radius");
    need(s.fontSize>=1&&s.fontSize<=2048&&Json::validUtf8(s.fontFamily)&&!s.fontFamily.empty()&&Json::validUtf8(s.fontFace),"Invalid short editor font");
    for(const auto* c:{&s.textColor,&s.caretColor,&s.selectionColor,&s.compositionColor})for(double n:*c)need(std::isfinite(n)&&n>=0&&n<=1,"Invalid editor straight-sRGB color");
    need(std::isfinite(o.pixelsPerPoint)&&o.pixelsPerPoint>=.25&&o.pixelsPerPoint<=4,"Invalid editor raster scale");
    const auto w=std::ceil(s.width*o.pixelsPerPoint),h=std::ceil(s.height*o.pixelsPerPoint);
    need(w<=8192&&h<=8192&&w*h<=LayerRasterizer::maximumPixels,"Short editor viewport exceeds local raster capacity");
}
// Caps only this explicitly bounded field, without changing caller storage or
// copying its rich/undo/composition model. Every transaction goes to the owner.
class BoundedDocument final:public text::Document {
    text::Document* owner_;std::uint32_t maximum_;
public:
    BoundedDocument(text::Document& d,std::uint32_t n):owner_(&d),maximum_(n){need(n>0&&n<=65536&&d.text().size()<=n,"Short editor capacity exceeded; use long-document viewport adapter");}
    std::u16string_view text()const noexcept override{return owner_->text();}
    text::Selection selection()const noexcept override{return owner_->selection();}
    std::uint64_t revision()const noexcept override{return owner_->revision();}
    std::uint32_t maximumUnits()const noexcept override{return std::min(maximum_,owner_->maximumUnits());}
    bool readOnly()const noexcept override{return owner_->readOnly();}
    void beginInputTransaction()override{owner_->beginInputTransaction();}void endInputTransaction()noexcept override{owner_->endInputTransaction();}
    void setSelection(text::Selection s)override{owner_->setSelection(s);}
    text::Change replace(text::Range r,std::u16string_view s)override{need(r.start<=r.end&&r.end<=text().size()&&text().size()-(r.end-r.start)<=maximumUnits()&&s.size()<=maximumUnits()-(text().size()-(r.end-r.start)),"Short editor capacity exceeded; no text was truncated");return owner_->replace(r,s);}
    void beginComposition(text::Range r)override{owner_->beginComposition(r);}void updateComposition(text::Range r)override{owner_->updateComposition(r);}
    std::optional<text::Range>composition()const noexcept override{return owner_->composition();}std::optional<text::Change>endComposition(bool cancel)override{return owner_->endComposition(cancel);}
};
}
struct NativeProjectedEditor::Impl {
    DWORD thread{GetCurrentThreadId()};bool alive{true},stopped{},dragging{},poseReady{},poseSet{},styleDirty{true},viewportDirty{},revealPending{};
    BoundedDocument document;core::notes::RichDocument* rich{};core::notes::RichDocument*history{};bool richLayout{},normalizedParagraphs{};std::vector<text::Range> spacedParagraphs;std::u16string paragraphText;LayerScene* scene;ProjectedEditorStyle style;LayerRasterOptions options;
    std::unique_ptr<LayerTextLayout> layout;std::unique_ptr<ProjectedTextInput> input;
    std::uint64_t glyphRevision{},shapeRevision{},paintRevision{},structureRevision{},paintedFontRevision{},paintedDocument{},adornedDocument{};
    text::Selection adornedSelection;std::optional<text::Range>adornedComposition;std::optional<char16_t>highUnit;
    std::array<std::size_t,4> surfaces{};std::array<DrawObject,4> originals;std::array<LayerPlacement,4> placements;
    std::array<std::array<PlaneMask,8>,4> masks;std::array<std::size_t,4> maskCounts{};
    text::Placement placement;ProjectedEditorPose pose;double scroll{},horizontal{};core::Rect caretRect;Json glyphContent;std::array<Json,4>shapeContent;
    static constexpr std::array<const char*,4> ids{"projected-editor-selection","projected-editor-glyphs","projected-editor-composition","projected-editor-caret"};
    Impl(text::Document& d,LayerScene& s,ProjectedEditorStyle styleValue,LayerRasterOptions opts,PlainEditorFixtureCapacity capacity,core::notes::RichDocument* richValue=nullptr,core::notes::RichDocument* historyValue=nullptr)
        :document(d,capacity.maximumUnits),rich(richValue),history(historyValue?historyValue:richValue),scene(&s),style(std::move(styleValue)),options(std::move(opts)){
        richLayout=rich&&rich->richText().has_value();if(rich)paragraphText=document.text();options.paddingPoints=0;options.retainEmptyTextLayout=true;options.plainTextDocument=!rich&&!style.naturalParagraphSpacingOne;options.richTextDocument=rich!=nullptr||style.naturalParagraphSpacingOne;options.textDocumentOffset={};options.retainedPlainText.reset();options.revealPlainTextPosition.reset();validate(style,options);need(s.contentRevision()==0&&s.draws().empty(),"Projected editor needs its dedicated initially empty borrowed scene");}
    void onThread()const{if(GetCurrentThreadId()!=thread)throw std::logic_error("Projected editor used outside its creating thread");need(alive&&!stopped,"Projected editor is stopped");}
    void current()const{onThread();need(layout&&layout->textRevision()==document.revision(),"Synchronize editor text before geometry/navigation");}
    Json base(unsigned slot)const{return Json::Object{{"id",ids[slot]},{"class","CAShapeLayer"},{"kind","shape"},{"bounds",Json::Array{0,0,style.width,style.height}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"opacity",1},{"children",Json::Array{}}};}
    void reconcileParagraphs(){
        if(!rich||paragraphText==document.text())return;const auto next=document.text();
        std::size_t prefix{};while(prefix<paragraphText.size()&&prefix<next.size()&&paragraphText[prefix]==next[prefix])++prefix;
        std::size_t suffix{};while(suffix<paragraphText.size()-prefix&&suffix<next.size()-prefix&&paragraphText[paragraphText.size()-1-suffix]==next[next.size()-1-suffix])++suffix;
        const auto oldEnd=paragraphText.size()-suffix,newEnd=next.size()-suffix;std::vector<text::Range> shifted;shifted.reserve(spacedParagraphs.size()+1);
        for(const auto r:spacedParagraphs){if(r.end<=prefix)shifted.push_back(r);else if(r.start>=oldEnd)shifted.push_back({static_cast<std::uint32_t>(r.start-oldEnd+newEnd),static_cast<std::uint32_t>(r.end-oldEnd+newEnd)});
            else shifted.push_back({static_cast<std::uint32_t>(std::min<std::size_t>(r.start,prefix)),static_cast<std::uint32_t>(r.end<=oldEnd?newEnd:r.end-oldEnd+newEnd)});}
        spacedParagraphs=std::move(shifted);paragraphText=next;
    }
    std::vector<text::Range> spacedSelection()const{
        if(normalizedParagraphs)return spacedParagraphs;const auto value=document.text();auto selected=document.selection().range;if(selected.start==selected.end)return spacedParagraphs;
        const auto delimiter=[](char16_t c){return (c>=u'\n'&&c<=u'\r')||c==0x85||c==0x2028||c==0x2029;};
        while(selected.start&& !delimiter(value[selected.start-1]))--selected.start;
        // Selection end is exclusive; a boundary immediately after a newline
        // belongs to the preceding selected paragraph, not the following one.
        --selected.end;while(selected.end<value.size()&&!delimiter(value[selected.end]))++selected.end;
        if(selected.end<value.size()){const auto c=value[selected.end++];if(c==u'\r'&&selected.end<value.size()&&value[selected.end]==u'\n')++selected.end;}
        std::vector<text::Range> joined;joined.reserve(spacedParagraphs.size()+1);bool inserted{};
        for(const auto r:spacedParagraphs){if(r.end<selected.start)joined.push_back(r);else if(selected.end<r.start){if(!inserted){joined.push_back(selected);inserted=true;}joined.push_back(r);}else{selected.start=std::min(selected.start,r.start);selected.end=std::max(selected.end,r.end);}}
        if(!inserted)joined.push_back(selected);return joined;
    }
    Json glyph()const{
        auto out=base(1);out["class"]="CATextLayer";out["kind"]="text";
        const char* alignment="natural";switch(style.alignment){case ProjectedEditorAlignment::natural:break;case ProjectedEditorAlignment::left:alignment="left";break;case ProjectedEditorAlignment::center:alignment="center";break;case ProjectedEditorAlignment::right:alignment="right";break;}
        out["text"]=Json::Object{{"string",utf8(document.text())},{"fontSize",style.fontSize},{"font",Json::Object{{"familyName",style.fontFamily},{"postScriptName",style.fontFace},{"pointSize",style.fontSize}}},{"foregroundColor",rgba(style.textColor)},{"alignment",alignment},{"wrapped",style.wrapped},{"truncation","none"},{"runs",Json::Array{}}};
        if(style.sourceSingleLineField)out["text"]["sourceSingleLineField"]=true;
        if(style.lineHeight>0&&!richLayout){out["text"]["font"]["ascender"]=style.baseline;out["text"]["font"]["descender"]=style.baseline-style.lineHeight;out["text"]["font"]["leading"]=0;}
        if(rich){
            Json::Array runs;runs.reserve(rich->runs().size());
            for(const auto&r:rich->runs()){const auto&st=r.style;const auto name=st.fontName&& !st.fontName->starts_with(".")?*st.fontName:style.fontFamily;
                Json font=Json::Object{{"familyName",name},{"postScriptName",st.fontName&& !st.fontName->starts_with(".")?*st.fontName:style.fontFace},{"pointSize",st.fontSize},{"symbolicTraits",(st.bold?2:0)|(st.italic?1:0)},{"preserveUserFont",st.fontName&& !st.fontName->starts_with(".")}};
                const auto c=st.color?std::array<double,4>{st.color->red,st.color->green,st.color->blue,st.color->alpha}:style.textColor;
                runs.emplace_back(Json::Object{{"utf16Range",Json::Array{double(r.location),double(r.length)}},{"attributes",Json::Object{{"NSFont",std::move(font)},{"NSColor",rgba(c)},{"NSUnderline",st.underline?1:0},{"NSStrikethrough",st.strikethrough?1:0}}}});
            }
            out["text"]["runs"]=std::move(runs);
        }
        if(rich||style.naturalParagraphSpacingOne){Json::Array spacing;
            if(style.naturalParagraphSpacingOne||normalizedParagraphs)spacing.emplace_back(Json::Object{{"start",0},{"end",double(document.text().size())},{"points",1}});
            else for(const auto r:spacedParagraphs)spacing.emplace_back(Json::Object{{"start",double(r.start)},{"end",double(r.end)},{"points",1}});
            out["text"]["paragraphLineSpacing"]=std::move(spacing);
        }return out;
    }
    Json adornment(unsigned slot,text::Range range)const{auto out=base(slot);out["bounds"]=Json::Array{0,0,0,0};Json::Array commands;
        if(slot==3){if(caretRect.height>0)rectanglePath(commands,{0,0,1,caretRect.height});}
        else if(layout&&range.start!=range.end)for(auto r:layout->selectionRectangles(range)){
            if(slot==2){r.y+=std::max(0.,r.height-1);r.height=1;}
            r.x-=horizontal;r.y-=scroll;const auto x=std::max(0.,r.x),y=std::max(0.,r.y),right=std::min(style.width,r.x+r.width),bottom=std::min(style.height,r.y+r.height);
            if(right>x&&bottom>y)rectanglePath(commands,{x,y,right-x,bottom-y});}
        const auto& c=slot==0?style.selectionColor:slot==2?style.compositionColor:style.caretColor;
        out["shape"]=Json::Object{{"path",std::move(commands)},{"fillColor",rgba(c)},{"strokeColor",nullptr},{"fillRule","non-zero"}};return out;}
    LayerRasterOptions glyphOptions(bool reuse)const{auto out=options;out.textDocumentOffset={horizontal,scroll};if(reuse)out.retainedPlainText=layout->painted();else if(revealPending){const auto selected=document.selection();out.revealPlainTextPosition=selected.activeEnd==text::ActiveEnd::start?selected.range.start:selected.range.end;}return out;}
    double maximumScroll()const noexcept{return layout?std::max(0.,layout->painted()->documentHeight()-style.height):0;}
    double maximumHorizontal()const noexcept{return style.sourceSingleLineField&&layout?std::max(0.,layout->painted()->documentWidth()-style.width):0;}
    void paintViewport(){scene->updateLocalContent(ids[1],++glyphRevision,glyphContent,glyphOptions(true));viewportDirty=false;++paintRevision;}
    void build(){
        need(document.text().size()<=document.maximumUnits(),"Caller text exceeds explicitly bounded editor capacity");reconcileParagraphs();glyphContent=glyph();
        options.richTextDocument=rich!=nullptr||style.naturalParagraphSpacingOne;options.plainTextDocument=!options.richTextDocument;
        if(!layout){
            Json::Array children;for(unsigned k=0;k<4;++k){if(k==1)children.push_back(glyphContent);else{shapeContent[k]=adornment(k,{});children.push_back(shapeContent[k]);}}
            scene->load(Json::Object{{"bounds",Json::Array{0,0,style.width,style.height}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"masksToBounds",true},{"children",std::move(children)}},options);
            need(scene->report().unsupported.empty(),"Projected editor scene contains unsupported local effects");structureRevision=scene->contentRevision();
            // Loaded leaves already own the structure's local revision. The
            // first format/style-only change must advance beyond it, otherwise
            // LayerScene correctly treats our reused token as unchanged.
            glyphRevision=shapeRevision=structureRevision;
            for(unsigned k=0;k<4;++k){const auto surface=scene->surfaceIndex(ids[k]);need(surface.has_value(),"Projected editor surface is missing");surfaces[k]=*surface;originals[k]=scene->draws()[*surface];need(originals[k].masks.size()==1&&originals[k].masks[0].bounds==core::Rect{0,0,style.width,style.height}&&originals[k].masks[0].worldToLocal==core::Matrix4{},"Editor leaves must share their one root viewport mask");maskCounts[k]=originals[k].masks.size();}
            layout=std::make_unique<LayerTextLayout>(scene->paintedTextLayout(ids[1]),document);
        }else{
            need(scene->contentRevision()==structureRevision,"Borrowed editor scene was replaced externally");
            scene->updateLocalContent(ids[1],++glyphRevision,glyphContent,glyphOptions(false));layout->bind(scene->paintedTextLayout(ids[1]),document);
        }
        horizontal=layout->painted()->initialPaintOffset().x;scroll=layout->painted()->initialPaintOffset().y;viewportDirty=false;revealPending=false;
        paintedFontRevision=scene->currentFontRevision();paintedDocument=document.revision();styleDirty=false;adornedDocument=0;++paintRevision;
    }
    bool sync(){
        onThread();bool changed{},layoutChanged{};
        // An equal placement is a no-op, but still checks the TSF lock before
        // any content/pixel mutation. Owner retries after its queued change.
        if(input){const auto ready=input->setPlacement(placement);if(ready==TS_E_NOLOCK||!alive)return false;checked(ready,"Check editor layout transaction");}
        if(layout&&paintedDocument!=document.revision())revealPending=true;
        if(!layout||paintedDocument!=document.revision()||paintedFontRevision!=scene->currentFontRevision()||styleDirty){if(history&&!rich)need(!history->richText(),"Plain history field cannot flatten externally formatted text");build();changed=layoutChanged=true;}
        if(revealPending){const auto selected=document.selection();const auto at=selected.activeEnd==text::ActiveEnd::start?selected.range.start:selected.range.end;const auto box=layout->bounds({at,at});
            if(box){const auto&r=box->bounds;auto next=scroll;if(r.y<next)next=r.y;else if(r.y+r.height>next+style.height)next=r.y+r.height-style.height;next=std::clamp(next,0.,maximumScroll());auto nextX=horizontal;if(style.sourceSingleLineField){if(r.x<nextX)nextX=r.x;else if(r.x+r.width>nextX+style.width)nextX=r.x+r.width-style.width;nextX=std::clamp(nextX,0.,maximumHorizontal());}viewportDirty|=next!=scroll||nextX!=horizontal;scroll=next;horizontal=nextX;}revealPending=false;}
        const bool scrolled=viewportDirty;if(viewportDirty){paintViewport();changed=true;}
        const auto selection=document.selection();const auto composition=document.composition();
        if(scrolled||adornedDocument!=document.revision()||selection!=adornedSelection||composition!=adornedComposition){
            const auto at=selection.activeEnd==text::ActiveEnd::start?selection.range.start:selection.range.end;
            const auto box=layout->bounds({at,at});caretRect=box?box->bounds:core::Rect{};
            for(unsigned slot:{0u,2u,3u}){auto content=adornment(slot,slot==0?selection.range:composition.value_or(text::Range{}));
                if(content!=shapeContent[slot]){scene->updateLocalContent(ids[slot],++shapeRevision,content,options);shapeContent[slot]=std::move(content);++paintRevision;}}
            adornedDocument=document.revision();adornedSelection=selection;adornedComposition=composition;changed=true;
        }
        if(changed)poseReady=false;
        // Selection/composition notifications already came from the text store.
        // Only a new glyph layout needs another TSF layout invalidation.
        if(input){bool placementNotified{};if(placement.scroll!=core::Point{horizontal,scroll}){auto next=placement;next.scroll={horizontal,scroll};const auto hr=input->setPlacement(next);if(!alive)return changed;checked(hr,"Update scrolled candidate geometry");placement=next;placementNotified=hr==S_OK;}
            if(layoutChanged&&!placementNotified)checked(input->layoutChanged(),"Notify current painted editor layout");}return changed;
    }
    ProjectedEditorResult select(std::uint32_t target,bool extend,bool reveal=true){
        if(document.composition())return {};const auto old=document.selection();const auto anchor=extend?(old.activeEnd==text::ActiveEnd::start?old.range.end:old.range.start):target;
        const text::Selection next{{std::min(anchor,target),std::max(anchor,target)},target<anchor?text::ActiveEnd::start:text::ActiveEnd::end,false};
        const auto hr=input->selectFromHost(next);if(hr==TS_E_NOLOCK)return {true,false,false};checked(hr,"Select projected editor text");revealPending|=reveal;return {true,next!=old,false};
    }
    ProjectedEditorResult replace(text::Range range,std::u16string_view value){const auto hr=input->replaceFromHost(range,value);if(hr==E_INVALIDARG||hr==TS_E_NOLOCK||hr==TS_E_READONLY)return {true,false,false};checked(hr,"Edit projected document");revealPending=true;return {true,true,false};}
};
NativeProjectedEditor::NativeProjectedEditor(HWND hwnd,text::Document& doc,LayerScene& scene,ProjectedEditorStyle style,LayerRasterOptions options,PlainEditorFixtureCapacity capacity,UINT message,UINT_PTR generation)
    :impl_([&]{need(dynamic_cast<core::notes::RichDocument*>(&doc)==nullptr,"Rich document requires explicit rich editor overload");return std::make_shared<Impl>(doc,scene,std::move(style),std::move(options),capacity);}()){
    auto& i=*impl_;i.sync();i.input=std::make_unique<ProjectedTextInput>(hwnd,i.document,*i.layout,message,generation);
    // Session offsets may be restored before the first projected pose. Keep a
    // valid, concealed candidate plane so that those updates still honor TSF
    // transaction locks without exposing fabricated screen coordinates.
    i.placement={core::Projection{},{0,0,i.style.width,i.style.height},{i.horizontal,i.scroll},false,i.style.cornerRadius};
    checked(i.input->setPlacement(i.placement),"Initialize concealed editor placement");
}
NativeProjectedEditor::NativeProjectedEditor(HWND hwnd,core::notes::RichDocument&doc,LayerScene&scene,ProjectedEditorStyle style,LayerRasterOptions options,PlainEditorFixtureCapacity capacity,UINT message,UINT_PTR generation,ProjectedEditorTextMode mode)
    :impl_([&]{need(mode==ProjectedEditorTextMode::rich||mode==ProjectedEditorTextMode::plainHistory,"Invalid projected text mode");need(mode!=ProjectedEditorTextMode::plainHistory||!doc.richText(),"Plain history mode cannot flatten imported rich text");return std::make_shared<Impl>(doc,scene,std::move(style),std::move(options),capacity,mode==ProjectedEditorTextMode::rich?&doc:nullptr,&doc);}()){
    auto&i=*impl_;i.sync();i.input=std::make_unique<ProjectedTextInput>(hwnd,i.document,*i.layout,message,generation);
    i.placement={core::Projection{},{0,0,i.style.width,i.style.height},{i.horizontal,i.scroll},false,i.style.cornerRadius};
    checked(i.input->setPlacement(i.placement),"Initialize concealed rich editor placement");
}
NativeProjectedEditor::~NativeProjectedEditor(){auto i=impl_;if(!i)return;if(GetCurrentThreadId()!=i->thread)std::terminate();i->alive=false;i->input.reset();}
HRESULT NativeProjectedEditor::connect(ITfThreadMgr& manager,TfClientId client)noexcept{auto i=impl_;if(GetCurrentThreadId()!=i->thread)return RPC_E_WRONG_THREAD;return i->input->connect(manager,client);}
HRESULT NativeProjectedEditor::focus(bool focused)noexcept{auto i=impl_;if(GetCurrentThreadId()!=i->thread)return RPC_E_WRONG_THREAD;i->highUnit.reset();i->dragging=false;return focused?i->input->focus():i->input->blur(true);}
HRESULT NativeProjectedEditor::stop()noexcept{auto i=impl_;if(GetCurrentThreadId()!=i->thread)return RPC_E_WRONG_THREAD;i->highUnit.reset();i->dragging=false;const auto hr=i->input->stop();if(i->alive&&SUCCEEDED(hr))i->stopped=true;return hr;}
HRESULT NativeProjectedEditor::commitComposition()noexcept{auto i=impl_;if(GetCurrentThreadId()!=i->thread)return RPC_E_WRONG_THREAD;i->highUnit.reset();return i->input->commitComposition();}
bool NativeProjectedEditor::filterKeyMessage(UINT message,WPARAM key,LPARAM data)noexcept{auto i=impl_;return i->input->filterKeyMessage(message,key,data);}
unsigned NativeProjectedEditor::takeChanges(UINT_PTR generation)noexcept{auto i=impl_;return i->input->takeChanges(generation);}
ITextStoreACP* NativeProjectedEditor::textStore()const noexcept{return impl_->input->textStore();}
bool NativeProjectedEditor::syncContent(){auto i=impl_;return i->sync();}
bool NativeProjectedEditor::setStyle(const ProjectedEditorStyle& style){auto i=impl_;i->onThread();auto options=i->options;options.richTextDocument=i->rich!=nullptr||style.naturalParagraphSpacingOne;validate(style,options);need(style.width==i->style.width&&style.height==i->style.height,"Short editor viewport resize requires owner replacement; long-document relayout is separate");if(style==i->style)return false;auto paintedStyle=style;paintedStyle.cornerRadius=i->style.cornerRadius;i->styleDirty|=paintedStyle!=i->style;i->style=style;i->poseReady=false;return true;}
bool NativeProjectedEditor::setPose(const ProjectedEditorPose& pose){
    auto i=impl_;i->current();need(i->scene->contentRevision()==i->structureRevision,"Editor scene structure was replaced");need(pose.localToScreen.finite()&&pose.screenToClip.finite()&&pose.pixelWidth>0&&pose.pixelHeight>0&&std::isfinite(pose.opacity)&&pose.opacity>=0&&pose.opacity<=1,"Invalid projected editor pose");
    const auto inverse=core::source::inverseSourceMatrix(pose.localToScreen);
    const text::Placement placement{core::Projection::viewport(pose.screenToClip*pose.localToScreen,pose.pixelWidth,pose.pixelHeight),{0,0,i->style.width,i->style.height},{i->horizontal,i->scroll},pose.visible,i->style.cornerRadius};
    if(i->poseReady&&i->pose.localToScreen==pose.localToScreen&&i->pose.screenToClip==pose.screenToClip&&i->pose.pixelWidth==pose.pixelWidth&&i->pose.pixelHeight==pose.pixelHeight&&i->pose.opacity==pose.opacity&&i->pose.visible==pose.visible&&i->pose.ownerFocused==pose.ownerFocused&&i->pose.caretVisible==pose.caretVisible)return false;
    const auto placementResult=i->input->setPlacement(placement);if(!i->alive)return false;checked(placementResult,"Update projected editor candidate plane");
    const auto selection=i->document.selection();const bool selectionVisible=selection.range.start!=selection.range.end;
    for(unsigned k=0;k<4;++k){auto& out=i->placements[k];out.surface=i->surfaces[k];out.world=pose.localToScreen*i->originals[k].world;if(k==3)out.world=out.world*core::Matrix4::translation(i->caretRect.x-i->horizontal,i->caretRect.y-i->scroll);out.opacity=pose.visible?pose.opacity:0;
        if((k==0&&!selectionVisible)||(k==2&&!i->document.composition())||(k==3&&(!pose.ownerFocused||!pose.caretVisible||selectionVisible)))out.opacity=0;
        for(std::size_t m=0;m<i->maskCounts[k];++m){i->masks[k][m]=i->originals[k].masks[m];i->masks[k][m].cornerRadius=i->style.cornerRadius;i->masks[k][m].worldToLocal=i->masks[k][m].worldToLocal*inverse;}out.masks={i->masks[k].data(),i->maskCounts[k]};}
    i->scene->setPlacements(i->placements);i->placement=placement;i->pose=pose;i->poseReady=i->poseSet=true;return true;
}
bool NativeProjectedEditor::setScrollOffset(double offset){
    auto i=impl_;i->current();need(std::isfinite(offset),"Invalid editor document offset");i->revealPending=false;const auto next=std::clamp(offset,0.,i->maximumScroll());if(next==i->scroll)return false;
    const auto oldPlacement=i->placement;const auto oldScroll=i->scroll;const bool hadPose=i->poseSet;
    {auto nextPlacement=oldPlacement;nextPlacement.scroll={i->horizontal,next};const auto hr=i->input->setPlacement(nextPlacement);if(hr==TS_E_NOLOCK||!i->alive)return false;checked(hr,"Scroll projected editor candidate plane");i->placement=nextPlacement;}
    i->scroll=next;i->viewportDirty=true;
    try{i->sync();if(hadPose&&i->alive)setPose(i->pose);}catch(...){i->scroll=oldScroll;i->viewportDirty=true;i->poseReady=false;if(i->alive){i->input->setPlacement(oldPlacement);i->placement=oldPlacement;}throw;}return true;
}
bool NativeProjectedEditor::scrollBy(double delta){need(std::isfinite(delta),"Invalid editor scroll delta");return setScrollOffset(impl_->scroll+delta);}
double NativeProjectedEditor::scrollOffset()const noexcept{return impl_->scroll;}
double NativeProjectedEditor::maximumScrollOffset()const noexcept{return impl_->maximumScroll();}
bool NativeProjectedEditor::setHorizontalScrollOffset(double offset){
    auto i=impl_;i->current();need(std::isfinite(offset),"Invalid horizontal field offset");need(i->style.sourceSingleLineField,"Horizontal scroll requires an explicit single-line field");i->revealPending=false;const auto next=std::clamp(offset,0.,i->maximumHorizontal());if(next==i->horizontal)return false;
    const auto oldPlacement=i->placement;const auto old=i->horizontal;const bool hadPose=i->poseSet;
    {auto proposed=oldPlacement;proposed.scroll={next,i->scroll};const auto hr=i->input->setPlacement(proposed);if(hr==TS_E_NOLOCK||!i->alive)return false;checked(hr,"Scroll projected field candidate plane");i->placement=proposed;}
    i->horizontal=next;i->viewportDirty=true;try{i->sync();if(hadPose&&i->alive)setPose(i->pose);}catch(...){i->horizontal=old;i->viewportDirty=true;i->poseReady=false;if(i->alive){i->input->setPlacement(oldPlacement);i->placement=oldPlacement;}throw;}return true;
}
double NativeProjectedEditor::horizontalScrollOffset()const noexcept{return impl_->horizontal;}
double NativeProjectedEditor::maximumHorizontalScrollOffset()const noexcept{return impl_->maximumHorizontal();}
bool NativeProjectedEditor::revealCaret(){auto i=impl_;i->onThread();i->revealPending=true;const bool changed=i->sync();if(changed&&i->poseSet&&i->alive)setPose(i->pose);return changed;}

ProjectedEditorResult NativeProjectedEditor::applyFormat(const core::notes::FormatChange&change){
    auto i=impl_;i->current();if(!i->rich)return {};bool changed{};
    const auto hr=i->input->performHostEdit([&]()->std::optional<text::Change>{
        const auto oldLayout=i->richLayout;const auto revision=i->rich->revision();
        // Allocate the complete paragraph update before mutating the rich model.
        // A format equal to its current attributes can still alter paragraph
        // spacing, matching the source imported-rich selection path.
        auto paragraphs=oldLayout?i->spacedSelection():std::vector<text::Range>{};
        const bool spacingChanged=oldLayout&&paragraphs!=i->spacedParagraphs;
        changed=i->rich->applyFormat(change);
        if(!oldLayout){i->richLayout=true;i->normalizedParagraphs=true;changed=true;}
        else if(spacingChanged){i->spacedParagraphs.swap(paragraphs);changed=true;}
        if(!oldLayout||spacingChanged||revision!=i->rich->revision()){i->styleDirty=true;i->revealPending=true;}return {};});
    if(hr==TS_E_NOLOCK||hr==TS_E_READONLY)return {true,false,false};checked(hr,"Format source rich document");return {true,changed,false};
}
ProjectedEditorResult NativeProjectedEditor::undo(){
    auto i=impl_;i->current();if(!i->history)return {};bool changed{};
    const auto hr=i->input->performHostEdit([&]()->std::optional<text::Change>{const auto before=std::u16string(i->document.text());changed=i->history->undo();if(!changed)return {};i->revealPending=true;
        if(before==i->document.text())return {};return text::Change{0,static_cast<std::uint32_t>(before.size()),static_cast<std::uint32_t>(i->document.text().size())};});
    if(hr==TS_E_NOLOCK||hr==TS_E_READONLY)return {true,false,false};checked(hr,"Undo source rich document");return {true,changed,false};
}
ProjectedEditorResult NativeProjectedEditor::redo(){
    auto i=impl_;i->current();if(!i->history)return {};bool changed{};
    const auto hr=i->input->performHostEdit([&]()->std::optional<text::Change>{const auto before=std::u16string(i->document.text());changed=i->history->redo();if(!changed)return {};i->revealPending=true;
        if(before==i->document.text())return {};return text::Change{0,static_cast<std::uint32_t>(before.size()),static_cast<std::uint32_t>(i->document.text().size())};});
    if(hr==TS_E_NOLOCK||hr==TS_E_READONLY)return {true,false,false};checked(hr,"Redo source rich document");return {true,changed,false};
}
std::optional<core::notes::TextStyle>NativeProjectedEditor::selectionStyle()const{auto i=impl_;i->onThread();return i->rich?std::optional(i->rich->selectionStyle()):std::nullopt;}
bool NativeProjectedEditor::richLayoutEnabled()const noexcept{return impl_->richLayout;}
ProjectedEditorResult NativeProjectedEditor::command(ProjectedEditorCommand command,bool extend){
    auto i=impl_;i->onThread();i->highUnit.reset();
    if(command==ProjectedEditorCommand::finish){if(i->document.composition()){const auto hr=i->input->cancelComposition();if(hr==TS_E_NOLOCK)return {true,false,false};checked(hr,"Cancel unconsumed IME composition");return {true,true,false};}return {true,false,true};}
    if(i->document.composition())return {};i->current();const auto selection=i->document.selection();auto range=selection.range;const auto at=selection.activeEnd==text::ActiveEnd::start?range.start:range.end;
    switch(command){
    case ProjectedEditorCommand::selectAll:{const auto n=std::uint32_t(i->document.text().size());const auto hr=i->input->selectFromHost({{0,n},text::ActiveEnd::end,false});if(hr==TS_E_NOLOCK)return {true,false,false};checked(hr,"Select entire bounded editor");i->revealPending=true;return {true,true,false};}
    case ProjectedEditorCommand::left:case ProjectedEditorCommand::right:{const bool left=command==ProjectedEditorCommand::left;const auto target=!extend&&range.start!=range.end?(left?range.start:range.end):(left?i->layout->previousCluster(at):i->layout->nextCluster(at));return i->select(target,extend);}
    case ProjectedEditorCommand::documentStart:return i->select(0,extend);
    case ProjectedEditorCommand::documentEnd:return i->select(std::uint32_t(i->document.text().size()),extend);
    case ProjectedEditorCommand::backspace:case ProjectedEditorCommand::deleteForward:
        if(range.start==range.end){if(command==ProjectedEditorCommand::backspace)range.start=i->layout->previousCluster(at);else range.end=i->layout->nextCluster(at);}return range.start==range.end?ProjectedEditorResult{true,false,false}:i->replace(range,{});
    case ProjectedEditorCommand::up:case ProjectedEditorCommand::down:{const auto box=i->layout->bounds({at,at});if(!box||box->bounds.height<=0)return {true,false,false};const auto& r=box->bounds;const auto target=i->layout->hit({r.x,r.y+(command==ProjectedEditorCommand::up?-.5:1.5)*r.height},true,true);return target?i->select(*target,extend):ProjectedEditorResult{true,false,false};}
    case ProjectedEditorCommand::finish:break;
    }return {};
}
ProjectedEditorResult NativeProjectedEditor::character(std::uint32_t value,bool scalar){auto i=impl_;i->onThread();if(i->document.composition()){i->highUnit.reset();return {true,false,false};}if(value<32&&value!='\r'&&value!='\n'){i->highUnit.reset();return {true,false,false};}std::array<char16_t,2>units{};std::size_t size{};
    if(scalar){i->highUnit.reset();if(value>0x10ffff||(value>=0xd800&&value<=0xdfff))return {true,false,false};if(value>0xffff){const auto v=value-0x10000;units={char16_t(0xd800+(v>>10)),char16_t(0xdc00+(v&1023))};size=2;}else{units[0]=value=='\r'?u'\n':char16_t(value);size=1;}}
    else {if(value>0xffff)return {true,false,false};const auto unit=char16_t(value);if(high(unit)){i->highUnit=unit;return {true,false,false};}if(low(unit)){if(!i->highUnit)return {true,false,false};units={*i->highUnit,unit};size=2;i->highUnit.reset();}else{i->highUnit.reset();units[0]=unit=='\r'?u'\n':unit;size=1;}}
    return i->replace(i->document.selection().range,{units.data(),size});}
ProjectedEditorResult NativeProjectedEditor::replaceTextFromHost(text::Range range,std::u16string_view value){auto i=impl_;i->onThread();i->highUnit.reset();return i->replace(range,value);}
ProjectedEditorResult NativeProjectedEditor::setSelectionFromHost(text::Selection selection){auto i=impl_;i->onThread();i->highUnit.reset();const auto previous=i->document.selection();const auto hr=i->input->selectFromHost(selection);if(hr==E_INVALIDARG||hr==TS_E_NOLOCK)return {true,false,false};checked(hr,"Restore projected editor selection");if(i->alive)i->revealPending=true;return {true,selection!=previous,false};}
ProjectedEditorResult NativeProjectedEditor::pointerDown(core::Point point,bool extend){auto i=impl_;i->current();need(i->poseReady,"Set editor pose before pointer input");if(i->document.composition())return {};const auto at=text::projectedHit(i->document,*i->layout,point,i->placement,false,true);if(!at)return {};i->dragging=true;return i->select(*at,extend,false);}
ProjectedEditorResult NativeProjectedEditor::pointerDrag(core::Point point){auto i=impl_;i->current();if(!i->dragging||i->document.composition()||!i->poseReady||!i->placement.visible)return {};
    // Source text view passes the actual unprojected point outside its viewport
    // to document hit-testing, then reveals the resulting active selection.
    const auto local=i->placement.projection.unproject(point);if(!local)return {};
    const auto at=i->layout->hit({local->x-i->placement.viewport.x+i->horizontal,local->y-i->placement.viewport.y+i->scroll},true,true);return at?i->select(*at,true):ProjectedEditorResult{};}

void NativeProjectedEditor::pointerUp()noexcept{if(GetCurrentThreadId()==impl_->thread)impl_->dragging=false;}
const LayerTextLayout&NativeProjectedEditor::layout()const{return *impl_->layout;}
const text::Placement&NativeProjectedEditor::placement()const noexcept{return impl_->placement;}
LayerScene&NativeProjectedEditor::scene()const noexcept{return *impl_->scene;}
std::uint64_t NativeProjectedEditor::paintRevision()const noexcept{return impl_->paintRevision;}
} // namespace endfield::native
#endif
