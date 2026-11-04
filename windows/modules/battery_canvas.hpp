#pragma once
// The Battery screen of the redesign (module Power, reached from the ⚡ chip):
// the port of Sources/BatteryCanvas.swift (BatteryCanvas, BatteryReadingPlate;
// MODS-456..466) on the design library. It replaces the v1.2.0 Power page
// (modules/battery_presentation, tools/battery_preview: "// BATTERY STATUS",
// laptop glyph, state line, "Battery alert ›"), whose strings are gone.
//
// Every value is a real reading (modules::BatteryReading: Windows' percentage,
// power state, minutes to empty, the battery class driver's capacity pair,
// cycles and temperature) plus the charge alert's metric. The host forwards
// every reading and metric change (update); values and colours update in
// place, a language change re-renders. Windows has no OS time-to-full
// estimate and no adapter rating: UNTIL FULL reads "—" while charging and the
// SOURCE plate shows "External" without watts (recorded platform gaps; the
// Mac reads IOPS Time to Full Charge and the adapter's Watts).
//
// The charging pulse (HUDCells, 2.4 s, the next three empty cells) is the
// only live loop and runs only while Battery is the active module.
#include "modules/battery_reading.hpp"
#include "modules/charge_indicator.hpp"
#include "modules/hud_design_badges.hpp"
#include "modules/hud_design_page.hpp"
#include <array>
#include <map>

namespace endfield::modules {
class BatteryCanvas final : public design::Canvas {
public:
    // BatteryCanvas.Tone (:20-46): BatteryLevelTone's thresholds (> 70 good,
    // 20...70 moderate, < 20 low) with the tag, colour, cell gradient and the
    // pulse (`cellPulseB`: the tone mixed 35 % into #2A2A2A).
    enum class Tone : std::uint8_t { good, moderate, low };
    static std::optional<Tone> tone(std::optional<unsigned> level) noexcept;
    static std::string_view tag(Tone) noexcept;
    static design::Color color(Tone) noexcept;
    static design::Cells::Tone cellTone(Tone) noexcept;
    static design::Color pulse(Tone) noexcept;
    // BatteryCanvas.condition (:223-233): macOS's battery health in the words
    // System Settings uses; other reports appear as written. Windows reports
    // no category (native/battery_capacity.hpp), so it reads "—" there.
    static std::string condition(const std::optional<std::string>&, core::Language);

    explicit BatteryCanvas(design::Context&);
    // The BAR READING plate opens Display > Battery alert (the v1.2.0 Power
    // screen's link). The host wires it.
    std::function<void()> onOpenBatteryAlert;
    // Every battery reading and alert metric change (BatteryCanvas.update):
    // nothing when equal; before the first build only stored; a page built
    // in another language re-renders, else values update in place (animated).
    void update(BatteryReading, ChargeMetric, double now);
    const BatteryReading& reading() const noexcept { return reading_; }
    ChargeMetric metric() const noexcept { return metric_; }
    // Cells pulsing on the current page (the host's live-loop diagnostics).
    int liveAnimationCount() const noexcept;

    void buildPage(design::Page&) override;
    void didActivate() override { applyCells(false); }
    void didDeactivate() override { applyCells(false); }
    void willLeave() override { applyCells(false, false); }
private:
    class ReadingPlate;
    struct CellState { int filled{}; std::optional<Tone> tone; bool pulsing{}; bool operator==(const CellState&) const = default; };
    struct Live {
        design::BigNumeral* numeral{};
        design::Layer *status{}, *caption{}, *timeLabel{}, *time{};
        design::Cells* cells{};
        std::array<design::StatPlate*, 6> plates{};
        design::Readout* hero{};
        std::array<design::Readout*, 5> readouts{};
        ReadingPlate* reading{};
        std::map<std::string, design::Chip*, std::less<>> chips; // made once per page, reattached by state
    };
    BatteryReading reading_;
    ChargeMetric metric_{ChargeMetric::battery};
    std::optional<Live> live_;
    std::optional<CellState> cellState_;
    design::Page* livePage_{};
    std::optional<Tone> currentTone() const noexcept;
    std::string timeText() const;
    design::Chip& chip(std::string_view tag, design::Chip::Variant, std::optional<design::Icon> = {},
        std::optional<std::pair<design::Color, design::Color>> custom = {});
    void apply(bool animated);
    void applyCells(bool animated, bool pulseAllowed = true);
    void applyPlates();
};
} // namespace endfield::modules
