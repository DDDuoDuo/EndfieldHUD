#include "tools/profile_owner.hpp"
#ifdef _WIN32
#include "modules/id_card_artwork.hpp"
#include "native/centre_backdrop_ramp.hpp"
#include "native/id_card_captions.hpp"
#include "native/profile_text.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace endfield::tools {
namespace {
namespace m=modules;namespace gpu=native;namespace d=modules::design;using M=core::Matrix4;using J=ehud::data::Json;
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}
constexpr std::size_t avatarTexture=0,backgroundTexture=1,hoverTexture=2;
constexpr std::string_view artMesh="profile.art.quad",artTexture="profile.art.photo",rampTexture="profile.art.ramp",frameKey="profile.frame",portraitKey="profile.portrait";
constexpr std::array<std::uint32_t,6>quadIndices{0,1,2,0,2,3};
J blank(){return J::Object{{"bounds",J::Array{0,0,590,510}},{"position",J::Array{0,0}},{"anchorPoint",J::Array{0,0}},{"children",J::Array{}}};}
// The page's own memory images: the tinted frame (254 px) and one panning
// portrait of up to 2048 px (two generations while a step changes).
gpu::LayerImageSourceLimits imageLimits(){gpu::LayerImageSourceLimits l;l.cachedImages=4;l.liveImages=8;l.liveBytes=40*1024*1024;l.dimension=2048;return l;}
}
struct ProfileOwner::Impl:std::enable_shared_from_this<Impl> {
    HWND hwnd;app::UtilityExecutor&executor;gpu::LayerRasterizer&raster;d::Context&context;const m::WorkModeController&work;
    ProfileOwnerOptions options;
    m::ProfileTextRules text;m::ProfileDateRules dates;m::ProfileSourceArtwork artwork;m::IdCardSource cardSource;
    std::shared_ptr<const m::IdCardArtwork>cardArtwork;
    gpu::LayerImageSource images{imageLimits()};
    std::unique_ptr<m::ProfileService>service;std::unique_ptr<gpu::ProfileImageService>imageService;
    // Declaration order: the core (and its canvas) borrows the state and is destroyed first.
    std::unique_ptr<m::ProfileState>state;std::unique_ptr<DesignOwnerCore>core;m::ProfileCanvas*canvas{};
    std::uint64_t stateObserver{};
    m::WorkModeProfileHours hours;m::ProfileCropRecorder crops;std::optional<m::IdCardBinding>card;
    std::optional<m::PersonalProfile>preview; // a slider drag's live geometry for the shell card
    app::ModuleAppearance appearance;bool recording{},overlay{},alive{true},started{},cropBaseline{};
    ehud::data::ProfileSyncFields locked{};double time{};std::optional<std::string>loadError;
    std::optional<std::function<void(m::PersonalProfile&)>>pendingGame;
    // Managed images currently referenced by the committed record and their decodes.
    std::optional<std::string>avatarName,backgroundName;bool imagesKnown{},avatarFailed{};
    std::shared_ptr<const gpu::ProfileDecodedImage>avatar,background;
    // Pickers: the host's chooser (asynchronous) and the modal colour dialog (posted).
    bool choosing{},coloring{};std::uint64_t chooserGeneration{};WPARAM pickerGeneration{1};std::optional<std::array<double,3>>pendingColor;
    // Card textures, generated on the utility worker; one job at a time.
    struct CardWorker {std::shared_ptr<const gpu::ProfileDecodedImage>source;std::shared_ptr<const m::ProfileImage>working;};
    std::shared_ptr<CardWorker>cardWorker=std::make_shared<CardWorker>();
    app::UtilityExecutor::Route cardRoute,portraitRoute;bool cardBusy{},cardDirty{};
    std::array<std::optional<IdCardTexture>,3>cardTextures;std::array<std::uint64_t,3>cardRequested{};
    // The tinted avatar frame (frameImage(accent:)) and the panning portraits.
    std::optional<std::array<double,3>>frameAccent;std::uint64_t frameRevision{},portraitRevision{};
    struct Portrait {int step{};double scale{};m::ProfilePortrait image;std::size_t bytes{};};
    std::optional<Portrait>portrait;std::optional<std::pair<int,double>>portraitWanted,portraitRunning;
    std::shared_ptr<const gpu::ProfileDecodedImage>portraitSource;std::shared_ptr<const m::ProfileImage>portraitWorking;
    // The art record (owner-drawn photo, inserted after the art slot's part).
    gpu::LayerScene carrier;std::array<gpu::DrawObject,1>draws;std::size_t drawCount{},insertedCount{SIZE_MAX};const d::Layer*insertedAfter{};
    std::shared_ptr<const gpu::LayerRasterImage>ramp;bool rampUploaded{},artUploaded{},meshUploaded{},artWanted{};std::uint64_t artRevision{},meshRevision{};
    std::shared_ptr<const gpu::ProfileDecodedImage>artSource;std::array<gpu::Vertex,4>quad{};gpu::Renderer*renderer{};
    ProfileOwnerStats statistics;

