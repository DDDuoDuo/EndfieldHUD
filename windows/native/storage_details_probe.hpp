#pragma once
#include "app/utility_executor.hpp"
#include "modules/storage_state.hpp"
#include <functional>
namespace endfield::native {
// Explicit-only details request adapter. No independent worker/timer. Scanner
// captures immutable inputs only; clock is called on the owner thread at drain.
// Controller/executor outlive this adapter. Destruction cancels a running scan
// cooperatively and invalidates its callback without waiting on file drivers.
class StorageDetailsProbe final {
public:
    using Scanner=std::function<modules::StorageDetailsSnapshot(const modules::StorageScanCancellation&)>;
    using Clock=std::function<double()>;
    struct Stats {std::uint64_t submitted{},completed{},failed{},backpressure{};bool waiting{},inFlight{};};
    StorageDetailsProbe(modules::StorageController&,app::UtilityExecutor&,Scanner,Clock);
    ~StorageDetailsProbe();
    StorageDetailsProbe(const StorageDetailsProbe&)=delete;
    StorageDetailsProbe&operator=(const StorageDetailsProbe&)=delete;
    bool submitPending(); // after explicit request or shared utility drain
    Stats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
