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
    gpu::Renderer* renderer{}; // the host's, from upload() (conceal() parks through it)
    gpu::LayerRasterizer* raster;
    Impl(h::HypergryphAccountController& c,gpu::LayerRasterizer& r,std::shared_ptr<const h::GaugeAssets> a,AccountSanityGaugeOptions o)
        :controller(c),assets(a),scene(a,r,o.raster),options(std::move(o)),raster(&r) {
        // Mac: accountGauge.onRefresh = { accountController.refresh(manual: true) }.
        model.onRefresh=[this]{controller.refresh(true);refresh();};
        // Mac 1.3.0: accountGauge.onOpenWorkMode = activateNavigationTarget(.module(.workMode)).
        model.onOpenWorkMode=[this]{if(options.openWorkMode) {auto open=options.openWorkMode;open();}};
        // HUDAccountGauge.Layout measures the menu's text: the source numerals'
        // advances, else HUDText.width (standard faces, 14.4 medium, the UI
        // region; a line-box measurement, rounded up to .5). Content events only.
        model.widths.numerals=[this](std::string_view text){return assets->numerals.width(text,h::GaugePopoverLayout::textSize);};
        model.widths.standard=[this](std::string_view text){
            const auto region=core::regionFor(options.language,raster->fontLanguage());
            const auto descriptor=ehud::data::Json::Object{{"string",std::string(text)},{"fontSize",h::GaugePopoverLayout::textSize},{"fontWeight",500},
                {"lineHeight",1},{"maximumLines",1},{"language",std::string(core::regionTag(region))}};
            return raster->measureSourceText("account.gauge.label",descriptor,1e6,options.raster).width;
        };
    }
    void advance(double t) {need(std::isfinite(t),"Gauge owner needs a finite clock");time=std::max(time,t);}
    double scale() const {return std::clamp(metrics.scale>0?metrics.scale:2,1.0,3.0);}
    h::WorkModeGaugeInput work() const {return options.workMode?options.workMode():h::WorkModeGaugeInput{};}
    // HUDAccountGauge.update(visible:) follows the header mode only: the
    // wallet stays in the header while the HUD closes and fades out with it
    // (it used to vanish when the close began). Input is the open HUD's only.
    void refresh() {
        const auto value=controller.gaugeValue(work());
        model.update(value.value,value.accessibilityLabel,value.visible,scale(),controller.sanityPresentation(),controller.now(),options.language);
        // HUDAccountGauge.setOpen: a bar that takes no input drops its hover;
        // the Work Mode link keeps it across the clock's updates.
        if(!model.isInteractive()) scene.setHover(std::nullopt,time,options.reduceMotion);
        scene.sync(model,options.language,time,options.reduceMotion);
    }
    std::optional<core::Point> local(core::Point p) const {
        if(!posed||!std::isfinite(p.x)||!std::isfinite(p.y)) return std::nullopt;
        return projection.unproject({p.x*metrics.scale,p.y*metrics.scale});
    }
    double toHost(double controllerTime) const {return time+std::max(0.0,controllerTime-controller.now());}
    // HUDAccountGauge.size at the gauge origin (its press and hit rect).
    static bool inWallet(core::Point q) {return q.x>=0&&q.y>=0&&q.x<g::bounds.width&&q.y<g::bounds.height;}
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
// The shown wallet is a header control on every module, whatever it shows
// (sanity, "— / —" before a snapshot, or Work Mode minutes): its press never
// reaches the source shell beneath it or the host's close for a press outside
// the displayed circle (most of the wallet lies outside it). The open popover
// owns every point.
bool AccountSanityGaugePreview::covers(core::Point p) const {
    const auto& i=*impl_;if(!i.overlayVisible||!i.scene.visible()) return false;
    if(i.model.popoverOpen()) return true;
    const auto q=i.local(p);return q&&i.inWallet(*q);
}
bool AccountSanityGaugePreview::capturesPointer() const noexcept {return impl_->model.popoverOpen();}
bool AccountSanityGaugePreview::pointer(const app::PointerEvent& e,double t) {
    auto& i=*impl_;i.advance(t);const auto q=i.local({e.x,e.y});
    if(e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost) {i.scene.setHover(std::nullopt,i.time,i.options.reduceMotion);return false;}
    if(e.kind==app::PointerKind::move) {i.scene.setHover(i.model.hover(q),i.time,i.options.reduceMotion);return i.model.popoverOpen();}
    if((e.kind==app::PointerKind::down||e.kind==app::PointerKind::doubleClick)&&e.button==app::PointerButton::left) {
        if(!i.overlayVisible) return false; // a closing or hidden HUD takes no press (allowsModuleInput)
        // HUDAccountGauge.mouseDown: the wallet with sanity toggles its
        // recovery popover; without sanity data (Work Mode minutes, or a
        // linked game with no snapshot yet, "— / —") it opens Work Mode; an
        // open popover takes every press.
        if(i.model.mouseDown(q)) {i.scene.sync(i.model,i.options.language,i.time,i.options.reduceMotion);i.scene.setHover(i.model.hover(q),i.time,i.options.reduceMotion);return true;}
        // SystemHUDView's sanity-bar rectangle guard: a press inside the shown
        // wallet never reaches the HUD close (covers()).
        return i.scene.visible()&&q&&i.inWallet(*q);
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
std::optional<core::Point> AccountSanityGaugePreview::clientPoint(core::Point local) const {
    const auto& i=*impl_;if(!i.posed||!(i.metrics.scale>0)) return std::nullopt;
    const auto p=i.projection.project(local);if(!p) return std::nullopt;
    return core::Point{p->x/i.metrics.scale,p->y/i.metrics.scale};
}
std::optional<core::Rect> AccountSanityGaugePreview::clientRect(core::Rect r) const {
    if(!impl_->scene.visible()) return std::nullopt;
    double left{},top{},right{},bottom{};bool first=true;
    for(const core::Point corner:{core::Point{r.x,r.y},core::Point{r.x+r.width,r.y},core::Point{r.x,r.y+r.height},core::Point{r.x+r.width,r.y+r.height}}) {
        const auto p=clientPoint(corner);if(!p||!std::isfinite(p->x)||!std::isfinite(p->y)) return std::nullopt;
        if(first) {left=right=p->x;top=bottom=p->y;first=false;} else {left=std::min(left,p->x);right=std::max(right,p->x);top=std::min(top,p->y);bottom=std::max(bottom,p->y);}
    }
    return core::Rect{left,top,right-left,bottom-top};
}
bool AccountSanityGaugePreview::perform(std::string_view id,double t) {
    auto& i=*impl_;i.advance(t);if(!i.overlayVisible||!i.scene.visible()) return false;
    const auto actions=i.model.accessibleActions(i.options.language);
    const auto hit=std::find_if(actions.begin(),actions.end(),[&](const auto& a){return a.id==id;});
    if(hit==actions.end()||!hit->enabled||!i.model.perform(id)) return false;
    i.scene.sync(i.model,i.options.language,i.time,i.options.reduceMotion);return true;
}
// SystemHUDView.layoutAccountGaugeControls hides the AX buttons while the HUD
// takes no module input.
std::vector<h::GaugeAction> AccountSanityGaugePreview::accessibleActions() const {
    if(!impl_->overlayVisible) return {};return impl_->model.accessibleActions(impl_->options.language);
}
std::optional<std::string> AccountSanityGaugePreview::accessibilityValue() const {return impl_->model.accessibilityValue(impl_->options.language);}
gpu::NativeSanityGaugeStats AccountSanityGaugePreview::sceneStats() const noexcept {return impl_->scene.stats();}
void AccountSanityGaugePreview::upload(gpu::Renderer& r) {if(impl_->posed) {impl_->renderer=&r;impl_->scene.upload(r);}}
bool AccountSanityGaugePreview::conceal() {return impl_->scene.park(impl_->renderer);}
std::span<const gpu::LayerCompositionEntry> AccountSanityGaugePreview::entries() {return impl_->posed?impl_->scene.entries():std::span<const gpu::LayerCompositionEntry>{};}
void AccountSanityGaugePreview::collected(gpu::Renderer& r) {impl_->scene.collected(r);}
void AccountSanityGaugePreview::release(gpu::Renderer& r) {need(impl_->scene.release(r),"Detach the gauge composition before releasing its resources");}
}
#endif