    Impl(HWND h,app::UtilityExecutor&e,gpu::LayerRasterizer&r,d::Context&c,const m::WorkModeController&w,ProfileOwnerOptions o)
        :hwnd(h),executor(e),raster(r),context(c),work(w),options(std::move(o)),
         text(gpu::nativeProfileTextRules()),dates(gpu::nativeProfileDateRules(options.timeZone)),
         artwork(m::loadProfileSourceArtwork(options.resources)),cardSource(m::loadIdCardSource(options.resources)),
         cardArtwork(std::make_shared<const m::IdCardArtwork>(artwork.card)),cardRoute(e.makeRoute()),portraitRoute(e.makeRoute()),carrier(r){
        need(IsWindow(h)!=FALSE,"Personal ID owner requires the caller's HWND");
        need(options.pickerMessage>=WM_APP&&options.pickerMessage<=0xbfff,"Profile picker requests use their own WM_APP message");
        draws[0].masks.reserve(8);appearance.accentSRGB={250./255,212./255,31./255};
        options.design.raster.memoryImages=&images;
        carrier.load(blank(),options.design.raster);
    }
    ~Impl(){executor.invalidate(cardRoute,true);executor.invalidate(portraitRoute,true);}
    void initialize(){
        const std::weak_ptr<Impl>weak=shared_from_this();
        service=std::make_unique<m::ProfileService>(options.appRoot,executor,m::ProfileServiceCallbacks{
            [weak](const m::PersonalProfile&p){if(auto i=weak.lock();i&&i->alive)i->loaded(p);},
            [weak](m::ProfileFailure code,std::string detail){if(auto i=weak.lock();i&&i->alive)i->loadFailed(code,detail);},
            [weak](m::ProfileFailure code,std::string detail){if(auto i=weak.lock();i&&i->alive&&i->canvas)i->canvas->showError(m::profileFailureMessage(code,i->language(),detail));},
            {}});
        imageService=std::make_unique<gpu::ProfileImageService>(options.appRoot/"Profile"/"Images",executor,gpu::ProfileImageCallbacks{
            [weak](m::ProfileImageKind kind,std::string filename){if(auto i=weak.lock();i&&i->alive)i->imported(kind,std::move(filename));},
            [weak](m::ProfileImageKind,m::ProfileFailure failure,std::string detail){if(auto i=weak.lock();i&&i->alive&&i->canvas)i->canvas->showError(m::profileFailureMessage(failure,i->language(),detail));},
            [weak](const std::string&filename,std::shared_ptr<const gpu::ProfileDecodedImage>image){if(auto i=weak.lock();i&&i->alive)i->decoded(filename,std::move(image));},
            [weak](const std::string&filename,m::ProfileFailure){if(auto i=weak.lock();i&&i->alive)i->decoded(filename,nullptr);}});
        auto design=options.design;
        design.appearance=[weak](const app::ModuleAppearance&a,double t){if(auto i=weak.lock())i->setAppearance(a,t);};
        design.hostEvent=[weak](const app::HostEvent&e,double t){auto i=weak.lock();return i&&i->hostEvent(e,t);};
        design.message=[weak](const app::NativeMessage&msg,double t){auto i=weak.lock();return i&&i->message(msg,t);};
        design.updated=[weak](const app::ModuleFrame&f){if(auto i=weak.lock())i->afterFrame(f);};
        design.closing=[weak](double t){if(auto i=weak.lock()){i->advance(t);i->cancelPickers();}};
        design.flush=[weak](double t){auto i=weak.lock();return !i||i->flush(t);};
        core=std::make_unique<DesignOwnerCore>(raster,context,std::move(design));
        auto owned=std::make_unique<m::ProfileCanvas>(context,text,dates,services());
        canvas=owned.get();
        core->add(std::move(owned));
    }
    core::Language language()const{return context.env().language;}
    // The shell card's language: the appearance's, System as English (v1.2.0 Windows rule).
    core::Language cardLanguage()const{return appearance.language==core::Language::system?core::Language::english:appearance.language;}
    void advance(double now){need(std::isfinite(now),"Personal ID needs a finite continuous clock");time=std::max(time,now);}
    void notify(){if(alive&&options.design.changed)options.design.changed();}
    void notifyCard(){if(alive&&options.cardChanged)options.cardChanged();}
    double scale()const{return context.boxScale();}

