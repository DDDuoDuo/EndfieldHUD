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
#include <array>
#include <cmath>
#include <stdexcept>

namespace endfield::tools {
namespace {
namespace h=modules::hypergryph;namespace gpu=native;namespace d=modules::design;
void need(bool v,const char* s) {if(!v) throw std::invalid_argument(s);}
void earliest(std::optional<double>& wake,std::optional<double> at) {if(at&&std::isfinite(*at)) wake=wake?std::min(*wake,*at):*at;}
}
struct AccountLinkingOwner::Impl {
    AccountLinkingOwnerOptions options;HWND window{};bool ready{},resized{},webView{},overlay{},selected{};double time{};
#ifdef EHUD_HAS_WEBVIEW2
    std::unique_ptr<gpu::WebViewLoginPresenter> login;
#endif
    std::unique_ptr<gpu::HypergryphAccountServices> services;
    std::unique_ptr<DesignOwnerCore> page;
    modules::AccountCanvas* canvas{};               // owned by `page`
    std::unique_ptr<AccountSanityGaugePreview> gauge;
    double now() {const double t=options.clock();if(std::isfinite(t)) time=std::max(time,t);return time;}
    void advance(double t) {need(std::isfinite(t),"Account owner needs a finite clock");time=std::max(time,t);}
    // The controller's presentation reaches the page (HUDAccountCanvas.update:
    // in place, a rebuild or a slide; the canvas dedupes an equal one).
    void syncPage(double t) {
        if(!canvas) return;
        auto& env=options.design->env();env.now=std::max(env.now,t);
        canvas->update(services->controller().presentation());
    }
    // HypergryphAccountController.setVisible(window != nil && interaction, module == .account).
    void visibility() {services->controller().setVisible(overlay,selected);}
    // Controller presentation/cache/sanity changed (owner thread only).
    void changed() {
        if(!ready) return;
        const double t=now();syncPage(t);gauge->contentChanged(t);
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
    need(i.options.design!=nullptr,"Account owner requires the HUD's design context");
    const double start=i.now();
    std::weak_ptr<Impl> weak=impl_;
    const HWND window=i.window;const UINT wake=i.options.serviceMessage;
    gpu::HypergryphAccountServicesOptions s;
    s.accountDirectory=(i.options.dataRoot/L"Account").lexically_normal();
    // Thread-safe: WinHTTP completion threads only post one owner message.
    if(i.options.wake) s.notify=i.options.wake;
    else s.notify=[window,wake]{PostMessageW(window,wake,0,0);};
    s.hostClock=i.options.clock;
    if(i.options.wallClock) s.wallClock=i.options.wallClock;
    if(i.options.localDate) s.localDate=i.options.localDate;
    s.synthetic=i.options.synthetic;
    s.language=i.options.language;
    s.onEvent=[weak](std::string_view action){if(auto self=weak.lock();self&&self->options.recordEvent) self->options.recordEvent(action);};
    s.changed=[weak]{if(auto self=weak.lock()) self->changed();};
    s.profile=i.options.profile;s.initialAvatarFilename=i.options.initialAvatarFilename;
    if(i.options.login) s.login=i.options.login;
#ifdef EHUD_HAS_WEBVIEW2
    // Official login through WebView2. Each Connect checks the installed
    // runtime; without one the controller receives the explicit
    // "unavailable" result and no window or browser is created.
    else if(i.options.officialLogin) {
        gpu::WebViewLoginOptions l;l.owner=window;l.language=i.options.language;l.now=i.options.clock;l.accent=i.options.accent;
        l.scheduleChanged=[weak]{if(auto self=weak.lock();self&&self->options.invalidate) self->options.invalidate();};
        l.openLink=i.options.openLink;
        i.login=std::make_unique<gpu::WebViewLoginPresenter>(std::move(l));s.login=i.login.get();i.webView=true;
    }
#endif
    i.services=std::make_unique<gpu::HypergryphAccountServices>(executor,std::move(s));
    auto& controller=i.services->controller();
    controller.setLanguage(i.options.language);
    // The page: the canvas's actions go to the controller (login, vault,
    // refresh), its sanity is projected on the controller's wall clock.
    auto canvas=std::make_unique<modules::AccountCanvas>(*i.options.design);
    i.canvas=canvas.get();
    canvas->wallClock=[weak]{auto self=weak.lock();return self?self->services->controller().now():0.;};
    canvas->onAction=[weak](const h::AccountAction& a){
        auto self=weak.lock();if(!self) return;
        self->services->controller().perform(a);self->syncPage(self->time);
    };
    canvas->update(controller.presentation());
    auto pageOptions=i.options.page;
    if(pageOptions.name.empty()) pageOptions.name="account";
    i.page=std::make_unique<DesignOwnerCore>(raster,*i.options.design,std::move(pageOptions));
    i.page->add(std::move(canvas));
    AccountSanityGaugeOptions g;g.raster=i.options.raster;g.language=i.options.language;g.reduceMotion=i.options.reduceMotion;
    g.workMode=i.options.workMode;g.workModeRunning=i.options.workModeRunning;g.openWorkMode=i.options.openWorkMode;
    i.gauge=std::make_unique<AccountSanityGaugePreview>(controller,raster,i.options.gaugeResources,g);
    i.ready=true;i.gauge->contentChanged(start);
}
AccountLinkingOwner::~AccountLinkingOwner() {
    auto& i=*impl_;i.ready=false;
    // Owners borrow the controller; the services (and any login) go last.
    i.gauge.reset();
    if(i.canvas) i.canvas->onAction={};
    i.page.reset();i.canvas=nullptr;i.services.reset();
#ifdef EHUD_HAS_WEBVIEW2
    i.login.reset();
#endif
}
void AccountLinkingOwner::resize(const app::ClientMetrics& m) {
    auto& i=*impl_;if(!m.pixelWidth||!m.pixelHeight||!(m.scale>0)) return;
    i.page->resize(m);i.gauge->resize(m);i.resized=true;
}
void AccountLinkingOwner::setAppearance(const app::ModuleAppearance& a,double t) {
    auto& i=*impl_;i.advance(t);i.options.language=a.language;i.options.reduceMotion=a.reduceMotion;i.options.accent=a.accentSRGB;
    i.services->controller().setLanguage(a.language);
    i.gauge->setReduceMotion(a.reduceMotion);i.gauge->setLanguage(a.language,i.time);
    i.page->setAppearance(a,i.time);
#ifdef EHUD_HAS_WEBVIEW2
    if(i.login) {i.login->setLanguage(a.language);i.login->setAccent(a.accentSRGB);}
#endif
    i.syncPage(i.time);
}
void AccountLinkingOwner::setOverlayVisible(bool visible,double t) {
    auto& i=*impl_;i.advance(t);i.page->setOverlayVisible(visible,i.time);i.gauge->setOverlayVisible(visible,i.time);
    if(i.overlay!=visible) {i.overlay=visible;i.visibility();i.syncPage(i.time);}
    // Hidden: the controller has cancelled its requests; the WinHTTP session
    // (if any) closes now, or with the last request still finishing.
    i.services->setNetworkRetained(visible);
}
void AccountLinkingOwner::overlayClosing(double t) {cancelInteraction(t);impl_->page->overlayClosing(t);setOverlayVisible(false,t);}
void AccountLinkingOwner::update(const app::ModuleFrame& frame,float headerOpacity) {
    auto& i=*impl_;i.advance(frame.time);if(!i.resized) return;
    const bool selected=frame.presentation.requested==core::Module::account;
    if(selected!=i.selected) {i.selected=selected;i.visibility();i.syncPage(i.time);}
    i.page->update(frame);i.gauge->update(frame.center,headerOpacity,i.time);
}
void AccountLinkingOwner::prewarm(const app::ModuleFrame& frame) {
    auto& i=*impl_;i.advance(frame.time);if(!i.resized) return;
    // The wallet is on every opening frame (unless the header mode hides it);
    // the hidden scene composes nothing, so posing it only enables upload().
    i.gauge->update(frame.center,0,i.time);
    i.page->prewarm(frame);
}
void AccountLinkingOwner::concealed(double t) {
    auto& i=*impl_;i.advance(t);
    (void)i.page->hostEvent({app::HostEvent::Kind::overlayConcealed,0},i.time);(void)i.gauge->conceal();
}
bool AccountLinkingOwner::requiresFrames(double t) const {const auto& i=*impl_;return i.page->requiresFrames(t)||i.gauge->requiresFrames(t);}
std::optional<double> AccountLinkingOwner::nextWakeTime(double now) const {
    const auto& i=*impl_;std::optional<double> wake;
    earliest(wake,i.page->nextWakeTime(now));earliest(wake,i.gauge->nextWakeTime(now));
    earliest(wake,i.services->nextDeadline());
    // The controller's refresh / recovery schedule (its wall clock) on the host clock.
    if(const auto c=i.services->controller().nextWakeTime()) earliest(wake,now+std::max(0.0,*c-i.services->controller().now()));
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
    const auto before=i.canvas->contentRevision();
    i.services->controller().tick();i.syncPage(i.time);
    changed|=i.canvas->contentRevision()!=before;
    changed|=i.page->deadline(i.time);changed|=i.gauge->deadline(i.time);
    return changed;
}
void AccountLinkingOwner::upload(gpu::Renderer& r) {auto& i=*impl_;i.page->upload(r);i.gauge->upload(r);}
std::span<const gpu::LayerCompositionEntry> AccountLinkingOwner::moduleEntries() {return impl_->page->entries(core::Module::account);}
std::span<const gpu::LayerCompositionEntry> AccountLinkingOwner::headerEntries() {return impl_->gauge->entries();}
void AccountLinkingOwner::collected(gpu::Renderer& r) {auto& i=*impl_;i.page->collected(r);i.gauge->collected(r);}
void AccountLinkingOwner::release(gpu::Renderer& r) {auto& i=*impl_;i.page->release(r);i.gauge->release(r);}
bool AccountLinkingOwner::covers(core::Point p) const {const auto& i=*impl_;return i.gauge->covers(p)||i.page->covers(p);}
bool AccountLinkingOwner::pointerLocked() const {return impl_->page->pointerLocked();}
bool AccountLinkingOwner::capturesPointer() const {const auto& i=*impl_;return i.gauge->capturesPointer()||i.page->capturesPointer();}
bool AccountLinkingOwner::capturesWheel() const {const auto& i=*impl_;return i.gauge->capturesPointer()||i.page->capturesWheel();}
bool AccountLinkingOwner::capturesKeys() const {const auto& i=*impl_;return i.gauge->capturesPointer()||i.page->capturesKeys();}
bool AccountLinkingOwner::pointer(const app::PointerEvent& e,double t) {
    auto& i=*impl_;i.advance(t);
    // Hover/leave reach both surfaces (each keeps its own feedback).
    if(e.kind==app::PointerKind::move||e.kind==app::PointerKind::leave||e.kind==app::PointerKind::captureLost) {
        const bool gauge=i.gauge->pointer(e,i.time);const bool page=i.page->pointer(e,i.time);return gauge||page;
    }
    // The open recovery popover owns every click (an outside click dismisses
    // it and is consumed, as SystemHUDView does); then an open page menu.
    if(i.gauge->capturesPointer()) return i.gauge->pointer(e,i.time);
    if(i.page->capturesPointer()) return i.page->pointer(e,i.time);
    if(e.kind==app::PointerKind::up) {const bool gauge=i.gauge->pointer(e,i.time);return i.page->pointer(e,i.time)||gauge;}
    return i.gauge->pointer(e,i.time)||i.page->pointer(e,i.time);
}
bool AccountLinkingOwner::wheel(const app::WheelEvent& e,double t) {auto& i=*impl_;i.advance(t);return i.page->wheel(e,i.time);}
bool AccountLinkingOwner::key(const app::KeyEvent& e,std::uint32_t modifiers,double t) {
    auto& i=*impl_;i.advance(t);
    if(i.gauge->capturesPointer()) return i.gauge->key(e,modifiers!=0,i.time);
    return i.page->key(e,modifiers,i.time);
}
bool AccountLinkingOwner::message(const app::NativeMessage& m,double t) {
    auto& i=*impl_;
    if(m.message!=i.options.serviceMessage||(m.window&&m.window!=static_cast<void*>(i.window))) return i.page->message(m,t);
    i.advance(t);i.services->drain();return true;
}
std::optional<app::ModuleCursor> AccountLinkingOwner::cursorAt(core::Point p) const {return impl_->page->cursorAt(p);}
bool AccountLinkingOwner::dismissWalletPopover(double t) {auto& i=*impl_;i.advance(t);if(!i.gauge||!i.gauge->model().popoverOpen())return false;(void)i.gauge->dismiss(i.time);return true;}
void AccountLinkingOwner::cancelInteraction(double t) {auto& i=*impl_;i.advance(t);i.page->cancelInteraction(i.time);i.gauge->dismiss(i.time);}
bool AccountLinkingOwner::flush(double t) {impl_->advance(t);return impl_->services->flush();}
void AccountLinkingOwner::profileAvatarChanged(const std::optional<std::string>& filename) {impl_->services->controller().profileAvatarChanged(filename);}
void AccountLinkingOwner::profileAvatarBaseline(const std::optional<std::string>& filename) {impl_->services->controller().profileAvatarBaseline(filename);}
void AccountLinkingOwner::workModeChanged(double t) {auto& i=*impl_;i.advance(t);i.gauge->contentChanged(i.time);}
void AccountLinkingOwner::localTimeChanged(double t) {auto& i=*impl_;i.advance(t);i.syncPage(i.time);i.gauge->contentChanged(i.time);}
std::vector<gpu::AutomationNode> AccountLinkingOwner::accessibleNodes() const {
    const auto& i=*impl_;std::vector<gpu::AutomationNode> out;
    if(i.page->inputModule()==core::Module::account) {
        std::vector<d::AccessibilityItem> items;i.canvas->accessibilityItems(items);
        out=gpu::automationNodes(items);
        for(auto& n:out) n.space=accountPageSpace;
    }
    for(const auto& a:i.gauge->accessibleActions()) {
        if(!i.gauge->clientRect(a.rect)) continue;
        gpu::AutomationNode n;n.id="wallet:"+a.id;n.name=a.label;n.role=gpu::AutomationNode::Role::button;n.space=accountWalletSpace;
        n.rect=*i.gauge->clientRect(a.rect);n.enabled=a.enabled;
        // SystemHUDView sets the open popover's countdowns as the toggle's value.
        if(a.id=="toggle") if(const auto value=i.gauge->accessibilityValue()) {n.value=*value;n.valued=true;}
        out.push_back(std::move(n));
    }
    return out;
}
std::optional<core::Rect> AccountLinkingOwner::accessibleClientRect(core::Rect r,std::uint8_t space) const {
    if(space==accountWalletSpace) return r;
    return impl_->page->clientRect(r);
}
bool AccountLinkingOwner::performAccessible(std::string_view id,double t) {
    auto& i=*impl_;i.advance(t);
    if(id.rfind("wallet:",0)==0) return i.gauge->perform(id.substr(7),i.time);
    if(i.page->inputModule()!=core::Module::account) return false;
    return i.canvas->performAccessibilityAction(id,i.time);
}
bool AccountLinkingOwner::setAccessibleValue(std::string_view id,double value,double t) {
    auto& i=*impl_;i.advance(t);
    return i.page->inputModule()==core::Module::account&&i.canvas->adjustAccessibilitySlider(id,value,i.time);
}
bool AccountLinkingOwner::expandAccessible(std::string_view id,bool expand,double t) {
    auto& i=*impl_;i.advance(t);
    if(i.page->inputModule()!=core::Module::account) return false;
    std::vector<d::AccessibilityItem> items;i.canvas->accessibilityItems(items);
    const auto it=std::find_if(items.begin(),items.end(),[&](const auto& item){return item.id==id;});
    if(it==items.end()) return false;
    if(it->on==expand) return true;
    return i.canvas->performAccessibilityAction(id,i.time);
}
h::HypergryphAccountController& AccountLinkingOwner::controller() noexcept {return impl_->services->controller();}
modules::AccountCanvas& AccountLinkingOwner::canvas() noexcept {return *impl_->canvas;}
DesignOwnerCore& AccountLinkingOwner::page() noexcept {return *impl_->page;}
const AccountSanityGaugePreview& AccountLinkingOwner::gauge() const noexcept {return *impl_->gauge;}
std::size_t AccountLinkingOwner::activeRequests() const {return impl_->services->transport().activeRequests();}
std::optional<gpu::WinHttpAccountTransport::SessionStats> AccountLinkingOwner::networkSession() const {return impl_->services->networkSession();}
bool AccountLinkingOwner::officialLoginAvailable() const noexcept {return impl_->webView;}
}
#endif
