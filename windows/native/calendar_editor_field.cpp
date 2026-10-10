#include "native/calendar_editor_field.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace endfield::native {namespace {
namespace m=modules;using J=ehud::data::Json;using M=core::Matrix4;using R=core::Rect;
void need(bool b,const char*message){if(!b)throw std::invalid_argument(message);}
J color(m::CalendarColor c){return J::Object{{"sRGB",J::Array{c[0],c[1],c[2],c[3]}}};}
J leaf(std::string id,R bounds){return J::Object{{"id",std::move(id)},{"bounds",J::Array{0,0,bounds.width,bounds.height}},{"position",J::Array{bounds.x,bounds.y}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
M localPlane(M value){
    // The editor's source leaves are translations in their field plane. Their
    // current placement is field world * leaf translation; multiplying by the
    // inverse field world can leave roundoff in these otherwise exact entries.
    // Reject a real out-of-plane transform; canonicalize only its proven
    // floating-point residue, not source geometry.
    for(const auto n:{1u,2u,3u,4u,6u,7u,8u,9u,11u,14u}){if(std::abs(value.values[n])>1e-8){char detail[128]{};std::snprintf(detail,sizeof detail,"Calendar field glyph leaves left their common local plane: entry=%u value=%.17g",n,value.values[n]);throw std::invalid_argument(detail);}value.values[n]=0;}
    for(const auto n:{0u,5u,10u,15u}){need(std::abs(value.values[n]-1)<=1e-8,"Calendar field glyph plane scale is invalid");value.values[n]=1;}
    return value;
}
}
struct NativeCalendarEditorField::Impl {
    m::CalendarEditorField kind;LayerRasterizer&raster;LayerRasterOptions options;m::CalendarAppearance appearance;std::string placeholder;
    core::notes::RichDocument document;LayerScene box,glyphs;NativeLayerGroup group;std::unique_ptr<NativeProjectedEditor>editor;UINT_PTR generation;double height,size;
    std::array<DrawObject,4>localGlyphs;std::array<LayerPlacement,4>boxPlacements;std::array<PlaneMask,1>localMask;std::array<PlaneMask,7>ownerMasks;NativeCalendarFieldPose pose;bool posed{},uploaded{},placeholderEmpty{},localDirty{true},inputStopped{};core::text::Selection localizedSelection;std::optional<core::text::Range>localizedComposition;std::uint64_t localizedDocument{};double localizedScroll{},localizedHorizontal{};std::uint64_t boxRevision{},glyphRevision{},fontRevision{},metadataRevision{1};
    Impl(HWND hwnd,LayerRasterizer&r,LayerRasterOptions o,m::CalendarEditorField k,std::string text,std::string p,m::CalendarAppearance a,UINT message,UINT_PTR g):kind(k),raster(r),options(std::move(o)),appearance(a),placeholder(std::move(p)),document(m::calendarEditorUTF16(text),{},65536),box(r),glyphs(r),group(box,"calendar.field."+std::to_string(static_cast<unsigned>(k)),options.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB),generation(g),height(k==m::CalendarEditorField::title?30:k==m::CalendarEditorField::date?28:86),size(k==m::CalendarEditorField::title?13:11){
        need(k==m::CalendarEditorField::title||k==m::CalendarEditorField::date||k==m::CalendarEditorField::details,"Unknown Calendar editor field");
        ProjectedEditorStyle style;style.width=312;style.height=height;style.fontSize=size;style.cornerRadius=2;style.fontFamily=".AppleSystemUIFont";style.fontFace=".AppleSystemUIFont";style.textColor=style.caretColor=style.compositionColor={a.dark?1.:0.,a.dark?1.:0.,a.dark?1.:0.,1};style.wrapped=k==m::CalendarEditorField::details;style.sourceSingleLineField=!style.wrapped;
        editor=std::make_unique<NativeProjectedEditor>(hwnd,document,glyphs,style,options,PlainEditorFixtureCapacity{65536},message,g,ProjectedEditorTextMode::plainHistory);editor->syncContent();rebuildBox();captureGlyphBindings();
    }
    J artwork(){
        // Virtual parent preserves four resident paint slots. A positive-sized
        // parent would be flattened by LayerScene and leave no glyph insertion
        // slot. The identical radius-two field mask is applied to every local
        // backing/glyph draw below, including the source placeholder/border.
        auto root=leaf("calendar/editor/box",{0,0,0,0});root["allowsGroupOpacity"]=false;auto background=leaf("calendar/editor/background",{0,0,312,height});const auto gray=appearance.dark?.12:.86;background["backgroundColor"]=color({gray,gray,gray,1});background["cornerRadius"]=2;
        auto thumb=leaf("calendar/editor/scrollIndicator",{0,0,3,16});thumb["backgroundColor"]=color({.5,.5,.5,.65});thumb["cornerRadius"]=1.5;
        const auto inset=editor->layout().painted()->contentInset();auto hint=leaf("calendar/editor/placeholder",{inset.x,inset.y,312-2*inset.x,std::max(1.,height-2*inset.y)});hint["kind"]="text";hint["text"]=J::Object{{"string",document.text().empty()?placeholder:std::string{}},{"fontSize",size},{"font",J::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".AppleSystemUIFont"},{"pointSize",size}}},{"foregroundColor",color({.5,.5,.5,1})},{"alignment","left"},{"wrapped",true},{"truncation","end"}};
        auto border=leaf("calendar/editor/border",{0,0,312,height});border["cornerRadius"]=2;border["borderWidth"]=1;auto accent=appearance.accent;accent[3]=.35;border["borderColor"]=color(accent);root["children"]=J::Array{std::move(background),std::move(thumb),std::move(hint),std::move(border)};return root;
    }
    void rebuildBox(){box.load(artwork(),options);need(box.report().unsupported.empty()&&box.draws().size()==4,"Unsupported Calendar field backing/placeholder");placeholderEmpty=document.text().empty();fontRevision=raster.fontRevision();++metadataRevision;}
    void captureGlyphBindings(){const auto draws=glyphs.draws();need(draws.size()==4,"Calendar field requires the existing four editor leaves");for(std::size_t n=0;n<4;++n){localGlyphs[n]=draws[n];localGlyphs[n].masks.reserve(1);}localMask[0]={M{},{0,0,312,height},2};localDirty=true;}
    void localize(){if(!posed||inputStopped)return;
        // NativeProjectedEditor::setPose writes placements into the glyph
        // scene's retained surfaces. LayerScene::draws() exposes only the last
        // *prepared* records, and this borrowed glyph scene is never presented
        // by a composition, so draws() would still hold the unposed load-time
        // leaves (and stale caret/selection opacity). Prepare with identity:
        // fixed-size numeric copy, no raster/upload/allocation on warm frames.
        const auto draws=glyphs.prepareDraws();const bool changed=localDirty||localizedDocument!=document.revision()||localizedSelection!=document.selection()||localizedComposition!=document.composition()||localizedScroll!=editor->scrollOffset()||localizedHorizontal!=editor->horizontalScrollOffset();std::optional<M>inverse;if(changed)inverse=core::source::inverseSourceMatrix(pose.world);for(std::size_t n=0;n<4;++n){auto&d=localGlyphs[n];if(changed){d.world=localPlane(*inverse*draws[n].world);d.masks.assign(localMask.begin(),localMask.end());d.shutter.reset();}d.opacity=draws[n].opacity;}localDirty=false;localizedDocument=document.revision();localizedSelection=document.selection();localizedComposition=document.composition();localizedScroll=editor->scrollOffset();localizedHorizontal=editor->horizontalScrollOffset();
        const auto documentHeight=editor->layout().painted()->documentHeight();const auto overflow=std::max(0.,documentHeight-height);const auto thumb=std::clamp(height*height/std::max(1.,documentHeight),16.,height);const auto y=overflow>0?(height-thumb)*editor->scrollOffset()/overflow:0;
        const auto originals=box.draws();for(std::size_t n=0;n<4;++n){boxPlacements[n]={n,originals[n].world,1,localMask};}boxPlacements[0].world=M{};boxPlacements[1].world=M::translation(308,y)*M::scale(1,thumb/16);boxPlacements[1].opacity=overflow>0?1:0;const auto inset=editor->layout().painted()->contentInset();boxPlacements[2].world=M::translation(inset.x,inset.y);boxPlacements[2].opacity=document.text().empty()?1:0;boxPlacements[3].world=M{};box.setPlacements(boxPlacements);
    }
    NativeGroupInsertion insertion(){return {1,localGlyphs,{0,0,312,height}};}
};
NativeCalendarEditorField::NativeCalendarEditorField(HWND h,LayerRasterizer&r,LayerRasterOptions o,m::CalendarEditorField k,std::string text,std::string p,m::CalendarAppearance a,UINT message,UINT_PTR g):impl_(std::make_unique<Impl>(h,r,std::move(o),k,std::move(text),std::move(p),a,message,g)){}
NativeCalendarEditorField::~NativeCalendarEditorField()=default;
bool NativeCalendarEditorField::normalize(const m::CalendarTextRules&r){auto&i=*impl_;if(i.document.composition())return false;const auto old=m::calendarEditorUTF8(i.document.text());const auto value=m::calendarEditorText(old,i.kind,r,false);if(value==old)return false;const auto replacement=m::calendarEditorUTF16(value);return i.editor->replaceTextFromHost({0,static_cast<std::uint32_t>(i.document.text().size())},replacement).changed;}
bool NativeCalendarEditorField::syncContent(){auto&i=*impl_;if(i.inputStopped)return false;const bool changed=i.editor->syncContent();if(i.fontRevision!=i.raster.fontRevision()||i.placeholderEmpty!=i.document.text().empty())i.rebuildBox();// Host commands may already have synchronized text before this call. Their
 // sync can reset a glyph quad to its local transform even when this second
 // sync reports false; reapply the accepted pose before inverse localization.
 if(i.posed)i.editor->setPose({i.pose.world,i.pose.camera,i.pose.pixelWidth,i.pose.pixelHeight,1,i.pose.visible,i.pose.focused,true});if(changed)i.captureGlyphBindings();i.localize();return changed;}