    // ---- The canvas's services -------------------------------------------
    m::ProfileCanvasServices services(){
        const std::weak_ptr<Impl>weak=shared_from_this();
        m::ProfileCanvasServices s;
        s.workSeconds=[weak]{const auto i=weak.lock();return i?i->hours.totalSeconds(i->work,i->time):0.;};
        s.minigameBest=[weak]{const auto i=weak.lock();return i&&i->options.minigameBest?i->options.minigameBest():std::int64_t{};};
        s.geometryPreview=[weak](const m::PersonalProfile&p){if(auto i=weak.lock()){i->preview=p;i->updateCard();}};
        s.portrait=[weak](int step){const auto i=weak.lock();return i?i->portraitFor(step):m::ProfileCanvasServices::Portrait{};};
        s.backgroundReady=[weak]{const auto i=weak.lock();return i&&i->background&&i->backgroundName;};
        s.frame=[weak](std::array<double,3>accent){const auto i=weak.lock();return i?i->frameFor(accent):std::nullopt;};
        s.chooseImage=[weak](m::ProfileImageKind kind){const auto i=weak.lock();return i&&i->requestImage(kind);};
        s.chooseColor=[weak](std::array<double,3>initial){const auto i=weak.lock();return i&&i->requestColor(initial);};
        s.dismissPanels=[weak]{if(auto i=weak.lock())i->cancelPickers();};
        s.importImage=[weak](m::ProfileImageKind kind,const std::string&path){
            const auto i=weak.lock();if(!i||!i->state||i->loadError)return false;
            return i->imageService->import(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()),path.size())),kind);};
        return s;
    }
    // HUDPortraitArtwork.frameImage(accent:): the selected source frame tinted, published once per accent.
    std::optional<d::ImageSource>frameFor(std::array<double,3>accent){
        if(frameAccent!=accent){
            const auto tinted=m::profileStraight(m::profileTintedFrame(artwork.frame,accent));
            (void)images.publish(frameKey,++frameRevision,tinted.width,tinted.height,tinted.rgba);
            frameAccent=accent;
        }
        return d::ImageSource{d::ImageSource::Kind::memory,std::string(frameKey),{},frameRevision};
    }
    // preparePortrait: the panning image of `step` at the current render scale,
    // made on the worker (one job at a time, the latest wanted step next).
    m::ProfileCanvasServices::Portrait portraitFor(int step){
        m::ProfileCanvasServices::Portrait out;
        if(!avatarName||loadError)return out;
        if(!avatar){out.pending=!avatarFailed;return out;}
        if(portrait)out.image=portrait->image;
        const std::pair wanted{step,scale()};
        if((!portrait||portrait->step!=step||portrait->scale!=wanted.second)&&portraitRunning!=wanted){portraitWanted=wanted;submitPortrait();}
        else if(portraitRunning==wanted)portraitWanted.reset(); // the running job is the one wanted
        if(!out.image)out.pending=true;
        return out;
    }
    void submitPortrait(){
        if(portraitRunning||!portraitWanted||!avatar)return;
        const auto [step,density]=*portraitWanted;portraitWanted.reset();
        if(portraitSource!=avatar){portraitWorking=gpu::profileAvatarWorking(avatar);portraitSource=avatar;}
        auto result=std::make_shared<std::optional<m::ProfilePanningImage>>();
        const auto source=portraitWorking;const int orientation=avatar->orientation;const auto weak=weak_from_this();const auto name=avatarName;
        const bool accepted=executor.submit(portraitRoute,[source,orientation,step,density,result]{
            *result=m::profilePanningImage(*source,orientation,m::ProfileCanvas::wellFrame.width,step,density);
        },[weak,result,step,density,name](std::exception_ptr error){
            const auto i=weak.lock();if(!i||!i->alive)return;i->portraitRunning.reset();
            if(!error&&*result&&i->avatarName==name){
                const auto straight=m::profileStraight((*result)->image);
                (void)i->images.publish(portraitKey,++i->portraitRevision,straight.width,straight.height,straight.rgba);
                i->portrait=Portrait{step,density,{{d::ImageSource::Kind::memory,std::string(portraitKey),{},i->portraitRevision},(*result)->fillWidth,(*result)->fillHeight,step},straight.rgba.size()};
                ++i->statistics.portraitPublications;i->statistics.portraitBytes=straight.rgba.size();
                if(i->canvas)i->canvas->imagesChanged();
                i->notify();
            }
            i->submitPortrait();
        });
        if(accepted){portraitRunning=std::pair{step,density};++statistics.portraitJobs;}
        else portraitWanted=std::pair{step,density}; // retried on queue capacity
    }
    // ---- Load ------------------------------------------------------------
    void loaded(const m::PersonalProfile&p){
        const std::weak_ptr<Impl>weak=shared_from_this();
        state=std::make_unique<m::ProfileState>(p,text,dates,service->persistence(
            [weak]{const auto i=weak.lock();return i?i->locked:ehud::data::ProfileSyncFields{};}));
        state->setSyncLocked(locked);
        // The read-only Work Mode controller may already hold the launch
        // lifetime: the stored record minus that restore is the offset, and
        // early checkpoints land once.
        hours.profileLoadedObserved(work,p.accumulatedWorkSeconds,[weak](double seconds){const auto i=weak.lock();return i&&i->state&&i->state->setWorkSeconds(seconds);});
        card.emplace(cardSource,gpu::nativeIdCardMeasure(raster,options.design.raster));
        stateObserver=state->observe([weak]{if(auto i=weak.lock())i->sync();});
        canvas->setStore(state.get());
        if(auto apply=std::exchange(pendingGame,std::nullopt)){try{applyGame(*apply);}catch(const std::exception&){/* the next account refresh retries */}}
        sync();notify();
        if(alive&&state&&options.loaded)options.loaded(state->profile());
    }
    void loadFailed(m::ProfileFailure code,const std::string&detail){
        // Source canvas with a nil store: defaults, the store's message, no persistence.
        loadError=m::profileFailureMessage(code,language(),detail);pendingGame.reset();
        canvas->setStoreError(ehud::data::Profile::defaults(),*loadError);
        notify();
    }
    // ---- Content events --------------------------------------------------
    void sync(){
        if(!state||!alive||loadError)return;
        const auto&p=state->profile();
        const bool first=!imagesKnown;imagesKnown=true;
        if(p.avatarFilename!=avatarName){
            avatarName=p.avatarFilename;avatar.reset();avatarFailed=false;portrait.reset();portraitWanted.reset();
            if(avatarName)imageService->request(*avatarName,m::ProfileImageKind::avatar);
            if(!first&&options.avatarChanged)options.avatarChanged(avatarName);
        }
        if(p.backgroundFilename!=backgroundName){
            backgroundName=p.backgroundFilename;background.reset();
            if(backgroundName)imageService->request(*backgroundName,m::ProfileImageKind::background);
        }
        // SystemEventRecorder: committed snapshots only, the first is the baseline.
        const auto target=crops.receive(p.backgroundZoom,p.thumbnailZoom);
        if(target&&cropBaseline&&options.cropChanged)options.cropChanged(*target);
        cropBaseline=true;
        if(preview&&!canvas->previewing())preview.reset();
        updateCard();notify();
    }
    void imported(m::ProfileImageKind kind,std::string filename){
        if(!state||loadError){imageService->discard(filename);return;}
        // UserProfileStore.importImage rollback: an uncommitted file is removed.
        try{state->imageImported(kind,filename);canvas->imageImported(kind);}
        catch(const std::exception&e){imageService->discard(filename);canvas->showError(m::profileErrorText(e,language()));}
    }
    void decoded(const std::string&filename,std::shared_ptr<const gpu::ProfileDecodedImage>image){
        bool used{};
        if(avatarName&&*avatarName==filename){avatar=image;avatarFailed=!image;portrait.reset();used=true;}
        if(backgroundName&&*backgroundName==filename){background=image;used=true;}
        if(!used)return;
        if(canvas)canvas->imagesChanged();
        updateCard();notify();
    }
    // ---- Bottom-left ID card --------------------------------------------
    void updateCard(){
        if(!card||!state||loadError)return;
        m::IdCardBinding::Input input;input.profile=preview?&*preview:&state->profile(); // a slider drag previews on the card
        input.hudAccent=appearance.accentSRGB;input.language=cardLanguage();input.uidRedacted=recording;
        if(avatar){input.avatarImage=avatarName;input.avatarOrientation=avatar->orientation;}
        if(background)input.backgroundImage=backgroundName;
        if(!card->update(input))return;
        scheduleCard();notifyCard();
    }
    std::array<std::uint64_t,3>cardWanted()const{return {card->avatarRevision(),card->backgroundRevision(),card->hoverRevision()};}
    void scheduleCard(){
        if(!card||cardWanted()==cardRequested)return;
        if(cardBusy){cardDirty=true;return;}
        submitCard();
    }
    void submitCard(){
        const auto wanted=cardWanted();const auto&p=preview?*preview:state->profile();const auto accent=card->accent();
        std::array<bool,3>make{};for(std::size_t k=0;k<3;++k)make[k]=wanted[k]!=cardRequested[k];
        auto results=std::make_shared<std::array<std::optional<m::ProfileImage>,3>>();
        const auto worker=cardWorker;const auto art=cardArtwork;const auto photo=background;const auto source=avatar;
        const int orientation=avatar?avatar->orientation:1;
        const double avatarZoom=p.avatarZoom,thumbnailZoom=p.thumbnailZoom;const core::Point avatarOffset{p.avatarOffsetX,p.avatarOffsetY},thumbnailOffset{p.thumbnailOffsetX,p.thumbnailOffsetY};
        const std::weak_ptr<Impl>weak=shared_from_this();
        const bool accepted=executor.submit(cardRoute,[worker,art,photo,source,orientation,avatarZoom,avatarOffset,thumbnailZoom,thumbnailOffset,accent,make,results]{
            if(make[avatarTexture]&&source){
                if(worker->source!=source){worker->working=gpu::profileAvatarWorking(source);worker->source=source;}
                (*results)[avatarTexture]=m::IdCardArtwork::avatar(*worker->working,orientation,avatarZoom,avatarOffset);
            }else if(make[avatarTexture]){worker->source.reset();worker->working.reset();}
            if(make[backgroundTexture])(*results)[backgroundTexture]=art->background(accent,photo?&photo->image:nullptr,thumbnailZoom,thumbnailOffset);
            if(make[hoverTexture])(*results)[hoverTexture]=art->hover(accent);
        },[weak,wanted,make,results](std::exception_ptr error){
            const auto i=weak.lock();if(!i||!i->alive)return;i->cardBusy=false;
            for(std::size_t k=0;k<3;++k){
                if(!make[k])continue;
                if(error){i->cardRequested[k]=0;continue;} // regenerated by the next card change
                if((*results)[k])i->cardTextures[k]=IdCardTexture{std::move(*(*results)[k]),wanted[k]};
                else i->cardTextures[k].reset(); // no photo avatar: the packet silhouette stays bound
            }
            if(std::exchange(i->cardDirty,false))i->scheduleCard();
            i->notifyCard();
        });
        if(!accepted){cardDirty=true;return;} // retried on queue capacity
        for(std::size_t k=0;k<3;++k)if(make[k])cardRequested[k]=wanted[k];
        cardBusy=true;
    }
    // ---- Account sync ----------------------------------------------------
    void applyGame(const std::function<void(m::PersonalProfile&)>&apply){
        auto next=state->profile();apply(next);
        next=m::normalizedProfile(std::move(next),text);m::validatePersonalProfile(next,text);
        if(next.uid!=state->profile().uid)throw m::ProfileError(m::ProfileFailure::record);
        state->refresh(service->acceptFromGame(next,locked));
    }
    // ---- Pickers ---------------------------------------------------------
    // PersonalProfileCanvas.chooseImage through the host's queued dialog; the
    // generation token ignores a completion that outlived the request.
    bool requestImage(m::ProfileImageKind kind){
        if(choosing||coloring||loadError||!options.requestImage)return false;
        const std::weak_ptr<Impl>weak=shared_from_this();const auto generation=++chooserGeneration;
        const auto done=std::make_shared<bool>(false);choosing=true;
        const bool queued=options.requestImage(kind,[weak,kind,generation,done](std::optional<std::filesystem::path>path){
            if(std::exchange(*done,true))return;
            const auto i=weak.lock();if(!i||!i->alive||i->chooserGeneration!=generation)return;
            i->choosing=false;
            std::optional<std::string>chosen;
            if(path){const auto u8=path->u8string();chosen=std::string(reinterpret_cast<const char*>(u8.data()),u8.size());}
            if(i->canvas)i->canvas->chooserFinished(kind,std::move(chosen));
            i->notify();
        });
        if(!queued&&!*done){*done=true;choosing=false;}
        return queued;
    }
    // The colour dialog is modal: it runs from a posted message, never inside input.
    bool requestColor(std::array<double,3>initial){
        if(choosing||coloring||loadError||!options.chooseColor)return false;
        pendingColor=initial;++pickerGeneration;
        if(PostMessageW(hwnd,options.pickerMessage,pickerGeneration,0)==FALSE){pendingColor.reset();return false;}
        coloring=true;return true;
    }
    void runColor(){
        auto initialRGB=std::exchange(pendingColor,std::nullopt);
        if(!initialRGB||!coloring||!state||!canvas){coloring=false;if(canvas)canvas->colorPanelClosed();return;}
        const auto self=shared_from_this();const auto initial=state->profile().themeColorHex;const std::weak_ptr<Impl>weak=self;
        const std::function<void(std::array<double,3>)>live=[weak](std::array<double,3>c){
            const auto i=weak.lock();if(!i||!i->alive||!i->coloring||!i->canvas)return;
            i->canvas->colorChanged(c);i->notify();
        };
        const auto result=options.chooseColor(*initialRGB,live);
        if(!alive)return;
        coloring=false;
        if(result&&overlay)canvas->colorChanged(*result);
        // Windows: Cancel restores the colour the dialog started from (v1.2.0 Windows rule).
        else if(state->profile().themeColorHex!=initial){try{state->update([&](m::PersonalProfile&p){p.themeColorHex=initial;});}catch(const std::exception&){}}
        canvas->colorPanelClosed();notify();
    }
    void cancelPickers(){
        pendingColor.reset();++pickerGeneration;
        if((choosing||coloring)&&options.cancelPicker)options.cancelPicker();
        if(choosing){choosing=false;++chooserGeneration;}
    }
    // ---- Core hooks --------------------------------------------------------
    void setAppearance(const app::ModuleAppearance&a,double t){
        advance(t);appearance=a;
        canvas->setHudAccent(a.accentSRGB);
        updateCard();
    }
    bool hostEvent(const app::HostEvent&e,double t){
        advance(t);using K=app::HostEvent::Kind;
        switch(e.kind){
        case K::utilityCompleted:queueCapacityAvailable();return false;
        case K::timeChanged:case K::systemSettingsChanged:setTimeZone(options.timeZone,t);return true;
        default:return false;
        }
    }
    bool message(const app::NativeMessage&msg,double t){
        if(msg.window!=hwnd||msg.message!=options.pickerMessage)return false;
        advance(t);if(msg.wParam==pickerGeneration)runColor();return true;
    }
    void setTimeZone(std::u16string zone,double t){
        advance(t);options.timeZone=zone;auto next=gpu::nativeProfileDateRules(std::move(zone));dates=next;
        canvas->setDateRules(next);
        if(state)state->setDateRules(std::move(next));
    }
    bool flush(double t){
        advance(t);
        if(state&&!loadError)(void)hours.retry();
        return loadError.has_value()||!started||service->flush();
    }
    // ---- The art record ----------------------------------------------------
    void afterFrame(const app::ModuleFrame&f){
        advance(f.time);
        poseArt();
    }
    // aspect-fill of the photo in the picture layer's bounds (contentsGravity resizeAspectFill).
    void buildQuad(const d::Layer&picture){
        const auto b=picture.bounds();const double ia=double(artSource->image.width)/artSource->image.height,pa=b.width/b.height;
        float u0=0,v0=0,u1=1,v1=1;
        if(ia>pa){const auto k=float((1-pa/ia)/2);u0=k;u1=1-k;}else{const auto k=float((1-ia/pa)/2);v0=k;v1=1-k;}
        const float w=float(b.width),h=float(b.height);
        quad={{{{0,0,0},{u0,v0}},{{w,0,0},{u1,v0}},{{w,h,0},{u1,v1}},{{0,h,0},{u0,v1}}}};
    }
    void poseArt(){
        drawCount=0;const d::Layer*after=nullptr;
        const auto pose=core->pose(core::Module::profile);
        // Resident while the HUD is open (a module switch keeps it, as v1.2.0 Windows); released once it closed.
        artWanted=(pose||overlay)&&canvas->artPicture()&&background;
        if(artWanted&&artSource!=background){artSource=background;artUploaded=meshUploaded=false;}
        if(pose&&artWanted&&artUploaded&&meshUploaded&&rampUploaded){
            const auto&root=canvas->root();
            const auto host=d::sampleLayer(*canvas->artSlot(),root,time),box=d::sampleLayer(*canvas->artBox(),root,time),picture=d::sampleLayer(*canvas->artPicture(),root,time);
            after=canvas->artSlot();
            if(picture.visible&&picture.opacity>0){
                auto&draw=draws[0];draw.sourceID="profile.art.draw";draw.meshID=artMesh;draw.textureID=artTexture;
                draw.world=pose->boxWorld*gpu::designMatrix(picture.world);draw.opacity=float(pose->opacity*picture.opacity);
                draw.masks.assign(pose->ownerMasks.begin(),pose->ownerMasks.end());
                const auto boxBounds=canvas->artBox()->bounds();
                draw.masks.push_back(gpu::PlaneMask{core::source::inverseSourceMatrix(pose->boxWorld*gpu::designMatrix(box.world)),{0,0,boxBounds.width,boxBounds.height},0});
                // The ramp in the art host's plane: mask-local = host-local - (maskFrame - hostFrame) origins.
                const auto mask=m::ProfileCanvas::maskFrame(),hostFrame=m::ProfileCanvas::artHostFrame();
                draw.alphaMask=gpu::PlaneAlphaMask{M::translation(-(mask.x-hostFrame.x),-(mask.y-hostFrame.y),0)*core::source::inverseSourceMatrix(pose->boxWorld*gpu::designMatrix(host.world)),ramp->bounds,std::string(rampTexture)};
                draw.shutter=pose->shutter;drawCount=1;++statistics.artDraws;
            }
        }
        if(after==insertedAfter&&drawCount==insertedCount)return;
        insertedAfter=after;insertedCount=drawCount;
        core->setInsertion(core::Module::profile,after,after?std::optional(gpu::LayerCompositionEntry{&carrier,std::span<const gpu::DrawObject>(draws.data(),drawCount)}):std::nullopt);
    }
    void uploadArt(gpu::Renderer&r){
        renderer=&r;
        if(!artWanted){releaseArt(r);return;}
        if(!ramp)ramp=gpu::rasterizeCentreBackdropRamp(raster,options.design.raster,std::string(rampTexture));
        if(!rampUploaded){r.setTexture(std::string(rampTexture),1,{ramp->width,ramp->height,ramp->straightRGBA,gpu::TextureColorSpace::sRGB});rampUploaded=true;statistics.rampResident=true;}
        if(!artUploaded&&artSource){
            const auto&img=artSource->image;
            r.setTexture(std::string(artTexture),++artRevision,{img.width,img.height,img.rgba,gpu::TextureColorSpace::sRGB,gpu::TextureFilter::linear});
            artUploaded=true;++statistics.artUploads;statistics.artResident=true;statistics.artBytes=img.rgba.size();meshUploaded=false;
        }
        if(!meshUploaded&&artSource&&canvas->artPicture()){
            buildQuad(*canvas->artPicture());r.setMesh(std::string(artMesh),++meshRevision,{quad,quadIndices});meshUploaded=true;
        }
    }
    void releaseArt(gpu::Renderer&r){
        if(insertedAfter){insertedAfter=nullptr;insertedCount=0;drawCount=0;core->setInsertion(core::Module::profile,nullptr,std::nullopt);}
        if(artUploaded&&r.removeTexture(std::string(artTexture))){artUploaded=false;statistics.artResident=false;statistics.artBytes=0;}
        if(meshUploaded&&r.removeMesh(std::string(artMesh)))meshUploaded=false;
        if(rampUploaded&&r.removeTexture(std::string(rampTexture))){rampUploaded=false;statistics.rampResident=false;}
        if(!artUploaded)artSource.reset();
    }
    void queueCapacityAvailable(){
        service->queueCapacityAvailable();imageService->queueCapacityAvailable();
        if(cardDirty&&!cardBusy){cardDirty=false;scheduleCard();}
        submitPortrait();
    }
};
ProfileOwner::ProfileOwner(HWND h,app::UtilityExecutor&e,gpu::LayerRasterizer&r,d::Context&c,const m::WorkModeController&w,ProfileOwnerOptions o)
    :impl_(std::make_shared<Impl>(h,e,r,c,w,std::move(o))){impl_->initialize();}
