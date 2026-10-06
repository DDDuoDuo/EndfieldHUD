import AppKit
import QuartzCore

enum HUDClockStyle: String, Codable, CaseIterable {
    case digital, split, dial, rail, stacked
    var index: Int { Self.allCases.firstIndex(of: self) ?? 0 }
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
final class HUDClockStyleArtwork {
    let layer = CALayer()
    let selection = CAShapeLayer()
    private let instrument = CAShapeLayer()
    private let hands = CAShapeLayer()
    private let seconds = CATextLayer()
    private var swipe: CGFloat = 0
    private var lastSwipeTime: TimeInterval = 0
    private var swipeConsumed = false
    static let body = CGRect(x: 0, y: 0, width: 340, height: 122)
    static let hitBounds = CGRect(x: 0, y: 0, width: 340, height: 145)
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
        for shape in [instrument, hands] {
            shape.fillColor = nil; shape.lineWidth = 1.5; layer.addSublayer(shape)
        }
        selection.fillColor = nil; selection.lineWidth = 1.5
        selection.name = "hud.clock.selection"
        seconds.font = NSFont.monospacedDigitSystemFont(ofSize: 22, weight: .semibold)
        seconds.fontSize = 22; seconds.alignmentMode = .center
        layer.addSublayer(seconds)
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

    func update(style: HUDClockStyle, reading: HUDClockReading?, time: CATextLayer, date: CATextLayer,
                accent: NSColor, scale: CGFloat) {
        CATransaction.begin(); CATransaction.setDisableActions(true); defer { CATransaction.commit() }
        let value = reading?.time ?? "--:--:--", day = reading?.date ?? ""
        time.string = value; date.string = day
        time.alignmentMode = .right; date.alignmentMode = .right
        time.font = NSFont.monospacedDigitSystemFont(ofSize: 32, weight: .semibold)
        time.fontSize = 32; date.fontSize = 13
        time.frame = CGRect(x: 22, y: 20, width: 296, height: 39)
        date.frame = CGRect(x: 22, y: 61, width: 296, height: 20)
        instrument.path = nil; hands.path = nil; seconds.isHidden = true
        for item in [time, date, seconds] { item.contentsScale = scale }
        instrument.strokeColor = accent.withAlphaComponent(0.55).cgColor
        hands.strokeColor = NSColor.white.cgColor
        seconds.foregroundColor = accent.cgColor
        selection.strokeColor = accent.cgColor; selection.lineWidth = 3
        let selected = CGMutablePath(), rect = Self.indicatorRect(style.index)
        selected.move(to: CGPoint(x: rect.minX, y: 131)); selected.addLine(to: CGPoint(x: rect.maxX - 7, y: 131))
        selection.path = selected
        let path = CGMutablePath()
        switch style {
        case .digital: break
        case .split:
            let tokens = value.split(separator: ":", maxSplits: 2).map(String.init)
            time.string = tokens.prefix(2).joined(separator: ":")
            time.fontSize = 42; time.frame = CGRect(x: 18, y: 15, width: 210, height: 51)
            seconds.isHidden = false; seconds.string = tokens.count == 3 ? tokens[2] : "--"
            seconds.frame = CGRect(x: 239, y: 33, width: 79, height: 30)
            path.move(to: CGPoint(x: 232, y: 24)); path.addLine(to: CGPoint(x: 232, y: 76))
        case .dial:
            let center = CGPoint(x: 57, y: 49)
            path.addEllipse(in: CGRect(x: 22, y: 14, width: 70, height: 70))
            for tick in 0..<12 {
                let angle = CGFloat(tick) * .pi / 6
                path.move(to: CGPoint(x: center.x + sin(angle) * 29, y: center.y - cos(angle) * 29))
                path.addLine(to: CGPoint(x: center.x + sin(angle) * 34, y: center.y - cos(angle) * 34))
            }
            let components = value.split(separator: ":").map { Double($0.prefix(2)) ?? 0 }
            let handPath = CGMutablePath()
            if components.count >= 2 {
                for (angle, length) in [(components[0].truncatingRemainder(dividingBy: 12) * .pi / 6 + components[1] * .pi / 360, 18.0), (components[1] * .pi / 30, 27.0)] {
                    handPath.move(to: center); handPath.addLine(to: CGPoint(x: center.x + sin(angle) * length, y: center.y - cos(angle) * length))
                }
            }
            hands.path = handPath
            time.fontSize = 25; time.frame = CGRect(x: 105, y: 24, width: 213, height: 34)
            date.frame = CGRect(x: 105, y: 64, width: 213, height: 18)
        case .rail:
            time.alignmentMode = .center; date.alignmentMode = .center
            time.frame = CGRect(x: 26, y: 22, width: 288, height: 39)
            for x in stride(from: CGFloat(27), through: 313, by: 13) {
                path.move(to: CGPoint(x: x, y: 14)); path.addLine(to: CGPoint(x: x, y: x == 27 || x > 307 ? 78 : 19))
            }
            path.move(to: CGPoint(x: 27, y: 83)); path.addLine(to: CGPoint(x: 313, y: 83))
        case .stacked:
            date.alignmentMode = .left; date.frame = CGRect(x: 26, y: 17, width: 286, height: 20)
            time.alignmentMode = .left; time.frame = CGRect(x: 26, y: 43, width: 286, height: 39)
            path.move(to: CGPoint(x: 26, y: 39)); path.addLine(to: CGPoint(x: 314, y: 39))
            path.addRect(CGRect(x: 303, y: 20, width: 11, height: 11))
        }
        instrument.path = path
    }
}

final class HUDClockActionButton: NSButton {
    var styleIndex = -1
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}
