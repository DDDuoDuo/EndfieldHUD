#include "tools/account_linking_owner.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "native/hypergryph_account_services.hpp"
#ifdef EHUD_HAS_WEBVIEW2
#include "native/hypergryph_account_webview.hpp"
#endif
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::tools {
namespace {
namespace h=modules::hypergryph;namespace gpu=native;
void need(bool v,const char* s) {if(!v) throw std::invalid_argument(s);}
void earliest(std::optional<double>& wake,std::optional<double> at) {if(at&&std::isfinite(*at)) wake=wake?std::min(*wake,*at):*at;}
}
struct AccountLinkingOwner::Impl {
    AccountLinkingOwnerOptions options;HWND window{};bool ready{},resized{},webView{};double time{};
#ifdef EHUD_HAS_WEBVIEW2
    std::unique_ptr<gpu::WebViewLoginPresenter> login;
#endif
    std::unique_ptr<gpu::HypergryphAccountServices> services;
    std::unique_ptr<AccountPreview> module;
    std::unique_ptr<AccountSanityGaugePreview> gauge;
    double now() {const double t=options.clock();if(std::isfinite(t)) time=std::max(time,t);return time;}
    void advance(double t) {need(std::isfinite(t),"Account owner needs a finite clock");time=std::max(time,t);}
    // Controller presentation/cache/sanity changed (owner thread only).
    void changed() {
        if(!ready) return;
        const double t=now();module->presentationChanged(t);gauge->contentChanged(t);
        if(options.invalidate) options.invalidate();
    }
};

AccountLinkingOwner::AccountLinkingOwner(app::UtilityExecutor& executor,gpu::LayerRasterizer& raster,AccountLinkingOwnerOptions options)
    :impl_(std::make_shared<Impl>()) {
    auto& i=*impl_;i.options=std::move(options);i.window=static_cast<HWND>(i.options.window);
    need(i.window&&IsWindow(i.window),"Account owner requires the borrowed HUD HWND");
    need(i.options.serviceMessage>=WM_APP&&i.options.serviceMessage<=0xBFFF,"Account wake must use an owned WM_APP message");
    need(bool(i.options.clock),"Account owner requires the shared host clock");
    need(i.options.dataRoot.is_absolute()&&!i.options.dataRoot.empty(),"Account owner requires the explicit absolute app data root");
    need(i.options.gaugeResources.is_absolute(),"Account owner requires the staged absolute account resources");
    const double start=i.now();
    std::weak_ptr<Impl> weak=impl_;
    const HWND window=i.window;const UINT wake=i.options.serviceMessage;
    gpu::HypergryphAccountServicesOptions s;
    s.accountDirectory=(i.options.dataRoot/L"Account").lexically_normal();
    // Thread-safe: WinHTTP completion threads only post one owner message.
    s.notify=[window,wake]{PostMessageW(window,wake,0,0);};
    s.hostClock=i.options.clock;
    if(i.options.wallClock) s.wallClock=i.options.wallClock;
    s.language=i.options.language;
    s.onEvent=[weak](std::string_view action){if(auto self=weak.lock();self&&self->options.recordEvent) self->options.recordEvent(action);};
    s.changed=[weak]{if(auto self=weak.lock()) self->changed();};
    s.profile=i.options.profile;s.initialAvatarFilename=i.options.initialAvatarFilename;
#ifdef EHUD_HAS_WEBVIEW2
    // Official login only with an installed Evergreen runtime; otherwise the
    // controller receives the explicit "unavailable" result (no browser).
    if(i.options.officialLogin&&gpu::webView2RuntimeVersion()) {
        gpu::WebViewLoginOptions l;l.owner=window;l.language=i.options.language;l.now=i.options.clock;
        l.scheduleChanged=[weak]{if(auto self=weak.lock();self&&self->options.invalidate) self->options.invalidate();};
        i.login=std::make_unique<gpu::WebViewLoginPresenter>(std::move(l));s.login=i.login.get();i.webView=true;
    }
#endif
    i.services=std::make_unique<gpu::HypergryphAccountServices>(executor,std::move(s));
    AccountPreviewOptions m;m.raster=i.options.raster;m.style=i.options.style;m.language=i.options.language;m.reduceMotion=i.options.reduceMotion;
    i.module=std::make_unique<AccountPreview>(i.services->controller(),raster,m);
    AccountSanityGaugeOptions g;g.raster=i.options.raster;g.language=i.options.language;g.reduceMotion=i.options.reduceMotion;
    g.workMode=i.options.workMode;g.workModeRunning=i.options.workModeRunning;
    i.gauge=std::make_unique<AccountSanityGaugePreview>(i.services->controller(),raster,i.options.gaugeResources,g);
    i.ready=true;i.gauge->contentChanged(start);
}
AccountLinkingOwner::~AccountLinkingOwner() {
    auto& i=*impl_;i.ready=false;
    // Owners borrow the controller; the services (and any login) go last.
    i.gauge.reset();i.module.reset();i.services.reset();
#ifdef EHUD_HAS_WEBVIEW2
    i.login.reset();
#endif
}
void AccountLinkingOwner::resize(const app::ClientMetrics& m) {
    auto& i=*impl_;if(!m.pixelWidth||!m.pixelHeight||!(m.scale>0)) return;
    i.module->resize(m);i.gauge->resize(m);i.resized=true;
}
void AccountLinkingOwner::setAppearance(core::Language language,const h::CanvasStyle& style,bool reduceMotion,double t) {
    auto& i=*impl_;i.advance(t);i.options.language=language;i.options.reduceMotion=reduceMotion;
    i.module->setReduceMotion(reduceMotion);i.gauge->setReduceMotion(reduceMotion);
    i.module->setStyle(style,i.time);i.module->setLanguage(language,i.time);i.gauge->setLanguage(language,i.time);
#ifdef EHUD_HAS_WEBVIEW2
    if(i.login) i.login->setLanguage(language);
#endif
}
void AccountLinkingOwner::setOverlayVisible(bool visible,double t) {
    auto& i=*impl_;i.advance(t);i.module->setOverlayVisible(visible,i.time);i.gauge->setOverlayVisible(visible,i.time);
}
void AccountLinkingOwner::overlayClosing(double t) {cancelInteraction(t);setOverlayVisible(false,t);}
void AccountLinkingOwner::update(const core::Matrix4& center,const core::source::DesktopChromeSettings& chrome,
        const core::ModulePresentationSample& sample,float opacity,float headerOpacity,double t) {
    auto& i=*impl_;i.advance(t);if(!i.resized) return;
    i.module->update(center,chrome,sample,opacity,i.time);i.gauge->update(center,headerOpacity,i.time);
}
bool AccountLinkingOwner::requiresFrames(double t) const {const auto& i=*impl_;return i.module->requiresFrames(t)||i.gauge->requiresFrames(t);}
std::optional<double> AccountLinkingOwner::nextWakeTime(double now) const {
    const auto& i=*impl_;std::optional<double> wake;
    earliest(wake,i.module->nextWakeTime(now));earliest(wake,i.gauge->nextWakeTime(now));
    earliest(wake,i.services->nextDeadline());
#ifdef EHUD_HAS_WEBVIEW2
    if(i.login) earliest(wake,i.login->nextDeadline());
#endif
    return wake;
}
bool AccountLinkingOwner::deadline(double t) {
    auto& i=*impl_;i.advance(t);bool changed=i.services->deadline(i.time);
#ifdef EHUD_HAS_WEBVIEW2
    if(i.login) i.login->deadline(i.time);
#endif
    changed|=i.module->deadline(i.time);changed|=i.gauge->deadline(i.time);
    return changed;
}
void AccountLinkingOwner::upload(gpu::Renderer& r) {auto& i=*impl_;i.module->upload(r);i.gauge->upload(r);}
std::span<const gpu::LayerCompositionEntry> AccountLinkingOwner::moduleEntries() {return impl_->module->entries();}
std::span<const gpu::LayerCompositionEntry> AccountLinkingOwner::headerEntries() {return impl_->gauge->entries();}
void AccountLinkingOwner::collected(gpu::Renderer& r) {auto& i=*impl_;i.module->collected(r);i.gauge->collected(r);}
void AccountLinkingOwner::release(gpu::Renderer& r) {auto& i=*impl_;i.module->release(r);i.gauge->release(r);}
bool AccountLinkingOwner::covers(core::Point p) const {const auto& i=*impl_;return i.gauge->covers(p)||i.module->covers(p);}
bool AccountLinkingOwner::capturesPointer() const {const auto& i=*impl_;return i.gauge->capturesPointer()||i.module->capturesPointer();}
bool AccountLinkingOwner::capturesKeys() const {return capturesPointer();}
bool AccountLinkingOwner::pointer(const app::PointerEvent& e,double t) {
    auto& i=*impl_;i.advance(t);
    // Hover/leave reach both surfaces (each keeps its own feedback).
    if(e.kind==app::PointerKind::move||e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost) {
        const bool gauge=i.gauge->pointer(e,i.time);const bool module=i.module->pointer(e,i.time);return gauge||module;
    }
    // The open recovery popover owns every click (an outside click dismisses
    // it and is consumed, as SystemHUDView does); then an open account menu.
    if(i.gauge->capturesPointer()) return i.gauge->pointer(e,i.time);
    if(i.module->capturesPointer()) return i.module->pointer(e,i.time);
    if(e.kind==app::PointerKind::up) {const bool gauge=i.gauge->pointer(e,i.time);return i.module->pointer(e,i.time)||gauge;}
    return i.gauge->pointer(e,i.time)||i.module->pointer(e,i.time);
}
bool AccountLinkingOwner::wheel(const app::WheelEvent& e,double t) {auto& i=*impl_;i.advance(t);return i.module->wheel(e,i.time);}
bool AccountLinkingOwner::key(const app::KeyEvent& e,bool modified,double t) {
    auto& i=*impl_;i.advance(t);
    if(i.gauge->capturesPointer()) return i.gauge->key(e,modified,i.time);
    return i.module->key(e,modified,i.time);
}
bool AccountLinkingOwner::message(const app::NativeMessage& m,double t) {
    auto& i=*impl_;
    if(m.message!=i.options.serviceMessage||(m.window&&m.window!=static_cast<void*>(i.window))) return false;
    i.advance(t);i.services->drain();return true;
}
void AccountLinkingOwner::cancelInteraction(double t) {auto& i=*impl_;i.advance(t);i.module->cancelInteraction(i.time);i.gauge->dismiss(i.time);}
bool AccountLinkingOwner::flush(double t) {impl_->advance(t);return impl_->services->flush();}
void AccountLinkingOwner::profileAvatarChanged(const std::optional<std::string>& filename) {impl_->services->controller().profileAvatarChanged(filename);}
void AccountLinkingOwner::workModeChanged(double t) {auto& i=*impl_;i.advance(t);i.gauge->contentChanged(i.time);}
h::HypergryphAccountController& AccountLinkingOwner::controller() noexcept {return impl_->services->controller();}
const AccountPreview& AccountLinkingOwner::module() const noexcept {return *impl_->module;}
const AccountSanityGaugePreview& AccountLinkingOwner::gauge() const noexcept {return *impl_->gauge;}
std::size_t AccountLinkingOwner::activeRequests() const {return impl_->services->transport().activeRequests();}
bool AccountLinkingOwner::officialLoginAvailable() const noexcept {return impl_->webView;}
}
#endif
