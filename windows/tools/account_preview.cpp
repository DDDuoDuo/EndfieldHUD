#include "tools/account_preview.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "native/module_scene.hpp"
#include "native/module_registration.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::tools {
namespace {
namespace gpu=native;namespace h=modules::hypergryph;using Matrix=core::Matrix4;using Json=ehud::data::Json;
void need(bool v,const char* s) {if(!v) throw std::invalid_argument(s);}
bool contains(core::Rect r,core::Point p) {return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
Json empty() {return Json::Object{{"bounds",Json::Array{0,0,400,334}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}};}
}
struct AccountPreview::Impl {
    h::HypergryphAccountController& controller;h::AccountCanvasModel canvas;gpu::NativeAccountCanvasScene scene;gpu::LayerScene geometry;
    std::unique_ptr<gpu::NativeModuleSurface> surface;gpu::NativeModuleRegistration registration{"account.registration"};
    std::array<gpu::LayerCompositionEntry,2> composed;app::ClientMetrics metrics;core::Projection projection;std::optional<gpu::ModuleSurfacePose> pose;
    bool overlayVisible{},selected{},active{},acceptsInput{},pressed{},reduced{};double time{};std::optional<core::Point> hover;
    gpu::Renderer* renderer{}; // the host's, from upload() (conceal() parks through it)
    Impl(h::HypergryphAccountController& c,gpu::LayerRasterizer& r,AccountPreviewOptions o)
        :controller(c),scene(canvas,r,o.raster,o.style),geometry(r),reduced(o.reduceMotion) {
        geometry.load(empty(),o.raster);surface=std::make_unique<gpu::NativeModuleSurface>(geometry,core::Module::account);
        canvas.setLanguage(o.language);controller.setLanguage(o.language);
        canvas.onAction=[this](const h::AccountAction& a){controller.perform(a);sync();};
        canvas.update(controller.presentation());
    }
    void advance(double t) {need(std::isfinite(t),"Account owner needs a finite clock");time=std::max(time,t);}
    void sync() {canvas.update(controller.presentation());if(pose||active) scene.syncContent(time,reduced);}
    void visibility() {controller.setVisible(overlayVisible,selected);}
    std::optional<core::Point> local(core::Point p) const {return acceptsInput&&std::isfinite(p.x)&&std::isfinite(p.y)?projection.unproject({p.x*metrics.scale,p.y*metrics.scale}):std::nullopt;}
    void feedback() {if(active&&pose) {scene.syncContent(time,reduced);scene.setFeedback(hover,pressed,reduced,time);}}
};
AccountPreview::AccountPreview(h::HypergryphAccountController& c,gpu::LayerRasterizer& r,AccountPreviewOptions o):impl_(std::make_unique<Impl>(c,r,std::move(o))) {}
AccountPreview::~AccountPreview() {impl_->canvas.onAction={};}
void AccountPreview::resize(const app::ClientMetrics& m) {need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid Account viewport");impl_->metrics=m;}
void AccountPreview::setLanguage(core::Language l,double t) {auto& i=*impl_;i.advance(t);i.controller.setLanguage(l);i.canvas.setLanguage(l);i.sync();}
void AccountPreview::setStyle(h::CanvasStyle s,double t) {auto& i=*impl_;i.advance(t);i.scene.setStyle(s);i.sync();}
void AccountPreview::setReduceMotion(bool v) noexcept {impl_->reduced=v;}
void AccountPreview::setOverlayVisible(bool v,double t) {
    auto& i=*impl_;i.advance(t);if(i.overlayVisible==v) return;i.overlayVisible=v;
    // Closing the HUD dismisses an open account menu immediately (Mac setVisible(false)).
    if(!v) {i.active=false;i.acceptsInput=false;i.pressed=false;i.hover.reset();i.canvas.setVisible(false);}
    i.visibility();i.sync();
}
void AccountPreview::presentationChanged(double t) {auto& i=*impl_;i.advance(t);i.sync();}
void AccountPreview::update(const Matrix& center,const core::source::DesktopChromeSettings& settings,const core::ModulePresentationSample& sample,float opacity,double t) {
    auto& i=*impl_;i.advance(t);need(i.metrics.pixelWidth&&i.metrics.pixelHeight,"Resize Account before presentation");
    const bool selected=sample.requested==core::Module::account;
    if(selected!=i.selected) {i.selected=selected;i.visibility();}
    const auto* shown=sample.current.module==core::Module::account?&sample.current:sample.incoming&&sample.incoming->module==core::Module::account?&*sample.incoming:nullptr;
    const bool active=shown&&selected&&i.overlayVisible;
    if(active!=i.active) {i.active=active;i.pressed=false;i.hover.reset();i.canvas.setVisible(active);if(active) i.canvas.update(i.controller.presentation());}
    i.acceptsInput=active&&sample.acceptsModuleInput;
    if(!i.acceptsInput&&i.canvas.isPopoverOpen()) i.canvas.dismissPopover(false);
    if(!shown) {i.pose.reset();i.registration.update({});return;}
    // Content arrives through presentationChanged(); the frame path only
    // re-poses retained surfaces (no presentation rebuild or allocation).
    i.scene.syncContent(i.time,i.reduced);
    i.surface->update(center,settings,*shown,opacity);i.pose=i.surface->pose();const auto& p=*i.pose;
    const auto camera=gpu::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight)*Matrix::scale(i.metrics.scale,i.metrics.scale);
    i.projection=core::Projection::viewport(camera*p.contentWorld,i.metrics.pixelWidth,i.metrics.pixelHeight);
    i.scene.updatePose(p.contentWorld,p.opacity,i.time,std::span(&p.hostClip,1),p.shutter?&*p.shutter:nullptr);i.registration.update(p.registration);
}
bool AccountPreview::requiresFrames(double t) const {const auto& i=*impl_;return i.pose&&i.scene.requiresFrames(std::max(t,i.time));}
bool AccountPreview::covers(core::Point p) const {const auto q=impl_->local(p);return q&&(contains(h::AccountCanvasModel::bounds,*q)||impl_->canvas.isPopoverOpen());}
bool AccountPreview::capturesPointer() const noexcept {return impl_->acceptsInput&&impl_->canvas.isPopoverOpen();}
bool AccountPreview::pointer(const app::PointerEvent& e,double t) {
    auto& i=*impl_;i.advance(t);const auto q=i.local({e.x,e.y});
    if(e.kind==app::PointerKind::captureLost||e.kind==app::PointerKind::leave) {const bool was=i.pressed;i.pressed=false;i.hover.reset();i.feedback();return was;}
    if(e.kind==app::PointerKind::up&&e.button==app::PointerButton::left) {const bool was=i.pressed;i.pressed=false;i.hover=q;i.feedback();return was;}
    if(!i.acceptsInput) return false;
    if(e.kind==app::PointerKind::move) {i.hover=q;i.feedback();return i.canvas.isPopoverOpen()||(q&&contains(h::AccountCanvasModel::bounds,*q));}
    if((e.kind!=app::PointerKind::down&&e.kind!=app::PointerKind::doubleClick)||e.button!=app::PointerButton::left) return i.canvas.isPopoverOpen();
    if(i.canvas.isPopoverOpen()) {i.canvas.mouseDown(q.value_or(core::Point{-1,-1}));i.sync();i.hover=q;i.feedback();return true;}
    if(!q||!contains(h::AccountCanvasModel::bounds,*q)) return false;
    i.pressed=true;i.hover=q;i.canvas.mouseDown(*q);i.sync();i.feedback();return true;
}
bool AccountPreview::wheel(const app::WheelEvent& e,double t) {
    auto& i=*impl_;i.advance(t);if(!i.acceptsInput||!i.canvas.isPopoverOpen()) return false;
    const auto q=i.local({e.x,e.y});
    if(q&&!e.horizontal&&std::isfinite(e.steps)&&e.linesPerStep) {
        const double travel=e.linesPerStep==UINT32_MAX?i.canvas.listRect().height:12.*e.linesPerStep;
        i.canvas.scroll(*q,-e.steps*travel);i.scene.syncContent(i.time,i.reduced);i.feedback();
    }
    return true;
}
bool AccountPreview::key(const app::KeyEvent& e,bool modified,double t) {
    auto& i=*impl_;i.advance(t);if(!i.acceptsInput||!i.canvas.isPopoverOpen()||e.kind!=app::KeyKind::down||modified) return false;
    if(e.previouslyDown) return true; // SystemHUDView.keyDown ignores auto-repeat while a menu owns the keys
    std::optional<h::AccountCanvasModel::Key> key;
    switch(e.value) {case VK_ESCAPE: key=h::AccountCanvasModel::Key::escape;break;case VK_DOWN: key=h::AccountCanvasModel::Key::down;break;
        case VK_UP: key=h::AccountCanvasModel::Key::up;break;case VK_RETURN: case VK_SPACE: key=h::AccountCanvasModel::Key::activate;break;default: break;}
    if(!key) return false;
    i.canvas.key(*key);i.sync();i.feedback();return true;
}
bool AccountPreview::dismissMenu(double t) {auto& i=*impl_;i.advance(t);const bool closed=i.canvas.dismissPopover(true);if(closed) i.sync();return closed;}
void AccountPreview::cancelInteraction(double t) {auto& i=*impl_;i.advance(t);i.pressed=false;i.hover.reset();i.feedback();}
std::optional<double> AccountPreview::nextWakeTime(double hostTime) const {
    const auto& i=*impl_;const auto c=i.controller.nextWakeTime();if(!c) return std::nullopt;
    return hostTime+std::max(0.0,*c-i.controller.now());
}
bool AccountPreview::deadline(double t) {auto& i=*impl_;i.advance(t);i.controller.tick();const auto before=i.canvas.contentRevision();i.sync();return i.canvas.contentRevision()!=before;}
const h::AccountCanvasModel& AccountPreview::canvas() const noexcept {return impl_->canvas;}
bool AccountPreview::accessible() const noexcept {return impl_->active&&impl_->pose.has_value();}
std::optional<core::Rect> AccountPreview::clientRect(core::Rect r) const {
    const auto& i=*impl_;if(!accessible()||!(i.metrics.scale>0)) return std::nullopt;
    double left{},top{},right{},bottom{};bool first=true;
    for(const core::Point corner:{core::Point{r.x,r.y},core::Point{r.x+r.width,r.y},core::Point{r.x,r.y+r.height},core::Point{r.x+r.width,r.y+r.height}}) {
        const auto p=i.projection.project(corner);if(!p||!std::isfinite(p->x)||!std::isfinite(p->y)) return std::nullopt;
        const double x=p->x/i.metrics.scale,y=p->y/i.metrics.scale;
        if(first) {left=right=x;top=bottom=y;first=false;} else {left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}
    }
    return core::Rect{left,top,right-left,bottom-top};
}
bool AccountPreview::perform(std::string_view id,double t) {
    // HUDAccountInteraction AX press -> canvas.perform(id) (accessibilityPerformPress refuses disabled peers).
    auto& i=*impl_;i.advance(t);if(!accessible()) return false;
    const auto controls=i.canvas.accessibleActions();
    const auto hit=std::find_if(controls.begin(),controls.end(),[&](const auto& c){return c.id==id;});
    if(hit==controls.end()||!hit->enabled) return false;
    i.canvas.perform(id);i.sync();i.feedback();return true;
}
gpu::NativeAccountSceneStats AccountPreview::sceneStats() const noexcept {return impl_->scene.stats();}
void AccountPreview::upload(gpu::Renderer& r) {
    auto& i=*impl_;if(!i.pose) return;i.renderer=&r;
    i.scene.uploadResources(r);i.registration.uploadGeometry(r); // revision-checked, no allocation when unchanged
}
bool AccountPreview::conceal() {
    // Only a face that is not shown: a close with Account selected keeps it
    // for the next opening (its entry is still published until then).
    auto& i=*impl_;if(i.pose||i.active) return false;
    return i.scene.park(i.renderer);
}
std::span<const gpu::LayerCompositionEntry> AccountPreview::entries() {
    auto& i=*impl_;if(!i.pose) return {};const auto group=i.scene.entry();if(!group) return {};
    // Canvas group first, then the module registration mark above it.
    i.composed[0]=*group;i.composed[1]={&i.geometry,i.registration.draws()};return i.composed;
}
void AccountPreview::collected(gpu::Renderer&) {} // the retained group collects its own retired leaves on upload
void AccountPreview::release(gpu::Renderer& r) {
    auto& i=*impl_;
    need(i.scene.releaseResources(r)&&i.registration.releaseResources(r)&&i.geometry.releaseResources(r),"Detach the Account composition before releasing its resources");
}
}
#endif
