import AppKit
import QuartzCore

/// Calendar (spec §3.17): the month grid on the left, the selected day's
/// events, the title / details fields and Add or Save + Delete on the right.
/// Events, reminders and their persistence belong to HUDCalendarController;
/// this canvas presents and edits them.
final class HUDCalendarCanvas: HUDDesignCanvas {
    let controller: HUDCalendarController
    private(set) var selectedDay: HUDCalendarDay
    /// Day 1 of the shown month.
    private(set) var month: HUDCalendarDay
    /// The event whose plate is open in the fields.
    private(set) var editingID: UUID?
    /// What was typed for a new event while another one is open.
    private var draft = (title: "", details: "")
    private var presented = false, controllerActive = false
    private var lastError: String?
    private struct Structure: Equatable {
        let month: HUDCalendarDay, selected: HUDCalendarDay, today: HUDCalendarDay, editing: UUID?
        let events: [HUDCalendarEvent], language: AppLanguage
    }
    private var renderedStructure: Structure?
    private var binders: [() -> Void] = []
    private lazy var titleField = makeField(id: "calendar.title", label: L10n.text("Title", "标题"), limit: 120)
    private lazy var detailsField = makeField(id: "calendar.details", label: L10n.text("Details", "详情"), limit: 2000)

    var eventsForSelectedDay: [HUDCalendarEvent] { events(on: selectedDay) }

    init(context: HUDDesignContext, controller: HUDCalendarController) {
        self.controller = controller
        selectedDay = controller.today
        month = HUDCalendarDay(year: selectedDay.year, month: selectedDay.month, day: 1)
        super.init(module: .calendar, context: context)
        controller.onChange = { [weak self] in self?.controllerChanged() }
    }

    // MARK: Host interface

    /// The calendar loads its store and observes clock changes while shown.
    func setPresented(_ value: Bool) { presented = value; syncController() }

    override func didActivate() { syncController(); lastError = controller.error; refresh() }
    override func didDeactivate() { syncController() }
    override func recordingModeChanged() {}

    private func syncController() {
        let wanted = presented || isActive
        guard wanted != controllerActive else { return }
        controllerActive = wanted
        controller.setActive(wanted)
    }

    /// Actions by id, for accessibility stand-ins, tests and diagnostics:
    /// today, previousMonth, nextMonth, monthPicker, reminders, new, add,
    /// save, delete, day:YYYY-MM-DD, month:YYYY-MM and event:UUID.
    func perform(_ id: String) {
        switch id {
        case "today": goToday()
        case "monthPicker": if let title = page.controls.first(where: { $0.id == "calendar.monthPicker" }) { openMonthPicker(title) }
        case "previousMonth": changeMonth(-1)
        case "nextMonth": changeMonth(1)
        case "reminders": refreshReminders()
        case "new": stopEditing(); refresh(); titleField.beginEditing()
        case "add": addEvent()
        case "save": saveEvent()
        case "delete": confirmDelete()
        default:
            if id.hasPrefix("day:"), let day = HUDCalendarDay.parse(String(id.dropFirst(4))) { show(day) }
            else if id.hasPrefix("month:"), let day = HUDCalendarDay.parse(String(id.dropFirst(6)) + "-01") { pick(day) }
            else if id.hasPrefix("event:"), let event = UUID(uuidString: String(id.dropFirst(6))) { toggle(event) }
        }
    }

    // MARK: State

    private var structure: Structure {
        Structure(month: month, selected: selectedDay, today: controller.today, editing: editingID,
                  events: controller.events, language: L10n.resolvedLanguage)
    }

    private func events(on day: HUDCalendarDay) -> [HUDCalendarEvent] {
        controller.events.filter { $0.day == day }
            .sorted { $0.created == $1.created ? $0.id.uuidString < $1.id.uuidString : $0.created < $1.created }
    }

    private func controllerChanged() {
        if let error = controller.error, error != lastError, isActive { toast(icon: .warn, "Calendar", "日历", detail: error) }
        lastError = controller.error
        if let id = editingID, !controller.events.contains(where: { $0.id == id }) { stopEditing() }
        refresh()
    }

