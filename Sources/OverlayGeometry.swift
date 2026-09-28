import AppKit

struct OverlayScreen {
    let id: UInt32
    let visibleFrame: NSRect
}

/// Positions are fractions of a display's usable desktop, independent of resolution and scale.
/// Clamping prevents a saved point or a disconnected monitor from leaving the HUD unreachable.
enum OverlayGeometry {
    static func clampedAnchor(_ point: NSPoint, screen: NSRect, scale: Double, editing: Bool) -> NSPoint {
        let factor = CGFloat(scale.isFinite ? min(1.6, max(0.65, scale)) : 1)
        let halfWidth = min(150 * factor, screen.width / 2)
        let topMargin = min(30 * factor, screen.height / 2)
        let bottomMargin = min(editing ? 24 * factor + 34 : 30 * factor, screen.height / 2)
        let x = point.x.isFinite ? point.x : screen.midX
        let y = point.y.isFinite ? point.y : screen.midY
        return NSPoint(x: min(screen.maxX - halfWidth, max(screen.minX + halfWidth, x)),
                       y: min(screen.maxY - topMargin, max(screen.minY + bottomMargin, y)))
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
        return clampedAnchor(point, screen: screen, scale: configuration.scale, editing: editing)
    }

    static func normalizedPosition(anchor: NSPoint, screen: OverlayScreen) -> OverlayPosition {
        guard screen.visibleFrame.width > 0, screen.visibleFrame.height > 0 else { return OverlayPosition() }
        return OverlayPosition(screenID: screen.id,
                               x: Double((anchor.x - screen.visibleFrame.minX) / screen.visibleFrame.width),
                               y: Double((anchor.y - screen.visibleFrame.minY) / screen.visibleFrame.height)).normalized
    }
}
