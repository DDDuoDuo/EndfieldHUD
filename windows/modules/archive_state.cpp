#include "modules/archive_state.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace endfield::modules {
namespace {
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
// The stored attachment list still holds the Mac media object `mac`: the
// same bookmark and last known path (other metadata may be re-encoded).
bool holdsMacMedia(const ArchiveEntry&e,const ArchiveJson&mac){for(const auto&m:e.media){if(!m.isObject()||m.contains("referencePlatform"))continue;if(mac["bookmark"].isString()?m["bookmark"]==mac["bookmark"]&&m["lastKnownPath"]==mac["lastKnownPath"]:m==mac)return true;}return false;}
bool holdsWindowsPath(const ArchiveEntry&e,std::string_view path){for(const auto&m:e.media)if(m.isObject()&&m["referencePlatform"]==ArchiveJson("windows")&&m["windowsPath"].isString()&&m["windowsPath"].string()==path)return true;return false;}
std::string message(std::exception_ptr e){try{if(e)std::rethrow_exception(e);}catch(const std::exception&v){return v.what();}catch(...){return "The archive could not be saved.";}return {};}
}
struct ArchiveState::Impl:std::enable_shared_from_this<Impl> {
    struct Pending{ArchiveEntry entry;std::uint64_t revision{};};
    struct Thumbnail{std::uint64_t token{};std::vector<std::function<void(std::optional<ArchiveJson>)>>callbacks;};
    std::shared_ptr<ArchiveRepository>store;ArchiveExecutor executor;ArchiveTextRules rules;
    std::thread::id thread=std::this_thread::get_id();bool alive{true},active{},completing{},flushing{},retryFlush{},backpressured{};
    std::size_t operations{},thumbnailBytes{};std::uint64_t revision{1},collectionRevision{1},generation{},dirtyRevision{},thumbnailToken{};
    std::vector<ArchiveCategory>categories;std::vector<ArchiveSummary>entries;std::optional<ArchiveEntry>selected;
    std::map<std::string,Pending,std::less<>>dirty;std::map<std::string,std::uint64_t,std::less<>>saving;
    std::map<std::string,ArchiveCategory,std::less<>>dirtyCategories;
    std::shared_ptr<const ArchiveRelinkLedger>ledger;bool reconciled{};
    std::set<std::string,std::less<>>savingCategories,deleting,unannounced,removingCategories;
    std::map<std::string,std::set<std::string,std::less<>>,std::less<>>categoryRemovals;
    std::map<std::string,std::uint64_t,std::less<>>categoryChanges;
    std::map<std::string,std::string,std::less<>>writeErrors,categoryErrors;
    std::optional<std::string>readError,error;std::optional<double>deadline,lastTime;
    std::optional<std::string>pendingCreatedSelection;std::uint64_t selectionRevision{},savingSelection{};bool selectionFailed{};
    std::map<std::string,Thumbnail,std::less<>>thumbnailRequests;
    std::vector<ArchiveStateEvent>events;
    // 1.3.0: renames and badge clears of committed categories (folded into a
    // queued creation instead), and the classification lock model.
    std::map<std::string,std::string,std::less<>>categoryRenames;std::set<std::string,std::less<>>renamingCategories,viewedCategories,markingViewed;
    std::set<std::string,std::less<>>classified,classificationWork;std::map<std::string,ArchiveClassificationStatus,std::less<>>verifierProblems;
    std::optional<std::string>unlocking,unlockedID;std::uint64_t visit{};OwnerAuthentication ownerAuthentication;
    Impl(std::shared_ptr<ArchiveRepository>s,ArchiveExecutor e,ArchiveTextRules r):store(std::move(s)),executor(std::move(e)),rules(std::move(r)){need(store&&executor.submit&&rules.characters&&rules.trimmed&&rules.categoryNameKey,"Archive requires repository, FIFO executor and Unicode rules");}
    void onThread()const{need(std::this_thread::get_id()==thread,"Archive state belongs to its creating thread");need(alive,"Archive state is detached");}
    bool categoryExists(const std::optional<std::string>&id)const{return !id||std::any_of(categories.begin(),categories.end(),[&](const auto&c){return c.id==*id;});}
    bool isLocked(std::string_view id)const{return classified.contains(id)&&unlockedID!=id;}
    void relock(){unlockedID.reset();++visit;}
    // record(status, for:) (ArchiveStore.swift:982-987).
    void record(ArchiveClassificationStatus status,const std::string&id){if(status==ArchiveClassificationStatus::unclassified)classified.erase(id);else classified.insert(id);
        if(status==ArchiveClassificationStatus::passwordMissing||status==ArchiveClassificationStatus::vaultUnavailable)verifierProblems[id]=status;else verifierProblems.erase(id);refreshSummary(id);}
    // Re-derives one tile's classified flag and thumbnail in place (:1199-1208).
    void refreshSummary(std::string_view id){const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto&e){return e.id==id;});if(found==entries.end())return;const bool hidden=classified.contains(id);
        thumbnailBytes-=archiveThumbnailCost(found->thumbnail);auto thumbnail=hidden?std::nullopt:found->thumbnail?found->thumbnail:(selected&&selected->id==id&&!selected->media.empty()?std::optional(selected->media.front()):std::nullopt);
        const auto cost=archiveThumbnailCost(thumbnail);if(cost>archiveMaximumSummaryThumbnailBytes-thumbnailBytes)thumbnail.reset();else thumbnailBytes+=cost;found->thumbnail=std::move(thumbnail);found->isClassified=hidden;}
    void refreshSelectedClassification(){if(!selected||!classified.contains(selected->id))return;const auto id=selected->id;
        (void)perform<ArchiveClassificationStatus>([store=store,id]{return store->classificationStatus(id);},[self=shared_from_this(),id](auto failure,auto result){if(!failure&&result)self->record(*result,id);self->changed();});}
    void markDeclassified(const std::string&id){classified.erase(id);verifierProblems.erase(id);if(unlockedID==id)unlockedID.reset();refreshSummary(id);events.push_back({"declassified",id});}
    void applyPendingCategoryEdits(){for(auto&c:categories){if(const auto found=categoryRenames.find(c.id);found!=categoryRenames.end())c.name=found->second;if(viewedCategories.contains(c.id))c.isNew=false;}}
    std::optional<std::string>insertDraftCategory(ArchiveCategory category){if(categories.size()+categoryRemovals.size()>=archiveMaximumCategories){readError="The archive is full.";changed();return {};}if(!writeErrors.empty()||!categoryErrors.empty())return {};
        need(!dirtyCategories.contains(category.id)&&std::none_of(categories.begin(),categories.end(),[&](const auto&c){return c.id==category.id;}),"Repeated Archive category identity");dirtyCategories[category.id]=category;upsertCategory(category);flush();changed();return category.id;}
    // Owner time never runs backwards: a time earlier than one already seen
    // (a host sample taken before a nested callback handed a later one) is
    // the latest time. Only non-finite time fails.
    double at(double t){need(std::isfinite(t),"Archive state requires a finite owner clock");if(lastTime&&t<*lastTime)t=*lastTime;lastTime=t;return t;}
    void changed(){error=!writeErrors.empty()?std::optional(writeErrors.begin()->second):!categoryErrors.empty()?std::optional(categoryErrors.begin()->second):readError;++revision;++collectionRevision;}
    void invalidate(){++generation;auto previous=std::move(thumbnailRequests);thumbnailRequests.clear();for(auto&[id,r]:previous){(void)id;for(auto&callback:r.callbacks)callback({});}}
    template<class T,class Action,class Completion>bool perform(Action action,Completion completion){
        auto self=shared_from_this();auto result=std::make_shared<std::optional<T>>();auto delivered=std::make_shared<bool>(false);++operations;
        bool accepted{};
        try{accepted=executor.submit([self,result,action=std::move(action)]()mutable{*result=action();},[self,result,delivered,completion=std::move(completion)](std::exception_ptr failure)mutable{
            *delivered=true;
            need(std::this_thread::get_id()==self->thread,"Archive executor delivered outside owner thread");need(self->operations>0,"Archive operation accounting mismatch");--self->operations;
            if(!self->alive)return;const bool outer=self->completing;self->completing=true;
            try{completion(failure,std::move(*result));}catch(...){self->completing=outer;throw;}self->completing=outer;
            // Shared queue capacity has become available. Retry retained saves
            // only here, or at explicit flush/deadline; never a polling timer.
            if(!outer&&self->backpressured)self->flush(false);
        });}catch(...){if(!*delivered)--operations;throw;}
        if(!accepted){need(!*delivered,"Rejected Archive job delivered a completion");--operations;return false;}return true;
    }
    ArchiveEntry applyingRemovals(ArchiveEntry e)const{if(e.categoryID&&categoryRemovals.contains(*e.categoryID))e.categoryID.reset();return e;}
    ArchiveSummary applyingRemovals(ArchiveSummary e)const{if(e.categoryID&&categoryRemovals.contains(*e.categoryID))e.categoryID.reset();return e;}
    // Classified documents never yield a thumbnail (boundedSummaries/upsertSummary, :1178-1197).
    void bounded(std::vector<ArchiveSummary>values){std::size_t bytes{};for(auto&s:values){s.isClassified=classified.contains(s.id);if(s.isClassified)s.thumbnail.reset();const auto cost=archiveThumbnailCost(s.thumbnail);if(cost>archiveMaximumSummaryThumbnailBytes-bytes)s.thumbnail.reset();else bytes+=cost;}entries=std::move(values);thumbnailBytes=bytes;}
    void upsertSummary(const ArchiveEntry&e){const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto&s){return s.id==e.id;});if(found!=entries.end()){thumbnailBytes-=archiveThumbnailCost(found->thumbnail);entries.erase(found);}auto next=archiveSummary(e);next.isClassified=classified.contains(e.id);if(next.isClassified)next.thumbnail.reset();const auto cost=archiveThumbnailCost(next.thumbnail);if(cost>archiveMaximumSummaryThumbnailBytes-thumbnailBytes)next.thumbnail.reset();else thumbnailBytes+=cost;entries.insert(entries.begin(),std::move(next));}
    void upsertCategory(const ArchiveCategory&c){categories.erase(std::remove_if(categories.begin(),categories.end(),[&](const auto&v){return v.id==c.id;}),categories.end());categories.push_back(c);std::sort(categories.begin(),categories.end(),[](const auto&a,const auto&b){return a.created==b.created?a.id<b.id:a.created<b.created;});}
    void snapshot(ArchiveSnapshot value){need(value.entries.size()<=archiveMaximumEntries&&value.categories.size()<=archiveMaximumCategories,"Archive snapshot exceeds source limits");std::set<std::string,std::less<>>names,ids;
        for(const auto&c:value.categories){validateArchiveCategory(c,rules);need(ids.insert(c.id).second&&names.insert(rules.categoryNameKey(c.name)).second,"Repeated Archive category");}
        std::set<std::string,std::less<>>documents;for(const auto&s:value.entries){validateArchiveSummary(s,rules);need(documents.insert(s.id).second&&(!s.categoryID||ids.contains(*s.categoryID)),"Invalid Archive summary category/identity");}
        if(value.selected){validateArchiveEntry(*value.selected,rules);need(documents.contains(value.selected->id)&&(!value.selected->categoryID||ids.contains(*value.selected->categoryID)),"Invalid selected Archive entry");}
        categories.clear();for(auto&c:value.categories)if(!categoryRemovals.contains(c.id))categories.push_back(std::move(c));for(const auto&[id,c]:dirtyCategories){(void)id;upsertCategory(c);}applyPendingCategoryEdits();
        classified.clear();for(const auto&s:value.entries)if(s.isClassified)classified.insert(s.id);
        std::vector<ArchiveSummary>incoming;incoming.reserve(value.entries.size());for(auto&s:value.entries)if(!deleting.contains(s.id))incoming.push_back(applyingRemovals(std::move(s)));bounded(std::move(incoming));for(const auto&[id,p]:dirty){(void)id;upsertSummary(p.entry);}
        selected=value.selected&&!deleting.contains(value.selected->id)?std::optional(dirty.contains(value.selected->id)?dirty.at(value.selected->id).entry:applyingRemovals(std::move(*value.selected))):std::nullopt;readError.reset();changed();
    }
    void flush(bool retryFailed=true){
        if(!alive)return;if(flushing){retryFlush=true;return;}flushing=true;backpressured=false;deadline.reset();
        struct Finish{Impl&state;~Finish(){state.flushing=false;}}finish{*this};
        // Stage stable candidate IDs: injected synchronous fixtures may deliver
        // inline and mutate the maps, just as source's injected executors can.
        // Renames go first: they only target committed categories and may free
        // a name a queued creation then uses (ArchiveStore.swift:1038-1054).
        std::vector<std::pair<std::string,std::string>>renames;for(const auto&[id,name]:categoryRenames)if(!renamingCategories.contains(id)&&!dirtyCategories.contains(id)&&!categoryRemovals.contains(id)&&(retryFailed||!categoryErrors.contains(id)))renames.emplace_back(id,name);
        for(const auto&[id,name]:renames){renamingCategories.insert(id);
            const bool accepted=perform<int>([store=store,id,name]{store->renameCategory(id,name);return 0;},[self=shared_from_this(),id,name](auto failure,auto){self->renamingCategories.erase(id);if(self->categoryRemovals.contains(id)){self->changed();return;}
                if(!failure){if(const auto found=self->categoryRenames.find(id);found!=self->categoryRenames.end()&&found->second==name)self->categoryRenames.erase(found);self->categoryErrors.erase(id);self->events.push_back({"categoryRenamed",id});if(self->categoryRenames.contains(id))self->flush(false);}
                else self->categoryErrors[id]=message(failure);self->changed();});
            if(!accepted){renamingCategories.erase(id);backpressured=true;}
        }
        std::vector<std::string>categoryIDs;for(const auto&[id,c]:dirtyCategories){(void)c;if(!savingCategories.contains(id)&&!categoryRemovals.contains(id)&&(retryFailed||!categoryErrors.contains(id)))categoryIDs.push_back(id);}
        for(const auto&id:categoryIDs){if(!dirtyCategories.contains(id)||savingCategories.contains(id)||categoryRemovals.contains(id))continue;const auto category=dirtyCategories.at(id);savingCategories.insert(id);
            const bool accepted=perform<int>([store=store,category]{store->saveCategory(category);return 0;},[self=shared_from_this(),id](auto failure,auto){self->savingCategories.erase(id);
                if(!failure){self->dirtyCategories.erase(id);self->categoryErrors.erase(id);self->events.push_back({"categoryCreated",id});if(self->categoryRenames.contains(id)||self->viewedCategories.contains(id))self->flush(false);}
                else{if(!self->categoryRemovals.contains(id))self->categoryErrors[id]=message(failure);
                    // The retry inserts the newest name and badge state directly.
                    if(const auto found=self->dirtyCategories.find(id);found!=self->dirtyCategories.end()){if(const auto rename=self->categoryRenames.find(id);rename!=self->categoryRenames.end()){found->second.name=rename->second;self->categoryRenames.erase(rename);}if(self->viewedCategories.erase(id))found->second.isNew=false;}}
                self->changed();});
            if(!accepted){savingCategories.erase(id);backpressured=true;}
        }
        std::vector<std::string>viewed;for(const auto&id:viewedCategories)if(!markingViewed.contains(id)&&!dirtyCategories.contains(id)&&!categoryRemovals.contains(id))viewed.push_back(id);
        for(const auto&id:viewed){markingViewed.insert(id);
            const bool accepted=perform<int>([store=store,id]{store->markCategoryViewed(id);return 0;},[self=shared_from_this(),id](auto,auto){self->markingViewed.erase(id);self->viewedCategories.erase(id);});
            if(!accepted){markingViewed.erase(id);backpressured=true;}
        }
        std::vector<std::string>removalIDs;for(const auto&[id,affected]:categoryRemovals){(void)affected;if(!removingCategories.contains(id)&&(retryFailed||!categoryErrors.contains(id)))removalIDs.push_back(id);}
        for(const auto&id:removalIDs){if(!categoryRemovals.contains(id)||removingCategories.contains(id))continue;const auto affected=categoryRemovals.at(id);std::vector<ArchiveEntry>drafts;std::map<std::string,std::uint64_t,std::less<>>included;for(const auto&[document,p]:dirty)if(affected.contains(document)&&!deleting.contains(document)){drafts.push_back(p.entry);included[document]=p.revision;}removingCategories.insert(id);
            const bool accepted=perform<int>([store=store,id,drafts=std::move(drafts)]{store->deleteCategory(id,drafts);return 0;},[self=shared_from_this(),id,affected,included=std::move(included)](auto failure,auto){self->removingCategories.erase(id);if(!failure){self->categoryRemovals.erase(id);self->categoryErrors.erase(id);for(const auto&[document,revision]:included)if(!self->deleting.contains(document)){self->writeErrors.erase(document);if(self->dirty.contains(document)&&self->dirty.at(document).revision==revision)self->dirty.erase(document);if(self->unannounced.erase(document))self->events.push_back({"created",document});if(self->categoryChanges.contains(document)&&self->categoryChanges.at(document)<=revision)self->categoryChanges.erase(document);}self->events.push_back({"categoryDeleted",id});
                if(std::any_of(affected.begin(),affected.end(),[&](const auto&document){return self->dirty.contains(document)&&!self->deleting.contains(document);}))self->flush(false);
            }else self->categoryErrors[id]=message(failure);self->changed();});
            if(!accepted){removingCategories.erase(id);backpressured=true;}
        }
        std::set<std::string,std::less<>>blocked;for(const auto&[id,documents]:categoryRemovals){(void)id;blocked.insert(documents.begin(),documents.end());}
        std::vector<std::string>documentIDs;for(const auto&[id,p]:dirty){(void)p;if(!saving.contains(id)&&!deleting.contains(id)&&!blocked.contains(id)&&(retryFailed||!writeErrors.contains(id)))documentIDs.push_back(id);}
        for(const auto&id:documentIDs){if(!dirty.contains(id)||saving.contains(id)||deleting.contains(id))continue;const auto pending=dirty.at(id);saving[id]=pending.revision;
            const bool accepted=perform<int>([store=store,entry=pending.entry]{store->save(entry);return 0;},[self=shared_from_this(),id,revision=pending.revision](auto failure,auto){self->saving.erase(id);if(self->deleting.contains(id))return;if(!failure){self->writeErrors.erase(id);if(self->dirty.contains(id)&&self->dirty.at(id).revision==revision)self->dirty.erase(id);if(self->unannounced.erase(id))self->events.push_back({"created",id});if(self->categoryChanges.contains(id)&&revision>=self->categoryChanges.at(id)){self->categoryChanges.erase(id);self->events.push_back({"categoryChanged",id});}}else self->writeErrors[id]=message(failure);const bool newer=self->dirty.contains(id)&&self->dirty.at(id).revision!=revision;if(newer){self->writeErrors.erase(id);self->flush(false);}self->changed();});
            if(!accepted){saving.erase(id);backpressured=true;}
        }
        if(pendingCreatedSelection&&!savingSelection&&(retryFailed||!selectionFailed)){
            const auto id=*pendingCreatedSelection;const auto token=selectionRevision;savingSelection=token;
            const bool accepted=perform<int>([store=store,id]{store->select(id);return 0;},[self=shared_from_this(),token](auto failure,auto){self->savingSelection=0;if(self->selectionRevision!=token){if(self->pendingCreatedSelection)self->flush(false);return;}
                if(failure){self->selectionFailed=true;self->readError=message(failure);}else{self->pendingCreatedSelection.reset();if(self->selectionFailed)self->readError.reset();self->selectionFailed=false;}self->changed();});
            if(!accepted){savingSelection=0;backpressured=true;}
        }
        // Inline fixture completions may request another pass while this pass
        // owns map iterators. The just-staged candidates already cover those
        // maps; pending work is retried by the next completion/explicit flush.
        retryFlush=false;
    }
};
namespace {[[noreturn]]void unsupported(){throw ehud::data::StoreError(ehud::data::StoreErrorCode::unavailable,"The archive could not be saved.");}}
void ArchiveRepository::renameCategory(std::string_view,std::string_view){unsupported();}
void ArchiveRepository::markCategoryViewed(std::string_view){unsupported();}
bool ArchiveRepository::isClassified(std::string_view){return false;}
ArchiveClassificationStatus ArchiveRepository::classificationStatus(std::string_view){return ArchiveClassificationStatus::unclassified;}
void ArchiveRepository::classify(std::string_view,std::string_view){unsupported();}
bool ArchiveRepository::verify(std::string_view,std::string_view){return true;}
void ArchiveRepository::declassify(std::string_view){}
void ArchiveRepository::recoverClassification(std::string_view,const std::optional<std::string>&){unsupported();}
ArchiveState::ArchiveState(std::shared_ptr<ArchiveRepository>repository,ArchiveExecutor executor,ArchiveTextRules rules):impl_(std::make_shared<Impl>(std::move(repository),std::move(executor),std::move(rules))){}
ArchiveState::~ArchiveState(){auto&i=*impl_;if(std::this_thread::get_id()!=i.thread)std::terminate();i.alive=false;i.active=false;i.thumbnailRequests.clear();}
void ArchiveState::activate(){auto&i=*impl_;i.onThread();i.active=true;i.invalidate();const auto token=i.generation;
    struct Loaded{ArchiveSnapshot snapshot;std::optional<ArchiveClassificationStatus>status;};
    const bool accepted=i.perform<Loaded>([store=i.store]{Loaded value;value.snapshot.entries=store->summaries();const auto id=store->selection();value.snapshot.selected=id?store->entry(*id):std::nullopt;value.snapshot.categories=store->categories();
        if(value.snapshot.selected&&std::any_of(value.snapshot.entries.begin(),value.snapshot.entries.end(),[&](const auto&s){return s.id==value.snapshot.selected->id&&s.isClassified;}))value.status=store->classificationStatus(value.snapshot.selected->id);return value;},
        [self=impl_,token](auto failure,auto result){if(self->generation!=token)return;if(failure)self->readError=message(failure);else if(result){try{const auto id=result->snapshot.selected?std::optional(result->snapshot.selected->id):std::nullopt;self->snapshot(std::move(result->snapshot));if(id&&result->status)self->record(*result->status,*id);self->changed();return;}catch(...){self->readError=message(std::current_exception());}}self->changed();});if(!accepted){i.readError="Archive file queue is busy. Retry after pending work completes.";i.changed();}
    // Once per state, behind the first load: the Mac import relink ledger
    // against the stored entries (a relink whose ledger write was lost).
    else if(i.ledger&&i.ledger->reconcile&&!i.reconciled)i.reconciled=i.perform<int>([store=i.store,ledger=i.ledger]{ledger->reconcile(*store);return 0;},[](auto,auto){});
}
void ArchiveState::deactivate(){auto&i=*impl_;i.onThread();i.active=false;i.relock();i.flush();i.invalidate();i.selected.reset();i.changed();}
bool ArchiveState::active()const noexcept{return impl_->active;}bool ArchiveState::busy()const noexcept{return impl_->operations>0;}
bool ArchiveState::hasUnsavedChanges()const noexcept{const auto&i=*impl_;return !i.dirty.empty()||!i.dirtyCategories.empty()||!i.categoryRenames.empty()||!i.categoryRemovals.empty()||!i.deleting.empty()||i.pendingCreatedSelection.has_value();}
std::span<const ArchiveCategory>ArchiveState::categories()const noexcept{return impl_->categories;}std::span<const ArchiveSummary>ArchiveState::entries()const noexcept{return impl_->entries;}
const std::optional<ArchiveEntry>&ArchiveState::selected()const noexcept{return impl_->selected;}const std::optional<std::string>&ArchiveState::error()const noexcept{return impl_->error;}
std::uint64_t ArchiveState::revision()const noexcept{return impl_->revision;}std::uint64_t ArchiveState::collectionRevision()const noexcept{return impl_->collectionRevision;}
std::vector<ArchiveStateEvent>ArchiveState::takeEvents(){impl_->onThread();return std::exchange(impl_->events,{});}
void ArchiveState::recordAction(std::string name,std::string id){auto&i=*impl_;i.onThread();need(name=="edited"||name=="mediaAdded"||name=="mediaRemoved","Unknown Archive interaction action");i.events.push_back({std::move(name),std::move(id)});}
void ArchiveState::setRelinkLedger(ArchiveRelinkLedger ledger){auto&i=*impl_;i.onThread();i.ledger=std::make_shared<const ArchiveRelinkLedger>(std::move(ledger));}
// The entry's save is queued first (FIFO), so the ledger job reads what it wrote.
bool ArchiveState::mediaRelinked(std::string id,ArchiveJson mac,std::string path){auto&i=*impl_;i.onThread();if(!i.ledger||!i.ledger->relinked)return false;i.flush();
    return i.perform<int>([store=i.store,ledger=i.ledger,id=std::move(id),mac=std::move(mac),path=std::move(path)]{const auto entry=store->entry(id);if(entry&&holdsWindowsPath(*entry,path)&&!holdsMacMedia(*entry,mac))ledger->relinked(id,mac,path);return 0;},[](auto,auto){});}
