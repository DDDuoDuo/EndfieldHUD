import AppKit
import QuartzCore

/// A bounds-origin change is a coordinate-frame replacement, not camera motion.
/// Explicit camera starts use the presented coordinate frame, including when a
/// new zoom interrupts an earlier one. An implicit action would instead read
/// the old, unrebased presentation position after a bounds replacement.
enum WorldMapLayerCoordinates {
    struct Presentation {
        let position: CGPoint
        let bounds: CGRect
        let anchorPoint: CGPoint
        let transform: CGAffineTransform
    }
    static func capture(_ layer: CALayer) -> Presentation {
        let current = layer.presentation() ?? layer
        return Presentation(position: current.position, bounds: current.bounds,
                            anchorPoint: current.anchorPoint, transform: current.affineTransform())
    }
    static func rebase(_ layer: CALayer, to bounds: CGRect) {
        let current = Presentation(position: layer.position, bounds: layer.bounds,
                                   anchorPoint: layer.anchorPoint, transform: layer.affineTransform())
        layer.position = rebasedPosition(current, bounds: bounds, anchorPoint: layer.anchorPoint)
        layer.bounds = bounds
    }
    static func apply(_ layer: CALayer, position: CGPoint, transform: CGAffineTransform,
                      from start: Presentation?) {
        // Pointer updates already run inside a disabled-action transaction.
        // Avoid nesting transactions or rewriting stable scale on every plate.
        let needsTransaction = !CATransaction.disableActions()
        if needsTransaction { CATransaction.begin(); CATransaction.setDisableActions(true) }
        if let keys = layer.animationKeys() {
            for key in keys where key == "position" || key == "transform" || key == "bounds" {
                layer.removeAnimation(forKey: key)
            }
        }
        if layer.position != position { layer.position = position }
        if layer.affineTransform() != transform { layer.setAffineTransform(transform) }
        if needsTransaction { CATransaction.commit() }
        guard let start else { return }
        let previousPosition = rebasedPosition(start, bounds: layer.bounds, anchorPoint: layer.anchorPoint)
        if previousPosition != position {
            let motion = CABasicAnimation(keyPath: "position")
            motion.fromValue = NSValue(point: previousPosition); motion.toValue = NSValue(point: position)
            add(motion, to: layer)
        }
        if start.transform != transform {
            let motion = CABasicAnimation(keyPath: "transform")
            motion.fromValue = NSValue(caTransform3D: CATransform3DMakeAffineTransform(start.transform))
            motion.toValue = NSValue(caTransform3D: CATransform3DMakeAffineTransform(transform))
            add(motion, to: layer)
        }
    }
    private static func rebasedPosition(_ start: Presentation, bounds: CGRect, anchorPoint: CGPoint) -> CGPoint {
        let dx = bounds.minX + bounds.width*anchorPoint.x - start.bounds.minX - start.bounds.width*start.anchorPoint.x
        let dy = bounds.minY + bounds.height*anchorPoint.y - start.bounds.minY - start.bounds.height*start.anchorPoint.y
        return CGPoint(x: start.position.x + dx*start.transform.a + dy*start.transform.c,
                       y: start.position.y + dx*start.transform.b + dy*start.transform.d)
    }
    private static func add(_ animation: CABasicAnimation, to layer: CALayer) {
        animation.duration = 0.18
        animation.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        layer.add(animation, forKey: animation.keyPath)
    }
}
