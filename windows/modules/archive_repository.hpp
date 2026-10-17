#pragma once
#include "modules/archive_state.hpp"
#include <filesystem>

namespace endfield::modules {
// Lazily opened, source-compatible Archive/archive.sqlite (SQLite user_version2).
// The explicit directory is the Archive directory itself, not an app-data root.
// Every operation, including reopen(), runs on one borrowed FIFO file executor;
// construction creates no directory/database/thread. Accepted owner jobs retain
// this repository. Original Mac bookmark metadata remains opaque, never opened.
// JSON dates use Foundation's epoch; indexed SQLite dates use Unix seconds.
class ArchiveSQLiteRepository final:public ArchiveRepository {
public:
    ArchiveSQLiteRepository(std::filesystem::path archiveDirectory,ArchiveTextRules);
    ~ArchiveSQLiteRepository();
    ArchiveSQLiteRepository(const ArchiveSQLiteRepository&)=delete;
    ArchiveSQLiteRepository&operator=(const ArchiveSQLiteRepository&)=delete;
    std::vector<ArchiveCategory>categories()override;
    std::vector<ArchiveSummary>summaries()override;
    std::optional<ArchiveEntry>entry(std::string_view)override;
    std::optional<std::string>selection()override;
    void select(const std::optional<std::string>&)override;
    void save(const ArchiveEntry&)override;
    void saveCategory(const ArchiveCategory&)override;
    void deleteCategory(std::string_view,std::span<const ArchiveEntry>)override;
    void remove(std::string_view)override;
    std::optional<ArchiveJson>thumbnail(std::string_view)override;
    const std::filesystem::path&path()const noexcept;
    // Explicit conflict recovery only after the owner resolves/reloads drafts.
    // No automatic reopen silently changes the optimistic-write baseline.
    void reopen();
private:struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::modules