bool ArchiveState::mediaRemoved(std::string id,ArchiveJson mac){auto&i=*impl_;i.onThread();if(!i.ledger||!i.ledger->removed)return false;i.flush();
    return i.perform<int>([store=i.store,ledger=i.ledger,id=std::move(id),mac=std::move(mac)]{const auto entry=store->entry(id);if(!entry||!holdsMacMedia(*entry,mac))ledger->removed(id,&mac);return 0;},[](auto,auto){});}
void ArchiveState::select(std::optional<std::string>id){auto&i=*impl_;i.onThread();need(!id||ehud::data::validUUID(*id),"Invalid Archive selection");if(id&&i.deleting.contains(*id))return;if(!id||id!=i.unlockedID)i.relock();i.flush();i.pendingCreatedSelection.reset();++i.selectionRevision;i.selectionFailed=false;i.invalidate();const auto token=i.generation;auto outgoing=i.selected;if(!id)i.selected.reset();i.changed();
    struct Opened{std::optional<ArchiveEntry>entry;std::optional<ArchiveClassificationStatus>status;};
    const bool accepted=i.perform<Opened>([store=i.store,id]{Opened value;value.entry=id?store->entry(*id):std::nullopt;store->select(id);if(value.entry)value.status=store->classificationStatus(value.entry->id);return value;},[self=impl_,id,token,outgoing=std::move(outgoing)](auto failure,auto result){if(self->generation!=token)return;
        if(!failure&&result){if(result->entry){validateArchiveEntry(*result->entry,self->rules);need(id&&result->entry->id==*id&&self->categoryExists(result->entry->categoryID),"Mismatched Archive selection payload");if(result->status)self->record(*result->status,*id);}self->selected=id?(self->dirty.contains(*id)?std::optional(self->dirty.at(*id).entry):(result->entry?std::optional(self->applyingRemovals(std::move(*result->entry))):std::nullopt)):std::nullopt;self->readError.reset();}
        else {const auto retained=outgoing?(self->selected&&self->selected->id==outgoing->id?self->selected:(self->dirty.contains(outgoing->id)?std::optional(self->dirty.at(outgoing->id).entry):outgoing)):std::nullopt;self->selected=id?(self->dirty.contains(*id)?std::optional(self->dirty.at(*id).entry):retained):std::nullopt;self->readError=message(failure);}self->changed();});if(!accepted){i.readError="Archive file queue is busy. Retry after pending work completes.";i.changed();}
}
bool ArchiveState::create(std::string id,double date,std::optional<std::string>category){auto&i=*impl_;i.onThread();need(ehud::data::validUUID(id)&&std::isfinite(date),"Invalid new Archive identity/date");if(!i.categoryExists(category)){i.readError="Invalid Archive category.";i.changed();return false;}if(i.entries.size()>=archiveMaximumEntries){i.readError="The archive is full.";i.changed();return false;}if(!i.writeErrors.empty()||!i.categoryErrors.empty())return false;need(std::none_of(i.entries.begin(),i.entries.end(),[&](const auto&e){return e.id==id;}),"Repeated new Archive identity");i.relock();i.flush();i.invalidate();ArchiveEntry entry;entry.id=id;entry.date=entry.modified=date;entry.categoryID=std::move(category);i.selected=entry;i.unannounced.insert(id);i.dirty[id]={entry,++i.dirtyRevision};i.upsertSummary(entry);i.pendingCreatedSelection=id;++i.selectionRevision;i.selectionFailed=false;i.flush();i.changed();return true;
}
std::optional<std::string>ArchiveState::createCategory(std::string id,std::string name,double date){auto&i=*impl_;i.onThread();ArchiveCategory category{std::move(id),i.rules.trimmed(name),date,{},true};validateArchiveCategory(category,i.rules);const auto key=i.rules.categoryNameKey(category.name);for(const auto&c:i.categories)if(i.rules.categoryNameKey(c.name)==key)return c.id;return i.insertDraftCategory(std::move(category));
}
std::string ArchiveState::nextCategoryName(core::Language language)const{const auto&i=*impl_;for(auto number=i.categories.size()+1;;++number){const auto n=std::to_string(number);const std::string_view arguments[]{n};auto name=core::localized("Category {0}","分类 {0}",language,arguments);
    if(!archiveCategoryNameIssue(name,i.categories,i.rules)||number>2*archiveMaximumCategories)return name;}}
