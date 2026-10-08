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
bool contains(core::Rect r,core::Point p){return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;}
std::optional<core::Rect>intersect(core::Rect a,core::Rect b){const auto x=std::max(a.x,b.x),y=std::max(a.y,b.y),right=std::min(a.x+a.width,b.x+b.width),bottom=std::min(a.y+a.height,b.y+b.height);if(right<=x||bottom<=y)return {};return core::Rect{x,y,right-x,bottom-y};}
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
    std::set<std::string,std::less<>>savingCategories,deleting,unannounced,removingCategories;
    std::map<std::string,std::set<std::string,std::less<>>,std::less<>>categoryRemovals;
    std::map<std::string,std::uint64_t,std::less<>>categoryChanges;
    std::map<std::string,std::string,std::less<>>writeErrors,categoryErrors;
    std::optional<std::string>readError,error;std::optional<double>deadline,lastTime;
    std::optional<std::string>pendingCreatedSelection;std::uint64_t selectionRevision{},savingSelection{};bool selectionFailed{};
    std::map<std::string,Thumbnail,std::less<>>thumbnailRequests;
    std::vector<ArchiveStateEvent>events;std::vector<std::size_t>filtered;
    std::optional<std::string>categoryID;bool uncategorized{};double galleryScroll{},categoryScroll{};
    Impl(std::shared_ptr<ArchiveRepository>s,ArchiveExecutor e,ArchiveTextRules r):store(std::move(s)),executor(std::move(e)),rules(std::move(r)){need(store&&executor.submit&&rules.characters&&rules.trimmed&&rules.categoryNameKey,"Archive requires repository, FIFO executor and Unicode rules");filtered.reserve(archiveMaximumEntries);}
    void onThread()const{need(std::this_thread::get_id()==thread,"Archive state belongs to its creating thread");need(alive,"Archive state is detached");}
    bool categoryExists(const std::optional<std::string>&id)const{return !id||std::any_of(categories.begin(),categories.end(),[&](const auto&c){return c.id==*id;});}
    void time(double t){need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Archive state requires a finite monotonic owner clock");lastTime=t;}
    void changed(){error=!writeErrors.empty()?std::optional(writeErrors.begin()->second):!categoryErrors.empty()?std::optional(categoryErrors.begin()->second):readError;++revision;rebuildFiltered();}
    void rebuildFiltered(){filtered.clear();for(std::size_t i=0;i<entries.size();++i)if(uncategorized?!entries[i].categoryID:(!categoryID||entries[i].categoryID==categoryID))filtered.push_back(i);
        if(categoryID&&!categoryExists(categoryID)){categoryID.reset();uncategorized=true;galleryScroll=0;filtered.clear();for(std::size_t i=0;i<entries.size();++i)if(!entries[i].categoryID)filtered.push_back(i);}
        galleryScroll=std::clamp(galleryScroll,0.,galleryMaximum());categoryScroll=std::clamp(categoryScroll,0.,categoryMaximum());++collectionRevision;
    }
    double galleryMaximum()const{return std::max(0.,double((filtered.size()+1)/2)*122-9-357);}
    double categoryMaximum()const{return std::max(0.,double(categories.size()+2)*34-7-238);}
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
    void bounded(std::vector<ArchiveSummary>values){std::size_t bytes{};for(auto&s:values){const auto cost=archiveThumbnailCost(s.thumbnail);if(cost>archiveMaximumSummaryThumbnailBytes-bytes)s.thumbnail.reset();else bytes+=cost;}entries=std::move(values);thumbnailBytes=bytes;}
    void upsertSummary(const ArchiveEntry&e){const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto&s){return s.id==e.id;});if(found!=entries.end()){thumbnailBytes-=archiveThumbnailCost(found->thumbnail);entries.erase(found);}auto next=archiveSummary(e);const auto cost=archiveThumbnailCost(next.thumbnail);if(cost>archiveMaximumSummaryThumbnailBytes-thumbnailBytes)next.thumbnail.reset();else thumbnailBytes+=cost;entries.insert(entries.begin(),std::move(next));}
    void upsertCategory(const ArchiveCategory&c){categories.erase(std::remove_if(categories.begin(),categories.end(),[&](const auto&v){return v.id==c.id;}),categories.end());categories.push_back(c);std::sort(categories.begin(),categories.end(),[](const auto&a,const auto&b){return a.created==b.created?a.id<b.id:a.created<b.created;});}
    void snapshot(ArchiveSnapshot value){need(value.entries.size()<=archiveMaximumEntries&&value.categories.size()<=archiveMaximumCategories,"Archive snapshot exceeds source limits");std::set<std::string,std::less<>>names,ids;
        for(const auto&c:value.categories){validateArchiveCategory(c,rules);need(ids.insert(c.id).second&&names.insert(rules.categoryNameKey(c.name)).second,"Repeated Archive category");}
        std::set<std::string,std::less<>>documents;for(const auto&s:value.entries){validateArchiveSummary(s,rules);need(documents.insert(s.id).second&&(!s.categoryID||ids.contains(*s.categoryID)),"Invalid Archive summary category/identity");}
        if(value.selected){validateArchiveEntry(*value.selected,rules);need(documents.contains(value.selected->id)&&(!value.selected->categoryID||ids.contains(*value.selected->categoryID)),"Invalid selected Archive entry");}
        categories.clear();for(auto&c:value.categories)if(!categoryRemovals.contains(c.id))categories.push_back(std::move(c));for(const auto&[id,c]:dirtyCategories){(void)id;upsertCategory(c);}
        std::vector<ArchiveSummary>incoming;incoming.reserve(value.entries.size());for(auto&s:value.entries)if(!deleting.contains(s.id))incoming.push_back(applyingRemovals(std::move(s)));bounded(std::move(incoming));for(const auto&[id,p]:dirty){(void)id;upsertSummary(p.entry);}
        selected=value.selected&&!deleting.contains(value.selected->id)?std::optional(dirty.contains(value.selected->id)?dirty.at(value.selected->id).entry:applyingRemovals(std::move(*value.selected))):std::nullopt;readError.reset();changed();
    }
    void flush(bool retryFailed=true){
        if(!alive)return;if(flushing){retryFlush=true;return;}flushing=true;backpressured=false;deadline.reset();
        struct Finish{Impl&state;~Finish(){state.flushing=false;}}finish{*this};
        // Stage stable candidate IDs: injected synchronous fixtures may deliver
        // inline and mutate the maps, just as source's injected executors can.
        std::vector<std::string>categoryIDs;for(const auto&[id,c]:dirtyCategories){(void)c;if(!savingCategories.contains(id)&&!categoryRemovals.contains(id)&&(retryFailed||!categoryErrors.contains(id)))categoryIDs.push_back(id);}
        for(const auto&id:categoryIDs){if(!dirtyCategories.contains(id)||savingCategories.contains(id)||categoryRemovals.contains(id))continue;const auto category=dirtyCategories.at(id);savingCategories.insert(id);
            const bool accepted=perform<int>([store=store,category]{store->saveCategory(category);return 0;},[self=shared_from_this(),id](auto failure,auto){self->savingCategories.erase(id);if(!failure){self->dirtyCategories.erase(id);self->categoryErrors.erase(id);self->events.push_back({"categoryCreated",id});}else if(!self->categoryRemovals.contains(id))self->categoryErrors[id]=message(failure);self->changed();});
            if(!accepted){savingCategories.erase(id);backpressured=true;}
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
ArchiveState::ArchiveState(std::shared_ptr<ArchiveRepository>repository,ArchiveExecutor executor,ArchiveTextRules rules):impl_(std::make_shared<Impl>(std::move(repository),std::move(executor),std::move(rules))){}
ArchiveState::~ArchiveState(){auto&i=*impl_;if(std::this_thread::get_id()!=i.thread)std::terminate();i.alive=false;i.active=false;i.thumbnailRequests.clear();}
void ArchiveState::activate(){auto&i=*impl_;i.onThread();i.active=true;i.invalidate();const auto token=i.generation;
    const bool accepted=i.perform<ArchiveSnapshot>([store=i.store]{ArchiveSnapshot value;value.entries=store->summaries();const auto id=store->selection();value.selected=id?store->entry(*id):std::nullopt;value.categories=store->categories();return value;},[self=impl_,token](auto failure,auto result){if(self->generation!=token)return;if(failure)self->readError=message(failure);else if(result){try{self->snapshot(std::move(*result));return;}catch(...){self->readError=message(std::current_exception());}}self->changed();});if(!accepted){i.readError="Archive file queue is busy. Retry after pending work completes.";i.changed();}
}
void ArchiveState::deactivate(){auto&i=*impl_;i.onThread();i.active=false;i.flush();i.invalidate();i.selected.reset();i.changed();}
bool ArchiveState::active()const noexcept{return impl_->active;}bool ArchiveState::busy()const noexcept{return impl_->operations>0;}
bool ArchiveState::hasUnsavedChanges()const noexcept{const auto&i=*impl_;return !i.dirty.empty()||!i.dirtyCategories.empty()||!i.categoryRemovals.empty()||!i.deleting.empty()||i.pendingCreatedSelection.has_value();}
std::span<const ArchiveCategory>ArchiveState::categories()const noexcept{return impl_->categories;}std::span<const ArchiveSummary>ArchiveState::entries()const noexcept{return impl_->entries;}
const std::optional<ArchiveEntry>&ArchiveState::selected()const noexcept{return impl_->selected;}const std::optional<std::string>&ArchiveState::error()const noexcept{return impl_->error;}
std::uint64_t ArchiveState::revision()const noexcept{return impl_->revision;}std::uint64_t ArchiveState::collectionRevision()const noexcept{return impl_->collectionRevision;}
std::vector<ArchiveStateEvent>ArchiveState::takeEvents(){impl_->onThread();return std::exchange(impl_->events,{});}
void ArchiveState::select(std::optional<std::string>id){auto&i=*impl_;i.onThread();need(!id||ehud::data::validUUID(*id),"Invalid Archive selection");if(id&&i.deleting.contains(*id))return;i.flush();i.pendingCreatedSelection.reset();++i.selectionRevision;i.selectionFailed=false;i.invalidate();const auto token=i.generation;auto outgoing=i.selected;if(!id)i.selected.reset();i.changed();
    const bool accepted=i.perform<std::optional<ArchiveEntry>>([store=i.store,id]{auto entry=id?store->entry(*id):std::nullopt;store->select(id);return entry;},[self=impl_,id,token,outgoing=std::move(outgoing)](auto failure,auto result){if(self->generation!=token)return;
        if(!failure&&result){if(*result){validateArchiveEntry(**result,self->rules);need(id&&(**result).id==*id&&self->categoryExists((**result).categoryID),"Mismatched Archive selection payload");}self->selected=id?(self->dirty.contains(*id)?std::optional(self->dirty.at(*id).entry):(*result?std::optional(self->applyingRemovals(std::move(**result))):std::nullopt)):std::nullopt;self->readError.reset();}
        else {const auto retained=outgoing?(self->selected&&self->selected->id==outgoing->id?self->selected:(self->dirty.contains(outgoing->id)?std::optional(self->dirty.at(outgoing->id).entry):outgoing)):std::nullopt;self->selected=id?(self->dirty.contains(*id)?std::optional(self->dirty.at(*id).entry):retained):std::nullopt;self->readError=message(failure);}self->changed();});if(!accepted){i.readError="Archive file queue is busy. Retry after pending work completes.";i.changed();}
}
bool ArchiveState::create(std::string id,double date,std::optional<std::string>category){auto&i=*impl_;i.onThread();need(ehud::data::validUUID(id)&&std::isfinite(date),"Invalid new Archive identity/date");if(!i.categoryExists(category)){i.readError="Invalid Archive category.";i.changed();return false;}if(i.entries.size()>=archiveMaximumEntries){i.readError="The archive is full.";i.changed();return false;}if(!i.writeErrors.empty()||!i.categoryErrors.empty())return false;need(std::none_of(i.entries.begin(),i.entries.end(),[&](const auto&e){return e.id==id;}),"Repeated new Archive identity");i.flush();i.invalidate();ArchiveEntry entry;entry.id=id;entry.date=entry.modified=date;entry.categoryID=std::move(category);i.selected=entry;i.unannounced.insert(id);i.dirty[id]={entry,++i.dirtyRevision};i.upsertSummary(entry);i.pendingCreatedSelection=id;++i.selectionRevision;i.selectionFailed=false;i.flush();i.changed();return true;
}
std::optional<std::string>ArchiveState::createCategory(std::string id,std::string name,double date){auto&i=*impl_;i.onThread();ArchiveCategory category{std::move(id),i.rules.trimmed(name),date,{}};validateArchiveCategory(category,i.rules);const auto key=i.rules.categoryNameKey(category.name);for(const auto&c:i.categories)if(i.rules.categoryNameKey(c.name)==key)return c.id;if(i.categories.size()+i.categoryRemovals.size()>=archiveMaximumCategories){i.readError="The archive is full.";i.changed();return {};}if(!i.writeErrors.empty()||!i.categoryErrors.empty())return {};need(!i.dirtyCategories.contains(category.id)&&std::none_of(i.categories.begin(),i.categories.end(),[&](const auto&c){return c.id==category.id;}),"Repeated Archive category identity");i.dirtyCategories[category.id]=category;i.upsertCategory(category);i.flush();i.changed();return category.id;
}
bool ArchiveState::update(ArchiveEntry entry,double modified,double time){auto&i=*impl_;i.onThread();i.time(time);validateArchiveEntry(entry,i.rules);need(std::isfinite(modified),"Invalid Archive modified date");if(!i.selected||i.selected->id!=entry.id||i.deleting.contains(entry.id)||!i.categoryExists(entry.categoryID))return false;entry.modified=modified;i.selected=entry;i.dirty[entry.id]={entry,++i.dirtyRevision};i.upsertSummary(entry);i.deadline=time+.35;i.changed();return true;}
bool ArchiveState::moveSelected(std::optional<std::string>id,double modified,double time){auto&i=*impl_;i.onThread();if(!i.selected||i.selected->categoryID==id||!i.categoryExists(id))return false;auto entry=*i.selected;entry.categoryID=std::move(id);if(!update(std::move(entry),modified,time))return false;i.categoryChanges[i.selected->id]=i.dirtyRevision;i.flush();i.changed();return true;}
void ArchiveState::flush(){impl_->onThread();impl_->flush();}void ArchiveState::retryPendingSaves(){auto&i=*impl_;i.onThread();i.readError.reset();i.flush();i.changed();}
void ArchiveState::queueCapacityAvailable(){auto&i=*impl_;i.onThread();if(i.backpressured)i.flush(false);}
std::optional<double>ArchiveState::saveDeadline()const noexcept{return impl_->deadline;}
bool ArchiveState::flushIfDue(double t){auto&i=*impl_;i.onThread();i.time(t);if(!i.deadline||t<*i.deadline)return false;i.flush();return true;}
bool ArchiveState::deleteSelected(){auto&i=*impl_;i.onThread();if(!i.selected||i.deleting.contains(i.selected->id))return false;const auto entry=*i.selected;const auto id=entry.id;std::optional<Impl::Pending>pending;if(i.dirty.contains(id)){pending=i.dirty.at(id);i.dirty.erase(id);}i.deadline.reset();i.deleting.insert(id);i.invalidate();i.selected.reset();const auto found=std::find_if(i.entries.begin(),i.entries.end(),[&](const auto&s){return s.id==id;});if(found!=i.entries.end()){i.thumbnailBytes-=archiveThumbnailCost(found->thumbnail);i.entries.erase(found);}i.changed();
    const bool accepted=i.perform<int>([store=i.store,id]{store->remove(id);return 0;},[self=impl_,entry,pending](auto failure,auto){self->deleting.erase(entry.id);if(!failure){self->writeErrors.erase(entry.id);self->unannounced.erase(entry.id);self->categoryChanges.erase(entry.id);self->events.push_back({"deleted",entry.id});}else {if(pending)self->dirty[entry.id]=*pending;self->upsertSummary(entry);self->writeErrors[entry.id]=message(failure);}self->changed();});if(!accepted){i.deleting.erase(id);if(pending)i.dirty[id]=*pending;i.upsertSummary(entry);i.writeErrors[id]="Archive file queue is busy. Retry deletion when capacity is available.";i.changed();return false;}return true;
}
bool ArchiveState::deleteCategory(std::string_view value){auto&i=*impl_;i.onThread();const std::string id(value);if(i.categoryRemovals.contains(id)||!i.categoryExists(std::optional(id)))return false;std::set<std::string,std::less<>>affected;for(const auto&s:i.entries)if(s.categoryID==id)affected.insert(s.id);for(const auto&[document,p]:i.dirty)if(p.entry.categoryID==id)affected.insert(document);if(i.selected&&i.selected->categoryID==id)affected.insert(i.selected->id);i.categoryRemovals[id]=affected;i.categories.erase(std::remove_if(i.categories.begin(),i.categories.end(),[&](const auto&c){return c.id==id;}),i.categories.end());i.dirtyCategories.erase(id);i.categoryErrors.erase(id);
    for(const auto&document:affected){std::optional<ArchiveEntry>entry=i.dirty.contains(document)?std::optional(i.dirty.at(document).entry):(i.selected&&i.selected->id==document?i.selected:std::nullopt);if(entry){entry->categoryID.reset();i.dirty[document]={*entry,++i.dirtyRevision};if(i.selected&&i.selected->id==document)i.selected=*entry;}}
    for(auto&s:i.entries)s=i.applyingRemovals(std::move(s));i.flush();i.changed();return true;
}
void ArchiveState::loadThumbnail(std::string id,std::function<void(std::optional<ArchiveJson>)>callback){auto&i=*impl_;i.onThread();need(ehud::data::validUUID(id)&&bool(callback),"Invalid Archive thumbnail request");if(!i.active){callback({});return;}if(i.dirty.contains(id)){callback(archiveSummary(i.dirty.at(id).entry).thumbnail);return;}if(i.thumbnailRequests.contains(id)){auto&callbacks=i.thumbnailRequests.at(id).callbacks;if(callbacks.size()>=8)callback({});else callbacks.push_back(std::move(callback));return;}if(i.thumbnailRequests.size()>=32){callback({});return;}const auto token=++i.thumbnailToken;i.thumbnailRequests[id]={token,{std::move(callback)}};
    const bool accepted=i.perform<std::optional<ArchiveJson>>([store=i.store,id]{return store->thumbnail(id);},[self=impl_,id,token](auto failure,auto result){const auto found=self->thumbnailRequests.find(id);if(found==self->thumbnailRequests.end()||found->second.token!=token)return;auto callbacks=std::move(found->second.callbacks);self->thumbnailRequests.erase(found);std::optional<ArchiveJson>value;if(!failure&&self->active&&result)value=std::move(*result);if(value)validateArchiveMedia(*value);for(auto&callback:callbacks)callback(value);});if(!accepted){auto callbacks=std::move(i.thumbnailRequests.at(id).callbacks);i.thumbnailRequests.erase(id);for(auto&c:callbacks)c({});}
}
bool ArchiveState::setFilter(std::optional<std::string>category,bool uncategorized){auto&i=*impl_;i.onThread();need(!category||ehud::data::validUUID(*category),"Invalid Archive category filter");need(!category||!uncategorized,"Ambiguous Archive category filter");if(!i.categoryExists(category))return false;if(i.categoryID==category&&i.uncategorized==uncategorized)return false;i.categoryID=std::move(category);i.uncategorized=uncategorized;i.galleryScroll=0;i.changed();return true;}
const std::optional<std::string>&ArchiveState::categoryID()const noexcept{return impl_->categoryID;}bool ArchiveState::filtersUncategorized()const noexcept{return impl_->uncategorized;}
std::span<const std::size_t>ArchiveState::filteredIndices()const noexcept{return impl_->filtered;}
double ArchiveState::galleryScroll()const noexcept{return impl_->galleryScroll;}double ArchiveState::categoryScroll()const noexcept{return impl_->categoryScroll;}double ArchiveState::galleryMaximum()const noexcept{return impl_->galleryMaximum();}double ArchiveState::categoryMaximum()const noexcept{return impl_->categoryMaximum();}
std::optional<core::Rect>ArchiveState::galleryThumb()const noexcept{const auto&i=*impl_;const auto max=i.galleryMaximum();if(i.selected||max<=0)return {};const auto height=std::max(24.,357*357/(357+max));return core::Rect{380,49+(357-height)*i.galleryScroll/max,8,height};}
bool ArchiveState::setGalleryScroll(double value){auto&i=*impl_;i.onThread();if(i.selected||!std::isfinite(value))return false;const auto next=std::clamp(value,0.,i.galleryMaximum());if(next==i.galleryScroll)return false;i.galleryScroll=next;++i.collectionRevision;return true;}
bool ArchiveState::scroll(core::Point point,double delta){auto&i=*impl_;i.onThread();if(i.selected||!std::isfinite(delta)||delta==0)return false;if(contains(categoriesRect(),point)){const auto next=std::clamp(i.categoryScroll+delta,0.,i.categoryMaximum());if(next==i.categoryScroll)return false;i.categoryScroll=next;++i.collectionRevision;return true;}if(contains(galleryRect(),point)||contains(galleryScrollerRect(),point))return setGalleryScroll(i.galleryScroll+delta);return false;}
std::vector<ArchiveGalleryRow>ArchiveState::galleryRows()const{const auto&i=*impl_;std::vector<ArchiveGalleryRow>out;if(i.selected)return out;const auto first=std::min(i.filtered.size(),static_cast<std::size_t>(std::floor(i.galleryScroll/122))*2),last=std::min(i.filtered.size(),static_cast<std::size_t>(std::ceil((i.galleryScroll+357)/122))*2);out.reserve(last-first);
    for(std::size_t n=first;n<last;++n){const core::Rect local{double(n%2)*143,double(n/2)*122-i.galleryScroll,132,113};const auto hit=intersect({local.x+101,local.y+49,132,113},galleryRect());if(hit&&hit->height>=2)out.push_back({i.entries[i.filtered[n]].id,local,*hit,i.filtered[n]});}return out;
}
std::vector<ArchiveCategoryRow>ArchiveState::categoryRows()const{const auto&i=*impl_;std::vector<ArchiveCategoryRow>out;if(i.selected)return out;const auto count=i.categories.size()+2,first=std::min(count,static_cast<std::size_t>(std::floor(i.categoryScroll/34))),last=std::min(count,static_cast<std::size_t>(std::ceil((i.categoryScroll+238)/34)));out.reserve(last-first);
    for(std::size_t n=first;n<last;++n){const core::Rect local{0,double(n)*34-i.categoryScroll,75,27};const auto hit=intersect({14,local.y+49,75,27},categoriesRect());if(hit&&hit->height>=2){const std::string_view id=n==0?std::string_view("all"):n==1?std::string_view("uncategorized"):std::string_view(i.categories[n-2].id);const bool selected=n==0?!i.categoryID&&!i.uncategorized:n==1?i.uncategorized:i.categoryID&&*i.categoryID==id;out.push_back({id,local,*hit,n,selected});}}return out;
}
core::Rect ArchiveState::bodyRect()const noexcept{return {24,111,352,impl_->selected&&!impl_->selected->media.empty()?147.:264.};}
} // namespace endfield::modules
