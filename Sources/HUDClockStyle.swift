import AppKit
import QuartzCore

enum HUDClockStyle: String, Codable, CaseIterable {
    case digital, split, dial, rail, stacked
    var index: Int { Self.allCases.firstIndex(of: self) ?? 0 }
    /// The one source of the styles' names (Settings > Display).
    var title: String {
        switch self {
        case .digital: return L10n.text("Digital", "数字")
        case .split: return L10n.text("Split", "分栏")
        case .dial: return L10n.text("Dial", "表盘")
        case .rail: return L10n.text("Rail", "轨道")
        case .stacked: return L10n.text("Stacked", "堆叠")
        }
    }
    func advanced(_ direction: Int) -> Self {
        Self.allCases[(index + direction % 5 + 5) % 5]
    }
}

/// Only the inner page moves. Its clipping ancestor, authored frame, selection
/// marks and Work Mode badge remain on the status panel's original HUD plane.
final class HUDClockPageViewport {
    let layer = CALayer()
    let page = CALayer()
    static let contentRect = CGRect(x: 14, y: 12, width: 312, height: 75)

    init() {
        layer.name = "hud.clock.viewport"
        layer.frame = Self.contentRect; layer.masksToBounds = true
        page.name = "hud.clock.page"
        page.frame = CGRect(x: -Self.contentRect.minX, y: -Self.contentRect.minY,
                            width: HUDClockStyleArtwork.body.width, height: HUDClockStyleArtwork.body.height)
        layer.addSublayer(page)
    }

    func install(time: CATextLayer, date: CATextLayer, artwork: CALayer) {
        for item in [artwork, time, date] where item.superlayer !== page { page.addSublayer(item) }
    }

    func transition(forward: Bool, animated: Bool) {
        cancelTransition()
        guard animated else { return }
        let slide = CATransition()
        slide.type = .push; slide.subtype = forward ? .fromRight : .fromLeft
        slide.duration = 0.26; slide.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        page.add(slide, forKey: kCATransition)
    }

    func cancelTransition() { page.removeAnimation(forKey: kCATransition) }

    var hasContainedHorizontalTransition: Bool {
        guard layer.masksToBounds, page.superlayer === layer,
              let slide = page.animation(forKey: kCATransition) as? CATransition else { return false }
        return slide.type == .push && (slide.subtype == .fromLeft || slide.subtype == .fromRight)
            && slide.duration == 0.26 && slide.isRemovedOnCompletion
    }
}

/// Five compact instruments share the Watch banner's existing pose and clock.
/// Artwork changes only on a wall-clock sample, setting or hover, never per frame.
/// Each style fills the clock area (y 12–87); the status row below it (y 89–112)
/// is reserved for the Work Mode and Recording mode lines, which never overlap.
final class HUDClockStyleArtwork {
    let layer = CALayer()
    let selection = CAShapeLayer()
    /// The status row on the panel itself: it stays put while a style slides in.
    let status = CALayer()
    private let instrument = CAShapeLayer()
    private let fill = CAShapeLayer()
    private let track = CAShapeLayer()
    private let hands = CAShapeLayer()
    private let secondHand = CAShapeLayer()
    private let seconds = HUDStandardTextLayer(size: 22, weight: .bold, tabular: true)
    private let aux = HUDStandardTextLayer(size: 12, weight: .bold)
    private let rail = CAShapeLayer()
    private let recordDot = CALayer()
    private let recordLabel = HUDStandardTextLayer(size: 12, weight: .semibold)
    /// A status that is off (Work Mode idle, Recording mode off).
    private static let statusOff = NSColor(white: 0.45, alpha: 1).cgColor
    private var recording = false
    private var swipe: CGFloat = 0
    private var lastSwipeTime: TimeInterval = 0
    private var swipeConsumed = false
    static let body = CGRect(x: 0, y: 0, width: 340, height: 122)
    static let hitBounds = CGRect(x: 0, y: 0, width: 340, height: 145)
    /// Content columns of every style and the reserved status row.
    static let content = CGRect(x: 22, y: 12, width: 296, height: 75)
    static let statusRow = CGRect(x: 22, y: 92, width: 296, height: 18)
    static func indicatorRect(_ index: Int) -> CGRect { CGRect(x: 14 + CGFloat(index) * 64, y: 124, width: 56, height: 18) }
    static func indicator(at point: CGPoint) -> Int? {
        (0..<5).first { indicatorRect($0).contains(point) }
    }
    /// The source banner maps its zero-origin local plane directly into design
    /// coordinates. Unlike the legacy HUD planes it has no center-pivot offset.
    static func projectedBounds(of rect: CGRect, through transform: CATransform3D) -> CGRect {
        let corners = [CGPoint(x: rect.minX, y: rect.minY), CGPoint(x: rect.maxX, y: rect.minY),
                       CGPoint(x: rect.maxX, y: rect.maxY), CGPoint(x: rect.minX, y: rect.maxY)]
        var minX = CGFloat.greatestFiniteMagnitude, minY = minX
        var maxX = -minX, maxY = -minX
        for corner in corners {
            let point = HUDMotionMath.project(corner, through: transform)
            minX = min(minX, point.x); maxX = max(maxX, point.x)
            minY = min(minY, point.y); maxY = max(maxY, point.y)
        }
        return CGRect(x: minX, y: minY, width: maxX - minX, height: maxY - minY)
    }

