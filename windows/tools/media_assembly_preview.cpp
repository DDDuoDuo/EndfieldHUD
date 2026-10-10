#include "tools/media_assembly_preview.hpp"
#ifdef _WIN32
#include "native/layer_group.hpp"
#include "native/media_assembly_codec.hpp"
#include "native/media_assembly_gpu.hpp"
#include "native/module_registration.hpp"
#include "native/module_scene.hpp"
#include "native/notes_controls_scene.hpp"
#include <algorithm>
#include <functional>
#include <cmath>
#include <stdexcept>

namespace endfield::tools {
namespace {
namespace gpu=native;namespace mod=modules;using M=core::Matrix4;using P=core::Point;using R=core::Rect;using J=ehud::data::Json;
void need(bool b,const char*why){if(!b)throw std::invalid_argument(why);}
bool inside(R r,P p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
J blank(){return J::Object{{"bounds",J::Array{0,0,440,440}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
// Windows equivalent of the source "Choose in Finder" label.
std::string chooseFile(core::Language l){
    switch(l){case core::Language::simplifiedChinese:return "从文件资源管理器选择";case core::Language::traditionalChinese:return "從檔案總管選擇";
    case core::Language::japanese:return "エクスプローラーで選択";case core::Language::korean:return "파일 탐색기에서 선택";default:return "Choose in File Explorer";}
}
mod::NotesControlsStrings menuStrings(core::Language l){
    mod::NotesControlsStrings s;s.chooseFile=chooseFile(l);s.chooseShelf=core::localized("Choose from Shelf","从暂存架选择",l);s.close=core::localized("Close","关闭",l);
    s.mediaOnly=core::localized("Media only","仅显示媒体",l);s.useMedia=core::localized("Use selected media","使用所选媒体",l);s.shelfHeading=core::localized("SHELF · IMAGE/VIDEO","暂存架 · 图片/视频",l);return s;
}
// Retained MediaAssemblyControlMenu leaves with tint/rim feedback tracks.
struct ControlMenu {
    mod::MediaAssemblyControlMenu model;gpu::LayerScene scene;std::unique_ptr<gpu::NativeLayerGroup>group;
    struct Leaf {std::string id,action;M local;float rest{1},from{1},target{1};double start{},duration{};bool tint{},rim{};};
    std::vector<Leaf>leaves;std::vector<gpu::LayerPlacement>placements;bool feedbackDirty{true};
    ControlMenu(gpu::LayerRasterizer&r,mod::MediaAssemblyControlMenu m,const mod::MediaAssemblyAppearance&a,const gpu::LayerRasterOptions&o,std::uint64_t serial):model(std::move(m)),scene(r){
        auto root=mod::mediaAssemblyMenuArtwork(model,a);J::Array children=root["children"].array();leaves.reserve(children.size());
        for(auto&c:children){const auto&p=c["position"].array();Leaf leaf;leaf.id=c["id"].string();leaf.local=M::translation(p[0].number(),p[1].number());c["position"]=J::Array{0,0};
            // Leaf opacity is applied once, through the retained placement.
            leaf.rest=c.contains("opacity")?float(c["opacity"].number()):1.f;c["opacity"]=1;leaf.tint=leaf.id.ends_with("/tint");leaf.rim=leaf.id.ends_with("/rim");
            if(leaf.tint||leaf.rim){constexpr std::string_view prefix="menu.item.";leaf.action=leaf.id.substr(prefix.size(),leaf.id.rfind('/')-prefix.size());}
            leaf.from=leaf.target=leaf.rest;leaves.push_back(std::move(leaf));}
        root["children"]=std::move(children);scene.load(root,o);need(scene.report().unsupported.empty(),"Unsupported Media Assembly menu artwork");placements.resize(leaves.size());
        for(std::size_t n=0;n<leaves.size();++n){const auto index=scene.surfaceIndex(leaves[n].id);need(index&&*index==n,"Media Assembly menu leaves must stay independent");}
        group=std::make_unique<gpu::NativeLayerGroup>(scene,"media.assembly.menu."+std::to_string(serial),o.pixelsPerPoint,gpu::NativeGroupColorSpace::encodedSRGB);
    }
    float value(const Leaf&l,double t)const{if(!l.duration||t>=l.start+l.duration)return l.target;const double k=std::clamp((t-l.start)/l.duration,0.,1.);return float(l.from+(l.target-l.from)*k);}
    bool animating(double t)const{for(const auto&l:leaves)if(l.duration&&t<l.start+l.duration&&l.from!=l.target)return true;return false;}
    void feedback(std::optional<std::string_view>action,bool pressed,bool reduced,double t){
        for(auto&l:leaves){if(!l.tint&&!l.rim)continue;const bool match=action&&*action==l.action;const float target=match?(l.rim?1.f:pressed?1.f:.62f):(l.rim?l.rest:0.f);
            if(l.target!=target){l.from=value(l,t);l.target=target;l.start=t;l.duration=reduced?0:pressed&&match?.06:.14;feedbackDirty=true;}}
    }
    void pose(double t){for(std::size_t n=0;n<leaves.size();++n){const auto&l=leaves[n];placements[n]={n,l.local,(l.tint||l.rim)?value(l,t):l.rest,{}};}scene.setPlacements(placements);scene.prepareDraws();}
};
struct NotesMenu {
    mod::NotesControlsInput input;mod::NotesControls controls;std::unique_ptr<gpu::NativeNotesControlsScene>scene;std::unique_ptr<gpu::NativeLayerGroup>group;double remainder{};bool dirty{true};
};
}
struct MediaAssemblyPreview::Impl:std::enable_shared_from_this<Impl> {
    gpu::LayerRasterizer&raster;app::UtilityExecutor&executor;app::UtilityExecutor::Route route,exportRoute{};std::shared_ptr<mod::MediaAssemblyEngine>engine;MediaAssemblyPreviewOptions options;
    gpu::NativeMediaAssemblyAssets assets;std::unique_ptr<mod::MediaAssemblyController>controller;mod::MediaAssemblyPresentation presentation;std::unique_ptr<gpu::NativeMediaAssemblyScene>scene;
    gpu::LayerScene geometry;gpu::NativeModuleSurface surface;gpu::NativeModuleRegistration registration{"mediaAssembly.registration"};
    app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;std::optional<P>pointerPoint;
    bool alive{true},overlay{true},prepared{},input{},pressed{},dirty{true},animateInline{},seen{};double time{};
    // Bounded preview texture: one live id; closing posters keep theirs until released.
    std::shared_ptr<const mod::MediaAssemblyPreviewImage>shown;std::string texture;std::uint64_t textureSerial{},textureRevision{};bool textureDirty{},meshUploaded{};
    std::vector<std::string>retiredTextures;gpu::Renderer*renderer{};
    // GPU previews: the renderer's media device edits the bounded source into
    // one retained media texture (same id while its size is unchanged).
    std::unique_ptr<gpu::NativeMediaAssemblyGpuProcessor>processor;bool gpuTried{},shadersPending{},shadersFailed{},mediaChecked{},mediaCapable{};std::shared_ptr<gpu::RendererMediaTexture>media;
    std::shared_ptr<const mod::MediaAssemblyBitmap>gpuSource;
    // Played frames: colour edits in place on the same device, configured once
    // per player (any edit releases the player, like the source).
    std::unique_ptr<gpu::NativeMediaAssemblyGpuProcessor>playbackProcessor;std::uint64_t filteredPlayers{};
    // Secondary menus (one live, closing ones fade out then retire).
    struct Menu {mod::MediaAssemblyMenuKind kind{};P origin;double opened{},closed{};bool closing{},uploaded{};std::unique_ptr<ControlMenu>control;std::unique_ptr<NotesMenu>notes;std::string documentID;};
    std::unique_ptr<Menu>menu;std::vector<std::unique_ptr<Menu>>closing,dead;std::uint64_t menuSerial{};bool menuPressed{};std::size_t menuFocus{};
    struct Dialog {gpu::MediaAssemblyDialogRequest request;std::string documentID;std::uint64_t generation{};};std::optional<Dialog>dialog;std::uint64_t dialogGeneration{};
    // Source AVPlayer through the shared broker. One request key; released on
    // pause/edit/crop/hide/close/export exactly like the source releasePlayer.
    // A fresh broker identity per player (source: a new AVPlayer each time),
    // so no paused/play intent is inherited from a released player.
    struct Video {bool requested{},started{};std::string key;std::vector<std::string>retiring;std::uint64_t generation{},revision{},meshRevision{};std::string document;mod::MediaAssemblyAdjustments adjustments;std::uint64_t previews{};
        std::string texture;unsigned width{},height{};std::array<gpu::Vertex,4>quad{};bool meshDirty{},meshUploaded{},shown{};
        // Position from the shared frame clock, re-anchored on each engine
        // progress report; one end-of-range deadline instead of an observer.
        bool playing{};double anchorTime{},anchorPosition{};std::optional<double>endDeadline;}video;
    std::array<gpu::LayerCompositionEntry,16>composed;std::size_t count{};MediaAssemblyPreviewStats statistics;
    Impl(gpu::LayerRasterizer&r,app::UtilityExecutor&q,std::shared_ptr<mod::MediaAssemblyEngine>e,MediaAssemblyPreviewOptions o):raster(r),executor(q),route(q.makeRoute()),engine(std::move(e)),options(std::move(o)),assets(options.raster.assetRoot),geometry(r),surface(prepare(),core::Module::mediaAssembly){
        need(engine&&bool(options.uuid),"Media Assembly owner requires its engine and identity source");if(options.exportExecutor)exportRoute=options.exportExecutor->makeRoute();closing.reserve(4);dead.reserve(4);retiredTextures.reserve(4);
        scene=std::make_unique<gpu::NativeMediaAssemblyScene>(raster,options.raster,assets);
    }
    gpu::LayerScene&prepare(){geometry.load(blank(),options.raster);return geometry;}
    void initialize(){
        const std::weak_ptr<Impl>weak=weak_from_this();
        controller=std::make_unique<mod::MediaAssemblyController>(engine,[weak](std::function<void()>work,std::function<void(std::exception_ptr)>done){auto i=weak.lock();if(!i||!i->alive)return false;return i->executor.submit(i->route,std::move(work),std::move(done));},
            mod::MediaAssemblyController::Callbacks{[weak]{if(auto i=weak.lock();i&&i->alive){i->dirty=true;if(i->options.changed)i->options.changed();}},
                [weak](std::string_view a){if(auto i=weak.lock();i&&i->alive&&i->options.event)i->options.event(a);},options.uuid},
            options.exportExecutor?mod::MediaAssemblySubmit([weak](std::function<void()>work,std::function<void(std::exception_ptr)>done){auto i=weak.lock();if(!i||!i->alive)return false;return i->options.exportExecutor->submit(i->exportRoute,std::move(work),std::move(done));}):mod::MediaAssemblySubmit{});
    }
    struct Event {Impl&i;Event(Impl&v,double t):i(v){need(std::isfinite(t)&&t>=0,"Media Assembly owner needs a finite clock");i.time=std::max(i.time,t);}};
    mod::MediaAssemblyView view()const{return controller->session().view();}
    core::Language language()const{return options.appearance.language;}
    std::optional<P>local(P p)const{return input&&pose&&std::isfinite(p.x)&&std::isfinite(p.y)?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
    void changed(){dirty=true;if(options.changed)options.changed();}
    void cropPreview(){controller->setCropPreview(presentation.tool()==mod::MediaAssemblyTool::crop,time);}
    gpu::DrawObject previewDraw(const std::string&id)const{gpu::DrawObject d;d.sourceID="media.assembly.preview.draw";d.meshID="media.assembly.preview.quad";d.textureID=id;return d;}
    // Content event only (never per warm frame): dirty is set by explicit
    // input, controller/engine completions, appearance or language events.
    void sync(){
        if(!dirty)return;const auto v=view();presentation.synchronize(v);
        const auto image=controller->previewImage();
        if(image!=shown){shown=image;
            // A cleared preview (hidden/parked editor) releases its texture.
            if(!shown)retireTexture();
            if(shown){
            // CPU pixels and GPU media targets never share an id.
            if(!texture.empty()&&(shown->deferred()!=bool(media)))retireTexture();
            if(texture.empty()&&!shown->deferred())texture="media.assembly.preview."+std::to_string(++textureSerial);textureDirty=true;}}
        presentPreview();
        const auto&e=controller->session().error();const auto message=e?mod::mediaAssemblyErrorMessage(*e,language()):std::string{};
        scene->syncContent(presentation,v,options.appearance,message,controller->session().progress(),time,animateInline,options.reduceMotion);animateInline=false;dirty=false;
    }
    // Borrowed preview draw: the live player texture while playing (source
    // AVPlayerLayer above the poster), otherwise the bounded edited frame.
    void presentPreview(){
        if(video.shown&&!video.texture.empty()&&video.meshUploaded){gpu::DrawObject d;d.sourceID="media.assembly.video.draw";d.meshID="media.assembly.video.quad";d.textureID=video.texture;scene->setPreview(gpu::MediaAssemblyPreviewDraw{d,{0,0,1,1}});return;}
        if(shown&&!texture.empty())scene->setPreview(gpu::MediaAssemblyPreviewDraw{previewDraw(texture),{0,0,1,1}});else scene->setPreview(std::nullopt);
    }
    void releaseVideo(){
        if(!video.requested)return;if(options.broker)options.broker->hideVideos(options.client,time);
        video.requested=video.started=video.shown=video.playing=false;video.endDeadline.reset();video.retiring.push_back(std::move(video.key));video.key.clear();++statistics.playbackReleases;
        if(playbackProcessor)playbackProcessor->releaseTargets();presentPreview();
    }
    // Source preparePlayer/beginPlayback/releasePlayer and the end-of-range
    // stop, folded into owner events and the broker's own frame demand.
    void syncPlayback(){
        auto&s=controller->session();const auto doc=s.document();
        const bool want=options.broker&&options.broker->hasVideoPlayback()&&doc&&doc->video&&s.active()&&s.wantsPlayback()&&!s.cropPreview()&&!s.exporting()&&input;
        if(video.requested&&(!want||video.document!=doc->id||video.adjustments!=s.adjustments())){
            const bool resume=want&&video.document==doc->id;releaseVideo();
            // Source: an edit releases the player; the next completed preview
            // prepares a new one when playback is still wanted.
            if(resume)video.previews=controller->completedPreviews();
            if(!want)return;}
        if(!want)return;
        if(!video.requested){
            if(video.previews&&controller->completedPreviews()==video.previews)return;
            video.key="media.assembly.video."+std::to_string(++video.revision);gpu::NotesVideoRequest r{video.key,doc->path,video.revision,1024,doc->duration,{},false};attachFrameFilter(r,s.adjustments());
            options.broker->setVideos(options.client,++video.generation,std::span(&r,1),time);
            video.requested=true;video.started=false;video.document=doc->id;video.adjustments=s.adjustments();video.previews=0;
        }
        const auto*rec=options.broker->findVideo(options.client,video.key);if(!rec)return;
        const double end=std::max(s.adjustments().trimStart,s.adjustments().trimEnd.value_or(doc->duration));
        if(rec->state==mod::NotesMediaState::failed){controller->report(mod::MediaAssemblyError::unavailable);controller->playbackSession().playbackEnded();releaseVideo();return;}
        if(!video.started&&(rec->state==mod::NotesMediaState::ready||rec->state==mod::NotesMediaState::paused)){
            double start=s.currentTime();if(start>=end-.05)start=s.adjustments().trimStart;
            options.broker->seekVideo(options.client,video.key,start);if(!rec->wantsPlayback)options.broker->toggleVideo(options.client,video.key,time);video.started=true;++statistics.playbackStarts;
            controller->playbackSession().playbackChanged(false,start);
        }
        if(!video.started)return;
        const bool playing=rec->state==mod::NotesMediaState::playing;
        // Re-anchor only when the engine actually reports a new position.
        if(playing&&(!video.playing||rec->currentTime!=video.anchorPosition)){video.anchorTime=time;video.anchorPosition=rec->currentTime;}
        video.playing=playing;const double position=playing?video.anchorPosition+(time-video.anchorTime):rec->currentTime;
        video.endDeadline=playing?std::optional<double>(video.anchorTime+std::max(0.,end-video.anchorPosition)):std::nullopt;
        if(!rec->textureID.empty()&&rec->width&&rec->height&&(rec->textureID!=video.texture||rec->width!=video.width||rec->height!=video.height||!video.meshUploaded)){
            video.texture=rec->textureID;video.width=rec->width;video.height=rec->height;
            // Geometry edits (crop/mirror/quarter turns) map through texture
            // coordinates; colour edits run in place through the frame
            // server's filter hook when it exists (attachFrameFilter).
            const auto plan=mod::mediaAssemblyPixelPlan(rec->width,rec->height,s.adjustments());const std::array<P,4>corners{{{0,0},{1,0},{1,1},{0,1}}};
            for(std::size_t k=0;k<4;++k){const auto uv=plan.sourceUV(corners[k]);video.quad[k]={{float(corners[k].x),float(corners[k].y),0},{float(uv.x),float(uv.y)}};}video.meshDirty=true;
        }
        // Source periodic observer granularity (0.25 s): no per-frame content
        // rebuild while the engine plays; state changes apply immediately.
        if(playing!=s.view().playing||std::abs(position-s.currentTime())>=.25)controller->playbackSession().playbackChanged(playing,position);
        video.shown=playing&&!video.texture.empty();
        if(playing&&video.endDeadline&&time>=*video.endDeadline){
            // forwardPlaybackEndTime: stop at the trim end and show a fresh poster.
            if(rec->wantsPlayback)options.broker->toggleVideo(options.client,video.key,time);controller->playbackSession().playbackEnded();releaseVideo();changed();return;
        }
        presentPreview();
    }
    // Source AVVideoComposition for played frames. The shared frame server
    // exposes a per-request filter hook (frameFilter, see the shared-file
    // request); when present, colour edits run in place between the engine's
    // transfer and the renderer commit (linear working space, like the
    // source compositor). Without the hook, geometry still maps through
    // texture coordinates and paused/exported frames carry every edit.
    template<class Request>void attachFrameFilter(Request&request,const mod::MediaAssemblyAdjustments&current){
        if(!mediaAssemblyFrameFilterHook<Request>||!processor||!renderer||!shown||!shown->deferred()||*shown->edits!=current)return;
        // Colour-only identity edits need no pass at all.
        if(mod::MediaAssemblyColorPipeline(current,shown->lookup,true,false).identity())return;
        try{
            if(!playbackProcessor)playbackProcessor=std::make_unique<gpu::NativeMediaAssemblyGpuProcessor>(renderer->mediaDevice());
            playbackProcessor->configure(current,shown->lookup,true);
        }catch(const std::exception&){playbackProcessor.reset();return;}
        const std::weak_ptr<Impl>weak=weak_from_this();
        if(attachMediaAssemblyFrameFilter(request,[weak](void*surface){
            const auto i=weak.lock();if(!i||!i->playbackProcessor||!surface)return;
            try{i->playbackProcessor->processInPlace(static_cast<IDXGISurface*>(surface));}catch(const std::exception&){i->playbackProcessor.reset();}
        }))++filteredPlayers;
    }
    // ---- menus ----
    void retire(std::unique_ptr<Menu>m,bool animated){if(!m)return;if(animated&&!options.reduceMotion&&m->uploaded){m->closing=true;m->closed=time;closing.push_back(std::move(m));}else dead.push_back(std::move(m));}
    void closeMenu(bool animated){menuPressed=false;retire(std::move(menu),animated);}
    void openMenu(mod::MediaAssemblyMenuKind kind,std::string_view anchor){
        closeMenu(false);auto m=std::make_unique<Menu>();m->kind=kind;m->opened=time;const auto doc=controller->session().document();m->documentID=doc?doc->id:std::string{};P size;
        if(kind==mod::MediaAssemblyMenuKind::exportMedia||kind==mod::MediaAssemblyMenuKind::confirmOverwrite){
            auto model=kind==mod::MediaAssemblyMenuKind::exportMedia?mod::mediaAssemblyExportMenu(*doc,language()):mod::mediaAssemblyOverwriteMenu(language());size=model.size;
            m->control=std::make_unique<ControlMenu>(raster,std::move(model),options.appearance,options.raster,++menuSerial);
        }else{
            m->notes=std::make_unique<NotesMenu>();auto&in=m->notes->input;in.kind=kind==mod::MediaAssemblyMenuKind::source?mod::NotesControlsKind::mediaSource:mod::NotesControlsKind::shelfMedia;
            in.dark=options.appearance.dark;in.accent=options.appearance.accent;in.systemOrange=options.appearance.systemOrange;in.strings=menuStrings(language());
            if(in.kind==mod::NotesControlsKind::shelfMedia&&options.shelfChoices)in.choices=options.shelfChoices();
            m->notes->controls.update(in);m->notes->scene=std::make_unique<gpu::NativeNotesControlsScene>(m->notes->controls,raster,options.raster);m->notes->scene->syncContent();m->notes->scene->updatePose({},1,time);
            m->notes->group=std::make_unique<gpu::NativeLayerGroup>(m->notes->scene->scene(),"media.assembly.menu."+std::to_string(++menuSerial),options.raster.pixelsPerPoint);
            const auto b=m->notes->controls.bounds();size={b.width,b.height};
        }
        ++statistics.menuBuilds;menuFocus=0;m->origin=mod::mediaAssemblyMenuOrigin(mod::mediaAssemblyMenuAnchor(presentation.actions(view(),language()),anchor),size);menu=std::move(m);
    }
    R menuBounds(const Menu&m)const{if(m.control)return {m.origin.x,m.origin.y,m.control->model.size.x,m.control->model.size.y};const auto b=m.notes->controls.bounds();return {m.origin.x,m.origin.y,b.width,b.height};}
    std::optional<std::string>menuActionAt(const Menu&m,P local)const{
        if(m.control){const auto a=m.control->model.actionAt(local);return a?std::optional<std::string>(std::string(*a)):std::nullopt;}
        const auto a=m.notes->controls.actionAt(local);return a?std::optional<std::string>(std::string(*a)):std::nullopt;
    }
    void requestDialog(gpu::MediaAssemblyDialogRequest r){
        const auto doc=controller->session().document();dialog=Dialog{std::move(r),doc?doc->id:std::string{},++dialogGeneration};
        if(options.window)PostMessageW(options.window,options.dialogMessage,WPARAM(dialog->generation),0);
    }
    bool doMenuAction(std::string_view action){
        if(!menu)return false;const auto kind=menu->kind;
        if(action=="close"){closeMenu(true);return true;}
        if(kind==mod::MediaAssemblyMenuKind::source){
            if(action=="finder"){closeMenu(true);gpu::MediaAssemblyDialogRequest r;r.kind=gpu::MediaAssemblyDialogRequest::Kind::open;r.extensions=gpu::mediaAssemblyOpenExtensions();requestDialog(std::move(r));return true;}
            if(action=="shelf"){openMenu(mod::MediaAssemblyMenuKind::shelf,"open");return true;}
            return false;
        }
        if(kind==mod::MediaAssemblyMenuKind::shelf){
            auto&n=*menu->notes;auto&in=n.input;
            if(action=="filter"){in.mediaOnly=!in.mediaOnly;in.firstRow=0;in.selectedID.reset();n.controls.update(in);n.scene->syncContent();n.dirty=true;return true;}
            std::optional<std::string>id;
            if(action=="use")id=in.selectedID;
            else if(action.starts_with("row:")){std::size_t row{};for(char c:action.substr(4)){if(c<'0'||c>'9')return false;row=row*10+std::size_t(c-'0');}
                std::vector<const mod::NotesShelfChoice*>rows;for(const auto&c:in.choices)if(!in.mediaOnly||c.supported)rows.push_back(&c);
                if(row>=rows.size()||!rows[row]->supported||!rows[row]->available)return false;id=rows[row]->id;in.selectedID=id;}
            if(!id)return false;closeMenu(true);
            std::optional<MediaAssemblyShelfAccess>access;try{if(options.shelfAccess)access=options.shelfAccess(*id);}catch(...){access.reset();}
            if(!access||access->path.empty()){controller->report(mod::MediaAssemblyError::unavailable);return true;}
            if(!controller->session().exporting())controller->importPath(std::move(access->path),std::move(access->lease),true,time);return true;
        }
        const auto doc=controller->session().document();
        if(!doc||doc->id!=menu->documentID||controller->session().busy()){closeMenu(true);return true;}
        if(kind==mod::MediaAssemblyMenuKind::exportMedia){
            if(action=="saveAs"){closeMenu(true);gpu::MediaAssemblyDialogRequest r;r.kind=gpu::MediaAssemblyDialogRequest::Kind::save;
                r.folder=gpu::mediaAssemblyUTF8(gpu::mediaAssemblyPath(doc->path).parent_path());r.filename=doc->suggestedFilename();r.extensions=controller->exportExtensions();requestDialog(std::move(r));return true;}
            if(action=="overwrite"){openMenu(mod::MediaAssemblyMenuKind::confirmOverwrite,"export");return true;}
            return false;
        }
        if(action=="cancel"){closeMenu(true);return true;}
        if(action=="confirm"){closeMenu(true);controller->exportTo(doc->path,true,doc->id,time);return true;}
        return false;
    }
    // ---- canvas requests ----
    void apply(const mod::MediaAssemblyRequest&r,std::string_view id={}){
        using C=mod::MediaAssemblyCommand;
        switch(r.command){
        case C::adjust:if(r.adjustments)controller->update(*r.adjustments,std::nullopt,time);break;
        case C::trim:if(r.adjustments)controller->update(*r.adjustments,r.time,time);break;
        case C::seek:controller->seek(r.time,time);break;
        case C::play:controller->togglePlayback(time);break;
        case C::open:if(!controller->session().exporting())openMenu(mod::MediaAssemblyMenuKind::source,"open");break;
        case C::exportMedia:{const auto&s=controller->session();if(!s.busy()&&!s.exporting()&&s.document())openMenu(mod::MediaAssemblyMenuKind::exportMedia,"export");break;}
        case C::cancelExport:controller->cancelExport();break;
        case C::closeMedia:closeSource();break;
        case C::reset:controller->reset(time);break;
        default:break;
        }
        if(id=="tools"||id=="filters"||id=="stickers"||id=="toolBack"||id=="crop"||id=="adjust"||id=="curves"||id=="levels"||id=="trim")scene->revealDrawer(time,!options.reduceMotion);
        cropPreview();syncPlayback();changed();
    }
    void closeSource(){
        const auto&s=controller->session();if(s.exporting()||(!s.document()&&!s.busy()))return;
        closeMenu(false);dialog.reset();presentation.pointerUp(view());pressed=false;
        // Keep the bounded poster and sticker leaves for the 0.16 s fade; the
        // texture id moves to the retired list and is released once unused.
        std::optional<gpu::MediaAssemblyPreviewDraw>poster;if(shown&&!texture.empty())poster=gpu::MediaAssemblyPreviewDraw{previewDraw(texture),{0,0,1,1}};
        if(pose)scene->beginClose(poster,time,!options.reduceMotion);
        releaseVideo();retireTexture();shown.reset();scene->setPreview(std::nullopt);
        controller->closeDocument(time);
    }
    // The live preview id moves to the retired list (removed once no longer
    // drawn); a retained GPU target is dropped with it.
    void retireTexture(){if(!texture.empty()){retiredTextures.push_back(texture);texture.clear();}media.reset();if(processor)processor->releaseTargets();}
    void gpuFailed(){processor.reset();media.reset();gpuSource.reset();controller->setDeferredPreviews(false,time);}
    void createProcessor(gpu::Renderer&r){
        try{processor=std::make_unique<gpu::NativeMediaAssemblyGpuProcessor>(r.mediaDevice());}catch(const std::exception&){processor.reset();}
        controller->setDeferredPreviews(processor!=nullptr,time);
    }
    // Edits the deferred source on the renderer's device into the retained
    // media texture; a new id only when the edited size changes.
    void renderPreview(gpu::Renderer&r){
        const auto&image=*shown;
        if(image.source!=gpuSource){processor->setSource(image.source->width,image.source->height,image.source->straightRGBA,std::size_t(image.source->width)*4,false);gpuSource=image.source;}
        const auto plan=processor->configure(*image.edits,image.lookup,false);
        if(!media||!media->valid()||media->width()!=plan.outputWidth||media->height()!=plan.outputHeight){
            retireTexture();texture="media.assembly.preview."+std::to_string(++textureSerial);media=r.createMediaTexture(texture,plan.outputWidth,plan.outputHeight);++statistics.gpuTargets;presentPreview();
        }
        processor->render(static_cast<IDXGISurface*>(media->targetSurface()));r.commitMediaTexture(*media);++statistics.gpuPreviews;
    }
    void activate(bool nextPrepared,bool nextInput){
        if(input!=nextInput){input=nextInput;pressed=false;if(!input){pointerPoint.reset();presentation.pointerUp(view());}}
        if(prepared!=nextPrepared){prepared=nextPrepared;
            if(!prepared){closeMenu(false);dialog.reset();presentation.hide();controller->setCropPreview(false,time);}
            controller->setActive(prepared,time);syncPlayback();dirty=true;}
    }
    void feedback(){if(!pose)return;std::optional<P>q;if(pointerPoint)q=local(*pointerPoint);
        if(menu){const auto b=menuBounds(*menu);const auto inMenu=q&&inside(b,*q)?std::optional<P>(P{q->x-b.x,q->y-b.y}):std::nullopt;
            if(menu->control)menu->control->feedback(inMenu?menuActionAt(*menu,*inMenu):std::nullopt,menuPressed,options.reduceMotion,time);
            else{const auto a=inMenu?menu->notes->controls.actionAt(*inMenu):std::nullopt;menu->notes->scene->setFeedback(a,menuPressed,options.reduceMotion,time);}
            scene->setFeedback(std::nullopt,false,options.reduceMotion,time);return;}
        scene->setFeedback(q,pressed,options.reduceMotion,time);
    }
    double menuOpacity(const Menu&m)const{return m.closing?mod::MediaAssemblyMenuMotion::opacity(false,time-m.closed,options.reduceMotion):mod::MediaAssemblyMenuMotion::opacity(true,time-m.opened,options.reduceMotion);}
    template<class F>void eachMenu(F&&f)const{for(const auto&m:closing)f(*m);if(menu)f(*menu);}
    void poseMenu(Menu&mm){Menu*m=&mm;{const auto world=pose->contentWorld*M::translation(m->origin.x,m->origin.y);const float alpha=float(menuOpacity(*m))*pose->opacity;
            // Local leaves pose every frame; the group pose needs its upload.
            if(m->control){m->control->pose(time);if(m->uploaded)m->control->group->setPose(world,alpha,std::span(&pose->hostClip,1),pose->shutter);}
            else{m->notes->scene->updatePose({},1,time);if(m->uploaded)m->notes->group->setPose(world,alpha,std::span(&pose->hostClip,1),pose->shutter);}}}
    void poseMenus(){
        if(pose)eachMenu([this](Menu&m){poseMenu(m);});
        // Source retirement: a closed menu's artwork leaves after 0.18 s.
        for(auto it=closing.begin();it!=closing.end();)if(time>=(*it)->closed+mod::MediaAssemblyMenuMotion::retire){dead.push_back(std::move(*it));it=closing.erase(it);}else ++it;
    }
    bool menuAnimating(double t)const{
        for(const auto&m:closing)if(t<m->closed+mod::MediaAssemblyMenuMotion::retire)return true;
        if(menu&&!options.reduceMotion&&t<menu->opened+mod::MediaAssemblyMenuMotion::fade)return true;
        if(menu&&menu->control&&menu->control->animating(t))return true;if(menu&&menu->notes&&menu->notes->scene->requiresFrames(t))return true;return false;
    }
};
MediaAssemblyPreview::MediaAssemblyPreview(gpu::LayerRasterizer&r,app::UtilityExecutor&q,std::shared_ptr<mod::MediaAssemblyEngine>e,MediaAssemblyPreviewOptions o):impl_(std::make_shared<Impl>(r,q,std::move(e),std::move(o))){impl_->initialize();}
MediaAssemblyPreview::~MediaAssemblyPreview(){auto i=impl_;try{i->releaseVideo();}catch(...){}i->alive=false;i->executor.invalidate(i->route);if(i->options.exportExecutor)i->options.exportExecutor->invalidate(i->exportRoute,false);i->controller->cancelExport();}
void MediaAssemblyPreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Media Assembly viewport");impl_->metrics=m;}
void MediaAssemblyPreview::setLanguage(core::Language l,double t){auto i=impl_;const Impl::Event e(*i,t);if(i->options.appearance.language==l)return;i->options.appearance.language=l;i->closeMenu(false);i->changed();}
void MediaAssemblyPreview::setAppearance(mod::MediaAssemblyAppearance a,double t){auto i=impl_;const Impl::Event e(*i,t);if(i->options.appearance==a)return;i->options.appearance=a;i->closeMenu(false);i->changed();}
void MediaAssemblyPreview::setReduceMotion(bool v,double t){auto i=impl_;const Impl::Event e(*i,t);if(i->options.reduceMotion==v)return;i->options.reduceMotion=v;if(v)i->scene->settle();i->changed();}
void MediaAssemblyPreview::setOverlayVisible(bool v,double t){auto i=impl_;const Impl::Event e(*i,t);if(i->overlay==v)return;i->overlay=v;if(!v)i->activate(false,false);i->changed();}
void MediaAssemblyPreview::update(const M&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){
    auto i=impl_;const Impl::Event e(*i,t);need(i->metrics.pixelWidth&&i->metrics.pixelHeight,"Resize Media Assembly before presentation");
    const auto*shown=sample.current.module==core::Module::mediaAssembly?&sample.current:sample.incoming&&sample.incoming->module==core::Module::mediaAssembly?&*sample.incoming:nullptr;
    const bool prepared=shown&&sample.requested==core::Module::mediaAssembly&&i->overlay;i->activate(prepared,prepared&&sample.acceptsModuleInput);
    if(!shown){i->pose.reset();i->registration.update({});return;}
    i->surface.update(center,settings,*shown,opacity);i->pose=i->surface.pose();if(i->dirty){i->syncPlayback();i->sync();}const auto&p=*i->pose;
    const auto camera=gpu::layerViewportProjection(i->metrics.pixelWidth,i->metrics.pixelHeight)*M::scale(i->metrics.scale,i->metrics.scale);
    i->projection=core::Projection::viewport(camera*p.contentWorld,i->metrics.pixelWidth,i->metrics.pixelHeight);i->feedback();
    i->scene->updatePose({p.contentWorld,p.opacity,i->time,std::span(&p.hostClip,1),p.shutter?&*p.shutter:nullptr});i->poseMenus();i->registration.update(p.registration);i->seen=true;
}
std::optional<double>MediaAssemblyPreview::nextWakeTime()const{auto out=impl_->controller->nextWakeTime();const auto end=impl_->video.endDeadline;if(end&&(!out||*end<*out))out=end;return out;}
bool MediaAssemblyPreview::deadline(double t){
    auto i=impl_;const Impl::Event e(*i,t);bool did=i->controller->deadline(i->time);
    if(i->video.endDeadline&&i->time>=*i->video.endDeadline){i->syncPlayback();did=true;}
    if(did){i->dirty=true;if(i->options.changed)i->options.changed();}return did;
}
bool MediaAssemblyPreview::requiresFrames(double t)const{const auto&i=*impl_;t=std::max(t,i.time);if(!i.pose||!i.overlay)return false;return i.scene->requiresFrames(t)||i.menuAnimating(t);}
bool MediaAssemblyPreview::covers(P p)const{const auto q=impl_->local(p);if(!q)return false;if(impl_->menu&&inside(impl_->menuBounds(*impl_->menu),*q))return true;return inside(mod::MediaAssemblyPresentation::bounds,*q);}
bool MediaAssemblyPreview::capturesPointer()const noexcept{return impl_->input&&(impl_->menu||impl_->presentation.dragging());}
bool MediaAssemblyPreview::pointerLocked()const noexcept{return impl_->input&&impl_->presentation.dragging();}
bool MediaAssemblyPreview::pointer(const app::PointerEvent&e,double t){
    auto i=impl_;const Impl::Event ev(*i,t);i->pointerPoint=P{e.x,e.y};const auto q=i->local(*i->pointerPoint);
    if(e.kind==app::PointerKind::captureLost){const bool owned=i->pressed||i->presentation.dragging()||i->menuPressed;i->pressed=i->menuPressed=false;i->pointerPoint.reset();if(owned){i->apply(i->presentation.pointerUp(i->view()));}i->feedback();return owned;}
    if(e.kind==app::PointerKind::leave){i->pointerPoint.reset();i->feedback();return false;}
    if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left){
        const bool owned=i->pressed||i->presentation.dragging()||i->menuPressed;i->pressed=false;i->menuPressed=false;if(!owned)return false;
        i->apply(i->presentation.pointerUp(i->view()));i->feedback();return true;}
    if(!i->input)return false;
    if(e.kind==app::PointerKind::move){
        if(q&&i->presentation.dragging()){i->apply(i->presentation.pointerDrag(*q,i->view()));if(i->pose){i->sync();}}
        i->feedback();return i->presentation.dragging()||bool(i->menu)||(q&&inside(mod::MediaAssemblyPresentation::bounds,*q));}
    if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left)return false;
    if(i->menu){
        // Menus consume input before the controls behind them (source mouseDown).
        i->menuPressed=true;const auto b=i->menuBounds(*i->menu);
        if(q&&inside(b,*q)){if(const auto a=i->menuActionAt(*i->menu,{q->x-b.x,q->y-b.y}))i->doMenuAction(*a);}else i->closeMenu(true);
        if(i->alive){i->changed();i->feedback();}return true;}
    if(!q)return false;
    auto r=i->presentation.pointerDown(*q,i->view(),i->language(),i->options.uuid());
    std::string action;for(const auto&a:i->presentation.actions(i->view(),i->language()))if(a.rect.contains(*q)){action=a.id;break;}
    if(r.consumed)i->pressed=true;i->apply(r,action);if(i->pose)i->sync();i->feedback();return r.consumed;
}
bool MediaAssemblyPreview::wheel(const app::WheelEvent&e,double t){
    auto i=impl_;const Impl::Event ev(*i,t);if(!i->input||!std::isfinite(e.steps)||!e.linesPerStep)return false;const auto q=i->local({e.x,e.y});if(!q)return false;
    const double points=-e.steps*(e.linesPerStep==UINT32_MAX?86:12.*e.linesPerStep);
    if(i->menu){if(i->menu->notes&&i->menu->kind==mod::MediaAssemblyMenuKind::shelf&&!e.horizontal&&inside(i->menuBounds(*i->menu),*q)){
            auto&n=*i->menu->notes;n.remainder+=std::clamp(points,-256.,256.);const auto rows=static_cast<long long>(n.remainder/30);if(rows){n.remainder-=double(rows)*30;
                const auto total=std::count_if(n.input.choices.begin(),n.input.choices.end(),[&](const auto&c){return !n.input.mediaOnly||c.supported;});
                const auto next=std::size_t(std::clamp<long long>(static_cast<long long>(n.input.firstRow)+rows,0,std::max<long long>(0,total-4)));if(next!=n.input.firstRow){n.input.firstRow=next;n.controls.update(n.input);n.scene->syncContent();n.dirty=true;i->changed();}}}
        return true;}
    // Source: Command/Option or a non-precise wheel zooms; touchpad pans.
    // Windows precision-touchpad pinch arrives as Ctrl+wheel.
    const bool integral=std::abs(e.steps-std::round(e.steps))<1e-9;const bool zoom=(e.modifiers&MK_CONTROL)||(!e.horizontal&&integral);
    const bool tool=i->presentation.tool().has_value();const double before=i->presentation.drawerOffset();
    const double delta=zoom&&!(e.modifiers&MK_CONTROL)?-e.steps*12:points; // source: -scrollingDeltaY*12 per wheel notch
    const bool used=i->presentation.scroll(*q,e.horizontal?delta:0,e.horizontal?0:delta,zoom,i->view());
    if(used){i->animateInline=tool&&before!=i->presentation.drawerOffset();i->changed();if(i->pose)i->sync();}return used;
}
bool MediaAssemblyPreview::key(const app::KeyEvent&e,mod::MediaAssemblyModifiers m,double t){
    auto i=impl_;const Impl::Event ev(*i,t);if(!i->input||e.kind!=app::KeyKind::down)return false;
    if(i->menu){
        if(e.value==VK_ESCAPE){i->closeMenu(true);i->changed();return true;}
        // Source NotesRetainedMenu.keyDown: arrows move among enabled items,
        // Return/Space perform the focused one.
        std::vector<std::string>enabled;
        if(i->menu->control){for(const auto&item:i->menu->control->model.items)if(item.enabled)enabled.push_back(item.id);}
        else for(const auto&a:i->menu->notes->controls.actions())if(a.enabled)enabled.push_back(a.id);
        if(enabled.empty())return true;auto&f=i->menuFocus;f=std::min(f,enabled.size()-1);
        if(e.value==VK_DOWN||e.value==VK_RIGHT)f=std::min(enabled.size()-1,f+1);else if(e.value==VK_UP||e.value==VK_LEFT){if(f)--f;}
        else if(e.value==VK_RETURN||e.value==VK_SPACE){i->doMenuAction(enabled[f]);i->changed();}
        return true;}
    mod::MediaAssemblyKey k=mod::MediaAssemblyKey::other;
    switch(e.value){case VK_ESCAPE:k=mod::MediaAssemblyKey::escape;break;case VK_BACK:k=mod::MediaAssemblyKey::deleteBackward;break;case VK_DELETE:k=mod::MediaAssemblyKey::deleteForward;break;
    case VK_LEFT:k=mod::MediaAssemblyKey::left;break;case VK_RIGHT:k=mod::MediaAssemblyKey::right;break;case VK_UP:k=mod::MediaAssemblyKey::up;break;case VK_DOWN:k=mod::MediaAssemblyKey::down;break;
    case VK_SPACE:k=mod::MediaAssemblyKey::space;break;case VK_OEM_PLUS:case VK_ADD:k=mod::MediaAssemblyKey::plus;break;case VK_OEM_MINUS:case VK_SUBTRACT:k=mod::MediaAssemblyKey::minus;break;default:break;}
    if(k==mod::MediaAssemblyKey::other)return false;m.alt=m.alt||e.alt;
    const auto r=i->presentation.key(k,m,i->view());if(!r.consumed)return false;i->apply(r);if(i->pose)i->sync();return true;
}
void MediaAssemblyPreview::focus(bool,double t){auto i=impl_;const Impl::Event e(*i,t);i->pressed=i->menuPressed=false;}
void MediaAssemblyPreview::cancelInteraction(double t){auto i=impl_;const Impl::Event e(*i,t);i->pressed=i->menuPressed=false;i->pointerPoint.reset();i->apply(i->presentation.pointerUp(i->view()));i->feedback();}
bool MediaAssemblyPreview::importFiles(std::span<const std::string>paths,double t){
    auto i=impl_;const Impl::Event e(*i,t);if(!i->input||paths.empty()||i->controller->session().exporting())return false;i->closeMenu(true);
    const bool ok=i->controller->importPath(paths.front(),nullptr,true,i->time);i->changed();return ok;
}
bool MediaAssemblyPreview::refreshSharedMedia(double t){
    auto i=impl_;const Impl::Event e(*i,t);if(!i->options.broker||(!i->video.requested&&i->video.retiring.empty()))return false;
    const bool before=i->video.shown;const auto position=i->controller->session().currentTime();i->syncPlayback();
    for(auto it=i->video.retiring.begin();it!=i->video.retiring.end();){const auto*rec=i->options.broker->findVideo(i->options.client,*it);
        if(!rec||(!rec->visible&&i->options.broker->retireVideo(i->options.client,*it)))it=i->video.retiring.erase(it);else ++it;}
    const bool moved=before!=i->video.shown||position!=i->controller->session().currentTime();if(moved){i->dirty=true;if(i->options.changed)i->options.changed();}return moved;
}
bool MediaAssemblyPreview::message(const app::NativeMessage&m,double t){
    auto i=impl_;const Impl::Event e(*i,t);if(m.message!=i->options.dialogMessage||!i->dialog||m.wParam!=WPARAM(i->dialog->generation))return false;
    const auto d=std::move(*i->dialog);i->dialog.reset();if(!i->options.dialog)return true;++i->statistics.dialogs;
    const auto chosen=i->options.dialog(i->options.window,d.request);if(!i->alive||!chosen||!i->prepared)return true;
    if(d.request.kind==gpu::MediaAssemblyDialogRequest::Kind::open){i->controller->importPath(*chosen,nullptr,true,i->time);i->changed();return true;}
    const auto&s=i->controller->session();if(s.busy()||!s.document()||s.document()->id!=d.documentID)return true;
    // NSSavePanel already obtained replacement confirmation: overwrite == exists.
    bool exists{};try{exists=gpu::mediaAssemblyFileIdentity(gpu::mediaAssemblyPath(*chosen)).has_value();}catch(...){exists=true;}
    i->controller->exportTo(*chosen,exists,d.documentID,i->time);i->changed();return true;
}
bool MediaAssemblyPreview::perform(std::string_view action,double t){auto i=impl_;const Impl::Event e(*i,t);if(!i->input)return false;auto r=i->presentation.perform(action,i->view(),action.starts_with("sticker:")?i->options.uuid():std::string{});i->apply(r,action);if(i->pose)i->sync();return r.consumed;}
bool MediaAssemblyPreview::menuAction(std::string_view action,double t){auto i=impl_;const Impl::Event e(*i,t);if(!i->input||!i->menu)return false;const bool ok=i->doMenuAction(action);i->changed();return ok;}
std::vector<mod::MediaAssemblyAccessible>MediaAssemblyPreview::accessibility()const{
    const auto&i=*impl_;if(!i.pose||!i.input)return {};auto all=i.presentation.accessibility(i.view(),i.language(),i.menu!=nullptr);
    for(auto&e:all){const auto r=e.rect;const std::array<P,4>corners{{{r.x,r.y},{r.x+r.width,r.y},{r.x,r.y+r.height},{r.x+r.width,r.y+r.height}}};double x0=INFINITY,y0=INFINITY,x1=-INFINITY,y1=-INFINITY;bool ok=true;
        for(const auto c:corners){const auto q=i.projection.project(c);if(!q){ok=false;break;}x0=std::min(x0,q->x);y0=std::min(y0,q->y);x1=std::max(x1,q->x);y1=std::max(y1,q->y);}
        e.rect=ok?R{x0/i.metrics.scale,y0/i.metrics.scale,(x1-x0)/i.metrics.scale,(y1-y0)/i.metrics.scale}:R{};}
    return all;
}
bool MediaAssemblyPreview::setAccessibleValue(std::string_view id,double value,double t){auto i=impl_;const Impl::Event e(*i,t);if(!i->input||i->menu)return false;const auto r=i->presentation.setAccessibleValue(id,value,i->view());if(!r.consumed&&!r.adjustments)return false;i->apply(r);if(i->pose)i->sync();return true;}
std::optional<mod::MediaAssemblyMenuKind>MediaAssemblyPreview::menu()const noexcept{return impl_->menu?std::optional(impl_->menu->kind):std::nullopt;}
const mod::MediaAssemblyController&MediaAssemblyPreview::controller()const noexcept{return *impl_->controller;}
const mod::MediaAssemblyPresentation&MediaAssemblyPreview::presentation()const noexcept{return impl_->presentation;}
gpu::MediaAssemblySceneStats MediaAssemblyPreview::sceneStats()const noexcept{return impl_->scene->stats();}
bool MediaAssemblyPreview::sceneClosing()const noexcept{return impl_->scene->closingMedia();}
MediaAssemblyPreviewStats MediaAssemblyPreview::stats()const noexcept{auto s=impl_->statistics;s.videoShown=impl_->video.shown;s.gpuActive=impl_->processor!=nullptr;
    s.filteredPlayers=impl_->filteredPlayers;s.frameFilterHook=mediaAssemblyFrameFilterHook<gpu::NotesVideoRequest>;s.retainedTextures=impl_->retiredTextures.size()+(impl_->texture.empty()?0:1);return s;}
namespace {constexpr std::array<std::uint32_t,6>videoIndices{0,1,2,0,2,3};}
void MediaAssemblyPreview::upload(gpu::Renderer&r){
    auto&i=*impl_;need(!i.renderer||i.renderer==&r,"Media Assembly resources belong to another renderer");i.renderer=&r;
    if(!i.pose)return;
    // GPU previews start once the editor is first shown: the HLSL compile runs
    // on the utility worker (no UI-thread hitch); until it completes, or if a
    // renderer lacks same-device media support, previews use the CPU path.
    if(!i.gpuTried&&i.options.gpuPreview){
        if(!i.mediaChecked){i.mediaChecked=true;try{i.mediaCapable=r.mediaDevice()!=nullptr;}catch(const std::exception&){i.mediaCapable=false;}}
        if(!i.mediaCapable){i.gpuTried=true;i.controller->setDeferredPreviews(false,i.time);}
        else if(gpu::mediaAssemblyGpuShadersReady()){i.gpuTried=true;i.createProcessor(r);}
        else if(!i.shadersPending){
            const std::weak_ptr<Impl>weak=i.weak_from_this();
            i.shadersPending=i.executor.submit(i.route,[]{gpu::prepareMediaAssemblyGpuShaders();},[weak](std::exception_ptr error){
                const auto self=weak.lock();if(!self||!self->alive)return;self->shadersPending=false;if(error){self->shadersFailed=true;self->gpuTried=true;}self->changed();});
        }
    }
    if(i.shadersFailed&&i.processor==nullptr&&i.controller->deferredPreviews())i.controller->setDeferredPreviews(false,i.time);
    if(!i.meshUploaded){static constexpr std::array<gpu::Vertex,4>quad{{{{0,0,0},{0,0}},{{1,0,0},{1,0}},{{1,1,0},{1,1}},{{0,1,0},{0,1}}}};static constexpr std::array<std::uint32_t,6>indices{0,1,2,0,2,3};r.setMesh("media.assembly.preview.quad",1,{quad,indices});i.meshUploaded=true;}
    if(i.video.meshDirty){r.setMesh("media.assembly.video.quad",++i.video.meshRevision,{i.video.quad,std::span<const std::uint32_t>(videoIndices)});i.video.meshDirty=false;const bool first=!i.video.meshUploaded;i.video.meshUploaded=true;if(first)i.presentPreview();}
    if(i.textureDirty&&i.shown){
        if(i.shown->deferred()){
            if(i.processor)try{i.renderPreview(r);++i.statistics.previewUploads;}catch(const std::exception&){i.gpuFailed();}
            // Without a processor the CPU preview requested by gpuFailed replaces it.
        }else if(!i.texture.empty()){r.setTexture(i.texture,++i.textureRevision,{i.shown->width,i.shown->height,i.shown->straightRGBA,gpu::TextureColorSpace::sRGB,gpu::TextureFilter::linear});++i.statistics.previewUploads;}
        i.textureDirty=false;}
    i.registration.uploadGeometry(r);i.scene->uploadResources(r);
    i.poseMenus();
    i.eachMenu([&](Impl::Menu&m){auto&group=m.control?*m.control->group:*m.notes->group;
        if(!m.uploaded||(m.notes&&m.notes->dirty)){group.uploadResources(r);m.uploaded=true;if(m.notes)m.notes->dirty=false;if(m.control)m.control->feedbackDirty=false;}
        else if((m.control&&m.control->feedbackDirty)||m.notes){group.updateLocal(r);if(m.control)m.control->feedbackDirty=m.control->animating(i.time);}});
    i.poseMenus();
}
std::span<const gpu::LayerCompositionEntry>MediaAssemblyPreview::entries(){
    auto&i=*impl_;i.count=0;if(!i.pose)return {};for(const auto&e:i.scene->entries()){need(i.count<i.composed.size(),"Media Assembly draw list exceeded its bound");i.composed[i.count++]=e;}
    i.eachMenu([&](Impl::Menu&m){if(!m.uploaded)return;need(i.count<i.composed.size(),"Media Assembly draw list exceeded its bound");i.composed[i.count++]=(m.control?m.control->group:m.notes->group)->entry();});
    need(i.count<i.composed.size(),"Media Assembly draw list exceeded its bound");i.composed[i.count++]={&i.geometry,i.registration.draws()};return {i.composed.data(),i.count};
}
void MediaAssemblyPreview::collected(gpu::Renderer&r){
    auto&i=*impl_;i.scene->collectRetired(r);
    for(auto it=i.dead.begin();it!=i.dead.end();){auto&group=(*it)->control?*(*it)->control->group:*(*it)->notes->group;if(!(*it)->uploaded||group.releaseResources(r))it=i.dead.erase(it);else ++it;}
    if(!i.scene->closingMedia())for(auto it=i.retiredTextures.begin();it!=i.retiredTextures.end();)if(r.removeTexture(*it))it=i.retiredTextures.erase(it);else ++it;
}
void MediaAssemblyPreview::release(gpu::Renderer&r){
    auto&i=*impl_;i.releaseVideo();i.closeMenu(false);for(auto&m:i.closing)i.dead.push_back(std::move(m));i.closing.clear();i.scene->settle();collected(r);
    need(i.dead.empty()&&i.scene->releaseResources(r)&&i.registration.releaseResources(r)&&i.geometry.releaseResources(r),"Media Assembly resources still published at teardown");
    i.retireTexture();i.shown.reset();i.textureDirty=false;
    for(const auto&id:i.retiredTextures)need(r.removeTexture(id),"Media Assembly preview texture still published");i.retiredTextures.clear();
    // Device-bound GPU state follows the renderer; a new renderer re-creates it.
    i.processor.reset();i.playbackProcessor.reset();i.gpuSource.reset();i.gpuTried=i.mediaChecked=false;i.controller->setDeferredPreviews(false,i.time);
    if(i.meshUploaded){need(r.removeMesh("media.assembly.preview.quad"),"Media Assembly preview mesh still published");i.meshUploaded=false;}
    if(i.video.meshUploaded){need(r.removeMesh("media.assembly.video.quad"),"Media Assembly video mesh still published");i.video.meshUploaded=false;}i.renderer=nullptr;
}
}
#endif
