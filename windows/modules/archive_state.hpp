#pragma once
#include "modules/archive_model.hpp"
#include "core/scene.hpp"
#include <exception>
#include <memory>

namespace endfield::modules {
// Synchronous repository contract, called only inside the injected FIFO file
// executor. The retained UI state never opens a database or owns a worker.
class ArchiveRepository {
public:
    virtual ~ArchiveRepository()=default;
    virtual std::vector<ArchiveCategory>categories()=0;
    virtual std::vector<ArchiveSummary>summaries()=0;
    virtual std::optional<ArchiveEntry>entry(std::string_view)=0;
    virtual std::optional<std::string>selection()=0;
    virtual void select(const std::optional<std::string>&)=0;
    virtual void save(const ArchiveEntry&)=0;
    virtual void saveCategory(const ArchiveCategory&)=0;
    virtual void deleteCategory(std::string_view,std::span<const ArchiveEntry>preservingDrafts)=0;
    virtual void remove(std::string_view)=0;
    virtual std::optional<ArchiveJson>thumbnail(std::string_view)=0;
};
struct ArchiveExecutor {
    // UtilityExecutor::submit-compatible. Work is FIFO; completion runs on the
    // creating/UI thread only. false means never accepted. No timed retry is
    // installed: the owner retries after queue capacity frees or explicit Retry.
    std::function<bool(std::function<void()>,std::function<void(std::exception_ptr)>)>submit;
};
struct ArchiveStateEvent {std::string name,id;};
struct ArchiveGalleryRow {std::string_view id;core::Rect local,hit;std::size_t index{};};
struct ArchiveCategoryRow {std::string_view id;core::Rect local,hit;std::size_t index{};bool selected{};};
// Source ArchiveController + ArchiveCanvas collection semantics. Content stays
// lazy: summaries and <=100 categories are retained; only selected/dirty entry
// bodies are loaded. Accepted saves can finish after facade destruction, without
// invoking an abandoned UI callback; borrowed execution routes must be retired
// only after accepted durable writes finish. No clock, timer, media decoder,
// filesystem, UUID generator, projected editor or renderer is created here.
class ArchiveState final {
public:
    ArchiveState(std::shared_ptr<ArchiveRepository>,ArchiveExecutor,ArchiveTextRules);
    ~ArchiveState();
    ArchiveState(const ArchiveState&)=delete;ArchiveState&operator=(const ArchiveState&)=delete;
    void activate();void deactivate();
    bool active()const noexcept;bool busy()const noexcept;bool hasUnsavedChanges()const noexcept;
    std::span<const ArchiveCategory>categories()const noexcept;
    std::span<const ArchiveSummary>entries()const noexcept;
    const std::optional<ArchiveEntry>&selected()const noexcept;
    const std::optional<std::string>&error()const noexcept;
    std::uint64_t revision()const noexcept;std::uint64_t collectionRevision()const noexcept;
    std::vector<ArchiveStateEvent>takeEvents();
    void select(std::optional<std::string>);
    bool create(std::string id,double foundationDate,std::optional<std::string>categoryID={});
    std::optional<std::string>createCategory(std::string id,std::string name,double foundationDate);
    bool moveSelected(std::optional<std::string>categoryID,double modifiedDate,double ownerTime);
    // Explicit timestamp + owner animation clock are distinct. The source's
    // .35-second debounce is one deadline returned to the existing scheduler.
    // Latest text/runs remain owned across failure; no plain-text flattening.
    bool update(ArchiveEntry,double modifiedDate,double ownerTime);
    void flush();void retryPendingSaves();
    // Owner calls after an unrelated shared-executor completion frees capacity.
    // Retries only rejected retained writes; failed disk writes stay explicit.
    void queueCapacityAvailable();
    std::optional<double>saveDeadline()const noexcept;
    bool flushIfDue(double ownerTime);
    // Caller confirms using the source menu before invoking these methods.
    bool deleteSelected();bool deleteCategory(std::string_view);
    void loadThumbnail(std::string id,std::function<void(std::optional<ArchiveJson>)>);
    bool setFilter(std::optional<std::string>categoryID,bool uncategorized=false);
    const std::optional<std::string>&categoryID()const noexcept;bool filtersUncategorized()const noexcept;
    std::span<const std::size_t>filteredIndices()const noexcept;
    double galleryScroll()const noexcept;double categoryScroll()const noexcept;
    double galleryMaximum()const noexcept;double categoryMaximum()const noexcept;
    std::optional<core::Rect>galleryThumb()const noexcept;
    bool scroll(core::Point,double delta);bool setGalleryScroll(double);
    std::vector<ArchiveGalleryRow>galleryRows()const;std::vector<ArchiveCategoryRow>categoryRows()const;
    static constexpr core::Rect bounds(){return {0,0,400,440};}
    static constexpr core::Rect galleryRect(){return {101,49,275,357};}
    static constexpr core::Rect galleryScrollerRect(){return {380,49,8,357};}
    static constexpr core::Rect categoriesRect(){return {14,49,75,238};}
    static constexpr core::Rect titleRect(){return {24,48,352,30};}
    static constexpr core::Rect dateRect(){return {24,81,180,23};}
    core::Rect bodyRect()const noexcept;
    static constexpr core::Rect mediaRect(){return {24,268,250,93};}
    static constexpr core::Rect seekRect(){return {24,367,250,12};}
private:struct Impl;std::shared_ptr<Impl>impl_;
};
} // namespace endfield::modules
