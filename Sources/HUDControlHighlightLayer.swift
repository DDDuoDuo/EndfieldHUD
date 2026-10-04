import AppKit
import QuartzCore

protocol HUDControlFeedbackHost: AnyObject {
    func refreshControlHighlights()
}

/// Lightweight retained feedback for controls drawn into the HUD. Hit testing
/// stays with the existing interactions; these layers only render feedback.
final class HUDControlHighlightLayer: CALayer {
    enum Shape { case rounded, cutCorner, ellipse }

    private let tint = CAShapeLayer()
    private let rim = CAShapeLayer()
    private var controlPath = CGPath(rect: .zero, transform: nil)
    private var enabled = true
    private var framed = false
    private var highlighted = false
    private var pressed = false
    private var color = NSColor.clear
    var accentOverride: NSColor? { didSet { setHighlighted(highlighted, pressed: pressed, force: true) } }
    private static let pendingHosts = NSHashTable<NSView>.weakObjects()

    /// Canvas mutations may replace artwork without a new pointer event. One
    /// deferred refresh per host restores feedback after the whole transaction.
    static func requestRefresh(on host: NSView?) {
        guard let host, host is HUDControlFeedbackHost, !pendingHosts.contains(host) else { return }
        pendingHosts.add(host)
        DispatchQueue.main.async { [weak host] in
            guard let host else { return }
            pendingHosts.remove(host)
            (host as? HUDControlFeedbackHost)?.refreshControlHighlights()
        }
    }

    static func highlightedCount(in root: CALayer) -> Int {
        (root as? HUDControlHighlightLayer).map { $0.highlighted ? 1 : 0 }
            ?? (root.sublayers ?? []).reduce(0) { $0 + highlightedCount(in: $1) }
    }

    override init() { super.init() }
    override init(layer: Any) { super.init(layer: layer) }
    required init?(coder: NSCoder) { super.init(coder: coder) }

    func setEnabled(_ value: Bool) {
        guard enabled != value else { return }
        enabled = value
        setHighlighted(false, pressed: false, force: true)
    }

    @discardableResult
    static func add(to parent: CALayer, rect: CGRect, shape: Shape = .rounded,
                    enabled: Bool = true, framed: Bool = false) -> HUDControlHighlightLayer {
        let result = HUDControlHighlightLayer()
        result.name = "hud.control.highlight"
        result.frame = rect
        result.enabled = enabled; result.framed = framed
        result.allowsGroupOpacity = false
        result.controlPath = path(in: result.bounds, shape: shape)
        result.tint.frame = result.bounds; result.tint.path = result.controlPath
        result.tint.strokeColor = nil; result.tint.opacity = 0
        result.rim.frame = result.bounds
        result.rim.path = path(in: result.bounds.insetBy(dx: framed ? -2 : 0, dy: framed ? -2 : 0), shape: shape)
        result.rim.fillColor = nil; result.rim.lineWidth = 0.9
        result.rim.opacity = enabled && framed ? 0.28 : 0
        result.addSublayer(result.tint); result.addSublayer(result.rim)
        parent.addSublayer(result)
        result.refreshColor()
        return result
    }

    /// Point is in `root` coordinates. Clipped/hidden controls cannot light up;
    /// a nested small action takes priority over its encompassing object card.
    static func update(in root: CALayer, point: CGPoint?, pressed: Bool = false) {
        var candidates: [HUDControlHighlightLayer] = []
        var hit: HUDControlHighlightLayer?
        func visit(_ item: CALayer, visible: Bool, pointVisible: Bool) {
            let shown = visible && !item.isHidden && item.opacity > 0.01
            var inside = pointVisible && shown
            // Artwork and text layers have no hit region. Convert only at
            // actual controls and clip boundaries; walking every glyph/graph
            // layer through Core Animation's ancestor transforms on each
            // pointer event does not contribute to the highlight result.
            let needsLocal = inside && (item is HUDControlHighlightLayer || item.masksToBounds || item.mask != nil)
            let local = needsLocal ? point.map { item.convert($0, from: root) } : nil
            if let local {
                if item.masksToBounds && !item.bounds.contains(local) { inside = false }
                if let mask = item.mask {
                    let p = CGPoint(x: local.x - mask.frame.minX + mask.bounds.minX,
                                    y: local.y - mask.frame.minY + mask.bounds.minY)
                    if let shape = mask as? CAShapeLayer, let path = shape.path {
                        if !path.contains(p) { inside = false }
                    } else if !mask.bounds.contains(p) { inside = false }
                }
            }
            if let feedback = item as? HUDControlHighlightLayer {
                candidates.append(feedback)
                if inside, feedback.enabled, let local, feedback.controlPath.contains(local),
                   hit == nil || feedback.bounds.width * feedback.bounds.height <= hit!.bounds.width * hit!.bounds.height {
                    hit = feedback
                }
                return
            }
            item.sublayers?.forEach { visit($0, visible: shown, pointVisible: inside) }
        }
        visit(root, visible: true, pointVisible: point != nil)
        for candidate in candidates { candidate.setHighlighted(candidate === hit, pressed: pressed) }
    }

    private func refreshColor() {
        color = accentOverride ?? HUDRuntimeAppearance.accent
        tint.fillColor = color.withAlphaComponent(0.30).cgColor
        rim.strokeColor = color.cgColor
    }

    private func setHighlighted(_ value: Bool, pressed: Bool, force: Bool = false) {
        let changed = highlighted != value || self.pressed != (pressed && value)
        let colorChanged = !color.isEqual(accentOverride ?? HUDRuntimeAppearance.accent)
        guard changed || colorChanged || force else { return }
        highlighted = value; self.pressed = pressed && value
        CATransaction.begin(); CATransaction.setDisableActions(true)
        refreshColor()
        animateOpacity(tint, to: value ? (self.pressed ? 1 : 0.62) : 0)
        animateOpacity(rim, to: value ? 1 : (enabled && framed ? 0.28 : 0))
        CATransaction.commit()
    }

    private func animateOpacity(_ item: CALayer, to value: Float) {
        let current = item.presentation()?.opacity ?? item.opacity
        item.removeAnimation(forKey: "control.highlight")
        item.opacity = value
        guard !HUDRuntimeAppearance.reduceMotion, current != value else { return }
        let animation = CABasicAnimation(keyPath: "opacity")
        animation.fromValue = current; animation.toValue = value
        animation.duration = pressed ? 0.06 : 0.14
        animation.timingFunction = CAMediaTimingFunction(name: .easeOut)
        item.add(animation, forKey: "control.highlight")
    }

    private static func path(in rect: CGRect, shape: Shape) -> CGPath {
        switch shape {
        case .rounded: return CGPath(roundedRect: rect, cornerWidth: 3, cornerHeight: 3, transform: nil)
        case .ellipse: return CGPath(ellipseIn: rect, transform: nil)
        case .cutCorner:
            let corner: CGFloat = min(4, min(rect.width, rect.height) / 3)
            let path = CGMutablePath()
            path.move(to: CGPoint(x: rect.minX + corner, y: rect.minY))
            path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
            path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - corner))
            path.addLine(to: CGPoint(x: rect.maxX - corner, y: rect.maxY))
            path.addLine(to: CGPoint(x: rect.minX, y: rect.maxY))
            path.addLine(to: CGPoint(x: rect.minX, y: rect.minY + corner))
            path.closeSubpath(); return path
        }
    }
}
