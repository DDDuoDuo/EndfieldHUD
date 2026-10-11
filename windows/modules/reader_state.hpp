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
};
struct ReaderImageView {double zoom{1};core::Point pan{};bool valid()const noexcept;ReaderImageView clamped(core::Point viewportSize)const;bool operator==(const ReaderImageView&)const=default;};
// The actual PDF/EPUB/TXT provider belongs exclusively to the same borrowed FIFO
// executor. It returns at most current/next/previous fixed-viewport rasters and
// never touches a HWND/renderer. No generic/fake decoding fallback is implied.
// Caller keeps the provider alive until accepted jobs finish, then releases its
// document on that same executor before destroying platform-specific resources.
class ReaderDocumentSource {
public:
    virtual ~ReaderDocumentSource()=default;
    virtual void open(const ReaderBook&,const std::function<bool()>&cancelled)=0;
    virtual std::vector<ReaderPage>render(std::optional<ReaderLocation>,std::optional<double>progress,
        const ReaderPreferences&,core::Point viewportSize,bool dark,const std::function<bool()>&cancelled)=0;
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
    // its fraction). Coalesced writes report each contained action once, in
    // order; a failed write reports nothing until a retry succeeds. Owner thread.
    void setEventObserver(std::function<void(std::string_view)>);
    // Caller has already validated the original file with the actual format
    // provider, and applied source title.prefix(200) using Unicode graphemes.
    // Duplicate locator refreshes reference/title, preserving ID/progress/marks.
    // Structural actions expose only confirmed repository snapshots. Progress
    // and preference previews remain responsive; failed structural writes stay
    // pending for explicit retry without pretending they reached durable data.
    bool addValidated(ReaderBook);bool select(std::string_view);bool remove(std::string_view);
    // Explicit relink (Windows only: a Mac bookmark cannot open a Windows
    // file). The user chose `windowsPath` for the book `id` in the picker and
    // the provider validated it like an import. The book keeps its identity,
    // title, location, progress, bookmarks and unknown fields; only its locator
    // becomes the Windows reference (as ehud::migration::relinkedReaderBook).
    // It is selected and opened once the write is durable, which reports
    // "imported" and then resolves the book's ledger row. Throws
    // ReaderError(unavailable) for an unknown id; false before the library loads
    // or when the book already references that file (it is then reopened).
    bool relink(std::string_view id,std::string windowsPath);
    // Set once by the owner before the first activation (owner thread).
    void setRelinkLedger(ReaderRelinkLedger);
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
