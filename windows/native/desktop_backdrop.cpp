#include "native/desktop_backdrop.hpp"
#include "native/renderer.hpp"
#include "core/source_animation.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <string>
#endif

namespace endfield::native {
namespace {
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
void unit(double value,const char*message){need(std::isfinite(value)&&value>=0&&value<=1,message);}
DesktopBackdropTrack track(const ehud::data::Json& value){
    const auto&curve=value["curve"];
    need(curve["group"].string()=="m_FloatCurves"&&curve["path"].string()=="BlurBG"&&curve["attribute"].string()=="m_Alpha","Unsupported original WatchBlur alpha binding");
    const auto parsed=core::source::ScalarCurve::fromJson(curve["raw"]["curve"]);
    const auto keys=parsed.keys();
    need(keys.size()==2&&keys[0].time==0&&keys[1].time>0&&keys[0].weightedMode==0&&keys[1].weightedMode==0&&keys[0].value!=keys[1].value&&std::isfinite(keys[0].outSlope)&&std::isfinite(keys[1].inSlope),"Unsupported original WatchBlur alpha track");
    unit(keys[0].value,"WatchBlur alpha outside unit range");unit(keys[1].value,"WatchBlur alpha outside unit range");
    const auto length=keys[1].time,delta=keys[1].value-keys[0].value;
    DesktopBackdropTrack result{length,keys[0].value,keys[1].value,
        {1.f/3.f,static_cast<float>(keys[0].outSlope*length/(3*delta)),2.f/3.f,static_cast<float>(1-keys[1].inSlope*length/(3*delta))}};
    for(const auto point:result.controlPoints)need(std::isfinite(point),"WatchBlur timing cannot fit source Float controls");
    return result;
}
[[maybe_unused]] void validate(const DesktopBackdropState&s){
    need(s.pixelWidth>0&&s.pixelHeight>0&&s.pixelWidth<=16384&&s.pixelHeight<=16384,"Backdrop dimensions must be in1..16384");
    unit(s.blurAmount,"Backdrop blur opacity outside unit range");unit(s.backgroundDarkness,"Backdrop tint opacity outside unit range");unit(s.sourceOpacity,"Backdrop source opacity outside unit range");
}
}
DesktopBackdropStyle desktopBackdropStyle(bool dark,bool projectionPlane) noexcept{
    if(projectionPlane)return DesktopBackdropStyle{};
    DesktopBackdropStyle result;result.tintWhite=dark?.015:.90;result.vignetteAlpha=dark?std::array<double,3>{0,.06,.38}:std::array<double,3>{0,.02,.14};return result;
}
double DesktopBackdropTrack::alpha(double elapsed)const{
    need(std::isfinite(elapsed)&&std::isfinite(duration)&&duration>0,"Invalid finite WatchBlur time");
    if(elapsed<=0)return startAlpha;if(elapsed>=duration)return endAlpha;
    const core::CubicTiming easing{controlPoints[0],controlPoints[1],controlPoints[2],controlPoints[3]};
    return startAlpha+(endAlpha-startAlpha)*easing.value(elapsed/duration);
}
DesktopBackdropAnimation DesktopBackdropAnimation::fromSource(const ehud::data::Json& source){
    need(source["wrapper"]["_options"]["animEase"].number()==1&&source["default_speed"].number()==1,"Unsupported WatchBlur wrapper clock");
    return {track(source["entrance"]),track(source["exit"])};
}

#ifdef _WIN32
namespace {
namespace wc=winrt::Windows::UI::Composition;
namespace wd=winrt::Windows::UI::Composition::Desktop;
namespace ws=winrt::Windows::System;
winrt::Windows::UI::Color gray(double white,double alpha){
    const auto byte=[](double x){return static_cast<std::uint8_t>(std::lround(std::clamp(x,0.,1.)*255));};
    const auto w=byte(white);return {byte(alpha),w,w,w};
}
template<class T>void close(T&value)noexcept{if(value){try{value.Close();}catch(...){}value=nullptr;}}
void checkResult(HRESULT result,const wchar_t*stage){if(FAILED(result))throw winrt::hresult_error(result,stage);}
}
struct DesktopBackdrop::Impl {
    DWORD thread{GetCurrentThreadId()};HWND window{};BOOL previousHost{};bool changedAttribute{};
    wc::Compositor compositor{nullptr};wd::DesktopWindowTarget target{nullptr};
    wc::ContainerVisual root{nullptr},tone{nullptr};wc::SpriteVisual blur{nullptr},tint{nullptr},vignette{nullptr};
    wc::CompositionBackdropBrush hostBrush{nullptr};wc::CompositionColorBrush tintBrush{nullptr};
    wc::CompositionRadialGradientBrush vignetteBrush{nullptr};
    wc::LayerVisual panel{nullptr};wc::SpriteVisual foreground{nullptr};
    wc::CompositionSurfaceBrush foregroundBrush{nullptr};wc::ICompositionSurface foregroundSurface{nullptr};
    std::shared_ptr<RendererCompositionSurface>rendererSurface;Renderer*renderer{};
    std::array<wc::CompositionColorGradientStop,3>stops{nullptr,nullptr,nullptr};
    DesktopBackdropState state;DesktopBackdropStats counts;
    void onThread()const{if(GetCurrentThreadId()!=thread)throw std::logic_error("Desktop backdrop used outside its owner UI thread");}
    bool detachBridge()noexcept{
        if(!rendererSurface)return true;bool restored=true;
        rendererSurface->unbindResetObserver(this);
        try{if(target)target.Root(nullptr);if(panel)panel.Children().RemoveAll();if(target&&root)target.Root(root);}catch(...){restored=false;}
        try{if(foreground)foreground.Brush(nullptr);if(foregroundBrush)foregroundBrush.Surface(nullptr);}catch(...){restored=false;}
        close(foreground);close(foregroundBrush);foregroundSurface=nullptr;close(panel);
        if(rendererSurface->valid()&&renderer){counts.foregroundRestorationAttempted=true;
            try{renderer->restoreComposition(*rendererSurface);counts.foregroundRestored=!renderer->stats().compositionSuspended;restored&=counts.foregroundRestored;}catch(...){counts.foregroundRestored=false;restored=false;}}
        rendererSurface.reset();renderer=nullptr;counts.foregroundAttached=false;counts.panelOpacity=1;++counts.foregroundDetachments;return restored;
    }
    void clear()noexcept{
        // Caller must destroy on owner thread, before HWND / DispatcherQueue.
        (void)detachBridge();
        if(target)try{target.Root(nullptr);}catch(...){}
        close(target);close(blur);close(tint);close(vignette);close(tone);close(root);
        close(hostBrush);close(tintBrush);close(vignetteBrush);for(auto&stop:stops)close(stop);close(compositor);
        if(changedAttribute){
            counts.hostAttributeRestorationAttempted=true;
            const auto result=window&&IsWindow(window)
                ?DwmSetWindowAttribute(window,DWMWA_USE_HOSTBACKDROPBRUSH,&previousHost,sizeof(previousHost))
                :HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
            counts.hostAttributeRestoreResult=static_cast<std::int32_t>(result);
            counts.hostAttributeRestored=SUCCEEDED(result);
        }
        window=nullptr;changedAttribute=false;counts.initialized=counts.hostBackdropEnabled=counts.lowerCompositionTarget=false;
    }
    ~Impl(){clear();}
    void apply(const DesktopBackdropState&next,bool initial){
        if(initial||next.pixelWidth!=state.pixelWidth||next.pixelHeight!=state.pixelHeight){const winrt::Windows::Foundation::Numerics::float2 size{static_cast<float>(next.pixelWidth),static_cast<float>(next.pixelHeight)};root.Size(size);++counts.propertyWrites;
            if(panel){panel.Size(size);foreground.Size(size);counts.propertyWrites+=2;}}
        if(initial||next.dark!=state.dark||next.projectionPlane!=state.projectionPlane){
            const auto style=desktopBackdropStyle(next.dark,next.projectionPlane);tintBrush.Color(gray(style.tintWhite,1));++counts.propertyWrites;
            for(std::size_t i=0;i<stops.size();++i){stops[i].Color(gray(0,style.vignetteAlpha[i]));++counts.propertyWrites;}
        }
        if(initial||next.sourceOpacity!=state.sourceOpacity){tone.Opacity(static_cast<float>(next.sourceOpacity));++counts.propertyWrites;}
        if(initial||next.backgroundDarkness!=state.backgroundDarkness){tint.Opacity(static_cast<float>(next.backgroundDarkness));++counts.propertyWrites;}
        if(initial||next.sourceOpacity!=state.sourceOpacity||next.blurAmount!=state.blurAmount||next.lowPower!=state.lowPower){
            const auto opacity=next.lowPower?0:next.blurAmount*next.sourceOpacity;blur.Opacity(static_cast<float>(opacity));blur.IsVisible(opacity>0);counts.propertyWrites+=2;
        }
        if(initial||(next.sourceOpacity>0)!=(state.sourceOpacity>0)){tone.IsVisible(next.sourceOpacity>0);++counts.propertyWrites;}
        state=next;++counts.updates;
    }
};
DesktopBackdrop::DesktopBackdrop():impl_(std::make_unique<Impl>()){}
DesktopBackdrop::~DesktopBackdrop()=default;
void DesktopBackdrop::initialize(void*handle,const DesktopBackdropState&state,const DesktopBackdropInitialization&options){
    impl_->onThread();validate(state);need(!impl_->counts.initialized,"Reset desktop backdrop before another initialization");
    const auto window=static_cast<HWND>(handle);DWORD process{};
    need(window&&IsWindow(window)&&GetWindowThreadProcessId(window,&process)==GetCurrentThreadId()&&process==GetCurrentProcessId(),"Backdrop must own a current-thread process HWND");
    need((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_NOREDIRECTIONBITMAP)!=0,"Backdrop HWND requires WS_EX_NOREDIRECTIONBITMAP");
    APTTYPE apartment{};APTTYPEQUALIFIER qualifier{};checkResult(CoGetApartmentType(&apartment,&qualifier),L"Backdrop: inspect caller COM apartment");
    need(apartment==APTTYPE_STA||apartment==APTTYPE_MAINSTA,"Backdrop requires caller-owned STA or ASTA");
    need(bool(ws::DispatcherQueue::GetForCurrentThread()),"Backdrop requires a caller-owned current-thread DispatcherQueue");
    auto candidate=std::make_unique<Impl>();candidate->window=window;
    // The explicit option asserts a state successfully set by this HWND owner.
    // A getter is not documented for this attribute; absent that assertion, a
    // failed getter still leaves the existing HWND completely unmodified.
    if(options.callerEstablishedHostBackdrop.has_value())candidate->previousHost=*options.callerEstablishedHostBackdrop?TRUE:FALSE;
    else checkResult(DwmGetWindowAttribute(window,DWMWA_USE_HOSTBACKDROPBRUSH,&candidate->previousHost,sizeof(BOOL)),L"Backdrop: original host flag unavailable; caller-established state required");
    candidate->counts.originalHostBackdropEnabled=candidate->previousHost!=FALSE;
    const BOOL enabled=TRUE;checkResult(DwmSetWindowAttribute(window,DWMWA_USE_HOSTBACKDROPBRUSH,&enabled,sizeof(enabled)),L"Backdrop: enable host backdrop attribute");candidate->changedAttribute=true;
    const wchar_t*stage=L"Backdrop: create caller-thread compositor";
    try{
    candidate->compositor=wc::Compositor();
    stage=L"Backdrop: create lower desktop composition target";
    const auto interop=candidate->compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
    checkResult(interop->CreateDesktopWindowTarget(window,FALSE,reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(winrt::put_abi(candidate->target))),stage);
    stage=L"Backdrop: create retained composition visuals";
    candidate->root=candidate->compositor.CreateContainerVisual();candidate->tone=candidate->compositor.CreateContainerVisual();
    candidate->blur=candidate->compositor.CreateSpriteVisual();candidate->tint=candidate->compositor.CreateSpriteVisual();candidate->vignette=candidate->compositor.CreateSpriteVisual();
    candidate->counts.visualAllocations=5;
    candidate->tone.RelativeSizeAdjustment({1,1});candidate->blur.RelativeSizeAdjustment({1,1});candidate->tint.RelativeSizeAdjustment({1,1});candidate->vignette.RelativeSizeAdjustment({1,1});
    stage=L"Backdrop: create host backdrop brush";
    candidate->hostBrush=candidate->compositor.CreateHostBackdropBrush();candidate->blur.Brush(candidate->hostBrush);
    stage=L"Backdrop: create tint and radial gradient brushes";
    candidate->tintBrush=candidate->compositor.CreateColorBrush();candidate->tint.Brush(candidate->tintBrush);
    candidate->vignetteBrush=candidate->compositor.CreateRadialGradientBrush();candidate->vignette.Brush(candidate->vignetteBrush);candidate->counts.brushAllocations=3;
    const auto style=desktopBackdropStyle(state.dark,state.projectionPlane);candidate->vignetteBrush.MappingMode(wc::CompositionMappingMode::Relative);
    candidate->vignetteBrush.EllipseCenter({static_cast<float>(style.vignetteStart[0]),static_cast<float>(style.vignetteStart[1])});
    // Feasibility mapping only: source CAGradientLayer.start/end are preserved
    // above, but radial falloff/color/group opacity need an actual pixel oracle.
    candidate->vignetteBrush.EllipseRadius({static_cast<float>(style.vignetteEnd[0]-style.vignetteStart[0]),static_cast<float>(style.vignetteEnd[1]-style.vignetteStart[1])});
    candidate->vignetteBrush.GradientOriginOffset({0,0});candidate->vignetteBrush.InterpolationSpace(wc::CompositionColorSpace::Rgb);
    for(std::size_t i=0;i<candidate->stops.size();++i){candidate->stops[i]=candidate->compositor.CreateColorGradientStop(static_cast<float>(style.vignetteLocations[i]),gray(0,style.vignetteAlpha[i]));candidate->vignetteBrush.ColorStops().Append(candidate->stops[i]);}
    candidate->tone.Children().InsertAtBottom(candidate->tint);candidate->tone.Children().InsertAtTop(candidate->vignette);
    candidate->root.Children().InsertAtBottom(candidate->blur);candidate->root.Children().InsertAtTop(candidate->tone);
    stage=L"Backdrop: apply retained state and attach lower root";
    candidate->apply(state,true);candidate->target.Root(candidate->root);
    candidate->counts.initialized=candidate->counts.hostBackdropEnabled=candidate->counts.lowerCompositionTarget=true;
    }catch(const winrt::hresult_error&error){throw winrt::hresult_error(error.code(),std::wstring(stage)+L": "+error.message().c_str());}
    impl_=std::move(candidate);
}
bool DesktopBackdrop::update(const DesktopBackdropState&state){
    auto&i=*impl_;i.onThread();validate(state);need(i.counts.initialized,"Backdrop is not initialized");
    if(i.rendererSurface)need(i.rendererSurface->valid()&&i.rendererSurface->width()==state.pixelWidth&&i.rendererSurface->height()==state.pixelHeight&&state.projectionPlane,"Resize the Projection renderer before updating its bridged backdrop");
    if(i.state==state)return false;
    try{i.apply(state,false);}catch(...){i.clear();throw;}return true;
}
bool DesktopBackdrop::attachForeground(Renderer&renderer){
    auto&i=*impl_;i.onThread();need(i.counts.initialized&&i.state.projectionPlane,"Foreground bridge requires an initialized Projection backdrop");
    if(i.rendererSurface){need(i.renderer==&renderer&&i.rendererSurface->valid(),"Backdrop already has another foreground renderer");return false;}
    need(!IsWindowVisible(i.window),"Foreground handoff requires a hidden owner HWND");
    auto borrowed=renderer.borrowCompositionSurface();need(borrowed->window()==i.window&&borrowed->width()==i.state.pixelWidth&&borrowed->height()==i.state.pixelHeight,"Foreground renderer must match backdrop HWND and physical dimensions");
    need(!renderer.stats().compositionSuspended,"Foreground renderer binding is already suspended");
    wc::ICompositionSurface surface{nullptr};wc::CompositionSurfaceBrush brush{nullptr};wc::SpriteVisual foreground{nullptr};wc::LayerVisual panel{nullptr};bool suspended{};
    try{
        const auto interop=i.compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>();
        checkResult(interop->CreateCompositionSurfaceForSwapChain(static_cast<IUnknown*>(borrowed->swapChain()),reinterpret_cast<ABI::Windows::UI::Composition::ICompositionSurface**>(winrt::put_abi(surface))),L"Projection: wrap existing renderer swap chain");
        brush=i.compositor.CreateSurfaceBrush(surface);brush.Stretch(wc::CompositionStretch::None);brush.HorizontalAlignmentRatio(0);brush.VerticalAlignmentRatio(0);
        foreground=i.compositor.CreateSpriteVisual();foreground.Brush(brush);panel=i.compositor.CreateLayerVisual();
        const winrt::Windows::Foundation::Numerics::float2 size{static_cast<float>(i.state.pixelWidth),static_cast<float>(i.state.pixelHeight)};
        foreground.Size(size);panel.Size(size);panel.Opacity(1);
        // Only the two existing target bindings change; no additional HWND,
        // swap chain, bitmap, device, or fullscreen application texture exists.
        suspended=renderer.suspendComposition(*borrowed);i.target.Root(nullptr);
        panel.Children().InsertAtBottom(i.root);panel.Children().InsertAtTop(foreground);i.target.Root(panel);
    }catch(...){
        const auto failure=std::current_exception();try{if(i.target)i.target.Root(nullptr);if(panel)panel.Children().RemoveAll();if(i.target)i.target.Root(i.root);}catch(...){i.clear();}
        try{if(foreground)foreground.Brush(nullptr);if(brush)brush.Surface(nullptr);}catch(...){}close(foreground);close(brush);surface=nullptr;close(panel);
        if(suspended&&borrowed->valid())try{renderer.restoreComposition(*borrowed);}catch(...){}std::rethrow_exception(failure);
    }
    i.renderer=&renderer;i.rendererSurface=std::move(borrowed);i.panel=std::move(panel);i.foreground=std::move(foreground);i.foregroundBrush=std::move(brush);i.foregroundSurface=std::move(surface);
    try{i.rendererSurface->bindResetObserver(&i,[](void*context)noexcept{auto&owner=*static_cast<Impl*>(context);++owner.counts.rendererInvalidations;owner.clear();});}catch(...){(void)i.detachBridge();throw;}
    i.counts.foregroundAttached=true;i.counts.panelOpacity=1;i.counts.foregroundRestorationAttempted=i.counts.foregroundRestored=false;++i.counts.foregroundAttachments;++i.counts.foregroundSurfaceAllocations;i.counts.visualAllocations+=2;++i.counts.brushAllocations;return true;
}
bool DesktopBackdrop::setPanelOpacity(double opacity){
    auto&i=*impl_;i.onThread();unit(opacity,"Projection panel opacity outside unit range");need(i.counts.foregroundAttached&&i.rendererSurface&&i.rendererSurface->valid(),"Attach Projection foreground before changing panel opacity");if(i.counts.panelOpacity==opacity)return false;
    try{i.panel.Opacity(static_cast<float>(opacity));i.counts.panelOpacity=opacity;++i.counts.propertyWrites;return true;}catch(...){i.clear();throw;}
}
bool DesktopBackdrop::detachForeground(){auto&i=*impl_;i.onThread();if(!i.rendererSurface)return false;need(!IsWindowVisible(i.window)||i.counts.panelOpacity==0,"Foreground restoration requires a hidden or fully faded owner");if(!i.detachBridge()){i.clear();throw std::runtime_error("Projection foreground restoration failed; bridge retired");}return true;}
void DesktopBackdrop::reset()noexcept{impl_->clear();}
DesktopBackdropStats DesktopBackdrop::stats()const noexcept{return impl_->counts;}
#endif
} // namespace endfield::native
