#include "native/volume_accessibility.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::native {
namespace {
std::string percent(double value){return std::to_string(static_cast<int>(std::round(value*100)))+"%";}
}
VolumeAccessibility volumeAccessibility(const VolumeController&controller,const VolumeAccessibilityStrings&strings){
    VolumeAccessibility out;const auto&s=controller.snapshot();
    // Source: perAppAudio.statusMessage ?? controller.statusMessage ?? default.
    out.status=s.perAppStatus?*s.perAppStatus:s.routingStopped?controller.strings().appRoutingStopped:s.status?*s.status:strings.status;
    for(const auto&a:controller.actions())out.buttons.push_back({a.id,a.label,out.status,a.rect,a.enabled,a.highlighted});
    for(const auto&control:controller.sliders()){
        VolumeAccessibleSlider slider{control.id,control.label,{},{},control.visibleRect.value_or(control.rect),control.value,control.minimum,control.maximum,control.enabled};
        if(!control.value)slider.valueText=strings.unavailable;
        else if(control.id!="balance")slider.valueText=percent(*control.value);
        else slider.valueText=std::abs(*control.value)<.01?strings.centered:std::string(*control.value<0?"L ":"R ")+percent(std::abs(*control.value));
        std::optional<std::string>help;
        if(control.id.starts_with("app:")){
            const auto id=std::string_view(control.id).substr(4);
            const auto app=std::find_if(s.applications.begin(),s.applications.end(),[&](const auto&a){return a.id==id;});
            if(app!=s.applications.end()){
                if(app->error)help=*app->error+(app->state==VolumeAppState::failed?strings.retryCleanup:std::string{});
                else if(app->state==VolumeAppState::active)help=strings.appActive;
                else if(app->state==VolumeAppState::failed)help=strings.appFailed;
            }
        }
        slider.help=slider.valueText+(help?" · "+*help:(control.enabled?std::string{}:strings.notAdjustable));
        out.sliders.push_back(std::move(slider));
    }
    return out;
}
std::optional<double>volumeAccessibleStep(const VolumeAccessibleSlider&slider,int direction)noexcept{
    if(!slider.enabled||!slider.value||!std::isfinite(*slider.value)||(direction!=1&&direction!=-1))return std::nullopt;
    return std::clamp(*slider.value+direction*(slider.maximum-slider.minimum)*.02,slider.minimum,slider.maximum);
}
} // namespace endfield::native