void NativeCalendarEditorField::setAppearance(m::CalendarAppearance a,std::string placeholder){auto&i=*impl_;if(i.inputStopped)return;if(i.appearance==a&&i.placeholder==placeholder&&i.fontRevision==i.raster.fontRevision())return;i.appearance=a;i.placeholder=std::move(placeholder);auto style=ProjectedEditorStyle{};style.width=312;style.height=i.height;style.fontSize=i.size;style.fontFamily=".AppleSystemUIFont";style.fontFace=".AppleSystemUIFont";style.cornerRadius=2;style.wrapped=i.kind==m::CalendarEditorField::details;style.sourceSingleLineField=!style.wrapped;style.textColor=style.caretColor=style.compositionColor={a.dark?1.:0.,a.dark?1.:0.,a.dark?1.:0.,1};i.editor->setStyle(style);const bool changed=i.editor->syncContent();if(changed&&i.posed)i.editor->setPose({i.pose.world,i.pose.camera,i.pose.pixelWidth,i.pose.pixelHeight,1,i.pose.visible,i.pose.focused,true});i.rebuildBox();i.captureGlyphBindings();i.localize();}
void NativeCalendarEditorField::updatePose(const NativeCalendarFieldPose&p){auto&i=*impl_;need(p.world.finite()&&p.camera.finite()&&p.pixelWidth&&p.pixelHeight&&p.ownerMasks.size()<=7&&std::isfinite(p.opacity)&&p.opacity>=0&&p.opacity<=1,"Invalid Calendar field pose");i.pose=p;std::copy(p.ownerMasks.begin(),p.ownerMasks.end(),i.ownerMasks.begin());i.pose.ownerMasks={i.ownerMasks.data(),p.ownerMasks.size()};i.posed=true;if(!i.inputStopped){i.editor->setPose({p.world,p.camera,p.pixelWidth,p.pixelHeight,1,p.visible,p.focused,true});i.localize();}if(i.uploaded)i.group.setPose(p.world,p.opacity,p.ownerMasks,p.shutter);}
bool NativeCalendarEditorField::upload(Renderer&r){auto&i=*impl_;const auto revision=i.glyphs.resourceRevision();if(revision!=i.glyphRevision){i.glyphs.uploadResources(r);i.glyphRevision=revision;}const bool installed=i.boxRevision!=i.box.resourceRevision();bool changed{};if(installed){changed=i.group.uploadResources(r,{},i.insertion());i.boxRevision=i.box.resourceRevision();i.uploaded=true;}else changed=i.group.updateLocal(r,i.insertion());if(i.posed)i.group.setPose(i.pose.world,i.pose.opacity,i.pose.ownerMasks,i.pose.shutter);i.glyphs.collectRetiredResources(r);return changed;}
LayerCompositionEntry NativeCalendarEditorField::entry(){return impl_->group.entry();}
DrawObject NativeCalendarEditorField::capturedLocalDraw(const M&world)const{need(impl_->uploaded&&impl_->posed,"Upload and pose Calendar field before capturing its visible face");auto d=impl_->group.draws()[0];d.world=world;d.opacity=1;d.masks.clear();d.shutter.reset();return d;}
bool NativeCalendarEditorField::releaseResources(Renderer&r){auto&i=*impl_;const bool group=i.group.releaseResources(r);if(!group)return false;return i.glyphs.releaseResources(r);}
NativeProjectedEditor&NativeCalendarEditorField::editor()noexcept{return *impl_->editor;}core::notes::RichDocument&NativeCalendarEditorField::document()noexcept{return impl_->document;}
m::CalendarEditorField NativeCalendarEditorField::kind()const noexcept{return impl_->kind;}UINT_PTR NativeCalendarEditorField::generation()const noexcept{return impl_->generation;}
bool NativeCalendarEditorField::stopInput(){if(impl_->inputStopped)return true;const auto hr=impl_->editor->stop();if(hr==TS_E_NOLOCK)return false;need(SUCCEEDED(hr),"Cannot stop Calendar field input");impl_->inputStopped=true;return true;}
}
#endif
