#include "modules/now_playing_player_controls.hpp"
#include "modules/now_playing_lyrics_sources.hpp"
#include "modules/now_playing_players.hpp"
#include "native/archive_text_rules.hpp"
#include <algorithm>
#include <array>
#include <cmath>

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
constexpr std::string_view progressHans="\xe8\xbf\x9b\xe5\xba\xa6";  // 进度
constexpr std::string_view progressHant="\xe9\x80\xb2\xe5\xba\xa6";  // 進度
constexpr std::string_view openFullWidth="\xef\xbc\x88";             // （
constexpr std::string_view closeFullWidth="\xef\xbc\x89";            // ）
constexpr std::array<std::string_view,5>playPauseNames{"Play","Pause",playHans,pauseHans,pauseHant};
constexpr std::array<std::string_view,3>previousNames{"Previous",previousTrack,previousItem};
constexpr std::array<std::string_view,3>nextNames{"Next",nextTrack,nextItem};
// At most this many unnamed images are asked for their source per walk, and
// this many player-bar texts are kept.
constexpr unsigned maximumImageDetails=8;
constexpr std::size_t maximumTexts=32;
// Transport controls at most this far below their common ancestor form a
// player bar; the bar is that ancestor's grandparent.
constexpr unsigned barDepth=3,barLift=2;

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
char lower(char c)noexcept{return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;}
bool caseless(std::string_view a,std::string_view b)noexcept{
    if(a.size()!=b.size())return false;
    for(std::size_t k=0;k<a.size();++k)if(lower(a[k])!=lower(b[k]))return false;
    return true;
}
bool listed(std::span<const std::string_view>names,std::string_view label){
    return std::any_of(names.begin(),names.end(),[&](std::string_view name){return caseless(name,label);});
}
std::string without(std::string_view text,char removed){
    std::string out;out.reserve(text.size());
    for(const char c:text)if(c!=removed)out.push_back(c);
    return out;
}
bool endsWith(std::string_view text,std::string_view suffix)noexcept{return text.size()>=suffix.size()&&text.substr(text.size()-suffix.size())==suffix;}
std::optional<NowPlayingCommandKind>kindOf(std::string_view label){
    const auto text=nowPlayingControlText(label);
    if(text.empty())return std::nullopt;
    if(listed(playPauseNames,text))return NowPlayingCommandKind::playPause;
    if(listed(previousNames,text))return NowPlayingCommandKind::previous;
    if(listed(nextNames,text))return NowPlayingCommandKind::next;
    return std::nullopt;
}
bool clockText(std::string_view label){
    const auto spaced=trimmedSpaces(label);
    if(!spaced.empty()&&spaced.back()=='/')return nowPlayingClock(without(label,'/')).has_value();
    if(const auto slash=label.find('/');slash!=std::string_view::npos)
        return nowPlayingClock(label.substr(0,slash)).has_value()&&nowPlayingClock(label.substr(slash+1)).has_value();
    return nowPlayingClock(label).has_value();
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
    std::string lowered(value);
    for(auto&c:lowered)c=lower(c);
    return lowered.find("volume")!=std::string::npos||lowered.find(volumeHans)!=std::string::npos;
}

std::vector<NowPlayingWalked>nowPlayingWalk(NowPlayingAccessibleTree&tree,std::size_t limit,const std::function<bool()>&stop,
    const std::function<void(const NowPlayingWalked&)>&visit){
    struct Pending{std::uint64_t id{};unsigned depth{};std::optional<std::uint64_t>parent;};
    std::vector<NowPlayingWalked>result;
    std::vector<Pending>pending{{0,0,std::nullopt}};
    while(!pending.empty()&&result.size()<limit&&!(stop&&stop())){
        const auto next=pending.back();pending.pop_back();
        auto node=tree.node(next.id);
        if(!node)continue; // gone meanwhile
        result.push_back({next.id,std::move(*node),next.parent,next.depth});
        if(visit)visit(result.back());
        if(next.depth>=12)continue;
        const auto children=tree.children(next.id);
        const std::size_t first=children.size()>160?children.size()-160:0;
        for(std::size_t k=first;k<children.size();++k)pending.push_back({children[k],next.depth+1,next.id});
    }
    return result;
}