std::optional<std::string>ArchiveState::addCategory(std::string id,double date,core::Language language){auto&i=*impl_;i.onThread();ArchiveCategory category{std::move(id),nextCategoryName(language),date,{},true};validateArchiveCategory(category,i.rules);return i.insertDraftCategory(std::move(category));}
std::optional<ArchiveCategoryNameIssue>ArchiveState::categoryNameIssue(std::string_view name,const std::optional<std::string>&renaming)const{return archiveCategoryNameIssue(name,impl_->categories,impl_->rules,renaming);}
ArchiveState::RenameOutcome ArchiveState::renameCategory(std::string_view id,std::string_view proposed){auto&i=*impl_;i.onThread();const auto found=std::find_if(i.categories.begin(),i.categories.end(),[&](const auto&c){return c.id==id;});
    if(found==i.categories.end()||i.categoryRemovals.contains(id))return {ArchiveCategoryRenameResult::rejected,ArchiveCategoryNameIssue::unknownCategory};
    auto name=i.rules.trimmed(proposed);if(name==found->name)return {ArchiveCategoryRenameResult::unchanged,{}};
    const std::string key(id);if(const auto issue=archiveCategoryNameIssue(name,i.categories,i.rules,key))return {ArchiveCategoryRenameResult::rejected,issue};
    auto value=*found;value.name=name;i.upsertCategory(value);
    if(const auto draft=i.dirtyCategories.find(key);draft!=i.dirtyCategories.end()&&!i.savingCategories.contains(key))draft->second.name=std::move(name);else i.categoryRenames[key]=std::move(name);
    i.flush();i.changed();return {ArchiveCategoryRenameResult::renamed,{}};}
