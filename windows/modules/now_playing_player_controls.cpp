#include "modules/now_playing_player_controls.hpp"
#include "native/archive_text_rules.hpp"
#include <algorithm>
#include <array>

namespace endfield::modules {
namespace {
// UTF-8 literals (the source files are not assumed to be compiled as UTF-8).
constexpr std::string_view pauseHans="\xe6\x9a\x82\xe5\x81\x9c";     // 暂停
constexpr std::string_view pauseHant="\xe6\x9a\xab\xe5\x81\x9c";     // 暫停
constexpr std::string_view playHans="\xe6\x92\xad\xe6\x94\xbe";      // 播放
constexpr std::string_view previousTrack="\xe4\xb8\x8a\xe4\xb8\x80\xe9\xa6\x96"; // 上一首
constexpr std::string_view previousItem="\xe4\xb8\x8a\xe4\xb8\x80\xe4\xb8\xaa";  // 上一个
constexpr std::string_view nextTrack="\xe4\xb8\x8b\xe4\xb8\x80\xe9\xa6\x96";     // 下一首
constexpr std::string_view nextItem="\xe4\xb8\x8b\xe4\xb8\x80\xe4\xb8\xaa";      // 下一个
constexpr std::string_view volumeHans="\xe9\x9f\xb3\xe9\x87\x8f";    // 音量
constexpr std::array<std::string_view,5>playPauseNames{"Play","Pause",playHans,pauseHans,pauseHant};
constexpr std::array<std::string_view,3>previousNames{"Previous",previousTrack,previousItem};
constexpr std::array<std::string_view,3>nextNames{"Next",nextTrack,nextItem};

std::string trimmedAll(std::string_view text){
    // Swift .whitespacesAndNewlines (the archive text rules implement it).
    static const auto rules=native::nativeArchiveTextRules();
    return rules.trimmed(text);
}
// Swift .whitespaces only (no newlines): ASCII space/tab and the Unicode
// space separators the controls use (NBSP, ideographic space).
std::string_view trimmedSpaces(std::string_view text){
    const auto strip=[&](bool front)->bool{
        for(const std::string_view space:{std::string_view(" "),std::string_view("\t"),std::string_view("\xc2\xa0"),std::string_view("\xe3\x80\x80")}){
            if(text.size()<space.size())continue;
            if(front?text.substr(0,space.size())==space:text.substr(text.size()-space.size())==space){
                if(front)text.remove_prefix(space.size());else text.remove_suffix(space.size());
                return true;
            }
        }
        return false;
    };
    while(strip(true)){}
    while(strip(false)){}
    return text;
}
bool listed(std::span<const std::string_view>names,std::string_view label){
    return std::find(names.begin(),names.end(),label)!=names.end();
}
std::string without(std::string_view text,char removed){
    std::string out;out.reserve(text.size());
    for(const char c:text)if(c!=removed)out.push_back(c);
    return out;
}
}

std::optional<double>nowPlayingClock(std::string_view value){
    if(value.size()>1024)return std::nullopt;
    const auto text=trimmedAll(value);
    std::vector<std::string_view>pieces;
    std::string_view rest(text);
    while(!rest.empty()){
        const auto colon=rest.find(':');
        const auto piece=rest.substr(0,colon);
        if(!piece.empty())pieces.push_back(piece); // Swift split omits empty fields
        if(colon==std::string_view::npos)break;
        rest.remove_prefix(colon+1);
    }
    if(pieces.size()!=2&&pieces.size()!=3)return std::nullopt;
    std::vector<double>numbers;
    for(const auto piece:pieces){
        if(piece.size()>3||!std::all_of(piece.begin(),piece.end(),[](char c){return c>='0'&&c<='9';}))return std::nullopt;
        double number{};for(const char c:piece)number=number*10+(c-'0');
        numbers.push_back(number);
    }
    for(std::size_t k=1;k<numbers.size();++k)if(numbers[k]>=60)return std::nullopt;
    double total{};for(const auto n:numbers)total=total*60+n;
    return total;
}
bool nowPlayingVolumeLabel(std::string_view value){
    std::string lower(value);
    for(auto&c:lower)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return lower.find("volume")!=std::string::npos||lower.find(volumeHans)!=std::string::npos;
}

std::vector<NowPlayingWalked>nowPlayingWalk(NowPlayingAccessibleTree&tree,std::size_t limit,const std::function<bool()>&stop){
    std::vector<NowPlayingWalked>result;
    std::vector<std::pair<std::uint64_t,unsigned>>pending{{0,0}};
    while(!pending.empty()&&result.size()<limit&&!(stop&&stop())){
        const auto[id,depth]=pending.back();pending.pop_back();
        auto node=tree.node(id);
        if(!node)continue; // gone meanwhile
        result.push_back({id,*node});
        if(depth>=12)continue;
        const auto children=tree.children(id);
        const std::size_t first=children.size()>160?children.size()-160:0;
        for(std::size_t k=first;k<children.size();++k)pending.emplace_back(children[k],depth+1);
    }
    return result;
}

NowPlayingPlayerControls nowPlayingPlayerControls(std::span<const NowPlayingWalked>walked){
    NowPlayingPlayerControls out;
    for(const auto&[id,node]:walked){
        const auto&label=node.label;
        switch(node.role){
        case NowPlayingControlRole::heading:
            if(!out.heading&&label.find(" - ")!=std::string::npos){out.heading=id;out.headingText=label;}
            break;
        case NowPlayingControlRole::text:{
            const auto spaced=trimmedSpaces(label);
            if(!spaced.empty()&&spaced.back()=='/'){
                if(!out.positionSeconds){out.positionSeconds=nowPlayingClock(without(label,'/'));out.position=id;}
            }else if(const auto slash=label.find('/');slash!=std::string::npos){
                // "1:23 / 4:56" in one label (web players).
                const auto first=nowPlayingClock(std::string_view(label).substr(0,slash)),second=nowPlayingClock(std::string_view(label).substr(slash+1));
                if(first&&second&&!out.positionSeconds&&!out.durationSeconds){out.positionSeconds=first;out.position=id;out.durationSeconds=second;out.duration=id;}
            }else if(!out.durationSeconds){
                if(const auto seconds=nowPlayingClock(label)){out.durationSeconds=seconds;out.duration=id;}
            }
            break;
        }
        case NowPlayingControlRole::slider:
            if(nowPlayingVolumeLabel(label))out.volume=id;
            break;
        case NowPlayingControlRole::button:
        case NowPlayingControlRole::other:
            if(!out.playPause&&listed(playPauseNames,label)){out.playPause=id;out.playing=nowPlayingControlPlaying(label);}
            else if(!out.previous&&listed(previousNames,label))out.previous=id;
            else if(!out.next&&listed(nextNames,label))out.next=id;
            break;
        case NowPlayingControlRole::document:
            if(!node.children)out.emptyDocument=true;
            break;
        case NowPlayingControlRole::image:
            break;
        }
    }
    if(out.positionSeconds&&out.durationSeconds&&*out.positionSeconds>*out.durationSeconds+1){out.positionSeconds.reset();out.position.reset();}
    return out;
}
bool nowPlayingControlNamed(NowPlayingCommandKind kind,std::string_view label){
    switch(kind){
    case NowPlayingCommandKind::playPause:return listed(playPauseNames,label);
    case NowPlayingCommandKind::previous:return listed(previousNames,label);
    case NowPlayingCommandKind::next:return listed(nextNames,label);
    case NowPlayingCommandKind::seek:return false;
    }
    return false;
}
std::optional<bool>nowPlayingControlPlaying(std::string_view label){
    if(label=="Pause"||label==pauseHans||label==pauseHant)return true;
    if(label=="Play"||label==playHans)return false;
    return std::nullopt;
}
}
