import AppKit

struct OverlayScreen {
    let id: UInt32
    let visibleFrame: NSRect
}

/// What the battery bar editor commits: where the bar sits, its Size and its
/// Length (nil keeps the classic bar; see `AppConfiguration.chargeBarLength`).
struct ChargeBarLayout: Equatable {
    /// nil when the bar was not dragged, so the placement (Top centre or the
    /// saved custom point) stays as it was.
    var position: OverlayPosition?
    var scale: Double
    var length: Double?
}

/// Positions are fractions of a display's usable desktop, independent of resolution and scale.
/// Clamping prevents a saved point or a disconnected monitor from leaving the HUD unreachable.
enum OverlayGeometry {
    /// The compact bar inside ChargeIndicatorView's classic 300 pt canvas, the
    /// shortest bar the Length slider allows, and the canvas margin on each
    /// side of the bar (room for its shadow). All in the bar's own points,
    /// before its Size.
    static let classicBarWidth: CGFloat = 264
    static let minimumBarWidth: CGFloat = 200
    static let barMargin: CGFloat = 18

    /// The bar's Size range (65 % to 160 %).
    static let scaleRange: ClosedRange<Double> = 0.65...1.6

    /// The scale every geometry helper uses (AppConfiguration's clamp).
    static func factor(_ scale: Double) -> CGFloat {
        CGFloat(AppConfiguration.clamp(scale, to: scaleRange, fallback: 1))
    }

    /// The compact bar's width in its own points: `length` of the usable width
    /// at this Size, never under the 200 pt minimum nor past the screen edges.
    static func barWidth(length: Double?, screen: NSRect, scale: Double) -> CGFloat {
        guard let length, length.isFinite, length > 0 else { return classicBarWidth }
        let factor = factor(scale)
        let full = max(minimumBarWidth, screen.width / factor)
        return min(full, max(minimumBarWidth, CGFloat(min(1, length)) * screen.width / factor))
    }

    /// The canvas never narrows below the classic 300 pt, so the circle and the
    /// 240 pt entrance banner keep their room; only a longer bar widens it.
    static func canvasWidth(barWidth: CGFloat) -> CGFloat {
        max(ChargeIndicatorView.canvasSize.width, barWidth + 2 * barMargin)
    }

    /// The bar panel's frame for the canvas frame `bar`: cut at the display's
    /// sides, so a full-length bar's shadow margins never hang onto (or take
    /// clicks from) the next display. Shorter bars are already kept inside by
    /// `clampedAnchor`, so for them this is `bar` itself.
    static func panelFrame(bar: NSRect, screen: NSRect) -> NSRect {
        let minX = max(bar.minX, screen.minX), maxX = min(bar.maxX, screen.maxX)
        guard maxX > minX else { return bar }
        return NSRect(x: minX, y: bar.minY, width: maxX - minX, height: bar.height)
    }

    /// The Length slider's fractions on this display at this Size: from the
    /// 200 pt minimum up to edge to edge.
    static func lengthRange(screen: NSRect, scale: Double) -> ClosedRange<Double> {
        guard screen.width > 0 else { return 1...1 }
        let minimum = Double(minimumBarWidth * factor(scale) / screen.width)
        return min(1, minimum)...1
    }

    /// The fraction the Length slider shows for a bar (the classic bar too).
    static func length(of configuration: AppConfiguration, screen: NSRect) -> Double {
        guard screen.width > 0 else { return 1 }
        let width = barWidth(length: configuration.chargeBarLength, screen: screen, scale: configuration.scale)
        return min(1, Double(width * factor(configuration.scale) / screen.width))
    }

