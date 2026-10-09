#pragma once
#ifdef _WIN32
#include "tools/projection_media_binding.hpp"
#include "native/desktop_backdrop.hpp"

namespace endfield::tools {
// Projection's session model survives each presentation. Its visible objects
// borrow the same host, renderer, backdrop and media broker as the HUD. Root
// detaches the HUD composition before show(), and republishes it after close().
// This owner creates no window, timer, decoder or thread.
class ProjectionWorkspace final {
public:
    ProjectionWorkspace(app::OverlayHost&,native::Renderer&,native::LayerRasterizer&,
        native::DesktopBackdrop&,native::NativeMediaRequestBroker&,
        modules::NotesColor,double darkness,double blur);
    ~ProjectionWorkspace();
    void show(ProjectionPreviewOptions,ProjectionMediaBindingOptions,double,bool showWindow=true);
    void resize(const app::ClientMetrics&,double visibleTop=0);
    void dismiss(double,bool reduceMotion);
    bool presented()const noexcept;
    bool closing()const noexcept;
    bool dismissalComplete(double)const noexcept;
    std::optional<double>dismissalDeadline()const noexcept;
    void close(double); // hidden restore; keeps only the session model
    // Caller alone accepts/samples the shared broker, then calls mediaChanged.
    bool mediaChanged(double);
    bool render(double,bool submit=true);
    bool pointer(const app::PointerEvent&,double);
    bool wheel(const app::WheelEvent&,double);
    bool key(const app::KeyEvent&,double);
    void cancelInteraction(double);
    core::FrameDemand demand(double)const;
    ProjectionPreview*preview()const noexcept;
    ProjectionMediaBinding*media()const noexcept;
    const modules::ProjectionModel&model()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