void ArchiveState::markCategoryViewed(std::string_view id){auto&i=*impl_;i.onThread();const auto found=std::find_if(i.categories.begin(),i.categories.end(),[&](const auto&c){return c.id==id;});if(found==i.categories.end()||!found->isNew)return;found->isNew=false;
    const std::string key(id);if(const auto draft=i.dirtyCategories.find(key);draft!=i.dirtyCategories.end()&&!i.savingCategories.contains(key))draft->second.isNew=false;else i.viewedCategories.insert(key);i.flush();i.changed();}
bool ArchiveState::isClassified(std::string_view id)const noexcept{return impl_->classified.contains(id);}
ArchiveClassificationStatus ArchiveState::selectedClassification()const noexcept{const auto&i=*impl_;if(!i.selected||!i.classified.contains(i.selected->id))return ArchiveClassificationStatus::unclassified;const auto found=i.verifierProblems.find(i.selected->id);return found==i.verifierProblems.end()?ArchiveClassificationStatus::protectedByPassword:found->second;}
bool ArchiveState::isSelectedLocked()const noexcept{const auto&i=*impl_;return i.selected&&i.isLocked(i.selected->id);}
bool ArchiveState::isClassificationBusy()const noexcept{const auto&i=*impl_;return i.unlocking||!i.classificationWork.empty();}
namespace {std::exception_ptr refused(){return std::make_exception_ptr(ehud::data::StoreError(ehud::data::StoreErrorCode::invalid,"The archive could not be read. The original was preserved."));}}
void ArchiveState::classifySelected(std::string password,Completion completion){auto&i=*impl_;i.onThread();
    if(!i.selected||i.deleting.contains(i.selected->id)||i.classified.contains(i.selected->id)||i.classificationWork.contains(i.selected->id)||archivePasswordIssue(password,std::nullopt,i.rules)){if(completion)completion(refused());return;}
    const auto id=i.selected->id;i.flush(); // FIFO: the pending document save commits before the flag.
    i.classificationWork.insert(id);i.changed();
    const bool accepted=i.perform<int>([store=i.store,id,password=std::move(password)]{store->classify(id,password);return 0;},[self=impl_,id,completion](auto failure,auto){self->classificationWork.erase(id);
        if(!failure){self->classified.insert(id);self->verifierProblems.erase(id);if(self->unlockedID==id)self->relock();self->refreshSummary(id);self->events.push_back({"classified",id});}self->changed();if(completion)completion(failure);});
    if(!accepted){i.classificationWork.erase(id);i.changed();if(completion)completion(std::make_exception_ptr(ehud::data::StoreError(ehud::data::StoreErrorCode::unavailable,"Archive file queue is busy. Retry after pending work completes.")));}}
