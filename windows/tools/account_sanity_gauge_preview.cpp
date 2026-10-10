#include "tools/account_sanity_gauge_preview.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace endfield::tools {
namespace {
namespace gpu=native;namespace h=modules::hypergryph;namespace g=modules::hypergryph::gauge_geometry;using Matrix=core::Matrix4;
void need(bool v,const char* s) {if(!v) throw std::invalid_argument(s);}
}
struct AccountSanityGaugePreview::Impl {
    h::HypergryphAccountController& controller;std::shared_ptr<const h::GaugeAssets> assets;h::SanityGaugeModel model;gpu::NativeSanityGaugeScene scene;
    AccountSanityGaugeOptions options;app::ClientMetrics metrics;core::Projection projection;bool overlayVisible{},posed{};double time{};
    Impl(h::HypergryphAccountController& c,gpu::LayerRasterizer& r,std::shared_ptr<const h::GaugeAssets> a,AccountSanityGaugeOptions o)
        :controller(c),assets(a),scene(a,r,o.raster),options(std::move(o)) {
        // Mac: accountGauge.onRefresh = { accountController.refresh(manual: true) }.
        model.onRefresh=[this]{controller.refresh(true);refresh();};
    }
    void advance(double t) {need(std::isfinite(t),"Gauge owner needs a finite clock");time=std::max(time,t);}
    double scale() const {return std::clamp(metrics.scale>0?metrics.scale:2,1.0,3.0);}
    h::WorkModeGaugeInput work() const {return options.workMode?options.workMode():h::WorkModeGaugeInput{};}
    void refresh() {
        const auto value=controller.gaugeValue(work());
        model.update(value.value,value.accessibilityLabel,value.visible&&overlayVisible,scale(),controller.sanityPresentation(),controller.now(),options.language);
        scene.sync(model,options.language,time,options.reduceMotion);
    }
    std::optional<core::Point> local(core::Point p) const {
        if(!posed||!std::isfinite(p.x)||!std::isfinite(p.y)) return std::nullopt;
        return projection.unproject({p.x*metrics.scale,p.y*metrics.scale});
    }
    double toHost(double controllerTime) const {return time+std::max(0.0,controllerTime-controller.now());}
    // HypergryphAccountController.gaugeValue shows Work Mode minutes unless the
    // header is hidden or a linked game (sanity) mode is selected.
    bool showsWorkMode() const {
        using M=h::AccountPresentation::HeaderMode;const auto mode=controller.headerMode();
        return mode!=M::hidden&&!(controller.record().linked&&(mode==M::endfield||mode==M::arknights));
    }
};
AccountSanityGaugePreview::AccountSanityGaugePreview(h::HypergryphAccountController& c,gpu::LayerRasterizer& r,const std::filesystem::path& resources,AccountSanityGaugeOptions o)
    :impl_(std::make_unique<Impl>(c,r,std::make_shared<const h::GaugeAssets>(h::GaugeAssets::load(resources)),std::move(o))) {}