    /// `canvasWidth` is the bar's canvas in its own points (300 for the classic
    /// bar). `editing` keeps the 1.3.0 two-button editor's room below the bar;
    /// the Mac editor's strip flips above the bar instead and passes false.
    static func clampedAnchor(_ point: NSPoint, screen: NSRect, scale: Double,
                              canvasWidth: CGFloat = ChargeIndicatorView.canvasSize.width, editing: Bool) -> NSPoint {
        let factor = factor(scale)
        let halfWidth = min(canvasWidth * factor / 2, screen.width / 2)
        let topMargin = min(30 * factor, screen.height / 2)
        let bottomMargin = min(editing ? 24 * factor + 34 : 30 * factor, screen.height / 2)
        let x = point.x.isFinite ? point.x : screen.midX
        let y = point.y.isFinite ? point.y : screen.midY
        return NSPoint(x: min(screen.maxX - halfWidth, max(screen.minX + halfWidth, x)),
                       y: min(screen.maxY - topMargin, max(screen.minY + bottomMargin, y)))
    }

    /// The heights the visible bar (38 pt tall before Size) can cover while
    /// the editor's Size slider moves from `anchor`, up to the largest Size,
    /// including the shift when that Size's margins clamp the anchor by an
    /// edge. The editor strip keeps clear of it.
    static func editorReach(anchor: NSPoint, screen: NSRect) -> ClosedRange<CGFloat> {
        let largest = scaleRange.upperBound
        let reach = 19 * factor(largest)
        let clamped = clampedAnchor(anchor, screen: screen, scale: largest, editing: false).y
        return (min(anchor.y, clamped) - reach)...(max(anchor.y, clamped) + reach)
    }

    static func anchor(configuration: AppConfiguration, screen: NSRect, editing: Bool) -> NSPoint {
        let configuration = configuration.normalized
        let point: NSPoint
        if configuration.placement == .topCenter {
            point = NSPoint(x: screen.midX, y: screen.maxY - 30 * CGFloat(configuration.scale))
        } else {
            point = NSPoint(x: screen.minX + CGFloat(configuration.customPosition.x) * screen.width,
                            y: screen.minY + CGFloat(configuration.customPosition.y) * screen.height)
        }
        return clampedAnchor(point, configuration: configuration, screen: screen, editing: editing)
    }

    /// `clampedAnchor` for the bar `configuration` draws (its Size and Length).
    static func clampedAnchor(_ point: NSPoint, configuration: AppConfiguration, screen: NSRect, editing: Bool) -> NSPoint {
        let configuration = configuration.normalized
        let width = barWidth(length: configuration.chargeBarLength, screen: screen, scale: configuration.scale)
        return clampedAnchor(point, screen: screen, scale: configuration.scale,
                             canvasWidth: canvasWidth(barWidth: width), editing: editing)
    }

    /// The bar editor's preview at the drafts in `configuration`: a dragged
    /// bar where it was dropped, otherwise where Confirm keeps it (Top centre
    /// or the saved custom point, placed at the drafts' Size and Length).
    static func editorAnchor(draft: NSPoint, moved: Bool, configuration: AppConfiguration, screen: NSRect) -> NSPoint {
        moved ? clampedAnchor(draft, configuration: configuration, screen: screen, editing: false)
            : anchor(configuration: configuration, screen: screen, editing: false)
    }

    /// The Length draft a Length slider report (in percent) sets, or nil when
    /// it reports the value the slider already showed (a press or release
    /// without movement), so a classic (nil) bar stays classic.
    static func draftLength(reported value: Double, shown: Double?) -> Double? {
        value == shown ? nil : value / 100
    }

    static func normalizedPosition(anchor: NSPoint, screen: OverlayScreen) -> OverlayPosition {
        guard screen.visibleFrame.width > 0, screen.visibleFrame.height > 0 else { return OverlayPosition() }
        return OverlayPosition(screenID: screen.id,
                               x: Double((anchor.x - screen.visibleFrame.minX) / screen.visibleFrame.width),
                               y: Double((anchor.y - screen.visibleFrame.minY) / screen.visibleFrame.height)).normalized
    }
}
