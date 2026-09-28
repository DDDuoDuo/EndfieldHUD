import Foundation

/// Equirectangular world coordinates: west/east wrap, poles remain bounded.
/// Zooming preserves the terrain under the pointer except at a polar limit.
enum WorldMapGeometry {
    static let size = CGSize(width: 440, height: 440)
    static let center = CGPoint(x: 220, y: 220)
    static let radius: CGFloat = 216
    static func constrained(_ value: WorldMapViewport) -> WorldMapViewport {
        var value = (try? value.normalized()) ?? WorldMapViewport()
        let halfHeight = min(0.5, Double(size.height / 2) / (220 * value.zoom))
        value.centerY = min(1 - halfHeight, max(halfHeight, value.centerY))
        return value
    }
    static func world(at point: CGPoint, viewport: WorldMapViewport) -> CGPoint {
        CGPoint(x: viewport.centerX + Double(point.x - center.x) / (440 * viewport.zoom),
                y: viewport.centerY + Double(point.y - center.y) / (220 * viewport.zoom))
    }
    static func screen(x: Double, y: Double, viewport: WorldMapViewport) -> CGPoint {
        var dx = x - viewport.centerX
        dx -= dx.rounded()
        return CGPoint(x: center.x + dx * 440 * viewport.zoom,
                       y: center.y + (y - viewport.centerY) * 220 * viewport.zoom)
    }
    static func panned(_ viewport: WorldMapViewport, delta: CGPoint) -> WorldMapViewport {
        guard delta.x.isFinite, delta.y.isFinite else { return viewport }
        var next = viewport
        next.centerX -= Double(delta.x) / (440 * viewport.zoom)
        next.centerY -= Double(delta.y) / (220 * viewport.zoom)
        return constrained(next)
    }
    static func zoomed(_ viewport: WorldMapViewport, at point: CGPoint, factor: Double) -> WorldMapViewport {
        guard point.x.isFinite, point.y.isFinite, factor.isFinite, factor > 0 else { return viewport }
        let anchor = world(at: point, viewport: viewport)
        var next = viewport
        next.zoom = min(WorldMapViewport.maxZoom, max(WorldMapViewport.minZoom, viewport.zoom * factor))
        next.centerX = anchor.x - Double(point.x - center.x) / (440 * next.zoom)
        next.centerY = anchor.y - Double(point.y - center.y) / (220 * next.zoom)
        return constrained(next)
    }
    static func contains(_ point: CGPoint) -> Bool {
        point.x.isFinite && point.y.isFinite && hypot(point.x - center.x, point.y - center.y) <= radius
    }
    static func coordinateDescription(x: Double, y: Double) -> String {
        let longitude = WorldMapStore.wrappedX(x) * 360 - 180
        let latitude = 90 - y * 180
        return String(format: "%.2f°%@  %.2f°%@", abs(latitude), latitude < 0 ? "S" : "N",
                      abs(longitude), longitude < 0 ? "W" : "E")
    }
}
