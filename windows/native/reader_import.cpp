#include "native/reader_import.hpp"
#ifdef _WIN32
#include <atomic>
#include <thread>

namespace endfield::native {
namespace {
modules::ReaderBook validate(modules::ReaderBook book,const LayerFontResources&fonts,const modules::ReaderCancel&cancel){
    modules::checkReaderCancelled(cancel);NativeReaderDocument document(fonts);
    try{document.open(book,cancel);book.title=document.info().title;document.release();}
    catch(...){document.release();throw;}
    modules::checkReaderCancelled(cancel);return book;
}
}
struct ReaderImportQueue::Impl:std::enable_shared_from_this<Impl> {
    struct Request {std::uint64_t generation{},serial{};modules::ReaderBook book;LayerFontResources fonts;std::shared_ptr<void>lease;};
    app::UtilityExecutor&executor;app::UtilityExecutor::Route route;
    Completion completed;Validator validator;std::optional<Request>pending;
    std::shared_ptr<std::atomic<std::uint64_t>>gate=std::make_shared<std::atomic<std::uint64_t>>(1);
    const std::thread::id owner=std::this_thread::get_id();bool alive{true},running{};
    Impl(app::UtilityExecutor&e,Completion completion,Validator validation):executor(e),route(e.makeRoute()),completed(std::move(completion)),validator(std::move(validation)){}
    void onOwner()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("Reader import belongs to its owner thread");}
    void cancel(){gate->fetch_add(1);pending.reset();}
    void pump(){
        onOwner();if(!alive||running||!pending)return;
        auto request=std::make_shared<Request>(*pending);auto result=std::make_shared<std::optional<modules::ReaderBook>>();
        const auto validate=validator;const auto sharedGate=gate;const auto weak=weak_from_this();
        const auto accepted=executor.submit(route,[request,result,validate,sharedGate]{
            const auto canceled=[sharedGate,serial=request->serial]{return sharedGate->load()!=serial;};
            modules::checkReaderCancelled(canceled);*result=validate(request->book,request->fonts,canceled);modules::checkReaderCancelled(canceled);
        },[weak,request,result](std::exception_ptr error){
            const auto self=weak.lock();if(!self||!self->alive)return;self->onOwner();self->running=false;
            if(self->gate->load()==request->serial&&self->completed)self->completed(request->generation,std::move(*result),error);
            if(self->alive)self->pump();
        });
        if(accepted){pending.reset();running=true;}
    }
};
ReaderImportQueue::ReaderImportQueue(app::UtilityExecutor&e,Completion completion,Validator validator){
    if(!validator)validator=validate;impl_=std::make_shared<Impl>(e,std::move(completion),std::move(validator));
}
ReaderImportQueue::~ReaderImportQueue(){auto self=impl_;self->onOwner();self->alive=false;self->cancel();self->executor.invalidate(self->route);}
void ReaderImportQueue::begin(std::uint64_t generation,modules::ReaderBook book,LayerFontResources fonts,std::shared_ptr<void>lease){
    auto self=impl_;self->onOwner();if(!generation)throw std::invalid_argument("Reader import needs a UI generation");
    const auto serial=self->gate->fetch_add(1)+1;
    self->pending=Impl::Request{generation,serial,std::move(book),std::move(fonts),std::move(lease)};self->pump();
}
void ReaderImportQueue::cancel(){auto self=impl_;self->onOwner();self->cancel();}
void ReaderImportQueue::queueCapacityAvailable(){impl_->pump();}
bool ReaderImportQueue::busy()const noexcept{return impl_->running||impl_->pending.has_value();}
}
#endif
