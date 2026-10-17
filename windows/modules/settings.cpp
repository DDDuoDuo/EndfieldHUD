#include "modules/settings.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace endfield::modules {namespace {
using Settings=ehud::data::Settings;using Json=ehud::data::Json;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
void finite(double time){need(std::isfinite(time),"Settings requires finite caller time");}
void bounded(std::string_view text){need(text.size()<=16*1024&&Json::validUtf8(text),"Invalid bounded Settings status");}
std::string tr(core::Language l,std::string_view en,std::string_view zh){return core::localized(en,zh,l);}
std::string keyTitle(std::uint32_t key){
    if((key>='A'&&key<='Z')||(key>='0'&&key<='9'))return std::string(1,static_cast<char>(key));
    if(key>=0x70&&key<=0x87)return "F"+std::to_string(key-0x6f);
    if(key>=0x60&&key<=0x69)return "Keypad "+std::to_string(key-0x60);
    switch(key){case 8:return "Backspace";case 9:return "Tab";case 13:return "Return";case 27:return "Escape";case 32:return "Space";case 33:return "Page Up";case 34:return "Page Down";case 35:return "End";case 36:return "Home";case 37:return "←";case 38:return "↑";case 39:return "→";case 40:return "↓";case 45:return "Insert";case 46:return "Delete";case 0x6a:return "Keypad *";case 0x6b:return "Keypad +";case 0x6d:return "Keypad −";case 0x6e:return "Keypad .";case 0x6f:return "Keypad /";case 0xba:return ";";case 0xbb:return "=";case 0xbc:return ",";case 0xbd:return "−";case 0xbe:return ".";case 0xbf:return "/";case 0xc0:return "`";case 0xdb:return "[";case 0xdc:return "\\";case 0xdd:return "]";case 0xde:return "'";default:return {};}
}
void putShortcut(Settings&settings,SettingsShortcut key){settings.set("windowsSummonShortcut",Json::Object{{"virtualKey",static_cast<std::int64_t>(key.key)},{"modifiers",static_cast<std::int64_t>(key.modifiers)}});}
}
std::optional<std::string>validateSettingsShortcut(SettingsShortcut key,core::Language language){
    if(key.modifiers&~15u)return tr(language,"That modifier is not supported.","不支持该修饰键。");
    const auto count=std::popcount(key.modifiers);if(count<1||count>2)return tr(language,"Use one key with one or two modifiers (up to three keys).","请使用一个普通键加一至两个修饰键（最多三个键）。");
    if(keyTitle(key.key).empty())return tr(language,"Choose a letter, number, punctuation, arrow, or function key.","请选择字母、数字、标点、方向键或功能键。");
    // Windows editing commands are protected in their native Control form.
    // Reserved OS/registered chords still fail through the injected platform.
    if((key.modifiers==2||key.modifiers==6)&&std::string_view("ACFHNOPQSTVWXZ").find(static_cast<char>(key.key))!=std::string_view::npos&&key.key<=127)
        return tr(language,"That combination is reserved for common editing commands.","该组合用于常见编辑命令，请选择其他快捷键。");
    return {};
}
std::string settingsShortcutTitle(SettingsShortcut key){std::string result;const auto append=[&](std::string_view part){if(!result.empty())result+=" + ";result+=part;};if(key.modifiers&2)append("Ctrl");if(key.modifiers&1)append("Alt");if(key.modifiers&4)append("Shift");if(key.modifiers&8)append("Win");const auto title=keyTitle(key.key);append(title.empty()?"Key "+std::to_string(key.key):title);return result;}
SettingsShortcut settingsShortcut(const Settings&settings){const auto&value=settings.fields["windowsSummonShortcut"];if(value.isNull())return {};need(value.isObject()&&value["virtualKey"].isNumber()&&value["modifiers"].isNumber(),"Invalid Windows shortcut record");const auto key=value["virtualKey"].integer(),modifiers=value["modifiers"].integer();need(key>=0&&key<=255&&modifiers>=0&&modifiers<=15,"Invalid Windows shortcut values");SettingsShortcut result{static_cast<std::uint32_t>(key),static_cast<std::uint32_t>(modifiers)};need(!validateSettingsShortcut(result,core::Language::english),"Invalid stored Windows shortcut");return result;}
SettingsController::Layout SettingsController::layout(const Settings&s){return {s.number("hudScale"),s.number("hudOffsetX"),s.number("hudOffsetY")};}
void SettingsController::applyLayout(Settings&s,Layout l){s.set("hudScale",l.scale);s.set("hudOffsetX",l.x);s.set("hudOffsetY",l.y);}
Settings SettingsController::normalize(Settings value){
    need(value.fields.isObject(),"Settings record must be an object");const auto defaults=Settings::defaults();
    for(const auto&[key,expected]:defaults.fields.object()){const auto&actual=std::as_const(value.fields)[key];need(expected.isNull()||(expected.isString()&&actual.isString())||(expected.isNumber()&&actual.isNumber())||(expected.isBool()&&actual.isBool())||(expected.isObject()&&actual.isObject()),"Settings record has a missing or invalid known field");}
    const auto clamp=[&](const char*key,double low,double high,double fallback){const auto n=value.number(key);value.set(key,std::isfinite(n)?std::clamp(n,low,high):fallback);};
    clamp("displayDuration",1,60,3);clamp("scale",.65,1.6,1);clamp("hudScale",.2,2,1);clamp("hudOffsetX",-.5,.5,0);clamp("hudOffsetY",-.5,.5,0);clamp("parallaxIntensity",0,2,1);clamp("perspectiveIntensity",0,2,1);clamp("backgroundDarkness",0,1,.63);clamp("blurAmount",0,1,.75);clamp("customPositionX",0,1,.5);clamp("customPositionY",0,1,.9);
    auto hex=value.string("accentHex");const auto first=hex.find_first_not_of(" \t\n\r"),last=hex.find_last_not_of(" \t\n\r");hex=first==std::string::npos?"":hex.substr(first,last-first+1);hex.erase(std::remove(hex.begin(),hex.end(),'#'),hex.end());for(auto&c:hex)if(c>='a'&&c<='f')c=static_cast<char>(c-'a'+'A');if(hex.size()!=6||!std::all_of(hex.begin(),hex.end(),[](char c){return(c>='0'&&c<='9')||(c>='A'&&c<='F');}))hex="FAD41F";value.set("accentHex",hex);
    (void)settingsShortcut(value);return value;
}
SettingsController::SettingsController(Settings initial,SettingsCallbacks callbacks,core::Language language):committed_(normalize(std::move(initial))),effective_(committed_),callbacks_(std::move(callbacks)),language_(language){}
void SettingsController::publish(){auto next=committed_;if(preview_)applyLayout(next,*preview_);const bool changed=next!=effective_;effective_=std::move(next);++revision_;if(changed&&callbacks_.configurationChanged)callbacks_.configurationChanged(effective_);if(callbacks_.changed)callbacks_.changed();}
void SettingsController::clearPreview(){preview_.reset();deadline_.reset();lastRemaining_.reset();}
void SettingsController::preview(Layout value,double time){finite(time);const auto deadline=time+confirmationDuration;need(std::isfinite(deadline),"Settings deadline overflow");if(value==layout(committed_)){revertLayout();return;}preview_=value;deadline_=deadline;lastRemaining_=12;status_.clear();publish();}
void SettingsController::update(Settings candidate,double time){finite(time);const auto previousLayout=layout(effective_);const auto requested=layout(normalize(candidate));const bool changed=layout(candidate)!=previousLayout;applyLayout(candidate,layout(committed_));applyPersistent(std::move(candidate));if(changed)preview(requested,time);}
void SettingsController::previewScale(double value,double time){auto next=layout(effective_);next.scale=std::isfinite(value)?std::clamp(value,.2,2.):1;preview(next,time);}
void SettingsController::previewPosition(double x,double y,double time){auto next=layout(effective_);next.x=std::isfinite(x)?std::clamp(x,-.5,.5):0;next.y=std::isfinite(y)?std::clamp(y,-.5,.5):0;preview(next,time);}
std::optional<int>SettingsController::confirmationRemaining(double time)const{finite(time);if(!deadline_)return {};return static_cast<int>(std::clamp(std::ceil(*deadline_-time),0.,confirmationDuration));}
std::optional<double>SettingsController::nextWakeTime(double time)const{const auto remaining=confirmationRemaining(time);if(!remaining)return {};if(*remaining==0)return *deadline_;return *deadline_-(*remaining-1);}
bool SettingsController::scalePending()const noexcept{return preview_&&preview_->scale!=layout(committed_).scale;}
bool SettingsController::positionPending()const noexcept{return preview_&&(preview_->x!=layout(committed_).x||preview_->y!=layout(committed_).y);}
bool SettingsController::confirmLayout(double time){finite(time);if(!preview_||!deadline_||time>=*deadline_){wake(time);return false;}auto candidate=committed_;applyLayout(candidate,*preview_);if(callbacks_.persist)callbacks_.persist(candidate);committed_=std::move(candidate);clearPreview();status_.clear();publish();return true;}
void SettingsController::revertLayout(){if(!preview_)return;clearPreview();publish();}
void SettingsController::wake(double time){finite(time);if(!deadline_)return;if(time>=*deadline_){const bool position=positionPending();clearPreview();status_=position?tr(language_,"UI layout restored","已恢复界面布局"):tr(language_,"UI scale restored","已恢复界面缩放");publish();}else if(confirmationRemaining(time)!=lastRemaining_){lastRemaining_=confirmationRemaining(time);publish();}}
void SettingsController::applyPersistent(Settings candidate,bool shortcutApplied,bool forceShortcut){
    status_.clear();const auto previous=committed_;const auto nextShortcut=settingsShortcut(candidate),oldShortcut=settingsShortcut(previous);
    if(!shortcutApplied&&(forceShortcut||nextShortcut!=oldShortcut)){auto error=validateSettingsShortcut(nextShortcut,language_);if(!error)error=callbacks_.registerShortcut?callbacks_.registerShortcut(nextShortcut):std::optional(tr(language_,"Unavailable","不可用"));shortcutError_=error.value_or("");if(error){const auto&old=previous.fields["windowsSummonShortcut"];if(old.isNull())candidate.fields.erase("windowsSummonShortcut");else candidate.set("windowsSummonShortcut",old);status_=*error;}}else shortcutError_.clear();
    if(candidate.boolean("launchAtLogin")!=previous.boolean("launchAtLogin")){const auto error=callbacks_.launchAtLogin?callbacks_.launchAtLogin(candidate.boolean("launchAtLogin")):std::optional(tr(language_,"Unavailable","不可用"));if(error){candidate.set("launchAtLogin",previous.boolean("launchAtLogin"));status_=*error;}}
    candidate=normalize(std::move(candidate));if(candidate!=committed_&&callbacks_.persist)callbacks_.persist(candidate);committed_=std::move(candidate);publish();
}
void SettingsController::restoreDefaults(){clearPreview();endShortcutCapture();auto defaults=Settings::defaults();putShortcut(defaults,{});applyPersistent(std::move(defaults),false,true);}
void SettingsController::close(){revertLayout();endShortcutCapture();}
std::optional<std::string>SettingsController::setShortcut(SettingsShortcut key){auto error=validateSettingsShortcut(key,language_);if(!error)error=callbacks_.registerShortcut?callbacks_.registerShortcut(key):std::optional(tr(language_,"Unavailable","不可用"));if(error){bounded(*error);shortcutError_=status_=*error;publish();return error;}auto candidate=effective_;putShortcut(candidate,key);applyLayout(candidate,layout(committed_));applyPersistent(std::move(candidate),true);return shortcutStatus().empty()?std::nullopt:std::optional(shortcutStatus());}
void SettingsController::beginShortcutCapture(){if(capturing_)return;capturing_=true;shortcutError_.clear();if(callbacks_.shortcutCapture)callbacks_.shortcutCapture(true);publish();}
void SettingsController::endShortcutCapture(){if(!capturing_)return;capturing_=false;if(callbacks_.shortcutCapture)callbacks_.shortcutCapture(false);publish();}
void SettingsController::setLanguage(core::Language language){if(language_==language)return;language_=language;publish();}
void SettingsController::setExternalStatus(std::string login,std::string shortcut){bounded(login);bounded(shortcut);if(login_==login&&shortcutRegistration_==shortcut)return;login_=std::move(login);shortcutRegistration_=std::move(shortcut);publish();}
void SettingsController::setStatus(std::string status){bounded(status);status_=std::move(status);publish();}
void SettingsController::setUpdateStatus(SettingsUpdateStatus value){for(const auto*s:{&value.title,&value.detail,&value.latestVersion,&value.releaseURL})bounded(*s);if(updates_==value)return;updates_=std::move(value);publish();}
void SettingsController::editBatteryPosition(){if(callbacks_.editBatteryPosition)callbacks_.editBatteryPosition();}
void SettingsController::openLink(std::string_view url){bounded(url);need(url.starts_with("https://"),"Settings link must be explicit HTTPS");if(callbacks_.openLink)callbacks_.openLink(url);}
void SettingsController::checkUpdates(){if(updates_.canCheck&&callbacks_.checkUpdates)callbacks_.checkUpdates();}
void SettingsController::toggleAutomaticUpdates(){if(updates_.canSetAutomatic&&callbacks_.automaticUpdates)callbacks_.automaticUpdates(!updates_.automaticallyInstalls);}
}
