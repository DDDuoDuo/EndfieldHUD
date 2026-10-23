// Calendar on the redesign: the port of Sources/HUDCalendarCanvas.swift (see
// the header). Line references are that file's (Mac 1.3.0).
//
// Adaptations: the Title / Details fields are canvas-owned (the Mac's lazy
// vars) and join each page when it is installed (didInstall): the next page
// is built when a month swap starts (Q4), and the fields leave with the old
// page meanwhile, as on the Mac. The fields' label follows the UI language
// and their frame the palette at every build (the Mac's lazy fields keep the
// language and theme of their first use: a Mac bug, reported, not copied). Error toasts translate a typed
// CalendarState error at display time (calendarErrorMessage).
#include "modules/calendar_canvas.hpp"
#include "modules/calendar_localization.hpp"
#include "modules/hud_design_badges.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>

namespace endfield::modules {
namespace d = design;
namespace {
using d::Point; using d::Rect; using d::Channel;
std::string two(int value) { char text[8]; std::snprintf(text, sizeof text, "%02d", value); return text; }
// The grid's Gregorian arithmetic past the stored range: the Mac shows the
// trailing / leading days of 1900-01 and 9999-12 (1899-12-31, 10000-01-01).
std::chrono::sys_days civil(CalendarDay day) { return std::chrono::year{day.year} / std::chrono::month{unsigned(day.month)} / std::chrono::day{unsigned(day.day)}; }
CalendarDay shifted(CalendarDay day, int days) {
    const std::chrono::year_month_day ymd{civil(day) + std::chrono::days{days}};
    return {int(ymd.year()), int(unsigned(ymd.month())), int(unsigned(ymd.day()))};
}
unsigned weekdayOf(CalendarDay day) { return std::chrono::weekday{civil(day)}.c_encoding() + 1; } // Sunday = 1

// CalendarDayCell (:487-534): one day of the 42-cell grid, also a month of the
// picker: outside-month, weekend, event diamond, today (accent) and selected
// (inset ring) states, restyled in place; hover lifts 1 px.
class DayCell final : public d::Control {
public:
    struct State {
        bool outside{}, weekend{}, event{}, today{}, selected{};
        bool operator==(const State&) const = default;
    };
    DayCell(d::Builder& b, std::string id, std::string label, std::string value, State state, d::Size size, double fontSize = 14)
        : Control(b.env, id, std::move(label), b.layer(id)), state_(state), fontSize_(fontSize) {
        auto& l = layer();
        l.setBounds(size);
        l.setAnimatable();                           // the hover lift
        const Rect bounds{0, 0, size.width, size.height};
        brighten_ = &b.layer(id + ".brighten");
        brighten_->setFrame(bounds);
        d::setBackground(*brighten_, palette().foreground(.06));
        brighten_->setAnimatable(d::SurfaceRole::lazy);
        brighten_->setValue(Channel::opacity, 0);
        text_ = &d::makeText(b, id + ".text", std::move(value), d::TextStyle{fontSize, d::FontWeight::regular, palette().token(d::ThemeToken::textOnDark), 1, 0, false, true},
            {0, (size.height - fontSize) / 2, size.width, fontSize}, d::TextAlignment::center);
        l.add(*brighten_); l.add(*text_);
        if (state.event && !state.outside) {
            auto& mark = d::badge::diamond(b, id + ".event", 5);
            mark.setPosition({size.width / 2, size.height - 5 - 2.5});
            l.add(mark);
        }
        // CALayer draws its border above the sublayers: a ring child on top.
        ring_ = &b.layer(id + ".ring");
        ring_->setFrame(bounds);
        l.add(*ring_);
        applyState();
        if (!state.outside) setTarget(bounds);
    }
    const State& state() const noexcept { return state_; }
    void setState(State next) { if (next != state_) { state_ = next; applyState(); } }
    void hoverChanged() override {
        if (state_.outside) return;
        const bool on = hovered() && enabled();
        d::motion::animate(layer(), Channel::translateY, on ? -1 : 0, d::duration::hover, clock());
        d::motion::animate(*brighten_, Channel::opacity, on ? 1 : 0, d::duration::hover, clock(), d::easing::ease);
    }
    std::optional<d::AccessibilityRole> accessibilityRole() const override {
        return state_.outside ? std::nullopt : std::optional(d::AccessibilityRole::tab);
    }
    void accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const override {
        if (state_.outside || label.empty()) return;
        auto item = accessibilityItem(root, d::AccessibilityRole::tab);
        item.on = state_.selected;
        out.push_back(std::move(item));
    }
private:
    State state_;
    double fontSize_;
    d::Layer *brighten_, *text_, *ring_;
    void applyState() {
        const auto& p = palette();
        const bool today = state_.today && !state_.outside, selected = state_.selected && !state_.outside;
        const d::Color color = state_.outside ? p.tone(0x555555) : today ? d::fixed::nearBlack : state_.weekend ? p.tone(0x9A9A9A) : p.tone(0xDDDDDD);
        d::editText(*text_, [&](d::TextBlock& block) {
            block.setStyle(d::TextStyle{fontSize_, today ? d::FontWeight::bold : d::FontWeight::regular, color, 1, 0, false, true});
        });
        d::setBackground(layer(), state_.outside ? p.tone(0x181818) : today ? p.yellow() : p.tone(0x1F1F1F));
        const double width = selected ? 2 : (today ? 0 : 1);
        if (width > 0) d::setBorder(*ring_, std::nullopt, selected ? (today ? d::fixed::nearBlack : p.tone(0xEDEDED)) : p.token(d::ThemeToken::separator), width);
        else ring_->clearContent();
    }
};

// CalendarMonthTitle (:538-567): the month over the grid; it opens the month
// picker. Text and caret centre on its position; hover lifts it 1 px under a
// faint plate and the caret brightens.
class MonthTitle final : public d::Control {
public:
    MonthTitle(d::Builder& b, std::string id, std::string value) : Control(b.env, id, value, b.layer(id)) {
        const auto& p = palette();
        const d::TextStyle style{17, d::FontWeight::medium, p.token(d::ThemeToken::textOnDark), 1, .04};
        const double textWidth = std::min(std::ceil(text().width(value, style)), 200.0);
        const Rect bounds{0, 0, 10 + textWidth + 8 + 8 + 10, 28};
        auto& l = layer();
        l.setBounds({bounds.width, bounds.height});
        l.setAnimatable();
        brighten_ = &b.layer(id + ".brighten");
        brighten_->setFrame(bounds);
        d::setBackground(*brighten_, p.foreground(.06));
        brighten_->setAnimatable(d::SurfaceRole::lazy);
        brighten_->setValue(Channel::opacity, 0);
        auto& caption = d::makeText(b, id + ".text", std::move(value), style, {10, 14 - 8.5, textWidth + 1, 17});
        caret_ = &b.layer(id + ".caret");
        caret_->setFrame({10 + textWidth + 8, 12, 8, 4.5});
        d::setTintedPath(*caret_, d::PathBuilder().move({0, 0}).line({8, 0}).line({4, 4.5}).close(), p.token(d::ThemeToken::steel));
        l.add(*brighten_); l.add(caption); l.add(*caret_);
        setTarget(bounds);
    }
    void hoverChanged() override {
        d::applyHoverMotion(*this, 1, 1, .97, brighten_);
        const auto& p = palette();
        d::motion::animateTint(*caret_, hovered() && enabled() ? p.token(d::ThemeToken::textOnDark) : p.token(d::ThemeToken::steel), d::duration::hover, clock(), d::easing::ease);
    }
    void pressChanged() override { d::applyHoverMotion(*this, 1, 1, .97, brighten_); }
    std::optional<d::AccessibilityRole> accessibilityRole() const override { return d::AccessibilityRole::popUp; }
    void accessibilityItems(const d::Layer& root, std::vector<d::AccessibilityItem>& out) const override {
        if (label.empty()) return;
        auto item = accessibilityItem(root, d::AccessibilityRole::popUp);
        item.value = label;
        out.push_back(std::move(item));
    }
private:
    d::Layer *brighten_, *caret_;
};

// CalendarEventPlate (:571-605): the light chamfered plate with the orange
// bar, title and detail (two lines each); the open event is dark.
class EventPlate final : public d::Control {
public:
    static d::TextStyle titleStyle() { return {13.5, d::FontWeight::medium, d::fixed::nearBlack, 1.3}; }
    static d::TextStyle detailStyle() { return {11, d::FontWeight::regular, d::rgb(0x555555), 1.3}; }
    EventPlate(d::Builder& b, std::string id, const CalendarEvent& event, bool selected, double width)
        : Control(b.env, id, event.details.empty() ? event.title : event.title + ", " + event.details, b.layer(id)) {
        const double textWidth = width - 26;
        const std::string details = d::joinedLines(event.details);
        const auto lines = [&](const std::string& value, const d::TextStyle& style) {
            const d::TextRun run{value, style};
            return std::max<std::size_t>(1, text().lineCount(std::span(&run, 1), textWidth, 2));
        };
        const double titleHeight = double(lines(event.title, titleStyle())) * 13.5 * 1.3;
        const double detailHeight = details.empty() ? 0 : double(lines(details, detailStyle())) * 11 * 1.3;
        const Rect bounds{0, 0, width, std::ceil(16 + titleHeight + detailHeight)};
        auto& l = layer();
        l.setBounds({bounds.width, bounds.height});
        auto& plate = b.layer(id + ".plate");
        plate.setFrame(bounds);
        const std::array<d::Color, 2> colors = selected ? std::array<d::Color, 2>{d::fixed::charcoal, d::fixed::charcoal} : std::array<d::Color, 2>{d::rgb(0xE2E3DE), d::rgb(0xCFD0CB)};
        plate.setContent(d::plate(plate.id(), bounds, colors, {}, 9));
        auto& bar = b.layer(id + ".bar");
        bar.setFrame({0, 8, 3, bounds.height - 16});
        d::setBackground(bar, d::fixed::orange);
        auto& title = d::makeText(b, id + ".title", event.title, titleStyle().colored(selected ? d::rgb(0xF2F2F2) : d::fixed::nearBlack), {16, 8, textWidth, titleHeight});
        auto& detail = d::makeText(b, id + ".detail", details, detailStyle().colored(selected ? d::rgb(0xAAAAAA) : d::rgb(0x555555)), {16, 8 + titleHeight, textWidth, detailHeight});
        d::editText(title, [](d::TextBlock& block) { block.maximumLines = 2; });
        d::editText(detail, [](d::TextBlock& block) { block.maximumLines = 2; });
        detail.setHidden(details.empty());
        l.add(plate); l.add(bar); l.add(title); l.add(detail);
        setTarget(bounds, d::chamfer(bounds, 9));
    }
};
}

// ---------------------------------------------------------------------------
// HUDCalendarCanvas (:8-482)
// ---------------------------------------------------------------------------
CalendarCanvas::CalendarCanvas(d::Context& context, CalendarState& state, CalendarCanvasOptions options)
    : Canvas(core::Module::calendar, context), state_(&state), options_(std::move(options)), selected_(state.today()), month_{selected_.year, selected_.month, 1} {}
CalendarCanvas::~CalendarCanvas() = default;

// --- Host interface (:41-73) -----------------------------------------------------------
void CalendarCanvas::setPresented(bool value) { presented_ = value; syncController(); }
void CalendarCanvas::show(double now) { Canvas::show(now); setPresented(true); }
void CalendarCanvas::willLeave() { setPresented(false); }
void CalendarCanvas::didActivate() {
    syncController();
    if (const auto& error = state_->error()) lastError_ = std::pair{state_->errorCode(), *error}; else lastError_.reset();
    refresh();
}
void CalendarCanvas::didDeactivate() { syncController(); }
void CalendarCanvas::syncController() {
    const bool wanted = presented_ || isActive();
    if (wanted == controllerActive_) return;
    controllerActive_ = wanted;
    state_->setActive(wanted);
}
void CalendarCanvas::perform(std::string_view id) {
    if (id == "today") goToday();
    else if (id == "monthPicker") { for (auto* control : page().controls()) if (control->id() == "calendar.monthPicker") { openMonthPicker(*control); break; } }
    else if (id == "previousMonth") changeMonth(-1);
    else if (id == "nextMonth") changeMonth(1);
    else if (id == "reminders") refreshReminders();
    else if (id == "new") { stopEditing(); refresh(); titleField().beginEditing(); }
    else if (id == "add") addEvent();
    else if (id == "save") saveEvent();
    else if (id == "delete") confirmDelete();
    else if (id.starts_with("day:")) { if (const auto day = CalendarDay::parse(id.substr(4))) showDay(*day); }
    else if (id.starts_with("month:")) { if (const auto day = CalendarDay::parse(std::string(id.substr(6)) + "-01")) pick(*day); }
    else if (id.starts_with("event:")) toggle(std::string(id.substr(6)));
}

// --- State (:77-137) ------------------------------------------------------------------
CalendarCanvas::Structure CalendarCanvas::structure() const {
    const auto list = state_->events();
    return {month_, selected_, state_->today(), editingID_, std::vector<CalendarEvent>(list.begin(), list.end()), env().language};
}
std::vector<CalendarEvent> CalendarCanvas::events(CalendarDay day) const {
    std::vector<CalendarEvent> out;
    for (const auto& event : state_->events()) if (event.day == day) out.push_back(event);
    std::stable_sort(out.begin(), out.end(), [](const CalendarEvent& a, const CalendarEvent& b) { return a.created == b.created ? a.id < b.id : a.created < b.created; });
    return out;
}
std::vector<CalendarEvent> CalendarCanvas::eventsForSelectedDay() const { return events(selected_); }
std::optional<std::string> CalendarCanvas::errorText() const {
    if (const auto code = state_->errorCode()) return calendarErrorMessage(*code, env().language);
    return state_->error();
}
void CalendarCanvas::controllerChanged() {
    std::optional<std::pair<std::optional<CalendarErrorCode>, std::string>> error;
    if (const auto& text = state_->error()) error = std::pair{state_->errorCode(), *text};
    if (error && error != lastError_ && isActive()) toast(d::Icon::warn, "Calendar", "日历", errorText());
    lastError_ = error;
    if (editingID_) {
        const auto list = state_->events();
        if (std::none_of(list.begin(), list.end(), [&](const CalendarEvent& e) { return e.id == *editingID_; })) stopEditing();
    }
    refresh();
}
// Structure changes re-render in place; busy flips update the buttons.
void CalendarCanvas::refresh() {
    if (!renderedLanguage() || transitioning()) return;
    if (structure() != renderedStructure_) {
        rerender();
        if (editingText()) refreshTextFieldProjection();
    } else for (const auto& bind : binders_) bind();
}
std::string CalendarCanvas::formatted(CalendarDay day, std::string_view skeleton) const {
    return options_.formats.format ? options_.formats.format(day, skeleton, env().language) : day.string();
}
// uppercased(with: Locale(identifier: "en_US_POSIX")).
std::string CalendarCanvas::uppercase(std::string_view value) const { return env().text.uppercase ? env().text.uppercase(value) : d::asciiUppercase(value); }
// The 42 cells: trailing days of the previous month, the month, then the next.
std::vector<CalendarCanvas::Cell> CalendarCanvas::cells() const {
    const unsigned first = options_.formats.firstWeekday ? options_.formats.firstWeekday() : 1;
    const int padding = (int(weekdayOf(month_)) - int(first) + 7) % 7;
    std::vector<Cell> out;
    for (int index = 0; index < 42; ++index) {
        const auto day = shifted(month_, index - padding);
        out.push_back({day, day.month != month_.month || day.year != month_.year});
    }
    return out;
}

// --- Page (:141-295) --------------------------------------------------------------------
void CalendarCanvas::buildPage(d::Page& page) {
    renderedStructure_ = structure();
    binders_.clear();
    auto& today = page.make<d::Button>("calendar.today", text("Today", "今天"), std::nullopt, d::Button::Kind::secondary, d::Button::Height::compact);
    today.place({0, 0});
    today.onActivate = [this] { goToday(); };
    auto& reminders = page.make<d::IconButton>("calendar.reminders", text("Refresh reminders", "刷新提醒"), d::Icon::refresh);
    reminders.onActivate = [this] { refreshReminders(); };
    binders_.push_back([this, &reminders] { reminders.setEnabled(!state_->busy()); });
    const auto count = d::format::pad2(std::int64_t(state_->events().size()));
    const std::array<std::string_view, 1> args{count};
    header().configure({"CALENDAR"}, "MOD-12", title("Calendar", "日历"), text("{0} EVENTS", "{0} 个事项", args), false, {&today, &reminders});
    buildMonth(page);
    buildEvents(page);
}
void CalendarCanvas::buildMonth(d::Page& page) {
    const auto& p = env().palette;
    auto& column = page.makeLayer("calendar.month");
    auto& previous = page.make<d::IconButton>("calendar.previousMonth", text("Previous month", "上个月"), d::Icon::left);
    auto& next = page.make<d::IconButton>("calendar.nextMonth", text("Next month", "下个月"), d::Icon::right);
    previous.setEnabled(month_.year > 1900 || month_.month > 1);
    next.setEnabled(month_.year < 9999 || month_.month < 12);
    previous.onActivate = [this] { changeMonth(-1); };
    next.onActivate = [this] { changeMonth(1); };
    auto& title = page.make<MonthTitle>("calendar.monthPicker", formatted(month_, "yMMMM"));
    title.onActivate = [this, &title] { if (popoverOpen()) closePopover(); else openMonthPicker(title); };
    const double cellWidth = (322.0 - 6 * 4) / 7;
    const d::TextStyle weekdayStyle{9.5, d::FontWeight::regular, p.token(d::ThemeToken::steel), 1, .1};
    const auto symbols = options_.formats.shortWeekdays ? options_.formats.shortWeekdays(env().language) : std::array<std::string, 7>{};
    const unsigned first = options_.formats.firstWeekday ? options_.formats.firstWeekday() : 1;
    std::set<CalendarDay> occupied;
    for (const auto& event : state_->events()) occupied.insert(event.day);
    const auto today = state_->today();
    std::vector<d::Control*> controls{&previous, &title, &next};
    column.setFrame({0, d::metric::headerHeight, 322, 314});
    previous.layer().setOrigin({0, 0});
    next.layer().setOrigin({292, 0});
    // Centred between the arrows, sized to the month name.
    title.layer().setPosition({161, 15});
    column.add(previous.layer()); column.add(title.layer()); column.add(next.layer());
    if (options_.formats.shortWeekdays)
        for (int index = 0; index < 7; ++index)
            column.add(d::makeText(page.builder(), "calendar.weekday" + std::to_string(index), uppercase(symbols[std::size_t((index + int(first) - 1) % 7)]),
                weekdayStyle, {index * (cellWidth + 4), 39.5, cellWidth, 9.5}, d::TextAlignment::center));
    const auto list = cells();
    for (std::size_t index = 0; index < list.size(); ++index) {
        const auto& cell = list[index];
        const Rect frame{double(index % 7) * (cellWidth + 4), 54 + double(index / 7) * 44, cellWidth, 40};
        const unsigned weekday = weekdayOf(cell.day);
        const DayCell::State state{cell.outside, weekday == 1 || weekday == 7, occupied.contains(cell.day), cell.day == today, cell.day == selected_};
        auto& control = page.make<DayCell>("calendar.day:" + cell.day.string(), formatted(cell.day, "yMMMMd"), std::to_string(cell.day.day), state, d::Size{frame.width, frame.height});
        control.layer().setFrame(frame);
        column.add(control.layer());
        if (!cell.outside) {
            const CalendarDay day = cell.day;
            control.onActivate = [this, day] { showDay(day); };
            controls.push_back(&control);
        }
    }
    page.layer().add(column);
    page.add(controls);
    page.stagger(column);
}
void CalendarCanvas::buildEvents(d::Page& page) {
    const auto& p = env().palette;
    auto& column = page.makeLayer("calendar.events");
    const double width = 254;
    auto& number = d::makeText(page.builder(), "calendar.date", d::format::pad2(selected_.day), d::type::numeral(p).sized(54), {});
    auto& weekday = d::makeText(page.builder(), "calendar.weekdayName", uppercase(formatted(selected_, "EEEE")), d::type::label(p), {});
    const double numberWidth = std::ceil(d::preferredWidth(d::textOf(number), env().text));
    column.setFrame({336, d::metric::headerHeight, width, 314});
    d::setLine(number, {0, 0, numberWidth + 1, 54 * .9});
    d::setLine(weekday, {numberWidth + 10, 49 - 6 - 10, width - numberWidth - 10, 10});
    column.add(number); column.add(weekday);
    page.layer().add(column);
    // The day's plates: a scroll area 40-150 px tall.
    const auto list = events(selected_);
    std::vector<d::Control*> plates;
    double y = 0;
    for (const auto& event : list) {
        auto& plate = page.make<EventPlate>("calendar.event:" + event.id, event, editingID_ == event.id, width - 6);
        const std::string id = event.id;
        plate.onActivate = [this, id] { toggle(id); };
        plate.layer().setOrigin({0, y});
        y += plate.layer().bounds().height + 6;
        plates.push_back(&plate);
    }
    const double contentHeight = std::max(0.0, y - 6);
    const double listHeight = std::min(150.0, std::max(40.0, contentHeight));
    auto& area = page.scrollArea("calendar.events:" + selected_.string(), {0, 55, width, listHeight}, &column);
    for (auto* plate : plates) area.content().add(plate->layer());
    if (list.empty()) area.content().add(d::makeText(page.builder(), "calendar.empty", text("NO EVENTS", "暂无事项"), d::type::label(p), {0, 10, width, 10}));
    area.setContentHeight(contentHeight);
    page.restoreOffset(area);
    page.add(plates);
    // Title, details and the buttons follow the list; the fields join the page when it is installed.
    const bool editing = editingID_ && std::any_of(state_->events().begin(), state_->events().end(), [&](const CalendarEvent& e) { return e.id == *editingID_; });
    const double fieldTop = 55 + listHeight + 6;
    fieldSlot_ = {&page, &column, fieldTop, editing};
    const double buttonTop = fieldTop + 76;
    std::vector<d::Control*> buttons;
    if (editing) {
        auto& erase = page.make<d::Button>("calendar.delete", text("Delete", "删除"), d::Icon::trash, d::Button::Kind::secondary, d::Button::Height::compact);
        erase.place({0, 0});
        auto& save = page.make<d::Button>("calendar.save", text("Save", "保存"), d::Icon::check, d::Button::Kind::primary, d::Button::Height::compact);
        save.place({0, buttonTop}, width - 6 - erase.layer().bounds().width);
        erase.layer().setOrigin({width - erase.layer().bounds().width, buttonTop});
        save.onActivate = [this] { saveEvent(); };
        erase.onActivate = [this] { confirmDelete(); };
        buttons = {&save, &erase};
    } else {
        auto& add = page.make<d::Button>("calendar.add", text("Add event", "添加事项"), d::Icon::plus, d::Button::Kind::primary, d::Button::Height::compact);
        add.place({0, buttonTop}, width);
        add.onActivate = [this] { addEvent(); };
        buttons = {&add};
    }
    for (auto* button : buttons) column.add(button->layer());
    page.add(buttons);
    binders_.push_back([this, buttons] { for (auto* button : buttons) button->setEnabled(!state_->busy()); });
    for (const auto& bind : binders_) bind();
    page.stagger(column);
}
// The lazy fields' placement (:256-263) once their page is installed.
void CalendarCanvas::didInstall(d::Page& page) {
    if (fieldSlot_.page != &page || !fieldSlot_.column) return;
    auto& title = titleField();
    auto& details = detailsField();
    title.setPlaceholder(fieldSlot_.editing ? text("Title", "标题") : text("New event", "新事项"));
    details.setPlaceholder(text("Details", "详情"));
    for (std::size_t index = 0; index < 2; ++index) {
        auto& field = index == 0 ? title : details;
        registerField(field);
        field.layer().setOrigin({0, fieldSlot_.top + double(index) * 38});
        fieldSlot_.column->add(field.layer());
    }
    fieldSlot_ = {};
}
d::TextField& CalendarCanvas::field(d::TextField*& slot, std::string_view id, std::string_view english, std::string_view chinese, std::size_t limit) {
    if (!slot) {
        d::TextFieldOptions options;
        options.maximumLength = limit;
        slot = &canvasBuilder().make<d::TextField>(std::string(id), text(english, chinese), "", "", d::Size{254, 32}, options);
    } else if (!slot->editing()) {
        slot->label = text(english, chinese);
        slot->frame().setState(slot->frame().state());   // the current palette (the Mac's lazy fields keep their first theme: not copied)
    }
    return *slot;
}
d::TextField& CalendarCanvas::titleField() { return field(titleField_, "calendar.title", "Title", "标题", 120); }
d::TextField& CalendarCanvas::detailsField() { return field(detailsField_, "calendar.details", "Details", "详情", 2000); }

// --- Navigation (:299-409) -------------------------------------------------------------
// The month changes, day 1 is selected, editing closes (subnav -/+1).
void CalendarCanvas::changeMonth(int step) {
    const auto next = month_.advancedMonth(step);
    if (!next || next->year < 1900 || next->year > 9999) return;
    month_ = {next->year, next->month, 1};
    selected_ = month_;
    stopEditing();
    navigate(step);
}
// Today enters from the side its month lies on, or re-renders in place.
void CalendarCanvas::goToday() {
    const auto today = state_->today();
    const CalendarDay target{today.year, today.month, 1};
    selected_ = today;
    stopEditing();
    if (target == month_) { refresh(); return; }
    const bool forward = target > month_;
    month_ = target;
    navigate(forward ? 1 : -1);
}
// The title's pop-over (:325-375): a year stepper over a 4 x 3 month grid.
// Today's month is the accent cell and the shown month carries the selection
// ring, as in the day grid; stepping the year updates the cells in place.
void CalendarCanvas::openMonthPicker(const d::Control& anchor) {
    const auto lines = title("SELECT MONTH", "选择月份").lines(env().language);
    const double width = 270, inner = width - 24, gap = 4, rowHeight = 32;
    auto popover = std::make_unique<d::Popover>(env(), lines.title, lines.subtitle.empty() ? std::nullopt : std::optional(lines.subtitle), width,
        30 + 10 + 3 * rowHeight + 2 * gap);
    auto& b = popover->builder();
    auto year = std::make_shared<int>(month_.year);
    auto& previous = b.make<d::IconButton>("calendar.previousYear", text("Previous year", "上一年"), d::Icon::left);
    auto& next = b.make<d::IconButton>("calendar.nextYear", text("Next year", "下一年"), d::Icon::right);
    auto& yearText = d::makeText(b, "calendar.year", "", d::TextStyle{17, d::FontWeight::medium, env().palette.token(d::ThemeToken::textOnDark), 1, .04, false, true},
        {30, 15 - 8.5, inner - 60, 17}, d::TextAlignment::center);
    const auto symbols = options_.formats.shortMonths ? options_.formats.shortMonths(env().language) : std::array<std::string, 12>{};
    const double cellWidth = (inner - 3 * gap) / 4;
    std::vector<DayCell*> cells;
    for (int number = 1; number <= 12; ++number) {
        const std::string symbol = options_.formats.shortMonths ? uppercase(symbols[std::size_t(number - 1)]) : std::to_string(number);
        auto& cell = b.make<DayCell>("calendar.month:" + two(number), symbol, symbol, DayCell::State{}, d::Size{cellWidth, rowHeight}, 13.0);
        cell.onActivate = [this, year, number] { pick({*year, number, 1}); };
        cells.push_back(&cell);
    }
    const auto render = [this, year, &previous, &next, &yearText, cells] {
        const auto today = state_->today();
        d::editText(yearText, [&](d::TextBlock& block) { block.setText(std::to_string(*year)); });
        previous.setEnabled(*year > 1900); next.setEnabled(*year < 9999);
        for (std::size_t index = 0; index < cells.size(); ++index) {
            const int number = int(index) + 1;
            DayCell::State state;
            state.today = today.year == *year && today.month == number;
            state.selected = month_.year == *year && month_.month == number;
            cells[index]->setState(state);
            cells[index]->label = formatted({*year, number, 1}, "yMMMM");
        }
    };
    previous.onActivate = [year, render] { *year = std::max(1900, *year - 1); render(); };
    next.onActivate = [year, render] { *year = std::min(9999, *year + 1); render(); };
    previous.layer().setOrigin({0, 0});
    next.layer().setOrigin({inner - 30, 0});
    auto& content = popover->content();
    content.add(previous.layer()); content.add(yearText); content.add(next.layer());
    for (std::size_t index = 0; index < cells.size(); ++index) {
        cells[index]->layer().setOrigin({double(index % 4) * (cellWidth + gap), 40 + double(index / 4) * (rowHeight + gap)});
        content.add(cells[index]->layer());
    }
    render();
    std::vector<d::Control*> controls{&previous, &next};
    controls.insert(controls.end(), cells.begin(), cells.end());
    auto* raw = popover.get();
    openPopover(std::move(popover), &anchor, std::nullopt, std::move(controls));
    // Centred under the title rather than right-aligned to it.
    const Rect title = anchor.rect(root());
    const auto size = raw->layer().bounds();
    raw->layer().setOrigin({std::min(std::max(0.0, title.x + title.width / 2 - size.width / 2), d::metric::boxWidth - size.width), title.y + title.height + 8});
}
// A month from the picker: today when it is today's month, else day 1; the
// page moves with the month arrows' subnav from the side it lies on.
void CalendarCanvas::pick(CalendarDay first) {
    closePopover();
    if (first.year < 1900 || first.year > 9999) return;
    const CalendarDay target{first.year, first.month, 1};
    const auto today = state_->today();
    const CalendarDay day = today.year == target.year && today.month == target.month ? today : target;
    if (target == month_) { if (day != selected_) showDay(day); return; }
    const bool forward = target > month_;
    month_ = target; selected_ = day;
    stopEditing();
    navigate(forward ? 1 : -1);
}
void CalendarCanvas::showDay(CalendarDay day) {
    if (day.year != month_.year || day.month != month_.month) {
        const bool forward = day > month_;
        month_ = {day.year, day.month, 1};
        selected_ = day; stopEditing(); navigate(forward ? 1 : -1);
        return;
    }
    selected_ = day;
    stopEditing();
    refresh();
}
// Left / right move the selected day, clamped to the month (no modifiers).
bool CalendarCanvas::handleExtraKey(const d::KeyInput& input) {
    if ((input.virtualKey != d::key::left && input.virtualKey != d::key::right) || input.commandModifiers() || input.shift) return false;
    const int target = std::min(int(month_.daysInMonth()), std::max(1, selected_.day + (input.virtualKey == d::key::right ? 1 : -1)));
    if (target != selected_.day) showDay({month_.year, month_.month, target});
    return true;
}

// --- Editing (:413-476) ----------------------------------------------------------------
// A plate opens its event in the fields; pressing it again closes it.
void CalendarCanvas::toggle(const std::string& id) {
    if (editingID_ == id) { stopEditing(); refresh(); return; }
    const auto list = state_->events();
    const auto it = std::find_if(list.begin(), list.end(), [&](const CalendarEvent& e) { return e.id == id; });
    if (it == list.end()) return;
    if (!editingID_) draft_ = {titleField().text(), detailsField().text()};
    editingID_ = id;
    titleField().setText(it->title); detailsField().setText(it->details);
    titleField().setInvalid(false);
    refresh();
}
void CalendarCanvas::stopEditing() {
    if (!editingID_) return;
    editingID_.reset();
    titleField().setText(draft_.first); detailsField().setText(draft_.second);
    titleField().setInvalid(false);
}
// Adds on the selected day; a missing title shakes and focuses the field.
void CalendarCanvas::addEvent() {
    const auto title = options_.text.trimmed ? options_.text.trimmed(titleField().text()) : titleField().text();
    if (title.empty()) { titleField().setInvalid(true, true); titleField().beginEditing(); return; }
    std::weak_ptr<int> life = life_;
    (void)state_->save(title, detailsField().text(), selected_, std::nullopt, [this, life, title](bool success) {
        if (life.expired() || !success) return;
        draft_ = {};
        if (!editingID_) { titleField().setText(""); detailsField().setText(""); }
        toast(d::Icon::plus, "Event added", "事项已添加", title);
    });
}
// Saves the open event; an emptied title keeps the previous one.
void CalendarCanvas::saveEvent() {
    if (!editingID_) return;
    const auto list = state_->events();
    const auto it = std::find_if(list.begin(), list.end(), [&](const CalendarEvent& e) { return e.id == *editingID_; });
    if (it == list.end()) return;
    const auto typed = options_.text.trimmed ? options_.text.trimmed(titleField().text()) : titleField().text();
    const std::string title = typed.empty() ? it->title : typed, id = *editingID_;
    std::weak_ptr<int> life = life_;
    (void)state_->save(title, detailsField().text(), it->day, id, [this, life, id, title](bool success) {
        if (life.expired() || !success) return;
        if (editingID_ == id) stopEditing();
        refresh();
        toast(d::Icon::check, "Event saved", "事项已保存", title);
    });
}
void CalendarCanvas::confirmDelete() {
    if (!editingID_) return;
    const auto list = state_->events();
    const auto it = std::find_if(list.begin(), list.end(), [&](const CalendarEvent& e) { return e.id == *editingID_; });
    if (it == list.end()) return;
    d::ConfirmRequest request;
    request.id = "calendar.delete";
    request.title = text("Delete this event?", "删除此事项？");
    request.message = it->title;
    request.confirmTitle = text("Delete", "删除");
    request.danger = true;
    request.icon = d::Icon::trash;
    std::weak_ptr<int> life = life_;
    confirm(std::move(request), [this, life, id = *editingID_] {
        if (life.expired()) return;
        (void)state_->remove(id, [this, life, id](bool success) {
            if (life.expired() || !success) return;
            if (editingID_ == id) stopEditing();
            refresh();
            toast(d::Icon::trash, "Event deleted", "提醒已取消");
        });
    });
}
// Re-plans the 09:00 reminders (the day before and on the day).
void CalendarCanvas::refreshReminders() {
    state_->refreshReminders();
    switch (state_->permission()) {
    case CalendarPermission::denied: case CalendarPermission::unavailable:
        toast(d::Icon::warn, "Reminders off", "提醒已关闭", calendarReminderStatus(state_->permission(), env().language)); break;
    case CalendarPermission::authorized: case CalendarPermission::unknown:
        toast(d::Icon::refresh, "Reminders scheduled", "提醒已安排", std::string("09:00")); break;
    }
}
} // namespace endfield::modules
