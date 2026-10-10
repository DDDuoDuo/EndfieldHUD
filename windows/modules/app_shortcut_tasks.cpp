#include "modules/app_shortcut_tasks.hpp"
#include <algorithm>
#ifdef _WIN32
#include <objbase.h>
#endif

namespace endfield::modules {
namespace {
bool durable(ShortcutTaskKind k) {
    return k==ShortcutTaskKind::save||k==ShortcutTaskKind::remove||k==ShortcutTaskKind::launch;
}
struct Apartment {
#ifdef _WIN32
    HRESULT result{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
    Apartment(){if(FAILED(result))throw std::runtime_error("Application inspection could not start.");}
    ~Apartment(){CoUninitialize();}
#endif
};
}
struct AppShortcutTasks::Impl:std::enable_shared_from_this<Impl> {
    struct Worker {
        ShortcutRepository repository;ShortcutTextRules rules;
        native::NativeAppShortcutService service;ShortcutFile file;bool loaded{};
        Worker(std::filesystem::path p,ShortcutTextRules r,native::AppShortcutOS os)
            :repository(std::move(p),r),rules(std::move(r)),service(std::move(os)){}
        void perform(const ShortcutTask&t,ShortcutTaskResult&r) {
            [[maybe_unused]] Apartment apartment;
            if(!loaded){file=repository.load();loaded=true;}
            switch(t.kind){
            case ShortcutTaskKind::load:r.file=file;break;
            case ShortcutTaskKind::inspect:r.candidate=service.inspect(*t.selection);break;
            case ShortcutTaskKind::edit:
                // Imported Mac records remain editable on Windows. Saving their
                // name/icon keeps the opaque original locator, without launching.
                r.candidate=t.record->native()?service.reinspect(t.record->locator):
                    ShortcutCandidate{t.record->originalName,t.record->bundleIdentifier,t.record->locator};break;
            case ShortcutTaskKind::save:{
                auto next=file;const auto&selected=*t.candidate;
                const bool native=selected.locator["referencePlatform"]==ShortcutJson("windows");
                const auto current=native?service.reinspect(selected.locator):selected;
                saveShortcutDraft(next,selected,current,t.name,t.icon,t.editingID,t.newID,t.createdAt,rules,
                    [](const auto&a,const auto&b){return a["referencePlatform"]==ShortcutJson("windows")?
                        native::NativeAppShortcutService::sameTarget(a,b):a==b;});
                repository.save(next);file=std::move(next);r.file=file;break;
            }
            case ShortcutTaskKind::remove:{
                auto next=file;if(!removeShortcut(next,t.record->id))throw ShortcutError(ShortcutErrorCode::missing);
                repository.save(next);file=std::move(next);r.file=file;break;
            }
            case ShortcutTaskKind::launch:r.launched=service.launch(*t.record,t.launchOwner);break;
            }
        }
    };
    app::UtilityExecutor&queue;std::shared_ptr<Worker> worker;
    app::UtilityExecutor::Route route;Completion completed;
    std::optional<ShortcutTask> pending;std::uint64_t running{},lastToken{};
    ShortcutTaskKind runningKind{};bool cancelled{};
    Impl(app::UtilityExecutor&q,std::filesystem::path p,ShortcutTextRules r,native::AppShortcutOS os,Completion c)
        :queue(q),worker(std::make_shared<Worker>(std::move(p),std::move(r),std::move(os))),route(q.makeRoute()),completed(std::move(c)){}
    void pump(){
        if(!pending||running)return;
        auto task=std::make_shared<const ShortcutTask>(*pending);
        auto result=std::make_shared<ShortcutTaskResult>();result->token=task->token;result->kind=task->kind;
        const auto weak=weak_from_this();auto state=worker;
        if(!queue.submit(route,[state,task,result]{state->perform(*task,*result);},[weak,result](std::exception_ptr error){
            const auto self=weak.lock();if(!self)return;
            const bool discard=self->cancelled;self->running=0;self->cancelled=false;
            if(error){try{std::rethrow_exception(error);}catch(const std::exception&e){result->error=e.what();}catch(...){result->error="Application action failed.";}}
            if(!discard)self->completed(std::move(*result));
        }))return;
        running=task->token;runningKind=task->kind;pending.reset();
    }
};
AppShortcutTasks::AppShortcutTasks(app::UtilityExecutor&q,std::filesystem::path p,ShortcutTextRules r,native::AppShortcutOS os,Completion c){
    if(!c)throw std::invalid_argument("Application tasks require an owner completion");
    impl_=std::make_shared<Impl>(q,std::move(p),std::move(r),std::move(os),std::move(c));
}
AppShortcutTasks::~AppShortcutTasks(){if(impl_)impl_->queue.invalidate(impl_->route,false);}
bool AppShortcutTasks::submit(ShortcutTask t){
    auto&i=*impl_;if(i.pending||i.running)return false;
    if(!t.token||t.token<=i.lastToken||t.kind<ShortcutTaskKind::load||t.kind>ShortcutTaskKind::launch)throw std::invalid_argument("Application request token must advance and kind must be valid");
    if((t.kind==ShortcutTaskKind::inspect&&!t.selection)||
       ((t.kind==ShortcutTaskKind::edit||t.kind==ShortcutTaskKind::remove||t.kind==ShortcutTaskKind::launch)&&!t.record)||
       (t.kind==ShortcutTaskKind::save&&!t.candidate))throw std::invalid_argument("Application request lacks its explicit input");
    i.lastToken=t.token;i.pending=std::move(t);i.pump();return true;
}
bool AppShortcutTasks::cancel(std::uint64_t token){
    auto&i=*impl_;if(!token)return false;
    if(i.pending&&i.pending->token==token){if(durable(i.pending->kind))return false;i.pending.reset();return true;}
    if(i.running==token&&!durable(i.runningKind)){i.cancelled=true;return true;}return false;
}
void AppShortcutTasks::queueCapacityAvailable(){impl_->pump();}
bool AppShortcutTasks::busy()const noexcept{return impl_->pending.has_value()||impl_->running!=0;}
}
