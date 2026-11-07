import AppKit
import QuartzCore

/// Perspective can magnify text beyond the canvas's nominal backing scale.
/// Spend the extra resolution only on small glyph buffers, never whole planes.
enum HUDRenderScale {
    /// The highest pixels-per-point any HUD buffer is drawn at.
    static let maximum: CGFloat = 4

    static func contentScale(for layer: CALayer, baseScale: CGFloat) -> CGFloat {
        layer is CATextLayer || layer is HUDTextLayer ? glyphScale(baseScale: baseScale) : clamped(baseScale)
    }

    /// Glyph buffers on the tilted plane: the base scale with a 1.35x
    /// oversample, rounded up to whole pixels and capped at `maximum`.
    /// `magnification` is a layer transform that enlarges the buffer on
    /// screen; the buffer grows with it, so the cap stays `maximum` pixels
    /// per on-screen point, as for text that is enlarged by its font size.
    static func glyphScale(baseScale: CGFloat, magnification: CGFloat = 1) -> CGFloat {
        let factor = magnification.isFinite && magnification > 0 ? magnification : 1
        return min(maximum * factor, ceil(clamped(baseScale) * 1.35 * factor))
    }

    private static func clamped(_ scale: CGFloat) -> CGFloat {
        scale.isFinite ? min(maximum, max(1, scale)) : 2
    }
}
