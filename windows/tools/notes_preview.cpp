#include "tools/notes_preview.hpp"
#include "native/module_scene.hpp"
#include "native/layer_group.hpp"
#include "native/module_registration.hpp"
#include "core/module_presentation.hpp"
#include "core/source_camera.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <wrl/client.h>

namespace endfield::tools {
namespace {
namespace gpu=native;namespace mod=modules;namespace data=ehud::data;
using Matrix=core::Matrix4;using Json=data::Json;
void need(bool value,const char*why){if(!value)throw std::runtime_error(why);}
struct TextManager {
    Microsoft::WRL::ComPtr<ITfThreadMgr> manager;TfClientId client{TF_CLIENTID_NULL};bool active{};
    void start(){need(SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager))),"Create shared preview text manager");need(SUCCEEDED(manager->Activate(&client)),"Activate shared preview text manager");active=true;}
    ~TextManager(){if(active)manager->Deactivate();}
};
gpu::NativeNotesWorkspaceStyle style(){gpu::NativeNotesWorkspaceStyle s;s.palette=mod::NotesPalette::source(true,{.98,.83,.12,1});s.editor={{.12,.12,.12,1},s.palette.accent};
    s.strings.textTitle="文字";s.strings.placeholder="双击开始输入…";s.strings.pin="固定便笺";s.strings.unpin="取消固定";s.strings.remove="删除便笺";s.strings.edit="编辑文字";return s;}
