#include "modules/notes_state.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::modules {
std::optional<core::notes::RichText> decodeNotesRichText(std::string_view text,const std::optional<std::string>& payload){
    if(text.size()>16*1024*1024||!ehud::data::Json::validUtf8(text))throw std::invalid_argument("Invalid or oversized Notes UTF-8 text");
    if(!payload)return std::nullopt;
    std::u16string units;units.reserve(text.size());
    for(std::size_t at=0;at<text.size();){const auto first=static_cast<unsigned char>(text[at++]);std::uint32_t cp=first;unsigned more{};
        if(first>=0xf0){cp=first&7;more=3;}else if(first>=0xe0){cp=first&15;more=2;}else if(first>=0xc0){cp=first&31;more=1;}
        while(more--)cp=(cp<<6)|(static_cast<unsigned char>(text[at++])&63);
        if(cp<0x10000)units.push_back(static_cast<char16_t>(cp));else{cp-=0x10000;units.push_back(static_cast<char16_t>(0xd800+(cp>>10)));units.push_back(static_cast<char16_t>(0xdc00+(cp&1023)));}}
    return core::notes::decodeRichText(ehud::data::Json::parse(*payload,16*1024*1024),units);
}
namespace {
bool finite(core::Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
bool valid(core::Rect r){return finite({r.x,r.y})&&std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>0&&r.height>0&&std::isfinite(r.x+r.width)&&std::isfinite(r.y+r.height);}
core::Rect geometry(ehud::data::NoteKind kind,core::Rect input,std::optional<core::Rect> bounds){
    // NotesStore.swift:53-73. Minimum dimensions are applied before the
    // workspace cap, so a tiny workspace can be smaller than those minima.
    core::Rect r{std::clamp(std::isfinite(input.x)?input.x:24.,-1'000'000.,1'000'000.),
        std::clamp(std::isfinite(input.y)?input.y:50.,-1'000'000.,1'000'000.),
        std::clamp(std::isfinite(input.width)?input.width:160.,kind==ehud::data::NoteKind::todo?180.:110.,32768.),
        std::clamp(std::isfinite(input.height)?input.height:110.,kind==ehud::data::NoteKind::todo?105.:70.,32768.)};
    if(bounds&&valid(*bounds)){
        r.width=std::min(bounds->width,r.width);r.height=std::min(bounds->height,r.height);
        r.x=std::min(bounds->x+bounds->width-r.width,std::max(bounds->x,r.x));
        r.y=std::min(bounds->y+bounds->height-r.height,std::max(bounds->y,r.y));
    }
    return r;
}
core::Rect geometry(const ehud::data::Note& n,std::optional<core::Rect> bounds){return geometry(n.kind,{n.x,n.y,n.width,n.height},bounds);}
// CGRect.contains includes the minimum edges and excludes the maximum edges.
bool contains(core::Rect r,core::Point p){return r.width>0&&r.height>0&&p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
struct Persisting {bool& flag;explicit Persisting(bool& v):flag(v){flag=true;}~Persisting(){flag=false;}};
}
NotesState::NotesState(std::vector<Note> initial,Persistence persistence,bool selected)
    :notes_(std::move(initial)),persistence_(std::move(persistence)),notesSelected_(selected){
    if(!persistence_.upsert||!persistence_.remove)throw std::invalid_argument("Notes require explicit persistence adapters");
    if(notes_.size()>10000)throw std::invalid_argument("Notes record bound exceeded");
    std::set<std::string,std::less<>> ids;
    for(auto& item:notes_){
        if(!ehud::data::validUUID(item.id)||!ids.insert(item.id).second||!std::isfinite(item.createdAt))throw std::invalid_argument("Invalid initial note identity/time");
        item=constrained(std::move(item));
    }
}
void NotesState::writable()const {if(persisting_)throw std::logic_error("Notes persistence must not reenter the controller");}
const NotesState::Note* NotesState::note(std::string_view id)const noexcept {for(const auto& n:notes_)if(n.id==id)return &n;return nullptr;}
NotesState::Note* NotesState::find(std::string_view id)noexcept {for(auto& n:notes_)if(n.id==id)return &n;return nullptr;}
NotesState::Note NotesState::constrained(Note item,std::optional<Rect> bounds){const auto r=geometry(item,bounds);item.x=r.x;item.y=r.y;item.width=r.width;item.height=r.height;return item;}
bool NotesState::visible(std::string_view id)const noexcept {const auto* n=note(id);return n&&(notesSelected_||n->isPinned);}
std::optional<NotesState::Card> NotesState::card(std::string_view id)const noexcept {
    const auto* n=note(id);if(!n)return {};const auto r=geometry(*n,workspace_);
    return Card{r,{r.x-workspace_.x,r.y-workspace_.y,r.width,r.height},{r.x+r.width-15,r.y+r.height-15,15,15},
        notesSelected_||n->isPinned,selected_&&*selected_==id,n->isPinned,double(n->zIndex)+1};
}
std::optional<std::string_view> NotesState::topNote(Point point)const noexcept {
    if(!finite(point))return {};const Note* top{};
    for(const auto& n:notes_)if((notesSelected_||n.isPinned)&&contains(geometry(n,workspace_),point)&&(!top||n.zIndex>top->zIndex))top=&n;
    if(top)return top->id;return {};
}
bool NotesState::setWorkspaceProjection(const core::Projection& value){
    writable();if(!std::all_of(value.values.begin(),value.values.end(),[](double n){return std::isfinite(n);}))throw std::invalid_argument("Invalid Notes workspace projection");
    if(projection_.values==value.values)return false;projection_=value;++placementRevision_;return true;
}
bool NotesState::setWorkspaceBounds(Rect value,std::optional<Point> point){
    writable();if(!valid(value)||(point&&!finite(*point)))return false;
    if(workspace_==value&&creationPoint_==point)return false;
    // Host must finish active text input before changing its editor geometry.
    if(editing_)throw std::logic_error("Finish Notes editing before workspace resize");
    (void)endGesture();workspace_=value;creationPoint_=point;++revision_;return true;
}
void NotesState::setPresentation(bool selected){
    writable();if(notesSelected_==selected)return;
    (void)endGesture();pendingDeletion_.reset();notesSelected_=selected;
    if(selected_&&!visible(*selected_))selected_.reset();++revision_;
}
void NotesState::cancelInteraction(){writable();(void)endGesture();if(pendingDeletion_){pendingDeletion_.reset();++revision_;}}
void NotesState::replace(const Note& item){if(auto* n=find(item.id))*n=item;else notes_.push_back(item);}
bool NotesState::save(const Note& item){
    try{Persisting guard(persisting_);persistence_.upsert(item);unsaved_.erase(item.id);if(unsaved_.empty())error_.reset();return true;}
    catch(const std::exception& e){unsaved_.insert(item.id);error_=e.what();return false;}
    catch(...){unsaved_.insert(item.id);error_="Notes persistence failed";return false;}
}
std::int64_t NotesState::nextZ(){
    std::int64_t maximum=-1;if(!notes_.empty())maximum=std::max_element(notes_.begin(),notes_.end(),[](const auto& a,const auto& b){return a.zIndex<b.zIndex;})->zIndex;
    if(maximum!=std::numeric_limits<std::int64_t>::max())return maximum+1;
    std::vector<Note> ordered=notes_;
    std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.zIndex==b.zIndex?a.createdAt<b.createdAt:a.zIndex<b.zIndex;});
    for(std::size_t i=0;i<ordered.size();++i){auto& n=ordered[i];n.zIndex=static_cast<std::int64_t>(i);replace(n);(void)save(n);}
    return static_cast<std::int64_t>(notes_.size());
}
bool NotesState::select(std::optional<std::string> id){
    writable();if(id&&!visible(*id))return false;bool changed=selected_!=id;
    if(pendingDeletion_!=id&&pendingDeletion_){pendingDeletion_.reset();changed=true;}
    selected_=id;
    if(id){const auto* found=note(*id);auto item=*found;
        const auto maximum=std::max_element(notes_.begin(),notes_.end(),[](const auto& a,const auto& b){return a.zIndex<b.zIndex;})->zIndex;
        if(item.zIndex<maximum){item.zIndex=nextZ();replace(item);(void)save(item);changed=true;}
    }
    if(changed)++revision_;return changed;
}
bool NotesState::createText(std::string id,double createdAt){
    writable();if(!notesSelected_)return false;
    if(!ehud::data::validUUID(id)||note(id)||!std::isfinite(createdAt))throw std::invalid_argument("Invalid new note identity/time");
    if(notes_.size()>=10000)throw std::length_error("Notes record bound exceeded");
    Note item{.id=std::move(id),.kind=ehud::data::NoteKind::text,.createdAt=createdAt};
    const auto offset=double(notes_.size()%7)*18;const auto origin=creationPoint_.value_or(Point{workspace_.x+workspace_.width/2-95,workspace_.y+workspace_.height/2-60});
    item.x=origin.x+offset;item.y=origin.y+offset;item.width=210;item.height=140;item.zIndex=nextZ();item=constrained(std::move(item),workspace_);
    replace(item);(void)save(item);(void)select(item.id);(void)beginEditing(item.id);++revision_;return true;
}
bool NotesState::createChecklist(std::string id,std::string firstID,double createdAt){
    writable();if(!notesSelected_)return false;
    if(!ehud::data::validUUID(id)||note(id)||!ehud::data::validUUID(firstID)||!std::isfinite(createdAt))throw std::invalid_argument("Invalid new checklist identity/time");
    if(notes_.size()>=10000)throw std::length_error("Notes record bound exceeded");
    Note item{.id=std::move(id),.kind=ehud::data::NoteKind::todo,.createdAt=createdAt};item.items.push_back({std::move(firstID),"",false,ehud::data::Json::Object{}});
    const auto offset=double(notes_.size()%7)*18;const auto origin=creationPoint_.value_or(Point{workspace_.x+workspace_.width/2-95,workspace_.y+workspace_.height/2-60});
    item.x=origin.x+offset;item.y=origin.y+offset;item.width=228;item.height=154;item.zIndex=nextZ();item=constrained(std::move(item),workspace_);
    replace(item);(void)save(item);(void)select(item.id);++revision_;return true;
}
bool NotesState::addChecklistItem(std::string_view id,std::string childID){
    writable();const auto*found=note(id);if(!found||!visible(id)||found->kind!=ehud::data::NoteKind::todo)return false;
    if(editing_)throw std::logic_error("Finish Notes editor before changing checklist rows");
    if(!ehud::data::validUUID(childID)||std::any_of(found->items.begin(),found->items.end(),[&](const auto&i){return i.id==childID;}))throw std::invalid_argument("Invalid new checklist row identity");
    if(found->items.size()>=100000)throw std::length_error("Checklist row capacity reached");
    const std::string owned(id);(void)select(owned);auto item=*note(owned);item.items.push_back({std::move(childID),"",false,ehud::data::Json::Object{}});replace(item);(void)save(item);++revision_;return true;
}
bool NotesState::mutateChecklistItem(std::string_view id,std::string_view childID,ChecklistAction action){
    writable();const auto*found=note(id);if(!found||!visible(id)||found->kind!=ehud::data::NoteKind::todo)return false;
    const auto child=std::find_if(found->items.begin(),found->items.end(),[&](const auto&i){return i.id==childID;});if(child==found->items.end())return false;
    if(editing_)throw std::logic_error("Finish Notes editor before changing checklist rows");
    if(action!=ChecklistAction::toggle&&action!=ChecklistAction::up&&action!=ChecklistAction::down&&action!=ChecklistAction::remove)throw std::invalid_argument("Invalid checklist mutation");
    const auto index=static_cast<std::size_t>(child-found->items.begin());const std::string owned(id);(void)select(owned);auto item=*note(owned);
    switch(action){case ChecklistAction::toggle:item.items[index].isChecked=!item.items[index].isChecked;break;
    case ChecklistAction::up:if(index)std::swap(item.items[index],item.items[index-1]);break;
    case ChecklistAction::down:if(index+1<item.items.size())std::swap(item.items[index],item.items[index+1]);break;
    case ChecklistAction::remove:item.items.erase(item.items.begin()+static_cast<std::ptrdiff_t>(index));break;
    default:throw std::invalid_argument("Invalid checklist mutation");}
    // Source still performs its save/render/mutation event at an up/down edge.
    replace(item);(void)save(item);++revision_;return true;
}
bool NotesState::togglePin(std::string_view id){
    writable();if(!visible(id))return false;const std::string owned(id);(void)select(owned);auto item=*note(owned);item.isPinned=!item.isPinned;
    replace(item);(void)save(item);++revision_;return true;
}
bool NotesState::requestDeletion(std::string_view id){writable();if(!visible(id))return false;const std::string owned(id);(void)select(owned);pendingDeletion_=owned;++revision_;return true;}
void NotesState::cancelDeletion(){writable();if(pendingDeletion_){pendingDeletion_.reset();++revision_;}}
bool NotesState::confirmDeletion(std::string_view id){
    writable();if(!visible(id)||!pendingDeletion_||*pendingDeletion_!=id)return false;
    const std::string owned(id);
    try{Persisting guard(persisting_);persistence_.remove(owned);}
    catch(const std::exception& e){error_=e.what();++revision_;return false;}
    catch(...){error_="Notes deletion failed";++revision_;return false;}
    notes_.erase(std::remove_if(notes_.begin(),notes_.end(),[&](const auto& n){return n.id==owned;}),notes_.end());
    pendingDeletion_.reset();unsaved_.erase(owned);if(selected_==owned)selected_.reset();if(editing_&&editing_->noteID==owned)editing_.reset();
    if(drag_&&drag_->noteID==owned)drag_.reset();++revision_;return true;
}
std::optional<std::array<NotesState::Rect,2>> NotesState::deletionControls()const noexcept {
    if(!pendingDeletion_||!visible(*pendingDeletion_))return {};const auto r=card(*pendingDeletion_)->rect;
    const auto x=std::min(workspace_.x+workspace_.width-56,std::max(workspace_.x,r.x+r.width-59));
    const auto y=std::min(workspace_.y+workspace_.height-25,std::max(workspace_.y,r.y+27));
    return std::array<Rect,2>{{{x,y,25,25},{x+31,y,25,25}}};
}
bool NotesState::beginGesture(std::string_view id,Point start,Gesture kind){
    writable();if(!visible(id)||!finite(start))return false;const std::string owned(id);(void)select(owned);
    const auto* item=note(owned);drag_=Drag{owned,item->kind,geometry(*item,workspace_),start,kind,false};++revision_;return true;
}
bool NotesState::dragTo(Point point){
    writable();if(!drag_||!finite(point))return false;auto* current=find(drag_->noteID);if(!current)return false;
    const auto dx=point.x-drag_->start.x,dy=point.y-drag_->start.y;if(!drag_->changed&&std::abs(dx)+std::abs(dy)<=1)return false;
    auto rect=drag_->original;if(drag_->kind==Gesture::resize){rect.width+=dx;rect.height+=dy;}else{rect.x+=dx;rect.y+=dy;}
    rect=geometry(drag_->noteKind,rect,workspace_);
    // Swift's CanvasNote copy shares String/Data storage. In C++, update only
    // geometry scalars so pointer samples never copy text/media payloads.
    current->x=rect.x;current->y=rect.y;current->width=rect.width;current->height=rect.height;
    drag_->changed=true;++revision_;return true;
}
bool NotesState::endGesture(){
    writable();if(!drag_)return false;auto gesture=std::move(*drag_);drag_.reset();
    if(gesture.changed)if(const auto* item=note(gesture.noteID))(void)save(*item);++revision_;return gesture.changed;
}
const NotesState::EditRequest& NotesState::beginEditing(std::string_view id){
    writable();const auto* item=note(id);if(!item||!visible(id))throw std::invalid_argument("Cannot edit an unavailable note");
    if(item->kind!=ehud::data::NoteKind::text)throw std::logic_error("Checklist editing requires its row adapter");
    (void)decodeNotesRichText(item->text,item->richText);
    const auto r=geometry(*item,workspace_);editing_=EditRequest{item->id,item->text,{r.x+9,r.y+29,r.width-18,std::max(1.,r.height-58)},12,true,item->richText};++revision_;return *editing_;
}
const NotesState::EditRequest& NotesState::beginEditingItem(std::string_view id,std::string_view childID,Rect local,double offset){
    writable();const auto*item=note(id);if(!item||!visible(id)||item->kind!=ehud::data::NoteKind::todo)throw std::invalid_argument("Cannot edit unavailable checklist");
    const auto child=std::find_if(item->items.begin(),item->items.end(),[&](const auto&v){return v.id==childID;});if(child==item->items.end())throw std::invalid_argument("Cannot edit unavailable checklist row");
    const auto r=geometry(*item,workspace_);if(!valid(local)||local.x<5||local.y<27||local.x+local.width>r.width-5||local.y+local.height>27+std::max(1.,r.height-55)||!std::isfinite(offset)||offset<0)throw std::invalid_argument("Invalid measured checklist editor viewport");
    (void)decodeNotesRichText(child->text,std::nullopt);editing_=EditRequest{item->id,child->text,{r.x+local.x,r.y+local.y,local.width,local.height},11,false,std::nullopt,child->id,offset};++revision_;return *editing_;
}
bool NotesState::finishEditing(std::string text){
    writable();if(!editing_)return false;const auto* found=note(editing_->noteID);if(!found)return false;
    if(!editing_->itemID&&(found->richText||editing_->richText))throw std::logic_error("Plain Notes commit cannot discard rich formatting");
    return finishEditing(std::move(text),std::nullopt);
}
bool NotesState::finishEditing(std::string text,std::optional<std::string> richText){
    writable();if(!editing_)return false;const auto* found=note(editing_->noteID);if(!found)return false;
    if(editing_->itemID){
        if(found->kind!=ehud::data::NoteKind::todo||richText)throw std::logic_error("Checklist commits cannot contain rich formatting");
        const auto child=std::find_if(found->items.begin(),found->items.end(),[&](const auto&i){return i.id==*editing_->itemID;});if(child==found->items.end())throw std::logic_error("Checklist edited row disappeared");
        (void)decodeNotesRichText(text,std::nullopt);const auto index=static_cast<std::size_t>(child-found->items.begin());auto item=*found;item.items[index].text=std::move(text);editing_.reset();replace(item);const bool saved=save(item);++revision_;return saved;
    }
    if(found->kind!=ehud::data::NoteKind::text)throw std::logic_error("Notes edit type changed");
    (void)decodeNotesRichText(text,richText); // complete validation before state/persistence mutation
    auto item=*found;item.text=std::move(text);item.richText=std::move(richText);editing_.reset();replace(item);const bool saved=save(item);++revision_;return saved;
}
void NotesState::detachEditor(){writable();if(editing_){editing_.reset();++revision_;}}
} // namespace endfield::modules