void ArchiveState::unlockSelected(std::string password,std::function<void(ArchiveUnlockResult,std::string)>completion){auto&i=*impl_;i.onThread();need(bool(completion),"Archive unlock needs a completion");
    if(!i.selected){completion(ArchiveUnlockResult::ignored,{});return;}const auto id=i.selected->id;if(!i.isLocked(id)){completion(ArchiveUnlockResult::unlocked,{});return;}
    if(i.unlocking||i.classificationWork.contains(id)){completion(ArchiveUnlockResult::ignored,{});return;}
    const auto token=i.visit;i.unlocking=id;i.changed();
    const bool accepted=i.perform<bool>([store=i.store,id,password=std::move(password)]{return store->verify(id,password);},[self=impl_,id,token,completion](auto failure,auto result){self->unlocking.reset();ArchiveUnlockResult outcome;std::string text;
        if(!failure&&result){if(*result)self->verifierProblems.erase(id);outcome=!*result?ArchiveUnlockResult::wrongPassword:self->visit==token&&self->selected&&self->selected->id==id?ArchiveUnlockResult::unlocked:ArchiveUnlockResult::ignored;if(outcome==ArchiveUnlockResult::unlocked)self->unlockedID=id;}
        else{std::optional<ArchiveClassificationErrorCode>code;try{if(failure)std::rethrow_exception(failure);}catch(const ArchiveClassificationError&e){code=e.code();}catch(...){}
            if(code==ArchiveClassificationErrorCode::passwordMissing)self->verifierProblems[id]=ArchiveClassificationStatus::passwordMissing;if(code==ArchiveClassificationErrorCode::vaultUnavailable)self->verifierProblems[id]=ArchiveClassificationStatus::vaultUnavailable;
            outcome=code==ArchiveClassificationErrorCode::passwordMissing?ArchiveUnlockResult::passwordMissing:ArchiveUnlockResult::failed;if(outcome==ArchiveUnlockResult::failed)text=message(failure);}
        self->changed();completion(outcome,std::move(text));});
    if(!accepted){i.unlocking.reset();i.changed();completion(ArchiveUnlockResult::failed,"Archive file queue is busy. Retry after pending work completes.");}}
