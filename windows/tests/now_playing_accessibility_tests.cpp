// Source HUDNowPlayingInteraction/NowPlayingCanvas accessibility contract and
// the fixed English "No music playing" state in every language.
#include "modules/now_playing_accessibility.hpp"
#include "modules/now_playing_artwork.hpp"
#include <iostream>
#include <stdexcept>
namespace m=endfield::modules;namespace c=endfield::core;
namespace {
unsigned checks{};
void check(bool value,const char*what){++checks;if(!value)throw std::runtime_error(what);}
m::NowPlayingViewInput track(){m::NowPlayingViewInput i;i.session=7;m::NowPlayingTrack t;t.title="Synthetic Song";t.artist="Example Artist";t.album="Fixture Album";t.duration=240;t.position=65;t.sampledAt=0;i.track=t;i.capabilities={true,true,true,true,true,true};return i;}
const m::NowPlayingSurface&surface(const std::vector<m::NowPlayingSurface>&rows,std::string_view id){for(const auto&s:rows)if(s.id==id)return s;throw std::runtime_error("Missing Now Playing surface");}
void empty(){
    for(const auto language:{c::Language::english,c::Language::simplifiedChinese,c::Language::traditionalChinese,c::Language::japanese,c::Language::korean}){
        m::NowPlayingPresentation view({true,false,language});view.setActive(true,0);view.sync({},0);
        const auto a=m::nowPlayingAccessibility(view,0);check(a.status=="No music playing","Accessible status keeps the source's fixed English empty state");
        const auto art=m::prepareNowPlayingArtwork(view);const auto&label=surface(art.panel,"nowPlaying.empty");
        check(label.content["text"]["string"].string()=="No music playing"&&label.opacity==1,"Empty label is the fixed English source string in every language");
        check(!a.buttons[0].enabled&&!a.buttons[2].enabled&&!a.buttons[4].enabled&&a.sliders.size()==1&&!a.sliders[0].enabled&&!a.sliders[0].value,"No track: every control is disabled");
        view.sync(track(),0);check(surface(m::prepareNowPlayingArtwork(view).panel,"nowPlaying.empty").opacity==0,"A track hides the empty label");
    }
}
void controls(){
    m::NowPlayingPresentation view({true,false,c::Language::simplifiedChinese});view.setActive(true,0);view.sync(track(),0);
    auto a=m::nowPlayingAccessibility(view,0);
    check(a.status=="Synthetic Song \xc2\xb7 Example Artist \xc2\xb7 Fixture Album","Status joins title · artist · album");
    check(a.buttons.size()==5&&a.buttons[0].id=="lyrics"&&a.buttons[1].id=="previous"&&a.buttons[2].id=="playPause"&&a.buttons[3].id=="next"&&a.buttons[4].id=="volume","Source button order and identifiers");
    check(a.buttons[1].label=="上一首"&&a.buttons[2].label=="播放"&&a.buttons[4].label=="应用音量"&&a.buttons[0].selected,"Localized source labels and lyric selection");
    check(a.buttons[2].help==a.status&&a.buttons[2].rect.width==36,"Buttons describe the playing track and keep the source hit rectangles");
    check(a.sliders.size()==1&&a.sliders[0].id=="seek"&&a.sliders[0].label=="播放进度"&&a.sliders[0].help=="1:05"&&a.sliders[0].maximum==240&&a.sliders[0].step==5&&!a.sliders[0].continuous,"Seek slider: label, time help, 5 s step, commit on release");
    check(m::nowPlayingAccessibleStep(a.sliders[0],-1)==60.&&m::nowPlayingAccessibleStep(a.sliders[0],1)==70.,"Increment/decrement step 5 seconds");
    view.perform(m::NowPlayingAction::volume,0);a=m::nowPlayingAccessibility(view,0);
    check(a.sliders.size()==2&&a.sliders[1].id=="appVolume"&&a.sliders[1].label=="当前播放应用音量"&&!a.sliders[1].enabled&&a.sliders[1].valueText=="不可用","Unavailable app volume reports the source unavailable value");
    check(a.sliders[1].help=="未找到单一可调整音频进程，请使用音量控制。"&&!m::nowPlayingAccessibleStep(a.sliders[1],1),"Unavailable volume explains why and cannot step");
    auto input=track();input.capabilities.volume=true;input.volumeAvailable=true;input.volume=.5;view.sync(input,0);a=m::nowPlayingAccessibility(view,0);
    check(a.sliders[1].enabled&&a.sliders[1].valueText=="50%"&&a.sliders[1].step==.02&&a.sliders[1].continuous&&a.sliders[1].help=="调整当前播放应用的现有音频路由。","Available route: percent value, 2% steps, continuous");
    check(m::nowPlayingAccessibleStep(a.sliders[1],1)==.52,"Volume increments by 2%");
    m::NowPlayingPresentation english({true,false,c::Language::english});english.setActive(true,0);auto playing=track();playing.track->isPlaying=true;english.sync(playing,0);
    check(m::nowPlayingAccessibility(english,0).buttons[2].label=="Pause","Playing state labels the toggle Pause");
}
}
int main(){
    try{empty();controls();std::cout<<"Now Playing accessibility: "<<checks<<" checks passed (synthetic presentation)\n";return 0;}
    catch(const std::exception&e){std::cerr<<"Now Playing accessibility failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
