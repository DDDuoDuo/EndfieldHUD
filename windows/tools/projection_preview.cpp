#include "tools/projection_preview.hpp"
#ifdef _WIN32
#include "modules/notes_format_bar.hpp"
#include "modules/projection_settings.hpp"
#include "native/projection_media_scene.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <deque>
#include <stdexcept>
namespace endfield::tools {
std::string_view projectionEventAction(const ProjectionPreviewEvent&event)noexcept{
    switch(event.kind){
    case ProjectionPreviewEvent::Kind::strokeCompleted:return "drawingEdited";
    case ProjectionPreviewEvent::Kind::erased:return "erased";
    case ProjectionPreviewEvent::Kind::brushChanged:return "brushChanged";
    case ProjectionPreviewEvent::Kind::backgroundChanged:return "backgroundChanged";
    case ProjectionPreviewEvent::Kind::cleared:return "cleared";
    case ProjectionPreviewEvent::Kind::mediaAdded:return "mediaAdded";
    case ProjectionPreviewEvent::Kind::mediaRemoved:break;
    }
    return "mediaRemoved";
}
namespace {
namespace n=native;namespace m=modules;namespace d=modules::design;using P=core::Point;using R=core::Rect;using M=core::Matrix4;using J=ehud::data::Json;
void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
bool inside(R r,P p){return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
P local(P p,R r){return {p.x-r.x,p.y-r.y};}
std::optional<std::size_t>index(std::string_view s){std::size_t result{};const auto value=std::from_chars(s.data(),s.data()+s.size(),result);if(value.ec!=std::errc{}||value.ptr!=s.data()+s.size())return {};return result;}
// The chrome's raster density: physical pixels per workspace point, .5 steps 1...4 (R11).
double density(double scale){return std::clamp(std::round(std::max(1.,scale)*2)/2,1.,4.);}
n::DesignImages chromeImages(){return {{std::string(m::notesColorWheelImageKey),m::notesColorWheelArtwork()}};}
}
struct ProjectionPreview::Impl {
    struct Media {std::uint64_t id{};n::ProjectionMediaAppearance appearance;std::unique_ptr<n::NativeProjectionMediaScene>scene;bool dead{};};
    m::ProjectionModel&model;m::ProjectionInteraction&interaction;n::LayerRasterizer&raster;ProjectionPreviewOptions options;
    n::NativeProjectionPlane plane;n::NativeProjectionInk ink;
    // The workspace's own design environment (palette, text and grapheme
    // rules from the HUD's; its own deadlines and Reduce Motion) and chrome.
    d::Environment env;std::unique_ptr<m::ProjectionWorkspaceChrome>chrome;std::unique_ptr<n::NativeDesignScene>chromeScene;
    app::ClientMetrics metrics{};R bounds{};M dpi;double topInset{64};std::optional<P>pointer;bool alive{true},active{},ready{},importing{};double now{},backgroundStart{},backgroundFrom{1},backgroundTarget{1};bool backgroundAnimating{};
    std::uint64_t liveRevision{1},mediaRevision{},preferencesRevision{},generation{1};P importPoint{};std::size_t importCount{};
    std::vector<std::unique_ptr<Media>>media;std::vector<n::LayerCompositionEntry>paint;std::deque<ProjectionPreviewAction>actions;std::vector<ProjectionPreviewEvent>events;
    Impl(m::ProjectionModel&model_,m::ProjectionInteraction&interaction_,n::LayerRasterizer&r,ProjectionPreviewOptions o):model(model_),interaction(interaction_),raster(r),options(std::move(o)),plane(r,options.raster),ink(r,options.raster){
        need(options.design&&options.design->text.measurer,"Projection's chrome needs the HUD design environment");media.reserve(32);paint.reserve(100);events.reserve(8);backgroundFrom=backgroundTarget=model.backgroundEnabled()?1:0;
        chromeScene=std::make_unique<n::NativeDesignScene>(raster,options.raster,"projection.chrome",chromeImages());makeChrome();}
    // Owner time never runs backwards: a time earlier than one already seen
    // (the host's sample taken before a nested callback, a direct media or
    // UI Automation call handed a later one) is the latest time, as the
    // Mac's single CACurrentMediaTime never decreases. Every scene below
    // receives this clamped time. Only non-finite time fails.
    double clock(double t){need(std::isfinite(t),"Projection requires a finite owner clock");now=std::max(now,t);env.now=now;return now;}
    std::string text(const char*en,const char*zh)const{return core::localized(en,zh,options.language);}
    void event(ProjectionPreviewEvent::Kind k,std::optional<m::NotesMediaKind>kind={}){events.push_back({k,kind});}
    void notify(){while(alive&&!events.empty()){const auto e=events.front();events.erase(events.begin());auto callback=options.event;if(callback)callback(e);}}
    void action(ProjectionPreviewAction::Kind k,std::string shelf={},std::uint64_t id=0,double seconds=0){if(k==ProjectionPreviewAction::Kind::chooseLocal||k==ProjectionPreviewAction::Kind::useShelf){if(importing)return;++generation;}actions.push_back({k,generation,id,std::move(shelf),seconds});}
    double background()const{if(!backgroundAnimating)return backgroundTarget;const auto p=std::clamp((now-backgroundStart)/.18,0.,1.);return backgroundFrom+(backgroundTarget-backgroundFrom)*p;}
    // The chrome is rebuilt in the current language (the workspace is made per presentation on the Mac).
    void makeChrome(){
        const auto&source=*options.design;env.palette=source.palette;env.text=source.text;env.characters=source.characters;env.language=options.language;env.reduceMotion=options.reduceMotion;env.recordingMode=false;env.now=now;
        if(chrome)chrome->setActive(false);
        m::ProjectionChromeServices s;
        s.event=[this](m::ProjectionChromeEvent e){event(e==m::ProjectionChromeEvent::brushChanged?ProjectionPreviewEvent::Kind::brushChanged:e==m::ProjectionChromeEvent::backgroundChanged?ProjectionPreviewEvent::Kind::backgroundChanged:ProjectionPreviewEvent::Kind::cleared);};
        s.close=[this]{action(ProjectionPreviewAction::Kind::returnToHUD);};
        s.chooseFinder=[this]{action(ProjectionPreviewAction::Kind::chooseLocal);};
        s.shelfChoices=[this]{return options.shelfChoices?options.shelfChoices():std::vector<m::NotesShelfChoice>{};};
        s.chooseShelf=[this](std::string id){action(ProjectionPreviewAction::Kind::useShelf,std::move(id));};
        s.clearContent=[this]{return clear();};
        s.backgroundChanged=[this]{refreshPreferences();};
        chrome=std::make_unique<m::ProjectionWorkspaceChrome>(env,model,m::ProjectionPenSettings::palette(m::notesFormat::hex(env.palette.yellow())),std::move(s));
        chrome->setActive(active);if(ready)chrome->layout({metrics.width,metrics.height},topInset);chrome->refresh();
    }
    void refreshPreferences(){if(preferencesRevision!=model.preferencesRevision()){const double target=model.backgroundEnabled()?1:0;if(target!=backgroundTarget){backgroundFrom=target?0:1;backgroundTarget=target;backgroundStart=now;backgroundAnimating=active&&!options.reduceMotion;}preferencesRevision=model.preferencesRevision();}chrome->refresh();}
    // clearContent (:329-339): imports and gestures stop, strokes and media
    // go; the chrome clears its message and closes the pop-over.
    bool clear(){const bool pending=importing||interaction.ownsPointer();++generation;importing=false;interaction.cancelGesture();++liveRevision;const bool changed=model.clearContent();return changed||pending;}
    // UI Automation over the chrome's router items, then the media buttons.
    void automationElements(std::vector<n::NotesAutomationElement>&out)const{
        if(!active||!ready)return;const double scale=metrics.scale;
        const auto at=[scale](R origin){return [scale,origin](P p)->std::optional<P>{return P{(origin.x+p.x)*scale,(origin.y+p.y)*scale};};};
        const auto button=[&](std::string id,std::string label,R rect,const auto&map,bool enabled=true,bool selected=false){if(const auto b=n::notesAutomationBounds(rect,map)){n::NotesAutomationElement e;e.id=std::move(id);e.label=std::move(label);e.rect=*b;e.enabled=enabled;e.selected=selected;out.push_back(std::move(e));}};
        std::vector<d::AccessibilityItem>items;chrome->accessibilityItems(items);const auto map=at({0,0,0,0});
        const auto element=[&](const d::AccessibilityItem&item,std::string prefix){const auto b=n::notesAutomationBounds(item.rect,map);if(!b)return;n::NotesAutomationElement e;e.id=std::move(prefix)+item.id;e.label=item.label;e.help=item.help;e.rect=*b;e.enabled=item.enabled;e.selected=item.on;
            if(item.role==d::AccessibilityRole::slider){e.role=n::NotesAutomationRole::slider;e.value=item.number;e.minimum=item.minimum;e.maximum=item.maximum;e.smallChange=e.largeChange=item.step;}out.push_back(std::move(e));};
        // The toolbar's own items (the brush pop-over's slider shares the dropdown's id "brush", as on the Mac: the role tells them apart).
        const auto toolbar=[&](const d::AccessibilityItem&item){const auto*c=chrome->toolbar().control(item.id);return c&&c->accessibilityRole()==std::optional(item.role);};
        for(const auto&item:items)if(toolbar(item))element(item,"projection.toolbar:");
        for(const auto&item:model.media()){const auto g=m::projectionMediaGeometry({item.frame.width,item.frame.height},item.reference->kind());const auto card=at(item.frame);const auto id="projection.media:"+std::to_string(item.id);
            if(g.moving){const auto*v=find(item.id);button(id+":play",v&&v->appearance.playing?text("Pause","暂停"):text("Play","播放"),g.play,card);}
            button(id+":remove",text("Remove media","移除媒体"),g.close,card);}
        for(const auto&item:items)if(!toolbar(item))element(item,"projection.menu:");
    }
    bool automationPerform(std::string_view id){
        if(!active||!ready)return false;
        if(id.starts_with("projection.toolbar:")){if(!chrome->performControl(id.substr(19)))return false;}
        else if(id.starts_with("projection.menu:")){if(!chrome->performControl(id.substr(16)))return false;}
        else if(id.starts_with("projection.media:")){const auto rest=id.substr(17);const auto colon=rest.find(':');if(colon==std::string_view::npos)return false;const auto number=index(rest.substr(0,colon));const auto verb=rest.substr(colon+1);
            const auto*item=number?model.find(*number):nullptr;if(!item)return false;m::ProjectionInputResult r;r.handled=true;
            if(verb=="remove"){r.changed=model.removeMedia(*number);r.mediaRemoved=r.changed;}else if(verb=="play"&&item->reference->kind()!=m::NotesMediaKind::image)r.togglePlayback=*number;else return false;result(r);}
        else return false;
        update();notify();return true;
    }
    bool automationValue(std::string_view id,double value){
        if(!active||!ready||!id.starts_with("projection.menu:")||!std::isfinite(value))return false;
        if(!chrome->adjust(id.substr(16),value))return false;update();notify();return true;
    }
    void mediaFeedback(P point,bool pressed){for(auto&v:media)if(!v->dead){const auto*item=model.find(v->id);if(!item)continue;const auto g=m::projectionMediaGeometry({item->frame.width,item->frame.height},item->reference->kind());const auto q=local(point,item->frame);v->scene->setFeedback(inside(g.close,q)?std::optional<std::string_view>("close"):g.moving&&inside(g.play,q)?std::optional<std::string_view>("play"):std::nullopt,pressed,options.reduceMotion,now);}}
    void result(const m::ProjectionInputResult&r){if(r.changed)++liveRevision;if(r.closeMenu)chrome->closePopup();if(r.returnToHUD)action(ProjectionPreviewAction::Kind::returnToHUD);if(r.togglePlayback)action(ProjectionPreviewAction::Kind::togglePlayback,{},*r.togglePlayback);if(r.seek)action(ProjectionPreviewAction::Kind::seek,{},r.seek->id,r.seek->seconds);if(r.strokeCompleted)event(ProjectionPreviewEvent::Kind::strokeCompleted);if(r.erased)event(ProjectionPreviewEvent::Kind::erased);if(r.brushChanged)event(ProjectionPreviewEvent::Kind::brushChanged);if(r.mediaRemoved)event(ProjectionPreviewEvent::Kind::mediaRemoved);if(r.drawingLimit)chrome->showMessage(text("This drawing has reached the stroke limit.","此画布已达到笔画数量上限。"));}
    Media*find(std::uint64_t id)const{for(const auto&v:media)if(!v->dead&&v->id==id)return v.get();return nullptr;}
    void syncMedia(){for(auto&v:media)if(!v->dead&&!model.find(v->id))v->dead=true;for(const auto&item:model.media()){auto*v=find(item.id);if(!v){auto next=std::make_unique<Media>();next->id=item.id;next->appearance.accent=options.accent;next->scene=std::make_unique<n::NativeProjectionMediaScene>(raster,options.raster,env.text);v=next.get();media.push_back(std::move(next));}v->appearance.accent=options.accent;v->scene->sync(item,v->appearance);v->scene->setPose(dpi);v->scene->update(now);}mediaRevision=model.mediaRevision();}
    // The ink follows the drawing once per frame (update(double) and
    // upload()): every sample of every pointer event since the previous frame
    // is painted by one region raster, not one raster per event.
    void syncInk(){if(!ready)return;const auto&live=interaction.liveStroke();ink.update(model.drawing().strokes(),model.drawingRevision(),live?&*live:nullptr,liveRevision,{metrics.width,metrics.height},metrics.scale);ink.setPose(dpi,1);}
    void update(){if(!ready)return;env.now=now;(void)chrome->advance(now);refreshPreferences();if(backgroundAnimating&&now>=backgroundStart+.18)backgroundAnimating=false;plane.setBackground(model.darkness(),background());plane.setPose(dpi,1);
        // updateCursor (:582-588): the ring hides over the toolbar and the popup.
        const bool cursorShown=active&&pointer&&inside(bounds,*pointer)&&!chrome->contains(*pointer);
        plane.setCursor(cursorShown?pointer:std::optional<P>{},model.brushWidth(),model.color(),model.erasing());syncMedia();
        const double ppp=density(metrics.scale);env.text.boxScale=ppp;
        (void)chromeScene->syncContent(chrome->root(),env.text,chrome->contentRevision(),ppp,now);
        chromeScene->updatePose({dpi,1,now,{},std::nullopt});
    }
};
ProjectionPreview::ProjectionPreview(m::ProjectionModel&m,m::ProjectionInteraction&i,n::LayerRasterizer&r,ProjectionPreviewOptions o):impl_(std::make_shared<Impl>(m,i,r,std::move(o))){}ProjectionPreview::~ProjectionPreview(){if(impl_){impl_->alive=false;impl_->interaction.setActive(false);}}
void ProjectionPreview::resize(const app::ClientMetrics&metrics,double safe,double visible){auto p=impl_;need(std::isfinite(metrics.scale)&&metrics.scale>0&&metrics.pixelWidth&&metrics.pixelHeight,"Invalid Projection metrics");auto&i=*p;i.bounds={0,0,metrics.width,metrics.height};i.plane.resize({metrics.width,metrics.height},metrics.scale);i.metrics=metrics;i.dpi={};i.dpi.values[0]=i.dpi.values[5]=metrics.scale;i.interaction.resize({metrics.width,metrics.height});for(const auto&item:i.model.media())i.model.setFrame(item.frame,item.id,i.bounds);
    i.topInset=m::ProjectionToolbar::topInset(safe,visible);i.chrome->layout({metrics.width,metrics.height},i.topInset);i.ready=true;++i.liveRevision;i.update();}
void ProjectionPreview::setActive(bool value,double t){auto p=impl_;auto&i=*p;t=i.clock(t);if(i.active==value)return;i.active=value;i.result(i.interaction.setActive(value));i.chrome->setActive(value);if(!value){++i.generation;i.importing=false;i.actions.clear();i.pointer.reset();for(auto&v:i.media)if(!v->dead)v->scene->setFeedback({},false,true,t);i.backgroundAnimating=false;++i.liveRevision;}i.update();i.notify();}bool ProjectionPreview::active()const noexcept{return impl_->active;}
void ProjectionPreview::setPreferences(core::Language l,m::NotesColor accent,bool reduced,double t){auto p=impl_;auto&i=*p;t=i.clock(t);const bool rebuild=i.options.language!=l||i.options.reduceMotion!=reduced;i.options.language=l;i.options.accent=accent;i.options.reduceMotion=reduced;if(rebuild)i.makeChrome();if(reduced)i.backgroundAnimating=false;i.update();}
void ProjectionPreview::update(double t){auto p=impl_;t=p->clock(t);p->update();p->syncInk();}double ProjectionPreview::backgroundOpacity()const{return impl_->background();}
bool ProjectionPreview::requiresFrames(double t)const{const auto&i=*impl_;need(std::isfinite(t),"Projection requires a finite owner clock");t=std::max(t,i.now);if(i.backgroundAnimating&&t<i.backgroundStart+.18)return true;
    if(i.chrome->requiresFrames(t)||i.chrome->nextDeadline()||i.chromeScene->requiresFrames(t))return true;for(const auto&v:i.media)if(!v->dead&&v->scene->requiresFrames(t))return true;return false;}
bool ProjectionPreview::pointer(const app::PointerEvent&e,double t){auto keep=impl_;auto&i=*keep;t=i.clock(t);if(!i.active||!i.ready||!std::isfinite(e.x)||!std::isfinite(e.y))return false;const P point{e.x,e.y};auto&chrome=*i.chrome;
    if(e.kind==app::PointerKind::captureLost){cancelInteraction(t);return true;}
    if(e.kind==app::PointerKind::leave){i.pointer.reset();chrome.pointerMoved(std::nullopt,false);i.mediaFeedback({-1e9,-1e9},false);i.update();return false;}
    i.pointer=point;
    if(e.kind==app::PointerKind::move){
        if(chrome.tracking())chrome.pointerDragged(point);
        else if(i.interaction.ownsPointer()){
            // Every sample the host recovered for this move (a 1000 Hz mouse
            // between two frames), oldest first, then the move's own point.
            for(const auto&h:e.history)if(std::isfinite(h.x)&&std::isfinite(h.y))i.result(i.interaction.drag({h.x,h.y}));
            i.result(i.interaction.drag(point));}
        else{chrome.pointerMoved(point,false);i.mediaFeedback(point,false);}
        i.update();i.notify();return true;}
    if(e.kind==app::PointerKind::up){if(chrome.tracking())chrome.pointerUp(point);else{i.mediaFeedback(point,false);i.result(i.interaction.up());}i.update();i.notify();return true;}
    if(e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)return false;
    // rightMouseDown (:482): the eraser toggles only while no popup is open.
    if(e.button==app::PointerButton::right){if(!chrome.popupOpen())chrome.perform("eraser");i.update();i.notify();return true;}
    if(e.button!=app::PointerButton::left)return false;
    // mouseDown (:495-524): the chrome takes its presses; an open popup closes
    // on any other press, which goes no further; else the plane draws.
    if(chrome.contains(point))chrome.pointerDown(point);
    else if(chrome.popupOpen())chrome.closePopup();
    else{chrome.showMessage("");i.mediaFeedback(point,true);i.result(i.interaction.down(point));}
    i.update();i.notify();return true;
}
// scrollWheel (:483-494): over an open menu it scrolls the menu, over the
// chrome nothing; anywhere else it changes the brush (committed with the next
// press or when the workspace hides: a wheel has no phase end).
bool ProjectionPreview::wheel(const app::WheelEvent&e,double t){auto keep=impl_;auto&i=*keep;t=i.clock(t);if(!i.active||!std::isfinite(e.steps)||!std::isfinite(e.x)||!std::isfinite(e.y))return false;if(e.horizontal)return true;
    if(i.chrome->scroll({e.x,e.y},-e.steps*12)){i.update();return true;}
    const bool precise=std::abs(e.steps-std::round(e.steps))>1e-6;i.result(i.interaction.wheel(e.steps,precise,false));i.update();i.notify();return true;}
// keyDown (:568-572): an open menu takes keys first; Esc closes the popup, else returns.
bool ProjectionPreview::key(const app::KeyEvent&e,double t){auto p=impl_;auto&i=*p;t=i.clock(t);if(!i.active||e.kind!=app::KeyKind::down)return false;d::KeyInput k;k.virtualKey=e.value;k.extended=e.extended;k.repeat=e.previouslyDown;if(!i.chrome->key(k))return false;i.update();i.notify();return true;}
bool ProjectionPreview::pointerLocked()const noexcept{const auto&i=*impl_;return i.interaction.ownsPointer()||i.chrome->tracking();}void ProjectionPreview::cancelInteraction(double t){auto p=impl_;auto&i=*p;t=i.clock(t);i.interaction.cancelGesture();++i.liveRevision;i.chrome->cancelInteraction();i.update();}bool ProjectionPreview::menuOpen()const noexcept{return impl_->chrome->popupOpen();}
bool ProjectionPreview::importBusy()const noexcept{return impl_->importing;}
std::optional<ProjectionPreviewAction>ProjectionPreview::takeAction(){auto&i=*impl_;if(i.actions.empty())return {};auto action=std::move(i.actions.front());i.actions.pop_front();return action;}bool ProjectionPreview::importRequestCurrent(std::uint64_t n)const noexcept{return impl_->active&&n==impl_->generation;}
std::optional<std::uint64_t>ProjectionPreview::prepareImportRequest(){auto&i=*impl_;if(!i.active||i.importing)return {};return ++i.generation;}
bool ProjectionPreview::beginImport(std::uint64_t n,std::size_t count,std::optional<P>point,double t){auto p=impl_;auto&i=*p;t=i.clock(t);if(!importRequestCurrent(n)||i.importing||!count)return false;if(count>m::ProjectionModel::maximumMedia-i.model.media().size()){i.chrome->showMessage(i.text("Projection supports up to 16 media items.","投影最多可添加 16 个媒体。"));i.update();return false;}const auto destination=point.value_or(P{i.metrics.width*.5,i.metrics.height*.5});if(!std::isfinite(destination.x)||!std::isfinite(destination.y))return false;i.importing=true;i.importCount=count;i.importPoint=destination;i.chrome->showMessage(i.text("Loading media…","正在载入媒体…"));i.update();return true;}
bool ProjectionPreview::receiveImport(std::uint64_t n,std::span<const std::shared_ptr<const m::ProjectionMediaReference>>refs,double t){auto p=impl_;auto&i=*p;t=i.clock(t);if(!importRequestCurrent(n)||!i.importing)return false;need(refs.size()==i.importCount&&std::all_of(refs.begin(),refs.end(),[](const auto&r){return bool(r);}),"Projection import must return every validated reference");need(refs.size()<=m::ProjectionModel::maximumMedia-i.model.media().size(),"Projection import no longer fits the source media limit");for(std::size_t k=0;k<refs.size();++k)if(i.model.addMedia(refs[k],{i.importPoint.x+double(k)*12,i.importPoint.y+double(k)*12},i.bounds))i.event(ProjectionPreviewEvent::Kind::mediaAdded,refs[k]->kind());i.importing=false;i.chrome->showMessage("");i.update();i.notify();return true;}
bool ProjectionPreview::receiveImportError(std::uint64_t n,std::optional<std::string>error,double t){auto p=impl_;auto&i=*p;t=i.clock(t);if(!importRequestCurrent(n))return false;i.importing=false;i.chrome->showMessage(error.value_or(std::string{}));i.update();return true;}void ProjectionPreview::showError(std::string error,double t){auto p=impl_;t=p->clock(t);p->chrome->showMessage(std::move(error));p->update();}
bool ProjectionPreview::setMediaDraw(std::uint64_t id,std::optional<n::DrawObject>draw,double t){auto p=impl_;t=p->clock(t);p->syncMedia();auto*media=p->find(id);if(!media)return false;media->scene->setDraw(std::move(draw));media->scene->update(t);return true;}bool ProjectionPreview::setMediaPlayback(std::uint64_t id,bool playing,double seconds,std::optional<std::string>error,double t){auto p=impl_;t=p->clock(t);auto*media=p->find(id);if(!media||!std::isfinite(seconds)||seconds<0)return false;media->appearance.playing=playing;media->appearance.seconds=seconds;media->appearance.error=std::move(error);const auto*item=p->model.find(id);media->scene->sync(*item,media->appearance);media->scene->update(t);return true;}
void ProjectionPreview::automationElements(std::vector<n::NotesAutomationElement>&out)const{impl_->automationElements(out);}
bool ProjectionPreview::perform(std::string_view id,double t){auto p=impl_;t=p->clock(t);return p->automationPerform(id);}
bool ProjectionPreview::setAutomationValue(std::string_view id,double value,double t){auto p=impl_;t=p->clock(t);return p->automationValue(id,value);}
bool ProjectionPreview::performAction(std::string_view action,double t){auto p=impl_;auto&i=*p;t=i.clock(t);if(!i.active||!i.ready)return false;i.chrome->perform(action);i.update();i.notify();return true;}
std::optional<std::string>ProjectionPreview::popupID()const{return impl_->chrome->popupID();}
const std::string&ProjectionPreview::message()const noexcept{return impl_->chrome->message();}
n::ProjectionInkStats ProjectionPreview::inkStats()const noexcept{return impl_->ink.stats();}
void ProjectionPreview::upload(n::Renderer&r){auto p=impl_;auto&i=*p;need(i.ready,"Resize Projection before upload");i.syncInk();i.plane.upload(r);i.ink.upload(r);for(auto&v:i.media)if(!v->dead)v->scene->upload(r);(void)i.chromeScene->uploadAnimations(r);}
std::span<const n::LayerCompositionEntry>ProjectionPreview::entries(){auto&i=*impl_;i.paint.clear();if(!i.ready)return i.paint;i.paint.push_back(i.plane.backgroundEntry());for(const auto&item:i.model.media())if(auto*v=i.find(item.id)){const auto entries=v->scene->entries();i.paint.insert(i.paint.end(),entries.begin(),entries.end());}for(const auto&e:i.ink.entries())i.paint.push_back(e);i.paint.push_back(i.plane.cursorEntry());for(const auto&e:i.chromeScene->entries())i.paint.push_back(e);return i.paint;}
void ProjectionPreview::collected(n::Renderer&r){auto&i=*impl_;i.ink.collectRetired(r);(void)i.chromeScene->collectRetired(r);for(auto it=i.media.begin();it!=i.media.end();)if((*it)->dead&&(*it)->scene->releaseResources(r))it=i.media.erase(it);else ++it;}
void ProjectionPreview::release(n::Renderer&r){auto&i=*impl_;need(i.plane.releaseResources(r),"Projection background remains published");need(i.ink.releaseResources(r),"Projection ink remains published");need(i.chromeScene->releaseResources(r),"Projection chrome remains published");for(auto&v:i.media)need(v->scene->releaseResources(r),"Projection media remains published");i.media.clear();}
}
#endif
