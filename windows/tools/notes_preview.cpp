#include "tools/notes_preview.hpp"
#include "tools/notes_format_menu.hpp"
#include "native/module_scene.hpp"
#include "native/layer_group.hpp"
#include "native/module_registration.hpp"
#include "core/module_presentation.hpp"
#include "core/source_camera.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
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
    s.strings.textTitle="文字";s.strings.placeholder="双击开始输入…";s.strings.pin="固定便笺";s.strings.unpin="取消固定";s.strings.remove="删除便笺";s.strings.edit="编辑文字";s.strings.todoTitle="待办";s.strings.itemPlaceholder="新事项…";s.strings.addItem="+ 添加事项";s.strings.checkItem="勾选";s.strings.uncheckItem="取消勾选";s.strings.editItem="编辑事项";s.strings.moveUp="上移";s.strings.moveDown="下移";s.strings.removeItem="删除事项";s.strings.addItemAction="添加事项";return s;}
mod::NotesControlsInput controlInput(mod::NotesControlsKind kind){mod::NotesControlsInput i;i.kind=kind;i.strings.heading="便笺";i.strings.tools={"文字","待办","图片/视频","画板"};i.strings.cancelDeletion="取消删除";i.strings.confirmDeletion="确认删除";i.strings.saveErrorPrefix="无法保存：";return i;}
Json emptyPlane(){return Json::Object{{"bounds",Json::Array{0,0,400,334}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
}
struct NotesPreview::Impl {
    enum class TrackKind {sectionIn,sectionOut,create,remove,mutation};
    struct Track {gpu::NativeNotesCardToken token;TrackKind kind;double start;core::MotionPoint direction;};
    HWND hwnd;gpu::LayerRasterizer&raster;const gpu::NativeNotesControlsAssets&assets;TextManager manager;
    std::unique_ptr<gpu::NativeNotesImageDecoder>imageDecoder;std::unique_ptr<gpu::NativeNotesImagePlayback>imagePlayback;
    std::unique_ptr<data::NotesStore>store;std::unique_ptr<mod::NotesState>state;std::unique_ptr<gpu::NativeNotesWorkspace>workspace;
    mod::NotesControls controls,confirmation;std::unique_ptr<gpu::NativeNotesControlsScene>controlScene,confirmScene;std::unique_ptr<gpu::NativeLayerGroup>confirmationGroup;bool groupUploaded{};
    gpu::LayerScene geometry;std::unique_ptr<gpu::NativeModuleSurface>surface;
    gpu::NativeModuleRegistration registration{"preview.notes.registration"};core::ModulePresentation modules{core::Module::notes};core::ModulePresentationSample moduleSample;
    std::unique_ptr<NotesFormatMenu>formatMenu;std::unique_ptr<NotesMediaMenu>mediaMenu;core::Rect formatNoteRect;
    std::optional<NotesMediaAction>mediaAction;UINT_PTR mediaGeneration{};bool mediaActive{true};std::uint64_t importSerial{};
    struct Import {std::string key,path;std::shared_ptr<const gpu::NotesImageInfo>info;HRESULT result{E_PENDING};std::shared_ptr<void>lease;};
    std::vector<Import>pendingImports;core::Point importPoint;std::optional<std::string>mediaError;
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
    Impl(HWND h,gpu::LayerRasterizer&r,const std::filesystem::path&root,const gpu::NativeNotesControlsAssets&a,bool tsf,const std::filesystem::path&formatRoot):hwnd(h),raster(r),assets(a),geometry(r){
        static std::atomic<UINT_PTR>mediaOwners{1};mediaGeneration=mediaOwners.fetch_add(1);pendingImports.reserve(8);
        need(root.is_absolute()&&!std::filesystem::exists(root),"Notes preview requires a new absolute synthetic data directory");
        data::detail::validateRoot(root);need(std::filesystem::create_directory(root),"Create isolated Notes fixture directory");
        store=std::make_unique<data::NotesStore>(root);data::Note sample;sample.text="双击编辑文字\n终末地 · EndfieldHUD\n日本語 한국어 😀\n01 · Scroll inside this note\n02 · 上下滚动查看内容\n03 · Select and edit this text\n04 · 中文输入测试\n05 · Notes keep their tilt\n06 · 滚动不改变便笺位置\n07 · More sample text\n08 · 临时测试内容\n09 · Drag the header to move\n10 · Resize with the corner\n11 · Scroll back to the top\n12 · End of the sample";sample.width=240;sample.height=145;sample.x=180;sample.y=245;store->upsert(sample);
        state=std::make_unique<mod::NotesState>(store->notes(),mod::NotesState::Persistence{[this](const auto&n){store->upsert(n);},[this](auto id){store->remove(id);}});
        state->setWorkspaceBounds({0,0,1280,800},{core::Point{540,280}});
        if(tsf)manager.start();gpu::NativeNotesWorkspaceOptions wo;wo.raster.pixelsPerPoint=2;wo.raster.paddingPoints=1;wo.activatedTextManager=manager.manager.Get();wo.textClient=manager.client;
        workspace=std::make_unique<gpu::NativeNotesWorkspace>(hwnd,*state,raster,style(),wo);
        mediaMenu=std::make_unique<NotesMediaMenu>(raster,[this](NotesMediaAction action){mediaAction=std::move(action);need(PostMessageW(hwnd,NotesPreview::mediaActionMessage,mediaGeneration,0)!=FALSE,"Queue Notes media choice");});
        if(!formatRoot.empty())formatMenu=std::make_unique<NotesFormatMenu>(raster,formatRoot,[this](const core::notes::FormatChange&c){auto*e=workspace->editor();if(!e)return false;const auto result=e->applyFormat(c);if(result.handled)editSync();return result.handled;});
        gpu::LayerRasterOptions ro;ro.pixelsPerPoint=2;ro.paddingPoints=1;ro.assetRoot=assets.assetRoot();
        controls.update(controlInput(mod::NotesControlsKind::center));controlScene=std::make_unique<gpu::NativeNotesControlsScene>(controls,raster,ro);controlScene->syncContent(assets.imagesFor(controls),1);
        confirmation.update(controlInput(mod::NotesControlsKind::deletion));confirmScene=std::make_unique<gpu::NativeNotesControlsScene>(confirmation,raster,ro);confirmScene->syncContent();confirmationGroup=std::make_unique<gpu::NativeLayerGroup>(confirmScene->scene(),"notes.confirmation",2);
        geometry.load(emptyPlane(),ro);surface=std::make_unique<gpu::NativeModuleSurface>(geometry,core::Module::notes);
        tracks.reserve(256);composed.reserve(132);
        moduleSample=modules.sample(0).presentation;
    }
    void startImageWorker(){if(imageDecoder)return;imageDecoder=std::make_unique<gpu::NativeNotesImageDecoder>([](const gpu::NotesImageRequest&r){return gpu::NotesImageAccess{std::filesystem::u8path(r.path),r.accessLease};},gpu::NotesImageRoute{hwnd,NotesPreview::mediaNoticeMessage,mediaGeneration});imagePlayback=std::make_unique<gpu::NativeNotesImagePlayback>(*imageDecoder);workspace->connectImagePlayback(*imagePlayback);}
    bool setMediaError(std::optional<std::string> value){if(mediaError==value)return false;auto input=controlInput(mod::NotesControlsKind::center);input.error=value;
        // Original NotesCanvas uses NSColor.systemOrange. This dark appearance
        // sRGB value was read from AppKit, rather than a substituted warning tint.
        input.systemOrange=mod::NotesColor{1,159./255.,10./255.,1};controls.update(input);controlScene->syncContent(assets.imagesFor(controls),1);mediaError=std::move(value);return true;}
    bool beginImport(std::span<const std::string>paths,core::Point point,std::shared_ptr<void>lease={}){if(!mediaActive||!state->notesSelected()||paths.empty())return false;need(std::isfinite(point.x)&&std::isfinite(point.y),"Media import requires a finite insertion point");
    if(paths.size()>10000-state->notes().size()){setMediaError("便笺记录数量已达到上限");return false;}for(const auto&path:paths)if(!data::validWindowsFilePath(path)){setMediaError("媒体路径无效，文件未被添加");return false;}if(!finish())return false;startImageWorker();
    std::vector<Impl::Import>pending;std::vector<gpu::NotesImageRequest>requests;pending.reserve(paths.size());requests.reserve(paths.size());need(importSerial!=std::numeric_limits<std::uint64_t>::max(),"Media import generation exhausted");const auto serial=importSerial+1;for(std::size_t n=0;n<paths.size();++n){const auto key="notes.import."+std::to_string(mediaGeneration)+"."+std::to_string(serial)+"."+std::to_string(n);pending.push_back({key,paths[n],{},E_PENDING,lease});requests.push_back({key,paths[n],serial,64,false,lease});}imageDecoder->setInspections(requests);pendingImports=std::move(pending);importSerial=serial;importPoint=point;setMediaError({});return true;
    }
    void cancelImport(){pendingImports.clear();if(imageDecoder)imageDecoder->setInspections({});}
    bool mediaCompletion(double time){if(!imageDecoder)return false;bool changed=workspace->acceptMedia(mediaGeneration,time);
        for(auto&result:imageDecoder->drainInspections(mediaGeneration)){const auto found=std::find_if(pendingImports.begin(),pendingImports.end(),[&](const auto&v){return v.key==result.key&&result.revision==importSerial;});if(found==pendingImports.end())continue;found->info=std::move(result.info);found->result=result.result;}
        if(pendingImports.empty()||std::any_of(pendingImports.begin(),pendingImports.end(),[](const auto&v){return v.result==E_PENDING;}))return changed;
        if(!mediaActive||!state->notesSelected()){cancelImport();return changed;}
        if(std::any_of(pendingImports.begin(),pendingImports.end(),[](const auto&v){return FAILED(v.result)||!v.info;})){changed|=setMediaError("媒体无法载入，文件未被添加");cancelImport();return changed;}
        auto completed=std::move(pendingImports);pendingImports.clear();imageDecoder->setInspections({});changed|=setMediaError({});
        for(std::size_t n=0;n<completed.size();++n){const auto&v=completed[n];const auto&info=*v.info;const auto filename=std::filesystem::u8path(v.path).filename().u8string();const std::string name(reinterpret_cast<const char*>(filename.data()),filename.size());
            const auto reference=data::makeWindowsMediaReference(v.path,name,static_cast<int>(info.pixelWidth),static_cast<int>(info.pixelHeight),info.kind==mod::NotesMediaKind::gif?"gif":"image",info.kind==mod::NotesMediaKind::gif?std::optional<double>(info.duration):std::nullopt,static_cast<int>(info.frameCount));const auto id=data::makeUUID();const double offset=double(n%4)*12;
            if(workspace->createMedia(id,data::foundationNow(),reference,{importPoint.x+offset,importPoint.y+offset},v.lease)){if(const auto token=workspace->cardToken(id))track(*token,TrackKind::create,time);changed=true;}else changed|=setMediaError(state->error());
        }return changed;
    }
    void track(gpu::NativeNotesCardToken token,TrackKind kind,double time,core::MotionPoint direction={}){
        const auto it=std::find_if(tracks.begin(),tracks.end(),[&](const auto&t){return t.token==token;});Track value{token,kind,time,direction};if(it!=tracks.end())*it=value;else tracks.push_back(value);
    }
    void change(const std::optional<core::ModulePresentationChange>&value,double time){
        if(!value)return;const bool before=value->from==core::Module::notes,after=value->to==core::Module::notes;if(before==after)return;
        if(!after)cancelImport();need(finish(),"Module transition began before the Notes text service unlocked");std::vector<std::pair<std::string,std::optional<gpu::NativeNotesCardToken>>>previous;previous.reserve(state->notes().size());for(const auto&n:state->notes())previous.emplace_back(n.id,workspace->cardToken(n.id));
        workspace->setPresentation(after,value->animated);outgoingGeneration=workspace->presentationGeneration();
        for(const auto&n:state->notes()){const auto token=workspace->cardToken(n.id);if(!token)continue;const auto old=std::find_if(previous.begin(),previous.end(),[&](const auto&p){return p.first==n.id;});
            if(old!=previous.end()&&old->second==token)continue;
            if(old!=previous.end()&&old->second)std::erase_if(tracks,[&](const auto&t){return t.token==*old->second;});
            if(value->animated)track(*token,after?TrackKind::sectionIn:TrackKind::sectionOut,time,core::moduleDirection(value->from,value->to));
        }
        lastConfirmationRect.reset();confirmationVisible=false;
    }
    void focusEditor(){pendingFocus=workspace->focusEditor(focused)==TS_E_NOLOCK;}
    bool finish(){if(mediaMenu)mediaMenu->close(currentTime);if(formatMenu)formatMenu->close(currentTime);if(workspace->editor()&&!workspace->finishEditing().finished)return false;pendingFocus=false;editorDrag=false;controlsPressed=false;workspace->cancelInteraction();workspace->cancelDeletion();lastConfirmationRect.reset();confirmationVisible=false;return true;}
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
        if(mediaMenu&&mediaMenu->pointer(e,metrics.scale,time))return true;
        if(formatMenu&&formatMenu->pointer(e,metrics.scale,time))return true;
        if(e.kind==app::PointerKind::captureLost){editorDrag=false;if(auto*editor=workspace->editor())editor->pointerUp();workspace->endGesture();controlsPressed=false;clearHover(time);return false;}
        if(e.kind==app::PointerKind::leave){clearHover(time);return false;}
        if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool handled=editorDrag||state->dragging()||controlsPressed;editorDrag=false;controlsPressed=false;if(auto*editor=workspace->editor())editor->pointerUp();workspace->endGesture();const auto q=controlsProjection.unproject(p);controlScene->setFeedback(q?controls.actionAt(*q):std::nullopt,false,false,time);return handled;}
        if(!moduleInput){clearHover(time);return false;}
        if(e.kind==app::PointerKind::move){
            if(editorDrag){if(auto*editor=workspace->editor()){editor->pointerDrag(p);editSync();}return true;}
            if(state->dragging()){if(const auto point=workspaceProjection.unproject(p))workspace->dragTo(*point);return true;}
            const bool onConfirm=confirmAt(p,time,false);const auto hit=onConfirm?std::nullopt:workspace->hitTest(p);
            if(hoveredNote&&(!hit||hit->noteID!=*hoveredNote)){workspace->setFeedback(*hoveredNote,{},false,false,time);hoveredNote.reset();}
            if(hit){const auto verb=hit->kind==gpu::NativeNotesWorkspaceHit::Kind::action&&hit->verb!="edit"&&!hit->verb.starts_with("editItem:")?std::optional<std::string_view>{hit->verb}:std::nullopt;
                if(!hoveredNote)hoveredNote=std::string(hit->noteID);workspace->setFeedback(hit->noteID,verb,false,false,time);}
            std::optional<std::string_view>action;if(!onConfirm&&!hit&&moduleVisible&&moduleInput){const auto q=controlsProjection.unproject(p);if(q)action=controls.actionAt(*q);}
            controlScene->setFeedback(action,controlsPressed,false,time);return onConfirm||hit.has_value()||action.has_value();
        }
        if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left)return false;
        if(confirmAt(p,time,true))return true;
        if(const auto hit=workspace->hitTest(p)){
            const std::string id(hit->noteID),verb(hit->verb);const auto basePoint=workspaceProjection.unproject(p);if(!basePoint)return true;const auto point=*basePoint;
            if(hit->kind==gpu::NativeNotesWorkspaceHit::Kind::editor){workspace->editor()->pointerDown(p,(e.modifiers&MK_SHIFT)!=0);editorDrag=true;editSync();return true;}
            if(verb.starts_with("format")&&formatMenu&&workspace->editingNoteID()==std::optional<std::string_view>(id)){
                if(const auto selected=workspace->editor()->selectionStyle()){const auto*n=state->note(id);formatNoteRect={n->x,n->y,n->width,n->height};formatMenu->open(verb,*selected,time);}return true;}
            if(workspace->editor()){if(formatMenu)formatMenu->close(time);if(!workspace->finishEditing().finished)return true;}
            workspace->select(id);
            if(hit->kind==gpu::NativeNotesWorkspaceHit::Kind::resize)workspace->beginGesture(id,point,mod::NotesState::Gesture::resize);
            else if(verb=="pin"){workspace->togglePin(id);if(state->visible(id))if(const auto token=workspace->cardToken(id))track(*token,TrackKind::mutation,time);}
            else if(verb=="mediaPlayback"){workspace->toggleMedia(id,time);}
            else if(verb=="delete"){workspace->requestDeletion(id);confirmationStarted=time;}
            else if(verb=="add"){if(workspace->addChecklistItem(id,data::makeUUID())){if(const auto token=workspace->cardToken(id))track(*token,TrackKind::mutation,time);focusEditor();}}
            else if(verb.starts_with("editItem:")){workspace->beginEditingItem(id,std::string_view(verb).substr(9));focusEditor();}
            else if(verb.starts_with("check:")||verb.starts_with("up:")||verb.starts_with("down:")||verb.starts_with("remove:")){
                const auto colon=verb.find(':');const auto kind=verb.starts_with("check:")?mod::NotesState::ChecklistAction::toggle:verb.starts_with("up:")?mod::NotesState::ChecklistAction::up:verb.starts_with("down:")?mod::NotesState::ChecklistAction::down:mod::NotesState::ChecklistAction::remove;
                if(workspace->mutateChecklistItem(id,std::string_view(verb).substr(colon+1),kind))if(const auto token=workspace->cardToken(id))track(*token,TrackKind::mutation,time);
            }
            else if(verb=="edit"&&e.kind==app::PointerKind::doubleClick){workspace->beginEditing(id);focusEditor();}
            else if(hit->kind==gpu::NativeNotesWorkspaceHit::Kind::body||verb=="edit")workspace->beginGesture(id,point,mod::NotesState::Gesture::move);
            else std::cout<<"Notes formatting requires its prepared menu assets\n";
            return true;
        }
        if(workspace->editor()&&!workspace->finishEditing().finished)return true;
        if(state->pendingDeletion()){workspace->cancelDeletion();confirmationVisible=false;return true;}
        if(moduleVisible&&moduleInput){const auto q=controlsProjection.unproject(p);if(const auto action=q?controls.actionAt(*q):std::nullopt){
            controlScene->setFeedback(action,true,false,time);controlsPressed=true;
            for(std::size_t n=0;n<controls.actions().size();++n)if(controls.actions()[n].id==*action)toolbarStarted[n]=time;
            if(*action=="tool:text"){const auto id=data::makeUUID();workspace->createText(id,data::foundationNow());if(const auto token=workspace->cardToken(id))track(*token,TrackKind::create,time);focusEditor();}
            else if(*action=="tool:todo"){const auto id=data::makeUUID();workspace->createChecklist(id,data::makeUUID(),data::foundationNow());if(const auto token=workspace->cardToken(id))track(*token,TrackKind::create,time);focusEditor();}
            else if(*action=="tool:image"){if(finish()){const auto offset=double(state->notes().size()%7)*18;mediaMenu->openSource({metrics.width*.5-100+offset,metrics.height*.5-120+offset},time);}}
            else std::cout<<"Notes drawing adapter is not yet installed in this isolated preview\n";return true;
        }}workspace->select({});return false;
    }
};
NotesPreview::NotesPreview(HWND h,native::LayerRasterizer&r,const std::filesystem::path&root,const native::NativeNotesControlsAssets&a,bool tsf,const std::filesystem::path&formatRoot):impl_(std::make_unique<Impl>(h,r,root,a,tsf,formatRoot)){}
NotesPreview::~NotesPreview()=default;
ITfThreadMgr*NotesPreview::activatedTextManager()const noexcept{return impl_->manager.manager.Get();}
TfClientId NotesPreview::textClient()const noexcept{return impl_->manager.client;}
std::optional<NotesMediaAction>NotesPreview::takeMediaAction(UINT_PTR generation){auto&i=*impl_;if(generation!=i.mediaGeneration)return{};auto result=std::move(i.mediaAction);i.mediaAction.reset();return result;}
bool NotesPreview::importMedia(std::span<const std::string>paths,core::Point point,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);return i.beginImport(paths,point);}
bool NotesPreview::importMedia(data::ShelfFileAccess access,core::Point point,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);if(!access.open()||access.metadata().isDirectory){i.setMediaError("暂存架媒体无法读取，文件未被添加");return false;}
    auto lease=std::make_shared<data::ShelfFileAccess>(std::move(access));const std::array paths{lease->metadata().windowsPath};return i.beginImport(paths,point,std::move(lease));}