std::string nowPlayingControlText(std::string_view label){
    if(label.size()>4096)return {};
    auto text=trimmedAll(label);
    // A trailing shortcut hint: "播放（Ctrl + P）", "Next (Ctrl+Right)".
    for(const auto&[open,close]:{std::pair{openFullWidth,closeFullWidth},std::pair{std::string_view("("),std::string_view(")")}}){
        if(!endsWith(text,close))continue;
        const auto at=text.rfind(open);
        if(at==std::string::npos||at==0)continue;
        auto head=trimmedAll(std::string_view(text).substr(0,at));
        if(!head.empty())return head;
    }
    return text;
}
std::optional<NowPlayingCommandKind>nowPlayingControlKind(const NowPlayingControlNode&node){
    switch(node.role){
    case NowPlayingControlRole::button:
    case NowPlayingControlRole::other:
    case NowPlayingControlRole::image:
        break;
    default:
        return std::nullopt; // texts, headings, sliders and documents are never controls
    }
    if(const auto kind=kindOf(node.label))return kind;
    return kindOf(node.description);
}
bool nowPlayingControlNamed(NowPlayingCommandKind kind,std::string_view label){
    const auto found=kindOf(label);
    return found&&*found==kind&&kind!=NowPlayingCommandKind::seek;
}
std::optional<bool>nowPlayingControlPlaying(std::string_view label){
    const auto text=nowPlayingControlText(label);
    if(caseless(text,"Pause")||text==pauseHans||text==pauseHant)return true;
    if(caseless(text,"Play")||text==playHans)return false;
    return std::nullopt;
}
bool nowPlayingProgressLabel(std::string_view label){
    if(label.size()>1024)return false;
    if(label.find(progressHans)!=std::string_view::npos||label.find(progressHant)!=std::string_view::npos)return true;
    std::string lowered(label);
    for(auto&c:lowered)c=lower(c);
    return lowered.find("progress")!=std::string::npos;
}
std::vector<std::pair<std::string,std::string>>nowPlayingAccessibleAttributes(std::string_view text){
    std::vector<std::pair<std::string,std::string>>out;
    if(text.size()>64*1024)return out;
    std::string name,value;bool inValue{},escaped{};
    const auto flush=[&]{
        if(!name.empty()&&out.size()<128)out.emplace_back(std::move(name),std::move(value));
        name.clear();value.clear();inValue=false;
    };
    for(const char c:text){
        if(escaped){(inValue?value:name).push_back(c);escaped=false;continue;}
        if(c=='\\'){escaped=true;continue;}
        if(c==';'){flush();continue;}
        if(c==':'&&!inValue){inValue=true;continue;}
        (inValue?value:name).push_back(c);
    }
    flush();
    return out;
}
std::optional<std::string>nowPlayingPlayerArtworkURL(std::string_view source){
    if(source.empty()||source.size()>4096)return std::nullopt;
    if(auto direct=nowPlayingCatalogCoverURL(source))return direct;
    // A URL embedded in a player's own cache address; its query belongs to
    // the player's thumbnail request.
    for(const std::string_view scheme:{std::string_view("https://"),std::string_view("http://")}){
        const auto at=source.find(scheme,1);
        if(at==std::string_view::npos)continue;
        auto embedded=source.substr(at);
        embedded=embedded.substr(0,embedded.find_first_of("?#&"));
        if(auto url=nowPlayingCatalogCoverURL(embedded))return url;
    }
    return std::nullopt;
}
bool nowPlayingTitleShown(std::string_view windowTitle,std::span<const std::string>texts){
    const auto track=nowPlayingHeadingTrack(windowTitle);
    if(!track)return false;
    return std::any_of(texts.begin(),texts.end(),[&](const std::string&text){return trimmedAll(text)==track->title;});
}

