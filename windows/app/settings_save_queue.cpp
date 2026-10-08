#include "app/settings_save_queue.hpp"
#include <limits>
#include <thread>

namespace endfield::app {
namespace {
std::string failureMessage(std::exception_ptr error){
    try{if(error)std::rethrow_exception(error);}catch(const std::exception&e){return std::string(e.what()).substr(0,1024);}catch(...){}
    return "Settings could not be saved";
}
}
struct SettingsSaveQueue::Impl:std::enable_shared_from_this<Impl> {
    struct Worker {
        std::filesystem::path root;
        std::unique_ptr<ehud::data::SettingsStore>store;
        explicit Worker(std::filesystem::path path):root(std::move(path)){}
        void initialize(){if(!store)store=std::make_unique<ehud::data::SettingsStore>(root);}
    };
    UtilityExecutor&executor;SettingsSaveCallbacks callbacks;std::shared_ptr<Worker>worker;
    UtilityExecutor::Route route;
    const std::thread::id owner=std::this_thread::get_id();
    SettingsSaveStatus state;std::shared_ptr<const ehud::data::Settings>baseline,latest;
    bool alive{true},started{};
    Impl(std::filesystem::path root,UtilityExecutor&e,SettingsSaveCallbacks cb)
        :executor(e),callbacks(std::move(cb)),worker(std::make_shared<Worker>(std::move(root))),route(e.makeRoute()){}
    void onOwner()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("Settings save queue owner-thread operation");}
    void notify(){if(alive&&callbacks.changed)callbacks.changed();}
    void pump(){
        if(!alive||!started||state.busy||state.failed||(state.loaded&&!latest))return;
        const bool loading=!state.loaded;const auto revision=state.revision;
        const auto value=latest;const auto storage=worker;
        auto loaded=std::make_shared<std::shared_ptr<const ehud::data::Settings>>();
        const std::weak_ptr<Impl>weak=shared_from_this();
        if(!executor.submit(route,[storage,value,loaded,loading]{
            storage->initialize();
            if(loading)*loaded=std::make_shared<const ehud::data::Settings>(storage->store->value());
            else storage->store->update(*value);
        },[weak,loaded,value,revision,loading](std::exception_ptr error){
            auto i=weak.lock();if(!i||!i->alive)return;i->state.busy=false;
            if(error){
                // A newer explicit preference gets one attempt of its own;
                // a stale failure must not block it or replace its status.
                if(loading||revision==i->state.revision){i->state.failed=true;i->state.error=failureMessage(error);}
            }else if(loading){
                i->baseline=*loaded;i->state.loaded=true;i->state.error.reset();
                if(i->callbacks.loaded)i->callbacks.loaded(*i->baseline);
            }else{
                i->baseline=value;i->state.savedRevision=revision;
                if(revision==i->state.revision){i->latest.reset();i->state.dirty=false;i->state.error.reset();}
            }
            if(!i->alive)return;
            // Admit a newer coalesced value before an observer can throw.
            i->pump();i->notify();
        }))return;
        state.busy=true;
    }
};
SettingsSaveQueue::SettingsSaveQueue(std::filesystem::path root,UtilityExecutor&e,SettingsSaveCallbacks callbacks){
    // Lexical checks only. Filesystem identity/ordinary-file checks stay in the
    // existing store and run on the worker, including a missing/new root.
    if(root.empty()||!root.is_absolute()||root==root.root_path()||root.lexically_normal()!=root||
       root.native().find(std::filesystem::path::value_type{})!=std::filesystem::path::string_type::npos)
        throw std::invalid_argument("Settings save queue requires an explicit absolute app root");
    impl_=std::make_shared<Impl>(std::move(root),e,std::move(callbacks));
}
SettingsSaveQueue::~SettingsSaveQueue(){auto i=impl_;i->alive=false;i->executor.invalidate(i->route,false);}
void SettingsSaveQueue::start(){auto i=impl_;i->onOwner();i->started=true;i->pump();}
void SettingsSaveQueue::save(const ehud::data::Settings&value){
    auto i=impl_;i->onOwner();if(!i->state.loaded)throw std::logic_error("Load Settings before saving a replacement record");
    if((i->latest&&*i->latest==value)||(!i->latest&&i->baseline&&*i->baseline==value))return;
    if(i->state.revision==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Settings save revision exhausted");
    auto copy=std::make_shared<const ehud::data::Settings>(value);
    i->latest=std::move(copy);++i->state.revision;i->state.dirty=true;i->state.failed=false;i->state.error.reset();i->pump();
}
void SettingsSaveQueue::queueCapacityAvailable(){auto i=impl_;i->onOwner();i->pump();}
void SettingsSaveQueue::retry(){auto i=impl_;i->onOwner();i->started=true;i->state.failed=false;i->state.error.reset();i->pump();}
const SettingsSaveStatus&SettingsSaveQueue::status()const{impl_->onOwner();return impl_->state;}
bool SettingsSaveQueue::flush(){
    auto i=impl_;i->onOwner();i->started=true;i->state.failed=false;i->state.error.reset();i->pump();
    while(i->alive){
        if(i->state.failed)return false;
        if(i->state.loaded&&!i->state.busy&&!i->state.dirty)return true;
        i->executor.waitIdle();i->executor.drain();if(!i->alive)return false;i->pump();
    }return false;
}
}
