#include "native/profile_editor_field.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <windows.h>
namespace endfield::native {namespace {
namespace m=modules;using J=ehud::data::Json;using M=core::Matrix4;using R=core::Rect;
void need(bool b,const char*message){if(!b)throw std::invalid_argument(message);}
J color(std::array<double,4>c){return J::Object{{"sRGB",J::Array{c[0],c[1],c[2],c[3]}}};}
J leaf(std::string id,R bounds){return J::Object{{"id",std::move(id)},{"bounds",J::Array{0,0,bounds.width,bounds.height}},{"position",J::Array{bounds.x,bounds.y}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
std::u16string utf16(std::string_view s){
    if(s.empty())return {};need(s.size()<=0x7fffffff,"Profile editor text exceeds native bounds");
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);need(n>0,"Profile editor text is not UTF8");
    std::u16string out(std::size_t(n),u'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),reinterpret_cast<wchar_t*>(out.data()),n);return out;
}
std::string utf8(std::u16string_view s){
    if(s.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(s.data()),int(s.size()),nullptr,0,nullptr,nullptr);
    need(n>0,"Profile editor text is not valid UTF16");std::string out(std::size_t(n),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,reinterpret_cast<const wchar_t*>(s.data()),int(s.size()),out.data(),n,nullptr,nullptr);return out;
}
M localPlane(M value){
    // Glyph leaves are translations in the field plane; canonicalize only the
    // floating-point residue of field world * inverse(field world).
    for(const auto n:{1u,2u,3u,4u,6u,7u,8u,9u,11u,14u}){need(std::abs(value.values[n])<=1e-8,"Profile field glyph leaves left their local plane");value.values[n]=0;}
    for(const auto n:{0u,5u,10u,15u}){need(std::abs(value.values[n]-1)<=1e-8,"Profile field glyph plane scale is invalid");value.values[n]=1;}
    return value;
}
ProjectedEditorStyle style(m::ProfileField field,R rect,const NativeProfileFieldAppearance&a){
    ProjectedEditorStyle s;s.width=rect.width;s.height=rect.height;
    s.fontSize=field==m::ProfileField::introduction?11:std::max(10.,std::min(20.,rect.height*.55));
    s.fontFamily=".AppleSystemUIFont";s.fontFace=".AppleSystemUIFontDemi";s.cornerRadius=2;
    const double ink=a.dark?1:0;s.textColor=s.caretColor=s.compositionColor={ink,ink,ink,1};
    s.wrapped=field==m::ProfileField::introduction;s.sourceSingleLineField=!s.wrapped;
    s.alignment=m::profileNumericField(field)?ProjectedEditorAlignment::right:ProjectedEditorAlignment::left;
    return s;
}
}
struct NativeProfileEditorField::Impl {
    m::ProfileField field;R rect;LayerRasterizer&raster;LayerRasterOptions options;NativeProfileFieldAppearance appearance;
    core::notes::RichDocument document;LayerScene box,glyphs;NativeLayerGroup group;std::unique_ptr<NativeProjectedEditor>editor;UINT_PTR generation;
    std::array<DrawObject,4>localGlyphs;std::array<LayerPlacement,4>boxPlacements;std::array<PlaneMask,1>localMask;std::array<PlaneMask,7>ownerMasks;NativeProfileFieldPose pose;
    bool posed{},uploaded{},localDirty{true},inputStopped{};core::text::Selection localizedSelection;std::optional<core::text::Range>localizedComposition;std::uint64_t localizedDocument{};double localizedScroll{},localizedHorizontal{};
    std::uint64_t boxRevision{},glyphRevision{},fontRevision{};
    Impl(HWND hwnd,LayerRasterizer&r,LayerRasterOptions o,m::ProfileField f,R area,std::string text,NativeProfileFieldAppearance a,UINT message,UINT_PTR g)
        :field(f),rect(area),raster(r),options(std::move(o)),appearance(a),document(utf16(text),{},65536),box(r),glyphs(r),
         group(box,"profile.field."+std::to_string(static_cast<unsigned>(f)),options.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB),generation(g){
        need(!m::profileGeometryField(f)&&rect.width>0&&rect.height>0,"Profile editor needs a text field rect");
        editor=std::make_unique<NativeProjectedEditor>(hwnd,document,glyphs,style(f,rect,a),options,PlainEditorFixtureCapacity{65536},message,g,ProjectedEditorTextMode::plainHistory);
        editor->syncContent();rebuildBox();captureGlyphBindings();
    }
    J artwork(){
        // Virtual parent keeps four resident paint slots; the field mask is
        // applied to every local backing/glyph draw.
        auto root=leaf("profile/editor/box",{0,0,0,0});root["allowsGroupOpacity"]=false;
        auto background=leaf("profile/editor/background",{0,0,rect.width,rect.height});const auto gray=appearance.dark?.13:.93;
        background["backgroundColor"]=color({gray,gray,gray,1});background["cornerRadius"]=2;
        auto thumb=leaf("profile/editor/scrollIndicator",{0,0,3,16});thumb["backgroundColor"]=color({.5,.5,.5,.65});thumb["cornerRadius"]=1.5;
        auto spacer=leaf("profile/editor/placeholder",{0,0,1,1});spacer["backgroundColor"]=color({0,0,0,0});
        auto border=leaf("profile/editor/border",{0,0,rect.width,rect.height});border["cornerRadius"]=2;border["borderWidth"]=1;
        border["borderColor"]=color(appearance.failed?std::array<double,4>{1,appearance.dark?69./255:59./255,appearance.dark?58./255:48./255,1}:std::array<double,4>{appearance.accent[0],appearance.accent[1],appearance.accent[2],1});
        root["children"]=J::Array{std::move(background),std::move(thumb),std::move(spacer),std::move(border)};return root;
    }
    void rebuildBox(){box.load(artwork(),options);need(box.report().unsupported.empty()&&box.draws().size()==4,"Unsupported profile field backing");fontRevision=raster.fontRevision();}
    void captureGlyphBindings(){const auto draws=glyphs.draws();need(draws.size()==4,"Profile field requires the existing four editor leaves");for(std::size_t n=0;n<4;++n){localGlyphs[n]=draws[n];localGlyphs[n].masks.reserve(1);}localMask[0]={M{},{0,0,rect.width,rect.height},2};localDirty=true;}
    void localize(){
        if(!posed||inputStopped)return;const auto draws=glyphs.prepareDraws();
        const bool changed=localDirty||localizedDocument!=document.revision()||localizedSelection!=document.selection()||localizedComposition!=document.composition()||localizedScroll!=editor->scrollOffset()||localizedHorizontal!=editor->horizontalScrollOffset();
        std::optional<M>inverse;if(changed)inverse=core::source::inverseSourceMatrix(pose.world);
        for(std::size_t n=0;n<4;++n){auto&d=localGlyphs[n];if(changed){d.world=localPlane(*inverse*draws[n].world);d.masks.assign(localMask.begin(),localMask.end());d.shutter.reset();}d.opacity=draws[n].opacity;}
        localDirty=false;localizedDocument=document.revision();localizedSelection=document.selection();localizedComposition=document.composition();localizedScroll=editor->scrollOffset();localizedHorizontal=editor->horizontalScrollOffset();
        const auto documentHeight=editor->layout().painted()->documentHeight();const auto overflow=std::max(0.,documentHeight-rect.height);
        const auto thumb=std::clamp(rect.height*rect.height/std::max(1.,documentHeight),16.,std::max(16.,rect.height));const auto y=overflow>0?(rect.height-thumb)*editor->scrollOffset()/overflow:0;
        const auto originals=box.draws();for(std::size_t n=0;n<4;++n)boxPlacements[n]={n,originals[n].world,1,localMask};
        boxPlacements[0].world=M{};boxPlacements[1].world=M::translation(rect.width-4,y)*M::scale(1,thumb/16);boxPlacements[1].opacity=overflow>0?1:0;
        boxPlacements[2].opacity=0;boxPlacements[3].world=M{};box.setPlacements(boxPlacements);
    }
    NativeGroupInsertion insertion(){return {1,localGlyphs,{0,0,rect.width,rect.height}};}
};
NativeProfileEditorField::NativeProfileEditorField(HWND h,LayerRasterizer&r,LayerRasterOptions o,m::ProfileField f,R rect,std::string text,NativeProfileFieldAppearance a,UINT message,UINT_PTR g)
    :impl_(std::make_unique<Impl>(h,r,std::move(o),f,rect,std::move(text),a,message,g)){}
NativeProfileEditorField::~NativeProfileEditorField()=default;
bool NativeProfileEditorField::normalize(const m::ProfileTextRules&rules){
    auto&i=*impl_;if(i.document.composition())return false;
    const auto old=utf8(i.document.text());const auto value=m::profileEditorText(i.field,old,rules);if(value==old)return false;
    const auto replacement=utf16(value);
    // Source: keep the caret at min(location, new UTF16 length).
    const auto selection=i.document.selection();
    const auto caret=std::min<std::uint32_t>(selection.range.start,static_cast<std::uint32_t>(replacement.size()));
    const bool changed=i.editor->replaceTextFromHost({0,static_cast<std::uint32_t>(i.document.text().size())},replacement).changed;
    if(changed)i.editor->setSelectionFromHost(core::text::Selection{{caret,caret},core::text::ActiveEnd::end,false});
    return changed;
}
bool NativeProfileEditorField::syncContent(){
    auto&i=*impl_;if(i.inputStopped)return false;const bool changed=i.editor->syncContent();
    if(i.fontRevision!=i.raster.fontRevision())i.rebuildBox();
    if(i.posed)i.editor->setPose({i.pose.world,i.pose.camera,i.pose.pixelWidth,i.pose.pixelHeight,1,i.pose.visible,i.pose.focused,true});
    if(changed)i.captureGlyphBindings();i.localize();return changed;
}
void NativeProfileEditorField::setAppearance(NativeProfileFieldAppearance a){
    auto&i=*impl_;if(i.inputStopped||(i.appearance==a&&i.fontRevision==i.raster.fontRevision()))return;
    const bool text=i.appearance.dark!=a.dark;i.appearance=a;
    if(text){i.editor->setStyle(style(i.field,i.rect,a));if(i.editor->syncContent()&&i.posed)i.editor->setPose({i.pose.world,i.pose.camera,i.pose.pixelWidth,i.pose.pixelHeight,1,i.pose.visible,i.pose.focused,true});}
    i.rebuildBox();i.captureGlyphBindings();i.localize();
}
void NativeProfileEditorField::updatePose(const NativeProfileFieldPose&p){
    auto&i=*impl_;need(p.world.finite()&&p.camera.finite()&&p.pixelWidth&&p.pixelHeight&&p.ownerMasks.size()<=7&&std::isfinite(p.opacity)&&p.opacity>=0&&p.opacity<=1,"Invalid profile field pose");
    i.pose=p;std::copy(p.ownerMasks.begin(),p.ownerMasks.end(),i.ownerMasks.begin());i.pose.ownerMasks={i.ownerMasks.data(),p.ownerMasks.size()};i.posed=true;
    if(!i.inputStopped){i.editor->setPose({p.world,p.camera,p.pixelWidth,p.pixelHeight,1,p.visible,p.focused,true});i.localize();}
    if(i.uploaded)i.group.setPose(p.world,p.opacity,p.ownerMasks,p.shutter);
}
bool NativeProfileEditorField::upload(Renderer&r){
    auto&i=*impl_;const auto revision=i.glyphs.resourceRevision();if(revision!=i.glyphRevision){i.glyphs.uploadResources(r);i.glyphRevision=revision;}
    const bool installed=i.boxRevision!=i.box.resourceRevision();bool changed{};
    if(installed){changed=i.group.uploadResources(r,{},i.insertion());i.boxRevision=i.box.resourceRevision();i.uploaded=true;}else changed=i.group.updateLocal(r,i.insertion());
    if(i.posed)i.group.setPose(i.pose.world,i.pose.opacity,i.pose.ownerMasks,i.pose.shutter);i.glyphs.collectRetiredResources(r);return changed;
}
LayerCompositionEntry NativeProfileEditorField::entry(){return impl_->group.entry();}
bool NativeProfileEditorField::releaseResources(Renderer&r){auto&i=*impl_;if(!i.group.releaseResources(r))return false;return i.glyphs.releaseResources(r);}
NativeProjectedEditor&NativeProfileEditorField::editor()noexcept{return *impl_->editor;}
core::notes::RichDocument&NativeProfileEditorField::document()noexcept{return impl_->document;}
std::string NativeProfileEditorField::text()const{return utf8(impl_->document.text());}
m::ProfileField NativeProfileEditorField::field()const noexcept{return impl_->field;}
R NativeProfileEditorField::rect()const noexcept{return impl_->rect;}
UINT_PTR NativeProfileEditorField::generation()const noexcept{return impl_->generation;}
bool NativeProfileEditorField::stopInput(){auto&i=*impl_;if(i.inputStopped)return true;const auto hr=i.editor->stop();if(hr==TS_E_NOLOCK)return false;need(SUCCEEDED(hr),"Cannot stop profile field input");i.inputStopped=true;return true;}
}
#endif
