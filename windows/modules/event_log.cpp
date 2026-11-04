#include "modules/event_log.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>

namespace endfield::modules {
namespace {
void need(bool okay,const char*message){if(!okay)throw std::invalid_argument(message);}
struct KindInfo{std::string_view key,title;EventCategory category;};
constexpr std::array kindInfo{
    KindInfo{"overlayOpened","Overlay opened",EventCategory::navigation},
    KindInfo{"moduleOpened","Module opened",EventCategory::navigation},
    KindInfo{"appShortcutOpened","App shortcut opened",EventCategory::navigation},
    KindInfo{"clipboardCopied","Clipboard item copied",EventCategory::clipboard},
    KindInfo{"shelfAdded","Added to file shelf",EventCategory::files},
    KindInfo{"shelfRemoved","Removed from file shelf",EventCategory::files},
    KindInfo{"shelfCleared","File shelf cleared",EventCategory::files},
    KindInfo{"workStarted","Work timer started",EventCategory::work},
    KindInfo{"workPaused","Work timer paused",EventCategory::work},
    KindInfo{"workResumed","Work timer resumed",EventCategory::work},
    KindInfo{"workReset","Work timer reset",EventCategory::work},
    KindInfo{"workCompleted","Work timer completed",EventCategory::work},
    KindInfo{"powerConnected","Power connected",EventCategory::power},
    KindInfo{"powerDisconnected","Power disconnected",EventCategory::power},
    KindInfo{"batteryStateChanged","Battery state changed",EventCategory::power},
    KindInfo{"audioDeviceConnected","Audio device connected",EventCategory::audio},
    KindInfo{"audioDeviceDisconnected","Audio device disconnected",EventCategory::audio},
    KindInfo{"displayConnected","Display connected",EventCategory::display},
    KindInfo{"displayDisconnected","Display disconnected",EventCategory::display},
    KindInfo{"displaySettingsChanged","Display setting changed",EventCategory::display},
    KindInfo{"profileCropChanged","Profile crop changed",EventCategory::display},
    KindInfo{"mapPinStyleChanged","Map pin style changed",EventCategory::navigation},
    KindInfo{"mapRecentered","Map recentered",EventCategory::navigation},
    KindInfo{"noteAction","Note changed",EventCategory::files},
    KindInfo{"playbackAction","Playback changed",EventCategory::audio},
    KindInfo{"projectionAction","Projection changed",EventCategory::display},
    KindInfo{"archiveAction","Archive changed",EventCategory::files},
    KindInfo{"readerAction","Reader changed",EventCategory::files},
    KindInfo{"mediaAssemblyAction","Media changed",EventCategory::files},
    KindInfo{"calendarAction","Calendar changed",EventCategory::work},
    KindInfo{"minigameAction","Minigame",EventCategory::work},
    KindInfo{"accountAction","Account changed",EventCategory::display},
};
constexpr std::array<std::string_view,7>categoryKeys{"navigation","clipboard","files","work","power","audio","display"},categoryTitles{"Navigation","Clipboard","Files","Work","Power","Audio","Display"};
const KindInfo&info(EventKind kind){const auto n=static_cast<std::size_t>(kind);need(n<kindInfo.size(),"Invalid system event kind");return kindInfo[n];}
bool uuid(std::string_view value){if(value.size()!=36)return false;for(std::size_t n=0;n<36;++n){if(n==8||n==13||n==18||n==23){if(value[n]!='-')return false;}else if(!((value[n]>='0'&&value[n]<='9')||(value[n]>='a'&&value[n]<='f')||(value[n]>='A'&&value[n]<='F')))return false;}return true;}
std::string canonicalID(std::string_view value){need(uuid(value),"Event identity requires a UUID");std::string out(value);for(auto&c:out)if(c>='a'&&c<='f')c=char(c-'a'+'A');return out;}
void shortText(std::string_view text,std::size_t bytes=4096){need(text.size()<=bytes&&ehud::data::Json::validUtf8(text),"Invalid bounded event display text");}
std::optional<std::string_view>get(const EventMetadata&m,std::string_view key){const auto it=m.find(key);if(it==m.end())return {};return it->second;}
bool in(std::string_view value,std::initializer_list<std::string_view>values){return std::find(values.begin(),values.end(),value)!=values.end();}
std::string compactName(std::string_view value,const EventNameCompactor&provided){
    // Do not invent an incomplete grapheme truncator. Native callers must
    // provide the original compaction boundary for long/complex Unicode names.
    if(provided){auto out=provided(value);shortText(out,160);need(out.find_first_of("/\\")==std::string::npos&&out.find("://")==std::string::npos,"Compacted event name contains a private path");for(unsigned char c:out)need(c>=32&&c!=127,"Compacted event name contains controls");return out;}
    shortText(value);std::string out;out.reserve(std::min<std::size_t>(160,value.size()));bool space=true,unicode{};std::size_t inspected{};
    for(std::size_t at=0;at<value.size()&&inspected<512;++inspected){const unsigned char c=static_cast<unsigned char>(value[at]);if(c<128){++at;if(c==13&&at<value.size()&&value[at]==10)++at;const bool blank=c<=32||c==127;if(blank){if(!space){if(out.size()==160)break;out+=' ';}space=true;continue;}if(out.size()==160)break;out+=char(c);space=false;continue;}
        unicode=true;const auto count=c<0xe0?2u:c<0xf0?3u:4u;std::uint32_t scalar=c&((1u<<(7-count))-1);for(unsigned k=1;k<count;++k)scalar=(scalar<<6)|(static_cast<unsigned char>(value[at+k])&63);
        const bool special=(scalar>=0x80&&scalar<=0x9f)||scalar==0xa0||scalar==0xad||scalar==0x61c||scalar==0x1680||scalar==0x180e||(scalar>=0x2000&&scalar<=0x200f)||scalar==0x2028||scalar==0x2029||(scalar>=0x202a&&scalar<=0x202f)||scalar==0x205f||(scalar>=0x2060&&scalar<=0x206f)||scalar==0x3000||scalar==0xfeff;
        need(!special&&out.size()+count<=160,"Unicode event name needs source-equivalent grapheme compactor");out.append(value.substr(at,count));at+=count;space=false;
    }
    // Source's byte bound must not sever a cluster that extends beyond it.
    if(unicode)need(value.size()<=160,"Unicode event name needs source-equivalent grapheme compactor");while(!out.empty()&&out.back()==' ')out.pop_back();return out;
}
std::string_view moduleTitle(std::string_view key){
    if(key=="notes")return "Notes";
    if(key=="fileShelf")return "Temporary File Shelf";
    if(key=="clipboard")return "Clipboard Cache";
    if(key=="volume")return "Volume";
    if(key=="account")return "Account Linking";
    if(key=="nowPlaying")return "Now Playing";
    if(key=="projection")return "Projection";
    if(key=="reader")return "E-Reader";
    if(key=="archive")return "Archive";
    if(key=="mediaAssembly")return "Media Assembly";
    if(key=="calendar")return "Calendar";
    if(key=="minigame")return "Closure's Minigame";
    if(key=="workMode")return "Work Mode";
    if(key=="eventLog")return "Event Log";
    if(key=="map")return "Map";
    if(key=="addApp")return "+ Add App";
    if(key=="system")return "System";
    if(key=="display")return "Display";
    if(key=="hotkeys")return "Hotkeys";
    if(key=="about")return "About";
    if(key=="storage")return "Storage";
    if(key=="activityMonitor")return "Activity Monitor";
    if(key=="power")return "Power";
    if(key=="profile")return "Personal Profile";
    return {};
}
}
std::string_view eventCategoryKey(EventCategory category){const auto n=static_cast<std::size_t>(category);need(n<categoryKeys.size(),"Invalid system event category");return categoryKeys[n];}
std::string_view eventCategoryTitle(EventCategory category){const auto n=static_cast<std::size_t>(category);need(n<categoryTitles.size(),"Invalid system event category");return categoryTitles[n];}
std::optional<EventCategory>eventCategoryFromKey(std::string_view key)noexcept{for(unsigned n=0;n<categoryKeys.size();++n)if(categoryKeys[n]==key)return static_cast<EventCategory>(n);return {};}
std::string_view eventKindKey(EventKind kind){return info(kind).key;}std::string_view eventKindTitle(EventKind kind){return info(kind).title;}EventCategory eventCategory(EventKind kind){return info(kind).category;}
EventMetadata sanitizedEventMetadata(EventKind kind,const EventMetadata&raw,const EventNameCompactor&compactor){info(kind);EventMetadata out;
    const auto one=[&](std::string_view key,std::initializer_list<std::string_view>allowed){if(auto value=get(raw,key);value&&in(*value,allowed))out.emplace(key,*value);};
    const auto number=[&](std::string_view key,std::int64_t maximum){if(auto value=get(raw,key);value&&value->size()<=12&&!value->empty()){auto text=*value;if(text.front()=='+')text.remove_prefix(1);std::int64_t parsed{};const auto result=std::from_chars(text.data(),text.data()+text.size(),parsed);if(result.ec==std::errc{}&&result.ptr==text.data()+text.size()&&parsed>=0&&parsed<=maximum)out.emplace(key,std::to_string(parsed));}};
    const auto name=[&](std::string_view key,bool basename){auto value=get(raw,key);if(!value||value->size()>4096||!ehud::data::Json::validUtf8(*value)||value->find("://")!=std::string_view::npos)return;std::string storage(*value);if(basename){std::replace(storage.begin(),storage.end(),'\\','/');while(!storage.empty()&&storage.back()=='/')storage.pop_back();const auto slash=storage.find_last_of('/');if(slash!=std::string::npos)storage.erase(0,slash+1);}else if(storage.find_first_of("/\\")!=std::string::npos)return;auto compact=compactName(storage,compactor);if(!compact.empty())out.emplace(key,std::move(compact));};
    switch(kind){
    case EventKind::overlayOpened:case EventKind::mapRecentered:break;
    case EventKind::moduleOpened:if(auto module=get(raw,"module");module&&!moduleTitle(*module).empty())out.emplace("module",*module);break;
    case EventKind::appShortcutOpened:name("app",false);break;
    case EventKind::clipboardCopied:one("kind",{"text","url","image","files"});break;
    case EventKind::shelfAdded:case EventKind::shelfRemoved:name("filename",true);break;
    case EventKind::shelfCleared:number("count",1000000);break;
    case EventKind::workStarted:case EventKind::workPaused:case EventKind::workResumed:case EventKind::workReset:case EventKind::workCompleted:one("kind",{"countdown","stopwatch"});number("seconds",31536000);break;
    case EventKind::powerConnected:case EventKind::powerDisconnected:case EventKind::batteryStateChanged:one("state",{"charging","full","connected","battery","noBattery","unavailable"});number("percentage",100);break;
    case EventKind::audioDeviceConnected:case EventKind::audioDeviceDisconnected:case EventKind::displayConnected:case EventKind::displayDisconnected:name("device",false);break;
    case EventKind::displaySettingsChanged:{const auto field=get(raw,"field"),value=get(raw,"value");if(field&&value&&((*field=="clockStyle"&&in(*value,{"digital","split","dial","rail","stacked"}))||(*field=="centerLogo"&&in(*value,{"endfield","rhodesIsland","babel","rhineLab","custom","customImported"}))||(*field=="alertMetric"&&in(*value,{"battery","ram","cpu","network","disk","gpu"})))){out.emplace("field",*field);out.emplace("value",*value);}break;}
    case EventKind::profileCropChanged:one("target",{"background","thumbnail","both"});break;
    case EventKind::mapPinStyleChanged:one("style",{"yellow","green","player"});break;
    case EventKind::noteAction:one("action",{"createdText","createdTODO","createdMedia","createdDrawing","editedText","formattedText","editedTODO","drawingEdited","deletedNote","mediaPlayback"});break;
    case EventKind::playbackAction:one("action",{"playPause","previous","next","seek"});one("source",{"music","spotify","netease","qqMusic","kugou","system"});break;
    case EventKind::archiveAction:one("action",{"created","edited","deleted","mediaAdded","mediaRemoved","categoryCreated","categoryChanged","categoryDeleted"});break;
    case EventKind::readerAction:one("action",{"imported","deleted","bookmarked","progress","settings"});break;
    case EventKind::mediaAssemblyAction:one("action",{"imported","edited","exported"});break;
    case EventKind::calendarAction:one("action",{"created","edited","deleted"});break;
    case EventKind::minigameAction:one("action",{"started","restarted","finished"});break;
    case EventKind::accountAction:one("action",{"linked","unlinked","synced","settings"});break;
    case EventKind::projectionAction:one("action",{"drawingEdited","erased","brushChanged","backgroundChanged","mediaAdded","mediaRemoved","cleared"});break;
    }return out;
}
std::string eventDetail(const SystemEvent&event,const EventNameCompactor&compactor){
    const auto metadata=sanitizedEventMetadata(event.kind,event.metadata,compactor);std::vector<std::string>fields;fields.reserve(4);
    const auto append=[&](std::string_view key,std::initializer_list<std::pair<std::string_view,std::string_view>>values){if(auto value=get(metadata,key))for(const auto&[from,to]:values)if(*value==from){fields.emplace_back(to);break;}};
    if(auto value=get(metadata,"module")){auto title=moduleTitle(*value);if(!title.empty())fields.emplace_back(title);}for(auto key:{"app","filename","device"})if(auto value=get(metadata,key))fields.emplace_back(*value);
    append("kind",{{"text","Text"},{"url","Link"},{"image","Image"},{"files","Files"},{"countdown","Countdown"},{"stopwatch","Stopwatch"}});
    if(auto value=get(metadata,"seconds")){unsigned seconds{};std::from_chars(value->data(),value->data()+value->size(),seconds);char buffer[32];std::snprintf(buffer,sizeof(buffer),"%02u:%02u:%02u",seconds/3600,(seconds/60)%60,seconds%60);fields.emplace_back(buffer);}
    append("state",{{"charging","Charging"},{"full","Full"},{"connected","Connected"},{"battery","On battery"},{"noBattery","No battery"},{"unavailable","Unavailable"}});
    if(auto value=get(metadata,"percentage"))fields.emplace_back(std::string(*value)+"%");if(auto value=get(metadata,"count"))fields.emplace_back(std::string(*value)+" items");
    if(event.kind==EventKind::displaySettingsChanged){append("field",{{"clockStyle","Clock style"},{"centerLogo","Center logo"},{"alertMetric","Charge metric"}});append("value",{{"digital","Digital"},{"split","Split"},{"dial","Dial"},{"rail","Rail"},{"stacked","Stacked"},{"endfield","Endfield"},{"rhodesIsland","Rhodes Island"},{"babel","Babel"},{"rhineLab","Rhine Lab"},{"custom","Custom"},{"customImported","Custom artwork imported"},{"battery","Battery"},{"ram","RAM"},{"cpu","CPU"},{"network","Network"},{"disk","Disk"},{"gpu","GPU"}});}
    if(event.kind==EventKind::profileCropChanged)append("target",{{"background","Background"},{"thumbnail","Thumbnail"},{"both","Background and thumbnail"}});
    if(event.kind==EventKind::mapPinStyleChanged)append("style",{{"yellow","Yellow"},{"green","Green"},{"player","Player"}});
    if(event.kind==EventKind::projectionAction)append("action",{{"drawingEdited","Drawing edited"},{"erased","Drawing erased"},{"brushChanged","Brush changed"},{"backgroundChanged","Background changed"},{"mediaAdded","Media added"},{"mediaRemoved","Media removed"},{"cleared","Content cleared"}});
    if(event.kind==EventKind::archiveAction||event.kind==EventKind::readerAction)append("action",{{"created","Document created"},{"edited","Document edited"},{"deleted","Document deleted"},{"mediaAdded","Media added"},{"mediaRemoved","Media removed"},{"imported","Document imported"},{"bookmarked","Bookmark changed"},{"progress","Reading progress changed"},{"settings","Reading settings changed"},{"categoryCreated","Category created"},{"categoryChanged","Category changed"},{"categoryDeleted","Category deleted"}});
    if(event.kind==EventKind::mediaAssemblyAction)append("action",{{"imported","Media imported"},{"edited","Media changed"},{"exported","Media exported"}});
    if(event.kind==EventKind::minigameAction)append("action",{{"started","Game started"},{"restarted","Game restarted"},{"finished","Game finished"}});
    if(event.kind==EventKind::accountAction)append("action",{{"linked","Account linked"},{"unlinked","Account unlinked"},{"synced","Profile refreshed"},{"settings","Account settings changed"}});
    if(event.kind==EventKind::calendarAction)append("action",{{"created","Event created"},{"edited","Event edited"},{"deleted","Event deleted"}});
    if(event.kind!=EventKind::projectionAction)append("action",{{"createdText","Text added"},{"createdTODO","Checklist added"},{"createdMedia","Media added"},{"createdDrawing","Drawing added"},{"editedText","Text edited"},{"formattedText","Text formatted"},{"editedTODO","Checklist edited"},{"drawingEdited","Drawing edited"},{"deletedNote","Note deleted"},{"mediaPlayback","Media playback"},{"playPause","Play / pause"},{"previous","Previous track"},{"next","Next track"},{"seek","Seek"}});
    append("source",{{"music","Music"},{"spotify","Spotify"},{"netease","NetEase Music"},{"qqMusic","QQ Music"},{"kugou","Kugou"},{"system","Now Playing"}});
    std::string out;for(const auto&field:fields){if(!out.empty())out+=" · ";out+=field;}return out;
}
EventLog::EventLog(std::size_t capacity,EventNameCompactor compactor):capacity_(std::clamp(capacity,std::size_t(1),maximumCapacity)),compactor_(std::move(compactor)){events_.reserve(capacity_+1);}
void EventLog::record(std::string id,EventKind kind,double time,const EventMetadata&raw){need(std::isfinite(time),"Nonfinite event timestamp");SystemEvent next{canonicalID(id),kind,time,sanitizedEventMetadata(kind,raw,compactor_)};need(std::none_of(events_.begin(),events_.end(),[&](const auto&e){return e.id==next.id;}),"Duplicate caller event identity");events_.insert(events_.begin(),std::move(next));if(events_.size()>capacity_)events_.resize(capacity_);++revision_;}
void EventLog::replace(std::span<const SystemEvent>input){need(input.size()<=maximumCapacity,"Incoming event snapshot exceeds source capacity");std::vector<SystemEvent>next;next.reserve(std::min(capacity_,input.size()));std::set<std::string>seen;for(const auto&e:input){if(!std::isfinite(e.createdAt))continue;auto id=canonicalID(e.id);if(!seen.insert(id).second)continue;next.push_back({std::move(id),e.kind,e.createdAt,sanitizedEventMetadata(e.kind,e.metadata,compactor_)});}std::stable_sort(next.begin(),next.end(),[](const auto&a,const auto&b){return a.createdAt>b.createdAt;});if(next.size()>capacity_)next.resize(capacity_);events_=std::move(next);++revision_;}
void EventLog::clear(){events_.clear();++revision_;}
} // namespace endfield::modules
