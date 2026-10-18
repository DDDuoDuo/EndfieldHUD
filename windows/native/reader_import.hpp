#pragma once
#ifdef _WIN32
#include "app/utility_executor.hpp"
#include "native/reader_document.hpp"

namespace endfield::native {
// At most one in-flight validation and one latest request on the existing file
// executor. Shelf leases remain alive through validation. This queue stores no
// books and never touches the active document, UI, picker, or renderer.
class ReaderImportQueue final {
public:
    using Validator=std::function<modules::ReaderBook(modules::ReaderBook,const LayerFontResources&,const modules::ReaderCancel&)>;
    using Completion=std::function<void(std::uint64_t,std::optional<modules::ReaderBook>,std::exception_ptr)>;
    ReaderImportQueue(app::UtilityExecutor&,Completion,Validator={});
    ~ReaderImportQueue();
    ReaderImportQueue(const ReaderImportQueue&)=delete;
    ReaderImportQueue&operator=(const ReaderImportQueue&)=delete;
    void begin(std::uint64_t generation,modules::ReaderBook,LayerFontResources,std::shared_ptr<void>lease={});
    void cancel();
    void queueCapacityAvailable();
    bool busy()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
