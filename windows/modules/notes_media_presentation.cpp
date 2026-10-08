#include "modules/notes_media_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}}
NotesMediaLayout::NotesMediaLayout(double w,double h,NotesMediaKind kind,std::optional<double>duration,bool legacy):kind_(kind),duration_(duration){
    need(std::isfinite(w)&&std::isfinite(h)&&w>=10&&h>=35&&w<=65536&&h<=65536,"Invalid media card extent");
    need(kind==NotesMediaKind::image||kind==NotesMediaKind::gif||kind==NotesMediaKind::video,"Invalid media kind");
    need(!duration||(std::isfinite(*duration)&&*duration>0&&*duration<=31536000),"Invalid media duration");
    need(!legacy||kind==NotesMediaKind::image,"Legacy managed artwork is image-only");
    geometry_.legacy=legacy;
    geometry_.content={5,29,w-10,legacy?h-35:std::max(1.,h-56)};
    geometry_.footer={8,h-23,kind==NotesMediaKind::video?65:w-18,18};
    geometry_.playback={6,h-24,66,19};geometry_.hasPlayback=!legacy&&kind!=NotesMediaKind::image;
    geometry_.seek={78,h-25,std::max(1.,w-97),20};geometry_.hasSeek=!legacy&&kind==NotesMediaKind::video&&duration.has_value();
    geometry_.rail={geometry_.seek.x,geometry_.seek.y+9,geometry_.seek.width,2};
}
core::Rect NotesMediaLayout::fittedImage(unsigned w,unsigned h)const{
    need(w&&h&&w<=65536&&h<=65536,"Invalid decoded media dimensions");const auto&r=geometry_.content;
    const double scale=std::min(r.width/double(w),r.height/double(h));
    return {r.x+(r.width-w*scale)*.5,r.y+(r.height-h*scale)*.5,w*scale,h*scale};
}
double NotesMediaLayout::seekSeconds(double x)const{
    need(geometry_.hasSeek&&std::isfinite(x),"Media seek requires finite video position");
    return std::clamp((x-geometry_.seek.x)/geometry_.seek.width,0.,1.)*(*duration_);
}
NotesLayer NotesMediaLayout::footer(const NotesMediaStatus&s,const NotesPalette&p,const NotesMediaStrings&strings)const{
    need(!geometry_.legacy,"Legacy image has no media footer");NotesLayer result;result.id="media/footer";result.kind=NotesLayerKind::text;
    result.frame=geometry_.footer;result.bounds={0,0,result.frame.width,result.frame.height};result.text.fontSize=10;result.text.truncateEnd=true;result.text.color=p.primary;
    if(s.localizedError)result.text.text=*s.localizedError;
    else if(s.state==NotesMediaState::loading)result.text.text=strings.loading;
    else if(kind_!=NotesMediaKind::image)result.text.text=s.state==NotesMediaState::playing?strings.pause:strings.play;
    return result;
}
std::optional<NotesAction>NotesMediaLayout::playbackAction(std::string_view id,const NotesMediaStatus&s,const NotesMediaStrings&strings)const{
    if(!geometry_.hasPlayback)return {};need(!id.empty(),"Media action requires note identity");
    return NotesAction{"note:"+std::string(id)+":mediaPlayback","mediaPlayback",s.state==NotesMediaState::playing?strings.pauseAction:strings.playAction,geometry_.playback};
}
std::array<NotesColor,3>NotesMediaLayout::progressColors(const NotesPalette&p)const noexcept{auto rail=p.muted;rail[3]=.3;return {rail,p.accent,p.primary};}
unsigned NotesMediaLayout::decodeDimension(int n)noexcept{return static_cast<unsigned>(std::clamp(n,32,768));}
double NotesMediaLayout::normalizedFrameDelay(std::optional<double>v)noexcept{return !v||!std::isfinite(*v)||*v<=0?.1:std::clamp(*v,.04,600.);}
NotesMediaProgress::NotesMediaProgress(const NotesMediaLayout&layout){if(layout.geometry().hasSeek){seek_=layout.geometry().seek;duration_=*layout.duration();}}
void NotesMediaProgress::update(double time,std::optional<double>preview,bool animated,bool playing,bool visible,bool reduced,double now){
    need(std::isfinite(time)&&(!preview||std::isfinite(*preview))&&std::isfinite(now),"Nonfinite media progress");
    if(!duration_){active_=false;return;}
    const double from=seek_.width*std::clamp(preview.value_or(time)/duration_,0.,1.);
    const bool interpolate=animated&&playing&&!preview&&visible&&!reduced;
    from_=from;to_=interpolate?std::min(seek_.width,from+seek_.width/duration_):from;start_=now;active_=interpolate&&from_!=to_;
}
NotesMediaProgressPose NotesMediaProgress::sample(double now)const{
    need(std::isfinite(now),"Nonfinite media progress clock");if(!duration_)return {};
    const double width=from_+(to_-from_)*std::clamp(now-start_,0.,1.),mid=seek_.y+seek_.height*.5;
    return {{seek_.x,mid-1,width,2},{seek_.x+width-2.5,mid-4,5,8},width/seek_.width,active_&&now<start_+1};
}
bool NotesMediaProgress::needsFrame(double now)const{need(std::isfinite(now),"Nonfinite media progress clock");return active_&&now<start_+1;}
} // namespace endfield::modules