    /// Structure changes re-render in place; busy flips update the buttons.
    private func refresh() {
        guard renderedLanguage != nil, !isTransitioning else { return }
        if structure != renderedStructure {
            rerender()
            if isEditingText { refreshTextFieldProjection() }
        } else { binders.forEach { $0() } }
    }

    private var zone: TimeZone { controller.zone }
    private var calendar: Calendar { HUDCalendarDay.calendar(controller.zone) }

    private static var locale: Locale {
        switch L10n.resolvedLanguage {
        case .simplifiedChinese: return Locale(identifier: "zh_CN")
        case .traditionalChinese: return Locale(identifier: "zh_TW")
        case .japanese: return Locale(identifier: "ja_JP")
        case .korean: return Locale(identifier: "ko_KR")
        case .english, .system: return Locale(identifier: "en_US")
        }
    }

    private func formatter(_ template: String) -> DateFormatter {
        let formatter = DateFormatter()
        formatter.calendar = calendar; formatter.timeZone = zone; formatter.locale = Self.locale
        formatter.setLocalizedDateFormatFromTemplate(template)
        return formatter
    }

    private func daysInMonth(_ day: HUDCalendarDay) -> Int {
        guard let date = day.date(in: zone) else { return 28 }
        return calendar.range(of: .day, in: .month, for: date)?.count ?? 28
    }

