import AppKit
import QuartzCore

final class HUDCalendarCanvas: NSObject, HUDModuleContentFactory {
    struct Action { let id: String, label: String; let rect: CGRect; var enabled = true }
    let layer = CALayer()
    let controller: HUDCalendarController
    private let face = CALayer()
    private(set) var actions: [Action] = []
    private(set) var dark = true
    private(set) var selectedDay: HUDCalendarDay
    private(set) var month: HUDCalendarDay
    private var visible = false, scale: CGFloat = 2, firstEvent = 0, scrollRemainder: CGFloat = 0
    private var artworkPrepared = false
    var onChange: (() -> Void)?, onAction: ((String) -> Void)?
    var eventsForSelectedDay: [HUDCalendarEvent] {
        controller.events.filter { $0.day == selectedDay }.sorted { $0.created == $1.created ? $0.id.uuidString < $1.id.uuidString : $0.created < $1.created }
    }
    init(controller: HUDCalendarController) {
        self.controller = controller; selectedDay = controller.today
        month = HUDCalendarDay(year: selectedDay.year, month: selectedDay.month, day: 1)
        super.init(); layer.bounds = CGRect(x: 0, y: 0, width: 400, height: 440); layer.name = "calendar"
        face.frame = layer.bounds; layer.addSublayer(face)
        controller.onChange = { [weak self] in self?.render() }
    }
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        artworkPrepared = true; dark = style.dark; scale = style.contentsScale; render(); return layer
    }
    func updateRenderScale(_ value: CGFloat) {
        guard value.isFinite else { return }
        let next = min(3, max(1, value)); guard scale != next else { return }
        scale = next; render()
    }
    func setVisible(_ value: Bool) { visible = value; if value { artworkPrepared = true }; controller.setActive(value); if value { render() } }
    func perform(_ id: String) {
        if id == "previousMonth" || id == "nextMonth" {
            guard let date = month.date(in: controller.zone), let target = HUDCalendarDay.calendar(controller.zone).date(byAdding: .month, value: id == "nextMonth" ? 1 : -1, to: date) else { return }
            let next = HUDCalendarDay(target, zone: controller.zone); guard next.isValid else { return }; month = next; render(animated: true)
        } else if id == "today" {
            selectedDay = controller.today; month = HUDCalendarDay(year: selectedDay.year, month: selectedDay.month, day: 1); firstEvent = 0; render(animated: true)
        } else if id.hasPrefix("day:"), let day = HUDCalendarDay.parse(String(id.dropFirst(4))) {
            selectedDay = day; firstEvent = 0; render(animated: true)
        } else if id == "reminders" { controller.refreshReminders() }
        else { onAction?(id) }
    }
    func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard layer.bounds.contains(point), delta.isFinite else { return false }
        if point.y >= 308 {
            scrollRemainder += min(256, max(-256, delta))
            let rows = Int(scrollRemainder / 32)
            if rows != 0 {
                scrollRemainder -= CGFloat(rows) * 32
                let next = min(max(0, eventsForSelectedDay.count - 3), max(0, firstEvent + rows))
                if next != firstEvent { firstEvent = next; render(animated: true) }
            }
        }
        return true
    }
    var monthCells: [(HUDCalendarDay, CGRect)] {
        let cal = HUDCalendarDay.calendar(controller.zone)
        guard let first = month.date(in: controller.zone), let range = cal.range(of: .day, in: .month, for: first) else { return [] }
        let padding = (cal.component(.weekday, from: first) - cal.firstWeekday + 7) % 7
        return range.map { day in
            let position = day - 1 + padding
            return (HUDCalendarDay(year: month.year, month: month.month, day: day), CGRect(x: 12 + (position % 7) * 54, y: 100 + (position / 7) * 30, width: 50, height: 26))
        }
    }
    func render(animated: Bool = false) {
        guard artworkPrepared else { return }
        if !visible, face.sublayers?.isEmpty == false { onChange?(); return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        face.sublayers?.forEach { $0.removeFromSuperlayer() }; actions = []
        let ink = NSColor(white: dark ? 0.95 : 0.10, alpha: 1), accent = HUDRuntimeAppearance.accent
        func text(_ string: String, _ rect: CGRect, _ size: CGFloat = 11, color: NSColor? = nil, centered: Bool = false) {
            let label = CATextLayer(); label.frame = rect; label.string = string; label.font = NSFont.systemFont(ofSize: size, weight: .semibold)
            label.fontSize = size; label.foregroundColor = (color ?? ink).cgColor; label.contentsScale = scale
            label.truncationMode = .end; label.alignmentMode = centered ? .center : .left; face.addSublayer(label)
        }
        func button(_ id: String, _ label: String, _ rect: CGRect, selected: Bool = false, enabled: Bool = true, centered: Bool = true, opaque: Bool = false, light: Bool = false) {
            actions.append(Action(id: id, label: label, rect: rect, enabled: enabled))
            let plate = CAShapeLayer(); plate.name = "calendar.button." + id; plate.frame = rect
            let path = CGMutablePath(), r = plate.bounds, corner: CGFloat = 4
            path.move(to: CGPoint(x: r.minX + corner, y: r.minY)); path.addLine(to: CGPoint(x: r.maxX, y: r.minY))
            path.addLine(to: CGPoint(x: r.maxX, y: r.maxY - corner)); path.addLine(to: CGPoint(x: r.maxX - corner, y: r.maxY))
            path.addLine(to: CGPoint(x: r.minX, y: r.maxY)); path.addLine(to: CGPoint(x: r.minX, y: r.minY + corner)); path.closeSubpath()
            plate.path = path
            plate.fillColor = (light ? NSColor(white: dark ? 0.82 : 0.9, alpha: 1)
                : opaque ? accent.withAlphaComponent(1)
                : selected ? accent.withAlphaComponent(0.23) : ink.withAlphaComponent(0.07)).cgColor
            face.addSublayer(plate)
            text(label, CGRect(x: rect.minX + (centered ? 3 : 8), y: rect.midY - 7, width: rect.width - (centered ? 6 : 16), height: 15), color: (light || opaque ? NSColor(white: 0.13, alpha: 1) : ink).withAlphaComponent(enabled ? 1 : 0.35), centered: centered)
            HUDControlHighlightLayer.add(to: face, rect: rect, shape: .cutCorner, enabled: enabled, framed: true)
        }
        text("// " + L10n.text("Calendar", "日历"), CGRect(x: 12, y: 9, width: 226, height: 24), 17)
        button("today", L10n.text("Today", "今天"), CGRect(x: 305, y: 8, width: 80, height: 27), opaque: true)
        let rule = CALayer(); rule.frame = CGRect(x: 12, y: 41, width: 376, height: 1); rule.backgroundColor = ink.withAlphaComponent(0.2).cgColor; face.addSublayer(rule)
        let formatter = DateFormatter(); formatter.calendar = HUDCalendarDay.calendar(controller.zone); formatter.timeZone = controller.zone
        switch L10n.resolvedLanguage {
        case .simplifiedChinese: formatter.locale = Locale(identifier: "zh_CN")
        case .traditionalChinese: formatter.locale = Locale(identifier: "zh_TW")
        case .japanese: formatter.locale = Locale(identifier: "ja_JP")
        case .korean: formatter.locale = Locale(identifier: "ko_KR")
        default: formatter.locale = Locale(identifier: "en_US")
        }
        formatter.setLocalizedDateFormatFromTemplate("yMMMM")
        text(month.date(in: controller.zone).map(formatter.string) ?? month.string, CGRect(x: 53, y: 54, width: 294, height: 22), 15, centered: true)
        button("previousMonth", "‹", CGRect(x: 12, y: 49, width: 28, height: 28), enabled: month.year > 1900 || month.month > 1)
        button("nextMonth", "›", CGRect(x: 360, y: 49, width: 28, height: 28), enabled: month.year < 9999 || month.month < 12)
        let weekdays = formatter.veryShortStandaloneWeekdaySymbols ?? []
        let first = HUDCalendarDay.calendar(controller.zone).firstWeekday - 1
        if weekdays.count == 7 { for index in 0..<7 { text(weekdays[(index + first) % 7], CGRect(x: 12 + index * 54, y: 82, width: 50, height: 14), 9, color: ink.withAlphaComponent(0.5), centered: true) } }
        let occupied = Set(controller.events.map(\.day))
        for (day, rect) in monthCells {
            button("day:" + day.string, String(day.day), rect, selected: day == selectedDay)
            if day == controller.today {
                let today = CALayer(); today.frame = rect.insetBy(dx: 1, dy: 1); today.cornerRadius = 2
                today.borderWidth = 0.8; today.borderColor = accent.withAlphaComponent(0.75).cgColor; face.addSublayer(today)
            }
            if occupied.contains(day) { let dot = CALayer(); dot.frame = CGRect(x: rect.midX - 1.5, y: rect.maxY - 4, width: 3, height: 2); dot.backgroundColor = accent.cgColor; face.addSublayer(dot) }
        }
        text(selectedDay.string, CGRect(x: 12, y: 281, width: 308, height: 22), 14)
        button("new", "+", CGRect(x: 359, y: 275, width: 29, height: 29), enabled: !controller.busy, light: true)
        let list = eventsForSelectedDay; firstEvent = min(firstEvent, max(0, list.count - 3))
        for (index, item) in list.dropFirst(firstEvent).prefix(3).enumerated() {
            let rect = CGRect(x: 12, y: 312 + index * 29, width: 376, height: 25)
            button("event:" + item.id.uuidString, item.title, rect, centered: false)
        }
        if list.isEmpty { text(L10n.text("No events", "暂无事项"), CGRect(x: 12, y: 336, width: 376, height: 25), 12, color: ink.withAlphaComponent(0.4), centered: true) }
        let warning = controller.error ?? ((controller.permission == .denied || controller.permission == .unavailable) ? controller.reminderStatus : nil)
        if let warning { text(warning, CGRect(x: 12, y: 398, width: 332, height: 15), 8, color: .systemOrange) }
        button("reminders", "", CGRect(x: 352, y: 384, width: 36, height: 29), enabled: !controller.busy, light: true)
        let arrow = CAShapeLayer(); arrow.name = "calendar.refresh.arrow"; arrow.bounds = CGRect(x: 0, y: 0, width: 22, height: 22)
        arrow.position = CGPoint(x: 370, y: 398.5); arrow.fillColor = NSColor(white: 0.13, alpha: controller.busy ? 0.35 : 1).cgColor
        let shaft = CGMutablePath(); shaft.addArc(center: CGPoint(x: 11, y: 11), radius: 6.2, startAngle: .pi / 3, endAngle: .pi * 65 / 36, clockwise: false)
        let path = CGMutablePath(); path.addPath(shaft.copy(strokingWithWidth: 1.7, lineCap: .round, lineJoin: .round, miterLimit: 1))
        path.move(to: CGPoint(x: 18.05, y: 10.1)); path.addLine(to: CGPoint(x: 13.25, y: 10.1)); path.addLine(to: CGPoint(x: 18.05, y: 5.3)); path.closeSubpath()
        arrow.path = path; face.addSublayer(arrow)
        if animated, !HUDRuntimeAppearance.reduceMotion { let change = CATransition(); change.type = .fade; change.duration = 0.18; face.add(change, forKey: kCATransition) }
        CATransaction.commit(); onChange?()
    }
}