void ArchiveState::lockSelected(){auto&i=*impl_;i.onThread();i.relock();i.flush();i.changed();}
void ArchiveState::declassifySelected(Completion completion){auto&i=*impl_;i.onThread();
    if(!i.selected||!i.classified.contains(i.selected->id)||i.unlockedID!=i.selected->id||i.classificationWork.contains(i.selected->id)){if(completion)completion(refused());return;}
    const auto id=i.selected->id;i.classificationWork.insert(id);i.changed();
    const bool accepted=i.perform<int>([store=i.store,id]{store->declassify(id);return 0;},[self=impl_,id,completion](auto failure,auto){self->classificationWork.erase(id);if(!failure)self->markDeclassified(id);self->changed();if(completion)completion(failure);});
    if(!accepted){i.classificationWork.erase(id);i.changed();if(completion)completion(std::make_exception_ptr(ehud::data::StoreError(ehud::data::StoreErrorCode::unavailable,"Archive file queue is busy. Retry after pending work completes.")));}}
void ArchiveState::refreshSelectedClassification(){impl_->onThread();impl_->refreshSelectedClassification();}
void ArchiveState::setOwnerAuthentication(OwnerAuthentication value){auto&i=*impl_;i.onThread();i.ownerAuthentication=std::move(value);}
void ArchiveState::recoverSelectedClassification(std::optional<std::string>password,std::string reason,Completion completion){auto&i=*impl_;i.onThread();need(bool(completion),"Archive recovery needs a completion");
    if(!i.selected||!i.classified.contains(i.selected->id)||!i.verifierProblems.contains(i.selected->id)||i.classificationWork.contains(i.selected->id)||i.unlocking||!i.ownerAuthentication||(password&&archivePasswordIssue(*password,std::nullopt,i.rules))){completion(refused());return;}
    const auto id=i.selected->id;const auto token=i.visit;i.classificationWork.insert(id);i.changed();const std::weak_ptr<Impl>weak=impl_;
    i.ownerAuthentication(std::move(reason),[weak,id,token,password=std::move(password),completion](bool approved)mutable{const auto self=weak.lock();if(!self||!self->alive)return;self->onThread();
        if(!approved||self->visit!=token||!self->selected||self->selected->id!=id){self->classificationWork.erase(id);self->changed();completion(std::make_exception_ptr(ArchiveClassificationError(ArchiveClassificationErrorCode::cancelled)));return;}
        const bool declassify=!password;
        const bool accepted=self->perform<int>([store=self->store,id,password=std::move(password)]{store->recoverClassification(id,password);return 0;},[self,id,token,declassify,completion](auto failure,auto){self->classificationWork.erase(id);
            if(!failure){if(declassify)self->markDeclassified(id);else{self->verifierProblems.erase(id);self->events.push_back({"classificationRecovered",id});if(self->visit==token&&self->selected&&self->selected->id==id)self->unlockedID=id;}}
            else self->refreshSelectedClassification();
            self->changed();completion(failure);});
        if(!accepted){self->classificationWork.erase(id);self->changed();completion(std::make_exception_ptr(ehud::data::StoreError(ehud::data::StoreErrorCode::unavailable,"Archive file queue is busy. Retry after pending work completes.")));}});}