    /// The 42 cells: trailing days of the previous month, the month, then the next.
    private func cells() -> [(day: HUDCalendarDay, outside: Bool)] {
        guard let first = month.date(in: zone) else { return [] }
        let padding = (calendar.component(.weekday, from: first) - calendar.firstWeekday + 7) % 7
        return (0..<42).compactMap { index in
            guard let date = calendar.date(byAdding: .day, value: index - padding, to: first) else { return nil }
            let day = HUDCalendarDay(date, zone: zone)
            return (day, day.month != month.month || day.year != month.year)
        }
    }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        renderedStructure = structure
        binders = []
        let today = HUDButton(id: "calendar.today", title: L10n.text("Today", "今天"), kind: .secondary, size: .small, env: env)
        today.place(at: .zero)
        today.onActivate = { [weak self] in self?.goToday() }
        let reminders = HUDIconButton(id: "calendar.reminders", label: L10n.text("Refresh reminders", "刷新提醒"), icon: .refresh, env: env)
        reminders.onActivate = { [weak self] in self?.refreshReminders() }
        binders.append { [weak self, weak reminders] in reminders?.isEnabled = self?.controller.busy == false }
        let count = HUDFormat.pad2(controller.events.count)
        header.configure(crumbs: ["CALENDAR"], code: "MOD-12", title: HUDModuleTitle("Calendar", "日历"),
                         counter: L10n.text("\(count) EVENTS", "\(count) 个事项"), showsBack: false, actions: [today, reminders])
        buildMonth(page)
        buildEvents(page)
    }

    private func buildMonth(_ page: HUDDesignPage) {
        let column = HUDLayer.plain("calendar.month")
        let previous = HUDIconButton(id: "calendar.previousMonth", label: L10n.text("Previous month", "上个月"), icon: .left, env: env)
        let next = HUDIconButton(id: "calendar.nextMonth", label: L10n.text("Next month", "下个月"), icon: .right, env: env)
        previous.isEnabled = month.year > 1900 || month.month > 1
        next.isEnabled = month.year < 9999 || month.month < 12
        previous.onActivate = { [weak self] in self?.changeMonth(-1) }
        next.onActivate = { [weak self] in self?.changeMonth(1) }
        let title = CalendarMonthTitle(id: "calendar.monthPicker", text: month.date(in: zone).map(formatter("yMMMM").string) ?? month.string,
                                       env: env)
        title.onActivate = { [weak self, weak title] in
            guard let self, let title else { return }
            if self.isPopoverOpen { self.closePopover() } else { self.openMonthPicker(title) }
        }
        let cellWidth: CGFloat = (322 - 6 * 4) / 7
        let weekdayStyle = HUDTextStyle(size: 9.5, weight: .regular, color: HUDColor.steel, tracking: 0.1)
        let symbols = formatter("EEE").shortStandaloneWeekdaySymbols ?? []
        let occupied = Set(controller.events.map(\.day))
        let names = formatter("yMMMMd")
        var dayControls: [HUDControl] = []
        HUDMotion.perform {
            column.frame = CGRect(x: 0, y: HUDMetric.headerHeight, width: 322, height: 314)
            previous.layer.frame.origin = .zero
            next.layer.frame.origin = CGPoint(x: 292, y: 0)
            // Centred between the arrows, sized to the month name.
            title.layer.position = CGPoint(x: 161, y: 15)
            [previous.layer, title.layer, next.layer].forEach(column.addSublayer)
            if symbols.count == 7 {
                for index in 0..<7 {
                    let symbol = symbols[(index + calendar.firstWeekday - 1) % 7].uppercased(with: Locale(identifier: "en_US_POSIX"))
                    let label = HUDTextLayer(symbol, style: weekdayStyle, alignment: .center)
                    label.lineFrame = CGRect(x: CGFloat(index) * (cellWidth + 4), y: 39.5, width: cellWidth, height: 9.5)
                    column.addSublayer(label)
                }
            }
            for (index, cell) in cells().enumerated() {
                let frame = CGRect(x: CGFloat(index % 7) * (cellWidth + 4), y: 54 + CGFloat(index / 7) * 44, width: cellWidth, height: 40)
                let weekday = cell.day.date(in: zone).map { calendar.component(.weekday, from: $0) } ?? 2
                let state = CalendarDayCell.State(outside: cell.outside, weekend: weekday == 1 || weekday == 7,
                                                  event: occupied.contains(cell.day), today: cell.day == controller.today,
                                                  selected: cell.day == selectedDay)
                let label = cell.day.date(in: zone).map(names.string) ?? cell.day.string
                let control = CalendarDayCell(id: "calendar.day:" + cell.day.string, label: label, text: String(cell.day.day), state: state,
                                              size: frame.size, env: env)
                control.layer.frame = frame
                column.addSublayer(control.layer)
                if !cell.outside {
                    let day = cell.day
                    control.onActivate = { [weak self] in self?.show(day) }
                    dayControls.append(control)
                }
            }
            page.layer.addSublayer(column)
        }
        page.add([previous, title, next] + dayControls)
        page.stagger(column)
    }

    private func buildEvents(_ page: HUDDesignPage) {
        let column = HUDLayer.plain("calendar.events")
        let width: CGFloat = 254
        let number = HUDTextLayer(HUDFormat.pad2(selectedDay.day), style: HUDType.numeral.sized(54))
        let weekday = HUDTextLayer((selectedDay.date(in: zone).map(formatter("EEEE").string) ?? "")
                                    .uppercased(with: Locale(identifier: "en_US_POSIX")), style: HUDType.label)
        let numberWidth = ceil(number.preferredWidth)
        HUDMotion.perform {
            column.frame = CGRect(x: 336, y: HUDMetric.headerHeight, width: width, height: 314)
            number.lineFrame = CGRect(x: 0, y: 0, width: numberWidth + 1, height: 54 * 0.9)
            weekday.lineFrame = CGRect(x: numberWidth + 10, y: 49 - 6 - 10, width: width - numberWidth - 10, height: 10)
            column.addSublayer(number); column.addSublayer(weekday)
            page.layer.addSublayer(column)
        }
        // The day's plates: a scroll area 40–150 px tall.
        let list = events(on: selectedDay)
        var plates: [CalendarEventPlate] = []
        var y: CGFloat = 0
        for event in list {
            let plate = CalendarEventPlate(id: "calendar.event:" + event.id.uuidString, event: event, selected: event.id == editingID,
                                           width: width - 6, env: env)
            plate.onActivate = { [weak self] in self?.toggle(event.id) }
            HUDMotion.perform { plate.layer.frame.origin = CGPoint(x: 0, y: y) }
            y += plate.layer.bounds.height + 6
            plates.append(plate)
        }
        let contentHeight = max(0, y - 6)
        let listHeight = min(150, max(40, contentHeight))
        let area = page.scrollArea(id: "calendar.events:" + selectedDay.string, frame: CGRect(x: 0, y: 55, width: width, height: listHeight), in: column)
        HUDMotion.perform {
            plates.forEach { area.content.addSublayer($0.layer) }
            if list.isEmpty {
                let empty = HUDTextLayer(L10n.text("NO EVENTS", "暂无事项"), style: HUDType.label)
                empty.lineFrame = CGRect(x: 0, y: 10, width: width, height: 10)
                area.content.addSublayer(empty)
            }
        }
        area.setContentHeight(contentHeight)
        page.restoreOffset(of: area)
        page.add(plates)
        // Title, details and the buttons follow the list.
        let editing = editingID.flatMap { id in controller.events.first { $0.id == id } }
        let fieldTop = 55 + listHeight + 6
        titleField.placeholder = editing == nil ? L10n.text("New event", "新事项") : L10n.text("Title", "标题")
        detailsField.placeholder = L10n.text("Details", "详情")
        for (index, field) in [titleField, detailsField].enumerated() {
            register(field)
            HUDMotion.perform { field.layer.frame.origin = CGPoint(x: 0, y: fieldTop + CGFloat(index) * 38); column.addSublayer(field.layer) }
        }
        let buttonTop = fieldTop + 76
        var buttons: [HUDButton] = []
        if editing != nil {
            let delete = HUDButton(id: "calendar.delete", title: L10n.text("Delete", "删除"), icon: .trash, kind: .secondary, size: .small, env: env)
            delete.place(at: .zero)
            let save = HUDButton(id: "calendar.save", title: L10n.text("Save", "保存"), icon: .check, kind: .primary, size: .small, env: env)
            save.place(at: CGPoint(x: 0, y: buttonTop), width: width - 6 - delete.layer.bounds.width)
            HUDMotion.perform { delete.layer.frame.origin = CGPoint(x: width - delete.layer.bounds.width, y: buttonTop) }
            save.onActivate = { [weak self] in self?.saveEvent() }
            delete.onActivate = { [weak self] in self?.confirmDelete() }
            buttons = [save, delete]
        } else {
            let add = HUDButton(id: "calendar.add", title: L10n.text("Add event", "添加事项"), icon: .plus, kind: .primary, size: .small, env: env)
            add.place(at: CGPoint(x: 0, y: buttonTop), width: width)
            add.onActivate = { [weak self] in self?.addEvent() }
            buttons = [add]
        }
        HUDMotion.perform { buttons.forEach { column.addSublayer($0.layer) } }
        page.add(buttons)
        binders.append { [weak self] in
            guard let self else { return }
            buttons.forEach { $0.isEnabled = !self.controller.busy }
        }
        binders.forEach { $0() }
        page.stagger(column)
    }

    private func makeField(id: String, label: String, limit: Int) -> HUDDesignTextField {
        var options = HUDDesignTextField.Options()
        options.maximumLength = limit
        return HUDDesignTextField(id: id, label: label, text: "", size: CGSize(width: 254, height: 32), options: options, env: env)
    }

    // MARK: Navigation

    /// ← / →: the month changes, day 1 is selected, editing closes (subnav ∓1).
    private func changeMonth(_ step: Int) {
        guard let date = month.date(in: zone), let target = calendar.date(byAdding: .month, value: step, to: date) else { return }
        let next = HUDCalendarDay(target, zone: zone)
        guard (1900...9999).contains(next.year) else { return }
        month = HUDCalendarDay(year: next.year, month: next.month, day: 1)
        selectedDay = month
        stopEditing()
        navigate(dx: CGFloat(step))
    }

    /// Today enters from the side its month lies on, or re-renders in place.
    private func goToday() {
        let today = controller.today
        let target = HUDCalendarDay(year: today.year, month: today.month, day: 1)
        selectedDay = today
        stopEditing()
        if target == month { refresh(); return }
        let forward = target > month
        month = target
        navigate(dx: forward ? 1 : -1)
    }

    /// The title's pop-over: a year stepper over a 4 × 3 month grid. Today's
    /// month is the accent cell and the shown month carries the selection
    /// ring, as in the day grid; stepping the year updates the cells in place.
    private func openMonthPicker(_ anchor: HUDControl) {
        let lines = HUDModuleTitle("SELECT MONTH", "选择月份").lines()
        let width: CGFloat = 270, inner = width - 24, gap: CGFloat = 4, rowHeight: CGFloat = 32
        let popover = HUDPopover(title: lines.title, subtitle: lines.subtitle.isEmpty ? nil : lines.subtitle, width: width,
                                 contentHeight: 30 + 10 + 3 * rowHeight + 2 * gap, env: env)
        var year = month.year
        let previous = HUDIconButton(id: "calendar.previousYear", label: L10n.text("Previous year", "上一年"), icon: .left, env: env)
        let next = HUDIconButton(id: "calendar.nextYear", label: L10n.text("Next year", "下一年"), icon: .right, env: env)
        let yearText = HUDTextLayer(style: HUDTextStyle(size: 17, weight: .medium, color: HUDColor.textOnDark, tracking: 0.04, tabular: true),
                                    alignment: .center)
        let symbols = formatter("MMM").shortStandaloneMonthSymbols ?? []
        let names = formatter("yMMMM")
        let cellWidth = (inner - 3 * gap) / 4
        let cells = (1...12).map { number -> CalendarDayCell in
            let symbol = symbols.count == 12 ? symbols[number - 1].uppercased(with: Locale(identifier: "en_US_POSIX")) : String(number)
            let cell = CalendarDayCell(id: "calendar.month:" + String(format: "%02d", number), label: symbol, text: symbol,
                                       state: .init(), size: CGSize(width: cellWidth, height: rowHeight), fontSize: 13, env: env)
            cell.onActivate = { [weak self] in self?.pick(HUDCalendarDay(year: year, month: number, day: 1)) }
            return cell
        }
        let render = { [weak self, weak previous, weak next, weak yearText] in
            guard let self else { return }
            let today = self.controller.today
            HUDMotion.perform { yearText?.text = String(year) }
            previous?.isEnabled = year > 1900; next?.isEnabled = year < 9999
            for (index, cell) in cells.enumerated() {
                let number = index + 1, first = HUDCalendarDay(year: year, month: number, day: 1)
                cell.state = .init(today: today.year == year && today.month == number,
                                   selected: self.month.year == year && self.month.month == number)
                cell.label = first.date(in: self.zone).map(names.string) ?? first.string
            }
        }
        previous.onActivate = { year = max(1900, year - 1); render() }
        next.onActivate = { year = min(9999, year + 1); render() }
        HUDMotion.perform {
            previous.layer.frame.origin = .zero
            next.layer.frame.origin = CGPoint(x: inner - 30, y: 0)
            yearText.lineFrame = CGRect(x: 30, y: 15 - 8.5, width: inner - 60, height: 17)
            for (index, cell) in cells.enumerated() {
                cell.layer.frame.origin = CGPoint(x: CGFloat(index % 4) * (cellWidth + gap), y: 40 + CGFloat(index / 4) * (rowHeight + gap))
            }
            ([previous.layer, yearText, next.layer] + cells.map(\.layer)).forEach(popover.content.addSublayer)
        }
        render()
        openPopover(popover, anchor: anchor, controls: [previous, next] + cells)
        // Centred under the title rather than right-aligned to it.
        let title = anchor.rect(in: root), size = popover.layer.bounds.size
        HUDMotion.perform {
            popover.layer.frame.origin = CGPoint(x: min(max(0, title.midX - size.width / 2), HUDMetric.box.width - size.width), y: title.maxY + 8)
        }
    }

    /// A month from the picker: today when it is today's month, else day 1;
    /// the page moves with the month arrows' subnav from the side it lies on.
    private func pick(_ first: HUDCalendarDay) {
        closePopover()
        guard (1900...9999).contains(first.year) else { return }
        let target = HUDCalendarDay(year: first.year, month: first.month, day: 1), today = controller.today
        let day = today.year == target.year && today.month == target.month ? today : target
        guard target != month else { if day != selectedDay { show(day) }; return }
        let forward = target > month
        month = target; selectedDay = day
        stopEditing()
        navigate(dx: forward ? 1 : -1)
    }

    private func show(_ day: HUDCalendarDay) {
        if day.year != month.year || day.month != month.month {
            let forward = day > month
            month = HUDCalendarDay(year: day.year, month: day.month, day: 1)
            selectedDay = day; stopEditing(); navigate(dx: forward ? 1 : -1)
            return
        }
        selectedDay = day
        stopEditing()
        refresh()
    }

    /// ← / → move the selected day, clamped to the month.
    override func handleExtraKey(_ event: NSEvent) -> Bool {
        guard [123, 124].contains(event.keyCode), event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        let target = min(daysInMonth(month), max(1, selectedDay.day + (event.keyCode == 124 ? 1 : -1)))
        if target != selectedDay.day { show(HUDCalendarDay(year: month.year, month: month.month, day: target)) }
        return true
    }

    // MARK: Editing

    /// A plate opens its event in the fields; pressing it again closes it.
    private func toggle(_ id: UUID) {
        if editingID == id { stopEditing(); refresh(); return }
        guard let event = controller.events.first(where: { $0.id == id }) else { return }
        if editingID == nil { draft = (titleField.text, detailsField.text) }
        editingID = id
        titleField.setText(event.title); detailsField.setText(event.details)
        titleField.setInvalid(false)
        refresh()
    }

    private func stopEditing() {
        guard editingID != nil else { return }
        editingID = nil
        titleField.setText(draft.title); detailsField.setText(draft.details)
        titleField.setInvalid(false)
    }

    /// Adds on the selected day; a missing title shakes and focuses the field.
    private func addEvent() {
        let title = titleField.text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !title.isEmpty else { titleField.setInvalid(true, shake: true); titleField.beginEditing(); return }
        controller.save(title: title, details: detailsField.text, day: selectedDay) { [weak self] success in
            guard let self, success else { return }
            self.draft = ("", "")
            if self.editingID == nil { self.titleField.setText(""); self.detailsField.setText("") }
            self.toast(icon: .plus, "Event added", "事项已添加", detail: title)
        }
    }

    /// Saves the open event; an emptied title keeps the previous one.
    private func saveEvent() {
        guard let id = editingID, let event = controller.events.first(where: { $0.id == id }) else { return }
        let typed = titleField.text.trimmingCharacters(in: .whitespacesAndNewlines)
        let title = typed.isEmpty ? event.title : typed
        controller.save(title: title, details: detailsField.text, day: event.day, id: id) { [weak self] success in
            guard let self, success else { return }
            if self.editingID == id { self.stopEditing() }
            self.refresh()
            self.toast(icon: .check, "Event saved", "事项已保存", detail: title)
        }
    }

    private func confirmDelete() {
        guard let id = editingID, let event = controller.events.first(where: { $0.id == id }) else { return }
        confirm(HUDConfirmRequest(id: "calendar.delete", title: L10n.text("Delete this event?", "删除此事项？"), message: event.title,
                                  confirmTitle: L10n.text("Delete", "删除"), danger: true, icon: .trash)) { [weak self] in
            self?.controller.delete(id) { [weak self] success in
                guard let self, success else { return }
                if self.editingID == id { self.stopEditing() }
                self.refresh()
                self.toast(icon: .trash, "Event deleted", "提醒已取消")
            }
        }
    }

    /// Re-plans the 09:00 reminders (the day before and on the day).
    private func refreshReminders() {
        controller.refreshReminders()
        switch controller.permission {
        case .denied, .unavailable: toast(icon: .warn, "Reminders off", "提醒已关闭", detail: controller.reminderStatus)
        case .authorized, .unknown: toast(icon: .refresh, "Reminders scheduled", "提醒已安排", detail: "09:00")
        }
    }

    // MARK: Testing

    var titleFieldForTesting: HUDDesignTextField { titleField }
    var detailsFieldForTesting: HUDDesignTextField { detailsField }
}

