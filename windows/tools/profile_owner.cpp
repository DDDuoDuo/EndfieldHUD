#include "tools/profile_owner.hpp"
#ifdef _WIN32
#include "modules/id_card_artwork.hpp"
#include "native/id_card_captions.hpp"
#include "native/profile_text.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace endfield::tools {
namespace {
namespace m=modules;namespace gpu=native;
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}
constexpr std::size_t avatarTexture=0,backgroundTexture=1,hoverTexture=2;
// The card avatar is HUDPortraitArtwork.renderedImage at 136 pt, 2x and 1.35
// oversampling (368 px); a bounded working copy keeps slider-drag
// regeneration cheap on the shared worker.
constexpr unsigned cardAvatarWorkingPixels=1024;
struct Exit {bool&flag;~Exit(){flag=false;}};
}
struct ProfileOwner::Impl:std::enable_shared_from_this<Impl> {
    HWND hwnd;app::UtilityExecutor&executor;gpu::LayerRasterizer&raster;gpu::LayerImageSource&images;const m::WorkModeController&work;
    ProfileOwnerOptions options;ITfThreadMgr*manager;TfClientId client;
    m::ProfileTextRules text;m::ProfileDateRules dates;m::ProfileSourceArtwork artwork;m::IdCardSource cardSource;
    std::shared_ptr<const m::IdCardArtwork>cardArtwork;
    std::unique_ptr<m::ProfileService>service;std::unique_ptr<gpu::ProfileImageService>imageService;
    // Declaration order: the page borrows the state and is destroyed first.
    std::unique_ptr<m::ProfileState>state;std::unique_ptr<ProfilePreview>page;
    m::WorkModeProfileHours hours;m::ProfileCropRecorder crops;std::optional<m::IdCardBinding>card;
    ProfileOwnerAppearance appearance;std::optional<app::ClientMetrics>metrics;
    bool overlay{true},focused{true},locked{},alive{true},started{},cropBaseline{};
    double time{};std::optional<std::string>loadError;
    std::optional<std::function<void(m::PersonalProfile&)>>pendingGame;
    // Managed images currently referenced by the committed record.
    std::optional<std::string>avatarName,backgroundName;bool imagesKnown{};
    std::shared_ptr<const gpu::ProfileDecodedImage>avatar,background;
    std::uint64_t seenRevision{},seenGeometry{};
    // Deferred picker requests (posted, then run outside page input).
    std::optional<m::ProfileImageKind>pendingImage;std::optional<std::array<double,3>>pendingColor;bool picking{};WPARAM pickerGeneration{1};
    // Card textures, generated on the utility worker; one job at a time.
    struct CardWorker {std::shared_ptr<const gpu::ProfileDecodedImage>source;m::ProfileImage working;};
    std::shared_ptr<CardWorker>cardWorker=std::make_shared<CardWorker>();
    app::UtilityExecutor::Route cardRoute;bool cardBusy{},cardDirty{};
    std::array<std::optional<IdCardTexture>,3>cardTextures;std::array<std::uint64_t,3>cardRequested{};

