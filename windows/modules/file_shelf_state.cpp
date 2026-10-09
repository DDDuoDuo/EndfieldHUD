#include "modules/file_shelf_state.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::modules {
namespace {
struct Call {bool& flag;explicit Call(bool& value):flag(value){if(flag)throw std::logic_error("Reentrant FileShelfState callback");flag=true;}~Call(){flag=false;}};
void text(std::string_view s){if(!ehud::data::Json::validUtf8(s))throw std::invalid_argument("Invalid shelf UTF-8");}
std::string countText(std::string pattern,std::size_t count){const auto p=pattern.find("{count}");if(p==std::string::npos)throw std::invalid_argument("Shelf count format needs {count}");pattern.replace(p,7,std::to_string(count));return pattern;}
void validate(const FileShelfStrings&s){
    for(const auto* value:{&s.add,&s.clear,&s.cancel,&s.confirmClear,&s.clearQuestion,&s.unavailable,&s.previewPrefix,&s.revealPrefix,&s.removePrefix,&s.storageUnavailable,&s.drop,&s.emptyStatus,&s.selectedStatus,&s.itemsStatus,&s.folder,&s.image,&s.video,&s.archive,&s.file})text(*value);
    (void)countText(s.selectedStatus,0);(void)countText(s.itemsStatus,0);
}
}
FileShelfStrings FileShelfStrings::simplifiedChinese(){
    FileShelfStrings s;s.add="添加文件";s.clear="清空";s.cancel="取消";s.confirmClear="清空暂存架";s.clearQuestion="仅清空文件引用？";s.unavailable="不可用";
    s.previewPrefix="快速查看：";s.revealPrefix="在访达中显示：";s.removePrefix="从暂存架移除：";s.storageUnavailable="文件暂存架存储不可用。";
    s.drop="松开以暂存文件引用";s.emptyStatus="文件保留在原位置";s.selectedStatus="已选 {count} 项 · 拖出以复制";s.itemsStatus="{count} 项 · Shift 点击多选";
    s.folder="文件夹";s.image="图像";s.video="视频";s.archive="压缩文件";s.file="文件";return s;
}
FileShelfState::FileShelfState(std::vector<Item> initial,Store store,PlatformActions platform,FileShelfStrings strings,std::optional<std::string> error)
:store_(std::move(store)),platform_(std::move(platform)),strings_(std::move(strings)),error_(std::move(error)){
    const unsigned functions=unsigned(bool(store_.snapshot))+unsigned(bool(store_.refresh))+unsigned(bool(store_.add))+unsigned(bool(store_.remove))+unsigned(bool(store_.clear));
    if(functions!=0&&functions!=5)throw std::invalid_argument("Shelf store requires all operations");
    validate(strings_);if(error_)text(*error_);load(std::move(initial));revision_=1;
}
bool FileShelfState::setStrings(FileShelfStrings value){writable();if(value==strings_)return false;validate(value);strings_=std::move(value);++revision_;return true;}
void FileShelfState::writable()const{if(busy_)throw std::logic_error("Reentrant FileShelfState mutation");}
void FileShelfState::event(EventKind kind,bool animated,int direction,std::vector<std::string> values,std::size_t count){events_.push_back({kind,animated,direction,count,std::move(values)});}
std::vector<FileShelfState::Event> FileShelfState::takeEvents(){writable();std::vector<Event> result;result.swap(events_);return result;}
const FileShelfState::Item* FileShelfState::item(std::string_view id)const noexcept{const auto i=indices_.find(id);return i==indices_.end()?nullptr:&items_[i->second];}
double FileShelfState::maximumOffset()const noexcept{return std::max(0.0,double(items_.size()/2+items_.size()%2)*80-248);}
FileShelfState::VisibleRange FileShelfState::visibleRange()const noexcept{
    const auto first=std::min(items_.size(),std::size_t(std::floor(scroll_/80))*2);
    const auto last=std::min(items_.size(),std::size_t(std::ceil((scroll_+248)/80))*2);return {first,std::max(first,last)};
}
bool FileShelfState::contains(Rect r,Point p)noexcept{return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
FileShelfState::Rect FileShelfState::fullCard(std::size_t i)const noexcept{return {12+double(i%2)*192,44+double(i/2)*80-scroll_,184,74};}
std::optional<FileShelfState::Rect> FileShelfState::clipped(Rect r)noexcept{
    const auto clip=contentRect();const double x=std::max(r.x,clip.x),y=std::max(r.y,clip.y),right=std::min(r.x+r.width,clip.x+clip.width),bottom=std::min(r.y+r.height,clip.y+clip.height);
    if(right<=x||bottom-y<2)return {};return Rect{x,y,right-x,bottom-y};
}
std::optional<FileShelfState::Card> FileShelfState::card(std::string_view id)const noexcept{
    const auto i=indices_.find(id);if(i==indices_.end())return {};const auto full=fullCard(i->second);const auto clip=clipped(full);if(!clip)return {};
    const bool selected=selectedIDs_.contains(id),available=!items_[i->second].availabilityError;
    return Card{full,*clip,selected,available,selected?-1.5:0,selected?5.0:0,selected?1.5:.6,available?1.0:.45};
}
std::optional<FileShelfState::Rect> FileShelfState::scrollIndicator()const noexcept{
    const double maximum=maximumOffset();if(maximum==0)return {};const double height=std::max(24.0,248.0*248/(maximum+248));return Rect{394,40+(248-height)*scroll_/maximum,2,height};
}
FileShelfState::Style FileShelfState::style(bool dark)noexcept{
    Style s{};s.cardWhite=dark?.76:.89;s.unselectedBorderWhite=dark?.88:.39;s.primary=dark?.94:.11;s.muted=dark?.68:.38;s.toolbarWhite=dark?.82:.9;s.toolbarBorderWhite=dark?.93:.38;return s;
}
std::array<FileShelfState::Point,6> FileShelfState::cutCorner(Rect r,double c)noexcept{return {{{r.x+c,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height-c},{r.x+r.width-c,r.y+r.height},{r.x,r.y+r.height},{r.x,r.y+c}}};}
void FileShelfState::load(std::vector<Item> next,std::optional<std::string_view> revealing){
    Indices indices;for(std::size_t i=0;i<next.size();++i){const auto& n=next[i];
        for(const auto* s:{&n.id,&n.name,&n.lastKnownPath,&n.typeDescription})text(*s);
        if(n.availabilityError)text(*n.availabilityError);
        if(n.id.empty()||n.id.find(':')!=std::string::npos||n.name.empty()||(n.byteCount&&*n.byteCount<0)||!indices.emplace(n.id,i).second)throw std::invalid_argument("Invalid or duplicate shelf metadata");
    }
    const auto oldIndex=std::size_t(std::floor(scroll_/80))*2;
    const auto oldAnchor=scroll_>0&&oldIndex<items_.size()?std::optional(items_[oldIndex].id):std::nullopt;
    const double remainder=std::fmod(scroll_,80);
    // Reveal may refer to selected_ and must survive replacing selection below.
    const auto revealID=revealing?std::optional(std::string(*revealing)):std::nullopt;
    items_=std::move(next);indices_=std::move(indices);
    std::erase_if(selectedIDs_,[&](const auto& id){return !indices_.contains(id);});
    if(selected_&&!indices_.contains(*selected_))selected_.reset();
    if(!selected_)for(const auto& n:items_)if(selectedIDs_.contains(n.id)){selected_=n.id;break;}
    if(anchor_&&!indices_.contains(*anchor_))anchor_=selected_;pending_.reset();
    if(oldAnchor){const auto i=indices_.find(*oldAnchor);if(i!=indices_.end())scroll_=double(i->second/2)*80+remainder;}
    scroll_=std::clamp(scroll_,0.0,maximumOffset());if(revealID){const auto i=indices_.find(*revealID);if(i!=indices_.end())revealRow(i->second);}
    if(items_.empty())confirmingClear_=false;++revision_;
}
void FileShelfState::refreshFromStore(std::optional<std::string_view> revealing){writable();std::vector<Item> next;if(store_.snapshot){Call c(busy_);next=store_.snapshot();}load(std::move(next),revealing);}
void FileShelfState::setReduceMotion(bool value){writable();if(reduceMotion_==value)return;reduceMotion_=value;if(value)event(EventKind::settle);}
void FileShelfState::activate(){writable();active_=true;if(!store_.refresh)return;try{{Call c(busy_);store_.refresh();}}catch(const std::exception& e){showError(e.what());}refreshFromStore();}
void FileShelfState::deactivate(){writable();active_=false;pending_.reset();event(EventKind::settle);if(confirmingClear_||drop_){confirmingClear_=false;drop_=false;++revision_;}}
bool FileShelfState::revealRow(std::size_t i){const auto before=scroll_;const double top=double(i/2)*80;if(top<scroll_)scroll_=top;else if(top+80>scroll_+248)scroll_=top+80-248;scroll_=std::clamp(scroll_,0.0,maximumOffset());return before!=scroll_;}
void FileShelfState::setScrollOffset(double value){const auto next=std::clamp(value,0.0,maximumOffset());if(next==scroll_)return;scroll_=next;pending_.reset();confirmingClear_=false;++revision_;event(EventKind::settle);}
bool FileShelfState::scroll(Point p,double delta){writable();if(!contains(contentRect(),p)||!std::isfinite(delta))return false;setScrollOffset(scroll_+delta);return true;}
void FileShelfState::scrollBy(double delta){writable();if(std::isfinite(delta))setScrollOffset(scroll_+delta);}
void FileShelfState::select(std::optional<std::string_view> id,Modifiers modifiers,bool preserve,bool revealing){
    if(id&&!indices_.contains(*id))return;
    const auto previous=selectedIDs_;const auto primary=selected_;const bool confirmation=confirmingClear_;
    const bool moved=revealing&&id&&revealRow(indices_.find(*id)->second);
    if(id){
        if(modifiers.shift&&anchor_&&indices_.contains(*anchor_)){
            const auto a=indices_.find(*anchor_)->second,b=indices_.find(*id)->second;selectedIDs_.clear();for(auto i=std::min(a,b);i<=std::max(a,b);++i)selectedIDs_.insert(items_[i].id);selected_=*id;
        }else if(modifiers.toggle){
            const auto found=selectedIDs_.find(*id);if(found!=selectedIDs_.end())selectedIDs_.erase(found);else selectedIDs_.emplace(*id);
            selected_.reset();if(selectedIDs_.contains(*id))selected_=*id;else for(const auto& n:items_)if(selectedIDs_.contains(n.id)){selected_=n.id;break;}anchor_=selected_;
        }else if(preserve&&selectedIDs_.contains(*id)&&selectedIDs_.size()>1){selected_=*id;pending_=*id;}
        else{selectedIDs_.clear();selectedIDs_.emplace(*id);selected_=*id;anchor_=selected_;}
    }else{selectedIDs_.clear();selected_.reset();anchor_.reset();}
    confirmingClear_=false;
    if(previous==selectedIDs_&&primary==selected_&&!confirmation&&!moved)return;
    ++revision_;std::vector<std::string> changed;const auto range=visibleRange();for(auto i=range.begin;i<range.end;++i)if(previous.contains(items_[i].id)!=selectedIDs_.contains(items_[i].id))changed.push_back(items_[i].id);
    if(!changed.empty())event(EventKind::selection,active_&&!reduceMotion_,0,std::move(changed));
}
void FileShelfState::selectItem(std::optional<std::string_view> id,Modifiers modifiers,bool preserve,bool revealing){writable();select(id,modifiers,preserve,revealing);}
void FileShelfState::selectNext(int direction,bool extending){writable();if(items_.empty())return;pending_.reset();
    std::size_t index=direction<0?items_.size()-1:0;
    if(selected_){const auto current=indices_.find(*selected_)->second;const auto delta=std::int64_t(direction);if(delta<0)index=current<std::uint64_t(-delta)?0:current-std::size_t(-delta);else index=current+std::min(std::size_t(delta),items_.size()-1-current);}
    select(items_[index].id,{extending,false},false,true);
}
void FileShelfState::finishPointerSelection(){writable();if(!pending_)return;const auto id=std::move(*pending_);pending_.reset();select(id,{});}
void FileShelfState::beginSelectionDrag(){writable();pending_.reset();}
std::optional<std::string_view> FileShelfState::itemAt(Point p)const noexcept{
    if(!contains(contentRect(),p))return {};const auto range=visibleRange();for(auto i=range.begin;i<range.end;++i){const auto& n=items_[i];if(n.availabilityError)continue;
        const auto c=card(n.id);if(c&&contains(c->clipped,p)&&!contains({c->full.x+111,c->full.y+47,71,26},p))return n.id;}return {};
}
std::vector<std::string> FileShelfState::dragSelection(std::string_view primary)const{
    std::vector<std::string> out;const bool multiple=selectedIDs_.contains(primary);for(const auto& n:items_)if(!n.availabilityError&&(multiple?selectedIDs_.contains(n.id):n.id==primary))out.push_back(n.id);return out;
}
void FileShelfState::setDropTarget(bool value){writable();if(drop_==value)return;drop_=value;++revision_;event(EventKind::dropTrace,active_&&!reduceMotion_&&value);}
void FileShelfState::showError(std::string value){writable();text(value);error_=std::move(value);++revision_;}
void FileShelfState::request(const std::function<void(std::string_view)>& callback,std::string_view id){if(!callback)return;const std::string stable(id);try{Call c(busy_);callback(stable);}catch(const std::exception& e){showError(e.what());}}
void FileShelfState::previewSelection(){writable();if(selected_){const auto* n=item(*selected_);if(n&&!n->availabilityError)request(platform_.preview,n->id);}}
void FileShelfState::revealSelection(){writable();if(selected_){const auto* n=item(*selected_);if(n&&!n->availabilityError)request(platform_.reveal,n->id);}}
void FileShelfState::revealItems(std::span<const std::string> ids){writable();std::set<std::string,std::less<>> selected;std::optional<std::size_t> last;
    for(std::size_t i=0;i<items_.size();++i)if(std::find(ids.begin(),ids.end(),items_[i].id)!=ids.end()){selected.insert(items_[i].id);last=i;}
    if(!last)return;const bool moved=revealRow(*last);if(selectedIDs_==selected&&selected_==items_[*last].id&&!moved)return;
    selectedIDs_=std::move(selected);selected_=items_[*last].id;anchor_=selected_;pending_.reset();++revision_;
}
bool FileShelfState::importFiles(std::span<const std::string> tokens){writable();if(!store_.add){showError(strings_.storageUnavailable);return false;}if(tokens.empty())return false;
    try{
        std::vector<Item> before,after;std::size_t count{};{Call c(busy_);before=store_.snapshot();count=store_.add(tokens);after=store_.snapshot();}
        std::set<std::string,std::less<>> previous;for(const auto& n:before)previous.insert(n.id);
        std::vector<std::string> names;std::set<std::string,std::less<>> added;for(const auto& n:after)if(!previous.contains(n.id)){names.push_back(n.name);added.insert(n.id);}
        // Validate the complete snapshot before publishing new selection.
        load(std::move(after));error_.reset();confirmingClear_=false;
        if(count>0){selected_=items_.empty()?std::nullopt:std::optional(items_.back().id);selectedIDs_=std::move(added);anchor_=selected_;if(selected_)revealRow(indices_.find(*selected_)->second);}
        if(!names.empty()){event(EventKind::collectionReveal,active_&&!reduceMotion_,1);event(EventKind::itemsAdded,false,0,std::move(names));}return true;
    }catch(const std::exception& e){const std::string message=e.what();refreshFromStore();showError(message);return false;}
}
void FileShelfState::remove(std::span<const std::string> ids){if(!store_.remove){showError(strings_.storageUnavailable);return;}
    try{
        for(const auto& id:ids){std::optional<std::string> name;{Call c(busy_);const auto before=store_.snapshot();for(const auto& n:before)if(n.id==id){name=n.name;break;}store_.remove(id);}if(name)event(EventKind::itemRemoved,false,0,{*name});}
        error_.reset();refreshFromStore();event(EventKind::collectionReveal,active_&&!reduceMotion_,-1);
    }catch(const std::exception& e){const std::string message=e.what();refreshFromStore();showError(message);}
}
void FileShelfState::deleteSelection(){writable();std::vector<std::string> ids;for(const auto& n:items_)if(selectedIDs_.contains(n.id))ids.push_back(n.id);if(!ids.empty())remove(ids);}
std::string FileShelfState::actionID(std::string_view id,std::string_view verb)const{return "shelf:"+std::string(id)+":"+std::string(verb);}
std::vector<FileShelfState::Action> FileShelfState::toolbarActions()const{
    if(confirmingClear_)return {{"shelf:cancelClear",strings_.cancel,{218,299,67,27}},{"shelf:confirmClear",strings_.confirmClear,{294,299,94,27}}};
    std::vector<Action> out{{"shelf:add",strings_.add,{12,299,84,27}}};if(!items_.empty())out.push_back({"shelf:clear",strings_.clear,{104,299,74,27}});return out;
}
std::vector<FileShelfState::Action> FileShelfState::cardActions(std::string_view id,bool clip)const{
    const auto found=indices_.find(id);if(found==indices_.end())return {};const auto& n=items_[found->second];const auto r=fullCard(found->second);std::vector<Action> out;
    const auto add=[&](std::string_view verb,const std::string& prefix,double x){Rect rect{r.x+x,r.y+49,20,20};const auto visible=clip?clipped(rect):std::optional(rect);if(visible)out.push_back({actionID(id,verb),prefix+n.name,*visible});};
    if(!n.availabilityError){add("preview",strings_.previewPrefix,113);add("reveal",strings_.revealPrefix,136);}add("remove",strings_.removePrefix,159);return out;
}
std::vector<FileShelfState::Action> FileShelfState::accessibleActions()const{
    auto out=toolbarActions();const auto range=visibleRange();for(auto i=range.begin;i<range.end;++i){const auto& n=items_[i];if(const auto c=card(n.id)){
        std::string details=n.name+", "+typeLabel(n)+", "+sizeLabel(n);if(n.availabilityError)details+=", "+strings_.unavailable;
        out.push_back({actionID(n.id,"select"),std::move(details),c->clipped});auto actions=cardActions(n.id);for(auto& a:actions)out.push_back(std::move(a));}}return out;
}
void FileShelfState::perform(std::string_view action){writable();
    if(action=="shelf:add"){if(!store_.snapshot){showError(strings_.storageUnavailable);return;}if(platform_.chooseFiles)try{Call c(busy_);platform_.chooseFiles();}catch(const std::exception& e){showError(e.what());}return;}
    if(action=="shelf:clear"){if(items_.empty())return;confirmingClear_=true;++revision_;event(EventKind::toolbar,active_&&!reduceMotion_);return;}
    if(action=="shelf:cancelClear"){confirmingClear_=false;++revision_;event(EventKind::toolbar,active_&&!reduceMotion_);return;}
    if(action=="shelf:confirmClear"){
        if(!confirmingClear_||!store_.clear)return;try{std::size_t count;{Call c(busy_);count=store_.snapshot().size();store_.clear();}
            error_.reset();selected_.reset();selectedIDs_.clear();anchor_.reset();confirmingClear_=false;refreshFromStore();event(EventKind::collectionReveal,active_&&!reduceMotion_,-1);if(count)event(EventKind::shelfCleared,false,0,{},count);
        }catch(const std::exception& e){showError(e.what());}return;
    }
    if(!action.starts_with("shelf:"))return;const auto tail=action.substr(6);const auto separator=tail.find(':');if(separator==std::string_view::npos)return;
    const auto id=tail.substr(0,separator),verb=tail.substr(separator+1);const auto* n=item(id);if(!n)return;
    if(verb=="select")select(id,{},false,true);
    else if(verb=="preview"||verb=="reveal"){if(n->availabilityError)return;const std::string stable(id);select(stable,{});request(verb=="preview"?platform_.preview:platform_.reveal,stable);}
    else if(verb=="remove"){const std::array<std::string,1> ids{std::string(id)};remove(ids);}
}
bool FileShelfState::mouseDown(Point p,int clicks,Modifiers modifiers){writable();if(!contains(bounds(),p))return false;pending_.reset();
    for(const auto& a:toolbarActions())if(contains(a.rect,p)){perform(a.id);return true;}
    const auto range=visibleRange();for(auto i=range.begin;i<range.end;++i){const auto c=card(items_[i].id);if(!c||!contains(c->clipped,p))continue;
        for(const auto& a:cardActions(items_[i].id))if(contains(a.rect,p)){perform(a.id);return true;}
        const auto id=items_[i].id;const bool available=!items_[i].availabilityError;select(id,modifiers,clicks==1);if(clicks>=2&&available)request(platform_.preview,id);return true;
    }
    if(contains(contentRect(),p))select({},{});return true;
}
std::string FileShelfState::statusText()const{if(drop_)return strings_.drop;if(error_)return *error_;if(items_.empty())return strings_.emptyStatus;return countText(selectedIDs_.size()>1?strings_.selectedStatus:strings_.itemsStatus,selectedIDs_.size()>1?selectedIDs_.size():items_.size());}
std::string FileShelfState::sizeLabel(const Item& n)const{if(n.isDirectory||!n.byteCount)return "—";if(!platform_.formatFileSize)throw std::logic_error("Shelf file-size formatter is required");Call c(busy_);return platform_.formatFileSize(*n.byteCount);}
std::string FileShelfState::typeLabel(const Item& n)const{
    auto path=std::string_view(n.lastKnownPath);while(path.size()>1&&(path.back()=='/'||path.back()=='\\'))path.remove_suffix(1);const auto slash=path.find_last_of("/\\");if(slash!=std::string_view::npos)path.remove_prefix(slash+1);const auto dot=path.find_last_of('.');std::string ext=dot==std::string_view::npos||dot==0?"":std::string(path.substr(dot+1));
    for(auto& c:ext)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    const auto oneOf=[&](std::initializer_list<std::string_view> list){return std::find(list.begin(),list.end(),ext)!=list.end();};
    if(n.isDirectory){if(oneOf({"app","bundle","framework","plugin","pages","numbers","key","rtfd","playground","xcodeproj","xcworkspace"})&&!n.typeDescription.empty())return n.typeDescription;return strings_.folder;}
    std::string prefix;if(oneOf({"jpg","jpeg","png","heic","heif","gif","tif","tiff","bmp","webp","svg"}))prefix=strings_.image;
    else if(ext=="pdf")return "PDF";
    else if(oneOf({"mov","mp4","m4v","avi","mkv","webm"}))prefix=strings_.video;
    else if(oneOf({"zip","rar","7z","tar","gz","bz2","xz","tgz"}))prefix=strings_.archive;
    else return n.typeDescription.empty()?strings_.file:n.typeDescription;
    for(auto& c:ext)if(c>='a'&&c<='z')c=char(c-'a'+'A');return prefix+" · "+ext;
}
} // namespace endfield::modules
