import AppKit

/// A passive native material behind the drawing; never intercepts HUD input.
final class HUDBackgroundBlurView: NSVisualEffectView {
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}

/// Separates the opening/closing envelope from the chosen material intensity.
/// Status updates can refresh the material without interrupting its fade.
final class HUDBackgroundContainerView: NSView {
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}
