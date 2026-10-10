#include "modules/now_playing_accessibility.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
NowPlayingAccessibility nowPlayingAccessibility(const NowPlayingPresentation&view,double time){
    NowPlayingAccessibility out;const auto&input=view.input();const auto language=view.appearance().language;
    if(!input.track)out.status="No music playing";
    else for(const auto*part:{&input.track->title,&input.track->artist,&input.track->album})if(!part->empty()){if(!out.status.empty())out.status+=" \xc2\xb7 ";out.status+=*part;}
    for(const auto&face:view.actions())out.buttons.push_back({face.action,std::string(nowPlayingActionID(face.action)),face.label,out.status,face.rect,face.enabled,face.selected});
    const auto faces=view.sliders(time);
    for(std::size_t k=0;k<view.sliderCount();++k){
        const auto&face=faces[k];NowPlayingAccessibleSlider slider;slider.kind=face.kind;slider.rect=face.rect;slider.value=face.value;
        slider.minimum=face.minimum;slider.maximum=face.maximum;slider.enabled=face.enabled;
        if(face.kind==NowPlayingSliderKind::seek){
            slider.id="seek";slider.label=core::localized("Playback position","播放进度",language);slider.step=5;slider.continuous=false;
            slider.help=nowPlayingTime(face.value);
        }else{
            slider.id="appVolume";slider.label=core::localized("Playing app volume","当前播放应用音量",language);slider.step=.02;slider.continuous=true;
            // Windows adjusts the player's mixer route; there is no player-internal
            // volume or macOS temporary-route permission to describe.
            slider.help=input.volumeAvailable?core::localized("Adjusts the playing app's existing audio route.","调整当前播放应用的现有音频路由。",language)
                :core::localized("No single adjustable audio process. Use Volume controls.","未找到单一可调整音频进程，请使用音量控制。",language);
        }
        slider.valueText=face.value?(face.kind==NowPlayingSliderKind::seek?nowPlayingTime(face.value):std::to_string(static_cast<int>(std::round(*face.value*100)))+"%")
            :core::localized("Unavailable","不可用",language);
        out.sliders.push_back(std::move(slider));
    }
    return out;
}
std::optional<double>nowPlayingAccessibleStep(const NowPlayingAccessibleSlider&slider,int direction)noexcept{
    if(!slider.enabled||!slider.value||direction==0)return {};
    return std::clamp(*slider.value+(direction>0?1:-1)*slider.step,slider.minimum,slider.maximum);
}
}