/// One day of the 42-cell grid (spec §3.17), also a month of the picker:
/// outside-month, weekend, event diamond, today (accent) and selected (inset
/// ring) states, restyled in place; hover lifts 1 px.
private final class CalendarDayCell: HUDControl {
    struct State: Equatable { var outside = false, weekend = false, event = false, today = false, selected = false }
    var state: State { didSet { if state != oldValue { applyState() } } }
    private let brighten = HUDLayer.plain("calendar.day.brighten")
    private let text: HUDTextLayer
    private let fontSize: CGFloat
    private var diamond: CALayer?

    init(id: String, label: String, text value: String, state: State, size: CGSize, fontSize: CGFloat = 14, env: HUDDesignEnvironment) {
        self.state = state; self.fontSize = fontSize
        text = HUDTextLayer(value, style: HUDTextStyle(size: fontSize, weight: .regular, color: HUDColor.textOnDark, tabular: true),
                            alignment: .center)
        super.init(id: id, label: label, env: env)
        let bounds = CGRect(origin: .zero, size: size)
        HUDMotion.perform {
            layer.bounds = bounds
            brighten.frame = bounds; brighten.backgroundColor = HUDColor.foreground(0.06); brighten.opacity = 0
            text.lineFrame = CGRect(x: 0, y: (size.height - fontSize) / 2, width: size.width, height: fontSize)
            layer.addSublayer(brighten); layer.addSublayer(text)
            if state.event && !state.outside {
                let mark = HUDBadge.diamond(size: 5)
                mark.position = CGPoint(x: size.width / 2, y: size.height - 5 - 2.5)
                layer.addSublayer(mark); diamond = mark
            }
        }
        applyState()
        if !state.outside { setTarget(bounds) }
    }

