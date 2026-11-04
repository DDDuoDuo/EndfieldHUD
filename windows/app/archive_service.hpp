#pragma once
#include "app/utility_executor.hpp"
#include "modules/archive_state.hpp"
#include <filesystem>

namespace endfield::app {
// App-owned lifetime boundary for the source-compatible Archive database.
// Construction is lazy and reads no files. All repository operations share the
// existing FIFO utility queue; there is no independent worker, timer or poll.
// The UI consumes state() and its saveDeadline() through its existing host clock.
// The vault holds classified documents' password verifiers (production: the
// native DpapiArchivePasswordVault under <Archive>\Classified; diagnostics and
// fixtures: ArchiveMemoryPasswordVault or none).
class ArchiveService final {
public:
    ArchiveService(std::filesystem::path archiveDirectory,UtilityExecutor&,
        modules::ArchiveTextRules,std::function<void()>changed={},std::shared_ptr<modules::ArchivePasswordVault> ={});
    ~ArchiveService();
    ArchiveService(const ArchiveService&)=delete;
    ArchiveService&operator=(const ArchiveService&)=delete;
    modules::ArchiveState&state()noexcept;
    const modules::ArchiveState&state()const noexcept;
    void queueCapacityAvailable();
    // Explicit shutdown only. A failed save retains all drafts and returns
    // false; the caller keeps the app open and presents the source error.
    bool flush();
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