    init() {
        layer.name = "hud.clock.style"; layer.frame = Self.body
        for shape in [track, fill] { shape.strokeColor = nil; layer.addSublayer(shape) }
        for shape in [instrument, hands, secondHand] {
            shape.fillColor = nil; shape.lineWidth = 1.5; shape.lineCap = .square; layer.addSublayer(shape)
        }
        hands.lineWidth = 2.5; hands.lineCap = .round; secondHand.lineWidth = 1.2
        selection.fillColor = nil; selection.lineWidth = 1.5
        selection.name = "hud.clock.selection"
        for text in [seconds, aux] { text.alignmentMode = .left; text.isWrapped = false; layer.addSublayer(text) }
        seconds.name = "hud.clock.seconds"; aux.name = "hud.clock.aux"; recordLabel.name = "hud.clock.recording"
        status.name = "hud.clock.status"; status.frame = Self.body
        rail.fillColor = nil; rail.lineWidth = 1
        recordDot.bounds = CGRect(x: 0, y: 0, width: 7, height: 7); recordDot.backgroundColor = HUDColor.red
        recordLabel.alignmentMode = .left; recordLabel.isWrapped = false
        recordLabel.foregroundColor = HUDColor.red
        for item in [rail, recordDot, recordLabel] as [CALayer] { status.addSublayer(item) }
    }

    /// One style per completed swipe; momentum cannot cascade through all five.
    func scroll(delta: CGFloat, phase: NSEvent.Phase, momentum: NSEvent.Phase, at time: TimeInterval) -> Int? {
        guard delta.isFinite, momentum.isEmpty else { return nil }
        if phase.contains(.began) || (phase.isEmpty && time - lastSwipeTime > 0.25) { swipe = 0; swipeConsumed = false }
        lastSwipeTime = time
        if phase.contains(.cancelled) || phase.contains(.ended) { swipe = 0; return nil }
        guard !delta.isZero else { return nil }
        guard !swipeConsumed else { return nil }
        swipe += delta
        guard abs(swipe) >= 28 else { return nil }
        let direction = swipe > 0 ? 1 : -1
        swipe = 0; swipeConsumed = true
        return direction
    }
    func resetGesture() { swipe = 0; lastSwipeTime = 0; swipeConsumed = false }

