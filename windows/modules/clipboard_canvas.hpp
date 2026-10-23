#pragma once
// Clipboard (spec section 3.8): the port of Sources/ClipboardCanvas.swift on
// the design library. The session's clipboard history as list cards under
// type filter tabs; capturing and restoring belong to the platform history
// (native/system_services.hpp ClipboardHistory, read through
// native/clipboard_provider.hpp); this canvas presents, copies, pins and
// removes records. Pinned records lead the list (in capture order among
// themselves) and are never deleted one by one (the guard is HERE, as on the
// Mac: ClipboardHistory::erase still deletes pinned items); Clear and the
// history's limits keep them too. Session-only: nothing here is saved.
//
// Replaces the v1.2.0 port (native/clipboard_scene.*, tools/clipboard_preview.*,
// tools/clipboard_automation.*): no status line, no "Clear unpinned" toolbar
// confirm, no 41 px rows, no reveal shutter, no help suffix on the spoken
// labels (inventory DOCS-550, DOCS-552).
#include "modules/hud_design_page.hpp"

namespace endfield::modules {
// ClipboardKind of the Mac store (ClipboardStore.swift:4).
enum class ClipboardRecordKind : std::uint8_t { text, url, image, files };
// ClipboardDetails (ClipboardStore.swift:19-31): the meta line's facts,
// captured with the record; never its contents.
struct ClipboardDetails {
    std::optional<std::string> source;              // the app that put it on the clipboard
    std::optional<std::int64_t> characters;         // text: extended grapheme clusters of the whole text
    std::optional<std::string> format;              // image: "PNG", "TIFF" (Windows: also "BMP" for DIB)
    std::optional<std::int64_t> pixelWidth, pixelHeight;
    std::optional<std::int64_t> fileCount, byteCount; // files: count, total size of <= 64 regular files
    bool operator==(const ClipboardDetails&) const = default;
};
// ClipboardItem as the canvas reads it (ClipboardStore.swift:6-16).
struct ClipboardRecord {
    std::uint64_t id{};
    bool pinned{};
    ClipboardRecordKind kind{ClipboardRecordKind::text};
    std::string preview;                            // the store's 90-character flattened preview
    std::optional<design::ImageSource> thumbnail;   // image records: the 96 px thumbnail (memory image)
    double createdAt{};                             // Unix seconds of the capture
    ClipboardDetails details;
    bool operator==(const ClipboardRecord&) const = default;
};
// ClipboardStore.items / capacity / statusMessage, newest capture first.
struct ClipboardSnapshot {
    std::vector<ClipboardRecord> items;
    std::size_t capacity{10};
    std::optional<std::string> status;
    bool operator==(const ClipboardSnapshot&) const = default;
};
// The store's operations (ClipboardStore.togglePin / remove / clearUnpinned
// and SystemHUDView's onCopy). `copy` restores a record to the clipboard and
// is false when it could not be copied. All run on the UI thread.
struct ClipboardActions {
    std::function<ClipboardSnapshot()> snapshot;
    std::function<bool(std::uint64_t)> copy, togglePin, remove;
    std::function<bool()> clearUnpinned;
};

class ClipboardCanvas final : public design::Canvas {
public:
    // Filter (:10-30): All, Text, Link, Image, File.
    enum class Filter : std::uint8_t { all, text, link, image, file };
    static constexpr std::size_t filterCount = 5;
    // `wallClock`: Unix seconds now (ages; tests inject a fixed clock).
    ClipboardCanvas(design::Context&, ClipboardActions, std::function<double()> wallClock);
    // The store's observer (:55, :102-118): the host calls it after every
    // history or status change, whether or not Clipboard is shown.
    void storeChanged();

    Filter filter() const noexcept { return filter_; }
    std::optional<std::uint64_t> focusedID() const noexcept { return focusedID_; }
    const ClipboardSnapshot& snapshot() const noexcept { return snapshot_; }
    // Actions (:197-243), also the verification and test hooks.
    void copy(std::uint64_t id);
    void togglePin(std::uint64_t id);
    void remove(std::uint64_t id);               // unpinned records only
    void selectFilter(Filter);
    void moveFocus(int step);
    void copyVisible(std::size_t index);         // 1-6: the cards in view, top to bottom

    // Formatting (:306-347), exposed for the contract suites.
    static design::ListCard::Kind cardKind(ClipboardRecordKind) noexcept;
    static std::string recordTitle(const ClipboardRecord&, core::Language); // title(_:) (:315)
    static std::string meta(const ClipboardRecord&, core::Language);
    static std::string age(double createdAt, double now, core::Language);
    // ByteCountFormatter.string(fromByteCount:countStyle: .file) as the Mac
    // shows it (decimal units, adaptive digits: KB 0, MB 1, GB+ 2, no zero
    // padding; "Zero KB", "1 byte", "n bytes").
    static std::string fileSize(std::int64_t bytes);

    // --- design::Canvas ---
    void buildPage(design::Page&) override;
    void show(double now) override;               // makeContent (:97-100)
    void didActivate() override;
    void didDeactivate() override;
    void willLeave() override { didDeactivate(); }
    void recordingModeChanged() override;
    bool handleKey(const design::KeyInput&) override;
    bool handleRightPointerDown(design::Point) override;

    // Test hooks (:351-352).
    std::vector<std::string> cardTitlesForTesting() const;
    std::optional<std::size_t> focusedCardIndexForTesting() const;
private:
    // What the page's layout depends on (:42-44); anything else updates in place.
    struct Structure {
        std::vector<std::uint64_t> records; std::vector<bool> pins; std::vector<std::uint64_t> visible;
        std::size_t capacity{}; Filter filter{};
        bool operator==(const Structure&) const = default;
    };
    ClipboardActions actions_;
    std::function<double()> wallClock_;
    ClipboardSnapshot snapshot_;
    Filter filter_{Filter::all};
    std::optional<std::uint64_t> focusedID_;
    std::vector<std::pair<std::uint64_t, design::ListCard*>> cards_;
    design::ScrollArea* list_{};
    std::optional<Structure> renderedStructure_;
    std::optional<std::string> lastStatus_;
    bool copying_{};
    design::Deadlines::Token ageTimer_{};
    Structure structure() const;
    std::vector<const ClipboardRecord*> ordered() const;     // pinned first (:91)
    std::vector<const ClipboardRecord*> visibleItems() const;
    const ClipboardRecord* item(std::uint64_t) const;
    void refreshAges();
    void scheduleAges();
    void confirmClear();
    void setFocus(std::optional<std::uint64_t>);
};
} // namespace endfield::modules