void NotesPreview::presentShelfMedia(std::vector<mod::NotesShelfChoice>choices,core::Point point,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);if(i.mediaActive&&i.moduleInput&&i.state->notesSelected()&&i.finish())i.mediaMenu->openShelf(std::move(choices),point,event.time);}
bool NotesPreview::setMediaActive(bool active,double t,bool preserve){auto&i=*impl_;const Impl::TimeScope event(i,t);i.mediaActive=active;if(!active){i.cancelImport();i.mediaAction.reset();i.mediaMenu->close(event.time,true);}return i.workspace->setMediaActive(active,event.time,preserve);}
std::optional<double>NotesPreview::nextWakeTime()const{return impl_->workspace->mediaNextWakeTime();}
bool NotesPreview::deadline(double t){auto&i=*impl_;const Impl::TimeScope event(i,t);return i.workspace->sampleMedia(event.time);}
const std::optional<std::string>&NotesPreview::mediaError()const noexcept{return impl_->mediaError;}
bool NotesPreview::showMediaError(std::string value,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);return i.setMediaError(std::move(value));}
void NotesPreview::select(core::Module m,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);t=event.time;if(!i.finish()){i.pendingModule=m;return;}i.pendingModule.reset();const auto result=i.modules.select(m,t);i.moduleInput=result.presentation.acceptsModuleInput;i.change(result.change,t);}
core::Module NotesPreview::selected()const noexcept{return impl_->modules.requested();}
const core::ModulePresentationSample&NotesPreview::modulePresentation()const noexcept{return impl_->moduleSample;}
void NotesPreview::resize(const app::ClientMetrics&m){auto&i=*impl_;if(i.metrics==m)return;if(!i.finish()){i.pendingResize=m;return;}i.pendingResize.reset();i.metrics=m;i.workspace->setWorkspaceBounds({0,0,m.width,m.height},core::Point{m.width*.5-100,m.height*.5-120});}
void NotesPreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,float opacity,double t,bool focused){
    auto&i=*impl_;const Impl::TimeScope event(i,t);t=event.time;i.focused=focused;const auto sample=i.modules.sample(t);i.moduleSample=sample.presentation;i.change(sample.change,t);i.animate(t);i.moduleInput=sample.presentation.acceptsModuleInput;
    i.workspace->sampleMedia(t);
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
    if(i.formatMenu){if(const auto id=i.workspace->editingNoteID())if(const auto*n=i.state->note(*id))i.formatNoteRect={n->x,n->y,n->width,n->height};
        i.formatMenu->update(i.workspaceWorld,camera,i.metrics.pixelWidth,i.metrics.pixelHeight,i.state->workspaceBounds(),i.formatNoteRect,opacity,t);}
    if(i.mediaMenu){const auto moduleWorld=i.surface->pose().contentWorld;i.mediaMenu->update(moduleWorld,i.workspaceWorld,camera,i.metrics.pixelWidth,i.metrics.pixelHeight,opacity,t);}
    i.hasPose=true;
}
bool NotesPreview::requiresFrames(double t)const{const auto&i=*impl_;t=i.queryTime(t);if((i.mediaMenu&&i.mediaMenu->requiresFrames(t))||(i.formatMenu&&i.formatMenu->requiresFrames(t))||i.modules.requiresFrames()||!i.tracks.empty()||i.workspace->requiresFrames(t)||i.controlScene->requiresFrames(t)||i.confirmScene->requiresFrames(t))return true;
    for(auto s:i.toolbarStarted)if(s>=0&&t-s<.18)return true;return i.confirmationVisible&&t-i.confirmationStarted<.16;}
