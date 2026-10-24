#pragma once
#include "modules/reader_repository.hpp"
#include "core/scene.hpp"
#include <exception>
#include <functional>
#include <memory>

namespace endfield::modules {
struct ReaderExecutor {
    // Same contract as UtilityExecutor::submit. False is explicit backpressure;
    // completion runs later on the owner, never inline or on the worker thread.
    std::function<bool(std::function<void()>,std::function<void(std::exception_ptr)>)>submit;
};
struct ReaderPagePixels {
    unsigned width{},height{};
    enum class Format {rgba8Premultiplied,bgra8Premultiplied}format{Format::rgba8Premultiplied};
    std::vector<std::uint8_t>bytes;
};
// Summary is at most the original 16,384-UTF16-unit chunk (80 graphemes can
// include an entire combining sequence), not an invented short byte truncation.
inline constexpr std::size_t readerMaximumPageSummaryBytes=16384*3;
struct ReaderPage {
    ReaderLocation location;std::optional<ReaderLocation>next,previous;
    double progress{};std::string summary;bool illustration{};
    std::shared_ptr<const ReaderPagePixels>pixels;
    // Mac 1.3.0 ReaderPage (ReaderDocument.swift:14-18): the PDF page count
    // (page numbers, the page slider's range) and, for text pages, the page
    // pt from the text's top (the top margin) to its last line's bottom, so
    // the continuous column joins pages without a gap. nil otherwise.
    std::optional<std::int64_t>pageCount;std::optional<double>textHeight;
};
struct ReaderImageView {double zoom{1};core::Point pan{};bool valid()const noexcept;ReaderImageView clamped(core::Point viewportSize)const;bool operator==(const ReaderImageView&)const=default;};
// The actual PDF/EPUB/TXT provider belongs exclusively to the same borrowed FIFO
// executor. It returns the page at the anchor first, then up to `ahead`
// following pages and the one before (ReaderWorker.render(ahead:), Mac 1.3.0:
// 2 ahead in vertical reading, 1 in paged), fixed-viewport rasters, and
// never touches a HWND/renderer. No generic/fake decoding fallback is implied.
// Caller keeps the provider alive until accepted jobs finish, then releases its
// document on that same executor before destroying platform-specific resources.
class ReaderDocumentSource {
public:
    virtual ~ReaderDocumentSource()=default;
    virtual void open(const ReaderBook&,const std::function<bool()>&cancelled)=0;
    virtual std::vector<ReaderPage>render(std::optional<ReaderLocation>,std::optional<double>progress,
        const ReaderPreferences&,core::Point viewportSize,bool dark,int ahead,const std::function<bool()>&cancelled)=0;
    virtual ReaderPage detail(ReaderLocation,const ReaderPreferences&,core::Point,bool dark,
        ReaderImageView,const std::function<bool()>&cancelled)=0;
    virtual void release()=0;
};
struct ReaderDetail {ReaderImageView view;ReaderPage page;};
// Migration/relink.json bookkeeping of imported Mac books (core/migration/
// mac_import_relink.hpp, store "reader.book"), injected by the native owner;
// unset in previews and portable tests. Each hook runs on the FIFO executor
// inside the job that made the change durable, after the library itself:
// reconcile after the library load, resolved after the confirmed write that
// contains a relink, dismissed after the confirmed write that removed an
// imported Mac book. A failing hook never fails the library (it is retried by
// the next load's reconcile).
struct ReaderRelinkLedger {
    std::function<void(const ReaderLibrary&)>reconcile;
    std::function<void(const std::string&bookID,const std::string&windowsPath)>resolved;
    std::function<void(const std::string&bookID)>dismissed;
};
// Source ReaderController ownership/queue contract. All public calls are on its
// creating thread; file/library/format work is injected, lazy and bounded. No
// private queue, timer, native service, filesystem access or book copy is made.
class ReaderState final {
public:
    ReaderState(std::shared_ptr<ReaderRepository>,ReaderExecutor,
        std::shared_ptr<ReaderDocumentSource> source={},std::function<void()>changed={});
    ~ReaderState();ReaderState(const ReaderState&)=delete;ReaderState&operator=(const ReaderState&)=delete;
    // Mac 1.3.0 ReaderController.setActive: deactivation drops the document,
    // its page cache and the history but KEEPS the current page, so a Reader
    // shown again (another module, the HUD reopened) draws it at once while
    // the document reopens behind it. The page goes when activation finds no
    // selected book, the library load fails or another book opens.
    void setActive(bool);bool active()const noexcept;bool loaded()const noexcept;
    bool busy()const noexcept;bool loading()const noexcept;bool hasUnsavedChanges()const noexcept;
    bool documentProviderAvailable()const noexcept;
    const ReaderLibrary&library()const noexcept;const ReaderBook*book()const noexcept;
    const ReaderPage*current()const noexcept;std::span<const ReaderPage>pages()const noexcept;
    const std::optional<ReaderDetail>&imageDetail()const noexcept;
    const std::optional<std::string>&error()const noexcept;std::uint64_t revision()const noexcept;
    // Typed source ReaderError of error() (display-time localization); empty
    // for the untyped provider/repository failures shown with their own text.
    std::optional<ReaderErrorCode>errorCode()const noexcept;
    // Source ReaderController.onEvent (SystemHUDView records readerAction):
    // "imported", "deleted", "bookmarked", "settings" once the structural
    // write that contains the change is confirmed durable, and "progress" when
    // a confirmed progress write changes the book's saved location (not only
    // its fraction; Mac 1.3.0 no longer LOGS it - the host drops it, older
    // "progress" entries still read back). Coalesced writes report each
    // contained action once, in order; a failed write reports nothing until
    // a retry succeeds. Owner thread.
    void setEventObserver(std::function<void(std::string_view)>);
    // Caller has already validated the original file with the actual format
    // provider, and applied source title.prefix(200) using Unicode graphemes.
    // Duplicate locator refreshes reference/title, preserving ID/progress/marks.
    // Structural actions expose only confirmed repository snapshots. Progress
    // and preference previews remain responsive; failed structural writes stay
    // pending for explicit retry without pretending they reached durable data.
    bool addValidated(ReaderBook);bool select(std::string_view);bool remove(std::string_view);
    // Set once by the owner before the first activation (owner thread).
    void setRelinkLedger(ReaderRelinkLedger);
    // The Mac 1.3.0 Relink… of ANY book (ReaderController.relink, ReaderStore
    // .relink): the user chose `windowsPath` in the picker and the provider
    // validated it like an import; the Windows locator replaces the book's
    // reference (ehud::migration::relinkedReaderBook: identity, title,
    // location, progress, bookmarks and unknown fields stay; the Mac locator
    // keys go - a Mac bookmark cannot open a Windows file) and the selection
    // does not change. No event is reported (the Mac logs none). Once the write is
    // durable `completion(true)` runs (false when it failed), the ledger row
    // of an imported Mac book resolves, and the open book reopens when the
    // Reader is active. Throws ReaderError(unavailable) for an unknown id;
    // false before the library loads or when the book already references the
    // file (the completion then runs at once with true; the Mac rewrites the
    // same reference and reopens: an open book reopens here too).
    bool relink(std::string_view id,std::string windowsPath,std::function<void(bool)>completion);
    // ReaderController.isReading / setReading (Mac 1.3.0): the Reader was
    // last left reading its open book (library.json `reading`). The write
    // happens only once the library loaded and when the value changes.
    bool reading()const noexcept;bool setReading(bool);
    // One bookmark of the open book (the Marks list's ×): reported as
    // "bookmarked" once durable; an unknown mark changes nothing.
    bool removeBookmark(std::string_view bookmarkID);
    bool saveProgress(ReaderLocation,double,std::string_view);
    bool toggleBookmark(std::string newUUID);bool setPreferences(ReaderPreferences);
    void setLayout(core::Point viewportSize,bool dark);
    bool next();bool previous();void jump(ReaderLocation);void jump(double progress);
    void requestImageDetail(ReaderImageView);void cancelImageDetail();
    // Event-only cache refill through this same bounded FIFO/provider. Repeated
    // input while a refill is accepted/pending creates no additional job.
    void prepareNeighbors();
    void queueCapacityAvailable();void retryPendingWrites();
    // Explicit owner flush submission only. App's existing executor wait/drain
    // barrier decides completion; failed latest state remains available here.
    void flush();
private:struct Impl;std::shared_ptr<Impl>impl_;
};
} // namespace endfield::modules