mod::NotesControlsInput controlInput(mod::NotesControlsKind kind){mod::NotesControlsInput i;i.kind=kind;i.strings.heading="便笺";i.strings.tools={"文字","待办","图片/视频","画板"};i.strings.cancelDeletion="取消删除";i.strings.confirmDeletion="确认删除";return i;}
Json emptyPlane(){return Json::Object{{"bounds",Json::Array{0,0,400,334}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
}
struct NotesPreview::Impl {
    enum class TrackKind {sectionIn,sectionOut,create,remove,mutation};
    struct Track {gpu::NativeNotesCardToken token;TrackKind kind;double start;core::MotionPoint direction;};
    HWND hwnd;gpu::LayerRasterizer&raster;const gpu::NativeNotesControlsAssets&assets;TextManager manager;
    std::unique_ptr<data::NotesStore>store;std::unique_ptr<mod::NotesState>state;std::unique_ptr<gpu::NativeNotesWorkspace>workspace;
    mod::NotesControls controls,confirmation;std::unique_ptr<gpu::NativeNotesControlsScene>controlScene,confirmScene;std::unique_ptr<gpu::NativeLayerGroup>confirmationGroup;bool groupUploaded{};
    gpu::LayerScene geometry;std::unique_ptr<gpu::NativeModuleSurface>surface;
    gpu::NativeModuleRegistration registration{"preview.notes.registration"};core::ModulePresentation modules{core::Module::notes};
    app::ClientMetrics metrics;Matrix workspaceWorld,confirmWorld;core::Projection controlsProjection,confirmationProjection,workspaceProjection;
    std::vector<Track>tracks;std::vector<gpu::LayerCompositionEntry>composed;
    std::array<double,4>toolbarStarted{-1,-1,-1,-1};
    bool hasPose{},moduleVisible{true},moduleInput{true},focused{},editorDrag{},controlsPressed{},confirmationVisible{};
    float confirmationOpacity{};double currentTime{},confirmationStarted{};std::uint64_t outgoingGeneration{},uploadedRegistration{};
    unsigned timeDepth{};
    // Win32 capture/focus and TSF can synchronously reenter this owner. Nested
    // Notes work shares the outer event instant; a resumed older callback is
    // clamped at the boundary. Keep the strict clocks inside retained scenes.
    double queryTime(double time) const {
        need(std::isfinite(time),"Notes owner requires a finite clock");
        return std::max(currentTime,time);
    }
    struct TimeScope {
        Impl& owner;
        const double time;
        TimeScope(Impl& value,double requested):owner(value),time(value.timeDepth?
            (need(std::isfinite(requested),"Notes owner requires a finite clock"),value.currentTime):value.queryTime(requested)) {
            owner.currentTime=time;++owner.timeDepth;
        }
        ~TimeScope(){--owner.timeDepth;}
        TimeScope(const TimeScope&)=delete;
        TimeScope& operator=(const TimeScope&)=delete;
    };

    std::optional<core::Module>pendingModule;std::optional<app::ClientMetrics>pendingResize;bool pendingCancel{},pendingFocus{};
    std::optional<std::string>hoveredNote;std::optional<core::Rect>lastConfirmationRect;
    Impl(HWND h,gpu::LayerRasterizer&r,const std::filesystem::path&root,const gpu::NativeNotesControlsAssets&a,bool tsf):hwnd(h),raster(r),assets(a),geometry(r){
        need(root.is_absolute()&&!std::filesystem::exists(root),"Notes preview requires a new absolute synthetic data directory");
        data::detail::validateRoot(root);need(std::filesystem::create_directory(root),"Create isolated Notes fixture directory");
        store=std::make_unique<data::NotesStore>(root);data::Note sample;sample.text="双击编辑文字\n终末地 · EndfieldHUD\n日本語 한국어 😀";sample.width=240;sample.height=145;sample.x=180;sample.y=245;store->upsert(sample);
        state=std::make_unique<mod::NotesState>(store->notes(),mod::NotesState::Persistence{[this](const auto&n){store->upsert(n);},[this](auto id){store->remove(id);}});
        state->setWorkspaceBounds({0,0,1280,800},{core::Point{540,280}});
        if(tsf)manager.start();gpu::NativeNotesWorkspaceOptions wo;wo.raster.pixelsPerPoint=2;wo.raster.paddingPoints=1;wo.activatedTextManager=manager.manager.Get();wo.textClient=manager.client;
        workspace=std::make_unique<gpu::NativeNotesWorkspace>(hwnd,*state,raster,style(),wo);
        gpu::LayerRasterOptions ro;ro.pixelsPerPoint=2;ro.paddingPoints=1;ro.assetRoot=assets.assetRoot();
        controls.update(controlInput(mod::NotesControlsKind::center));controlScene=std::make_unique<gpu::NativeNotesControlsScene>(controls,raster,ro);controlScene->syncContent(assets.imagesFor(controls),1);
        confirmation.update(controlInput(mod::NotesControlsKind::deletion));confirmScene=std::make_unique<gpu::NativeNotesControlsScene>(confirmation,raster,ro);confirmScene->syncContent();confirmationGroup=std::make_unique<gpu::NativeLayerGroup>(confirmScene->scene(),"notes.confirmation",2);
        geometry.load(emptyPlane(),ro);surface=std::make_unique<gpu::NativeModuleSurface>(geometry,core::Module::notes);
        tracks.reserve(256);composed.reserve(132);
    }
    void track(gpu::NativeNotesCardToken token,TrackKind kind,double time,core::MotionPoint direction={}){
        const auto it=std::find_if(tracks.begin(),tracks.end(),[&](const auto&t){return t.token==token;});Track value{token,kind,time,direction};if(it!=tracks.end())*it=value;else tracks.push_back(value);
    }
    void change(const std::optional<core::ModulePresentationChange>&value,double time){
        if(!value)return;const bool before=value->from==core::Module::notes,after=value->to==core::Module::notes;if(before==after)return;
        need(finish(),"Module transition began before the Notes text service unlocked");std::vector<std::pair<std::string,std::optional<gpu::NativeNotesCardToken>>>previous;previous.reserve(state->notes().size());for(const auto&n:state->notes())previous.emplace_back(n.id,workspace->cardToken(n.id));
        workspace->setPresentation(after,value->animated);outgoingGeneration=workspace->presentationGeneration();
        for(const auto&n:state->notes()){const auto token=workspace->cardToken(n.id);if(!token)continue;const auto old=std::find_if(previous.begin(),previous.end(),[&](const auto&p){return p.first==n.id;});
            if(old!=previous.end()&&old->second==token)continue;
            if(old!=previous.end()&&old->second)std::erase_if(tracks,[&](const auto&t){return t.token==*old->second;});
            if(value->animated)track(*token,after?TrackKind::sectionIn:TrackKind::sectionOut,time,core::moduleDirection(value->from,value->to));
        }
        lastConfirmationRect.reset();confirmationVisible=false;
    }
    void focusEditor(){pendingFocus=workspace->focusEditor(focused)==TS_E_NOLOCK;}
    bool finish(){if(workspace->editor()&&!workspace->finishEditing().finished)return false;pendingFocus=false;editorDrag=false;controlsPressed=false;workspace->cancelInteraction();workspace->cancelDeletion();lastConfirmationRect.reset();confirmationVisible=false;return true;}
    void animate(double time){bool settle=false;
        for(auto it=tracks.begin();it!=tracks.end();){mod::NotesMotionSample m;switch(it->kind){
            case TrackKind::sectionIn:case TrackKind::sectionOut:m=mod::notesSectionMotion(it->kind==TrackKind::sectionIn,it->direction,time-it->start);break;
            case TrackKind::create:case TrackKind::remove:m=mod::notesCardMotion(it->kind==TrackKind::create,time-it->start);break;
            case TrackKind::mutation:m=mod::notesMutationMotion(time-it->start);break;}
            const std::array patch{gpu::NativeNotesCardMotion{it->token,m}};const bool live=workspace->setCardMotions(patch);
            if(!live||!m.active){if(live&&it->kind==TrackKind::remove)workspace->settleDeletion(it->token);if(live&&it->kind==TrackKind::sectionOut)settle=true;it=tracks.erase(it);}else ++it;
        }if(settle)workspace->settleOutgoing(outgoingGeneration);
    }
    core::Point physical(const app::PointerEvent&e)const{return {e.x*metrics.scale,e.y*metrics.scale};}
    void clearHover(double time){if(hoveredNote){workspace->setFeedback(*hoveredNote,{},false,false,time);hoveredNote.reset();}controlScene->setFeedback({},false,false,time);confirmScene->setFeedback({},false,false,time);}
    void editSync(){workspace->syncEditor();}
    bool confirmAt(core::Point p,double time,bool press){
        if(!confirmationVisible)return false;const auto q=confirmationProjection.unproject(p);const auto action=q?confirmation.actionAt(*q):std::nullopt;confirmScene->setFeedback(action,press,false,time);
        if(!action)return false;if(!press)return true;
        if(*action=="cancelDelete")workspace->cancelDeletion();else if(state->pendingDeletion()){const auto id=*state->pendingDeletion();if(const auto token=workspace->confirmDeletionRetainingArtwork(id))track(*token,TrackKind::remove,time);}
        confirmationVisible=false;lastConfirmationRect.reset();return true;
    }
    bool pointer(const app::PointerEvent&e,double time){
        if(!hasPose)return false;const TimeScope event(*this,time);time=event.time;const auto p=physical(e);
        if(e.kind==app::PointerKind::captureLost){editorDrag=false;if(auto*editor=workspace->editor())editor->pointerUp();workspace->endGesture();controlsPressed=false;clearHover(time);return false;}
        if(e.kind==app::PointerKind::leave){clearHover(time);return false;}
        if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool handled=editorDrag||state->dragging()||controlsPressed;editorDrag=false;controlsPressed=false;if(auto*editor=workspace->editor())editor->pointerUp();workspace->endGesture();const auto q=controlsProjection.unproject(p);controlScene->setFeedback(q?controls.actionAt(*q):std::nullopt,false,false,time);return handled;}
        if(!moduleInput){clearHover(time);return false;}
        if(e.kind==app::PointerKind::move){
            if(editorDrag){if(auto*editor=workspace->editor()){editor->pointerDrag(p);editSync();}return true;}
            if(state->dragging()){if(const auto point=workspaceProjection.unproject(p))workspace->dragTo(*point);return true;}
            const bool onConfirm=confirmAt(p,time,false);const auto hit=onConfirm?std::nullopt:workspace->hitTest(p);
            if(hoveredNote&&(!hit||hit->noteID!=*hoveredNote)){workspace->setFeedback(*hoveredNote,{},false,false,time);hoveredNote.reset();}
            if(hit){const auto verb=hit->kind==gpu::NativeNotesWorkspaceHit::Kind::action&&hit->verb!="edit"?std::optional<std::string_view>{hit->verb}:std::nullopt;
                if(!hoveredNote)hoveredNote=std::string(hit->noteID);workspace->setFeedback(hit->noteID,verb,false,false,time);}
            std::optional<std::string_view>action;if(!onConfirm&&!hit&&moduleVisible&&moduleInput){const auto q=controlsProjection.unproject(p);if(q)action=controls.actionAt(*q);}
            controlScene->setFeedback(action,controlsPressed,false,time);return onConfirm||hit.has_value()||action.has_value();
        }
        if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left)return false;
        if(confirmAt(p,time,true))return true;
        if(const auto hit=workspace->hitTest(p)){
            const std::string id(hit->noteID),verb(hit->verb);const auto basePoint=workspaceProjection.unproject(p);if(!basePoint)return true;const auto point=*basePoint;
            if(hit->kind==gpu::NativeNotesWorkspaceHit::Kind::editor){workspace->editor()->pointerDown(p,(e.modifiers&MK_SHIFT)!=0);editorDrag=true;editSync();return true;}
            if(workspace->editor()){if(!workspace->finishEditing().finished)return true;}
            workspace->select(id);
            if(hit->kind==gpu::NativeNotesWorkspaceHit::Kind::resize)workspace->beginGesture(id,point,mod::NotesState::Gesture::resize);
            else if(verb=="pin"){workspace->togglePin(id);if(state->visible(id))if(const auto token=workspace->cardToken(id))track(*token,TrackKind::mutation,time);}
            else if(verb=="delete"){workspace->requestDeletion(id);confirmationStarted=time;}
            else if(verb=="edit"&&e.kind==app::PointerKind::doubleClick){workspace->beginEditing(id);focusEditor();}
            else if(hit->kind==gpu::NativeNotesWorkspaceHit::Kind::body||verb=="edit")workspace->beginGesture(id,point,mod::NotesState::Gesture::move);
            else std::cout<<"Notes formatting is not yet installed in this isolated plain-text preview\n";
            return true;
        }
        if(workspace->editor()&&!workspace->finishEditing().finished)return true;
        if(state->pendingDeletion()){workspace->cancelDeletion();confirmationVisible=false;return true;}
        if(moduleVisible&&moduleInput){const auto q=controlsProjection.unproject(p);if(const auto action=q?controls.actionAt(*q):std::nullopt){
            controlScene->setFeedback(action,true,false,time);controlsPressed=true;
            for(std::size_t n=0;n<controls.actions().size();++n)if(controls.actions()[n].id==*action)toolbarStarted[n]=time;
            if(*action=="tool:text"){const auto id=data::makeUUID();workspace->createText(id,data::foundationNow());if(const auto token=workspace->cardToken(id))track(*token,TrackKind::create,time);focusEditor();}
            else std::cout<<"Notes TODO/media/drawing adapter is not yet installed in this isolated preview\n";return true;
        }}workspace->select({});return false;
    }
};
NotesPreview::NotesPreview(HWND h,native::LayerRasterizer&r,const std::filesystem::path&root,const native::NativeNotesControlsAssets&a,bool tsf):impl_(std::make_unique<Impl>(h,r,root,a,tsf)){}
NotesPreview::~NotesPreview()=default;
void NotesPreview::select(core::Module m,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);t=event.time;if(!i.finish()){i.pendingModule=m;return;}i.pendingModule.reset();const auto result=i.modules.select(m,t);i.moduleInput=result.presentation.acceptsModuleInput;i.change(result.change,t);}
core::Module NotesPreview::selected()const noexcept{return impl_->modules.requested();}
void NotesPreview::resize(const app::ClientMetrics&m){auto&i=*impl_;if(i.metrics==m)return;if(!i.finish()){i.pendingResize=m;return;}i.pendingResize.reset();i.metrics=m;i.workspace->setWorkspaceBounds({0,0,m.width,m.height},core::Point{m.width*.5-100,m.height*.5-120});}
void NotesPreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,float opacity,double t,bool focused){
    auto&i=*impl_;const Impl::TimeScope event(i,t);t=event.time;i.focused=focused;const auto sample=i.modules.sample(t);i.change(sample.change,t);i.animate(t);i.moduleInput=sample.presentation.acceptsModuleInput;
    const auto layout=core::source::DesktopChromeLayout::make(settings,center,{});i.workspaceWorld=center*core::source::inverseSourceMatrix(layout.designToScreen);
    const auto camera=gpu::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight)*Matrix::scale(i.metrics.scale,i.metrics.scale);
    i.workspaceProjection=core::Projection::viewport(camera*i.workspaceWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);
    i.workspace->updatePose({i.workspaceWorld,camera,i.metrics.pixelWidth,i.metrics.pixelHeight,opacity,t,focused,true});
    const auto*surface=sample.presentation.current.module==core::Module::notes?&sample.presentation.current:sample.presentation.incoming&&sample.presentation.incoming->module==core::Module::notes?&*sample.presentation.incoming:nullptr;
    i.moduleVisible=surface!=nullptr;
    if(surface){i.surface->update(center,settings,*surface,opacity);const auto&p=i.surface->pose();std::array<double,4>offsets{};for(std::size_t n=0;n<4;++n)if(i.toolbarStarted[n]>=0)offsets[n]=mod::notesToolbarMotion(t-i.toolbarStarted[n]).y;
        i.controlScene->updatePose(p.contentWorld,p.opacity,t,offsets,std::span(&p.hostClip,1),p.shutter);i.registration.update(p.registration);
        i.controlsProjection=core::Projection::viewport(camera*p.contentWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);
    }else{i.controlScene->updatePose({},0,t);i.registration.update({});}
    const auto rects=i.state->deletionControls();i.confirmationVisible=rects.has_value();
    if(rects){const auto r=(*rects)[0];const auto motion=mod::notesDeletionMenuMotion(t-i.confirmationStarted);i.confirmWorld=i.workspaceWorld*Matrix::translation(r.x,r.y+motion.y);i.lastConfirmationRect=r;
        i.confirmScene->updatePose({},1,t);i.confirmationOpacity=opacity;i.confirmationProjection=core::Projection::viewport(camera*i.confirmWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);
    }else{i.confirmScene->updatePose({},1,t);i.confirmationOpacity=0;}
    i.hasPose=true;
}
bool NotesPreview::requiresFrames(double t)const{const auto&i=*impl_;t=i.queryTime(t);if(i.modules.requiresFrames()||!i.tracks.empty()||i.workspace->requiresFrames(t)||i.controlScene->requiresFrames(t)||i.confirmScene->requiresFrames(t))return true;
    for(auto s:i.toolbarStarted)if(s>=0&&t-s<.18)return true;return i.confirmationVisible&&t-i.confirmationStarted<.16;}
