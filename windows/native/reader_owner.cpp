#include "native/reader_owner.hpp"
#ifdef _WIN32
#include <thread>
#include <utility>
namespace endfield::native {
struct ReaderOwner::Impl:std::enable_shared_from_this<Impl> {
    app::UtilityExecutor&executor;app::UtilityExecutor::Route route{};
    LayerFontResources fonts;std::shared_ptr<modules::ReaderJSONRepository>repository;
    std::shared_ptr<NativeReaderDocument>document;std::unique_ptr<modules::ReaderState>state;
    std::unique_ptr<ReaderImportQueue>imports;ReaderImportQueue::Completion imported;
    std::function<void()>changed;const std::thread::id owner=std::this_thread::get_id();
    bool alive{true},barrier{},closed{},everStateWork{};
    Impl(std::filesystem::path directory,app::UtilityExecutor&e,LayerFontResources f,
        ReaderImportQueue::Completion completion,std::function<void()>notify)
        :executor(e),fonts(std::move(f)),imported(std::move(completion)),changed(std::move(notify)){
        if(!fonts)throw std::invalid_argument("Reader needs retained shared font resources");
        repository=std::make_shared<modules::ReaderJSONRepository>(std::move(directory),modules::ReaderPreferences::defaults("System"));
        document=std::make_shared<NativeReaderDocument>(fonts);route=executor.makeRoute();
    }
    void onOwner()const{if(owner!=std::this_thread::get_id())throw std::logic_error("Reader owner belongs to its creating thread");}
    void open()const{onOwner();if(!alive||closed||!state)throw std::logic_error("Reader owner is closed");}
    void initialize(){const std::weak_ptr<Impl>weak=shared_from_this();
        modules::ReaderExecutor bridge{[weak](auto work,auto completed){auto self=weak.lock();if(!self||!self->alive)return false;self->onOwner();const bool accepted=self->executor.submit(self->route,std::move(work),[weak,completed=std::move(completed)](std::exception_ptr error){auto self=weak.lock();if(!self||!self->alive)return;self->onOwner();completed(error);});if(accepted)self->everStateWork=true;return accepted;}};
        state=std::make_unique<modules::ReaderState>(repository,std::move(bridge),document,[weak]{auto self=weak.lock();if(self&&self->alive&&self->changed)self->changed();});
        imports=std::make_unique<ReaderImportQueue>(executor,[weak](auto generation,auto result,std::exception_ptr error){auto self=weak.lock();if(!self||!self->alive)return;self->onOwner();if(self->imported)self->imported(generation,std::move(result),error);});
    }
    bool flush(){open();if(barrier)throw std::logic_error("Reader owner barrier is already active");barrier=true;struct End{Impl&i;~End(){i.barrier=false;}}end{*this};
        state->retryPendingWrites();state->flush();
        for(;;){executor.waitIdle();executor.drain();if(!alive||!state)return false;state->queueCapacityAvailable();if(!state->busy())return !state->hasUnsavedChanges();}
    }
    // Terminal accepted-work cleanup: invalidated callbacks cannot touch dead
    // state, but accepted immutable writes are retained. The release uses a NEW
    // route because State's conditional release can be canceled by its gate.
    void dispose(){onOwner();if(closed)return;alive=false;
        imports.reset();if(state){state->setActive(false);state.reset();}
        executor.invalidate(route,false);
        if(!everStateWork){document.reset();repository.reset();closed=true;return;}
        struct Release {bool done{};std::exception_ptr error;};auto result=std::make_shared<Release>();
        const auto releaseRoute=executor.makeRoute();const auto retained=document;
        try{
            while(!executor.submit(releaseRoute,[retained]{retained->release();},[result](std::exception_ptr error){result->error=error;result->done=true;})){
                executor.waitIdle();executor.drain();
            }
            while(!result->done){executor.waitIdle();executor.drain();}
        }catch(...){executor.invalidate(releaseRoute,false);throw;}
        executor.invalidate(releaseRoute,false);
        if(result->error)std::rethrow_exception(result->error);
        document.reset();repository.reset();closed=true;
    }
};
ReaderOwner::ReaderOwner(std::filesystem::path directory,app::UtilityExecutor&e,LayerFontResources fonts,
    ReaderImportQueue::Completion completed,std::function<void()>changed){
    impl_=std::make_shared<Impl>(std::move(directory),e,std::move(fonts),std::move(completed),std::move(changed));
    try{impl_->initialize();}catch(...){impl_->alive=false;e.invalidate(impl_->route,false);throw;}
}
ReaderOwner::~ReaderOwner(){auto self=impl_;try{self->dispose();}catch(...){std::terminate();}}
modules::ReaderState&ReaderOwner::state(){impl_->open();return *impl_->state;}
const modules::ReaderState&ReaderOwner::state()const{impl_->open();return *impl_->state;}
void ReaderOwner::import(std::uint64_t generation,modules::ReaderBook book,std::shared_ptr<void>lease){auto self=impl_;self->open();self->imports->begin(generation,std::move(book),self->fonts,std::move(lease));}
void ReaderOwner::cancelImport(){auto self=impl_;self->open();self->imports->cancel();}
bool ReaderOwner::importBusy()const noexcept{return impl_->imports&&impl_->imports->busy();}
void ReaderOwner::setFonts(LayerFontResources fonts){auto self=impl_;self->open();if(!fonts)throw std::invalid_argument("Reader needs retained shared font resources");
    const auto&old=self->fonts;if(old.revision()==fonts.revision()&&old.factory()==fonts.factory()&&old.bundledCollection()==fonts.bundledCollection()&&old.systemCollection()==fonts.systemCollection()&&old.fallbackFamily()==fonts.fallbackFamily()&&old.locale()==fonts.locale())return;
    self->document->setFontResources(fonts);self->fonts=std::move(fonts);
    // A pending first render also needs replacement; waiting for current() would
    // permit its captured old language snapshot to publish after this event.
    if(self->state->active())if(const auto*book=self->state->book())self->state->jump(self->state->current()?self->state->current()->location:book->location);
}
void ReaderOwner::queueCapacityAvailable(){auto self=impl_;self->open();self->state->queueCapacityAvailable();if(self->alive&&self->imports)self->imports->queueCapacityAvailable();}
bool ReaderOwner::flush(){auto self=impl_;return self->flush();}
bool ReaderOwner::shutdown(){auto self=impl_;self->onOwner();if(self->closed)return true;self->open();self->imports->cancel();if(!self->flush())return false;self->dispose();return true;}
bool ReaderOwner::closed()const noexcept{return impl_->closed;}
}
#endif
