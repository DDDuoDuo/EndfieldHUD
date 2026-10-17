#include "modules/profile_service.hpp"
#include <algorithm>
#include <limits>
#include <thread>

namespace endfield::modules {
namespace {
using ehud::data::ProfileStore;using ehud::data::StoreError;using ehud::data::StoreErrorCode;
std::pair<ProfileFailure,std::string>classify(std::exception_ptr error,bool loading){
    try{if(error)std::rethrow_exception(error);}
    catch(const StoreError&e){
        switch(e.code()){
        case StoreErrorCode::newerVersion:return {ProfileFailure::newerVersion,{}};
        case StoreErrorCode::changedOnDisk:return {ProfileFailure::changedOnDisk,{}};
        case StoreErrorCode::invalid:if(loading)return {ProfileFailure::record,{}};break;
        default:break;
        }
        return {ProfileFailure::persistence,std::string(e.what()).substr(0,1024)};
    }
    catch(const std::exception&e){return {ProfileFailure::persistence,std::string(e.what()).substr(0,1024)};}
    catch(...){}
    return {ProfileFailure::persistence,"Unknown profile storage error"};
}
}
struct ProfileService::Impl:std::enable_shared_from_this<Impl> {
    struct Worker {
        std::filesystem::path root;std::unique_ptr<ProfileStore>store;
        explicit Worker(std::filesystem::path path):root(std::move(path)){}
        void initialize(){if(!store)store=std::make_unique<ProfileStore>(root);}
    };
    struct Pending {PersonalProfile value;bool fromGame{},locked{};std::set<std::string>obsolete;};
    app::UtilityExecutor&executor;ProfileServiceCallbacks callbacks;std::filesystem::path root;std::shared_ptr<Worker>worker;
    app::UtilityExecutor::Route route;const std::thread::id owner=std::this_thread::get_id();
    ProfileServiceStatus state;std::optional<PersonalProfile>accepted;std::shared_ptr<const Pending>latest;std::set<std::string>obsolete;
    bool alive{true},started{};
    Impl(std::filesystem::path path,app::UtilityExecutor&e,ProfileServiceCallbacks cb)
        :executor(e),callbacks(std::move(cb)),root(path),worker(std::make_shared<Worker>(std::move(path))),route(e.makeRoute()){}
    void onOwner()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("Profile service owner-thread operation");}
    void notify(){if(alive&&callbacks.changed)callbacks.changed();}
    PersonalProfile accept(const PersonalProfile&value,bool locked,bool fromGame){
        onOwner();
        if(!state.loaded)throw ProfileError(ProfileFailure::unavailable);
        if(accepted&&sameProfileValues(*accepted,value)&&accepted->originalFields==value.originalFields&&
           (!latest||(latest->locked==locked&&(latest->fromGame||!fromGame))))return value;
        if(state.revision==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Profile save revision exhausted");
        if(accepted)for(const auto&name:{accepted->avatarFilename,accepted->backgroundFilename})
            if(name&&name!=value.avatarFilename&&name!=value.backgroundFilename)obsolete.insert(*name);
        auto next=std::make_shared<Pending>();next->value=value;next->locked=locked;
        // An account write coalesced with a later local edit stays an account write.
        next->fromGame=fromGame||(latest&&latest->fromGame);next->obsolete=obsolete;
        latest=std::move(next);accepted=value;++state.revision;state.dirty=true;state.failed=false;state.error.reset();
        pump();return value;
    }
    void pump(){
        if(!alive||!started||state.busy||state.failed||state.loadFailed||(state.loaded&&!latest))return;
        const bool loading=!state.loaded;const auto revision=state.revision;const auto job=latest;const auto storage=worker;
        auto loadedValue=std::make_shared<std::optional<PersonalProfile>>();
        const std::weak_ptr<Impl>weak=shared_from_this();
        if(!executor.submit(route,[storage,job,loadedValue,loading]{
            storage->initialize();auto&store=*storage->store;
            if(loading){*loadedValue=store.value();return;}
            const auto before=store.value();auto next=job->value;
            // Lifetime hours are absolute and monotonic: an older coalesced
            // edit can never lower a checkpoint that already reached the file.
            next.accumulatedWorkSeconds=std::max(next.accumulatedWorkSeconds,before.accumulatedWorkSeconds);
            store.setProfileSyncLocked(job->locked);
            if(job->fromGame)store.updateFromGame(next);else store.update(next);
            auto names=job->obsolete;
            for(const auto&name:{before.avatarFilename,before.backgroundFilename})if(name)names.insert(*name);
            for(const auto&name:names){
                if(name==next.avatarFilename||name==next.backgroundFilename)continue;
                try{std::error_code ignored;std::filesystem::remove(store.imagePath(name),ignored);}catch(const std::exception&){}
            }
        },[weak,loadedValue,job,revision,loading](std::exception_ptr error){
            auto i=weak.lock();if(!i||!i->alive)return;i->state.busy=false;
            if(loading){
                if(error){
                    const auto [code,detail]=classify(error,true);i->state.loadFailed=true;
                    i->state.error=profileFailureMessage(code,core::Language::english,detail);
                    if(i->callbacks.loadFailed)i->callbacks.loadFailed(code,detail);
                }else{
                    i->state.loaded=true;i->accepted=**loadedValue;
                    if(i->callbacks.loaded)i->callbacks.loaded(*i->accepted);
                }
            }else if(error){
                // A newer accepted value gets one attempt of its own.
                if(revision==i->state.revision){
                    const auto [code,detail]=classify(error,false);i->state.failed=true;
                    i->state.error=profileFailureMessage(code,core::Language::english,detail);
                    if(i->callbacks.saveFailed)i->callbacks.saveFailed(code,detail);
                }
            }else{
                i->state.savedRevision=revision;
                for(const auto&name:job->obsolete)i->obsolete.erase(name);
                if(revision==i->state.revision){i->latest.reset();i->state.dirty=false;i->state.error.reset();}
            }
            if(!i->alive)return;
            i->pump();i->notify();
        }))return;
        state.busy=true;
    }
};
ProfileService::ProfileService(std::filesystem::path root,app::UtilityExecutor&executor,ProfileServiceCallbacks callbacks){
    // Lexical checks only; the store validates the root on the worker.
    if(root.empty()||!root.is_absolute()||root==root.root_path()||root.lexically_normal()!=root||
       root.native().find(std::filesystem::path::value_type{})!=std::filesystem::path::string_type::npos)
        throw std::invalid_argument("Profile service requires an explicit absolute app root");
    impl_=std::make_shared<Impl>(std::move(root),executor,std::move(callbacks));
}
ProfileService::~ProfileService(){auto i=impl_;i->alive=false;i->executor.invalidate(i->route,false);}
void ProfileService::start(){auto i=impl_;i->onOwner();i->started=true;i->pump();}
PersonalProfile ProfileService::accept(const PersonalProfile&value,bool locked){return impl_->accept(value,locked,false);}
PersonalProfile ProfileService::acceptFromGame(const PersonalProfile&value,bool locked){return impl_->accept(value,locked,true);}
ProfilePersistence ProfileService::persistence(std::function<bool()>locked,std::function<double()>workSeconds){
    ProfilePersistence io;const std::weak_ptr<Impl>weak=impl_;
    io.commit=[weak,locked=std::move(locked)](const PersonalProfile&value){
        const auto i=weak.lock();if(!i||!i->alive)throw ProfileError(ProfileFailure::unavailable);
        return i->accept(value,locked&&locked(),false);
    };
    io.workSeconds=std::move(workSeconds);return io;
}
void ProfileService::queueCapacityAvailable(){auto i=impl_;i->onOwner();i->pump();}
void ProfileService::retry(){auto i=impl_;i->onOwner();i->started=true;i->state.failed=false;i->state.error.reset();i->pump();}
const ProfileServiceStatus&ProfileService::status()const{impl_->onOwner();return impl_->state;}
const std::optional<PersonalProfile>&ProfileService::profile()const{impl_->onOwner();return impl_->accepted;}
std::filesystem::path ProfileService::imagePath(std::string_view filename)const{
    const auto suffix=filename.ends_with(".png")?std::string_view(".png"):std::string_view(".image");
    if(!filename.ends_with(suffix)||filename.size()!=36+suffix.size()||!ehud::data::validUUID(filename.substr(0,36)))throw ProfileError(ProfileFailure::record);
    return impl_->root/"Profile"/"Images"/std::string(filename);
}
const std::filesystem::path&ProfileService::root()const{return impl_->root;}
bool ProfileService::flush(){
    auto i=impl_;i->onOwner();i->started=true;i->state.failed=false;i->state.error.reset();i->pump();
    while(i->alive){
        if(i->state.failed||i->state.loadFailed)return false;
        if(i->state.loaded&&!i->state.busy&&!i->state.dirty)return true;
        i->executor.waitIdle();i->executor.drain();if(!i->alive)return false;i->pump();
    }
    return false;
}
}
