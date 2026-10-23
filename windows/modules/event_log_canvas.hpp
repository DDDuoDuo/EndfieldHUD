#pragma once
// Event Log (spec section 3.19): the port of Sources/EventLogCanvas.swift on
// the design library. Filter tabs over a scrolling list of log rows (time,
// category chip, one localised sentence) with the micro tag; Clear asks
// first through the shared confirm bar. Only the rows in view (and one either
// side) are in the tree, so a 500-record log keeps the layer tree small; the
// store is observed only while the module is active. The store, its save
// queue and the recorder are unchanged (core/data/event_log_store.*,
// native/event_log_owner.*, app/event_log_save_queue.*,
// core/application_event_recorder.*).
//
// Gone with the v1.2.0 page (inventory MODS-700..720): the 400 x 334 canvas,
// the "// Event Log" heading and "N events" status line, eight category
// chips, row selection and its stroke trace, the footer clear confirmation,
// the two-plane handoff, the MM-dd HH:mm:ss timestamps and the per-row help
// text. Rows are not redacted in Recording mode (recordingModeChanged is a
// no-op, :260).
#include "modules/event_log.hpp"
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_page.hpp"

namespace endfield::modules {
// EventLogFilter (:7-34): records keep their stored kind; the tabs group them.
enum class EventLogFilter : std::uint8_t { all, timer, shelf, devices, other };
inline constexpr std::size_t eventLogFilterCount = 5;
EventLogFilter eventLogFilterOf(EventKind) noexcept;
bool eventLogFilterIncludes(EventLogFilter, EventKind) noexcept;
std::string eventLogFilterTitle(EventLogFilter, core::Language);
// Timer = yellow, Devices = cyan, otherwise dark.
design::Chip::Variant eventLogFilterChip(EventLogFilter) noexcept;
// EventLogText.line (:38-211): one localised sentence per record; only the
// allowlisted metadata the log stores (names, kinds, durations) appears.
std::string eventLogLine(const SystemEvent&, core::Language);
// EventLogCanvas.time (:367-372): `06:45` today (24-hour, whatever the 12-hour
// preference); records from another day also carry `10.19` under it.
struct EventLogTime { std::string clock; std::optional<std::string> day; bool operator==(const EventLogTime&) const = default; };
EventLogTime eventLogTime(double createdAt, double now, const EventLogLocalTime&);

class EventLogCanvas final : public design::Canvas {
public:
    static constexpr double listTop = 125, listHeight = 350, rowHeight = 40, pitch = 46;
    // `now`: Foundation-epoch seconds now (read when the page builds, as the Mac's Date()).
    EventLogCanvas(design::Context&, EventLogCallbacks store, EventLogLocalTime, std::function<double()> now);
    // The store's observer (:251, :262-267): the host calls it after every
    // change; it acts only while the canvas observes (active), as the Mac
    // observes only between didActivate and didDeactivate.
    void storeChanged();
    // Records the log lists: routine navigation stays saved but hidden (:239).
    std::vector<SystemEvent> events() const;
    EventLogFilter filter() const noexcept { return filter_; }
    std::span<const SystemEvent> shown() const noexcept { return shown_; }

    // --- design::Canvas ---
    void buildPage(design::Page&) override;
    void show(double now) override;               // makeContent (:243-247)
    void didActivate() override;
    void didDeactivate() override;
    void recordingModeChanged() override {}       // rows are not redacted (:260)
    bool handleScroll(design::Point, design::Point delta) override;
    bool handleKey(const design::KeyInput&) override;

    // Test hooks (:428-430).
    std::size_t rowCountForTesting() const noexcept { return rows_.size(); }
    double listOffsetForTesting() const noexcept;
    void scrollListForTesting(double delta) { scrollList(delta); }
private:
    EventLogCallbacks store_;
    EventLogLocalTime localTime_;
    std::function<double()> now_;
    bool observing_{};
    EventLogFilter filter_{EventLogFilter::all};
    std::optional<std::vector<SystemEvent>> renderedEvents_;
    std::vector<SystemEvent> shown_;
    // The virtualised rows by index. Rows that leave the window are detached
    // and kept for the page's lifetime (a page arena cannot free single
    // layers): one returning to view is attached again instead of rebuilt.
    std::map<std::size_t, design::RowPlate*> rows_, parked_;
    design::ScrollArea* list_{};
    design::Page* listPage_{};
    double listOffset_{}, rowWidth_{design::metric::boxWidth};
    std::optional<std::string> lastStatus_;
    std::optional<std::string> status() const;
    void refresh();
    std::optional<std::pair<std::string, double>> topVisibleAnchor() const;
    void updateRows(design::Page&, bool initial);
    design::RowPlate& makeRow(design::Page&, const SystemEvent&);
    void select(EventLogFilter, double direction);
    void confirmClear();
    void scrollList(double delta);
};
} // namespace endfield::modules