bool NotesPreview::pointerLocked()const{return impl_->state->dragging();}
bool NotesPreview::covers(core::Point p)const{const auto&i=*impl_;if(!i.hasPose||!i.moduleInput)return false;p={p.x*i.metrics.scale,p.y*i.metrics.scale};
    if(i.confirmationVisible){const auto q=i.confirmationProjection.unproject(p);if(q&&i.confirmation.actionAt(*q))return true;}
    if(i.workspace->hitTest(p))return true;if(i.moduleVisible){const auto q=i.controlsProjection.unproject(p);if(q&&i.controls.actionAt(*q))return true;}return false;
}
bool NotesPreview::pointer(const app::PointerEvent&e,double t){return impl_->pointer(e,t);}
bool NotesPreview::filterKey(const app::NativeMessage&m){auto*e=impl_->workspace->editor();return e&&e->filterKeyMessage(m.message,m.wParam,m.lParam);}
bool NotesPreview::message(const app::NativeMessage&m){auto&i=*impl_;if(m.message!=WM_APP+181)return false;if(i.workspace->takeEditorChanges(m.wParam))i.editSync();
    // Selection/layout notifications are not focus transitions: refocusing
    // clears the field's active drag and any pending UTF-16 high surrogate.
    // Retry only an explicit field/window focus request held by a TSF lock.
    if(i.pendingCancel&&i.finish())i.pendingCancel=false;if(i.pendingResize){const auto value=*i.pendingResize;resize(value);}if(i.pendingModule){const auto value=*i.pendingModule;select(value,i.currentTime);}if(i.pendingFocus)i.focusEditor();return true;}
