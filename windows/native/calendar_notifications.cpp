#include "native/calendar_notifications.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <limits>
#include <set>
#include <thread>
#ifdef _WIN32
#include "modules/calendar_editor_text.hpp"
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Notifications.h>
#endif
namespace endfield::native {namespace {namespace m=modules;
void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
void optionsValid(const CalendarNotificationOptions&o){need(o.establishedAppUserModelID.size()<=128&&o.establishedAppUserModelID.find('\0')==std::string::npos&&m::CalendarJson::validUtf8(o.establishedAppUserModelID)&&(o.language==core::Language::english||o.language==core::Language::simplifiedChinese||o.language==core::Language::traditionalChinese||o.language==core::Language::japanese||o.language==core::Language::korean)&&o.calendarIdentity.size()<=128&&!o.calendarIdentity.empty()&&m::CalendarJson::validUtf8(o.calendarIdentity)&&o.calendarIdentity.find('\0')==std::string::npos,"Invalid Calendar notification identity/language");}
std::vector<CalendarScheduledNotification>scopedPending(CalendarNotificationProvider&p){auto values=p.pending();need(values.size()<=4096,"Notification schedule exceeds enumeration bound");std::vector<CalendarScheduledNotification>out;out.reserve(std::min<std::size_t>(values.size(),512));for(auto&v:values)if(calendarNotificationKey(v.identifier)){need(out.size()<512&&v.signature.size()<=m::calendarMaximumBytes&&m::CalendarJson::validUtf8(v.signature),"Calendar pending schedule exceeds source bound");out.push_back(std::move(v));}return out;}
std::string errorText(std::exception_ptr e){try{if(e)std::rethrow_exception(e);}catch(const std::exception&v){return std::string(v.what()).substr(0,1024);}catch(...){}return "Calendar reminders could not be scheduled";}
}
std::optional<CalendarNotificationKey>calendarNotificationKey(std::string_view id){
 if(!id.starts_with(m::calendarNotificationPrefix))return {};const auto rest=id.substr(m::calendarNotificationPrefix.size());if(rest.size()<40)return {};const auto uuid=rest.substr(0,36),suffix=rest.substr(36);if(!ehud::data::validUUID(uuid)||(suffix!=".before"&&suffix!=".day"))return {};return CalendarNotificationKey{std::string(id.substr(0,m::calendarNotificationPrefix.size()+36)),std::string(suffix.substr(1))};
}
std::string calendarNotificationArguments(std::string_view id,std::string_view signature){need(ehud::data::validUUID(id)&&signature.size()<=m::calendarMaximumBytes&&m::CalendarJson::validUtf8(signature)&&signature.find('\0')==signature.npos,"Invalid Calendar notification activation");return m::CalendarJson(m::CalendarJson::Object{{"type","calendar"},{"eventID",std::string(id)},{"signature",std::string(signature)}}).encode();}
std::optional<std::string>calendarNotificationEvent(std::string_view args){try{const auto j=m::CalendarJson::parse(args);if(!j.isObject()||!j["type"].isString()||j["type"].string()!="calendar"||!j["eventID"].isString()||!ehud::data::validUUID(j["eventID"].string())||!j["signature"].isString()||j["signature"].string().find('\0')!=std::string::npos)return {};return j["eventID"].string();}catch(...){return {};}}
void cancelObsoleteCalendarNotifications(CalendarNotificationProvider&p,std::span<const std::string>keeping){need(keeping.size()<=60,"Calendar reminder plan exceeds source capacity");std::set<std::string,std::less<>>wanted;for(const auto&id:keeping){need(calendarNotificationKey(id).has_value(),"Invalid retained Calendar reminder identity");wanted.insert(id);}auto existing=scopedPending(p);for(const auto&v:existing)if(!wanted.contains(v.identifier))p.remove(v.identifier);}
void reconcileCalendarNotifications(CalendarNotificationProvider&p,std::span<const m::CalendarReminder>reminders,const CalendarNotificationOptions&o){
 optionsValid(o);need(reminders.size()<=60,"Calendar reminder plan exceeds source capacity");std::map<std::string,CalendarScheduledNotification,std::less<>>wanted;
 for(const auto&r:reminders){const auto key=calendarNotificationKey(r.identifier);need(key&&key->group==std::string(m::calendarNotificationPrefix)+r.eventID&&key->tag==(r.previousDay?"before":"day")&&r.day.valid()&&std::isfinite(r.date)&&r.date>=-m::calendarFoundationToUnix&&r.date<253402300800.-m::calendarFoundationToUnix&&r.title.size()<=m::calendarMaximumBytes&&m::CalendarJson::validUtf8(r.title)&&r.title.find('\0')==std::string::npos,"Invalid Calendar reminder payload");const auto label=core::localized(r.previousDay?"Tomorrow":"Today",r.previousDay?"明天":"今天",o.language);CalendarScheduledNotification v{r.identifier,r.signature()+"|"+o.calendarIdentity+"|"+std::string(core::languageSetting(o.language)),r.title,label+" · "+r.day.string(),r.date,r.catchUp};need(wanted.emplace(r.identifier,std::move(v)).second,"Duplicate Calendar reminder identity");}
 auto pending=scopedPending(p);std::map<std::string,const CalendarScheduledNotification*,std::less<>>existing;for(const auto&v:pending)existing.try_emplace(v.identifier,&v);
 for(const auto&v:pending)if(!wanted.contains(v.identifier))p.remove(v.identifier);
 for(const auto&r:reminders){const auto old=existing.find(r.identifier);if(old==existing.end()&&r.wasScheduled)continue;const auto&next=wanted.at(r.identifier);if(old!=existing.end()&&old->second->signature==next.signature)continue;if(old!=existing.end())p.remove(r.identifier);p.add(next);}
}
struct NativeCalendarNotifications::Impl:std::enable_shared_from_this<Impl>{
 struct Job{enum class Kind{permission,reconcile,cancel};Kind kind;bool request{};std::vector<m::CalendarReminder>reminders;std::vector<std::string>keeping;std::function<void(m::CalendarPermission)>permission;std::function<void(std::exception_ptr)>completion;std::function<void()>cancelled;};
 app::UtilityExecutor&executor;CalendarNotificationOptions options;CalendarNotificationFactory factory;app::UtilityExecutor::Route route;const std::thread::id owner{std::this_thread::get_id()};CalendarNotificationStatus status;std::deque<std::shared_ptr<Job>>queue;bool alive{true};
 Impl(app::UtilityExecutor&e,CalendarNotificationOptions o,CalendarNotificationFactory f):executor(e),options(std::move(o)),factory(std::move(f)),route(e.makeRoute()){}
 void check()const{if(owner!=std::this_thread::get_id())throw std::logic_error("Calendar notification owner thread required");}
 void enqueue(std::shared_ptr<Job>j){check();need(alive&&queue.size()<4,"Calendar notification requests require completion backpressure");queue.push_back(std::move(j));status.pending=queue.size();pump();}
 void pump(){if(!alive||status.busy||queue.empty())return;const auto job=queue.front();const auto snapshot=options;const auto make=factory;auto permission=std::make_shared<m::CalendarPermission>(m::CalendarPermission::unavailable);const std::weak_ptr<Impl>weak=shared_from_this();
  if(!executor.submit(route,[job,snapshot,make,permission]{auto provider=make(snapshot);need(bool(provider),"Calendar notification factory returned no provider");if(job->kind==Job::Kind::permission)*permission=provider->authorization(job->request);else if(job->kind==Job::Kind::reconcile)reconcileCalendarNotifications(*provider,job->reminders,snapshot);else cancelObsoleteCalendarNotifications(*provider,job->keeping);},[weak,job,permission](std::exception_ptr error){auto i=weak.lock();if(!i||!i->alive)return;i->status.busy=false;i->status.error=error?std::optional(errorText(error)):std::nullopt;try{if(job->kind==Job::Kind::permission){if(job->permission)job->permission(error?m::CalendarPermission::unavailable:*permission);}else if(job->kind==Job::Kind::reconcile){if(job->completion)job->completion(error);}else if(job->cancelled)job->cancelled();}catch(...){if(i->alive)i->pump();throw;}if(i->alive)i->pump();}))return;
  queue.pop_front();status.pending=queue.size();status.busy=true;
 }
};
NativeCalendarNotifications::NativeCalendarNotifications(app::UtilityExecutor&e,CalendarNotificationOptions o,CalendarNotificationFactory f){optionsValid(o);need(bool(f),"Calendar notification provider is required");impl_=std::make_shared<Impl>(e,std::move(o),std::move(f));}
NativeCalendarNotifications::~NativeCalendarNotifications(){auto i=impl_;i->alive=false;i->executor.invalidate(i->route,false);}
m::CalendarScheduling NativeCalendarNotifications::scheduling(){const std::weak_ptr<Impl>weak=impl_;return {
 [weak](bool request,auto complete){if(auto i=weak.lock();i&&i->alive){auto j=std::make_shared<Impl::Job>();j->kind=Impl::Job::Kind::permission;j->request=request;j->permission=std::move(complete);i->enqueue(std::move(j));}else complete(m::CalendarPermission::unavailable);},
 [weak](std::vector<m::CalendarReminder>v,m::CalendarTimeZone zone,auto complete){need(!zone.identity.empty(),"Calendar reminders require an explicit civil zone");if(auto i=weak.lock();i&&i->alive){auto j=std::make_shared<Impl::Job>();j->kind=Impl::Job::Kind::reconcile;j->reminders=std::move(v);j->completion=std::move(complete);i->enqueue(std::move(j));}else complete(std::make_exception_ptr(m::CalendarError(m::CalendarErrorCode::notifications)));},
 [weak](std::vector<std::string>v,auto complete){if(auto i=weak.lock();i&&i->alive){auto j=std::make_shared<Impl::Job>();j->kind=Impl::Job::Kind::cancel;j->keeping=std::move(v);j->cancelled=std::move(complete);i->enqueue(std::move(j));}else complete();}};}
void NativeCalendarNotifications::setLanguage(core::Language l){auto i=impl_;i->check();auto o=i->options;o.language=l;optionsValid(o);i->options=std::move(o);}
void NativeCalendarNotifications::queueCapacityAvailable(){auto i=impl_;i->check();i->pump();}
const CalendarNotificationStatus&NativeCalendarNotifications::status()const{impl_->check();return impl_->status;}
bool NativeCalendarNotifications::flush(){auto i=impl_;i->check();while(i->alive&&(i->status.busy||!i->queue.empty())){i->pump();i->executor.waitIdle();i->executor.drain();}return i->alive&&!i->status.error;}
#ifdef _WIN32
namespace {namespace n=winrt::Windows::UI::Notifications;namespace x=winrt::Windows::Data::Xml::Dom;
struct Apartment{Apartment(){winrt::init_apartment(winrt::apartment_type::multi_threaded);}~Apartment(){winrt::uninit_apartment();}};
winrt::hstring wide(std::string_view v){return winrt::hstring(reinterpret_cast<const wchar_t*>(m::calendarEditorUTF16(v).c_str()));}
std::string utf8(const winrt::hstring&v){return m::calendarEditorUTF8({reinterpret_cast<const char16_t*>(v.c_str()),v.size()});}
std::string xml(std::string_view v){std::string out;out.reserve(v.size());for(char c:v){switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;case '\'':out+="&apos;";break;default:out+=c;}}return out;}
struct WinNotifications final:CalendarNotificationProvider {
 CalendarNotificationOptions options;explicit WinNotifications(CalendarNotificationOptions o):options(std::move(o)){}
 n::ToastNotifier notifier(){return n::ToastNotificationManager::CreateToastNotifier(wide(options.establishedAppUserModelID));}
 m::CalendarPermission authorization(bool)override{if(options.establishedAppUserModelID.empty())return m::CalendarPermission::unavailable;Apartment a;switch(notifier().Setting()){case n::NotificationSetting::Enabled:return m::CalendarPermission::authorized;case n::NotificationSetting::DisabledForApplication:case n::NotificationSetting::DisabledForUser:case n::NotificationSetting::DisabledByGroupPolicy:return m::CalendarPermission::denied;default:return m::CalendarPermission::unavailable;}}
 std::vector<CalendarScheduledNotification>pending()override{if(options.establishedAppUserModelID.empty())return {};Apartment a;auto source=notifier().GetScheduledToastNotifications();need(source.Size()<=4096,"Notification schedule exceeds enumeration bound");std::vector<CalendarScheduledNotification>out;for(const auto&v:source){const auto id=utf8(v.Group())+"."+utf8(v.Tag());if(!calendarNotificationKey(id))continue;const auto args=utf8(v.Content().DocumentElement().GetAttribute(L"launch"));std::string signature;if(const auto event=calendarNotificationEvent(args);event&&std::string(m::calendarNotificationPrefix)+*event==utf8(v.Group()))signature=m::CalendarJson::parse(args)["signature"].string();out.push_back({id,signature,{},{},static_cast<double>(v.DeliveryTime().time_since_epoch().count()/10000000.L-11644473600.L-m::calendarFoundationToUnix)});}return out;}
 void remove(std::string_view id)override{const auto key=calendarNotificationKey(id);need(key.has_value(),"Only Calendar notification IDs may be removed");Apartment a;const auto center=notifier();const auto pending=center.GetScheduledToastNotifications();need(pending.Size()<=4096,"Notification schedule exceeds enumeration bound");for(const auto&v:pending)if(utf8(v.Group())==key->group&&utf8(v.Tag())==key->tag)center.RemoveFromSchedule(v);}
 void add(const CalendarScheduledNotification&v)override{need(!options.establishedAppUserModelID.empty(),"Install Calendar app notification identity before scheduling");const auto key=calendarNotificationKey(v.identifier);need(key.has_value(),"Invalid Calendar notification key");Apartment a;x::XmlDocument doc;const auto eventID=std::string_view(key->group).substr(m::calendarNotificationPrefix.size());doc.LoadXml(wide("<toast launch=\""+xml(calendarNotificationArguments(eventID,v.signature))+"\"><visual><binding template=\"ToastGeneric\"><text>"+xml(v.title)+"</text><text>"+xml(v.body)+"</text></binding></visual><audio src=\"ms-winsoundevent:Notification.Default\"/></toast>"));const auto delivery=v.catchUp?std::max(v.delivery,ehud::data::foundationNow()+1):v.delivery;const auto ticks=std::round((static_cast<long double>(delivery)+m::calendarFoundationToUnix+11644473600.L)*10000000.L);need(ticks>=0&&ticks<=static_cast<long double>(std::numeric_limits<std::int64_t>::max()),"Calendar delivery exceeds Windows timestamp range");n::ScheduledToastNotification toast(doc,winrt::clock::time_point{winrt::clock::duration{static_cast<std::int64_t>(ticks)}});toast.Group(wide(key->group));toast.Tag(wide(key->tag));notifier().AddToSchedule(toast);}
};
}
CalendarNotificationFactory nativeCalendarNotificationFactory(){return [](const CalendarNotificationOptions&o){return std::make_unique<WinNotifications>(o);};}
#else
CalendarNotificationFactory nativeCalendarNotificationFactory(){return [](const CalendarNotificationOptions&)->std::unique_ptr<CalendarNotificationProvider>{throw std::runtime_error("Windows Calendar notifications unavailable on this platform");};}
#endif
}