    private func applyState() {
        let today = state.today && !state.outside, selected = state.selected && !state.outside
        let color = state.outside ? HUDColor.tone(0x555555) : today ? HUDColor.nearBlack : state.weekend ? HUDColor.tone(0x9A9A9A) : HUDColor.tone(0xDDDDDD)
        HUDMotion.perform {
            text.textStyle = HUDTextStyle(size: fontSize, weight: today ? .bold : .regular, color: color, tabular: true)
            layer.backgroundColor = state.outside ? HUDColor.tone(0x181818) : today ? HUDColor.yellow : HUDColor.tone(0x1F1F1F)
            layer.borderWidth = selected ? 2 : (today ? 0 : 1)
            layer.borderColor = selected ? (today ? HUDColor.nearBlack : HUDColor.tone(0xEDEDED)) : HUDColor.separator
        }
    }

    override func hoverChanged() {
        guard !state.outside else { return }
        let on = isHovered && isEnabled
        HUDMotion.animate(layer, "transform.translation.y", to: on ? -1 : 0, duration: HUDDuration.hover, env: env)
        HUDMotion.animate(brighten, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
    }
    override var accessibilityRole: HUDAccessibilityItem.Role? { state.outside ? nil : .tab(selected: state.selected) }
}

/// The month title over the grid: it opens the month picker. Its text and a
/// small caret centre on its position; hover lifts it 1 px under a faint plate.
private final class CalendarMonthTitle: HUDControl {
    private let brighten = HUDLayer.plain("calendar.title.brighten")
    private let caret = HUDLayer.shape("calendar.title.caret")

