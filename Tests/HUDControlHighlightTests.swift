import AppKit
import QuartzCore

enum HUDControlHighlightTests {
    static func run() -> Int {
        var count = 0
        func check(_ result: Bool, _ message: String) {
            count += 1; if !result { fatalError(message) }
        }
        let original = HUDRuntimeAppearance.configuration
        defer { HUDRuntimeAppearance.configuration = original }
        HUDRuntimeAppearance.configuration.reduceMotion = true
        HUDRuntimeAppearance.configuration.accentHex = "32A7E0"
        let root = CALayer(); root.frame = CGRect(x: 0, y: 0, width: 400, height: 334)
        let container = CALayer(); container.frame = CGRect(x: 30, y: 40, width: 150, height: 80)
        root.addSublayer(container)
        let large = HUDControlHighlightLayer.add(to: container, rect: container.bounds, shape: .cutCorner, framed: true)
        let small = HUDControlHighlightLayer.add(to: container, rect: CGRect(x: 80, y: 10, width: 30, height: 25))
        func opacity(_ layer: CALayer) -> Float { layer.sublayers!.first!.opacity }
        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: 125, y: 60))
        check(opacity(small) > 0 && opacity(large) == 0, "The small nested action wins over its enclosing card")
        check(small.sublayers!.allSatisfy { ($0.animationKeys() ?? []).isEmpty }, "Reduce Motion changes feedback without animation")
        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: 125, y: 60), pressed: true)
        check(opacity(small) == 1, "Pressed feedback is stronger than hover feedback")
        small.setEnabled(false)
        check(opacity(small) == 0, "Disabling a control clears existing feedback")
        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: 125, y: 60))
        check(opacity(small) == 0 && opacity(large) > 0, "Disabled actions do not receive hover")
        let rim = large.sublayers!.last as! CAShapeLayer
        check(rim.path!.boundingBox.minX < large.bounds.minX && rim.path!.boundingBox.maxX > large.bounds.maxX,
              "Irregular control frames sit outside the face with a visible gap")
        check(NSColor(cgColor: rim.strokeColor!)!.isEqual(HUDRuntimeAppearance.accent), "Feedback follows the selected accent")
        HUDRuntimeAppearance.configuration.accentHex = "DE468A"
        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: 125, y: 60))
        check(NSColor(cgColor: rim.strokeColor!)!.isEqual(HUDRuntimeAppearance.accent), "Stationary hovered controls can adopt a new theme")
        container.isHidden = true
        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: 125, y: 60))
        check(opacity(large) == 0, "Hidden ancestor layers suppress hover")
        container.isHidden = false
        let mask = CAShapeLayer(); mask.frame = container.bounds
        mask.path = CGPath(rect: CGRect(x: 0, y: 0, width: 40, height: 80), transform: nil)
        container.mask = mask
        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: 125, y: 60))
        check(opacity(large) == 0, "Clipped controls cannot highlight outside their visible mask")
        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: 50, y: 60))
        check(opacity(large) > 0, "Mask-visible controls retain feedback")
        HUDControlHighlightLayer.update(in: root, point: nil)
        check(opacity(large) == 0 && opacity(small) == 0, "Pointer exit clears feedback")
        container.mask = nil
        // Rich graphs/tables contain many decorative layers between controls.
        // Their transforms must not be queried as if each were a hit target.
        let artwork = ConversionCountingLayer()
        artwork.frame = container.bounds; container.addSublayer(artwork)
        artwork.transform = CATransform3DMakeTranslation(10, 5, 0)
        let nested = HUDControlHighlightLayer.add(to: artwork, rect: CGRect(x: 0, y: 0, width: 20, height: 20))
        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: 45, y: 50))
        check(opacity(nested) > 0, "A control inside transformed decoration keeps its exact hit region")
        check(artwork.conversions == 0, "Decorative ancestors perform no redundant point conversion")
        artwork.masksToBounds = true
        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: 45, y: 50))
        check(artwork.conversions == 1 && opacity(nested) > 0, "Clip boundaries still transform and accept visible controls")
        artwork.bounds = CGRect(x: 0, y: 0, width: 1, height: 1)
        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: 45, y: 50))
        check(opacity(nested) == 0, "Ancestor clipping clears a previously highlighted descendant")
        artwork.isHidden = true
        let priorConversions = artwork.conversions
        HUDControlHighlightLayer.update(in: root, point: CGPoint(x: 45, y: 50))
        check(artwork.conversions == priorConversions && opacity(nested) == 0,
              "Hidden branches clear feedback without converting invisible geometry")
        return count
    }

    private final class ConversionCountingLayer: CALayer {
        var conversions = 0
        override func convert(_ point: CGPoint, from layer: CALayer?) -> CGPoint {
            conversions += 1
            return super.convert(point, from: layer)
        }
    }
}