bool ArchiveState::update(ArchiveEntry entry,double modified,double time){auto&i=*impl_;i.onThread();time=i.at(time);validateArchiveEntry(entry,i.rules);need(std::isfinite(modified),"Invalid Archive modified date");if(!i.selected||i.selected->id!=entry.id||i.deleting.contains(entry.id)||i.isLocked(entry.id)||!i.categoryExists(entry.categoryID))return false;entry.modified=modified;i.selected=entry;i.dirty[entry.id]={entry,++i.dirtyRevision};i.upsertSummary(entry);i.deadline=time+.35;i.changed();return true;}
bool ArchiveState::moveSelected(std::optional<std::string>id,double modified,double time){auto&i=*impl_;i.onThread();if(!i.selected||i.isLocked(i.selected->id)||i.selected->categoryID==id||!i.categoryExists(id))return false;auto entry=*i.selected;entry.categoryID=std::move(id);if(!update(std::move(entry),modified,time))return false;i.categoryChanges[i.selected->id]=i.dirtyRevision;i.flush();i.changed();return true;}
void ArchiveState::flush(){impl_->onThread();impl_->flush();}void ArchiveState::retryPendingSaves(){auto&i=*impl_;i.onThread();i.readError.reset();i.flush();i.changed();}
void ArchiveState::queueCapacityAvailable(){auto&i=*impl_;i.onThread();if(i.backpressured)i.flush(false);}
std::optional<double>ArchiveState::saveDeadline()const noexcept{return impl_->deadline;}
bool ArchiveState::flushIfDue(double t){auto&i=*impl_;i.onThread();t=i.at(t);if(!i.deadline||t<*i.deadline)return false;i.flush();return true;}
// Locked documents cannot be deleted; unlock (or recover) first.
bool ArchiveState::deleteSelected(){auto&i=*impl_;i.onThread();if(!i.selected||i.deleting.contains(i.selected->id)||i.isLocked(i.selected->id))return false;const auto entry=*i.selected;const auto id=entry.id;std::optional<Impl::Pending>pending;if(i.dirty.contains(id)){pending=i.dirty.at(id);i.dirty.erase(id);}i.deadline.reset();i.deleting.insert(id);i.relock();i.invalidate();i.selected.reset();const auto found=std::find_if(i.entries.begin(),i.entries.end(),[&](const auto&s){return s.id==id;});if(found!=i.entries.end()){i.thumbnailBytes-=archiveThumbnailCost(found->thumbnail);i.entries.erase(found);}i.changed();
    const bool accepted=i.perform<int>([store=i.store,id,ledger=i.ledger]{store->remove(id);if(ledger&&ledger->removed)ledger->removed(id,nullptr);return 0;},[self=impl_,entry,pending](auto failure,auto){self->deleting.erase(entry.id);if(!failure){self->writeErrors.erase(entry.id);self->unannounced.erase(entry.id);self->categoryChanges.erase(entry.id);self->classified.erase(entry.id);self->verifierProblems.erase(entry.id);self->events.push_back({"deleted",entry.id});}else {if(pending)self->dirty[entry.id]=*pending;self->upsertSummary(entry);self->writeErrors[entry.id]=message(failure);}self->changed();});if(!accepted){i.deleting.erase(id);if(pending)i.dirty[id]=*pending;i.upsertSummary(entry);i.writeErrors[id]="Archive file queue is busy. Retry deletion when capacity is available.";i.changed();return false;}return true;
}
bool ArchiveState::deleteCategory(std::string_view value){auto&i=*impl_;i.onThread();const std::string id(value);if(i.categoryRemovals.contains(id)||!i.categoryExists(std::optional(id)))return false;std::set<std::string,std::less<>>affected;for(const auto&s:i.entries)if(s.categoryID==id)affected.insert(s.id);for(const auto&[document,p]:i.dirty)if(p.entry.categoryID==id)affected.insert(document);if(i.selected&&i.selected->categoryID==id)affected.insert(i.selected->id);i.categoryRemovals[id]=affected;i.categories.erase(std::remove_if(i.categories.begin(),i.categories.end(),[&](const auto&c){return c.id==id;}),i.categories.end());i.dirtyCategories.erase(id);i.categoryErrors.erase(id);i.categoryRenames.erase(id);i.viewedCategories.erase(id);
    for(const auto&document:affected){std::optional<ArchiveEntry>entry=i.dirty.contains(document)?std::optional(i.dirty.at(document).entry):(i.selected&&i.selected->id==document?i.selected:std::nullopt);if(entry){entry->categoryID.reset();i.dirty[document]={*entry,++i.dirtyRevision};if(i.selected&&i.selected->id==document)i.selected=*entry;}}
    for(auto&s:i.entries)s=i.applyingRemovals(std::move(s));i.flush();i.changed();return true;
}
void ArchiveState::loadThumbnail(std::string id,std::function<void(std::optional<ArchiveJson>)>callback){auto&i=*impl_;i.onThread();need(ehud::data::validUUID(id)&&bool(callback),"Invalid Archive thumbnail request");if(!i.active||i.classified.contains(id)){callback({});return;}if(i.dirty.contains(id)){callback(archiveSummary(i.dirty.at(id).entry).thumbnail);return;}if(i.thumbnailRequests.contains(id)){auto&callbacks=i.thumbnailRequests.at(id).callbacks;if(callbacks.size()>=8)callback({});else callbacks.push_back(std::move(callback));return;}if(i.thumbnailRequests.size()>=32){callback({});return;}const auto token=++i.thumbnailToken;i.thumbnailRequests[id]={token,{std::move(callback)}};
    const bool accepted=i.perform<std::optional<ArchiveJson>>([store=i.store,id]{return store->thumbnail(id);},[self=impl_,id,token](auto failure,auto result){const auto found=self->thumbnailRequests.find(id);if(found==self->thumbnailRequests.end()||found->second.token!=token)return;auto callbacks=std::move(found->second.callbacks);self->thumbnailRequests.erase(found);std::optional<ArchiveJson>value;if(!failure&&self->active&&!self->classified.contains(id)&&result)value=std::move(*result);if(value)validateArchiveMedia(*value);for(auto&callback:callbacks)callback(value);});if(!accepted){auto callbacks=std::move(i.thumbnailRequests.at(id).callbacks);i.thumbnailRequests.erase(id);for(auto&c:callbacks)c({});}
}
const ArchiveTextRules&ArchiveState::textRules()const noexcept{return impl_->rules;}
} // namespace endfield::modules