AccountSanityGaugePreview::~AccountSanityGaugePreview()=default;
void AccountSanityGaugePreview::resize(const app::ClientMetrics& m) {
    need(m.pixelWidth&&m.pixelHeight&&std::isfinite(m.scale)&&m.scale>0,"Invalid gauge viewport");
    const bool rescale=impl_->metrics.scale!=m.scale;impl_->metrics=m;if(rescale) impl_->refresh();
}
void AccountSanityGaugePreview::setLanguage(core::Language l,double t) {impl_->advance(t);impl_->options.language=l;impl_->refresh();}
void AccountSanityGaugePreview::setReduceMotion(bool v) noexcept {impl_->options.reduceMotion=v;}
void AccountSanityGaugePreview::setOverlayVisible(bool v,double t) {
    auto& i=*impl_;i.advance(t);if(i.overlayVisible==v) return;i.overlayVisible=v;
    if(!v) {i.model.dismiss();i.scene.setHover(std::nullopt,i.time,true);}
    i.refresh();
}
void AccountSanityGaugePreview::contentChanged(double t) {impl_->advance(t);impl_->refresh();}
void AccountSanityGaugePreview::update(const Matrix& center,float opacity,double t) {
    auto& i=*impl_;i.advance(t);need(i.metrics.pixelWidth&&i.metrics.pixelHeight,"Resize the gauge before presentation");
    // Header frame origin (270,2) plus HUDAccountGauge.headerPosition (292,0).
    const auto world=center*Matrix::translation(g::headerOrigin.x+g::headerPosition.x,g::headerOrigin.y+g::headerPosition.y);
    const auto camera=gpu::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight)*Matrix::scale(i.metrics.scale,i.metrics.scale);
    i.projection=core::Projection::viewport(camera*world,i.metrics.pixelWidth,i.metrics.pixelHeight);i.posed=true;
    i.scene.updatePose(world,opacity,i.time);
}
bool AccountSanityGaugePreview::requiresFrames(double t) const {return impl_->scene.requiresFrames(std::max(t,impl_->time));}
bool AccountSanityGaugePreview::covers(core::Point p) const {
    const auto& i=*impl_;if(!i.scene.visible()) return false;const auto q=i.local(p);if(!q) return false;
    const bool wallet=q->x>=0&&q->y>=0&&q->x<g::bounds.width&&q->y<g::bounds.height;
    const bool popover=i.model.popoverOpen();
    return (i.model.canOpen()&&wallet)||popover;
}
bool AccountSanityGaugePreview::capturesPointer() const noexcept {return impl_->model.popoverOpen();}
bool AccountSanityGaugePreview::pointer(const app::PointerEvent& e,double t) {
    auto& i=*impl_;i.advance(t);const auto q=i.local({e.x,e.y});
    if(e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost) {i.scene.setHover(std::nullopt,i.time,i.options.reduceMotion);return false;}
    if(e.kind==app::PointerKind::move) {i.scene.setHover(i.model.hover(q),i.time,i.options.reduceMotion);return i.model.popoverOpen();}
    if((e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)&&e.button==app::PointerButton::left) {
        const bool handled=i.model.mouseDown(q);
        if(handled) {i.scene.sync(i.model,i.options.language,i.time,i.options.reduceMotion);i.scene.setHover(i.model.hover(q),i.time,i.options.reduceMotion);}
        return handled;
    }
    return i.model.popoverOpen()&&e.kind!=app::PointerKind::up;
}
bool AccountSanityGaugePreview::key(const app::KeyEvent& e,bool modified,double t) {
    auto& i=*impl_;i.advance(t);if(!i.model.popoverOpen()||e.kind!=app::KeyKind::down) return false;
    if(e.previouslyDown) return true; // SystemHUDView.keyDown ignores auto-repeat (still consumed)
    if(!modified&&e.value==VK_ESCAPE) i.model.dismiss();
    else if(!modified&&(e.value==VK_RETURN||e.value==VK_SPACE)) i.model.perform("refresh");
    i.scene.sync(i.model,i.options.language,i.time,i.options.reduceMotion);
    return true; // the open recovery menu owns the keyboard (host checks its summon shortcut first)
}
bool AccountSanityGaugePreview::dismiss(double t) {auto& i=*impl_;i.advance(t);const bool closed=i.model.dismiss();if(closed) i.scene.sync(i.model,i.options.language,i.time,i.options.reduceMotion);return closed;}
std::optional<double> AccountSanityGaugePreview::nextWakeTime(double hostTime) const {
    const auto& i=*impl_;std::optional<double> wake;
    const auto earliest=[&](double at){wake=wake?std::min(*wake,at):at;};
    if(const auto c=i.controller.nextWakeTime()) earliest(hostTime+std::max(0.0,*c-i.controller.now()));
    if(const auto tip=i.model.nextTooltipChange()) earliest(hostTime+std::max(0.0,*tip-i.controller.now()));
    if(i.overlayVisible&&i.options.workModeRunning&&i.options.workModeRunning()&&i.showsWorkMode()) {
        const auto w=i.work();double delta;
        if(w.countdown) {const double remaining=std::max(0.0,w.duration-w.elapsed);const double shown=std::ceil(remaining/60);delta=remaining-60*(shown-1);}
        else {const double shown=std::floor(w.elapsed/60);delta=60*(shown+1)-w.elapsed;}
        if(std::isfinite(delta)&&delta>0) earliest(hostTime+delta);
    }
    return wake;
}
bool AccountSanityGaugePreview::deadline(double t) {
    auto& i=*impl_;i.advance(t);i.controller.tick();const auto before=i.model.updateCount()+i.model.tooltipRenders();i.refresh();
    return i.model.updateCount()+i.model.tooltipRenders()!=before;
}
const h::SanityGaugeModel& AccountSanityGaugePreview::model() const noexcept {return impl_->model;}
std::vector<h::GaugeAction> AccountSanityGaugePreview::accessibleActions() const {return impl_->model.accessibleActions(impl_->options.language);}
gpu::NativeSanityGaugeStats AccountSanityGaugePreview::sceneStats() const noexcept {return impl_->scene.stats();}
void AccountSanityGaugePreview::upload(gpu::Renderer& r) {if(impl_->posed) impl_->scene.upload(r);}
std::span<const gpu::LayerCompositionEntry> AccountSanityGaugePreview::entries() {return impl_->posed?impl_->scene.entries():std::span<const gpu::LayerCompositionEntry>{};}
void AccountSanityGaugePreview::collected(gpu::Renderer& r) {impl_->scene.collected(r);}
void AccountSanityGaugePreview::release(gpu::Renderer& r) {need(impl_->scene.release(r),"Detach the gauge composition before releasing its resources");}
}
#endif
