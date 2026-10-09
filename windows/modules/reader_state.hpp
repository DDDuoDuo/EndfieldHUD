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
    // Caller has already validated the original file with the actual format
    // provider, and applied source title.prefix(200) using Unicode graphemes.
    // Duplicate locator refreshes reference/title, preserving ID/progress/marks.
    // Structural actions expose only confirmed repository snapshots. Progress
    // and preference previews remain responsive; failed structural writes stay
    // pending for explicit retry without pretending they reached durable data.
    bool addValidated(ReaderBook);bool select(std::string_view);bool remove(std::string_view);
    bool saveProgress(ReaderLocation,double,std::string_view);
    bool toggleBookmark(std::string newUUID);bool setPreferences(ReaderPreferences);
    void setLayout(core::Point viewportSize,bool dark);
    bool next();bool previous();void jump(ReaderLocation);void jump(double progress);
    void requestImageDetail(ReaderImageView);void cancelImageDetail();
    void queueCapacityAvailable();void retryPendingWrites();
    // Explicit owner flush submission only. App's existing executor wait/drain
    // barrier decides completion; failed latest state remains available here.
    void flush();
private:struct Impl;std::shared_ptr<Impl>impl_;
};
} // namespace endfield::modules
