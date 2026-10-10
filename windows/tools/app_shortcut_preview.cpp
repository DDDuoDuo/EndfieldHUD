#include "tools/app_shortcut_preview.hpp"
#ifdef _WIN32
#include "native/app_shortcut_field_text.hpp"
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include "modules/archive_model.hpp"
#include "core/source_camera.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>
namespace endfield::tools {namespace {
namespace g=native;namespace m=modules;using J=ehud::data::Json;using M=core::Matrix4;
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}
J blank(){return J::Object{{"bounds",J::Array{0,0,400,334}},{"children",J::Array{}}};}
J rgba(const m::ShortcutColor&c){return J::Object{{"sRGB",J::Array{c[0],c[1],c[2],c[3]}}};}
J fieldNode(bool border,const m::ShortcutAppearance&a){J node=J::Object{{"id",border?"apps.name.border":"apps.name.background"},{"kind","layer"},{"bounds",J::Array{0,0,304,27}},{"cornerRadius",3},{"children",J::Array{}}};if(border){node["borderWidth"]=1;node["borderColor"]=rgba(a.accent);}else {const auto white=a.dark?.17:.91;node["backgroundColor"]=rgba({white,white,white,1});}return node;}
J fieldRoot(bool border,const m::ShortcutAppearance&a){auto root=blank();root["children"]=J::Array{fieldNode(border,a)};return root;}
g::ProjectedEditorStyle fieldStyle(const m::ShortcutAppearance&a){g::ProjectedEditorStyle s;s.width=304;s.height=27;s.fontSize=11;s.fontFamily=".AppleSystemUIFont";s.fontFace=".AppleSystemUIFontDemi";const auto v=a.dark?1.:0.;s.textColor=s.caretColor=s.compositionColor={v,v,v,1};s.cornerRadius=3;s.wrapped=false;s.sourceSingleLineField=true;return s;}
UINT_PTR editorGeneration(){static std::atomic<UINT_PTR>next{1};const auto value=next.fetch_add(1);need(value&&value!=UINTPTR_MAX,"Shortcut editor generation exhausted");return value;}
bool writeRequest(ShortcutRequestKind kind){return kind==ShortcutRequestKind::save||kind==ShortcutRequestKind::remove;}
}
struct AppShortcutPreview::Impl {
    struct Field {
        core::notes::RichDocument document;g::LayerScene background,glyphs,border;
        std::shared_ptr<g::NativeProjectedEditor>editor;UINT_PTR generation;bool published{};
        std::array<g::LayerPlacement,4>placements;std::array<g::PlaneMask,2>masks;
        std::uint64_t backgroundRevision{},borderRevision{},normalizedRevision{};
        Field(HWND w,g::LayerRasterizer&r,const g::LayerRasterOptions&o,const m::ShortcutAppearance&a,std::string_view text,UINT message):document(m::archiveUTF16(text),{},65536),background(r),glyphs(r),border(r),generation(editorGeneration()){
            background.load(fieldRoot(false,a),o);border.load(fieldRoot(true,a),o);need(background.report().unsupported.empty()&&border.report().unsupported.empty(),"Unsupported Shortcut editor artwork");backgroundRevision=background.contentRevision();borderRevision=border.contentRevision();
            document.setSelection({{0,static_cast<std::uint32_t>(document.text().size())},core::text::ActiveEnd::end,false});
            editor=std::make_shared<g::NativeProjectedEditor>(w,document,glyphs,fieldStyle(a),o,g::PlainEditorFixtureCapacity{65536},message,generation,g::ProjectedEditorTextMode::plainHistory);normalizedRevision=document.revision();
        }
        bool normalize(bool force=false){if(document.composition()||(!force&&normalizedRevision==document.revision()))return true;auto retained=editor;if(!retained)return false;auto text=g::normalizeShortcutField(document.text());if(text!=document.text()){const auto selection=document.selection();const auto result=retained->replaceTextFromHost({0,static_cast<std::uint32_t>(document.text().size())},text);if(!result.changed||document.text()!=text)return false;const auto position=std::min(selection.range.start,static_cast<std::uint32_t>(text.size()));const core::text::Selection restored{{position,position},core::text::ActiveEnd::end,false};retained->setSelectionFromHost(restored);if(document.selection()!=restored)return false;}normalizedRevision=document.revision();return true;}
        void appearance(const m::ShortcutAppearance&a,const g::LayerRasterOptions&o){editor->setStyle(fieldStyle(a));background.updateLocalContent("apps.name.background",++backgroundRevision,fieldNode(false,a),o);border.updateLocalContent("apps.name.border",++borderRevision,fieldNode(true,a),o);}
        bool release(g::Renderer&r){return background.releaseResources(r)&&glyphs.releaseResources(r)&&border.releaseResources(r);}
    };
    struct Flight {std::uint64_t generation{};ShortcutRequestKind kind{};std::optional<std::string>editingID;};
    HWND window;g::LayerRasterizer&raster;const g::NativeAppShortcutAssets&assets;ITfThreadMgr*manager;TfClientId client;AppShortcutPreviewOptions options;
    m::AppShortcutState state;g::NativeAppShortcutScene scene;g::LayerScene geometry;g::NativeModuleSurface surface;g::NativeModuleRegistration registration{"apps.registration"};
    app::ClientMetrics metrics;core::Projection projection;std::optional<g::ModuleSurfacePose>pose;
    std::shared_ptr<Field>field;std::vector<std::shared_ptr<Field>>retired;std::vector<g::LayerCompositionEntry>composed;
    std::optional<ShortcutRequest>pending;std::optional<Flight>flight;std::optional<std::uint64_t>cancelled;std::uint64_t nextRequest{};
    bool active{},input{},requested{},overlayVisible{true},focused{},pressed{},fieldDragging{},externalDrag{},reduced{},alive{true},finishing{};
    std::optional<bool>pendingFinish;std::uint64_t lifecycle{},eventRevision{};double time{};std::optional<core::Point>pointerPoint;
    static AppShortcutPreviewOptions prepare(AppShortcutPreviewOptions o,const g::NativeAppShortcutAssets&a){o.raster.assetRoot=a.root();return o;}
    Impl(HWND w,g::LayerRasterizer&r,const g::NativeAppShortcutAssets&a,ITfThreadMgr*m,TfClientId c,AppShortcutPreviewOptions o):window(w),raster(r),assets(a),manager(m),client(c),options(prepare(std::move(o),a)),state(options.initial,options.initialError),scene(state,a,r,options.raster,options.appearance),geometry(r),surface(prepareGeometry(),core::Module::addApp){need(w&&IsWindow(w),"Shortcut owner requires caller HWND");need((manager==nullptr)==(client==TF_CLIENTID_NULL),"Shortcut TSF manager/client must be supplied together");need(options.textMessage>=WM_APP&&options.textMessage<=0xbfff,"Invalid Shortcut private message");retired.reserve(2);composed.reserve(6);}
    g::LayerScene&prepareGeometry(){geometry.load(blank(),options.raster);return geometry;}
    void clock(double t){need(std::isfinite(t),"Shortcut owner requires finite time");time=std::max(time,t);++eventRevision;}
    bool busy()const noexcept{return pending.has_value()||flight.has_value();}
    bool panel()const noexcept{return (pending&&pending->kind==ShortcutRequestKind::choose)||(flight&&flight->kind==ShortcutRequestKind::choose);}
    std::optional<core::Point>local(core::Point p)const{return input?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
    void cancelRead(){if(pending&&!writeRequest(pending->kind)){cancelled=pending->generation;pending.reset();}if(flight&&!writeRequest(flight->kind)&&flight->kind!=ShortcutRequestKind::launch){cancelled=flight->generation;flight.reset();}}
    void retireField(){auto previous=std::move(field);fieldDragging=false;pendingFinish.reset();if(!previous)return;previous->editor.reset();if(previous->published)retired.push_back(std::move(previous));}
    bool finish(bool commit){if(!field)return true;if(finishing)return false;finishing=true;struct Reset{bool&v;~Reset(){v=false;}}reset{finishing};auto current=field;auto editor=current->editor;if(!editor)return false;
        if(commit){const auto result=editor->commitComposition();if(!alive||field!=current)return true;if(result==TS_E_NOLOCK){pendingFinish=true;return false;}need(SUCCEEDED(result),"Cannot commit Shortcut composition");if(!current->normalize(true)){pendingFinish=true;return false;}if(!alive||field!=current)return true;}
        const auto text=commit?m::archiveUTF8(current->document.text()):std::string{};const auto stopped=editor->stop();if(!alive||field!=current)return true;if(stopped==TS_E_NOLOCK){pendingFinish=commit;return false;}need(SUCCEEDED(stopped),"Cannot finish Shortcut name");if(commit)state.setDraftName(text);retireField();return true;
    }
    void poseField(){if(!field||!pose)return;auto current=field;auto editor=current->editor;if(!editor)return;const auto event=eventRevision;const auto world=pose->contentWorld*M::translation(m::shortcutNameRect.x,m::shortcutNameRect.y);const auto camera=g::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*M::scale(metrics.scale,metrics.scale);
        editor->setPose({world,camera,metrics.pixelWidth,metrics.pixelHeight,pose->opacity,input&&!state.transitioning(),focused,true});if(!alive||field!=current||event!=eventRevision||!pose)return;
        for(auto*part:{&field->background,&field->border}){const std::array<g::LayerPlacement,1>v{{{0,world,pose->opacity,std::span(&pose->hostClip,1)}}};part->setPlacements(v);part->setGroupShutter(pose->shutter);}
        field->masks={g::PlaneMask{core::source::inverseSourceMatrix(world),{0,0,304,27},3},pose->hostClip};const auto draws=field->glyphs.prepareDraws();need(draws.size()==4,"Shortcut editor needs original shared four leaves");for(std::size_t n=0;n<4;++n)field->placements[n]={n,draws[n].world,draws[n].opacity,field->masks};field->glyphs.setPlacements(field->placements);field->glyphs.setGroupShutter(pose->shutter);
    }
    void begin(){if(!input||busy()||!state.editing()||state.transitioning()||!finish(true)||!alive)return;const auto token=lifecycle;auto next=std::make_shared<Field>(window,raster,options.raster,options.appearance,state.draftName(),options.textMessage);auto editor=next->editor;if(manager)need(SUCCEEDED(editor->connect(*manager,client)),"Cannot connect Shortcut name to shared TSF");if(!alive||token!=lifecycle||!input||!state.editing())return;field=next;if(manager&&focused)need(SUCCEEDED(editor->focus(true)),"Cannot focus Shortcut name");if(!alive||field!=next||token!=lifecycle||!input)return;poseField();}
    void syncField(){auto current=field;if(current&&current->editor){auto editor=current->editor;current->normalize();if(alive&&field==current)editor->syncContent();}}
    bool enqueue(ShortcutRequest request){if(busy())return false;need(nextRequest!=UINT64_MAX,"Shortcut request generation exhausted");request.generation=++nextRequest;pending=std::move(request);return true;}
    bool perform(m::ShortcutAction a){if(!input||busy()||state.transitioning()||!finish(true)||!alive||!input||busy()||state.transitioning())return false;
        // Copy actions before editor completion can change state storage.
        switch(a.kind){case m::ShortcutActionKind::name:begin();return true;case m::ShortcutActionKind::icon:return state.selectIcon(a.value,time);case m::ShortcutActionKind::cancel:state.cancelDraft(time);return true;case m::ShortcutActionKind::choose:{ShortcutRequest r;r.kind=ShortcutRequestKind::choose;return enqueue(std::move(r));}case m::ShortcutActionKind::save:{if(!state.candidate())return false;ShortcutRequest r;r.kind=ShortcutRequestKind::save;r.candidate=state.candidate();r.editingID=state.editingID();r.name=state.draftName();r.icon=state.draftIcon();return enqueue(std::move(r));}default:break;}
        const auto found=std::find_if(state.file().items.begin(),state.file().items.end(),[&](const auto&v){return v.id==a.value;});if(found==state.file().items.end()||state.editing())return false;ShortcutRequest r;r.record=*found;r.kind=a.kind==m::ShortcutActionKind::edit?ShortcutRequestKind::edit:a.kind==m::ShortcutActionKind::remove?ShortcutRequestKind::remove:ShortcutRequestKind::launch;if(r.kind==ShortcutRequestKind::edit)r.editingID=found->id;return enqueue(std::move(r));
    }
    void deactivate(){const auto token=++lifecycle;finish(true);if(!alive||token!=lifecycle)return;cancelRead();pressed=fieldDragging=externalDrag=false;pointerPoint.reset();state.setActive(false,time);active=false;input=false;scene.setFeedback({},false,reduced,time);}
};
AppShortcutPreview::AppShortcutPreview(HWND w,g::LayerRasterizer&r,const g::NativeAppShortcutAssets&a,ITfThreadMgr*m,TfClientId c,AppShortcutPreviewOptions o):impl_(std::make_shared<Impl>(w,r,a,m,c,std::move(o))){}
AppShortcutPreview::~AppShortcutPreview(){auto i=std::move(impl_);i->alive=false;if(i->field)i->field->editor.reset();}
void AppShortcutPreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Shortcut viewport");impl_->metrics=m;}
void AppShortcutPreview::setAppearance(m::ShortcutAppearance a,double t){auto i=impl_;i->clock(t);const bool changed=i->scene.setAppearance(a);i->options.appearance=a;if(i->field&&(changed||i->field->glyphs.fontRevision()!=i->raster.fontRevision())){i->field->appearance(a,i->options.raster);i->syncField();}}
void AppShortcutPreview::setLanguage(core::Language l,double t){auto a=impl_->options.appearance;a.language=l;setAppearance(a,t);}
void AppShortcutPreview::setReduceMotion(bool value,double t){auto i=impl_;i->clock(t);i->reduced=value;i->state.setReducedMotion(value,i->time);}
void AppShortcutPreview::setOriginalImages(m::ShortcutOriginalImages value){impl_->scene.setOriginalImages(std::move(value));}
void AppShortcutPreview::showError(std::optional<std::string> value,double t){impl_->clock(t);impl_->state.showError(std::move(value));}
void AppShortcutPreview::setOverlayVisible(bool value,double t){auto i=impl_;i->clock(t);if(value==i->overlayVisible)return;i->overlayVisible=value;if(!value)i->deactivate();}
void AppShortcutPreview::update(const M&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){auto i=impl_;i->clock(t);const auto event=i->eventRevision;if(i->pendingFinish)i->finish(*i->pendingFinish);if(!i->alive||event!=i->eventRevision)return;const auto*shown=sample.current.module==core::Module::addApp?&sample.current:sample.incoming&&sample.incoming->module==core::Module::addApp?&*sample.incoming:nullptr;i->requested=shown&&sample.requested==core::Module::addApp;const bool active=i->overlayVisible&&i->requested;if(active!=i->active){if(active){i->active=true;i->state.setActive(true,i->time);}else i->deactivate();}if(!i->alive||event!=i->eventRevision)return;i->input=active&&sample.acceptsModuleInput;
    if(!shown){i->pose.reset();i->registration.update({});return;}i->state.advance(i->time);i->scene.syncContent(i->time);i->surface.update(center,settings,*shown,opacity);i->pose=i->surface.pose();const auto&p=*i->pose;const auto camera=g::layerViewportProjection(i->metrics.pixelWidth,i->metrics.pixelHeight)*M::scale(i->metrics.scale,i->metrics.scale);i->projection=core::Projection::viewport(camera*p.contentWorld,i->metrics.pixelWidth,i->metrics.pixelHeight);
    i->scene.setFeedback(i->pointerPoint?i->local(*i->pointerPoint):std::nullopt,i->pressed,i->reduced,i->time);i->scene.updatePose({p.contentWorld,p.opacity,i->time,std::span(&p.hostClip,1),p.shutter?&*p.shutter:nullptr});i->registration.update(p.registration);if(i->input&&i->state.takeNameRequest())i->begin();i->syncField();i->poseField();
}
bool AppShortcutPreview::requiresFrames(double t)const{return impl_->pose&&impl_->scene.requiresFrames(std::max(t,impl_->time));}
bool AppShortcutPreview::covers(core::Point p)const{const auto q=impl_->local(p);return q&&core::Rect{0,0,400,334}.contains(*q);}
bool AppShortcutPreview::pointer(const app::PointerEvent&e,double t){auto i=impl_;i->clock(t);i->pointerPoint=core::Point{e.x,e.y};const auto q=i->local(*i->pointerPoint);const core::Point physical{e.x*i->metrics.scale,e.y*i->metrics.scale};
    if(e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost){cancelInteraction(t);return false;}if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool owned=i->pressed||i->fieldDragging;i->pressed=i->fieldDragging=false;if(auto current=i->field){auto editor=current->editor;if(editor)editor->pointerUp();}i->scene.setFeedback(q,false,i->reduced,i->time);return owned;}if(!i->input||i->panel())return false;
    if(e.kind==app::PointerKind::move){if(i->fieldDragging&&i->field){auto current=i->field;auto editor=current->editor;if(editor)editor->pointerDrag(physical);return true;}i->scene.setFeedback(q,i->pressed,i->reduced,i->time);return q&&core::Rect{0,0,400,334}.contains(*q);}
    if((e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)&&e.button==app::PointerButton::left&&q&&core::Rect{0,0,400,334}.contains(*q)){if(i->field&&m::shortcutNameRect.contains(*q)){auto current=i->field;auto editor=current->editor;const auto event=i->eventRevision;i->syncField();if(!i->alive||i->field!=current||event!=i->eventRevision||!editor)return true;const auto result=editor->pointerDown(physical,(e.modifiers&MK_SHIFT)!=0);if(i->alive&&i->field==current&&event==i->eventRevision)i->fieldDragging=result.handled;return true;}const auto*hit=i->state.hit(*q);const auto action=hit?std::optional(*hit):std::nullopt;if(!i->finish(true)||!i->alive||!i->input)return true;i->pressed=true;i->scene.setFeedback(q,true,i->reduced,i->time);if(action)i->perform(*action);return true;}return false;
}
bool AppShortcutPreview::scroll(core::Point p,double delta,double t){auto i=impl_;i->clock(t);if(!i->input||i->panel()||i->field||i->busy())return false;const auto q=i->local(p);return q&&i->state.scroll(*q,delta);}
bool AppShortcutPreview::wheel(const app::WheelEvent&e,double t){if(e.horizontal)return false;return scroll({e.x,e.y},-e.steps*(e.linesPerStep==UINT32_MAX?252.:12.*e.linesPerStep),t);}
bool AppShortcutPreview::key(const app::KeyEvent&e,double t){return key(e,e.alt||GetKeyState(VK_SHIFT)<0||GetKeyState(VK_CONTROL)<0||GetKeyState(VK_MENU)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0,t);}
bool AppShortcutPreview::key(const app::KeyEvent&e,bool modified,double t){auto i=impl_;i->clock(t);if(!i->input||i->panel())return false;if(auto current=i->field){auto editor=current->editor;if(!editor)return false;const auto event=i->eventRevision;const auto valid=[&]{return i->alive&&i->field==current&&event==i->eventRevision;};const bool shift=GetKeyState(VK_SHIFT)<0,control=GetKeyState(VK_CONTROL)<0,system=e.alt||GetKeyState(VK_MENU)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0;
    if(e.kind==app::KeyKind::character||e.kind==app::KeyKind::unicodeCharacter){if(e.value=='\r'||e.value=='\n'||e.value=='\t'||e.value==27)return true;if(control||system)return false;editor->character(e.value,e.kind==app::KeyKind::unicodeCharacter);if(valid())i->syncField();return true;}if(e.kind!=app::KeyKind::down)return false;if(!modified&&!current->document.composition()){if(e.value==VK_ESCAPE){i->finish(false);return true;}if(e.value==VK_RETURN||e.value==VK_TAB){i->finish(true);return true;}}if(system)return false;using C=g::ProjectedEditorCommand;
    if(control){if(e.value=='A')editor->command(C::selectAll);else if(e.value=='Z'){if(shift)editor->redo();else editor->undo();}else if(e.value=='Y')editor->redo();else return false;if(valid())i->syncField();return true;}
    std::optional<C>command;switch(e.value){case VK_LEFT:command=C::left;break;case VK_RIGHT:command=C::right;break;case VK_HOME:command=C::documentStart;break;case VK_END:command=C::documentEnd;break;case VK_BACK:command=C::backspace;break;case VK_DELETE:command=C::deleteForward;break;default:return false;}i->syncField();if(!valid())return true;editor->command(*command,shift);if(valid())i->syncField();return true;}
    if(e.kind==app::KeyKind::down&&!modified&&e.value==VK_ESCAPE&&i->state.editing()&&!i->busy()){i->state.cancelDraft(i->time);return true;}return false;
}
bool AppShortcutPreview::filterKey(const app::NativeMessage&m){auto i=impl_;auto current=i->field;if(!i->input||!current||m.window!=i->window)return false;auto editor=current->editor;return editor&&editor->filterKeyMessage(m.message,m.wParam,m.lParam);}
bool AppShortcutPreview::message(const app::NativeMessage&m,double t){auto i=impl_;if(m.window!=i->window||m.message!=i->options.textMessage)return false;i->clock(t);auto current=i->field;if(current&&m.wParam==current->generation){auto editor=current->editor;if(editor)editor->takeChanges(m.wParam);if(i->pendingFinish)i->finish(*i->pendingFinish);else i->syncField();}return true;}
void AppShortcutPreview::focus(bool value,double t){auto i=impl_;i->clock(t);if(i->focused==value)return;i->focused=value;++i->lifecycle;if(!value){i->finish(true);return;}auto current=i->field;if(current&&current->editor&&i->manager){auto editor=current->editor;const auto result=editor->focus(true);if(!i->alive)return;need(SUCCEEDED(result)||result==TS_E_NOLOCK,"Cannot update Shortcut text focus");}}
void AppShortcutPreview::cancelInteraction(double t){auto i=impl_;i->clock(t);i->pressed=i->fieldDragging=i->externalDrag=false;i->pointerPoint.reset();i->state.setDropTarget(false);if(auto current=i->field){auto editor=current->editor;if(editor)editor->pointerUp();}i->scene.setFeedback({},false,i->reduced,i->time);}
bool AppShortcutPreview::pointerLocked()const noexcept{return impl_->fieldDragging||impl_->externalDrag||impl_->panel();}
bool AppShortcutPreview::presentingPanel()const noexcept{return impl_->panel();}
bool AppShortcutPreview::editingName()const noexcept{return bool(impl_->field);}
bool AppShortcutPreview::finishEditing(bool commit,double t){auto i=impl_;i->clock(t);return i->finish(commit);}
bool AppShortcutPreview::perform(const m::ShortcutAction&a,double t){auto i=impl_;i->clock(t);return i->perform(a);}
bool AppShortcutPreview::inspectSelection(g::AppShortcutSelection selection,double t){auto i=impl_;i->clock(t);if(!i->input||i->busy()||!i->finish(true)||!i->alive||!i->input||i->busy())return false;ShortcutRequest r;r.kind=ShortcutRequestKind::inspect;r.selection=std::move(selection);return i->enqueue(std::move(r));}
void AppShortcutPreview::setExternalDrag(bool value,double t){auto i=impl_;i->clock(t);if(value&&(!i->input||i->panel()||!i->finish(true)||!i->alive||!i->input||i->panel()))return;i->externalDrag=value;i->state.setDropTarget(value);}
std::optional<ShortcutRequest>AppShortcutPreview::takeRequest(){auto i=impl_;if(!i->pending)return {};auto result=std::move(i->pending);i->pending.reset();i->flight=Impl::Flight{result->generation,result->kind,result->editingID};return result;}
std::optional<std::uint64_t>AppShortcutPreview::takeCancelledRequest()noexcept{return std::exchange(impl_->cancelled,{});}
bool AppShortcutPreview::completeCandidate(std::uint64_t token,m::ShortcutCandidate value,double t){auto i=impl_;i->clock(t);if(!i->flight||i->flight->generation!=token)return false;const auto kind=i->flight->kind;need(kind==ShortcutRequestKind::choose||kind==ShortcutRequestKind::inspect||kind==ShortcutRequestKind::edit,"Shortcut result kind mismatch");if(!i->active){i->flight.reset();return false;}++i->lifecycle;i->state.beginDraft(std::move(value),i->flight->editingID,i->time);i->flight.reset();return true;}
bool AppShortcutPreview::completeFile(std::uint64_t token,m::ShortcutFile value,double t){auto i=impl_;i->clock(t);if(!i->flight||i->flight->generation!=token)return false;need(writeRequest(i->flight->kind),"Shortcut file completion requires committed write");if(i->flight->kind==ShortcutRequestKind::save)i->state.saved(std::move(value),i->time);else i->state.replaceItems(std::move(value));i->flight.reset();return true;}
bool AppShortcutPreview::completeRequest(std::uint64_t token,std::optional<std::string>error,double t){auto i=impl_;i->clock(t);if(!i->flight||i->flight->generation!=token)return false;need(error||!writeRequest(i->flight->kind),"Successful Shortcut write needs committed file");if(error)i->state.showError(std::move(error));i->flight.reset();return true;}
bool AppShortcutPreview::busy()const noexcept{return impl_->busy();}
const m::AppShortcutState&AppShortcutPreview::state()const noexcept{return impl_->state;}
void AppShortcutPreview::upload(g::Renderer&r){auto i=impl_;if(i->pose){i->registration.uploadGeometry(r);i->scene.uploadAnimations(r);}}
std::span<const g::LayerCompositionEntry>AppShortcutPreview::entries(){auto i=impl_;i->composed.clear();if(i->pose){for(const auto&e:i->scene.entries())i->composed.push_back(e);if(i->field&&i->active){i->field->published=true;i->composed.push_back({&i->field->background,{}});i->composed.push_back({&i->field->glyphs,{}});i->composed.push_back({&i->field->border,{}});}i->composed.push_back({&i->geometry,i->registration.draws()});}return i->composed;}
void AppShortcutPreview::collected(g::Renderer&r){auto i=impl_;need(i->scene.collectRetired(r),"Shortcut retired artwork remains published");for(auto&f:i->retired)need(f->release(r),"Shortcut retired field remains published");i->retired.clear();if(i->field)for(auto*s:{&i->field->background,&i->field->glyphs,&i->field->border})s->collectRetiredResources(r);}
void AppShortcutPreview::release(g::Renderer&r){auto i=impl_;need(i->finish(false),"Finish Shortcut text transaction before release");collected(r);need(i->scene.releaseResources(r)&&i->geometry.releaseResources(r)&&i->registration.releaseResources(r),"Detach Shortcut composition before release");}
}
#endif
