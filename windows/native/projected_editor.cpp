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
    DWORD thread{GetCurrentThreadId()};bool alive{true},stopped{},dragging{},poseReady{},styleDirty{true};
    BoundedDocument document;LayerScene* scene;ProjectedEditorStyle style;LayerRasterOptions options;
    std::unique_ptr<LayerTextLayout> layout;std::unique_ptr<ProjectedTextInput> input;
    std::uint64_t glyphRevision{},shapeRevision{},paintRevision{},structureRevision{},paintedDocument{},adornedDocument{};
    text::Selection adornedSelection;std::optional<text::Range>adornedComposition;std::optional<char16_t>highUnit;
    std::array<std::size_t,4> surfaces{};std::array<DrawObject,4> originals;std::array<LayerPlacement,4> placements;
    std::array<std::array<PlaneMask,8>,4> masks;std::array<std::size_t,4> maskCounts{};
    text::Placement placement;ProjectedEditorPose pose;
    static constexpr std::array<const char*,4> ids{"projected-editor-selection","projected-editor-glyphs","projected-editor-composition","projected-editor-caret"};
    Impl(text::Document& d,LayerScene& s,ProjectedEditorStyle styleValue,LayerRasterOptions opts,PlainEditorFixtureCapacity capacity)
        :document(d,capacity.maximumUnits),scene(&s),style(std::move(styleValue)),options(std::move(opts)){
        options.paddingPoints=0;options.retainEmptyTextLayout=true;validate(style,options);need(s.contentRevision()==0&&s.draws().empty(),"Projected editor needs its dedicated initially empty borrowed scene");}
    void onThread()const{if(GetCurrentThreadId()!=thread)throw std::logic_error("Projected editor used outside its creating thread");need(alive&&!stopped,"Projected editor is stopped");}
    void current()const{onThread();need(layout&&layout->textRevision()==document.revision(),"Synchronize editor text before geometry/navigation");}
    Json base(unsigned slot)const{return Json::Object{{"id",ids[slot]},{"class","CAShapeLayer"},{"kind","shape"},{"bounds",Json::Array{0,0,style.width,style.height}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"opacity",1},{"children",Json::Array{}}};}
    Json glyph()const{auto out=base(1);out["class"]="CATextLayer";out["kind"]="text";out["text"]=Json::Object{{"string",utf8(document.text())},{"fontSize",style.fontSize},{"font",Json::Object{{"familyName",style.fontFamily},{"postScriptName",style.fontFace},{"pointSize",style.fontSize}}},{"foregroundColor",rgba(style.textColor)},{"alignment","natural"},{"wrapped",true},{"truncation","none"},{"runs",Json::Array{}}};if(style.lineHeight>0){out["text"]["font"]["ascender"]=style.baseline;out["text"]["font"]["descender"]=style.baseline-style.lineHeight;out["text"]["font"]["leading"]=0;}return out;}
    Json adornment(unsigned slot,text::Range range)const{auto out=base(slot);Json::Array commands;
        if(layout)for(auto r:layout->selectionRectangles(range)){if(slot==2){r.y+=std::max(0.,r.height-1);r.height=1;}rectanglePath(commands,r);}
        const auto& c=slot==0?style.selectionColor:slot==2?style.compositionColor:style.caretColor;
        out["shape"]=Json::Object{{"path",std::move(commands)},{"fillColor",rgba(c)},{"strokeColor",nullptr},{"fillRule","non-zero"}};return out;}
    void build(){
        need(document.text().size()<=document.maximumUnits(),"Caller text exceeds explicitly bounded editor capacity");
        if(!layout){
            Json::Array children;children.push_back(adornment(0,{}));children.push_back(glyph());children.push_back(adornment(2,{}));children.push_back(adornment(3,{}));
            scene->load(Json::Object{{"bounds",Json::Array{0,0,style.width,style.height}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"masksToBounds",true},{"children",std::move(children)}},options);
            need(scene->report().unsupported.empty(),"Projected editor scene contains unsupported local effects");structureRevision=scene->contentRevision();
            for(unsigned i=0;i<4;++i){const auto surface=scene->surfaceIndex(ids[i]);need(surface.has_value(),"Projected editor surface is missing");surfaces[i]=*surface;originals[i]=scene->draws()[*surface];need(originals[i].masks.size()==1&&originals[i].masks[0].bounds==core::Rect{0,0,style.width,style.height}&&originals[i].masks[0].worldToLocal==core::Matrix4{},"Editor leaves must share their one root viewport mask");maskCounts[i]=originals[i].masks.size();}
            layout=std::make_unique<LayerTextLayout>(scene->paintedTextLayout(ids[1]),document);
        }else{
            need(scene->contentRevision()==structureRevision,"Borrowed editor scene was replaced externally");
            scene->updateLocalContent(ids[1],++glyphRevision,glyph(),options);layout->bind(scene->paintedTextLayout(ids[1]),document);
        }
        paintedDocument=document.revision();styleDirty=false;adornedDocument=0;++paintRevision;
    }
    bool sync(){
        onThread();bool changed{};if(!layout||paintedDocument!=document.revision()||styleDirty){build();changed=true;}
        const auto selection=document.selection();const auto composition=document.composition();
        if(adornedDocument!=document.revision()||selection!=adornedSelection||composition!=adornedComposition){
            ++shapeRevision;scene->updateLocalContent(ids[0],shapeRevision,adornment(0,selection.range),options);
            scene->updateLocalContent(ids[2],shapeRevision,adornment(2,composition.value_or(text::Range{})),options);
            const auto at=selection.activeEnd==text::ActiveEnd::start?selection.range.start:selection.range.end;scene->updateLocalContent(ids[3],shapeRevision,adornment(3,{at,at}),options);
            adornedDocument=document.revision();adornedSelection=selection;adornedComposition=composition;++paintRevision;changed=true;
        }
        if(changed){poseReady=false;if(input)checked(input->layoutChanged(),"Notify current painted editor layout");}return changed;
    }
    ProjectedEditorResult select(std::uint32_t target,bool extend){
        if(document.composition())return {};const auto old=document.selection();const auto anchor=extend?(old.activeEnd==text::ActiveEnd::start?old.range.end:old.range.start):target;
        const text::Selection next{{std::min(anchor,target),std::max(anchor,target)},target<anchor?text::ActiveEnd::start:text::ActiveEnd::end,false};
        const auto hr=input->selectFromHost(next);if(hr==TS_E_NOLOCK)return {true,false,false};checked(hr,"Select projected editor text");return {true,next!=old,false};
    }
    ProjectedEditorResult replace(text::Range range,std::u16string_view value){const auto hr=input->replaceFromHost(range,value);if(hr==E_INVALIDARG||hr==TS_E_NOLOCK||hr==TS_E_READONLY)return {true,false,false};checked(hr,"Edit projected document");return {true,true,false};}
};
NativeProjectedEditor::NativeProjectedEditor(HWND hwnd,text::Document& doc,LayerScene& scene,ProjectedEditorStyle style,LayerRasterOptions options,PlainEditorFixtureCapacity capacity,UINT message,UINT_PTR generation)
    :impl_(std::make_shared<Impl>(doc,scene,std::move(style),std::move(options),capacity)){
    auto& i=*impl_;i.sync();i.input=std::make_unique<ProjectedTextInput>(hwnd,i.document,*i.layout,message,generation);
}
NativeProjectedEditor::~NativeProjectedEditor(){auto i=impl_;if(!i)return;if(GetCurrentThreadId()!=i->thread)std::terminate();i->alive=false;i->input.reset();}
HRESULT NativeProjectedEditor::connect(ITfThreadMgr& manager,TfClientId client)noexcept{auto i=impl_;if(GetCurrentThreadId()!=i->thread)return RPC_E_WRONG_THREAD;return i->input->connect(manager,client);}
HRESULT NativeProjectedEditor::focus(bool focused)noexcept{auto i=impl_;if(GetCurrentThreadId()!=i->thread)return RPC_E_WRONG_THREAD;i->highUnit.reset();i->dragging=false;return focused?i->input->focus():i->input->blur(true);}
HRESULT NativeProjectedEditor::stop()noexcept{auto i=impl_;if(GetCurrentThreadId()!=i->thread)return RPC_E_WRONG_THREAD;i->highUnit.reset();i->dragging=false;const auto hr=i->input->stop();if(i->alive&&SUCCEEDED(hr))i->stopped=true;return hr;}
bool NativeProjectedEditor::filterKeyMessage(UINT message,WPARAM key,LPARAM data)noexcept{auto i=impl_;return i->input->filterKeyMessage(message,key,data);}
unsigned NativeProjectedEditor::takeChanges(UINT_PTR generation)noexcept{auto i=impl_;return i->input->takeChanges(generation);}
ITextStoreACP* NativeProjectedEditor::textStore()const noexcept{return impl_->input->textStore();}
bool NativeProjectedEditor::syncContent(){auto i=impl_;return i->sync();}
bool NativeProjectedEditor::setStyle(const ProjectedEditorStyle& style){auto i=impl_;i->onThread();validate(style,i->options);need(style.width==i->style.width&&style.height==i->style.height,"Short editor viewport resize requires owner replacement; long-document relayout is separate");if(style==i->style)return false;auto paintedStyle=style;paintedStyle.cornerRadius=i->style.cornerRadius;i->styleDirty|=paintedStyle!=i->style;i->style=style;i->poseReady=false;return true;}
bool NativeProjectedEditor::setPose(const ProjectedEditorPose& pose){
    auto i=impl_;i->current();need(i->scene->contentRevision()==i->structureRevision,"Editor scene structure was replaced");need(pose.localToScreen.finite()&&pose.screenToClip.finite()&&pose.pixelWidth>0&&pose.pixelHeight>0&&std::isfinite(pose.opacity)&&pose.opacity>=0&&pose.opacity<=1,"Invalid projected editor pose");
    const auto inverse=core::source::inverseSourceMatrix(pose.localToScreen);
    const text::Placement placement{core::Projection::viewport(pose.screenToClip*pose.localToScreen,pose.pixelWidth,pose.pixelHeight),{0,0,i->style.width,i->style.height},{0,0},pose.visible,i->style.cornerRadius};
    if(i->poseReady&&i->pose.localToScreen==pose.localToScreen&&i->pose.screenToClip==pose.screenToClip&&i->pose.pixelWidth==pose.pixelWidth&&i->pose.pixelHeight==pose.pixelHeight&&i->pose.opacity==pose.opacity&&i->pose.visible==pose.visible&&i->pose.ownerFocused==pose.ownerFocused&&i->pose.caretVisible==pose.caretVisible)return false;
    const auto placementResult=i->input->setPlacement(placement);if(!i->alive)return false;checked(placementResult,"Update projected editor candidate plane");
    const auto selection=i->document.selection();const bool selectionVisible=selection.range.start!=selection.range.end;
    for(unsigned k=0;k<4;++k){auto& out=i->placements[k];out.surface=i->surfaces[k];out.world=pose.localToScreen*i->originals[k].world;out.opacity=pose.visible?pose.opacity:0;
        if((k==0&&!selectionVisible)||(k==2&&!i->document.composition())||(k==3&&(!pose.ownerFocused||!pose.caretVisible||selectionVisible)))out.opacity=0;
        for(std::size_t m=0;m<i->maskCounts[k];++m){i->masks[k][m]=i->originals[k].masks[m];i->masks[k][m].cornerRadius=i->style.cornerRadius;i->masks[k][m].worldToLocal=i->masks[k][m].worldToLocal*inverse;}out.masks={i->masks[k].data(),i->maskCounts[k]};}
    i->scene->setPlacements(i->placements);i->placement=placement;i->pose=pose;i->poseReady=true;return true;
}
ProjectedEditorResult NativeProjectedEditor::command(ProjectedEditorCommand command,bool extend){
    auto i=impl_;i->onThread();i->highUnit.reset();
    if(command==ProjectedEditorCommand::finish){if(i->document.composition()){const auto hr=i->input->cancelComposition();if(hr==TS_E_NOLOCK)return {true,false,false};checked(hr,"Cancel unconsumed IME composition");return {true,true,false};}return {true,false,true};}
    if(i->document.composition())return {};i->current();const auto selection=i->document.selection();auto range=selection.range;const auto at=selection.activeEnd==text::ActiveEnd::start?range.start:range.end;
    switch(command){
    case ProjectedEditorCommand::selectAll:{const auto n=std::uint32_t(i->document.text().size());const auto hr=i->input->selectFromHost({{0,n},text::ActiveEnd::end,false});if(hr==TS_E_NOLOCK)return {true,false,false};checked(hr,"Select entire bounded editor");return {true,true,false};}
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
ProjectedEditorResult NativeProjectedEditor::pointerDown(core::Point point,bool extend){auto i=impl_;i->current();need(i->poseReady,"Set editor pose before pointer input");if(i->document.composition())return {};const auto at=text::projectedHit(i->document,*i->layout,point,i->placement,false,true);if(!at)return {};i->dragging=true;return i->select(*at,extend);}
ProjectedEditorResult NativeProjectedEditor::pointerDrag(core::Point point){auto i=impl_;i->current();if(!i->dragging||i->document.composition())return {};const auto at=text::projectedHit(i->document,*i->layout,point,i->placement,true,true);return at?i->select(*at,true):ProjectedEditorResult{};}
void NativeProjectedEditor::pointerUp()noexcept{if(GetCurrentThreadId()==impl_->thread)impl_->dragging=false;}
const LayerTextLayout&NativeProjectedEditor::layout()const{return *impl_->layout;}
const text::Placement&NativeProjectedEditor::placement()const noexcept{return impl_->placement;}
LayerScene&NativeProjectedEditor::scene()const noexcept{return *impl_->scene;}
std::uint64_t NativeProjectedEditor::paintRevision()const noexcept{return impl_->paintRevision;}
} // namespace endfield::native
#endif
