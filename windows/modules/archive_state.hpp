#pragma once
#include "modules/archive_classification.hpp"
#include "core/localization.hpp"
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
    // 1.3.0 (ArchiveStore.swift:202-224, 380-430). Defaults for repositories
    // without these features: nothing is classified (status unclassified,
    // verify true, declassify no-op); writes (rename, badge, classify,
    // recovery) refuse with StoreError(unavailable).
    virtual void renameCategory(std::string_view id,std::string_view name);
    virtual void markCategoryViewed(std::string_view id);
    virtual bool isClassified(std::string_view id);
    virtual ArchiveClassificationStatus classificationStatus(std::string_view id);
    virtual void classify(std::string_view id,std::string_view password);
    virtual bool verify(std::string_view id,std::string_view password);
    virtual void declassify(std::string_view id);
    virtual void recoverClassification(std::string_view id,const std::optional<std::string>&newPassword);
};
struct ArchiveExecutor {
    // UtilityExecutor::submit-compatible. Work is FIFO; completion runs on the
    // creating/UI thread only. false means never accepted. No timed retry is
    // installed: the owner retries after queue capacity frees or explicit Retry.
    std::function<bool(std::function<void()>,std::function<void(std::exception_ptr)>)>submit;
};
struct ArchiveStateEvent {std::string name,id;};
// Mac import relink ledger upkeep (modules/notes_relink_ledger.hpp), set by
// the repository's owner. Each function runs on the FIFO file executor right
// after the record write it follows (record first, ledger second) and must
// not throw; an unset function is skipped.
//   relinked   the entry's attachment that held the Mac media object now
//              holds the Windows reference at the path (the stored entry
//              was checked to hold it);
//   removed    the attachment that held the Mac media object was removed
//              (stored entry checked), or, with null, the entry was deleted;
//   reconcile  once after the first load, against the repository.
struct ArchiveRelinkLedger {
    std::function<void(std::string_view entryID,const ArchiveJson& macMedia,std::string_view windowsPath)>relinked;
    std::function<void(std::string_view entryID,const ArchiveJson* macMedia)>removed;
    std::function<void(ArchiveRepository&)>reconcile;
};
// Source ArchiveController semantics (the 1.3.0 ArchiveCanvas keeps its own
// filter, scroll and layout: modules/archive_canvas.hpp). Content stays
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
    // Source archiveAction names, in order: store results (created, deleted,
    // categoryCreated/Deleted/Changed, and since 1.3.0 categoryRenamed,
    // classified, declassified, classificationRecovered) and the interaction's
    // own edited, mediaAdded and mediaRemoved (recordAction). The root drains
    // them into the Event Log with the action name only, never document text.
    // (The Mac event-log allowlist still lacks the four 1.3.0 names, so the
    // Mac logs them as a generic Archive row: DOCS-318, a decision for D7.)
    std::vector<ArchiveStateEvent>takeEvents();
    void recordAction(std::string name,std::string id);
    void setRelinkLedger(ArchiveRelinkLedger);
    // After update() replaced the selected entry's attachment that held the
    // Mac media object `macMedia` with the Windows reference at `windowsPath`
    // (mediaRelinked), or removed it (mediaRemoved): saves now, then updates
    // the ledger behind that write once the stored entry shows the change. A
    // failed or rejected save leaves the row to the next load's reconcile.
    // False: no ledger, or nothing could be queued.
    bool mediaRelinked(std::string entryID,ArchiveJson macMedia,std::string windowsPath);
    bool mediaRemoved(std::string entryID,ArchiveJson macMedia);
    void select(std::optional<std::string>);
    bool create(std::string id,double foundationDate,std::optional<std::string>categoryID={});
    // Kept API (ArchiveController.createCategory): same-name returns the
    // existing id; 80-Character load rule; the new category has the NEW badge.
    std::optional<std::string>createCategory(std::string id,std::string name,double foundationDate);

    // --- Editable categories (Mac 1.3.0 ArchiveStore.swift:819-863) ---------
    // The "+" action: "Category N" in `language` (N = custom categories + 1,
    // skipping names nameIssue refuses, giving up after 200) with the NEW
    // badge. Same limits as createCategory (100, refused while a write or
    // category error is pending).
    std::optional<std::string>addCategory(std::string id,double foundationDate,core::Language language);
    std::string nextCategoryName(core::Language)const;
    // Live validation for the rename field (nullopt: the name can be committed).
    std::optional<ArchiveCategoryNameIssue>categoryNameIssue(std::string_view name,const std::optional<std::string>&renaming={})const;
    // Commits a trimmed valid unique name in memory at once; storage follows
    // in order (renames of committed categories are written first). `issue`
    // is set when rejected (unknownCategory for an unknown/removed id).
    struct RenameOutcome{ArchiveCategoryRenameResult result;std::optional<ArchiveCategoryNameIssue>issue;};
    RenameOutcome renameCategory(std::string_view id,std::string_view name);
    // Clears the NEW badge (call when the category filter is opened). A failed
    // write is not retried: at worst the badge returns after a relaunch.
    void markCategoryViewed(std::string_view id);

    // --- Classified documents (Mac 1.3.0 ArchiveController lock model) ------
    // A classified document opens locked; the right password unlocks it for
    // one visit; leaving it (select of another/none), creating or deleting a
    // document, lockSelected() or deactivate() locks it again. Locked
    // documents reject update/moveSelected/deleteSelected. While locked,
    // selected() still holds the text and media for sizing the redaction bars
    // only: they must not be shown. Completions run on the owner thread with
    // nullptr on success, else the store's error (ArchiveClassificationError
    // passwordMissing/vaultUnavailable/cancelled or StoreError); a refused
    // request completes at once with StoreError(invalid).
    using Completion=std::function<void(std::exception_ptr)>;
    bool isClassified(std::string_view id)const noexcept;
    ArchiveClassificationStatus selectedClassification()const noexcept;
    bool isSelectedLocked()const noexcept;
    bool isClassificationBusy()const noexcept;
    // Flushes first, so the latest title/body commit before the flag; success
    // locks the document (the sealing view). Password rules: archivePasswordIssue.
    void classifySelected(std::string password,Completion={});
    // Verification on the file executor (about 80 ms). A missing or unreadable
    // verifier is reported as passwordMissing / failed with its message, never
    // as wrongPassword; ignored while another unlock or classification runs,
    // or when the document was left meanwhile.
    void unlockSelected(std::string password,std::function<void(ArchiveUnlockResult,std::string message)>);
    void lockSelected();
    void declassifySelected(Completion={}); // only while unlocked in this visit
    void refreshSelectedClassification();   // re-reads the vault state
    // Owner proof for recovery (Mac LAContext .deviceOwnerAuthentication;
    // Windows: Windows Hello / the account password, injected by the native
    // owner). Without one, recovery is refused. `reason` is the catalog pair
    // "restore access to a classified document". The answer must be delivered
    // on the owner thread (synchronously or later), exactly once.
    using OwnerAuthentication=std::function<void(std::string reason,std::function<void(bool approved)>)>;
    void setOwnerAuthentication(OwnerAuthentication);
    // Only for a classified document whose verifier is missing or unreadable:
    // a new password keeps it classified and unlocks this visit; nullopt
    // declassifies it. Cancelled/denied/left -> ArchiveClassificationError(cancelled).
    void recoverSelectedClassification(std::optional<std::string>newPassword,std::string reason,Completion);
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
    // The caller asks first (the canvas's confirm bar).
    bool deleteSelected();bool deleteCategory(std::string_view);
    void loadThumbnail(std::string id,std::function<void(std::optional<ArchiveJson>)>);
    // The Unicode rules the state validates with (the password and category
    // name rules of the canvas use the same).
    const ArchiveTextRules& textRules()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
} // namespace endfield::modules
