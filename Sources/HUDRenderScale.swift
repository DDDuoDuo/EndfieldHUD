import AppKit
import QuartzCore

/// Perspective can magnify text beyond the canvas's nominal backing scale.
/// Spend the extra resolution only on small glyph buffers, never whole planes.
enum HUDRenderScale {
    static func contentScale(for layer: CALayer, baseScale: CGFloat) -> CGFloat {
        let base = baseScale.isFinite ? min(4, max(1, baseScale)) : 2
        return layer is CATextLayer ? min(4, ceil(base * 1.35)) : base
    }
}