    init(id: String, text value: String, env: HUDDesignEnvironment) {
        super.init(id: id, label: value, env: env)
        let text = HUDTextLayer(value, style: HUDTextStyle(size: 17, weight: .medium, color: HUDColor.textOnDark, tracking: 0.04))
        let textWidth = min(ceil(text.preferredWidth), 200)
        let bounds = CGRect(x: 0, y: 0, width: 10 + textWidth + 8 + 8 + 10, height: 28)
        HUDMotion.perform {
            layer.bounds = bounds
            brighten.frame = bounds; brighten.backgroundColor = HUDColor.foreground(0.06); brighten.opacity = 0
            text.lineFrame = CGRect(x: 10, y: 14 - 8.5, width: textWidth + 1, height: 17)
            let path = CGMutablePath()
            path.addLines(between: [CGPoint(x: 0, y: 0), CGPoint(x: 8, y: 0), CGPoint(x: 4, y: 4.5)]); path.closeSubpath()
            caret.frame = CGRect(x: 10 + textWidth + 8, y: 12, width: 8, height: 4.5)
            caret.path = path; caret.fillColor = HUDColor.steel
            [brighten, text, caret].forEach(layer.addSublayer)
        }
        setTarget(bounds)
    }

    override func hoverChanged() {
        applyHoverMotion(lift: 1, pressScale: 0.97, brighten: brighten)
        HUDMotion.animate(caret, "fillColor", to: isHovered && isEnabled ? HUDColor.textOnDark : HUDColor.steel,
                          duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
    }
    override func pressChanged() { applyHoverMotion(lift: 1, pressScale: 0.97, brighten: brighten) }
    override var accessibilityRole: HUDAccessibilityItem.Role? { .popUp(label) }
}

/// An event plate (spec §3.17): light chamfered plate with the orange bar,
/// title and detail; the open event is dark.
private final class CalendarEventPlate: HUDControl {
    static let titleStyle = HUDTextStyle(size: 13.5, weight: .medium, color: HUDColor.nearBlack, lineHeight: 1.3)
    static let detailStyle = HUDTextStyle(size: 11, weight: .regular, color: HUDColor.rgb(0x555555), lineHeight: 1.3)

