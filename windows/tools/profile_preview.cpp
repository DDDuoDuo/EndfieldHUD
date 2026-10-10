#include "tools/profile_preview.hpp"
#ifdef _WIN32
#include "core/motion.hpp"
#include "core/source_camera.hpp"
#include "native/module_registration.hpp"
#include "native/module_scene.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace endfield::tools {namespace {
namespace gpu=native;namespace m=modules;using M=core::Matrix4;using P=core::Point;using R=core::Rect;using J=ehud::data::Json;
void need(bool v,const char*s){if(!v)throw std::invalid_argument(s);}
bool locked(HRESULT r){return r==TS_E_NOLOCK||r==TF_E_NOLOCK;}
bool inside(R r,P p){return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
constexpr R canvas{0,0,400,334};
constexpr core::CubicTiming easeOut{0,0,.58,1},easeInOut{.42,0,.58,1};
J blank(){return J::Object{{"bounds",J::Array{0,0,400,334}},{"children",J::Array{}}};}
UINT_PTR nextGeneration(){static std::atomic<UINT_PTR>value{1};const auto v=value.fetch_add(1);need(v&&v!=std::numeric_limits<UINT_PTR>::max(),"Profile editor generation exhausted");return v;}
struct Track {
    double from{},target{},start{},duration{};const core::CubicTiming*timing{&easeOut};
    double value(double now)const{if(duration<=0)return target;const double t=std::clamp((now-start)/duration,0.,1.);return from+(target-from)*timing->value(t);}
    bool active(double now)const{return duration>0&&from!=target&&now<start+duration;}
};
}
struct ProfilePreview::Impl:std::enable_shared_from_this<Impl> {
    HWND hwnd;m::ProfileState&state;gpu::LayerRasterizer&raster;ProfilePreviewOptions options;ITfThreadMgr*manager;TfClientId client;
    gpu::NativeProfileScene scene;gpu::LayerScene geometry;gpu::NativeModuleSurface surface;gpu::NativeModuleRegistration registration{"profile.registration"};
    app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose>pose;std::optional<gpu::ModuleSurfacePose>lastPose;
    std::uint64_t seenRevision{},seenWork{},seenFont{},sceneRevision{},seenOpens{},seenDismissals{},seenToggles{};bool dirty{true},synced{};
    m::PersonalProfile committed;m::ProfileArtwork art;
    Track section,travel;bool selected{},sectionPosed{};M backdropWorld;
    std::unique_ptr<gpu::NativeProfileEditorField>field;std::vector<std::unique_ptr<gpu::NativeProfileEditorField>>retired;
    std::optional<double>fieldOpened;bool fieldFailed{},fieldDragging{},fieldPublished{},pendingFinish{},pendingCommit{},pendingBlur{};
    std::optional<P>point;bool pressed{},input{},active{},overlay{true},ownerFocused{true},alive{true},drawing{};double time{};
    std::vector<gpu::LayerCompositionEntry>composed;gpu::Renderer*renderer{};
    // Page imagery published into the borrowed LayerImageSource.
    std::shared_ptr<const gpu::ProfileDecodedImage>avatar,background;std::optional<m::ProfileImage>avatarWork;
    std::optional<std::array<double,5>>cropKey;std::optional<std::array<double,3>>frameAccent;std::uint64_t imageRevision{};std::string portraitKey,frameKey;
    Impl(HWND h,m::ProfileState&s,gpu::LayerRasterizer&r,ProfilePreviewOptions o,ITfThreadMgr*mgr,TfClientId id)
        :hwnd(h),state(s),raster(r),options(withImages(std::move(o))),manager(mgr),client(id),scene(r,options.raster),geometry(r),surface(prepareGeometry(),core::Module::profile){
        need(IsWindow(h)!=FALSE,"Personal Profile requires the caller's HWND");
        need((manager==nullptr)==(client==TF_CLIENTID_NULL),"Borrowed TSF manager/client must be supplied together");
        need(options.textMessage>=WM_APP&&options.textMessage<=0xbfff,"Profile text notices use an owned WM_APP message");
        committed=state.profile();seenOpens=state.popoverOpens();seenDismissals=state.popoverDismissals();seenToggles=state.visibilityToggles();
        static std::atomic<std::uint64_t>serial{};const auto serialText=std::to_string(serial.fetch_add(1));portraitKey="profile.page.portrait."+serialText;frameKey="profile.page.frame."+serialText;
        retired.reserve(3);composed.reserve(12);
    }
    // The retained scene resolves the published page imagery through the same source.
    static ProfilePreviewOptions withImages(ProfilePreviewOptions o){if(o.images)o.raster.memoryImages=o.images;return o;}
    gpu::LayerScene&prepareGeometry(){geometry.load(blank(),options.raster);return geometry;}
    void advance(double now){need(std::isfinite(now),"Personal Profile needs a finite continuous clock");time=std::max(time,now);}
    std::optional<P>local(P p)const{return input&&pose?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
    // Content event: repaint only when the state, its work caption, fonts,
    // appearance or images changed. Source animations follow the canvas rules.
    // HUDPortraitArtwork for the page: crop and tinted frame republished only
    // when the image, crop numbers, render scale or card accent change.
    void imagery(){
        if(!options.images)return;auto contents=options.contents;const auto&p=state.preview();
        if(avatar&&avatarWork){
            const double scale=std::min(options.appearance.scale,256/(55*1.35));
            const std::array<double,5>key{static_cast<double>(reinterpret_cast<std::uintptr_t>(avatar.get())),p.avatarZoom,p.avatarOffsetX,p.avatarOffsetY,scale};
            if(cropKey!=key){
                const auto crop=m::profileStraight(m::profileRenderedImage(*avatarWork,avatar->orientation,{55,55},p.avatarZoom,{p.avatarOffsetX,p.avatarOffsetY},scale));
                options.images->publish(portraitKey,++imageRevision,crop.width,crop.height,crop.rgba);
                contents.avatar=J::Object{{"memoryImage",portraitKey},{"revision",static_cast<std::int64_t>(imageRevision)}};cropKey=key;
            }
        }else if(!contents.avatar.isNull()){contents.avatar=J{};cropKey.reset();options.images->retire(portraitKey);}
        if(options.frameSprite){const auto accent=state.accent();
            if(frameAccent!=accent){const auto tinted=m::profileStraight(m::profileTintedFrame(*options.frameSprite,accent));
                options.images->publish(frameKey,++imageRevision,tinted.width,tinted.height,tinted.rgba);
                contents.frame=J::Object{{"memoryImage",frameKey},{"revision",static_cast<std::int64_t>(imageRevision)}};frameAccent=accent;}}
        contents.background=background!=nullptr;contents.backgroundPixels=background?P{double(background->image.width),double(background->image.height)}:P{};
        if(!(contents==options.contents)){options.contents=std::move(contents);dirty=true;}
    }
    void content(){
        if(options.images&&(seenRevision!=state.revision()||dirty))imagery();
        if(!dirty&&seenRevision==state.revision()&&seenWork==state.workRevision()&&seenFont==raster.fontRevision())return;
        auto next=m::prepareProfileArtwork(state,options.appearance,options.contents);
        gpu::NativeProfileChange change;const bool dragging=state.dragging();
        if(active&&!dragging){change.update=m::profileUpdateAnimation(state,committed,state.profile());
            change.backdropMoved=synced&&(next.backdrop.frame!=art.backdrop.frame||next.backdrop.contentsRect!=art.backdrop.contentsRect);}
        change.popoverOpened=state.popoverOpens()!=seenOpens;change.popoverDismissed=active&&state.popoverDismissals()!=seenDismissals;
        change.visibilityToggled=state.visibilityToggles()!=seenToggles;
        seenOpens=state.popoverOpens();seenDismissals=state.popoverDismissals();seenToggles=state.visibilityToggles();
        scene.syncContent(next,++sceneRevision,time,change,options.reduceMotion);art=std::move(next);committed=state.profile();
        seenRevision=state.revision();seenWork=state.workRevision();seenFont=raster.fontRevision();dirty=false;synced=true;
        if(field)field->setAppearance({options.appearance.dark,state.accent(),fieldFailed});
    }
    void requests(){
        while(auto r=state.takeRequest()){
            switch(r->kind){
            case m::ProfileRequest::Kind::edit:if(r->field)begin(*r->field,r->rect,r->text);break;
            case m::ProfileRequest::Kind::chooseAvatar:if(finish(true)&&options.chooseImage)options.chooseImage(m::ProfileImageKind::avatar);break;
            case m::ProfileRequest::Kind::chooseBackground:if(finish(true)&&options.chooseImage)options.chooseImage(m::ProfileImageKind::background);break;
            case m::ProfileRequest::Kind::chooseColor:if(finish(true)&&options.chooseColor)options.chooseColor(r->rgb);break;
            }
            if(!alive)return;
        }
    }
    void begin(m::ProfileField f,R rect,const std::string&value){
        if(!active||!state.canEdit(f)||!finish(true))return;
        auto next=std::make_unique<gpu::NativeProfileEditorField>(hwnd,raster,options.raster,f,rect,value,gpu::NativeProfileFieldAppearance{options.appearance.dark,state.accent(),false},options.textMessage,nextGeneration());
        if(manager){const auto hr=next->editor().connect(*manager,client);if(!alive)return;need(SUCCEEDED(hr),"Cannot connect profile field to the shared TSF manager");
            if(ownerFocused){const auto f2=next->editor().focus(true);if(!alive)return;need(SUCCEEDED(f2)||locked(f2),"Cannot focus profile field");}}
        next->editor().command(gpu::ProjectedEditorCommand::selectAll);next->syncContent();
        field=std::move(next);fieldFailed=false;fieldOpened=options.reduceMotion?std::nullopt:std::optional(time);fieldDragging=false;poseField();
    }
    void retireField(){if(!field)return;if(fieldPublished){retired.push_back(std::move(field));need(retired.size()<=3,"Publish and collect earlier profile editors");}else field.reset();fieldPublished=false;fieldDragging=false;pendingFinish=false;fieldOpened.reset();}
    // HUDPersonalProfileInteraction.finishEditing.
    bool finish(bool commit){
        if(!field)return true;auto*current=field.get();const auto f=current->field();
        const bool shouldCommit=commit&&state.canEdit(f);
        if(shouldCommit){
            const auto hr=current->editor().commitComposition();if(!alive||field.get()!=current)return false;
            if(locked(hr)){pendingFinish=true;pendingCommit=true;return false;}need(SUCCEEDED(hr),"Cannot commit profile marked text");
            current->normalize(state.text());current->syncContent();
            if(!state.commit(f,current->text())){
                fieldFailed=true;current->setAppearance({options.appearance.dark,state.accent(),true});
                current->editor().command(gpu::ProjectedEditorCommand::selectAll);current->syncContent();content();return false;
            }
        }
        if(!current->stopInput()){pendingFinish=true;pendingCommit=false;return false;}
        if(!alive)return true;retireField();content();return true;
    }
    void poseField(){
        if(!field||!pose)return;const auto&p=*pose;const auto rect=field->rect();
        const auto camera=gpu::layerViewportProjection(metrics.pixelWidth,metrics.pixelHeight)*M::scale(metrics.scale,metrics.scale);
        const double fade=fieldOpened?.3+.7*std::clamp((time-*fieldOpened)/.14,0.,1.):1;
        field->updatePose({p.contentWorld*M::translation(rect.x,rect.y),camera,metrics.pixelWidth,metrics.pixelHeight,float(p.opacity*fade),input,ownerFocused&&input,std::span(&p.hostClip,1),p.shutter});
    }
    void deactivate(){
        if(!finish(true))finish(false);
        input=false;pressed=fieldDragging=false;state.deactivate();scene.setFeedback({},false,time,options.reduceMotion);
    }
    void feedback(){if(pose)scene.setFeedback(input&&point&&!state.dragging()?local(*point):std::nullopt,pressed,time,options.reduceMotion);}
    void startSection(bool target,bool animated){
        const double previous=section.value(time),prior=travel.value(time);
        section={previous,target?1.:0.,time,animated?.24:0,&easeInOut};
        travel=animated?Track{target&&previous<.001?16.:prior,target?0.:-16.,time,.24,&easeOut}:Track{0,0,time,0,&easeOut};
    }
};
ProfilePreview::ProfilePreview(HWND h,m::ProfileState&s,gpu::LayerRasterizer&r,ProfilePreviewOptions o,ITfThreadMgr*mgr,TfClientId id):impl_(std::make_shared<Impl>(h,s,r,std::move(o),mgr,id)){}
ProfilePreview::~ProfilePreview(){auto i=impl_;i->alive=false;if(i->field)i->field->stopInput();if(i->options.images){i->options.images->retire(i->portraitKey);i->options.images->retire(i->frameKey);}}
void ProfilePreview::resize(const app::ClientMetrics&m){need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Personal Profile client metrics");impl_->metrics=m;}
void ProfilePreview::setLanguage(core::Language l,double t){auto i=impl_;i->advance(t);i->state.setLanguage(l);i->content();}
void ProfilePreview::setAppearance(m::ProfileAppearance a,double t){auto i=impl_;i->advance(t);if(i->options.appearance==a)return;i->options.appearance=a;i->state.setHudAccent(a.hudAccent);i->dirty=true;i->content();}
void ProfilePreview::setReduceMotion(bool v,double t){auto i=impl_;i->advance(t);i->options.reduceMotion=v;}
void ProfilePreview::setContents(m::ProfileContents c,double t){auto i=impl_;i->advance(t);if(i->options.contents==c)return;i->options.contents=std::move(c);i->dirty=true;i->content();}
void ProfilePreview::setBackdropImage(std::optional<gpu::NativeProfileBackdropImage>image,double t){auto i=impl_;i->advance(t);i->scene.setBackdropImage(image);}
void ProfilePreview::setAvatar(std::shared_ptr<const gpu::ProfileDecodedImage>image,double t){
    auto i=impl_;i->advance(t);if(i->avatar==image)return;i->avatar=std::move(image);i->cropKey.reset();i->avatarWork.reset();
    if(i->avatar){const auto&v=i->avatar->image;const unsigned largest=std::max(v.width,v.height);
        if(largest>1024){const double s=1024./largest;i->avatarWork=m::profileResampled(v,std::max(1u,unsigned(std::lround(v.width*s))),std::max(1u,unsigned(std::lround(v.height*s))));}
        else i->avatarWork=v;}
    i->dirty=true;i->content();
}
void ProfilePreview::setBackground(std::shared_ptr<const gpu::ProfileDecodedImage>image,double t){
    auto i=impl_;i->advance(t);if(i->background==image)return;i->background=std::move(image);
    if(i->background)i->scene.setBackdropImage(gpu::NativeProfileBackdropImage{++i->imageRevision,i->background->image.width,i->background->image.height,i->background->image.rgba});
    else i->scene.setBackdropImage(std::nullopt);
    i->dirty=true;i->content();
}
void ProfilePreview::stateChanged(double t){
    auto i=impl_;i->advance(t);
    // HUDPersonalProfileInteraction.layoutAccessibility: a field the account
    // lock made read-only closes without committing.
    if(i->field&&!i->state.canEdit(i->field->field()))i->finish(false);
    if(!i->alive)return;i->content();i->requests();
}
void ProfilePreview::setOverlayVisible(bool v,double t){auto i=impl_;i->advance(t);if(i->overlay==v)return;i->overlay=v;if(!v&&i->active){i->active=false;i->deactivate();}}
std::optional<double>ProfilePreview::nextWakeTime()const noexcept{return impl_->state.nextWorkRefresh();}
void ProfilePreview::wake(double t){
    auto i=impl_;i->advance(t);
    if(i->state.wake(i->time)){i->content();if(i->active)i->scene.animateWork(i->time,i->options.reduceMotion);}
}
void ProfilePreview::update(const M&center,const core::source::DesktopChromeSettings&settings,const core::ModulePresentationSample&sample,float opacity,double t){
    auto i=impl_;i->advance(t);
    const auto*shown=sample.current.module==core::Module::profile?&sample.current:sample.incoming&&sample.incoming->module==core::Module::profile?&*sample.incoming:nullptr;
    const bool requested=i->overlay&&sample.requested==core::Module::profile;
    if(requested!=i->selected||!i->sectionPosed){i->startSection(requested,i->sectionPosed&&sample.acceptsModuleInput&&!i->options.reduceMotion);i->selected=requested;i->sectionPosed=true;}
    const bool active=requested&&shown;
    if(active!=i->active){i->active=active;if(active){i->state.activate(i->time);i->dirty=true;}else{i->deactivate();if(!i->alive)return;}}
    i->input=active&&sample.acceptsModuleInput;if(!i->input){i->pressed=false;if(i->field)i->field->editor().pointerUp();i->fieldDragging=false;}
    i->content();
    // Background host: module report placement without the module transition,
    // plus its own section travel (transform.translation.x before scale).
    const auto layout=core::source::DesktopChromeLayout::make(settings,center,{});const auto frame=core::moduleLocalContentFrame(core::Module::profile);
    i->backdropWorld=center*M::translation(500+i->travel.value(i->time),layout.reportCenterY)*M::scale(layout.reportScale,layout.reportScale)*M::translation(frame.x-220,frame.y-220);
    const double sectionOpacity=i->section.value(i->time);
    if(shown){i->surface.update(center,settings,*shown,opacity);i->pose=i->surface.pose();i->lastPose=i->pose;}else i->pose.reset();
    // The background keeps drawing through its own section fade-out.
    i->drawing=i->lastPose&&(shown||sectionOpacity>0||i->section.active(i->time));
    if(!i->drawing){i->registration.update({});return;}
    const auto&p=*i->lastPose;
    if(i->pose){const auto camera=gpu::layerViewportProjection(i->metrics.pixelWidth,i->metrics.pixelHeight)*M::scale(i->metrics.scale,i->metrics.scale);
        i->projection=core::Projection::viewport(camera*p.contentWorld,i->metrics.pixelWidth,i->metrics.pixelHeight);i->registration.update(p.registration);}
    else i->registration.update({});
    i->feedback();
    i->scene.updatePose({p.contentWorld,i->backdropWorld,i->pose?p.opacity:0.f,float(opacity*sectionOpacity),i->time,std::span(&p.hostClip,1),p.shutter});
    if(i->field){i->field->syncContent();i->poseField();}
}
bool ProfilePreview::requiresFrames(double t)const{
    const auto&i=*impl_;t=std::max(t,i.time);if(!i.drawing)return false;
    return i.scene.requiresFrames(t)||i.section.active(t)||i.travel.active(t)||(i.field&&i.fieldOpened&&t<*i.fieldOpened+.14);
}
bool ProfilePreview::covers(P p)const{const auto q=impl_->local(p);return q&&(inside(canvas,*q)||impl_->state.containsPopoverPoint(*q));}
bool ProfilePreview::capturesPointer()const noexcept{return impl_->active&&impl_->state.popover().has_value();}
bool ProfilePreview::pointerLocked()const noexcept{return impl_->state.dragging()||impl_->fieldDragging||bool(impl_->field);}
bool ProfilePreview::pointer(const app::PointerEvent&e,double t){
    auto i=impl_;i->advance(t);i->point=P{e.x,e.y};const auto q=i->local(*i->point);const P physical{e.x*i->metrics.scale,e.y*i->metrics.scale};
    if(e.kind==app::PointerKind::captureLost||(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left)){
        const bool owned=i->pressed||i->fieldDragging||i->state.dragging();i->pressed=false;
        if(i->field)i->field->editor().pointerUp();i->fieldDragging=false;
        if(i->state.dragging()){i->state.mouseUp();i->content();}
        i->feedback();return owned;
    }
    if(e.kind==app::PointerKind::leave){if(!i->state.dragging())i->point.reset();i->feedback();return false;}
    if(!i->input)return false;
    if(e.kind==app::PointerKind::move){
        if(i->state.dragging()){if(q){i->state.mouseDragged(*q);i->content();}return true;}
        if(i->fieldDragging&&i->field){i->field->editor().pointerDrag(physical);i->field->syncContent();return true;}
        i->feedback();return capturesPointer()||(q&&inside(canvas,*q));
    }
    if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left)return capturesPointer();
    // An open popover consumes every click; outside the card it dismisses.
    if(i->state.popover()&&(!q||!inside(canvas,*q))){if(i->finish(true))i->state.dismissPopover();if(i->alive)i->content();return true;}
    if(!q||!inside(canvas,*q))return false;
    if(i->field&&inside(i->field->rect(),*q)){i->field->syncContent();i->fieldDragging=i->field->editor().pointerDown(physical,(e.modifiers&MK_SHIFT)!=0).handled;i->field->syncContent();return true;}
    if(!i->finish(true))return true;if(!i->alive)return true;
    i->pressed=true;i->state.mouseDown(*q);if(!i->alive)return true;i->content();i->requests();if(i->alive)i->feedback();return true;
}
bool ProfilePreview::key(const app::KeyEvent&e,double t){
    auto i=impl_;i->advance(t);if(!i->input)return false;
    const bool shift=GetKeyState(VK_SHIFT)<0,control=GetKeyState(VK_CONTROL)<0,system=e.alt||GetKeyState(VK_MENU)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0;
    if(i->field){
        auto&f=*i->field;
        if(e.kind==app::KeyKind::character||e.kind==app::KeyKind::unicodeCharacter){
            if(e.value=='\r'||e.value=='\n'||e.value=='\t'||e.value==27)return true; // routed by key-down
            if(control||system)return true;f.editor().character(e.value,e.kind==app::KeyKind::unicodeCharacter);f.normalize(i->state.text());f.syncContent();return true;
        }
        if(e.kind!=app::KeyKind::down)return false;
        const bool composing=f.document().composition().has_value();
        if(!composing&&!control&&!system){
            if(e.value==VK_ESCAPE&&!shift){i->finish(false);return true;}
            if((e.value==VK_RETURN||e.value==VK_TAB)&&!shift){i->finish(true);return true;}
            if(e.value==VK_RETURN&&shift&&f.field()==m::ProfileField::introduction){f.editor().character('\n');f.normalize(i->state.text());f.syncContent();return true;}
        }
        using C=gpu::ProjectedEditorCommand;if(system)return false;
        if(control){
            if(e.value=='A')f.editor().command(C::selectAll);else if(e.value=='Z'){if(shift)f.editor().redo();else f.editor().undo();}else if(e.value=='Y')f.editor().redo();else return false;
            f.normalize(i->state.text());f.syncContent();return true;
        }
        std::optional<C>command;
        switch(e.value){case VK_LEFT:command=C::left;break;case VK_RIGHT:command=C::right;break;case VK_UP:command=C::up;break;case VK_DOWN:command=C::down;break;
            case VK_HOME:command=C::documentStart;break;case VK_END:command=C::documentEnd;break;case VK_BACK:command=C::backspace;break;case VK_DELETE:command=C::deleteForward;break;default:break;}
        if(!command)return false;f.editor().command(*command,shift);f.normalize(i->state.text());f.syncContent();return true;
    }
    if(e.kind!=app::KeyKind::down||shift||control||system)return false;
    if(e.value==VK_ESCAPE&&i->state.popover()){i->state.dismissPopover();i->content();return true;}
    if((e.value==VK_LEFT||e.value==VK_RIGHT)&&!i->state.sliders().empty()){const bool used=i->state.nudgeSlider(e.value==VK_LEFT?-1:1);i->content();return used;}
    return false;
}
bool ProfilePreview::filterKey(const app::NativeMessage&m){auto i=impl_;return i->field&&i->input&&i->ownerFocused&&m.window==i->hwnd&&i->field->editor().filterKeyMessage(m.message,m.wParam,m.lParam);}
bool ProfilePreview::message(const app::NativeMessage&m,double t){
    auto i=impl_;if(m.window!=i->hwnd||m.message!=i->options.textMessage)return false;i->advance(t);
    if(!i->field||i->field->generation()!=m.wParam)return true;auto*current=i->field.get();
    if(!current->editor().takeChanges(m.wParam))return true;
    if(i->pendingFinish){i->pendingFinish=false;i->finish(i->pendingCommit);return true;}
    if(i->pendingBlur){i->pendingBlur=false;focus(false,t);if(!i->alive||i->field.get()!=current)return true;}
    current->normalize(i->state.text());current->syncContent();return true;
}
void ProfilePreview::focus(bool focused,double t){
    auto i=impl_;i->advance(t);i->ownerFocused=focused;if(!i->field||!i->manager)return;
    const auto hr=i->field->editor().focus(focused);if(locked(hr)){i->pendingBlur=!focused;return;}need(SUCCEEDED(hr),"Cannot update profile field focus");
}
void ProfilePreview::cancelInteraction(double t){
    auto i=impl_;i->advance(t);i->pressed=false;i->point.reset();if(i->field)i->field->editor().pointerUp();i->fieldDragging=false;
    if(i->state.dragging()){i->state.mouseUp();i->content();}i->feedback();
}
bool ProfilePreview::active()const noexcept{return impl_->active&&impl_->overlay;}
bool ProfilePreview::editing()const noexcept{return bool(impl_->field);}
std::optional<m::ProfileField>ProfilePreview::editingField()const noexcept{return impl_->field?std::optional(impl_->field->field()):std::nullopt;}
bool ProfilePreview::finishEditing(bool commit,double t){auto i=impl_;i->advance(t);return i->finish(commit);}
const m::ProfileArtwork&ProfilePreview::artwork()const noexcept{return impl_->art;}
void ProfilePreview::upload(gpu::Renderer&r){
    auto i=impl_;need(!i->renderer||i->renderer==&r,"Personal Profile resources belong to another renderer");i->renderer=&r;if(!i->lastPose)return;
    i->scene.uploadResources(r);i->registration.uploadGeometry(r);if(i->field)i->field->upload(r);
}
std::span<const gpu::LayerCompositionEntry>ProfilePreview::entries(){
    auto i=impl_;i->composed.clear();if(!i->drawing||!i->synced)return {};
    for(const auto&e:i->scene.entries())i->composed.push_back(e);
    if(i->field&&i->pose){i->composed.push_back(i->field->entry());i->fieldPublished=true;}
    if(i->pose)i->composed.push_back({&i->geometry,i->registration.draws()});
    return i->composed;
}
void ProfilePreview::collected(gpu::Renderer&r){
    auto i=impl_;need(i->scene.collectRetired(r),"Personal Profile artwork remains published");
    for(auto it=i->retired.begin();it!=i->retired.end();)if((*it)->releaseResources(r))it=i->retired.erase(it);else ++it;
}
void ProfilePreview::release(gpu::Renderer&r){
    auto i=impl_;need(!i->field||i->finish(false),"Profile editor transaction must finish before release");collected(r);
    need(i->retired.empty()&&i->scene.releaseResources(r)&&i->registration.releaseResources(r)&&i->geometry.releaseResources(r),"Detach Personal Profile composition before release");i->renderer=nullptr;
}
}
#endif
