#include "modules/now_playing_model.hpp"
#include "modules/archive_model.hpp"
#include "native/archive_text_rules.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#ifdef _WIN32
#include <icu.h>
#else
#include <unicode/ubrk.h>
#include <unicode/unorm2.h>
#include <unicode/uregex.h>
#endif

namespace endfield::modules {
namespace {
std::string prefix(std::string_view value,std::size_t maximum){
    if(value.size()>12*1024*1024)throw std::invalid_argument("Now Playing metadata exceeds source adapter bound");
    const auto text=archiveUTF16(value);if(text.empty())return {};
    // Every Character spans at least one UTF-16 unit, so fewer units than the
    // bound can never be truncated: skip segmentation (the source result is
    // the unchanged string, exactly as the iterator would reach UBRK_DONE).
    if(text.size()<maximum)return std::string(value);
    UErrorCode error=U_ZERO_ERROR;auto*iterator=ubrk_open(UBRK_CHARACTER,"",reinterpret_cast<const UChar*>(text.data()),static_cast<std::int32_t>(text.size()),&error);
    if(U_FAILURE(error)||!iterator){if(iterator)ubrk_close(iterator);throw std::runtime_error("Cannot segment Now Playing text");}
    struct End{UBreakIterator*value;~End(){ubrk_close(value);}}end{iterator};
    std::int32_t finish{};ubrk_first(iterator);for(std::size_t i=0;i<maximum;++i){const auto next=ubrk_next(iterator);if(next==UBRK_DONE)return std::string(value);finish=next;}
    return archiveUTF8(std::u16string_view(text).substr(0,static_cast<std::size_t>(finish)));
}
std::u16string canonical(std::string_view value){const auto text=archiveUTF16(value);UErrorCode error=U_ZERO_ERROR;const auto*normalizer=unorm2_getNFCInstance(&error);if(U_FAILURE(error))throw std::runtime_error("Cannot normalize Now Playing text");
    error=U_ZERO_ERROR;const auto length=unorm2_normalize(normalizer,reinterpret_cast<const UChar*>(text.data()),static_cast<std::int32_t>(text.size()),nullptr,0,&error);if(error!=U_BUFFER_OVERFLOW_ERROR&&U_FAILURE(error))throw std::runtime_error("Cannot size Now Playing text");
    std::u16string out(static_cast<std::size_t>(length),u'\0');error=U_ZERO_ERROR;unorm2_normalize(normalizer,reinterpret_cast<const UChar*>(text.data()),static_cast<std::int32_t>(text.size()),reinterpret_cast<UChar*>(out.data()),length,&error);if(U_FAILURE(error))throw std::runtime_error("Cannot normalize Now Playing text");return out;
}
bool equal(std::string_view a,std::string_view b){return a==b||canonical(a)==canonical(b);}
bool newline(char16_t c){return (c>=10&&c<=13)||c==0x85||c==0x2028||c==0x2029;}
struct Regex {
    URegularExpression*value{};UErrorCode error=U_ZERO_ERROR;
    explicit Regex(std::u16string_view pattern){value=uregex_open(reinterpret_cast<const UChar*>(pattern.data()),static_cast<int32_t>(pattern.size()),0,nullptr,&error);check();}
    ~Regex(){if(value)uregex_close(value);}
    void check(){if(U_FAILURE(error)){if(value)uregex_close(value);value=nullptr;throw std::runtime_error("Cannot match Now Playing lyrics");}}
    void set(std::u16string_view text){uregex_setText(value,reinterpret_cast<const UChar*>(text.data()),static_cast<int32_t>(text.size()),&error);check();}
    bool next(){const auto result=uregex_findNext(value,&error);check();return result!=0;}
    int32_t start(int32_t group=0){const auto result=uregex_start(value,group,&error);check();return result;}
    int32_t end(int32_t group=0){const auto result=uregex_end(value,group,&error);check();return result;}
    std::u16string_view group(std::u16string_view text,int32_t index){const auto first=start(index);return first<0?std::u16string_view{}:text.substr(static_cast<std::size_t>(first),static_cast<std::size_t>(end(index)-first));}
};
double decimal(std::u16string_view text,double fallback){
    if(text.empty())return fallback;double sign=1,value=0,scale=1;std::size_t i{};bool fraction{};
    if(text.front()==u'-'||text.front()==u'+'){sign=text.front()==u'-'?-1:1;++i;}
    for(;i<text.size();++i){const auto c=text[i];if((c==u'.'||c==u',')&&!fraction){fraction=true;continue;}if(c<u'0'||c>u'9')return fallback;value=value*10+(c-u'0');if(fraction)scale*=10;}
    return sign*value/scale;
}
}
NowPlayingTrack NowPlayingTrack::bounded(NowPlayingTrack value){
    value.title=prefix(value.title,512);value.artist=prefix(value.artist,512);value.album=prefix(value.album,512);
    if(value.duration&&std::isfinite(*value.duration)&&*value.duration>0)value.duration=std::min(*value.duration,nowPlayingMaximumSeconds);else value.duration.reset();
    if(value.position&&std::isfinite(*value.position)&&*value.position>=0)value.position=std::min(*value.position,value.duration.value_or(nowPlayingMaximumSeconds));else value.position.reset();
    if(!std::isfinite(value.sampledAt))value.sampledAt=0;
    if(value.identifier){if(value.identifier->empty())value.identifier.reset();else value.identifier=prefix(*value.identifier,512);}
    if(value.timedLyrics&&value.timedLyrics->size()>nowPlayingMaximumLyricsBytes)value.timedLyrics.reset();
    if(value.artworkRevision)value.artworkRevision=prefix(*value.artworkRevision,256);
    return value;
}
std::optional<double>NowPlayingTrack::elapsed(double now)const noexcept{
    if(!position)return {};const auto delta=isPlaying&&std::isfinite(now)?std::max(0.,now-sampledAt):0.;return std::min(duration.value_or(nowPlayingMaximumSeconds),*position+delta);
}
bool NowPlayingTrack::sameIdentity(const NowPlayingTrack&other)const{
    if(identifier&&other.identifier&&!equal(*identifier,*other.identifier))return false;
    return equal(title,other.title)&&equal(artist,other.artist)&&equal(album,other.album)&&duration==other.duration;
}
std::optional<NowPlayingLyrics>NowPlayingLyrics::parse(std::string_view lrc){
    if(lrc.size()>nowPlayingMaximumLyricsBytes||!ehud::data::Json::validUtf8(lrc))return {};
    // Foundation NSRegularExpression uses ICU; preserve its Unicode stamp and
    // offset matching, including the final stamp whose numeric parse can fail.
    Regex stamp(uR"(\[(?:(\d{1,2}):)?(\d{1,3}):(\d{1,2}(?:[.,]\d{1,3})?)\])");
    Regex offsetPattern(uR"((?i)\[offset:([+-]?\d{1,7})\])");
    const auto text=archiveUTF16(lrc);double offset{};offsetPattern.set(text);if(offsetPattern.next())offset=std::clamp(decimal(offsetPattern.group(text,1),0)/1000.,-3600.,3600.);
    std::vector<NowPlayingLyricLine>parsed;parsed.reserve(64);std::size_t first{},rows{};const auto trim=native::nativeArchiveTextRules().trimmed;
    while(first<=text.size()&&rows++<4096&&parsed.size()<4096){auto last=first;while(last<text.size()&&!newline(text[last]))++last;
        if(last-first<=8192){const auto line=std::u16string_view(text).substr(first,last-first);stamp.set(line);std::array<double,32>times{};std::size_t count{};int32_t lastEnd{};bool starts{};
            while(stamp.next()){if(!lastEnd)starts=stamp.start()==0;lastEnd=stamp.end();if(count<times.size()){
                const auto hours=decimal(stamp.group(line,1),0),minutes=decimal(stamp.group(line,2),-1),seconds=decimal(stamp.group(line,3),-1);
                const auto time=hours*3600+minutes*60+seconds+offset;times[count++]=minutes>=0&&seconds>=0&&seconds<60&&std::isfinite(time)&&time<=nowPlayingMaximumSeconds?std::max(0.,time):-1.;}}
            if(starts&&lastEnd){const auto content=prefix(trim(archiveUTF8(line.substr(static_cast<std::size_t>(lastEnd)))),1024);for(std::size_t i=0;i<count&&parsed.size()<4096;++i)if(times[i]>=0)parsed.push_back({times[i],content});}
        }
        if(last==text.size())break;first=last+1;
    }
    std::stable_sort(parsed.begin(),parsed.end(),[](const auto&a,const auto&b){return a.time<b.time;});NowPlayingLyrics result;result.lines_.reserve(parsed.size());bool content{};
    for(auto&line:parsed){if(!result.lines_.empty()&&result.lines_.back().time==line.time){auto&previous=result.lines_.back();if(!line.text.empty()&&!equal(previous.text,line.text))previous.text=prefix(previous.text.empty()?line.text:previous.text+" / "+line.text,1024);}
        else result.lines_.push_back(std::move(line));if(!result.lines_.back().text.empty())content=true;}
    if(!content)return {};return result;
}
std::size_t NowPlayingLyrics::firstAfter(double value)const noexcept{return static_cast<std::size_t>(std::upper_bound(lines_.begin(),lines_.end(),value,[](double time,const auto&line){return time<line.time;})-lines_.begin());}
std::array<std::string_view,3>NowPlayingLyrics::window(double elapsed)const noexcept{
    std::array<std::string_view,3>out{};if(!std::isfinite(elapsed)){if(!lines_.empty())out[2]=lines_.front().text;return out;}
    const auto current=static_cast<std::ptrdiff_t>(firstAfter(elapsed))-1;for(std::ptrdiff_t i=0;i<3;++i){const auto index=current+i-1;if(index>=0&&static_cast<std::size_t>(index)<lines_.size())out[static_cast<std::size_t>(i)]=lines_[static_cast<std::size_t>(index)].text;}return out;
}
std::optional<double>NowPlayingLyrics::nextBoundary(double elapsed)const noexcept{if(!std::isfinite(elapsed))return {};const auto index=firstAfter(elapsed);return index<lines_.size()?std::optional(lines_[index].time):std::nullopt;}
std::string nowPlayingTime(std::optional<double>value){if(!value||!std::isfinite(*value)||*value<0)return "—:—";const auto seconds=static_cast<unsigned>(std::min(*value,nowPlayingMaximumSeconds));return std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60);}
std::optional<double>nowPlayingDisplayDeadline(const NowPlayingTrack&track,double now,bool active,const NowPlayingLyrics*lyrics,bool lyricsVisible,bool seeking)noexcept{
    if(!active||!track.isPlaying||!std::isfinite(now))return {};const auto elapsed=track.elapsed(now);if(!elapsed||!std::isfinite(*elapsed)||(track.duration&&*elapsed>=*track.duration))return {};
    auto delay=std::floor(*elapsed)+1-*elapsed;if(lyrics&&lyricsVisible&&!seeking)if(const auto boundary=lyrics->nextBoundary(*elapsed))delay=std::min(delay,*boundary-*elapsed);
    if(track.duration)delay=std::min(delay,*track.duration-*elapsed);return now+std::max(1./30.,delay);
}
bool nowPlayingAcknowledgesSeek(const NowPlayingTrack&track,double requested,double sampledAt)noexcept{
    if(!track.position||!std::isfinite(requested)||!std::isfinite(sampledAt))return false;const auto advance=track.isPlaying?std::min(3.,std::max(0.,track.sampledAt-sampledAt)):0.;return std::abs(*track.position-std::min(track.duration.value_or(nowPlayingMaximumSeconds),requested+advance))<=1.25;
}
bool nowPlayingArtworkMetadata(unsigned width,unsigned height,std::size_t bytes)noexcept{return bytes>0&&bytes<=nowPlayingMaximumArtworkBytes&&width>0&&height>0&&width<=8192&&height<=8192&&std::uint64_t(width)*height<=nowPlayingMaximumArtworkPixels;}
std::optional<NowPlayingCommand>nowPlayingCommand(const NowPlayingTrack&track,const NowPlayingCapabilities&controls,NowPlayingCommand command)noexcept{
    switch(command.kind){case NowPlayingCommandKind::playPause:if(!(controls.toggle||(track.isPlaying?controls.pause:controls.play)))return {};break;
    case NowPlayingCommandKind::previous:if(!controls.previous)return {};break;case NowPlayingCommandKind::next:if(!controls.next)return {};break;
    case NowPlayingCommandKind::seek:if(!controls.seek||!track.supportsSeeking||!track.position||!track.duration||!std::isfinite(command.seconds))return {};command.seconds=std::clamp(command.seconds,0.,*track.duration);break;default:return {};}
    return command;
}
}
