#include "app/archive_service.hpp"
#include "modules/archive_repository.hpp"
#include "modules/notes_relink_ledger.hpp"
#include <thread>

namespace endfield::app {
struct ArchiveService::Impl:std::enable_shared_from_this<Impl> {
    UtilityExecutor&executor;std::function<void()>changed;
    std::shared_ptr<modules::ArchiveSQLiteRepository>repository;
    std::unique_ptr<modules::ArchiveState>state;
    UtilityExecutor::Route route;bool alive{true};
    std::filesystem::path appRoot; // the data root above Archive/: Migration/relink.json
    const std::thread::id owner=std::this_thread::get_id();
    Impl(std::filesystem::path directory,UtilityExecutor&e,modules::ArchiveTextRules rules,std::function<void()>notify,std::shared_ptr<modules::ArchivePasswordVault>vault)
      :executor(e),changed(std::move(notify)),repository(std::make_shared<modules::ArchiveSQLiteRepository>(directory,std::move(rules),std::move(vault))),route(e.makeRoute()),appRoot(directory.parent_path()){}
    void onOwner()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("Archive service belongs to its owner thread");}
    void initialize(modules::ArchiveTextRules rules){
        const std::weak_ptr<Impl>weak=shared_from_this();
        modules::ArchiveExecutor bridge{[weak](auto work,auto completion){
            const auto self=weak.lock();if(!self||!self->alive)return false;self->onOwner();
            return self->executor.submit(self->route,std::move(work),[weak,completion=std::move(completion)](std::exception_ptr error){
                const auto self=weak.lock();if(!self||!self->alive)return;self->onOwner();
                completion(error);if(self->alive&&self->changed)self->changed();
            });
        }};
        state=std::make_unique<modules::ArchiveState>(repository,std::move(bridge),std::move(rules));
        // Mac import relink rows of Archive attachments follow the entry writes
        // on the same FIFO queue (modules/notes_relink_ledger.hpp).
        modules::ArchiveRelinkLedger ledger;const auto root=appRoot;
        ledger.relinked=[root](std::string_view id,const modules::ArchiveJson&mac,std::string_view path){(void)modules::archiveMediaRelinked(root,id,mac,path);};
        ledger.removed=[root](std::string_view id,const modules::ArchiveJson*mac){(void)modules::archiveMediaRemoved(root,id,mac);};
        ledger.reconcile=[root](modules::ArchiveRepository&store){(void)modules::archiveMediaReconcile(root,[&store](std::string_view id)->std::optional<std::vector<modules::ArchiveJson>>{auto entry=store.entry(id);if(!entry)return std::nullopt;return std::move(entry->media);});};
        state->setRelinkLedger(std::move(ledger));
    }
};
ArchiveService::ArchiveService(std::filesystem::path directory,UtilityExecutor&executor,modules::ArchiveTextRules rules,std::function<void()>changed,std::shared_ptr<modules::ArchivePasswordVault>vault){
    impl_=std::make_shared<Impl>(std::move(directory),executor,rules,std::move(changed),std::move(vault));
    try{impl_->initialize(std::move(rules));}catch(...){executor.invalidate(impl_->route,false);throw;}
}
ArchiveService::~ArchiveService(){auto self=impl_;self->alive=false;self->executor.invalidate(self->route,false);self->state.reset();}
modules::ArchiveState&ArchiveService::state()noexcept{return *impl_->state;}
const modules::ArchiveState&ArchiveService::state()const noexcept{return *impl_->state;}
void ArchiveService::queueCapacityAvailable(){auto self=impl_;self->onOwner();self->state->queueCapacityAvailable();}
bool ArchiveService::flush(){
    auto self=impl_;self->onOwner();self->state->retryPendingSaves();self->state->flush();
    for(;;){
        self->executor.waitIdle();self->executor.drain();if(!self->alive)return false;
        self->state->queueCapacityAvailable();
        if(!self->state->busy())return !self->state->hasUnsavedChanges();
    }
}
}
