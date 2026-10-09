#pragma once
#ifdef _WIN32
#include "native/reader_import.hpp"
#include "modules/reader_repository.hpp"
#include <filesystem>
namespace endfield::native {
// One lazy Reader lifetime on the application's borrowed FIFO UtilityExecutor.
// Constructor performs no IO/COM initialization or worker start. The supplied
// directory is explicit; referenced books are read-only and never copied.
// State/preview must be detached before successful shutdown. Executor outlives
// this owner. All calls (including destruction) belong to its creating thread.
class ReaderOwner final {
public:
    ReaderOwner(std::filesystem::path readerDirectory,app::UtilityExecutor&,
        LayerFontResources,ReaderImportQueue::Completion={},std::function<void()>changed={});
    ~ReaderOwner();
    ReaderOwner(const ReaderOwner&)=delete;ReaderOwner&operator=(const ReaderOwner&)=delete;
    modules::ReaderState&state();const modules::ReaderState&state()const;
    void import(std::uint64_t generation,modules::ReaderBook,std::shared_ptr<void>lease={});
    void cancelImport();bool importBusy()const noexcept;
    // Event-only snapshot replacement. Regenerates the current source page
    // through the same FIFO using its unchanged saved font preference; System
    // follows SC/KR, explicit/imported faces are preserved.
    void setFonts(LayerFontResources);
    void queueCapacityAvailable();
    // Explicit owner barriers only, never pointer/key/paint callbacks. A save
    // failure returns false and retains full state for retry. Successful shutdown
    // finishes accepted writes, then unconditionally releases the provider on
    // the same MTA before destroying it; it never shuts down the shared executor.
    bool flush();bool shutdown();bool closed()const noexcept;
    // Destructor is a terminal cleanup barrier for accepted work/provider only,
    // not an implicit save of unaccepted drafts. Use flush/shutdown to preserve
    // failure UI. Destruction during an owner callback invalidates further callbacks; the
    // caller must still keep the shared executor alive through this barrier.
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
