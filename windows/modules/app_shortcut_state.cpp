#include "modules/app_shortcut_state.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace endfield::modules {
namespace {
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
bool finite(core::Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
void text(std::string_view s){need(s.size()<=shortcutMaximumBytes&&ShortcutJson::validUtf8(s)&&s.find('\0')==s.npos,"Invalid shortcut canvas text");}
void validateFile(const ShortcutFile&f){std::set<std::string>ids;for(const auto&v:f.items){need(ehud::data::validUUID(v.id)&&ids.insert(v.id).second&&validShortcutIcon(v.iconPreset),"Invalid shortcut canvas identity");text(v.name);text(v.originalName);}}
core::Rect intersection(core::Rect a,core::Rect b){const auto x=std::max(a.x,b.x),y=std::max(a.y,b.y);return{x,y,std::max(0.,std::min(a.x+a.width,b.x+b.width)-x),std::max(0.,std::min(a.y+a.height,b.y+b.height)-y)};}
}
core::Rect shortcutIconRect(std::size_t index){need(index<shortcutIconOrder.size(),"Unknown shortcut icon");return{12+double(index%7)*54,140+double(index/7)*55,48,48};}
AppShortcutState::AppShortcutState(ShortcutFile value,std::optional<std::string>error):file_(std::move(value)),error_(std::move(error)){validateFile(file_);if(error_)text(*error_);rebuildActions();}
void AppShortcutState::clock(double t){need(std::isfinite(t),"Shortcut canvas needs finite owner time");time_=std::max(time_,t);}
void AppShortcutState::changed(){need(revision_!=UINT64_MAX,"Shortcut canvas revision exhausted");++revision_;rebuildActions();}
double AppShortcutState::maximumScroll()const noexcept{return std::max(0.,double(file_.items.size())*52-6-shortcutListRect.height);}
void AppShortcutState::replaceItems(ShortcutFile value){validateFile(value);if(file_==value)return;file_=std::move(value);scroll_=std::min(scroll_,maximumScroll());changed();}
void AppShortcutState::showError(std::optional<std::string>v){if(v)text(*v);if(v==error_)return;error_=std::move(v);changed();}
void AppShortcutState::setActive(bool v,double t){clock(t);if(v==active_)return;active_=v;if(!v){cancelTransitions();if(dropTarget_){dropTarget_=false;changed();}}}
void AppShortcutState::setReducedMotion(bool v,double t){clock(t);if(v==reduced_)return;reduced_=v;if(v){if(transition_.active){transition_.active=false;nameRequested_=active_&&editing()&&nameAfter_;nameAfter_=false;changed();}preset_.generation=0;}}
void AppShortcutState::cancelTransitions(){need(transition_.generation!=UINT64_MAX,"Shortcut transition generation exhausted");++transition_.generation;const bool had=transition_.active;transition_.active=false;nameAfter_=nameRequested_=false;preset_.generation=0;if(had)changed();}
void AppShortcutState::transition(int direction,bool edit,double t){clock(t);cancelTransitions();transition_.start=time_;transition_.direction=direction;transition_.active=active_&&!reduced_;nameAfter_=transition_.active&&edit;nameRequested_=active_&&edit&&!transition_.active;changed();}
void AppShortcutState::beginDraft(ShortcutCandidate value,std::optional<std::string>id,double t){need(std::isfinite(t),"Shortcut canvas needs finite owner time");text(value.name);const ShortcutRecord*item=nullptr;if(id){auto found=std::find_if(file_.items.begin(),file_.items.end(),[&](const auto&v){return v.id==*id;});need(found!=file_.items.end(),"Missing shortcut draft identity");item=&*found;}
    // Prepare strings before changing any visible draft.
    auto name=item?item->name:value.name;auto icon=item?item->iconPreset:std::string("original");candidate_=std::move(value);editingID_=std::move(id);name_=std::move(name);icon_=std::move(icon);error_.reset();transition(1,true,t);
}
void AppShortcutState::cancelDraft(double t){clock(t);if(!editing())return;candidate_.reset();editingID_.reset();name_.clear();icon_="original";error_.reset();transition(-1,false,t);}
void AppShortcutState::setDraftName(std::string v){text(v);if(!editing()||name_==v)return;name_=std::move(v);changed();}
bool AppShortcutState::selectIcon(std::string_view value,double t){clock(t);need(validShortcutIcon(value),"Unknown shortcut icon");if(!editing()||transitioning()||icon_==value)return false;const auto before=std::find(shortcutIconOrder.begin(),shortcutIconOrder.end(),icon_),after=std::find(shortcutIconOrder.begin(),shortcutIconOrder.end(),value);icon_=value;need(preset_.generation!=UINT64_MAX,"Shortcut preset generation exhausted");++preset_.generation;preset_.start=time_;preset_.direction=after>before?1:-1;preset_.icon=icon_;if(!active_||reduced_)preset_.generation=0;changed();return true;}
bool AppShortcutState::setDropTarget(bool value){if(dropTarget_==value)return false;dropTarget_=value;changed();return true;}
void AppShortcutState::saved(ShortcutFile value,double t){validateFile(value);cancelDraft(t);replaceItems(std::move(value));}
bool AppShortcutState::scroll(core::Point p,double delta){if(editing()||transitioning()||!finite(p)||!shortcutListRect.contains(p)||!std::isfinite(delta)||std::abs(delta)<=.001)return false;const auto next=std::clamp(scroll_+delta,0.,maximumScroll());if(next!=scroll_){scroll_=next;changed();}return true;}
std::optional<core::Rect>AppShortcutState::rawCardRect(std::string_view id)const noexcept{const auto found=std::find_if(file_.items.begin(),file_.items.end(),[&](const auto&v){return v.id==id;});if(found==file_.items.end())return {};return core::Rect{12,44+double(found-file_.items.begin())*52-scroll_,376,46};}
std::optional<core::Rect>AppShortcutState::cardRect(std::string_view id)const noexcept{if(editing())return {};const auto r=rawCardRect(id);if(!r)return {};const auto clip=intersection(*r,shortcutListRect);return clip.height>=1?std::optional(clip):std::nullopt;}
void AppShortcutState::rebuildActions(){actions_.clear();if(transitioning())return;actions_.push_back({ShortcutActionKind::choose,{}, {270,2,118,28}});if(editing()){actions_.push_back({ShortcutActionKind::name,{},shortcutNameRect});for(std::size_t n=0;n<shortcutIconOrder.size();++n)actions_.push_back({ShortcutActionKind::icon,std::string(shortcutIconOrder[n]),shortcutIconRect(n)});actions_.push_back({ShortcutActionKind::cancel,{}, {134,274,108,28}});actions_.push_back({ShortcutActionKind::save,{}, {252,274,136,28}});return;}
    const auto first=static_cast<std::size_t>(scroll_/52);const auto end=std::min(file_.items.size(),first+7);
    for(auto index=first;index<end;++index){const auto&v=file_.items[index];const core::Rect r{12,44+double(index)*52-scroll_,376,46};const auto clip=intersection(r,shortcutListRect);if(clip.height<1)continue;for(const auto&pair:{std::pair{ShortcutActionKind::launch,core::Rect{r.x,r.y,r.width-70,r.height}},std::pair{ShortcutActionKind::edit,core::Rect{r.x+r.width-64,r.y+10,26,26}},std::pair{ShortcutActionKind::remove,core::Rect{r.x+r.width-32,r.y+10,26,26}}}){const auto hit=intersection(pair.second,clip);if(hit.height>=18)actions_.push_back({pair.first,v.id,hit});}}
}
const ShortcutAction*AppShortcutState::hit(core::Point p)const noexcept{if(!active_||!finite(p))return nullptr;for(const auto&v:actions_)if(v.rect.contains(p))return &v;return nullptr;}
bool AppShortcutState::advance(double t){clock(t);if(!transition_.active||time_<transition_.start+.26)return false;transition_.active=false;nameRequested_=active_&&editing()&&nameAfter_;nameAfter_=false;changed();return true;}
bool AppShortcutState::takeNameRequest()noexcept{return std::exchange(nameRequested_,false);}
bool AppShortcutState::requiresFrames(double t)const noexcept{return active_&&(transition_.active||(preset_.generation&&t<preset_.start+.18));}
}