bool NotesPreview::key(const app::KeyEvent&e,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);auto*editor=i.workspace->editor();if(!editor||!i.moduleInput)return false;gpu::ProjectedEditorResult result;
    if(e.kind==app::KeyKind::character||e.kind==app::KeyKind::unicodeCharacter)result=editor->character(e.value,e.kind==app::KeyKind::unicodeCharacter);
    else if(e.kind==app::KeyKind::down){std::optional<gpu::ProjectedEditorCommand>command;using C=gpu::ProjectedEditorCommand;switch(e.value){case VK_LEFT:command=C::left;break;case VK_RIGHT:command=C::right;break;case VK_UP:command=C::up;break;case VK_DOWN:command=C::down;break;case VK_HOME:command=C::documentStart;break;case VK_END:command=C::documentEnd;break;case VK_BACK:command=C::backspace;break;case VK_DELETE:command=C::deleteForward;break;case VK_ESCAPE:command=C::finish;break;case 'A':if(GetKeyState(VK_CONTROL)<0)command=C::selectAll;break;}if(command)result=editor->command(*command,GetKeyState(VK_SHIFT)<0);}
    if(result.finishRequested){i.workspace->finishEditing();return true;}if(result.handled)i.editSync();return result.handled;
}
void NotesPreview::focus(bool f){impl_->focused=f;impl_->focusEditor();if(!f){impl_->editorDrag=false;if(!impl_->finish())impl_->pendingCancel=true;}}
bool NotesPreview::finish(){return impl_->finish();}
void NotesPreview::upload(native::Renderer&r){auto&i=*impl_;const auto revision=i.registration.stats().geometryRevision;if(i.uploadedRegistration!=revision){i.registration.uploadGeometry(r);i.uploadedRegistration=revision;}if(!i.groupUploaded){i.confirmationGroup->uploadResources(r);i.groupUploaded=true;}else i.confirmationGroup->updateLocal(r);i.confirmationGroup->setPose(i.confirmWorld,i.confirmationOpacity);}
std::span<const native::LayerCompositionEntry>NotesPreview::entries(){auto&i=*impl_;i.composed.clear();i.composed.push_back({&i.controlScene->scene(),i.registration.draws()});for(const auto&e:i.workspace->entries())i.composed.push_back(e);i.composed.push_back(i.confirmationGroup->entry());return i.composed;}
void NotesPreview::collected(native::Renderer&r){need(impl_->workspace->collectRetired(r),"Detached Notes assets must retire after publication");}
void NotesPreview::release(native::Renderer&r){auto&i=*impl_;need(i.workspace->releaseResources(r),"Notes workspace remains published during teardown");need(i.registration.releaseResources(r),"Notes registration remains published during teardown");need(i.controlScene->scene().releaseResources(r)&&i.confirmationGroup->releaseResources(r),"Notes controls remain published during teardown");}
}
