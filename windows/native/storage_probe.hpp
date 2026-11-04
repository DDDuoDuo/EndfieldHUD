#pragma once
#include "app/utility_executor.hpp"
#include "modules/storage_state.hpp"
#include <functional>

namespace endfield::native {
// The application supplies a bounded, independently owned capacity reader.
// It runs on the EXISTING utility executor; it must not capture UI/controllers,
// scan directories, or block on UI. No provider is installed by this class.
// One read lists every disk with its numbers (StorageCapacityResult, the Mac
// refreshCapacity worker); `startupName` is the startup disk's fallback name
// in the HUD language at the time of the request (the Mac passes L10n.language
// to its worker), `clock` the owner time of the completion.
class StorageCapacityProbe final {
public:
    using Reader=std::function<modules::StorageCapacityResult(const std::string&startupName)>;
    struct Options {Reader read;std::function<std::string()>startupName;std::function<double()>clock;};
    struct Stats {std::uint64_t submitted{},completed{},failed{},backpressure{};bool waiting{},inFlight{};};
    StorageCapacityProbe(modules::StorageController&,app::UtilityExecutor&,Options);
    ~StorageCapacityProbe();
    StorageCapacityProbe(const StorageCapacityProbe&)=delete;
    StorageCapacityProbe&operator=(const StorageCapacityProbe&)=delete;
    // Controller requests and utility-completion events only. Queue-full
    // retains one request; caller retries after its shared executor drain.
    bool submitPending();
    Stats stats()const noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