    init(id: String, event: HUDCalendarEvent, selected: Bool, width: CGFloat, env: HUDDesignEnvironment) {
        super.init(id: id, label: event.details.isEmpty ? event.title : event.title + ", " + event.details, env: env)
        let textWidth = width - 26
        let details = event.details.components(separatedBy: .newlines).joined(separator: " ")
        let titleLines = Self.lineCount(event.title, Self.titleStyle, width: textWidth)
        let detailLines = details.isEmpty ? 0 : Self.lineCount(details, Self.detailStyle, width: textWidth)
        let titleHeight = CGFloat(titleLines) * 13.5 * 1.3, detailHeight = CGFloat(detailLines) * 11 * 1.3
        let bounds = CGRect(x: 0, y: 0, width: width, height: ceil(16 + titleHeight + detailHeight))
        let plate = HUDLayer.plain("calendar.event.plate"), bar = HUDLayer.plain("calendar.event.bar")
        let title = HUDTextLayer(event.title, style: Self.titleStyle.colored(selected ? HUDColor.rgb(0xF2F2F2) : HUDColor.nearBlack))
        let detail = HUDTextLayer(details, style: Self.detailStyle.colored(selected ? HUDColor.rgb(0xAAAAAA) : HUDColor.rgb(0x555555)))
        title.maximumLines = 2; detail.maximumLines = 2
        HUDMotion.perform {
            layer.bounds = bounds
            plate.frame = bounds
            let colors = selected ? [HUDColor.charcoal, HUDColor.charcoal] : [HUDColor.rgb(0xE2E3DE), HUDColor.rgb(0xCFD0CB)]
            plate.showArtwork(HUDArtwork.plate(colors, size: bounds.size, cut: 9, scale: env.renderScale), scale: env.renderScale)
            bar.frame = CGRect(x: 0, y: 8, width: 3, height: bounds.height - 16); bar.backgroundColor = HUDColor.orange
            title.lineFrame = CGRect(x: 16, y: 8, width: textWidth, height: titleHeight)
            detail.lineFrame = CGRect(x: 16, y: 8 + titleHeight, width: textWidth, height: detailHeight)
            detail.isHidden = details.isEmpty
            [plate, bar, title, detail].forEach(layer.addSublayer)
        }
        setTarget(bounds, path: HUDShape.chamfer(bounds, cut: 9))
    }

    private static func lineCount(_ text: String, _ style: HUDTextStyle, width: CGFloat) -> Int {
        let string = HUDText.attributed([HUDText.Run(text, style)], language: HUDTextLanguage.tag())
        return max(1, HUDText.lines(string, width: width, maximumLines: 2, truncate: true).count)
    }
}