    /// The status row: Work Mode on the left (the host's badge), Recording
    /// mode on the right, both always shown and grey while off. Both read in
    /// English in every language. The host's render-scale pass owns every
    /// text layer's contentsScale (never set here).
    func updateStatus(work: HUDStandardTextLayer, workActive: Bool, recording: Bool, accent: NSColor) {
        CATransaction.begin(); CATransaction.setDisableActions(true); defer { CATransaction.commit() }
        self.recording = recording
        let row = Self.statusRow
        let ticks = CGMutablePath()
        ticks.move(to: CGPoint(x: row.minX, y: 88.5)); ticks.addLine(to: CGPoint(x: row.maxX, y: 88.5))
        ticks.addRect(CGRect(x: row.minX, y: 86, width: 14, height: 2.5))
        rail.path = ticks; rail.strokeColor = NSColor(white: 1, alpha: 0.16).cgColor
        rail.fillColor = accent.withAlphaComponent(0.8).cgColor
        recordLabel.string = "RECORDING MODE"
        work.fontSize = 12; work.weight = .semibold; work.alignmentMode = .left
        work.foregroundColor = workActive ? accent.cgColor : Self.statusOff
        recordLabel.foregroundColor = recording ? HUDColor.red : Self.statusOff
        recordDot.backgroundColor = recording ? HUDColor.red : Self.statusOff
        let recordWidth = ceil(Self.width(of: recordLabel))
        let recordX = row.maxX - recordWidth
        place(recordLabel, x: recordX, capTop: row.minY + 5, width: recordWidth + 1)
        recordDot.position = CGPoint(x: recordX - 10, y: row.minY + 9.5)
        if !recording { recordDot.removeAnimation(forKey: "clock.recording"); recordDot.opacity = 1 }
        place(work, x: row.minX, capTop: row.minY + 5, width: recordX - 24 - row.minX)
    }