bool NotesPreview::diagnosticEditing(bool enabled,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);if(!enabled)return i.finish();if(i.state->notes().empty())return false;i.workspace->beginEditing(i.state->notes().front().id);i.focusEditor();return i.workspace->editor()!=nullptr;}
bool NotesPreview::pointerLocked()const{return impl_->state->dragging();}
bool NotesPreview::covers(core::Point p)const{const auto&i=*impl_;if(!i.hasPose||!i.moduleInput)return false;p={p.x*i.metrics.scale,p.y*i.metrics.scale};
    if(i.mediaMenu&&i.mediaMenu->contains(p))return true;
    if(i.formatMenu&&i.formatMenu->contains(p))return true;
    if(i.confirmationVisible){const auto q=i.confirmationProjection.unproject(p);if(q&&i.confirmation.actionAt(*q))return true;}
    if(i.workspace->hitTest(p))return true;if(i.moduleVisible){const auto q=i.controlsProjection.unproject(p);if(q&&i.controls.actionAt(*q))return true;}return false;
}
bool NotesPreview::pointer(const app::PointerEvent&e,double t){return impl_->pointer(e,t);}
bool NotesPreview::wheel(const app::WheelEvent&e,double t){
    auto&i=*impl_;const Impl::TimeScope event(i,t);
    if(i.mediaMenu&&i.mediaMenu->wheel(e,i.metrics.scale,event.time))return true;
    if(i.formatMenu&&i.formatMenu->wheel(e,i.metrics.scale,event.time))return true;
    if(!covers({e.x,e.y}))return false;
    // Consume wheel input on Notes controls/menus and at scroll limits so it
    // cannot reach the navigation behind them. Native wheel units remain a
    // platform preference; the workspace inverse preserves fractional motion.
    if(e.horizontal||!std::isfinite(e.steps)||e.steps==0||e.linesPerStep==0)return true;
    const core::Point point{e.x*i.metrics.scale,e.y*i.metrics.scale};
    if(i.confirmationVisible){const auto q=i.confirmationProjection.unproject(point);if(q&&i.confirmation.actionAt(*q))return true;}
    const auto hit=i.workspace->hitTest(point);if(!hit)return true;
    const auto*card=i.workspace->card(hit->noteID);if(!card)return true;
    const double distance=e.linesPerStep==UINT32_MAX?card->contentViewport().height:12.*e.linesPerStep;
    i.workspace->scrollAt(point,-e.steps*distance*i.metrics.scale);
    return true;
}
bool NotesPreview::filterKey(const app::NativeMessage&m){if((impl_->mediaMenu&&impl_->mediaMenu->acceptsInput())||(impl_->formatMenu&&impl_->formatMenu->acceptsInput()))return false;auto*e=impl_->workspace->editor();return e&&e->filterKeyMessage(m.message,m.wParam,m.lParam);}
bool NotesPreview::message(const app::NativeMessage&m,std::optional<double>time){auto&i=*impl_;const Impl::TimeScope event(i,time.value_or(i.currentTime));if(m.message==mediaNoticeMessage){if(m.wParam!=i.mediaGeneration)return false;return i.mediaCompletion(event.time);}if(m.message!=WM_APP+181)return false;if(i.workspace->takeEditorChanges(m.wParam))i.editSync();
    // Selection/layout notifications are not focus transitions: refocusing
    // clears the field's active drag and any pending UTF-16 high surrogate.
    // Retry only an explicit field/window focus request held by a TSF lock.
    if(i.pendingCancel&&i.finish())i.pendingCancel=false;if(i.pendingResize){const auto value=*i.pendingResize;resize(value);}if(i.pendingModule){const auto value=*i.pendingModule;select(value,i.currentTime);}if(i.pendingFocus)i.focusEditor();return true;}
