import AppKit
import QuartzCore

/// An event-driven cursor image in the caller's untransformed coordinate space.
/// The caller owns focus, hit testing, native-control yielding and teardown.
final class HUDRenderedCursor {
    let layer = CALayer()
    private let hideCursor: () -> Void
    private let unhideCursor: () -> Void
    private var ownsHide = false
    private var image: CGImage?

    init(hideCursor: @escaping () -> Void = NSCursor.hide,
         unhideCursor: @escaping () -> Void = NSCursor.unhide) {
        self.hideCursor = hideCursor; self.unhideCursor = unhideCursor
        withoutActions {
            layer.name = "hud.renderedCursor"
            layer.anchorPoint = .zero
            // Core Animation rejects depths at or beyond FLT_MAX.
            layer.zPosition = 5_000_000
            layer.contentsGravity = .resize
            layer.isHidden = true
        }
    }

    /// Size, hotspot and point are all in the supplied parent's coordinates.
    /// Reparenting preserves the single hide lease. A caller detaching the
    /// host must explicitly hide; this component installs no observers.
    func show(image: CGImage, size: CGSize, hotspot: CGPoint, point: CGPoint, parent: CALayer) {
        let position = CGPoint(x: point.x - hotspot.x, y: point.y - hotspot.y)
        guard size.width.isFinite, size.height.isFinite, size.width > 0, size.height > 0,
              hotspot.x.isFinite, hotspot.y.isFinite, position.x.isFinite, position.y.isFinite else {
            hide(); return
        }
        withoutActions {
            if layer.superlayer !== parent { parent.addSublayer(layer) }
            if self.image !== image { self.image = image; layer.contents = image }
            let bounds = CGRect(origin: .zero, size: size)
            if layer.bounds != bounds { layer.bounds = bounds }
            if layer.position != position { layer.position = position }
            if layer.isHidden { layer.isHidden = false }
        }
        if !ownsHide {
            ownsHide = true
            hideCursor()
        }
    }

    func hide() {
        withoutActions {
            if !layer.isHidden { layer.isHidden = true }
        }
        if ownsHide {
            ownsHide = false
            unhideCursor()
        }
    }

    deinit {
        hide()
        withoutActions { layer.removeFromSuperlayer() }
    }

    private func withoutActions(_ body: () -> Void) {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        body()
        CATransaction.commit()
    }
}