ProfileOwner::~ProfileOwner(){
    auto i=impl_;i->alive=false;
    if(i->state&&i->stateObserver)i->state->removeObserver(i->stateObserver);
    i->core.reset();i->canvas=nullptr;
}
DesignOwnerCore&ProfileOwner::core()noexcept{return *impl_->core;}
void ProfileOwner::start(double t){auto i=impl_;i->advance(t);if(std::exchange(i->started,true))return;i->service->start();}
void ProfileOwner::setOverlayVisible(bool v,double t){auto i=impl_;i->advance(t);i->overlay=v;if(!v)i->cancelPickers();}
void ProfileOwner::setRecordingMode(bool v,double t){auto i=impl_;i->advance(t);if(i->recording==v)return;i->recording=v;i->updateCard();}
void ProfileOwner::upload(gpu::Renderer&r){auto i=impl_;i->core->upload(r);i->uploadArt(r);}
void ProfileOwner::collected(gpu::Renderer&r){auto i=impl_;i->core->collected(r);}
void ProfileOwner::release(gpu::Renderer&r){
    auto i=impl_;i->releaseArt(r);i->core->release(r);
    need(i->carrier.releaseResources(r),"Personal ID art records still published at teardown");
}
void ProfileOwner::queueCapacityAvailable(){impl_->queueCapacityAvailable();}
void ProfileOwner::workModeCheckpoint(double total){
    auto i=impl_;i->hours.checkpoint(total);
    // A delivered checkpoint updates the stored lifetime; the shown hours
    // caption itself keeps the source 30 s refresh.
}
void ProfileOwner::setSyncLocked(ehud::data::ProfileSyncFields v,double t){
    auto i=impl_;i->advance(t);if(i->locked==v)return;i->locked=v;
    if(i->state&&!i->loadError)i->state->setSyncLocked(v);
}
void ProfileOwner::acceptFromGame(std::function<void(m::PersonalProfile&)>apply,double t){
    auto i=impl_;i->advance(t);need(static_cast<bool>(apply),"Account profile update is required");
    if(i->loadError)throw m::ProfileError(m::ProfileFailure::unavailable);
    if(!i->state){i->pendingGame=std::move(apply);return;}
    i->applyGame(apply);
}
std::string ProfileOwner::importGameAvatar(std::span<const std::uint8_t>png,double t){
    auto i=impl_;i->advance(t);
    if(!i->state||i->loadError)throw m::ProfileError(m::ProfileFailure::unavailable);
    auto name=gpu::importProfileImageBytes(png,m::ProfileImageKind::avatar,i->options.appRoot/"Profile"/"Images");
    try{i->state->imageImported(m::ProfileImageKind::avatar,name);}
    catch(const std::exception&){i->imageService->discard(name);throw m::ProfileError(m::ProfileFailure::persistence);}
    return name;
}
void ProfileOwner::setTimeZone(std::u16string zone,double t){impl_->setTimeZone(std::move(zone),t);}
bool ProfileOwner::loaded()const noexcept{return impl_->state!=nullptr&&!impl_->loadError;}
const std::optional<std::string>&ProfileOwner::loadError()const noexcept{return impl_->loadError;}
const m::ProfileState*ProfileOwner::state()const noexcept{return impl_->state.get();}
m::ProfileCanvas*ProfileOwner::canvas()noexcept{return impl_->canvas;}
const m::ProfileServiceStatus*ProfileOwner::serviceStatus()const{return &impl_->service->status();}
const m::IdCardBinding*ProfileOwner::card()const noexcept{return impl_->card&&impl_->card->revision()?&*impl_->card:nullptr;}
const IdCardTexture*ProfileOwner::cardTexture(IdCardTextureKind kind)const noexcept{
    const auto&t=impl_->cardTextures[static_cast<std::size_t>(kind)];return t?&*t:nullptr;
}
bool ProfileOwner::pickerOpen()const noexcept{return impl_->choosing||impl_->coloring;}
ProfileOwnerStats ProfileOwner::stats()const{return impl_->statistics;}
}
#endif