    func update(style: HUDClockStyle, reading: HUDClockReading?, time: HUDStandardTextLayer, date: HUDStandardTextLayer,
                accent: NSColor) {
        CATransaction.begin(); CATransaction.setDisableActions(true); defer { CATransaction.commit() }
        let value = reading?.time ?? "--:--:--", day = reading?.date ?? ""
        // "HH:mm:ss" or "hh:mm:ss a": the meridiem is set apart in every style.
        let words = value.split(separator: " ", maxSplits: 1).map(String.init)
        let meridiem = words.count > 1 ? words[1] : ""
        let parts = (words.first ?? value).split(separator: ":").map(String.init)
        let hm = parts.prefix(2).joined(separator: ":"), ss = parts.count > 2 ? parts[2] : "--"
        let numbers = parts.map { Double($0) ?? 0 }
        var hour = numbers.first ?? 0
        if meridiem == "PM", hour < 12 { hour += 12 } else if meridiem == "AM", hour == 12 { hour = 0 }
        let minute = numbers.count > 1 ? numbers[1] : 0, second = numbers.count > 2 ? numbers[2] : 0
        let white = NSColor(white: 0.96, alpha: 1).cgColor, muted = NSColor(white: 0.64, alpha: 1).cgColor
        for item in [time, date, seconds, aux] { item.isWrapped = false; item.alignmentMode = .left }
        time.weight = .bold; time.tabular = true; date.weight = .medium
        time.foregroundColor = white; date.foregroundColor = muted
        seconds.foregroundColor = accent.cgColor; aux.foregroundColor = muted
        time.string = value; date.string = day; seconds.string = ss; aux.string = meridiem
        seconds.isHidden = true; aux.isHidden = meridiem.isEmpty
        instrument.strokeColor = accent.withAlphaComponent(0.55).cgColor
        hands.strokeColor = white; secondHand.strokeColor = accent.cgColor
        fill.fillColor = accent.cgColor; track.fillColor = NSColor(white: 1, alpha: 0.14).cgColor
        selection.strokeColor = accent.cgColor; selection.lineWidth = 3
        let selected = CGMutablePath(), rect = Self.indicatorRect(style.index)
        selected.move(to: CGPoint(x: rect.minX, y: 131)); selected.addLine(to: CGPoint(x: rect.maxX - 7, y: 131))
        selection.path = selected
        let lines = CGMutablePath(), solid = CGMutablePath(), dim = CGMutablePath(), hand = CGMutablePath(), sweep = CGMutablePath()
        let area = Self.content
        switch style {
        case .digital:
            // Full-width time; date and a 60-second ruler share the line below.
            time.string = words.first ?? value
            fit(time, size: 50, width: area.width - (meridiem.isEmpty ? 0 : 44))
            place(time, x: area.minX, capTop: 17, width: area.width)
            if !meridiem.isEmpty { aux.fontSize = 14; place(aux, x: area.maxX - 40, capTop: 17, width: 40); aux.alignmentMode = .right }
            date.fontSize = 13; place(date, x: area.minX, capTop: 68, width: 120)
            let start = area.minX + min(124, ceil(Self.width(of: date)) + 14), end = area.maxX
            for index in 0...60 {
                let x = start + (end - start) * CGFloat(index) / 60, tall: CGFloat = index % 15 == 0 ? 9 : index % 5 == 0 ? 6 : 3
                (index <= Int(second) ? solid : dim).addRect(CGRect(x: x - 0.5, y: 80 - tall, width: 1, height: tall))
            }
        case .split:
            // HH:MM over the full height | seconds and date in their own column.
            time.string = hm
            fit(time, size: 62, width: 196)
            place(time, x: area.minX, capTop: 20, width: 200)
            lines.move(to: CGPoint(x: 226, y: 18)); lines.addLine(to: CGPoint(x: 226, y: 82))
            seconds.isHidden = false; seconds.fontSize = 30
            place(seconds, x: 238, capTop: 20, width: 46)
            if !meridiem.isEmpty { aux.fontSize = 12; place(aux, x: 286, capTop: 22, width: 32) }
            date.fontSize = 12; fit(date, size: 12, width: 80); place(date, x: 238, capTop: 58, width: 80)
            solid.addRect(CGRect(x: 238, y: 74, width: 80 * CGFloat(second + 1) / 60, height: 3))
            dim.addRect(CGRect(x: 238, y: 74, width: 80, height: 3))
        case .dial:
            // A 74-point dial with a second hand; the digital reading beside it.
            let center = CGPoint(x: area.minX + 37, y: 49.5)
            lines.addEllipse(in: CGRect(x: center.x - 36, y: center.y - 36, width: 72, height: 72))
            for tick in 0..<60 {
                let angle = CGFloat(tick) * .pi / 30
                if tick % 5 == 0 {
                    lines.move(to: CGPoint(x: center.x + sin(angle) * 26, y: center.y - cos(angle) * 26))
                    lines.addLine(to: CGPoint(x: center.x + sin(angle) * 32, y: center.y - cos(angle) * 32))
                } else {
                    dim.addEllipse(in: CGRect(x: center.x + sin(angle) * 31 - 0.6, y: center.y - cos(angle) * 31 - 0.6, width: 1.2, height: 1.2))
                }
            }
            for (angle, length) in [((hour.truncatingRemainder(dividingBy: 12) + minute / 60) * .pi / 6, 17.0), ((minute + second / 60) * .pi / 30, 25.0)] {
                hand.move(to: center); hand.addLine(to: CGPoint(x: center.x + sin(angle) * length, y: center.y - cos(angle) * length))
            }
            let secondAngle = second * .pi / 30
            sweep.move(to: CGPoint(x: center.x - sin(secondAngle) * 7, y: center.y + cos(secondAngle) * 7))
            sweep.addLine(to: CGPoint(x: center.x + sin(secondAngle) * 30, y: center.y - cos(secondAngle) * 30))
            solid.addEllipse(in: CGRect(x: center.x - 2.5, y: center.y - 2.5, width: 5, height: 5))
            time.string = words.first ?? value
            let column = area.maxX - 104
            fit(time, size: 38, width: column - (meridiem.isEmpty ? 0 : 30))
            place(time, x: 104, capTop: 22, width: column)
            if !meridiem.isEmpty { aux.fontSize = 12; aux.alignmentMode = .right; place(aux, x: area.maxX - 30, capTop: 22, width: 30) }
            date.fontSize = 13; place(date, x: 104, capTop: 64, width: column)
            lines.move(to: CGPoint(x: 104, y: 56.5)); lines.addLine(to: CGPoint(x: area.maxX, y: 56.5))
        case .rail:
            // Time and date on one line above a 24-hour rail of the day so far.
            time.string = words.first ?? value
            fit(time, size: 40, width: 200)
            place(time, x: area.minX, capTop: 17, width: 210)
            date.fontSize = 13; date.alignmentMode = .right
            place(date, x: area.maxX - 90, capTop: 20, width: 90)
            if !meridiem.isEmpty { aux.fontSize = 13; aux.alignmentMode = .right; place(aux, x: area.maxX - 90, capTop: 40, width: 90) }
            let left = area.minX, width = area.width, progress = CGFloat((hour * 60 + minute) / 1440)
            for index in 0...24 {
                let x = left + width * CGFloat(index) / 24, tall: CGFloat = index % 6 == 0 ? 8 : 4
                dim.addRect(CGRect(x: x - 0.5, y: 71 - tall, width: 1, height: tall))
            }
            dim.addRect(CGRect(x: left, y: 74, width: width, height: 4))
            solid.addRect(CGRect(x: left, y: 74, width: max(1, width * progress), height: 4))
            let marker = left + width * progress
            solid.move(to: CGPoint(x: marker - 4, y: 82)); solid.addLine(to: CGPoint(x: marker + 4, y: 82))
            solid.addLine(to: CGPoint(x: marker, y: 78)); solid.closeSubpath()
        case .stacked:
            // A date band in the accent over the time; this week's days at its side.
            solid.addPath(HUDShape.chamfer(CGRect(x: area.minX, y: 14, width: area.width, height: 21), cut: 7))
            date.fontSize = 13; date.weight = .bold; date.foregroundColor = HUDColor.nearBlack
            place(date, x: area.minX + 8, capTop: 20, width: 150)
            if !meridiem.isEmpty { aux.fontSize = 12; aux.alignmentMode = .right; aux.foregroundColor = HUDColor.nearBlack; place(aux, x: area.maxX - 48, capTop: 20.5, width: 40) }
            time.string = words.first ?? value
            fit(time, size: 44, width: 222)
            place(time, x: area.minX, capTop: 46, width: 226)
            let weekday = ["MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"].firstIndex(of: String(day.prefix(3))) ?? -1
            for index in 0..<7 {
                let cell = CGRect(x: area.maxX - 66 + CGFloat(index) * 9.5, y: 62, width: 7, height: 16)
                (index == weekday ? solid : dim).addRect(index == weekday ? cell.insetBy(dx: 0, dy: -4) : cell)
            }
        }
        instrument.path = lines; fill.path = solid; track.path = dim; hands.path = hand; secondHand.path = sweep
        // The recording dot blinks with the wall clock: no loop of its own.
        guard recording else { return }
        let lit: Float = Int(second) % 2 == 0 ? 1 : 0.28, current = recordDot.presentation()?.opacity ?? recordDot.opacity
        recordDot.removeAnimation(forKey: "clock.recording")
        recordDot.opacity = lit
        guard !HUDRuntimeAppearance.reduceMotion else { recordDot.opacity = 1; return }
        let fade = CABasicAnimation(keyPath: "opacity")
        fade.fromValue = current; fade.toValue = lit; fade.duration = 0.35
        fade.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        recordDot.add(fade, forKey: "clock.recording")
    }

