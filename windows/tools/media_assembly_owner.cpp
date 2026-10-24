#include "tools/media_assembly_owner.hpp"
#ifdef _WIN32
#include "core/source_camera.hpp"
#include "native/layer_image_source.hpp"
#include "native/media_assembly_gpu.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace endfield::tools {
namespace {
namespace gpu=native;namespace mod=modules;namespace d=modules::design;using M=core::Matrix4;using P=core::Point;using J=ehud::data::Json;
void need(bool b,const char*why){if(!b)throw std::invalid_argument(why);}
J blank(){return J::Object{{"bounds",J::Array{0,0,590,510}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
constexpr std::array<std::uint32_t,6>quadIndices{0,1,2,0,2,3};
constexpr std::string_view previewMesh="media.assembly.preview.quad",videoMesh="media.assembly.video.quad";
// The drawer's sticker thumbnails (MediaAssemblyAssetCatalog.stickerThumbnail, 96 px).
constexpr unsigned thumbnailPixels=96;
gpu::LayerImageSourceLimits thumbnailLimits(){gpu::LayerImageSourceLimits l;l.cachedImages=32;l.liveImages=64;l.liveBytes=8*1024*1024;return l;}
}
struct MediaAssemblyOwner::Impl:std::enable_shared_from_this<Impl> {
    gpu::LayerRasterizer&raster;app::UtilityExecutor&executor;app::UtilityExecutor::Route route,exportRoute{};d::Context&context;
    std::shared_ptr<mod::MediaAssemblyEngine>engine;MediaAssemblyOwnerOptions options;
    gpu::NativeMediaAssemblyAssets assets;gpu::LayerImageSource thumbnails{thumbnailLimits()};
    std::unique_ptr<mod::MediaAssemblyController>controller;std::unique_ptr<DesignOwnerCore>core;mod::MediaAssemblyCanvas*canvas{};
    // The owner-drawn records composed after the stage's picture slot.
    gpu::LayerScene carrier;std::array<gpu::DrawObject,3>draws;std::size_t drawCount{},insertedCount{SIZE_MAX};const d::Layer*insertedAfter{};
    bool alive{true},overlay{true},presented{},composed{};double time{};
    // Two preview textures alternate (the cross-fade keeps the previous one);
    // a slot no image needs gives its texture back (retired after publication).
    struct Slot {mod::MediaAssemblyStage::Image image;std::string texture;std::uint64_t revision{};std::shared_ptr<gpu::RendererMediaTexture>media;bool dirty{},uploaded{};};
    std::array<Slot,2>slots;std::uint64_t textureSerial{};std::vector<std::string>retiredTextures;gpu::Renderer*renderer{};bool meshUploaded{};
    // GPU previews: the renderer's media device edits the bounded source into
    // a retained media texture (same id while its size is unchanged).
    std::unique_ptr<gpu::NativeMediaAssemblyGpuProcessor>processor;bool gpuTried{},shadersPending{},shadersFailed{},mediaChecked{},mediaCapable{};
    std::shared_ptr<const mod::MediaAssemblyBitmap>gpuSource;
    // Played frames: colour edits in place on the same device, configured once
    // per player (any edit releases the player, like the source).
    std::unique_ptr<gpu::NativeMediaAssemblyGpuProcessor>playbackProcessor;std::uint64_t filteredPlayers{},filteredFrames{};
    struct Dialog {gpu::MediaAssemblyDialogRequest request;std::uint64_t generation{};};std::optional<Dialog>dialog;std::uint64_t dialogGeneration{};
    // Source AVPlayer through the shared broker. One request key; released on
    // pause/edit/crop/hide/close/export exactly like the source releasePlayer.
    // A fresh broker identity per player (source: a new AVPlayer each time),
    // so no paused/play intent is inherited from a released player.
    struct Video {bool requested{},started{};std::string key;std::vector<std::string>retiring;std::uint64_t generation{},revision{},meshRevision{};std::string document;mod::MediaAssemblyAdjustments adjustments;std::uint64_t previews{};
        std::string texture;unsigned width{},height{};std::array<gpu::Vertex,4>quad{};bool meshDirty{},meshUploaded{},shown{};
        // Position from the shared frame clock, re-anchored on each engine
        // progress report; one end-of-range deadline instead of an observer.
        bool playing{};double anchorTime{},anchorPosition{};std::optional<double>endDeadline;
        // The colour-edit filter attached to this player's request, and the
        // frame revision when playback was first seen: the live texture
        // replaces the edited poster only with a frame played after that.
        std::shared_ptr<const std::function<void(void*)>>filter;bool playingSeen{};std::uint64_t playingRevision{};}video;
    // Source preparePlayer/makePlaybackItem: every new player first re-resolves
    // the original and re-checks its identity on the utility queue; a passed
    // check is consumed by exactly one player request for that document.
    struct PlayerCheck {std::uint64_t token{};bool pending{};std::optional<std::string>passed;std::string path;}playerCheck;
    MediaAssemblyOwnerStats statistics;

    Impl(gpu::LayerRasterizer&r,app::UtilityExecutor&q,d::Context&c,std::shared_ptr<mod::MediaAssemblyEngine>e,MediaAssemblyOwnerOptions o)
        :raster(r),executor(q),route(q.makeRoute()),context(c),engine(std::move(e)),options(std::move(o)),assets(options.assetRoot),carrier(r){
        need(engine&&bool(options.uuid),"Media Assembly owner requires its engine and identity source");
        if(options.exportExecutor)exportRoute=options.exportExecutor->makeRoute();
        retiredTextures.reserve(8);
        for(auto&draw:draws)draw.masks.reserve(8);
        options.design.raster.assetRoot=options.assetRoot;options.design.raster.memoryImages=&thumbnails;
        carrier.load(blank(),options.design.raster);
    }
    double now()const{return options.design.now?std::max(time,options.design.now()):time;}
    void clock(double t){need(std::isfinite(t),"Media Assembly owner needs finite time");time=std::max(time,t);}
    void frame(){if(options.design.changed)options.design.changed();}
    void initialize(){
        const std::weak_ptr<Impl>weak=weak_from_this();
        const auto submit=[weak](std::function<void()>work,std::function<void(std::exception_ptr)>done){auto i=weak.lock();if(!i||!i->alive)return false;return i->executor.submit(i->route,std::move(work),std::move(done));};
        mod::MediaAssemblySubmit exportSubmit;
        if(options.exportExecutor)exportSubmit=[weak](std::function<void()>work,std::function<void(std::exception_ptr)>done){auto i=weak.lock();if(!i||!i->alive)return false;return i->options.exportExecutor->submit(i->exportRoute,std::move(work),std::move(done));};
        controller=std::make_unique<mod::MediaAssemblyController>(engine,submit,mod::MediaAssemblyController::Callbacks{
            [weak]{if(auto i=weak.lock();i&&i->alive){if(i->canvas)i->canvas->controllerChanged();if(i->video.requested||i->playerWanted())i->syncPlayback();i->frame();}}, // play / edits act at once
            [weak](std::string_view a){if(auto i=weak.lock();i&&i->alive&&i->options.event)i->options.event(a);},options.uuid},std::move(exportSubmit));
        mod::MediaAssemblyCanvasServices s;
        s.now=[weak]{auto i=weak.lock();return i?i->now():0.;};
        s.uuid=options.uuid;
        s.shelfChoices=options.shelfChoices;
        s.shelfAccess=options.shelfAccess;
        s.chooseFile=[weak]{auto i=weak.lock();if(!i)return false;gpu::MediaAssemblyDialogRequest r;r.kind=gpu::MediaAssemblyDialogRequest::Kind::open;r.extensions=gpu::mediaAssemblyOpenExtensions();return i->requestDialog(std::move(r));};
        s.saveAs=[weak](std::string folder,std::string filename,std::vector<std::string>extensions){
            auto i=weak.lock();if(!i)return false;gpu::MediaAssemblyDialogRequest r;r.kind=gpu::MediaAssemblyDialogRequest::Kind::save;
            r.folder=std::move(folder);r.filename=std::move(filename);r.extensions=std::move(extensions);return i->requestDialog(std::move(r));};
        s.cancelPanel=[weak]{if(auto i=weak.lock())i->dialog.reset();};
        // NSSavePanel already obtained replacement confirmation: overwrite == exists.
        s.fileExists=[](const std::string&path){try{return gpu::mediaAssemblyFileIdentity(gpu::mediaAssemblyPath(path)).has_value();}catch(...){return true;}};
        s.samePath=[weak](const std::string&a,const std::string&b){auto i=weak.lock();return i&&i->engine->samePath(a,b);};
        s.folderOf=[](const std::string&path){return gpu::mediaAssemblyUTF8(gpu::mediaAssemblyPath(path).parent_path());};
        s.filterThumbnail=[weak](mod::MediaAssemblyFilter f)->std::optional<d::ImageSource>{
            auto i=weak.lock();if(!i||f==mod::MediaAssemblyFilter::none)return std::nullopt;const auto&a=i->assets.filterThumbnail(f);return d::ImageSource{d::ImageSource::Kind::asset,a.path,a.sha256,0};};
        s.stickerThumbnail=[weak](mod::MediaAssemblyStickerKind k){auto i=weak.lock();return i?i->stickerThumbnail(k):std::nullopt;};
        s.stickerImage=[weak](mod::MediaAssemblyStickerKind k)->std::optional<d::ImageSource>{auto i=weak.lock();if(!i)return std::nullopt;const auto&a=i->assets.sticker(k);return d::ImageSource{d::ImageSource::Kind::asset,a.path,a.sha256,0};};
        core=std::make_unique<DesignOwnerCore>(raster,context,options.design);
        auto made=std::make_unique<mod::MediaAssemblyCanvas>(context,*controller,std::move(s));canvas=made.get();core->add(std::move(made));
    }
    // A 96 px memory image decoded on first use; nullopt: no thumbnail drawn.
    std::optional<d::ImageSource>stickerThumbnail(mod::MediaAssemblyStickerKind kind){
        const auto key="media.assembly.thumbnail."+std::string(mod::mediaAssemblyStickerItem(kind).id);
        if(!thumbnails.acquire(key,1)){
            try{const auto image=gpu::mediaAssemblyThumbnail(assets,assets.sticker(kind),thumbnailPixels);
                if(!thumbnails.publish(key,1,image.width,image.height,image.straightRGBA))return std::nullopt;++statistics.thumbnailDecodes;}
            catch(const std::exception&){return std::nullopt;}
        }
        return d::ImageSource{d::ImageSource::Kind::memory,key,{},1};
    }
    bool requestDialog(gpu::MediaAssemblyDialogRequest r){
        if(!options.window||!options.dialog)return false;
        dialog=Dialog{std::move(r),++dialogGeneration};
        PostMessageW(options.window,options.dialogMessage,WPARAM(dialog->generation),0);return true;
    }
    bool input()const{return core&&core->inputModule()==core::Module::mediaAssembly;}

    // ---- pictures ----
    // The slots follow the stage's shown and previous preview: a new preview
    // reuses a slot whose preview left (same texture while the size holds);
    // a slot left without a preview gives its texture back.
    void syncSlots(){
        const auto&shown=canvas->stage().image();const auto&previous=canvas->stage().previousImage();
        const auto wanted=[&](const Slot&s){return s.image&&(s.image==shown||s.image==previous);};
        for(const auto*image:{&shown,&previous}){
            if(!*image||std::any_of(slots.begin(),slots.end(),[&](const Slot&s){return s.image==*image;}))continue;
            auto free=std::find_if(slots.begin(),slots.end(),[&](const Slot&s){return s.image&&!wanted(s);});
            if(free==slots.end())free=std::find_if(slots.begin(),slots.end(),[](const Slot&s){return !s.image;});
            if(free==slots.end())continue;
            free->image=*image;free->dirty=true;free->uploaded=false;
        }
        for(auto&slot:slots)if(!wanted(slot)&&(slot.image||!slot.texture.empty())){slot.image.reset();retireSlot(slot);}
    }
    void retireSlot(Slot&slot){
        if(!slot.texture.empty()){retiredTextures.push_back(std::move(slot.texture));slot.texture.clear();}
        if(slot.media){slot.media.reset();if(processor)processor->releaseTargets();}
        slot.uploaded=slot.dirty=false;slot.revision=0;
    }
    // The hidden editor keeps no preview (CPU pixels, deferred sources, textures).
    void releasePictures(){for(auto&slot:slots){slot.image.reset();retireSlot(slot);}if(canvas)canvas->releasePreview();}
    const Slot*ready(const mod::MediaAssemblyStage::Image&image)const{
        if(!image)return nullptr;for(const auto&slot:slots)if(slot.image==image&&slot.uploaded&&!slot.texture.empty())return &slot;return nullptr;}
    // The records follow the picture layer's presented pose (the rotator /
    // picture transition, content fade, stagger, module switch), clipped by
    // the stage; previous preview, incoming preview (its cross-fade), video.
    void poseDraws(){
        drawCount=0;
        const auto pose=core->pose(core::Module::mediaAssembly);
        const d::Layer*after=nullptr;
        if(pose&&canvas->renderedLanguage()){
            auto&stage=canvas->stage();after=&stage.pictureSlot();
            const auto picture=d::sampleLayer(stage.picture(),canvas->root(),time);
            if(picture.visible&&picture.opacity>0){
                const auto box=d::sampleLayer(stage.layer(),canvas->root(),time);
                const gpu::PlaneMask clip{core::source::inverseSourceMatrix(pose->boxWorld*gpu::designMatrix(box.world)),{0,0,mod::MediaAssemblyStage::size.width,mod::MediaAssemblyStage::size.height},0};
                const M world=pose->boxWorld*gpu::designMatrix(picture.world);
                const auto add=[&](std::string_view source,std::string_view mesh,const std::string&texture,double opacity){
                    auto&draw=draws[drawCount++];draw.sourceID=source;draw.meshID=mesh;draw.textureID=texture;draw.world=world;draw.opacity=float(pose->opacity*opacity);
                    draw.masks.assign(pose->ownerMasks.begin(),pose->ownerMasks.end());draw.masks.push_back(clip);draw.shutter=pose->shutter;};
                if(const auto*slot=ready(stage.previousImage()))add("media.assembly.preview.previous",previewMesh,slot->texture,picture.opacity);
                if(const auto*slot=ready(stage.image()))add("media.assembly.preview.draw",previewMesh,slot->texture,d::sampleLayer(stage.pictureFade(),canvas->root(),time).opacity);
                if(stage.videoShown()&&video.shown&&!video.texture.empty()&&video.meshUploaded)add("media.assembly.video.draw",videoMesh,video.texture,picture.opacity);
            }
        }
        statistics.pictureDraws=drawCount;
        if(after==insertedAfter&&drawCount==insertedCount)return;
        insertedAfter=after;insertedCount=drawCount;
        core->setInsertion(core::Module::mediaAssembly,after,after?std::optional(gpu::LayerCompositionEntry{&carrier,std::span<const gpu::DrawObject>(draws.data(),drawCount)}):std::nullopt);
    }
    // ---- video ----
    void releaseVideo(){
        if(!video.requested)return;if(options.broker)options.broker->hideVideos(options.client,time);
        video.requested=video.started=video.shown=video.playing=video.playingSeen=false;video.endDeadline.reset();video.filter.reset();video.retiring.push_back(std::move(video.key));video.key.clear();++statistics.playbackReleases;
        // The released player's frames are no longer filtered (each filter
        // checks its player): its scratch and lookup textures go now.
        if(playbackProcessor)playbackProcessor->releaseResources();
    }
    bool playerWanted()const{
        const auto&s=controller->session();const auto&doc=s.document();
        return options.broker&&options.broker->hasVideoPlayback()&&doc&&doc->video&&s.active()&&s.wantsPlayback()&&!s.cropPreview()&&!s.exporting()&&input();
    }
    // A missing original (or one that is no longer a regular file) asks to be
    // chosen again; a changed original is changedOnDisk. Like the source
    // bookmark, a renamed or moved original is played where it now is. On
    // failure playback stops and the error is shown; a full queue retries on
    // the next playback sync.
    void checkPlayer(const std::shared_ptr<const mod::MediaAssemblyDocumentInfo>&doc){
        if(playerCheck.pending)return;
        const auto token=++playerCheck.token;const std::weak_ptr<Impl>weak=weak_from_this();
        struct Result {std::optional<mod::MediaAssemblyError>error;std::string path;};auto result=std::make_shared<Result>();
        playerCheck.pending=executor.submit(route,[doc,result]{
            try{result->path=gpu::mediaAssemblyLocate(doc->path,doc->identity);const auto identity=gpu::mediaAssemblyFileIdentity(gpu::mediaAssemblyPath(result->path));
                if(!identity)result->error=mod::MediaAssemblyError::sourceUnavailable;else if(*identity!=doc->identity)result->error=mod::MediaAssemblyError::changedOnDisk;}
            catch(const mod::MediaAssemblyFailure&f){result->error=f.error()==mod::MediaAssemblyError::unavailable?mod::MediaAssemblyError::sourceUnavailable:f.error();}
        },[weak,token,doc,result](std::exception_ptr error){
            const auto i=weak.lock();if(!i||!i->alive||token!=i->playerCheck.token)return;i->playerCheck.pending=false;
            const auto&current=i->controller->session().document();if(!current||current->id!=doc->id||!i->playerWanted())return;
            if(error&&!result->error)result->error=mod::MediaAssemblyError::unavailable;
            if(result->error){i->controller->togglePlayback(i->now());i->controller->report(*result->error);}else{i->playerCheck.passed=doc->id;i->playerCheck.path=std::move(result->path);}
            i->syncPlayback();i->frame();
        });
    }
    // The player's state reaches the session directly (playbackSession), not
    // through the controller's change notice: the canvas lays out the stage's
    // play / pause and time again when the playing state or the shown time moved.
    void syncPlayback(){
        const auto&s=controller->session();const bool playing=s.view().playing;const double position=s.currentTime();const bool hadDocument=s.document()!=nullptr;
        syncPlaybackState();
        if(canvas&&hadDocument&&(playing!=s.view().playing||position!=s.currentTime())){canvas->controllerChanged();frame();}
    }
    void syncPlaybackState(){
        auto&s=controller->session();const auto doc=s.document();
        const bool want=playerWanted();
        if(video.requested&&(!want||video.document!=doc->id||video.adjustments!=s.adjustments())){
            const bool resume=want&&video.document==doc->id;releaseVideo();
            // Source: an edit releases the player; the next completed preview
            // prepares a new one when playback is still wanted.
            if(resume)video.previews=controller->completedPreviews();
            if(!want)return;}
        if(!want)return;
        if(!video.requested){
            if(video.previews&&controller->completedPreviews()==video.previews)return;
            if(playerCheck.passed!=doc->id){checkPlayer(doc);return;}
            playerCheck.passed.reset();auto path=std::exchange(playerCheck.path,{});if(path.empty())path=doc->path;
            video.key="media.assembly.video."+std::to_string(++video.revision);gpu::NotesVideoRequest r{video.key,std::move(path),video.revision,1024,doc->duration,{},false};attachFrameFilter(r,s.adjustments(),doc->bt709);
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
        if(playing&&!video.playingSeen){video.playingSeen=true;video.playingRevision=rec->frameRevision;}
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
        video.shown=playing&&!video.texture.empty()&&rec->frameRevision>video.playingRevision;
        if(playing&&video.endDeadline&&time>=*video.endDeadline){
            // forwardPlaybackEndTime: stop at the trim end and show a fresh poster.
            if(rec->wantsPlayback)options.broker->toggleVideo(options.client,video.key,time);controller->playbackSession().playbackEnded();releaseVideo();frame();
        }
    }
    // Source AVVideoComposition for played frames: every BT.709 frame read as
    // Core Image's CoreMedia 709 and the colour edits in its linear working
    // space, in place on the renderer's media device between the engine's
    // transfer and the renderer commit; geometry maps through texture
    // coordinates. A filter cube comes from the deferred preview of the same
    // filter. Without compiled shaders, a media device or that cube, played
    // frames keep the decoded values and the geometry only; paused frames and
    // exports always carry every edit.
    template<class Request>void attachFrameFilter(Request&request,const mod::MediaAssemblyAdjustments&current,bool bt709){
        if constexpr(mediaAssemblyFrameFilterHook<Request>){
            if(!renderer||!gpu::mediaAssemblyGpuShadersReady())return;
            std::span<const std::uint8_t>lookup;
            if(current.filter!=mod::MediaAssemblyFilter::none){
                const auto shown=controller->previewImage();
                if(!shown||!shown->deferred()||shown->edits->filter!=current.filter||shown->lookup.empty())return;
                lookup=shown->lookup;
            }
            // Identity colour edits of a movie read as decoded need no pass.
            const auto colour=bt709?mod::mediaAssemblyVideoColour():nullptr;
            if(!colour&&mod::MediaAssemblyColorPipeline(current,lookup,true,false).identity())return;
            try{
                if(!playbackProcessor)playbackProcessor=std::make_unique<gpu::NativeMediaAssemblyGpuProcessor>(renderer->mediaDevice());
                playbackProcessor->configure(current,lookup,true);playbackProcessor->setColour(colour);
            }catch(const std::exception&){playbackProcessor.reset();return;}
            const std::weak_ptr<Impl>weak=weak_from_this();const auto player=video.revision;
            if(attachMediaAssemblyFrameFilter(request,[weak,player](void*surface){
                const auto i=weak.lock();if(!i||!i->alive||!i->playbackProcessor||!surface)return;
                // Only this player's frames: a released one parked the processor.
                if(!i->video.requested||i->video.revision!=player)return;
                // A device failure leaves the remaining frames geometry-only.
                try{i->playbackProcessor->processInPlace(static_cast<IDXGISurface*>(surface));++i->filteredFrames;}catch(const std::exception&){i->playbackProcessor.reset();}
            })){++filteredPlayers;video.filter=request.frameFilter;}
        }else{(void)request;(void)current;(void)bt709;}
    }
    // ---- GPU ----
    // Source setActive(false) -> engine.clearCaches (CIContext caches): the
    // hidden editor keeps no decoded source on the CPU or the GPU. The
    // processors keep only their device programs.
    void releaseGpu(){gpuSource.reset();if(processor)processor->releaseResources();if(playbackProcessor)playbackProcessor->releaseResources();}
    // The HUD closed: the processors themselves go too (each holds the
    // renderer's device, Renderer::mediaDevice); the next showing creates
    // them again (build-time programs, no compile).
    void dropProcessors(){processor.reset();playbackProcessor.reset();gpuSource.reset();if(gpuTried&&mediaCapable&&!shadersFailed)gpuTried=false;}
    void gpuFailed(){for(auto&slot:slots)if(slot.media)retireSlot(slot);processor.reset();gpuSource.reset();controller->setDeferredPreviews(false,now());}
    void createProcessor(gpu::Renderer&r){
        try{processor=std::make_unique<gpu::NativeMediaAssemblyGpuProcessor>(r.mediaDevice());}catch(const std::exception&){processor.reset();}
        controller->setDeferredPreviews(processor!=nullptr,now());
    }
    // Edits a deferred source on the renderer's device into the slot's media
    // texture; a new id only when the edited size changes.
    void renderPreview(gpu::Renderer&r,Slot&slot){
        const auto&image=*slot.image;
        if(image.source!=gpuSource){processor->setSource(image.source->width,image.source->height,image.source->straightRGBA,std::size_t(image.source->width)*4,false,0,image.source->colour);gpuSource=image.source;}
        const auto plan=processor->configure(*image.edits,image.lookup,false);
        if(!slot.media||!slot.media->valid()||slot.media->width()!=plan.outputWidth||slot.media->height()!=plan.outputHeight){
            retireSlot(slot);slot.texture="media.assembly.preview."+std::to_string(++textureSerial);slot.media=r.createMediaTexture(slot.texture,plan.outputWidth,plan.outputHeight);++statistics.gpuTargets;
        }
        processor->render(static_cast<IDXGISurface*>(slot.media->targetSurface()));r.commitMediaTexture(*slot.media);++statistics.gpuPreviews;
    }
    void uploadSlot(gpu::Renderer&r,Slot&slot){
        if(slot.image->deferred()){
            // Without a processor the CPU preview requested by gpuFailed replaces it.
            if(!processor)return;
            try{renderPreview(r,slot);slot.uploaded=true;++statistics.previewUploads;}catch(const std::exception&){gpuFailed();return;}
        }else{
            // CPU pixels and GPU media targets never share an id.
            if(slot.media||slot.texture.empty()){retireSlot(slot);slot.texture="media.assembly.preview."+std::to_string(++textureSerial);}
            r.setTexture(slot.texture,++slot.revision,{slot.image->width,slot.image->height,slot.image->straightRGBA,gpu::TextureColorSpace::sRGB,gpu::TextureFilter::linear});
            slot.uploaded=true;++statistics.previewUploads;
        }
        slot.dirty=false;
    }
    // After each frame's core update: presentation edges, the player, the slots and the records.
    void afterFrame(){
        const bool nowPresented=canvas->isPresented();
        if(nowPresented!=presented){presented=nowPresented;if(!presented)releaseGpu();}
        composed=core->pose(core::Module::mediaAssembly).has_value();
        if(!composed&&!presented)releasePictures();
        if(video.requested||playerWanted())syncPlayback();
        syncSlots();poseDraws();
    }
    void removeRetired(gpu::Renderer&r){for(auto it=retiredTextures.begin();it!=retiredTextures.end();)if(r.removeTexture(*it))it=retiredTextures.erase(it);else ++it;}
};

MediaAssemblyOwner::MediaAssemblyOwner(gpu::LayerRasterizer&r,app::UtilityExecutor&q,d::Context&c,std::shared_ptr<mod::MediaAssemblyEngine>e,MediaAssemblyOwnerOptions o)
    :impl_(std::make_shared<Impl>(r,q,c,std::move(e),std::move(o))){impl_->initialize();}
MediaAssemblyOwner::~MediaAssemblyOwner(){
    auto i=impl_;try{i->releaseVideo();}catch(...){}i->alive=false;i->executor.invalidate(i->route);
    if(i->options.exportExecutor)i->options.exportExecutor->invalidate(i->exportRoute,false);
    i->controller->cancelExport();
    // The canvas goes before the controller it observes.
    i->canvas=nullptr;i->core.reset();
    // The hidden player channel is retired, then the client detached (a client
    // still holding a retiring record stays attached until the broker goes).
    if(i->options.broker&&i->options.client)try{(void)i->options.broker->collectRetired();(void)i->options.broker->detachClient(i->options.client);}catch(...){}
}
std::string_view MediaAssemblyOwner::name()const noexcept{return impl_->options.design.name;}
app::ModuleRouting MediaAssemblyOwner::routing()const noexcept{return impl_->options.design.routing;}
bool MediaAssemblyOwner::presents(core::Module m)const noexcept{return m==core::Module::mediaAssembly;}
void MediaAssemblyOwner::resize(const app::ClientMetrics&m){if(m.pixelWidth&&m.pixelHeight)impl_->core->resize(m);}
void MediaAssemblyOwner::setAppearance(const app::ModuleAppearance&a,double t){auto&i=*impl_;i.clock(t);i.core->setAppearance(a,t);}
void MediaAssemblyOwner::setOverlayVisible(bool v,double t){
    auto&i=*impl_;i.clock(t);i.overlay=v;i.core->setOverlayVisible(v,t);
    if(!v){i.dialog.reset();i.afterFrame();}
}
void MediaAssemblyOwner::overlayClosing(double t){auto&i=*impl_;i.clock(t);i.core->overlayClosing(t);i.dialog.reset();i.afterFrame();}
void MediaAssemblyOwner::update(const app::ModuleFrame&f){auto&i=*impl_;i.clock(f.time);i.core->update(f);i.afterFrame();}
void MediaAssemblyOwner::prewarm(const app::ModuleFrame&f){auto&i=*impl_;i.clock(f.time);i.core->prewarm(f);}
bool MediaAssemblyOwner::requiresFrames(double t)const{return impl_->core->requiresFrames(t);}
std::optional<double>MediaAssemblyOwner::nextWakeTime(double t)const{
    const auto&i=*impl_;auto out=i.core->nextWakeTime(t);
    for(const auto v:{i.controller->nextWakeTime(),i.video.endDeadline})if(v&&(!out||*v<*out))out=v;
    return out;
}
bool MediaAssemblyOwner::deadline(double t){
    auto&i=*impl_;i.clock(t);bool did=i.controller->deadline(i.time);
    if(i.video.endDeadline&&i.time>=*i.video.endDeadline){i.syncPlayback();did=true;}
    did=i.core->deadline(t)||did;
    if(did)i.frame();return did;
}
void MediaAssemblyOwner::upload(gpu::Renderer&r){
    auto&i=*impl_;need(!i.renderer||i.renderer==&r,"Media Assembly resources belong to another renderer");i.renderer=&r;
    // GPU previews start once the editor is first shown: with build-time
    // programs at once, otherwise after the HLSL compile on the utility
    // worker (no UI-thread hitch); until then, or without same-device media
    // support, previews use the CPU path. Not while the HUD is closed: the
    // processors would hold the closed HUD's device.
    if(i.presented&&!i.gpuTried&&i.options.gpuPreview&&i.overlay){
        if(!i.mediaChecked){i.mediaChecked=true;try{i.mediaCapable=r.mediaDevice()!=nullptr;}catch(const std::exception&){i.mediaCapable=false;}}
        if(!i.mediaCapable){i.gpuTried=true;i.controller->setDeferredPreviews(false,i.now());}
        else if(gpu::mediaAssemblyGpuShadersReady()){i.gpuTried=true;i.createProcessor(r);}
        else if(!i.shadersPending){
            const std::weak_ptr<Impl>weak=i.weak_from_this();
            i.shadersPending=i.executor.submit(i.route,[]{gpu::prepareMediaAssemblyGpuShaders();},[weak](std::exception_ptr error){
                const auto self=weak.lock();if(!self||!self->alive)return;self->shadersPending=false;if(error){self->shadersFailed=true;self->gpuTried=true;}self->frame();});
        }
    }
    if(i.shadersFailed&&!i.processor&&i.controller->deferredPreviews())i.controller->setDeferredPreviews(false,i.now());
    if(!i.meshUploaded){static constexpr std::array<gpu::Vertex,4>quad{{{{0,0,0},{0,0}},{{1,0,0},{1,0}},{{1,1,0},{1,1}},{{0,1,0},{0,1}}}};r.setMesh(std::string(previewMesh),1,{quad,quadIndices});i.meshUploaded=true;}
    if(i.video.meshDirty){r.setMesh(std::string(videoMesh),++i.video.meshRevision,{i.video.quad,std::span<const std::uint32_t>(quadIndices)});i.video.meshDirty=false;i.video.meshUploaded=true;}
    for(auto&slot:i.slots)if(slot.image&&slot.dirty)i.uploadSlot(r,slot);
    i.core->upload(r);
    i.poseDraws();
}
std::span<const gpu::LayerCompositionEntry>MediaAssemblyOwner::entries(core::Module m){return impl_->core->entries(m);}
void MediaAssemblyOwner::collected(gpu::Renderer&r){auto&i=*impl_;i.renderer=&r;i.core->collected(r);i.removeRetired(r);}
void MediaAssemblyOwner::release(gpu::Renderer&r){
    auto&i=*impl_;i.releaseVideo();i.insertedCount=SIZE_MAX;i.insertedAfter=nullptr;i.core->setInsertion(core::Module::mediaAssembly,nullptr,std::nullopt);
    i.core->release(r);
    need(i.carrier.releaseResources(r),"Media Assembly picture records still published at teardown");
    for(auto&slot:i.slots){slot.image.reset();i.retireSlot(slot);}
    for(const auto&id:i.retiredTextures)need(r.removeTexture(id),"Media Assembly preview texture still published");i.retiredTextures.clear();
    // Device-bound GPU state follows the renderer; a new renderer re-creates it.
    i.processor.reset();i.playbackProcessor.reset();i.gpuSource.reset();i.gpuTried=i.mediaChecked=false;i.controller->setDeferredPreviews(false,i.now());
    if(i.meshUploaded){need(r.removeMesh(std::string(previewMesh)),"Media Assembly preview mesh still published");i.meshUploaded=false;}
    if(i.video.meshUploaded){need(r.removeMesh(std::string(videoMesh)),"Media Assembly video mesh still published");i.video.meshUploaded=false;}i.renderer=nullptr;
}
bool MediaAssemblyOwner::covers(core::Point p)const{return impl_->core->covers(p);}
bool MediaAssemblyOwner::pointerLocked()const{return impl_->core->pointerLocked();}
bool MediaAssemblyOwner::capturesPointer()const{return impl_->core->capturesPointer();}
bool MediaAssemblyOwner::capturesWheel()const{return impl_->core->capturesWheel();}
bool MediaAssemblyOwner::capturesKeys()const{return impl_->core->capturesKeys();}
bool MediaAssemblyOwner::suppressesKeyFilters()const{return impl_->core->suppressesKeyFilters();}
bool MediaAssemblyOwner::pointer(const app::PointerEvent&e,double t){auto&i=*impl_;i.clock(t);return i.core->pointer(e,t);}
bool MediaAssemblyOwner::wheel(const app::WheelEvent&e,double t){return wheel(e,GetKeyState(VK_MENU)<0,t);}
bool MediaAssemblyOwner::wheel(const app::WheelEvent&e,bool alt,double t){
    auto&i=*impl_;i.clock(t);
    // No hasPreciseScrollingDeltas on Windows: a precision touchpad sends fractional detents.
    i.canvas->setWheelKind(std::isfinite(e.steps)&&std::abs(e.steps-std::round(e.steps))>1e-9,alt);
    return i.core->wheel(e,t);
}
bool MediaAssemblyOwner::key(const app::KeyEvent&e,double t){auto&i=*impl_;i.clock(t);return i.core->key(e,t);}
bool MediaAssemblyOwner::key(const app::KeyEvent&e,std::uint32_t modifiers,double t){auto&i=*impl_;i.clock(t);return i.core->key(e,modifiers,t);}
bool MediaAssemblyOwner::filterKey(const app::NativeMessage&m){return impl_->core->filterKey(m);}
bool MediaAssemblyOwner::message(const app::NativeMessage&m,double t){
    auto i=impl_;i->clock(t);
    if(m.message!=i->options.dialogMessage)return i->core->message(m,t);
    if(!i->dialog||m.wParam!=WPARAM(i->dialog->generation))return false;
    const auto d=std::move(*i->dialog);i->dialog.reset();++i->statistics.dialogs;
    // The modal dialog keeps this UI thread; the HUD stays up (presentingPanel).
    const auto chosen=i->options.dialog?i->options.dialog(i->options.window,d.request):std::nullopt;
    if(!i->alive)return true;
    i->canvas->panelFinished(chosen,i->now());i->frame();return true;
}
void MediaAssemblyOwner::focus(bool v,double t){impl_->core->focus(v,t);}
void MediaAssemblyOwner::cancelInteraction(double t){impl_->core->cancelInteraction(t);}
bool MediaAssemblyOwner::finishEditing(double t){return impl_->core->finishEditing(t);}
bool MediaAssemblyOwner::flush(double t){return impl_->core->flush(t);}
bool MediaAssemblyOwner::hostEvent(const app::HostEvent&e,double t){
    auto&i=*impl_;i.clock(t);const bool changed=i.core->hostEvent(e,t);
    // The HUD closed: no preview, texture or processor is kept (the source
    // canvas layers die with the HUD; the session survives).
    if(e.kind==app::HostEvent::Kind::overlayConcealed){i.releaseVideo();i.releasePictures();i.dropProcessors();i.poseDraws();}
    return changed;
}
bool MediaAssemblyOwner::drop(std::span<const std::string>paths,core::Point,double t){
    auto&i=*impl_;i.clock(t);
    if(!i.input()||paths.empty()||i.canvas->capturesPointer()||i.canvas->presentingPanel()||i.canvas->isExporting())return false;
    i.canvas->importFiles(paths,i.now());i.frame();return true;
}
std::optional<app::ModuleCursor>MediaAssemblyOwner::cursorAt(core::Point p)const{return impl_->core->cursorAt(p);}
bool MediaAssemblyOwner::automationRefresh(IRawElementProviderFragmentRoot*r){return impl_->core->automationRefresh(r);}
std::size_t MediaAssemblyOwner::automationCount()const{return impl_->core->automationCount();}
long MediaAssemblyOwner::automationChild(std::size_t n,IRawElementProviderFragment**out)const{return impl_->core->automationChild(n,out);}
long MediaAssemblyOwner::automationFromPoint(double x,double y,IRawElementProviderFragment**out)const{return impl_->core->automationFromPoint(x,y,out);}
void MediaAssemblyOwner::automationDisconnect()noexcept{impl_->core->automationDisconnect();}
std::optional<core::Module>MediaAssemblyOwner::automationModule()const noexcept{return impl_->core->automationModule();}
app::ModuleResidency MediaAssemblyOwner::residency()const{return impl_->core->residency();}
bool MediaAssemblyOwner::refreshSharedMedia(double t){
    auto i=impl_;i->clock(t);if(!i->options.broker||(!i->video.requested&&i->video.retiring.empty()))return false;
    const bool before=i->video.shown;const auto position=i->controller->session().currentTime();i->syncPlayback();
    for(auto it=i->video.retiring.begin();it!=i->video.retiring.end();){const auto*rec=i->options.broker->findVideo(i->options.client,*it);
        if(!rec||(!rec->visible&&i->options.broker->retireVideo(i->options.client,*it)))it=i->video.retiring.erase(it);else ++it;}
    const bool moved=before!=i->video.shown||position!=i->controller->session().currentTime();if(moved)i->frame();return moved;
}
mod::MediaAssemblyCanvas&MediaAssemblyOwner::canvas()const noexcept{return *impl_->canvas;}
DesignOwnerCore&MediaAssemblyOwner::core()const noexcept{return *impl_->core;}
const mod::MediaAssemblyController&MediaAssemblyOwner::controller()const noexcept{return *impl_->controller;}
std::optional<gpu::MediaAssemblyDialogRequest>MediaAssemblyOwner::pendingDialog()const{return impl_->dialog?std::optional(impl_->dialog->request):std::nullopt;}
std::shared_ptr<const std::function<void(void*)>>MediaAssemblyOwner::playbackFrameFilter()const noexcept{return impl_->video.filter;}
MediaAssemblyOwnerStats MediaAssemblyOwner::stats()const noexcept{
    const auto&i=*impl_;auto s=i.statistics;s.videoShown=i.video.shown;s.gpuActive=i.processor!=nullptr;
    s.filteredPlayers=i.filteredPlayers;s.filteredFrames=i.filteredFrames;s.frameFilterHook=mediaAssemblyFrameFilterHook<gpu::NotesVideoRequest>;s.frameFilterRoute=mediaAssemblyBrokerFilterRoute<core::ScopedMediaRequest>;
    s.retainedTextures=i.retiredTextures.size();for(const auto&slot:i.slots)s.retainedTextures+=!slot.texture.empty();
    if(const auto scene=i.core->sceneStats(core::Module::mediaAssembly)){s.sceneParts=scene->parts;s.scenePixelBytes=scene->residentPixelBytes;s.parks=scene->parks;}
    // CPU pixels the owner and the stage hold: shown previews (or their deferred sources) and the GPU source.
    std::vector<const void*>counted;
    const auto count=[&](const void*key,std::size_t bytes){if(!key||std::find(counted.begin(),counted.end(),key)!=counted.end())return;counted.push_back(key);s.previewPixelBytes+=bytes;};
    for(const auto&image:{i.canvas->stage().image(),i.canvas->stage().previousImage()})if(image){
        if(image->deferred())count(image->source.get(),image->source->straightRGBA.size());else count(image.get(),image->straightRGBA.size());}
    if(i.gpuSource)count(i.gpuSource.get(),i.gpuSource->straightRGBA.size());
    s.gpuResidentBytes=i.processor?i.processor->stats().residentBytes:0;s.playbackResidentBytes=i.playbackProcessor?i.playbackProcessor->stats().residentBytes:0;
    s.thumbnailBytes=i.thumbnails.stats().cachedBytes;
    return s;
}
}
#endif
