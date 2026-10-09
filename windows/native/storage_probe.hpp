#pragma once
#include "app/utility_executor.hpp"
#include "modules/storage_state.hpp"
#include <functional>

namespace endfield::native {
// The application supplies a bounded, independently owned capacity reader.
// It runs on the EXISTING utility executor; it must not capture UI/controllers,
// scan directories, or block on UI. No provider is installed by this class.
class StorageCapacityProbe final {
public:
    using Reader=std::function<std::optional<modules::StorageCapacity>()>;
    struct Stats {std::uint64_t submitted{},completed{},failed{},backpressure{};bool waiting{},inFlight{};};
    StorageCapacityProbe(modules::StorageController&,app::UtilityExecutor&,Reader);
    ~StorageCapacityProbe();
    StorageCapacityProbe(const StorageCapacityProbe&)=delete;
    StorageCapacityProbe&operator=(const StorageCapacityProbe&)=delete;
    // Activate/refresh/wake and utility-completion events only. Queue-full
    // retains one request; caller retries after its shared executor drain.
    bool submitPending();
    Stats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
