import AppKit
import QuartzCore

enum WorldMapLayerCoordinatesTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func near(_ a: CGFloat, _ b: CGFloat) -> Bool { abs(a - b) < 0.000_001 }
        func samePoint(_ a: CGPoint, _ b: CGPoint) -> Bool { near(a.x, b.x) && near(a.y, b.y) }
        // Replacing a clipped working set must not itself move the world.
        // Test both standard map scaling and a general affine transform.
        for transform in [CGAffineTransform(scaleX: 31, y: 31),
                          CGAffineTransform(a: 2, b: 0.3, c: -0.2, d: 3, tx: 0, ty: 0)] {
            let root = CALayer(), plane = CALayer()
            plane.anchorPoint = .zero
            plane.bounds = CGRect(x: 810, y: 180, width: 22, height: 22)
            plane.position = CGPoint(x: 20, y: 40); plane.setAffineTransform(transform)
            root.addSublayer(plane)
            let point = CGPoint(x: 818,y: 188)
            let before = plane.convert(point,to: root)
            CATransaction.begin(); CATransaction.setDisableActions(true)
            WorldMapLayerCoordinates.rebase(plane,to: CGRect(x: 814,y: 185,width: 13,height: 13))
            CATransaction.commit()
            check(samePoint(plane.convert(point,to: root),before),
                  "Changing a clipped layer's coordinate frame does not move its geographic content")
            check(plane.affineTransform() == transform && (plane.animationKeys() ?? []).isEmpty,
                  "Rebasing preserves the previous camera transform without adding a bounds-origin animation")
        }
        let animatedPlane = CALayer()
        animatedPlane.anchorPoint = .zero
        animatedPlane.bounds = CGRect(x: 0, y: 0, width: 40, height: 40)
        animatedPlane.position = CGPoint(x: 20, y: 20)
        animatedPlane.setAffineTransform(CGAffineTransform(scaleX: 2, y: 2))
        let cameraStart = WorldMapLayerCoordinates.capture(animatedPlane)
        CATransaction.begin(); CATransaction.setDisableActions(true)
        WorldMapLayerCoordinates.rebase(animatedPlane, to: CGRect(x: 10, y: 10, width: 40, height: 40))
        WorldMapLayerCoordinates.apply(animatedPlane, position: CGPoint(x: 60, y: 60),
            transform: CGAffineTransform(scaleX: 3, y: 3), from: cameraStart)
        CATransaction.commit()
        let motion = animatedPlane.animation(forKey: "position") as? CABasicAnimation
        let scaleMotion = animatedPlane.animation(forKey: "transform") as? CABasicAnimation
        check((motion?.fromValue as? NSValue)?.pointValue == CGPoint(x: 40, y: 40)
              && (motion?.toValue as? NSValue)?.pointValue == CGPoint(x: 60, y: 60),
              "Zoom explicitly starts at the rebased visible position rather than the stale presentation coordinate")
        check((scaleMotion?.fromValue as? NSValue)?.caTransform3DValue.m11 == 2
              && (scaleMotion?.toValue as? NSValue)?.caTransform3DValue.m11 == 3
              && motion?.duration == scaleMotion?.duration && animatedPlane.animation(forKey: "bounds") == nil,
              "The camera animates position and scale together without interpolating its new clipping bounds")
        // An interrupted camera presents an intermediate state, not the prior
        // model destination. The replacement must retain that visible start.
        let interrupted = WorldMapLayerCoordinates.Presentation(position: CGPoint(x: 48, y: 48),
            bounds: animatedPlane.bounds, anchorPoint: .zero, transform: CGAffineTransform(scaleX: 2.4, y: 2.4))
        CATransaction.begin(); CATransaction.setDisableActions(true)
        WorldMapLayerCoordinates.rebase(animatedPlane, to: CGRect(x: 15, y: 15, width: 30, height: 30))
        WorldMapLayerCoordinates.apply(animatedPlane, position: CGPoint(x: 90, y: 90),
            transform: CGAffineTransform(scaleX: 4, y: 4), from: interrupted)
        CATransaction.commit()
        let interruptedMotion = animatedPlane.animation(forKey: "position") as? CABasicAnimation
        let interruptedScale = animatedPlane.animation(forKey: "transform") as? CABasicAnimation
        check((interruptedMotion?.fromValue as? NSValue)?.pointValue == CGPoint(x: 60, y: 60)
              && (interruptedScale?.fromValue as? NSValue)?.caTransform3DValue.m11 == 2.4,
              "An interrupted zoom uses the presented transform and rebased position rather than the old endpoint")
        let pulse = CABasicAnimation(keyPath: "opacity"); pulse.duration = 1
        animatedPlane.add(pulse, forKey: "map.pulse")
        WorldMapLayerCoordinates.apply(animatedPlane, position: CGPoint(x: 75, y: 80), transform: .identity, from: nil)
        check(animatedPlane.position == CGPoint(x: 75, y: 80)
              && animatedPlane.animation(forKey: "position") == nil && animatedPlane.animation(forKey: "transform") == nil
              && animatedPlane.animation(forKey: "map.pulse") != nil,
              "Direct pointer input cancels pending camera easing immediately without removing unrelated pin animation")
        return count
    }
}
