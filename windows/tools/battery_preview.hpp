#pragma once
#include "app/overlay_host.hpp"
#include "modules/battery_presentation.hpp"
#include "native/module_scene.hpp"
#include "native/system_services.hpp"
#ifdef _WIN32
namespace endfield::tools {
// An observer of the one shared SystemServices battery provider. This owner
// never polls power, constructs another service, starts a timer or writes data.
class BatteryPreview final {
public:
    BatteryPreview(native::LayerRasterizer&,native::LayerRasterOptions,
        modules::BatteryReading={},modules::BatteryAppearance={},std::function<void()>openSettings={});
    ~BatteryPreview();
    void resize(const app::ClientMetrics&);bool receive(modules::BatteryReading);
    bool receive(const native::BatterySnapshot&);bool setAppearance(modules::BatteryAppearance);
    void setReduceMotion(bool,double);void cancelInteraction(double);
    void update(const core::Matrix4&,const core::source::DesktopChromeSettings&,
        const core::ModulePresentationSample&,float,double);
    bool requiresFrames(double)const;bool covers(core::Point)const;
    bool pointer(const app::PointerEvent&,double);
    const modules::BatteryPresentation&state()const noexcept;
    void upload(native::Renderer&);std::span<const native::LayerCompositionEntry>entries();
    void collected(native::Renderer&);void release(native::Renderer&);
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
#endif