    Impl(HWND h,app::UtilityExecutor&e,gpu::LayerRasterizer&r,gpu::LayerImageSource&source,const m::WorkModeController&w,ProfileOwnerOptions o,ITfThreadMgr*mgr,TfClientId id)
        :hwnd(h),executor(e),raster(r),images(source),work(w),options(std::move(o)),manager(mgr),client(id),
         text(gpu::nativeProfileTextRules()),dates(gpu::nativeProfileDateRules(options.timeZone)),
         artwork(m::loadProfileSourceArtwork(options.resources)),cardSource(m::loadIdCardSource(options.resources)),
         cardArtwork(std::make_shared<const m::IdCardArtwork>(artwork.card)),cardRoute(e.makeRoute()){
        need(IsWindow(h)!=FALSE,"Personal Profile owner requires the caller's HWND");
        need((manager==nullptr)==(client==TF_CLIENTID_NULL),"Borrowed TSF manager/client must be supplied together");
        need(options.pickerMessage>=WM_APP&&options.pickerMessage<=0xbfff&&options.pickerMessage!=options.textMessage,"Profile picker requests use their own WM_APP message");
    }
    ~Impl(){executor.invalidate(cardRoute,true);}
    void initialize(){
        const std::weak_ptr<Impl>weak=shared_from_this();
        service=std::make_unique<m::ProfileService>(options.appRoot,executor,m::ProfileServiceCallbacks{
            [weak](const m::PersonalProfile&p){if(auto i=weak.lock();i&&i->alive)i->loaded(p);},
            [weak](m::ProfileFailure code,std::string detail){if(auto i=weak.lock();i&&i->alive)i->loadFailed(code,detail);},
            [weak](m::ProfileFailure code,std::string detail){if(auto i=weak.lock();i&&i->alive&&i->state){i->state->persistenceFailed(code,detail);i->external();}},
            {}});
        imageService=std::make_unique<gpu::ProfileImageService>(options.appRoot/"Profile"/"Images",executor,gpu::ProfileImageCallbacks{
            [weak](m::ProfileImageKind kind,std::string filename){if(auto i=weak.lock();i&&i->alive)i->imported(kind,std::move(filename));},
            [weak](m::ProfileImageKind,m::ProfileFailure failure,std::string detail){if(auto i=weak.lock();i&&i->alive&&i->state){i->state->imageImportFailed(failure,detail);i->external();}},
            [weak](const std::string&filename,std::shared_ptr<const gpu::ProfileDecodedImage>image){if(auto i=weak.lock();i&&i->alive)i->decoded(filename,std::move(image));},
            [weak](const std::string&filename,m::ProfileFailure){if(auto i=weak.lock();i&&i->alive)i->decoded(filename,nullptr);}});
    }
    void advance(double now){need(std::isfinite(now),"Personal Profile needs a finite continuous clock");time=std::max(time,now);}
    void notify(){if(alive&&options.changed)options.changed();}
    void notifyCard(){if(alive&&options.cardChanged)options.cardChanged();}
    // ---- Load ------------------------------------------------------------
    void createPage(){
        ProfilePreviewOptions o;o.raster=options.raster;o.appearance={appearance.dark,appearance.scale,appearance.accentSRGB};
        o.reduceMotion=appearance.reduceMotion;o.textMessage=options.textMessage;o.images=&images;o.frameSprite=artwork.frame;
        const std::weak_ptr<Impl>weak=shared_from_this();
        o.chooseImage=[weak](m::ProfileImageKind kind){if(auto i=weak.lock())i->request(kind,std::nullopt);};
        o.chooseColor=[weak](std::array<double,3>rgb){if(auto i=weak.lock())i->request(std::nullopt,rgb);};
        page=std::make_unique<ProfilePreview>(hwnd,*state,raster,std::move(o),manager,client);
        if(metrics)page->resize(*metrics);
        page->setOverlayVisible(overlay,time);page->focus(focused,time);
    }
    void loaded(const m::PersonalProfile&p){
        const std::weak_ptr<Impl>weak=shared_from_this();
        state=std::make_unique<m::ProfileState>(p,text,dates,service->persistence(
            [weak]{const auto i=weak.lock();return i&&i->locked;},
            [weak]{const auto i=weak.lock();return i?i->hours.totalSeconds(i->work,i->time):0.;}),appearance.language);
        state->setSyncLocked(locked);state->setHudAccent(appearance.accentSRGB);
        // The Work Mode owner was constructed without a restored total: the
        // stored lifetime is the offset, and early checkpoints land once.
        hours.profileLoaded(p.accumulatedWorkSeconds,[weak](double seconds){const auto i=weak.lock();return i&&i->state&&i->state->setWorkSeconds(seconds);});
        card.emplace(cardSource,gpu::nativeIdCardMeasure(raster,options.raster));
        createPage();
        if(auto apply=std::exchange(pendingGame,std::nullopt)){try{applyGame(*apply);}catch(const std::exception&){/* the next account refresh retries */}}
        external();
        if(alive&&state&&options.loaded)options.loaded(state->profile());
    }
    void loadFailed(m::ProfileFailure code,const std::string&detail){
        // Source canvas with a nil store: defaults, the store's message and no persistence.
        loadError=m::profileFailureMessage(code,appearance.language,detail);pendingGame.reset();
        state=std::make_unique<m::ProfileState>(ehud::data::Profile::defaults(),text,dates,m::ProfilePersistence{},appearance.language);
        state->setHudAccent(appearance.accentSRGB);state->persistenceFailed(code,detail);
        createPage();external();
    }
    // ---- Content events --------------------------------------------------
    void external(){if(!state)return;if(page)page->stateChanged(time);sync(true);notify();}
    void sync(bool force=false){
        if(!state||!alive)return;
        if(!force&&state->revision()==seenRevision&&state->geometryRevision()==seenGeometry)return;
        seenRevision=state->revision();seenGeometry=state->geometryRevision();
        if(loadError)return; // a failed store never feeds the card or managed images
        const auto&p=state->profile();
        const bool first=!imagesKnown;imagesKnown=true;
        if(p.avatarFilename!=avatarName){
            avatarName=p.avatarFilename;avatar.reset();if(page)page->setAvatar(nullptr,time);
            if(avatarName)imageService->request(*avatarName,m::ProfileImageKind::avatar);
            if(!first&&options.avatarChanged)options.avatarChanged(avatarName);
        }
        if(p.backgroundFilename!=backgroundName){
            backgroundName=p.backgroundFilename;background.reset();if(page)page->setBackground(nullptr,time);
            if(backgroundName)imageService->request(*backgroundName,m::ProfileImageKind::background);
        }
        // SystemEventRecorder: committed snapshots only, the first is the baseline.
        const auto target=crops.receive(p.backgroundZoom,p.thumbnailZoom);
        if(target&&cropBaseline&&options.cropChanged)options.cropChanged(*target);
        cropBaseline=true;
        updateCard();
    }
    void imported(m::ProfileImageKind kind,std::string filename){
        if(!state||loadError){imageService->discard(filename);return;}
        // UserProfileStore.importImage rollback: an uncommitted file is removed.
        if(!state->imageImported(kind,filename))imageService->discard(filename);
        external();
    }
    void decoded(const std::string&filename,std::shared_ptr<const gpu::ProfileDecodedImage>image){
        bool used{};
        if(avatarName&&*avatarName==filename){avatar=image;if(page)page->setAvatar(image,time);used=true;}
        if(backgroundName&&*backgroundName==filename){background=image;if(page)page->setBackground(image,time);used=true;}
        if(!used)return;
        updateCard();notify();
    }
    // ---- Bottom-left ID card --------------------------------------------
    void updateCard(){
        if(!card||!state||loadError)return;
        m::IdCardBinding::Input input;input.profile=&state->preview(); // a slider drag previews on the card
        input.hudAccent=appearance.accentSRGB;input.language=appearance.language;
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
        const auto wanted=cardWanted();const auto&p=state->preview();const auto accent=card->accent();
        std::array<bool,3>make{};for(std::size_t k=0;k<3;++k)make[k]=wanted[k]!=cardRequested[k];
        auto results=std::make_shared<std::array<std::optional<m::ProfileImage>,3>>();
        const auto worker=cardWorker;const auto art=cardArtwork;const auto photo=background;const auto portrait=avatar;
        const int orientation=avatar?avatar->orientation:1;
        const double avatarZoom=p.avatarZoom,thumbnailZoom=p.thumbnailZoom;const core::Point avatarOffset{p.avatarOffsetX,p.avatarOffsetY},thumbnailOffset{p.thumbnailOffsetX,p.thumbnailOffsetY};
        const std::weak_ptr<Impl>weak=shared_from_this();
        const bool accepted=executor.submit(cardRoute,[worker,art,photo,portrait,orientation,avatarZoom,avatarOffset,thumbnailZoom,thumbnailOffset,accent,make,results]{
            if(make[avatarTexture]&&portrait){
                if(worker->source!=portrait){
                    const auto&v=portrait->image;const unsigned largest=std::max(v.width,v.height);
                    if(largest>cardAvatarWorkingPixels){const double s=double(cardAvatarWorkingPixels)/largest;
                        worker->working=m::profileResampled(v,std::max(1u,unsigned(std::lround(v.width*s))),std::max(1u,unsigned(std::lround(v.height*s))));}
                    else worker->working=v;
                    worker->source=portrait;
                }
                (*results)[avatarTexture]=m::IdCardArtwork::avatar(worker->working,orientation,avatarZoom,avatarOffset);
            }else if(make[avatarTexture]){worker->source.reset();worker->working={};}
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
        state->refresh(service->acceptFromGame(next,locked),locked);
        external();
    }
    // ---- Pickers ---------------------------------------------------------
    void request(std::optional<m::ProfileImageKind>kind,std::optional<std::array<double,3>>rgb){
        if(picking||loadError)return;
        pendingImage=kind;pendingColor=rgb;++pickerGeneration;
        if(PostMessageW(hwnd,options.pickerMessage,pickerGeneration,0)==FALSE){pendingImage.reset();pendingColor.reset();}
    }
    void runPicker(){
        auto kind=std::exchange(pendingImage,std::nullopt);auto rgb=std::exchange(pendingColor,std::nullopt);
        if(picking||!page||!state||loadError||!overlay||!page->active())return;
        const auto self=shared_from_this();picking=true;
        if(kind){
            std::optional<std::filesystem::path>path;
            {Exit done{picking};if(options.chooseImage)path=options.chooseImage(*kind);}
            if(!alive||!state)return;
            // The source completion ignores a chooser that outlived the page.
            if(path&&overlay&&page&&page->active()){state->beginImageImport();(void)imageService->import(*path,*kind);}
            external();
        }else if(rgb){
            const auto initial=state->profile().themeColorHex;const std::weak_ptr<Impl>weak=self;
            const std::function<void(std::array<double,3>)>live=[weak](std::array<double,3>c){
                const auto i=weak.lock();if(!i||!i->alive||!i->state||!i->picking)return;
                i->state->setCustomColor(c[0],c[1],c[2]);i->external();
            };
            std::optional<std::array<double,3>>result;
            {Exit done{picking};if(options.chooseColor)result=options.chooseColor(*rgb,live);}
            if(!alive||!state)return;
            if(result&&overlay&&page&&page->active())state->setCustomColor((*result)[0],(*result)[1],(*result)[2]);
            else if(state->profile().themeColorHex!=initial)state->setThemeColor(initial);
            external();
        }else picking=false;
    }
};
ProfileOwner::ProfileOwner(HWND h,app::UtilityExecutor&e,gpu::LayerRasterizer&r,gpu::LayerImageSource&s,const m::WorkModeController&w,ProfileOwnerOptions o,ITfThreadMgr*mgr,TfClientId id)
    :impl_(std::make_shared<Impl>(h,e,r,s,w,std::move(o),mgr,id)){impl_->initialize();}
ProfileOwner::~ProfileOwner(){auto i=impl_;i->alive=false;i->page.reset();}
void ProfileOwner::start(double t){auto i=impl_;i->advance(t);if(std::exchange(i->started,true))return;i->service->start();}
void ProfileOwner::resize(const app::ClientMetrics&metrics){auto i=impl_;i->metrics=metrics;if(i->page)i->page->resize(metrics);}
void ProfileOwner::setAppearance(const ProfileOwnerAppearance&a,double t){
    auto i=impl_;i->advance(t);need(a.language!=core::Language::system,"Resolve the profile language once in the application owner");
    if(i->appearance==a)return;i->appearance=a;
    if(i->page){i->page->setLanguage(a.language,t);i->page->setAppearance({a.dark,a.scale,a.accentSRGB},t);i->page->setReduceMotion(a.reduceMotion,t);}
    i->updateCard();
}
void ProfileOwner::setOverlayVisible(bool v,double t){
    auto i=impl_;i->advance(t);i->overlay=v;
    if(!v){i->pendingImage.reset();i->pendingColor.reset();++i->pickerGeneration;if(i->picking&&i->options.cancelPicker)i->options.cancelPicker();}
    if(i->page)i->page->setOverlayVisible(v,t);i->sync();
}
void ProfileOwner::overlayClosing(double t){cancelInteraction(t);setOverlayVisible(false,t);}
void ProfileOwner::update(const core::Matrix4&center,const core::source::DesktopChromeSettings&chrome,const core::ModulePresentationSample&sample,float opacity,double t){
    auto i=impl_;i->advance(t);if(!i->page)return;i->page->update(center,chrome,sample,opacity,t);i->sync();
}
bool ProfileOwner::requiresFrames(double t)const{return impl_->page&&impl_->page->requiresFrames(t);}
std::optional<double>ProfileOwner::nextWakeTime(double)const{return impl_->page?impl_->page->nextWakeTime():std::nullopt;}
bool ProfileOwner::deadline(double t){
    auto i=impl_;i->advance(t);if(!i->page||!i->state)return false;
    const auto before=i->state->workRevision();i->page->wake(t);return i->state->workRevision()!=before;
}
void ProfileOwner::upload(gpu::Renderer&r){if(impl_->page)impl_->page->upload(r);}
std::span<const gpu::LayerCompositionEntry>ProfileOwner::entries(){return impl_->page?impl_->page->entries():std::span<const gpu::LayerCompositionEntry>{};}
void ProfileOwner::collected(gpu::Renderer&r){if(impl_->page)impl_->page->collected(r);}
void ProfileOwner::release(gpu::Renderer&r){if(impl_->page)impl_->page->release(r);}
bool ProfileOwner::covers(core::Point p)const{return impl_->page&&impl_->page->covers(p);}
bool ProfileOwner::pointerLocked()const{return impl_->picking||(impl_->page&&impl_->page->pointerLocked());}
bool ProfileOwner::capturesPointer()const{return impl_->picking||(impl_->page&&impl_->page->capturesPointer());}
bool ProfileOwner::pointer(const app::PointerEvent&e,double t){
    auto i=impl_;i->advance(t);if(i->picking)return true;if(!i->page)return false;
    const bool used=i->page->pointer(e,t);i->sync();return used;
}
bool ProfileOwner::key(const app::KeyEvent&e,double t){
    auto i=impl_;i->advance(t);if(i->picking)return true;if(!i->page)return false;
    const bool used=i->page->key(e,t);i->sync();return used;
}
bool ProfileOwner::filterKey(const app::NativeMessage&m){auto i=impl_;return !i->picking&&i->page&&i->page->filterKey(m);}
bool ProfileOwner::message(const app::NativeMessage&m,double t){
    auto i=impl_;
    if(m.window==i->hwnd&&m.message==i->options.pickerMessage){i->advance(t);if(m.wParam==i->pickerGeneration)i->runPicker();return true;}
    if(!i->page)return false;const bool used=i->page->message(m,t);if(used)i->sync();return used;
}
void ProfileOwner::focus(bool v,double t){auto i=impl_;i->advance(t);i->focused=v;if(i->page)i->page->focus(v,t);}
void ProfileOwner::cancelInteraction(double t){auto i=impl_;i->advance(t);if(i->page){i->page->cancelInteraction(t);i->sync();}}
bool ProfileOwner::finishEditing(double t){
    auto i=impl_;i->advance(t);if(!i->page)return true;
    // HUDPersonalProfileInteraction.deactivate: commit, else cancel.
    if(!i->page->finishEditing(true,t))i->page->finishEditing(false,t);
    i->sync();return true;
}
bool ProfileOwner::flush(double t){
    auto i=impl_;finishEditing(t);
    if(i->state&&!i->loadError)(void)i->hours.retry();
    return i->loadError.has_value()||!i->started||i->service->flush();
}
void ProfileOwner::queueCapacityAvailable(){
    auto i=impl_;i->service->queueCapacityAvailable();i->imageService->queueCapacityAvailable();
    if(i->cardDirty&&!i->cardBusy){i->cardDirty=false;i->scheduleCard();}
}
void ProfileOwner::workModeCheckpoint(double total){
    auto i=impl_;i->hours.checkpoint(total);
    // A delivered checkpoint updates the stored lifetime; the shown hours
    // caption itself keeps the source 30 s refresh.
    if(i->state&&!i->loadError)i->sync();
}
void ProfileOwner::setSyncLocked(bool v,double t){
    auto i=impl_;i->advance(t);if(i->locked==v)return;i->locked=v;
    if(i->state&&!i->loadError){i->state->setSyncLocked(v);i->external();}
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
    if(!i->state->imageImported(m::ProfileImageKind::avatar,name)){i->imageService->discard(name);i->external();throw m::ProfileError(m::ProfileFailure::persistence);}
    i->external();return name;
}
void ProfileOwner::setTimeZone(std::u16string zone,double t){
    auto i=impl_;i->advance(t);auto next=gpu::nativeProfileDateRules(std::move(zone));i->dates=next;
    if(i->state){i->state->setDateRules(std::move(next));i->external();}
}
bool ProfileOwner::perform(std::string_view id,double t){
    auto i=impl_;i->advance(t);if(!i->page||!i->state||!i->page->finishEditing(true,t))return false;
    const bool used=i->state->perform(id);i->page->stateChanged(t);i->sync();return used;
}
bool ProfileOwner::setSlider(m::ProfileField field,double value,double t){
    auto i=impl_;i->advance(t);if(!i->page||!i->state||!i->page->finishEditing(true,t))return false;
    const bool used=i->state->setSlider(field,value);i->page->stateChanged(t);i->sync();return used;
}
bool ProfileOwner::loaded()const noexcept{return impl_->state!=nullptr&&!impl_->loadError;}
const std::optional<std::string>&ProfileOwner::loadError()const noexcept{return impl_->loadError;}
const m::ProfileState*ProfileOwner::state()const noexcept{return impl_->state.get();}
const m::ProfileServiceStatus*ProfileOwner::serviceStatus()const{return &impl_->service->status();}
const m::IdCardBinding*ProfileOwner::card()const noexcept{return impl_->card&&impl_->card->revision()?&*impl_->card:nullptr;}
const IdCardTexture*ProfileOwner::cardTexture(IdCardTextureKind kind)const noexcept{
    const auto&t=impl_->cardTextures[static_cast<std::size_t>(kind)];return t?&*t:nullptr;
}
bool ProfileOwner::pickerOpen()const noexcept{return impl_->picking;}
ProfilePreview*ProfileOwner::page()noexcept{return impl_->page.get();}
}
#endif