    /// The largest size up to `size` at which the layer's text fits `width`.
    private func fit(_ text: HUDStandardTextLayer, size: CGFloat, width: CGFloat) {
        text.fontSize = size
        let measured = Self.width(of: text)
        if measured > width { text.fontSize = max(10, floor(size * width / measured * 2) / 2) }
    }

    /// Frames a single line so its capitals start at `capTop` (top-aligned line box).
    private func place(_ text: HUDStandardTextLayer, x: CGFloat, capTop: CGFloat, width: CGFloat) {
        let font = (text.font as? NSFont) ?? HUDFontResolver.nsFont(size: text.fontSize, weight: text.weight)
        let lineHeight = ceil(font.ascender - font.descender + font.leading) + 1
        text.frame = CGRect(x: x, y: capTop - (font.ascender - font.capHeight), width: max(0, width), height: lineHeight)
    }

    static func width(of text: HUDStandardTextLayer) -> CGFloat {
        guard let string = text.string as? String, !string.isEmpty,
              let font = text.font as? NSFont else { return 0 }
        return (string as NSString).size(withAttributes: [.font: font]).width
    }
}

final class HUDClockActionButton: NSButton {
    var styleIndex = -1
    /// The indicator's current projection in screen coordinates; the view
    /// frame itself only follows the camera once it settles.
    var projectedFrame: (() -> CGRect)?
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}