bool NotesPreview::key(const app::KeyEvent&e,double t){auto&i=*impl_;const Impl::TimeScope event(i,t);if(i.mediaMenu&&i.mediaMenu->key(e,event.time))return true;if(i.formatMenu&&i.formatMenu->key(e,event.time))return true;auto*editor=i.workspace->editor();if(!editor||!i.moduleInput)return false;gpu::ProjectedEditorResult result;
    // Original TODO fields finish on unconsumed Return; imported/pasted line
    // breaks still remain valid data. TSF receives keys before this owner.
    if(i.workspace->editingItemID()&&((e.kind==app::KeyKind::down&&e.value==VK_RETURN)||((e.kind==app::KeyKind::character||e.kind==app::KeyKind::unicodeCharacter)&&(e.value=='\r'||e.value=='\n')))){
        if(i.workspace->editorDocument()->composition())return true;if(!i.workspace->finishEditing().finished)i.pendingCancel=true;return true;
    }
    if(e.kind==app::KeyKind::character||e.kind==app::KeyKind::unicodeCharacter)result=editor->character(e.value,e.kind==app::KeyKind::unicodeCharacter);
    else if(e.kind==app::KeyKind::down&&GetKeyState(VK_CONTROL)<0&&(e.value=='Z'||e.value=='Y'))result=e.value=='Y'||GetKeyState(VK_SHIFT)<0?editor->redo():editor->undo();
    else if(e.kind==app::KeyKind::down){std::optional<gpu::ProjectedEditorCommand>command;using C=gpu::ProjectedEditorCommand;switch(e.value){case VK_LEFT:command=C::left;break;case VK_RIGHT:command=C::right;break;case VK_UP:command=C::up;break;case VK_DOWN:command=C::down;break;case VK_HOME:command=C::documentStart;break;case VK_END:command=C::documentEnd;break;case VK_BACK:command=C::backspace;break;case VK_DELETE:command=C::deleteForward;break;case VK_ESCAPE:command=C::finish;break;case 'A':if(GetKeyState(VK_CONTROL)<0)command=C::selectAll;break;}if(command)result=editor->command(*command,GetKeyState(VK_SHIFT)<0);}
    if(result.finishRequested){if(i.formatMenu)i.formatMenu->close(event.time);i.workspace->finishEditing();return true;}if(result.handled)i.editSync();return result.handled;
}
void NotesPreview::focus(bool f){impl_->focused=f;impl_->focusEditor();if(!f){impl_->editorDrag=false;if(!impl_->finish())impl_->pendingCancel=true;}}
bool NotesPreview::finish(){return impl_->finish();}
std::uint64_t NotesPreview::compositionRevision()const noexcept{return impl_->workspace->compositionRevision();}
void NotesPreview::upload(native::Renderer&r){auto&i=*impl_;i.workspace->uploadMedia(r);if(i.mediaMenu)i.mediaMenu->upload(r);if(i.formatMenu)i.formatMenu->upload(r);const auto revision=i.registration.stats().geometryRevision;if(i.uploadedRegistration!=revision){i.registration.uploadGeometry(r);i.uploadedRegistration=revision;}if(!i.groupUploaded){i.confirmationGroup->uploadResources(r);i.groupUploaded=true;}else i.confirmationGroup->updateLocal(r);i.confirmationGroup->setPose(i.confirmWorld,i.confirmationOpacity);}
std::span<const native::LayerCompositionEntry>NotesPreview::entries(){auto&i=*impl_;i.composed.clear();i.composed.push_back({&i.controlScene->scene(),i.registration.draws()});for(const auto&e:i.workspace->entries())i.composed.push_back(e);i.composed.push_back(i.confirmationGroup->entry());if(i.formatMenu)for(const auto&e:i.formatMenu->entries())i.composed.push_back(e);if(i.mediaMenu)for(const auto&e:i.mediaMenu->entries())i.composed.push_back(e);return i.composed;}
void NotesPreview::collected(native::Renderer&r){if(impl_->mediaMenu)impl_->mediaMenu->collected(r);if(impl_->formatMenu)impl_->formatMenu->collected(r);need(impl_->workspace->collectRetired(r),"Detached Notes assets must retire after publication");}
void NotesPreview::release(native::Renderer&r){auto&i=*impl_;if(i.mediaMenu)i.mediaMenu->release(r);if(i.formatMenu)i.formatMenu->release(r);need(i.workspace->releaseResources(r),"Notes workspace remains published during teardown");need(i.registration.releaseResources(r),"Notes registration remains published during teardown");need(i.controlScene->scene().releaseResources(r)&&i.confirmationGroup->releaseResources(r),"Notes controls remain published during teardown");}
}