NowPlayingControlScan::NowPlayingControlScan(NowPlayingAccessibleTree*tree):tree_(tree){}
std::optional<std::uint64_t>NowPlayingControlScan::buttonOf(std::uint64_t id)const{
    auto at=entries_.find(id);
    for(unsigned level=0;level<barDepth&&at!=entries_.end()&&at->second.parent;++level){
        at=entries_.find(*at->second.parent);
        if(at!=entries_.end()&&at->second.role==NowPlayingControlRole::button)return at->first;
    }
    return std::nullopt;
}
bool NowPlayingControlScan::inside(std::uint64_t id,std::uint64_t ancestor)const{
    for(auto at=entries_.find(id);at!=entries_.end();){
        if(at->first==ancestor)return true;
        if(!at->second.parent)return false;
        at=entries_.find(*at->second.parent);
    }
    return false;
}
std::optional<std::uint64_t>NowPlayingControlScan::container()const{
    if(!playPause_||!previous_||!next_)return std::nullopt;
    // The common ancestor of the three controls.
    std::vector<std::uint64_t>chain;
    for(auto at=entries_.find(*playPause_);at!=entries_.end();){
        chain.push_back(at->first);
        if(!at->second.parent)break;
        at=entries_.find(*at->second.parent);
    }
    std::optional<std::uint64_t>common;
    for(const auto id:chain)
        if(inside(*previous_,id)&&inside(*next_,id)){common=id;break;}
    if(!common)return std::nullopt;
    const unsigned top=entries_.at(*common).depth;
    for(const auto id:{*playPause_,*previous_,*next_})
        if(entries_.at(id).depth>top+barDepth)return std::nullopt;
    auto bar=*common;
    for(unsigned level=0;level<barLift;++level){
        const auto&entry=entries_.at(bar);
        if(!entry.parent||!entries_.count(*entry.parent))break;
        bar=*entry.parent;
    }
    return bar;
}
void NowPlayingControlScan::add(const NowPlayingWalked&walked){
    const auto&[id,node,parent,depth]=walked;
    entries_[id]={parent,depth,node.role};
    walked_.push_back(walked);
    if(!parent&&node.role==NowPlayingControlRole::document)webPage_=true;
    if(node.role==NowPlayingControlRole::document&&!node.children)emptyDocument_=true;
    if(const auto kind=nowPlayingControlKind(node)){
        // An icon inside a button stands for the button (its default action).
        const auto target=node.role==NowPlayingControlRole::button?std::optional<std::uint64_t>(id):buttonOf(id);
        const auto pressed=target.value_or(id);
        switch(*kind){
        case NowPlayingCommandKind::playPause:
            if(!playPause_){
                playPause_=pressed;
                playing_=nowPlayingControlPlaying(node.label);
                if(!playing_)playing_=nowPlayingControlPlaying(node.description);
                if(pressed!=id)playLabel_=id;
            }
            break;
        case NowPlayingCommandKind::previous:if(!previous_)previous_=pressed;break;
        case NowPlayingCommandKind::next:if(!next_)next_=pressed;break;
        case NowPlayingCommandKind::seek:break;
        }
    }
    if(!progress_&&(node.role==NowPlayingControlRole::other||node.role==NowPlayingControlRole::slider)&&nowPlayingProgressLabel(node.label)){
        progress_=id;
        if(tree_)progressBounds_=tree_->details(id).bounds;
    }else if(progress_&&!played_&&parent==progress_&&tree_){
        const auto details=tree_->details(id);
        if(details.bounds&&std::find(details.classes.begin(),details.classes.end(),"track")!=details.classes.end()){played_=id;playedBounds_=details.bounds;}
    }
    if(node.role==NowPlayingControlRole::text&&clocks_.size()<64&&clockText(node.label))clocks_.push_back(id);
    if(node.role==NowPlayingControlRole::image&&node.label.empty()&&tree_&&imageDetails_<maximumImageDetails){
        ++imageDetails_;
        if(auto url=nowPlayingPlayerArtworkURL(tree_->details(id).source))images_.emplace_back(id,std::move(*url));
    }
}
bool NowPlayingControlScan::complete()const{
    const auto bar=container();
    if(!bar)return false;
    if(played_&&inside(*played_,*bar))return true;
    return std::any_of(clocks_.begin(),clocks_.end(),[&](std::uint64_t id){return inside(id,*bar);});
}
NowPlayingPlayerControls NowPlayingControlScan::result()const{
    NowPlayingPlayerControls out;
    out.emptyDocument=emptyDocument_;
    const auto bar=container();
    if(bar){
        out.playPause=playPause_;out.previous=previous_;out.next=next_;out.playLabel=playLabel_;out.playing=playing_;
        if(progress_&&inside(*progress_,*bar)&&played_&&progressBounds_&&playedBounds_&&progressBounds_->width>0){
            const double end=playedBounds_->left+playedBounds_->width-progressBounds_->left;
            if(std::isfinite(end)){out.progress=progress_;out.played=played_;out.fraction=std::clamp(end/progressBounds_->width,0.,1.);}
        }
        for(const auto&[image,url]:images_)
            if(inside(image,*bar)){out.artwork=image;out.artworkURL=url;break;}
    }
    for(const auto&[id,node,parent,depth]:walked_){
        (void)parent;(void)depth;
        const auto&label=node.label;
        // A web page lists track lengths (playlists): its clocks count only
        // inside the player bar.
        const bool counted=bar?inside(id,*bar):!webPage_;
        switch(node.role){
        case NowPlayingControlRole::heading:
            if(!out.heading&&label.find(" - ")!=std::string::npos){out.heading=id;out.headingText=label;}
            break;
        case NowPlayingControlRole::text:{
            if(bar&&counted&&!label.empty()&&out.texts.size()<maximumTexts){out.texts.push_back(id);out.textLabels.push_back(label);}
            if(!counted)break;
            const auto spaced=trimmedSpaces(label);
            if(!spaced.empty()&&spaced.back()=='/'){
                // Only a label that is a clock is the elapsed label (NetEase's
                // artist separator " / " is not; re-reading it would fail).
                if(!out.positionSeconds)if(const auto seconds=nowPlayingClock(without(label,'/'))){out.positionSeconds=seconds;out.position=id;}
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
        default:
            break;
        }
    }
    if(out.positionSeconds&&out.durationSeconds&&*out.positionSeconds>*out.durationSeconds+1){out.positionSeconds.reset();out.position.reset();}
    return out;
}
NowPlayingPlayerControls nowPlayingPlayerControls(std::span<const NowPlayingWalked>walked){
    NowPlayingControlScan scan;
    for(const auto&w:walked)scan.add(w);
    return scan.result();
}
}
