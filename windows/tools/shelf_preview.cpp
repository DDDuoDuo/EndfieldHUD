#include "tools/shelf_preview.hpp"
#include "native/shelf_icon_provider.hpp"
#include "native/shelf_file_picker.hpp"
#include "native/shelf_file_preview.hpp"
#include "native/file_shelf_files.hpp"
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <sstream>
#include <iomanip>
#include <utility>

namespace endfield::tools {
namespace {
namespace gpu=native;namespace mod=modules;namespace data=ehud::data;
using Matrix=core::Matrix4;using Json=data::Json;
void need(bool value,const char*why){if(!value)throw std::runtime_error(why);}
bool contains(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
Json emptyPlane(){return Json::Object{{"bounds",Json::Array{0,0,400,334}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
std::string byteLabel(std::int64_t bytes){
    // Decimal file sizes, matching the source formatter's file-count style.
    constexpr std::array units{"bytes","KB","MB","GB","TB","PB","EB"};
    double count=double(bytes);std::size_t unit{};while(count>=1000&&unit+1<units.size()){count/=1000;++unit;}
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(unit&&count<10?1:0)<<count<<' '<<units[unit];return out.str();
}
std::vector<mod::FileShelfItem> snapshot(const data::FileShelfStore&store){
    std::vector<mod::FileShelfItem> result;result.reserve(store.items().size());
    for(const auto&i:store.items())result.push_back({i.id,i.name,i.windowsPath,i.typeDescription,i.byteCount,i.isDirectory,i.availabilityError});return result;
}
mod::ShelfPresentationStyle style(){mod::ShelfPresentationStyle s;s.accent={250./255,212./255,31./255,1};s.heading="文件暂存架";s.emptyTitle="将文件或文件夹拖到这里";s.emptyHelp="仅保存文件引用，需要时随时拖出。";s.clearQuestion="仅清空文件引用？";s.unavailable="不可用";s.errorColor=mod::ShelfColor{1,.27,.23,1};return s;}
constexpr UINT iconMessage=WM_APP+187;
constexpr UINT pickerMessage=WM_APP+188;
constexpr UINT previewMessage=WM_APP+189;
std::atomic<UINT_PTR> nextGeneration{1};
}
struct ShelfPreview::Impl {
    struct DropState {HWND hwnd{};core::Projection projection;double scale{1};bool enabled{};};
    struct Selection {std::string id;double fromY{},fromZ{},toY{},toZ{},start{};};
    HWND hwnd;gpu::LayerRasterizer&raster;const gpu::NativeShelfAssets&assets;ShelfPreviewOptions options;
    gpu::NativeFileShelfFiles files{{"文件","文件夹"}};
    std::shared_ptr<data::FileShelfStore>store;std::unique_ptr<mod::FileShelfState>state;
    mod::ShelfPresentation presentation;mod::ShelfPresentationStyle appearance=style();
    gpu::LayerImageSource memoryImages;std::shared_ptr<const gpu::LayerMemoryImage>emptyIcon;
    std::unique_ptr<gpu::NativeShelfIconProvider>icons;std::unique_ptr<gpu::NativeShelfScene>scene;
    gpu::LayerScene geometry;std::unique_ptr<gpu::NativeModuleSurface>surface;
    gpu::NativeModuleRegistration registration{"shelf.registration"};
    std::shared_ptr<DropState>dropState=std::make_shared<DropState>();std::unique_ptr<gpu::ShelfDropTarget>drop;
    std::unique_ptr<gpu::NativeShelfFilePicker>picker;
    std::unique_ptr<gpu::NativeShelfFilePreview>preview;
    std::vector<gpu::NativeShelfImage>images;std::vector<gpu::ShelfIconRequest>requests;
    std::vector<Selection>selectionTracks;std::vector<gpu::ShelfSelectionPose>selectionPoses;
    std::map<std::string,gpu::ShelfIconRequest,std::less<>>requestVersions;
    std::vector<gpu::LayerCompositionEntry>composed;std::optional<ShelfPreviewAction>action;
    std::optional<std::pair<std::string,core::Point>>dragCandidate;
    app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;
    bool active{},acceptsInput{},pressed{},nativeDrag{},dirty{true},released{};
    std::uint64_t imageRevision{},iconRevision{};UINT_PTR generation{nextGeneration++};
    double time{},toolbarStart{-1},dropStart{-1},revealStart{-1},revealDirection{1};
    unsigned eventDepth{};
    struct Event {
        Impl&i;Event(Impl&v,double requested):i(v){need(std::isfinite(requested),"Shelf owner requires finite time");if(!i.eventDepth)i.time=std::max(i.time,requested);++i.eventDepth;}~Event(){--i.eventDepth;}
    };
    Impl(HWND h,gpu::LayerRasterizer&r,const gpu::NativeShelfAssets&a,ShelfPreviewOptions o):hwnd(h),raster(r),assets(a),options(std::move(o)),geometry(r){
        need(options.newDataRoot.is_absolute()&&!std::filesystem::exists(options.newDataRoot),"Shelf preview requires a NEW absolute data root");
        data::detail::validateRoot(options.newDataRoot);need(std::filesystem::create_directory(options.newDataRoot),"Create isolated Shelf fixture data");
        store=std::make_shared<data::FileShelfStore>(options.newDataRoot,files.platform());
        mod::FileShelfState::Store persistence{[s=store]{return snapshot(*s);},[s=store]{s->refresh();},[s=store](auto paths){return s->add(paths);},[s=store](auto id){s->remove(id);},[s=store]{s->clear();}};
        mod::FileShelfState::PlatformActions actions;
        actions.chooseFiles=[this]{request({ShelfPreviewAction::Kind::choose,{}});};
        actions.preview=[this](auto id){request({ShelfPreviewAction::Kind::preview,std::string(id)});};
        actions.reveal=[this](auto id){request({ShelfPreviewAction::Kind::reveal,std::string(id)});};actions.formatFileSize=byteLabel;
        auto strings=mod::FileShelfStrings::simplifiedChinese();strings.revealPrefix="在资源管理器中显示：";
        state=std::make_unique<mod::FileShelfState>(snapshot(*store),std::move(persistence),std::move(actions),std::move(strings));
        const std::array<std::uint8_t,4> transparent{};emptyIcon=memoryImages.publish("shelf.pending-icon",1,1,1,transparent);
        gpu::LayerRasterOptions ro;ro.pixelsPerPoint=2;ro.assetRoot=assets.root();ro.memoryImages=&memoryImages;
        scene=std::make_unique<gpu::NativeShelfScene>(presentation,raster,ro,options.revealSamples);
        geometry.load(emptyPlane(),ro);surface=std::make_unique<gpu::NativeModuleSurface>(geometry,core::Module::fileShelf);
        if(options.nativeIcons)icons=std::make_unique<gpu::NativeShelfIconProvider>(memoryImages,files.platform().resolve,gpu::ShelfIconRoute{hwnd,iconMessage,generation});
        picker=std::make_unique<gpu::NativeShelfFilePicker>(gpu::ShelfPickerRoute{hwnd,pickerMessage,generation},gpu::ShelfPickerLabels{"添加到文件暂存架","添加引用","添加选中的文件与文件夹"});
        preview=std::make_unique<gpu::NativeShelfFilePreview>(gpu::ShelfPreviewRoute{hwnd,previewMessage,generation});
        dropState->hwnd=hwnd;
        if(options.registerDropTarget){
            auto shared=dropState;auto retainedStore=store;
            drop=std::make_unique<gpu::ShelfDropTarget>(gpu::ShelfTransferRoute{hwnd,noticeMessage,generation},gpu::ShelfDropTarget::Callbacks{
                [shared](POINTL p){if(!shared->enabled)return false;POINT point{p.x,p.y};if(!ScreenToClient(shared->hwnd,&point))return false;const auto local=shared->projection.unproject({double(point.x),double(point.y)});return local&&contains(mod::FileShelfState::bounds(),*local);},
                [retainedStore](auto paths){retainedStore->add(paths);return true;}});
            need(SUCCEEDED(drop->registerTarget()),"Register Windows Shelf file drop target");drop->setEnabled(false);
        }
        images.reserve(10);requests.reserve(8);selectionTracks.reserve(16);selectionPoses.reserve(8);composed.reserve(12);
    }
    ~Impl(){dropState->enabled=false;if(drop)drop->stop();if(icons)icons->stop();}
    void request(ShelfPreviewAction value){if(action||nativeDrag)return;action=std::move(value);need(PostMessageW(hwnd,actionMessage,generation,0)!=FALSE,"Queue Shelf user action");}
    const data::ShelfRecord*record(std::string_view id)const{const auto&items=store->items();const auto found=std::find_if(items.begin(),items.end(),[&](const auto&i){return i.id==id;});return found==items.end()?nullptr:&*found;}
    std::optional<std::string_view>hitAction(core::Point point)const{
        // Nested card controls win over the containing selection plate.
        const auto actions=presentation.actions();for(auto it=actions.rbegin();it!=actions.rend();++it)if(contains(it->rect,point))return it->id;return {};
    }
    static double progress(double t,double start,double duration){return core::CubicTiming{.16,.78,.25,1}.value(std::clamp((t-start)/duration,0.,1.));}
    std::pair<double,double> selectedPose(std::string_view id,bool target)const{
        const auto found=std::find_if(selectionTracks.begin(),selectionTracks.end(),[&](const auto&s){return s.id==id;});
        if(found==selectionTracks.end())return {target?-1.5:0,target?5.:0};const double p=progress(time,found->start,.18);return {found->fromY+(found->toY-found->fromY)*p,found->fromZ+(found->toZ-found->fromZ)*p};
    }
    void changed(){
        dirty=true;
        for(const auto&e:state->takeEvents())switch(e.kind){
            case mod::FileShelfState::EventKind::settle:selectionTracks.clear();toolbarStart=dropStart=revealStart=-1;break;
            case mod::FileShelfState::EventKind::selection:
                for(const auto&id:e.values){const auto card=state->card(id);if(!card)continue;auto found=std::find_if(selectionTracks.begin(),selectionTracks.end(),[&](const auto&s){return s.id==id;});
                    const auto before=found==selectionTracks.end()?std::pair{card->selected?0.:-1.5,card->selected?0.:5.}:selectedPose(id,card->selected);
                    Selection track{id,before.first,before.second,card->selectionY,card->selectionZ,e.animated?time:time-.18};if(found!=selectionTracks.end())*found=std::move(track);else selectionTracks.push_back(std::move(track));}break;
            case mod::FileShelfState::EventKind::toolbar:toolbarStart=e.animated?time:-1;break;
            case mod::FileShelfState::EventKind::dropTrace:dropStart=e.animated?time:-1;break;
            case mod::FileShelfState::EventKind::collectionReveal:revealStart=e.animated?time:-1;revealDirection=e.direction;break;
            default:break;
        }
    }
    void content(){
        if(!dirty)return;presentation.update(*state,appearance);requests.clear();images.clear();
        for(const auto&d:presentation.chromeImages())images.push_back(assets.image(d));
        for(const auto&card:presentation.cards())for(const auto&d:card.images){
            const auto*file=record(d.itemID);need(file!=nullptr,"Shelf descriptor has no committed reference");
            gpu::ShelfIconRequest req{"shelf.icon."+file->id,0,file->id,file->windowsPath,file->identity,file->isDirectory,bool(file->availabilityError)};
            const auto old=requestVersions.find(file->id);if(old!=requestVersions.end()){req.revision=old->second.revision;if(req!=old->second)req.revision=++iconRevision;}else req.revision=++iconRevision;
            requestVersions[file->id]=req;requests.push_back(req);auto pixels=memoryImages.acquire(req.imageKey,req.revision);if(!pixels)pixels=emptyIcon;
            images.push_back({d,Json::Object{{"memoryImage",pixels->key()},{"revision",std::int64_t(pixels->revision())}}});
        }
        std::erase_if(requestVersions,[&](const auto&item){return !record(item.first);});
        if(icons&&active)icons->setVisible(requests);
        scene->syncContent(images,++imageRevision);dirty=false;
    }
    std::optional<core::Point>local(core::Point logical)const{return acceptsInput?projection.unproject({logical.x*metrics.scale,logical.y*metrics.scale}):std::nullopt;}
};
ShelfPreview::ShelfPreview(HWND h,native::LayerRasterizer&r,const native::NativeShelfAssets&a,ShelfPreviewOptions o):impl_(std::make_unique<Impl>(h,r,a,std::move(o))){}
ShelfPreview::~ShelfPreview()=default;
void ShelfPreview::resize(const app::ClientMetrics&m){impl_->metrics=m;}
void ShelfPreview::update(const Matrix&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){
    auto&i=*impl_;const Impl::Event event(i,t);t=i.time;
    const auto*shown=sample.current.module==core::Module::fileShelf?&sample.current:sample.incoming&&sample.incoming->module==core::Module::fileShelf?&*sample.incoming:nullptr;
    const bool active=shown&&sample.requested==core::Module::fileShelf;
    if(active!=i.active){i.active=active;if(active)i.state->activate();else{i.state->deactivate();i.dragCandidate.reset();i.pressed=false;if(i.icons)i.icons->hide();}i.changed();}
    i.acceptsInput=active&&sample.acceptsModuleInput&&!i.nativeDrag;i.dropState->enabled=i.acceptsInput;if(i.drop)i.drop->setEnabled(i.acceptsInput);
    if(!shown){i.pose.reset();i.registration.update({});return;}
    i.content();i.surface->update(center,settings,*shown,opacity);i.pose=i.surface->pose();const auto&p=*i.pose;
    const auto camera=gpu::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight)*Matrix::scale(i.metrics.scale,i.metrics.scale);
    i.projection=core::Projection::viewport(camera*p.contentWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);i.dropState->projection=i.projection;
    i.selectionPoses.clear();for(const auto&card:i.presentation.cards()){const auto s=i.selectedPose(card.itemID,i.state->selectedIDs().contains(card.itemID));i.selectionPoses.push_back({card.itemID,s.first,s.second});}
    std::erase_if(i.selectionTracks,[&](const auto&s){return t-s.start>=.18||!i.state->card(s.id);});
    gpu::NativeShelfPose pose;pose.contentWorld=p.contentWorld;pose.opacity=p.opacity;pose.time=t;pose.selections=i.selectionPoses;pose.ownerMasks=std::span(&p.hostClip,1);pose.moduleShutter=p.shutter;
    if(i.toolbarStart>=0){const double v=1-Impl::progress(t,i.toolbarStart,.18);pose.toolbarY=6*v;pose.toolbarZ=-6*v;}
    if(i.dropStart>=0)pose.dropStrokeEnd=core::CubicTiming{0,0,.58,1}.value(std::clamp((t-i.dropStart)/.20,0.,1.));
    if(i.revealStart>=0&&t-i.revealStart<.26&&i.options.revealSamples)pose.reveal= gpu::ShelfRevealSample{i.revealDirection,t-i.revealStart};
    i.scene->updatePose(pose);i.registration.update(p.registration);
}
bool ShelfPreview::requiresFrames(double t)const{const auto&i=*impl_;if(!i.pose)return false;t=std::max(t,i.time);return i.scene->requiresFrames(t)||!i.selectionTracks.empty()||(i.toolbarStart>=0&&t-i.toolbarStart<.18)||(i.dropStart>=0&&t-i.dropStart<.20)||(i.revealStart>=0&&t-i.revealStart<.26);}
bool ShelfPreview::covers(core::Point p)const{const auto q=impl_->local(p);return q&&contains(mod::FileShelfState::bounds(),*q);}
bool ShelfPreview::pointer(const app::PointerEvent&e,double t){
    auto&i=*impl_;const Impl::Event event(i,t);const auto q=i.local({e.x,e.y});
    if(e.kind==app::PointerKind::captureLost||e.kind==app::PointerKind::leave){i.dragCandidate.reset();i.pressed=false;if(i.pose)i.scene->setFeedback({},false,false,i.time);return false;}
    if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){const bool owned=i.pressed||i.dragCandidate.has_value();i.dragCandidate.reset();i.pressed=false;if(owned){i.state->finishPointerSelection();i.changed();}if(i.pose)i.scene->setFeedback(q?i.hitAction(*q):std::nullopt,false,false,i.time);return owned;}
    if(!i.acceptsInput)return false;
    if(e.kind==app::PointerKind::move){if(i.dragCandidate&&q&&std::hypot(q->x-i.dragCandidate->second.x,q->y-i.dragCandidate->second.y)>=5){const auto id=i.dragCandidate->first;i.dragCandidate.reset();i.state->beginSelectionDrag();i.request({ShelfPreviewAction::Kind::drag,id});}
        if(i.pose)i.scene->setFeedback(q?i.hitAction(*q):std::nullopt,i.pressed,false,i.time);return q&&contains(mod::FileShelfState::bounds(),*q);}
    if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left||!q||!contains(mod::FileShelfState::bounds(),*q))return false;
    i.pressed=true;if(e.kind==app::PointerKind::down)if(const auto id=i.state->itemAt(*q))i.dragCandidate=std::pair{std::string(*id),*q};
    i.state->mouseDown(*q,e.kind==app::PointerKind::doubleClick?2:1,{bool(e.modifiers&MK_SHIFT),bool(e.modifiers&MK_CONTROL)});i.changed();return true;
}
bool ShelfPreview::wheel(const app::WheelEvent&e,double t){auto&i=*impl_;const Impl::Event event(i,t);const auto q=i.local({e.x,e.y});if(!q||!contains(mod::FileShelfState::bounds(),*q))return false;if(e.horizontal||!std::isfinite(e.steps)||!e.linesPerStep)return true;const double travel=e.linesPerStep==UINT32_MAX?208:12.*e.linesPerStep;i.state->scroll(*q,-e.steps*travel);i.changed();return true;}
bool ShelfPreview::key(const app::KeyEvent&e,double t){auto&i=*impl_;const Impl::Event event(i,t);if(!i.acceptsInput||e.kind!=app::KeyKind::down)return false;const bool shift=GetKeyState(VK_SHIFT)<0,control=GetKeyState(VK_CONTROL)<0;
    switch(e.value){case VK_LEFT:i.state->selectNext(-1,shift);break;case VK_RIGHT:i.state->selectNext(1,shift);break;case VK_UP:i.state->selectNext(-2,shift);break;case VK_DOWN:i.state->selectNext(2,shift);break;
    case VK_PRIOR:i.state->scrollBy(-208);break;case VK_NEXT:i.state->scrollBy(208);break;case VK_HOME:i.state->scrollBy(-i.state->maximumOffset());break;case VK_END:i.state->scrollBy(i.state->maximumOffset());break;
    case VK_DELETE:case VK_BACK:i.state->deleteSelection();break;case VK_SPACE:i.state->previewSelection();break;
    case VK_ESCAPE:if(!i.state->confirmingClear())return false;i.state->perform("shelf:cancelClear");break;
    case 'V':if(!control)return false;i.request({ShelfPreviewAction::Kind::paste,{}});break;
    case 'R':if(!control)return false;i.state->revealSelection();break;default:return false;}i.changed();return true;
}
bool ShelfPreview::message(const app::NativeMessage&m,double t){auto&i=*impl_;if(m.wParam!=i.generation)return false;
    // The native modal pump must not borrow this owner's event instant or state
    // mutation guard. Nested animation/input callbacks get their current times.
    if(m.message==pickerMessage){i.picker->handleMessage(m.wParam,m.lParam);if(auto result=i.picker->drain(m.wParam)){if(SUCCEEDED(result->result)&&!result->paths.empty())importFiles(result->paths,t);else if(!result->canceled())showError("无法打开文件选择窗口。",t);}return true;}
    if(m.message==previewMessage){i.preview->handleMessage(m.wParam,m.lParam);for(const auto&notice:i.preview->drain(m.wParam)){
        if(notice.kind==gpu::ShelfPreviewEventKind::failed)showError("Windows 未提供此文件的预览程序。",t);
        else if(notice.kind==gpu::ShelfPreviewEventKind::revealRequested)i.request({ShelfPreviewAction::Kind::reveal,notice.itemID});}return true;}
    const Impl::Event event(i,t);
    if(m.message==noticeMessage&&i.drop){auto changes=i.drop->takeChanges(m.wParam);if(changes.committed){i.state->refreshFromStore();std::vector<std::string>ids;for(const auto&file:i.store->items())if(std::find(changes.paths.begin(),changes.paths.end(),file.windowsPath)!=changes.paths.end())ids.push_back(file.id);i.state->revealItems(ids);i.revealStart=i.time;i.revealDirection=1;}if(changes.feedbackChanged)i.state->setDropTarget(changes.hovering);if(changes.error)i.state->showError(*changes.error);i.changed();return true;}
    if(m.message==iconMessage&&i.icons){if(!i.icons->drain(m.wParam).empty())i.dirty=true;return true;}return false;
}
bool ShelfPreview::pointerLocked()const{return impl_->dragCandidate.has_value()||impl_->nativeDrag;}
std::optional<ShelfPreviewAction>ShelfPreview::takeAction(){return std::exchange(impl_->action,{});}
bool ShelfPreview::importFiles(std::span<const std::string>paths,double t){auto&i=*impl_;const Impl::Event event(i,t);const bool value=i.state->importFiles(paths);i.changed();return value;}
void ShelfPreview::showError(std::string message,double t){auto&i=*impl_;const Impl::Event event(i,t);i.state->showError(std::move(message));i.changed();}
void ShelfPreview::requestFiles(){impl_->picker->request();}
void ShelfPreview::requestPreview(std::string_view id){auto&i=*impl_;i.preview->request(std::string(id),i.store->access(id));}
bool ShelfPreview::filterKey(const app::NativeMessage&m){MSG message{};message.hwnd=static_cast<HWND>(m.window);message.message=m.message;message.wParam=m.wParam;message.lParam=m.lParam;return impl_->preview->preTranslate(message);}
void ShelfPreview::cancelPanels(){impl_->picker->cancel();impl_->preview->close(false);}
void ShelfPreview::cancelInteraction(){auto&i=*impl_;i.dragCandidate.reset();i.pressed=false;i.action.reset();}
void ShelfPreview::setNativeDragActive(bool value){impl_->nativeDrag=value;cancelInteraction();}
data::ShelfFileAccess ShelfPreview::access(std::string_view id){return impl_->store->access(id);}
std::unique_ptr<gpu::ShelfDragTransfer>ShelfPreview::prepareDrag(std::string_view id){auto&i=*impl_;auto ids=i.state->dragSelection(id);return std::make_unique<gpu::ShelfDragTransfer>(i.store->prepareCopy(ids),i.files.platform().resolve);}
const mod::FileShelfState&ShelfPreview::state()const{return *impl_->state;}
void ShelfPreview::upload(gpu::Renderer&r){auto&i=*impl_;if(!i.pose)return;i.registration.uploadGeometry(r);i.scene->uploadAnimations(r);}
std::span<const gpu::LayerCompositionEntry>ShelfPreview::entries(){auto&i=*impl_;i.composed.clear();if(i.pose){for(const auto&e:i.scene->entries())i.composed.push_back(e);i.composed.push_back({&i.geometry,i.registration.draws()});}return i.composed;}
void ShelfPreview::collected(gpu::Renderer&r){need(impl_->scene->collectRetired(r),"Shelf retired artwork must be detached after publication");}
void ShelfPreview::release(gpu::Renderer&r){auto&i=*impl_;cancelPanels();i.dropState->enabled=false;if(i.drop)i.drop->stop();if(i.icons)i.icons->stop();need(i.scene->releaseResources(r)&&i.registration.releaseResources(r)&&i.geometry.releaseResources(r),"Shelf resources remain published during teardown");i.released=true;}
}
