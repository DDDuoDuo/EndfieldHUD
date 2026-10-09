#include "tools/projection_workspace.hpp"
#ifdef _WIN32
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::tools {
namespace {namespace n=native;namespace m=modules;
void need(bool value,const char*message){if(!value)throw std::logic_error(message);}
}
struct ProjectionWorkspace::Impl {
    app::OverlayHost&host;n::Renderer&renderer;n::LayerRasterizer&raster;
    n::DesktopBackdrop&backdrop;n::NativeMediaRequestBroker&broker;
    n::NativeMediaRequestBroker::Client client;
    m::ProjectionModel model;m::ProjectionInteraction interaction;
    std::unique_ptr<ProjectionPreview>view;std::unique_ptr<ProjectionMediaBinding>media;
    n::LayerComposition composition;app::ClientMetrics metrics;double visibleTop{};
    double started{},closeStarted{},closeAlpha{1},time{};bool closing{},reduced{};
    std::optional<bool>blurEnabled;double blurFrom{},blurTo{},blurStart{},observedBlur{-1};bool blurAnimating{};
    Impl(app::OverlayHost&h,n::Renderer&r,n::LayerRasterizer&l,n::DesktopBackdrop&b,
        n::NativeMediaRequestBroker&mb,m::NotesColor color,double darkness,double blur)
        :host(h),renderer(r),raster(l),backdrop(b),broker(mb),client(broker.attachClient()),
         model(color,darkness,blur),interaction(model,{1280,800}){}
    void clock(double value){need(std::isfinite(value),"Projection workspace needs finite time");time=std::max(time,value);}
    double opacity(double at)const {
        if(!view)return 0;if(reduced)return closing?0:1;
        const double duration=closing?.16:.18;
        const auto progress=std::clamp((at-(closing?closeStarted:started))/duration,0.,1.);
        // NSWindow animator default is captured by the isolated source probe.
        const auto eased=core::CubicTiming{.25,.1,.25,1}.value(progress);
        return closing?closeAlpha*(1-eased):eased;
    }
    double blurOpacity(double at)const {
        if(!blurAnimating)return blurTo;
        const double p=std::clamp((at-blurStart)/.18,0.,1.);
        return blurFrom+(blurTo-blurFrom)*core::CubicTiming{.25,.1,.25,1}.value(p);
    }
    void updateBlur(){
        const bool enabled=model.backgroundEnabled()&&model.blur()>0;
        if(blurEnabled&&*blurEnabled==enabled&&observedBlur==model.blur())return;
        const bool changed=blurEnabled&&*blurEnabled!=enabled;
        blurFrom=blurOpacity(time);blurTo=enabled?model.blur():0;blurStart=time;
        blurAnimating=changed&&!reduced&&!closing;blurEnabled=enabled;observedBlur=model.blur();
    }
    void publish(){
        view->upload(renderer);
        // The retained fast path compares full supplemental draw identities,
        // including a poster becoming a live video texture without new storage.
        composition.setEntries(renderer,view->entries());composition.present(renderer);
        view->collected(renderer);media->collectRetired();broker.collectRetired();
    }
};
ProjectionWorkspace::ProjectionWorkspace(app::OverlayHost&h,n::Renderer&r,n::LayerRasterizer&l,
    n::DesktopBackdrop&b,n::NativeMediaRequestBroker&mb,m::NotesColor color,double darkness,double blur)
    :impl_(std::make_unique<Impl>(h,r,l,b,mb,color,darkness,blur)){}
ProjectionWorkspace::~ProjectionWorkspace(){auto&i=*impl_;try{close(i.time);need(i.broker.detachClient(i.client),"Projection client still has borrowed provider resources");}catch(...){std::terminate();}}
void ProjectionWorkspace::show(ProjectionPreviewOptions options,ProjectionMediaBindingOptions binding,double time,bool showWindow){
    auto&i=*impl_;i.clock(time);time=i.time;need(!i.view&&!i.host.stats().visible,"Projection begins on the hidden shared host");
    i.metrics=i.host.metrics();i.reduced=options.reduceMotion;i.started=time;i.closing=false;i.blurEnabled.reset();i.blurAnimating=false;i.updateBlur();
    i.view=std::make_unique<ProjectionPreview>(i.model,i.interaction,i.raster,std::move(options));
    try{
        i.view->resize(i.metrics,0,i.visibleTop);i.media=std::make_unique<ProjectionMediaBinding>(*i.view,i.model,i.broker,i.client,i.renderer,std::move(binding));
        i.backdrop.update({i.metrics.pixelWidth,i.metrics.pixelHeight,true,false,i.model.blur(),0,1,true});
        i.backdrop.attachForeground(i.renderer);i.backdrop.setPanelOpacity(0);i.renderer.setSourcePassEnabled(false);
        i.view->setActive(true,time);i.media->sync(time);render(time);if(showWindow)i.host.show();
    }catch(...){close(time);throw;}
}
void ProjectionWorkspace::resize(const app::ClientMetrics&metrics,double top){auto&i=*impl_;need(std::isfinite(top)&&top>=0,"Projection display inset is invalid");i.metrics=metrics;i.visibleTop=top;if(i.view&&metrics.pixelWidth&&metrics.pixelHeight)i.view->resize(metrics,0,top);}
void ProjectionWorkspace::dismiss(double time,bool reduceMotion){auto&i=*impl_;i.clock(time);time=i.time;if(!i.view||i.closing)return;
    i.closeAlpha=i.opacity(time);i.closeStarted=time;i.closing=true;i.reduced=reduceMotion;
    i.host.capturePointer(false);i.view->setActive(false,time);i.media->cancelImport();i.media->sync(time);
}
bool ProjectionWorkspace::presented()const noexcept{return bool(impl_->view);}
bool ProjectionWorkspace::closing()const noexcept{return impl_->closing;}
bool ProjectionWorkspace::dismissalComplete(double time)const noexcept{return impl_->view&&impl_->closing&&(impl_->reduced||time>=impl_->closeStarted+.16);}
std::optional<double>ProjectionWorkspace::dismissalDeadline()const noexcept{const auto&i=*impl_;return i.view&&i.closing?std::optional{i.closeStarted+(i.reduced?0:.16)}:std::nullopt;}
void ProjectionWorkspace::close(double time){auto&i=*impl_;i.clock(time);time=i.time;if(!i.view)return;
    i.view->setActive(false,time);if(i.media){i.media->cancelImport();i.media->sync(time);}
    i.host.hide();if(i.backdrop.stats().foregroundAttached)i.backdrop.setPanelOpacity(0);
    i.composition.detach(i.renderer);i.view->release(i.renderer);
    if(i.media){need(i.media->releaseResources(time),"Projection provider resources remain published after detach");i.media.reset();}i.view.reset();
    i.broker.collectRetired();i.backdrop.detachForeground();i.renderer.setSourcePassEnabled(true);i.closing=false;
}
bool ProjectionWorkspace::mediaChanged(double time){auto&i=*impl_;i.clock(time);time=i.time;return i.media&&i.media->refresh(time);}
bool ProjectionWorkspace::render(double time,bool submit){auto&i=*impl_;i.clock(time);time=i.time;if(!i.view)return false;if(!i.metrics.pixelWidth||!i.metrics.pixelHeight)return true;
    i.view->update(time);i.media->sync(time);i.media->refresh(time);i.updateBlur();
    i.backdrop.update({i.metrics.pixelWidth,i.metrics.pixelHeight,true,false,i.blurOpacity(time),0,1,true});
    i.backdrop.setPanelOpacity(i.opacity(time));i.publish();
    // Workspace entries already include logical-to-physical scale.
    i.renderer.setCamera(n::layerViewportProjection(i.metrics.pixelWidth,i.metrics.pixelHeight));
    if(submit)i.renderer.draw();return true;
}
bool ProjectionWorkspace::pointer(const app::PointerEvent&e,double time){auto&i=*impl_;i.clock(time);return i.view&&!i.closing&&i.view->pointer(e,i.time);}
bool ProjectionWorkspace::wheel(const app::WheelEvent&e,double time){auto&i=*impl_;i.clock(time);return i.view&&!i.closing&&i.view->wheel(e,i.time);}
bool ProjectionWorkspace::key(const app::KeyEvent&e,double time){auto&i=*impl_;i.clock(time);return i.view&&!i.closing&&i.view->key(e,i.time);}
void ProjectionWorkspace::cancelInteraction(double time){auto&i=*impl_;i.clock(time);if(i.view&&i.view->active())i.view->cancelInteraction(i.time);}
core::FrameDemand ProjectionWorkspace::demand(double time)const {const auto&i=*impl_;core::FrameDemand d;if(!i.view)return d;
    d.phase=core::VisibilityPhase::visible;d.presented=d.canAdvanceTransition=true;d.onScreen=i.metrics.pixelWidth&&i.metrics.pixelHeight;d.reduceMotion=i.reduced;
    d.finiteAnimation=(!i.reduced&&time<(i.closing?i.closeStarted+.16:i.started+.18))||(i.blurAnimating&&time<i.blurStart+.18)||i.view->requiresFrames(time)||i.broker.requiresFrames();return d;
}
ProjectionPreview*ProjectionWorkspace::preview()const noexcept{return impl_->view.get();}
ProjectionMediaBinding*ProjectionWorkspace::media()const noexcept{return impl_->media.get();}
const m::ProjectionModel&ProjectionWorkspace::model()const noexcept{return impl_->model;}
}
#endif
