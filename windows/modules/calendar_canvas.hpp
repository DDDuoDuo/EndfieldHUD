#pragma once
// Calendar on the redesign: the port of Sources/HUDCalendarCanvas.swift (Mac
// 1.3.0, spec section 3.17): the 42-cell month grid on the left with the
// month / year picker pop-over under the month title, the selected day's
// events, the inline Title / Details fields and Add or Save + Delete on the
// right. MODS-650..664.
//
// Events, reminders and their persistence belong to CalendarState
// (modules/calendar_state.hpp, the Mac HUDCalendarController); the app routes
// its change notice to controllerChanged(). Dates are formatted by the host
// (CalendarFormats: the native ICU civil context) in the UI language's
// locale, as the Mac's DateFormatter templates; the week starts on the
// user's calendar's first weekday.
//
// Removed with the v1.2.0 page (MODS-658, MODS-665): the pop-up event menu,
// its Date field (an event keeps its day; there is no move to another day),
// Cmd/Ctrl+Return save, the two-step inline delete and the permanent warning
// line (permission status shows only through the Reminders toast, MODS-660).
// Data-visible (MODS-661, Mac authority): the fields are single-line, so
// saving an event whose details had several lines stores them joined with
// spaces; unopened events keep their text.
#include "modules/calendar_state.hpp"
#include "modules/hud_design_page.hpp"

namespace endfield::modules {

struct CalendarCanvasOptions {
    CalendarFormats formats;                   // calendar_model.hpp (native: CalendarCivilContext::formats)
    CalendarTextRules text;                    // trimmingCharacters(in: .whitespacesAndNewlines)
};

class CalendarCanvas final : public design::Canvas {
public:
    CalendarCanvas(design::Context&, CalendarState&, CalendarCanvasOptions);
    ~CalendarCanvas() override;
    // The calendar loads its store and observes clock changes while shown or
    // active (setPresented + isActive -> CalendarState::setActive, :41-52):
    // the canvas is presented from show() until it leaves.
    void setPresented(bool);
    // HUDCalendarController.onChange (:87-92).
    void controllerChanged();
    // Actions by id, for accessibility stand-ins, tests and diagnostics
    // (:57-73): today, previousMonth, nextMonth, monthPicker, reminders, new,
    // add, save, delete, day:YYYY-MM-DD, month:YYYY-MM and event:UUID.
    void perform(std::string_view id);

    // --- Testing (the Mac's ...ForTesting) ------------------------------------------
    CalendarDay selectedDay() const noexcept { return selected_; }
    CalendarDay month() const noexcept { return month_; }
    const std::optional<std::string>& editingID() const noexcept { return editingID_; }
    std::vector<CalendarEvent> eventsForSelectedDay() const;
    design::TextField& titleField();
    design::TextField& detailsField();

    // --- design::Canvas ---------------------------------------------------------------
    void buildPage(design::Page&) override;
    void didInstall(design::Page&) override;
    void show(double now) override;
    void didActivate() override;
    void didDeactivate() override;
    void willLeave() override;
    void recordingModeChanged() override {}   // calendar content is not redacted (:45)
    bool handleExtraKey(const design::KeyInput&) override;
private:
    struct Structure {
        CalendarDay month, selected, today; std::optional<std::string> editing;
        std::vector<CalendarEvent> events; core::Language language{};
        bool operator==(const Structure&) const = default;
    };
    CalendarState* state_;
    CalendarCanvasOptions options_;
    CalendarDay selected_, month_;
    std::optional<std::string> editingID_;
    std::pair<std::string, std::string> draft_;    // typed for a new event while another is open
    bool presented_{}, controllerActive_{};
    std::optional<std::pair<std::optional<CalendarErrorCode>, std::string>> lastError_;
    std::optional<Structure> renderedStructure_;
    std::vector<std::function<void()>> binders_;
    design::TextField *titleField_{}, *detailsField_{};   // lazy, canvas-owned (canvasBuilder)
    struct FieldSlot { design::Page* page{}; design::Layer* column{}; double top{}; bool editing{}; };
    FieldSlot fieldSlot_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);  // [weak self] of the controller completions

    Structure structure() const;
    std::vector<CalendarEvent> events(CalendarDay) const;
    std::optional<std::string> errorText() const;
    std::string formatted(CalendarDay, std::string_view skeleton) const;
    std::string uppercase(std::string_view) const;
    struct Cell { CalendarDay day; bool outside{}; };
    std::vector<Cell> cells() const;
    void syncController();
    void refresh();
    void buildMonth(design::Page&);
    void buildEvents(design::Page&);
    design::TextField& field(design::TextField*&, std::string_view id, std::string_view english, std::string_view chinese, std::size_t limit);
    void changeMonth(int step);
    void goToday();
    void openMonthPicker(const design::Control& anchor);
    void pick(CalendarDay first);
    void showDay(CalendarDay);
    void toggle(const std::string& id);
    void stopEditing();
    void addEvent();
    void saveEvent();
    void confirmDelete();
    void refreshReminders();
};
} // namespace endfield::modules
