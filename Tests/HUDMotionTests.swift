import AppKit
import QuartzCore

/// Geometry and layer-lifecycle checks do not open a window or use a frame timer.
enum HUDMotionTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func finite(_ t: CATransform3D) -> Bool {
            [t.m11, t.m12, t.m13, t.m14, t.m21, t.m22, t.m23, t.m24,
             t.m31, t.m32, t.m33, t.m34, t.m41, t.m42, t.m43, t.m44].allSatisfy { $0.isFinite }
        }
        check(HUDMotionMath.normalizedPoint(CGPoint(x: 100, y: -2)) == CGPoint(x: 1, y: -1), "Pointer coordinates clamp to the interaction region")
        check(HUDMotionMath.normalizedPoint(CGPoint(x: CGFloat.nan, y: CGFloat.infinity)) == .zero, "Nonfinite pointer input returns to center")
        let localPointer = HUDMotionMath.normalizedPointer(location: CGPoint(x: 650, y: 320),
                                                          center: HUDMotionMath.canvasCenter)
        check(localPointer == CGPoint(x: 150.0 / 320, y: 0), "A normal cursor move uses the HUD radius instead of display size")
        check(HUDMotionMath.normalizedPointer(location: CGPoint(x: -500, y: 2000), center: HUDMotionMath.canvasCenter)
              == CGPoint(x: -1, y: 1), "HUD-local pointer coordinates clamp beyond the active radius")
        check(HUDMotionMath.normalizedPointer(location: CGPoint(x: CGFloat.nan, y: CGFloat.infinity), center: .zero) == .zero,
              "Nonfinite pointer locations fail safely")
        check(HUDMotionMath.normalizedPointer(location: CGPoint(x: 150, y: 120), center: .zero,
                                              radius: CGSize(width: CGFloat.nan, height: 0)) == CGPoint(x: 150.0 / 320, y: 0.5),
              "Invalid interaction radii use the safe HUD defaults")
        let visibleForeStart = HUDMotionMath.project(.zero, through: HUDMotionMath.transform(normalizedPoint: .zero, depth: 48, travel: 24))
        let visibleForeEnd = HUDMotionMath.project(.zero, through: HUDMotionMath.transform(normalizedPoint: localPointer, depth: 48, travel: 24))
        check(visibleForeEnd.x - visibleForeStart.x >= 10 && visibleForeEnd.x - visibleForeStart.x <= 24,
              "A 150-design-point cursor move visibly shifts the foreground by at least ten points")
        let depthOnlyFore = HUDMotionMath.project(.zero, through: HUDMotionMath.transform(normalizedPoint: CGPoint(x: 1, y: 0), depth: 80, travel: 0))
        let depthOnlyRear = HUDMotionMath.project(.zero, through: HUDMotionMath.transform(normalizedPoint: CGPoint(x: 1, y: 0), depth: -80, travel: 0))
        check(depthOnlyFore.x > 8 && depthOnlyRear.x < -7,
              "A shared camera attitude separates depth planes even without artificial pointer travel")
        let pointer = CGPoint(x: 1, y: 0.75)
        let front = HUDMotionMath.transform(normalizedPoint: pointer, depth: 110, travel: 25)
        let rear = HUDMotionMath.transform(normalizedPoint: pointer, depth: -170, travel: -12)
        let frontCenter = HUDMotionMath.project(.zero, through: front)
        let rearCenter = HUDMotionMath.project(.zero, through: rear)
        check(frontCenter.x > 0 && frontCenter.y > 0, "Positive front travel follows the pointer")
        check(rearCenter.x < 0 && rearCenter.y < 0, "Negative rear travel moves in the opposite direction")
        check(front.m43 > 0 && rear.m43 < 0, "Front and rear retain separate depth offsets")
        let baseline = HUDMotionMath.transform(normalizedPoint: .zero, depth: 0, travel: 25)
        check(abs(baseline.m34) > 0 && !CATransform3DIsIdentity(baseline), "The neutral plane retains a global inclined perspective")
        check((0.02...0.11).contains(abs(baseline.m13)) && (0.02...0.11).contains(abs(baseline.m23))
              && (0.008...0.03).contains(abs(baseline.m12)), "The resting instrument has a mild pitch, yaw and roll before pointer movement")
        check(HUDMotionMath.project(.zero, through: baseline) == .zero, "Resting tilt keeps the anchor centered")
        check(CATransform3DIsIdentity(HUDMotionMath.transform(normalizedPoint: pointer, depth: 100, travel: 25, reducedMotion: true)), "Reduced Motion flattens and disables the pointer transform")
        check(HUDMotionMath.tiltDegrees(normalizedPoint: .zero, depth: 0) == .zero,
              "Neutral input has no fixed angle that could oppose a small cursor movement")
        // Use the actual row-vector matrix rather than assuming Euler-angle
        // signs. In flipped design coordinates, top is -Y and bottom is +Y.
        // Positive camera Z is nearer because m34 is negative.
        func cameraZ(_ p: CGPoint, _ t: CATransform3D) -> CGFloat {
            p.x * t.m13 + p.y * t.m23 + t.m43
        }
        func perspectiveW(_ p: CGPoint, _ t: CATransform3D) -> CGFloat {
            p.x * t.m14 + p.y * t.m24 + t.m44
        }
        let cursorSides: [(String, CGPoint, CGPoint)] = [
            ("right", CGPoint(x: 1, y: 0), CGPoint(x: 300, y: 0)),
            ("left", CGPoint(x: -1, y: 0), CGPoint(x: -300, y: 0)),
            ("bottom", CGPoint(x: 0, y: 1), CGPoint(x: 0, y: 180)),
            ("top", CGPoint(x: 0, y: -1), CGPoint(x: 0, y: -180))
        ]
        for depth: CGFloat in [-170, 0, 110] {
            let rest = HUDMotionMath.transform(normalizedPoint: .zero, depth: depth, travel: 0)
            for (side, pointer, edge) in cursorSides {
                let opposite = CGPoint(x: -edge.x, y: -edge.y)
                let tilted = HUDMotionMath.transform(normalizedPoint: pointer, depth: depth, travel: 0)
                check(cameraZ(edge, tilted) - cameraZ(opposite, tilted) > cameraZ(edge, rest) - cameraZ(opposite, rest),
                      "The cursor-side \(side) edge rises relative to the resting attitude on every plane")
                check(perspectiveW(edge, tilted) > 0 && perspectiveW(edge, tilted) - perspectiveW(opposite, tilted) < perspectiveW(edge, rest) - perspectiveW(opposite, rest),
                      "Perspective enlarges the nearer \(side) edge instead of pushing it away")
                let smallPointer = CGPoint(x: pointer.x * 0.1, y: pointer.y * 0.1)
                let smallTilt = HUDMotionMath.transform(normalizedPoint: smallPointer, depth: depth, travel: 0)
                let restDifference = cameraZ(edge, rest) - cameraZ(opposite, rest)
                let movedDifference = cameraZ(edge, smallTilt) - cameraZ(opposite, smallTilt)
                check(movedDifference > restDifference,
                      "A ten-percent move toward \(side) raises that side from the shared inclined pose")
                check(perspectiveW(edge, smallTilt) - perspectiveW(opposite, smallTilt) < perspectiveW(edge, rest) - perspectiveW(opposite, rest),
                      "Even a small \(side) offset enlarges its cursor-side edge under perspective")
            }
        }
        let values: [CGFloat] = [-1_000_000, -1, 0, 1, 1_000_000, .nan, .infinity, -.infinity]
        for x in values {
            for y in values {
                let transform = HUDMotionMath.transform(normalizedPoint: CGPoint(x: x, y: y), depth: x, travel: y)
                check(finite(transform), "Untrusted geometry never produces a nonfinite matrix")
                let center = HUDMotionMath.project(.zero, through: transform)
                check(center.x.isFinite && center.y.isFinite && abs(center.x) <= 310 && abs(center.y) <= 310,
                      "Even extreme finite inputs retain bounded projected travel")
                let corner = HUDMotionMath.project(CGPoint(x: 500, y: 320), through: transform)
                check(corner.x.isFinite && corner.y.isFinite && abs(corner.x) < 2000 && abs(corner.y) < 2000,
                      "Canvas-corner projection never reaches the perspective singularity")
                let tilt = HUDMotionMath.tiltDegrees(normalizedPoint: CGPoint(x: x, y: y), depth: x)
                check(tilt.x.isFinite && tilt.y.isFinite && abs(tilt.x) <= 10 && abs(tilt.y) <= 10,
                      "Depth response cannot exceed the bounded readable attitude")
            }
        }
        var invalid = CATransform3DIdentity
        invalid.m44 = 0
        check(HUDMotionMath.project(.zero, through: invalid) == .zero, "A singular external projection fails safely")
        check(HUDMotionMath.project(CGPoint(x: CGFloat.nan, y: 0), through: baseline) == .zero, "Invalid projection coordinates fail safely")

        let frontPlane = HUDDepthPlane(name: "front", depth: 80, travel: 24, lag: 0.24)
        let rearPlane = HUDDepthPlane(name: "rear", depth: -80, travel: -10, lag: 0.38)
        let rotor = CALayer()
        frontPlane.content.addSublayer(rotor)
        let motion = HUDMotionController(planes: [rearPlane, frontPlane])
        check(motion.registerAmbient(layer: rotor, key: "rotation", keyPath: "transform.rotation.z",
                                     fromValue: 0, toValue: .pi * 2, duration: 45, autoreverses: false),
              "A supported finite ambient configuration registers")
        check(!motion.registerAmbient(layer: rotor, key: "bad", keyPath: "transform.rotation.z",
                                      fromValue: .nan, toValue: 1, duration: 45),
              "Nonfinite ambient offsets are rejected")
        check(!motion.registerAmbient(layer: rotor, key: "bad", keyPath: "position",
                                      fromValue: 0, toValue: 1, duration: 45),
              "Unsupported animation paths cannot escape motion scope")
        let initialPointer = CGPoint(x: 0.6, y: -0.4)
        motion.start(reducedMotion: false, initialPoint: initialPointer)
        check(motion.ambientAnimationCount == 1 && motion.hasAmbientAnimations,
              "Starting adds exactly the registered ambient track")
        check(motion.targetNormalizedPoint == initialPointer && motion.parallaxAnimationCount == 0,
              "Starting seeds the current pointer immediately without finite pointer animations")
        for plane in motion.planes {
            check(CATransform3DEqualToTransform(plane.spatial.transform,
                  HUDMotionMath.transform(normalizedPoint: initialPointer, depth: plane.depth, travel: plane.travel)),
                  "Every initial plane follows the current pointer before the first mouse event")
        }
        let began = rotor.animation(forKey: "ambient.rotation")?.beginTime
        motion.start(reducedMotion: false, initialPoint: CGPoint(x: -1, y: 1))
        check(rotor.animation(forKey: "ambient.rotation")?.beginTime == began,
              "Repeated start keeps the ambient phase instead of restarting it")
        check(motion.targetNormalizedPoint == initialPointer && motion.parallaxAnimationCount == 0,
              "Repeated start is idempotent even if supplied a different initial pointer")
        _ = motion.registerAmbient(layer: rotor, key: "rotation", keyPath: "transform.rotation.z",
                                   fromValue: 0, toValue: .pi * 2, duration: 45, autoreverses: false)
        check(motion.ambientAnimationCount == 1 && rotor.animation(forKey: "ambient.rotation")?.beginTime == began,
              "Identical registration also preserves phase and track count")
        motion.setParallax(normalizedPoint: CGPoint(x: 1, y: -1), animated: true)
        check(motion.parallaxAnimationCount == 2 && motion.hasParallaxAnimations,
              "Pointer movement creates one finite track per plane")
        check(motion.targetNormalizedPoint == CGPoint(x: 1, y: -1), "The last pointer target is exposed")
        if let pointerAnimation = frontPlane.spatial.animation(forKey: "parallax.transform") as? CABasicAnimation,
           let timing = pointerAnimation.timingFunction {
            var firstControl: [Float] = [0, 0]
            var secondControl: [Float] = [0, 0]
            firstControl.withUnsafeMutableBufferPointer { timing.getControlPoint(at: 1, values: $0.baseAddress!) }
            secondControl.withUnsafeMutableBufferPointer { timing.getControlPoint(at: 2, values: $0.baseAddress!) }
            check(firstControl[0] > 0 && firstControl[1] / firstControl[0] > 1,
                  "Retargeted pointer animations start with positive velocity instead of starving under continuous input")
            check(firstControl[0] < secondControl[0] && secondControl[0] < 1
                  && firstControl[1] > 0 && firstControl[1] <= secondControl[1] && secondControl[1] <= 1,
                  "Pointer timing stays monotonic without overshoot")
            check(pointerAnimation.duration >= 0.06 && pointerAnimation.duration <= 0.12,
                  "Pointer follow-through completes within 120 ms instead of the old 240 ms profile")
            let rearDuration = rearPlane.spatial.animation(forKey: "parallax.transform")?.duration ?? 0
            check(pointerAnimation.duration < rearDuration && rearDuration <= 0.12,
                  "Front planes remain quicker than rear planes without a long input delay")
        } else { check(false, "Pointer movement installs a finite timed transform") }
        motion.setParallax(normalizedPoint: localPointer, animated: true)
        check(motion.parallaxAnimationCount == 2 && motion.targetNormalizedPoint == localPointer,
              "A later pointer event replaces existing tracks without accumulating animations")
        let deployment = CABasicAnimation(keyPath: "opacity")
        deployment.duration = 1
        frontPlane.deployment.add(deployment, forKey: "deployment.opacity")
        motion.stop(freezePresentation: true)
        check(motion.ambientAnimationCount == 0 && motion.parallaxAnimationCount == 0 && !motion.isRunning,
              "Stopping removes every scoped ambient and parallax track")
        check(frontPlane.deployment.animation(forKey: "deployment.opacity") != nil,
              "Stopping leaves deployment and closing animations alone")
        motion.setParallax(normalizedPoint: .zero, animated: true)
        check(motion.parallaxAnimationCount == 0, "Hidden motion cannot restart from a late mouse event")
        frontPlane.deployment.removeAnimation(forKey: "deployment.opacity")
        motion.start(reducedMotion: true, initialPoint: CGPoint(x: 1, y: -1))
        motion.setParallax(normalizedPoint: CGPoint(x: 1, y: 1))
        check(motion.ambientAnimationCount == 0 && motion.parallaxAnimationCount == 0,
              "Reduced Motion starts no ambient or pointer animations")
        check(CATransform3DIsIdentity(frontPlane.spatial.transform)
              && CATransform3DIsIdentity(rearPlane.spatial.transform),
              "Reduced Motion flattens both front and rear plane transforms")
        check(motion.targetNormalizedPoint == .zero,
              "Reduced Motion ignores the opening pointer and retains an effective zero target")
        motion.start(reducedMotion: false, initialPoint: CGPoint(x: -0.8, y: 0.5))
        check(motion.targetNormalizedPoint == CGPoint(x: -0.8, y: 0.5)
              && motion.ambientAnimationCount == 1 && motion.parallaxAnimationCount == 0,
              "Leaving Reduced Motion starts with a fresh pointer instead of a stale target")
        check(CATransform3DEqualToTransform(frontPlane.spatial.transform,
              HUDMotionMath.transform(normalizedPoint: CGPoint(x: -0.8, y: 0.5), depth: frontPlane.depth, travel: frontPlane.travel)),
              "An accessibility restart applies the fresh opening transform immediately")
        motion.setParallax(normalizedPoint: CGPoint(x: 1, y: 1))
        motion.start(reducedMotion: true, initialPoint: CGPoint(x: 1, y: 1))
        check(motion.targetNormalizedPoint == .zero && motion.ambientAnimationCount == 0
              && motion.parallaxAnimationCount == 0 && CATransform3DIsIdentity(frontPlane.spatial.transform),
              "Enabling Reduced Motion during pointer motion removes tracks and the seeded attitude")
        motion.stop(freezePresentation: false)
        check(motion.targetNormalizedPoint == .zero && !motion.hasAmbientAnimations && !motion.hasParallaxAnimations,
              "An ordinary stop restores a quiet centered baseline")
        for (supplied, expected) in [
            (CGPoint(x: 6, y: -4), CGPoint(x: 1, y: -1)),
            (CGPoint(x: CGFloat.nan, y: CGFloat.infinity), CGPoint.zero),
            (CGPoint(x: -0.25, y: 0.75), CGPoint(x: -0.25, y: 0.75))
        ] {
            motion.start(reducedMotion: false, initialPoint: supplied)
            check(motion.targetNormalizedPoint == expected && motion.parallaxAnimationCount == 0,
                  "Each reopening clamps and replaces the previous pointer without an animated correction")
            for plane in motion.planes {
                check(CATransform3DEqualToTransform(plane.spatial.transform,
                      HUDMotionMath.transform(normalizedPoint: expected, depth: plane.depth, travel: plane.travel)),
                      "Fresh openings apply bounded finite pointer input consistently to every plane")
            }
            motion.stop(freezePresentation: true)
            check(motion.ambientAnimationCount == 0 && motion.parallaxAnimationCount == 0,
                  "Closing a seeded opening removes all owned motion tracks")
        }
        motion.start(reducedMotion: false)
        check(motion.targetNormalizedPoint == .zero && motion.parallaxAnimationCount == 0,
              "The default start remains compatible with callers that request a centered opening")
        motion.stop(freezePresentation: false)

        // Pointer-only mode belongs to the finite deployment/retraction phase.
        // An explicit transaction keeps detached fixture tracks inspectable.
        CATransaction.begin(); CATransaction.setDisableActions(true)
        let deployingPlane = HUDDepthPlane(name: "deploying", depth: 54, travel: 24, lag: 0.22)
        let deployingRotor = CALayer(); deployingPlane.content.addSublayer(deployingRotor)
        let deployingMotion = HUDMotionController(planes: [deployingPlane])
        _ = deployingMotion.registerAmbient(layer: deployingRotor, key: "rotation", keyPath: "transform.rotation.z",
            fromValue: 0, toValue: .pi * 2, duration: 45, autoreverses: false)
        check(!deployingMotion.isRunning && !deployingMotion.isPointerFollowing,
              "A hidden motion controller accepts no pointer or ambient work")
        deployingMotion.startPointerFollowing(reducedMotion: false, initialPoint: initialPointer)
        check(deployingMotion.isPointerFollowing && !deployingMotion.isRunning
              && deployingMotion.targetNormalizedPoint == initialPointer
              && deployingMotion.ambientAnimationCount == 0 && deployingMotion.parallaxAnimationCount == 0,
              "Deployment seeds its current pointer without installing any infinite or corrective tracks")
        let deploymentPointer = CGPoint(x: -0.7, y: 0.8)
        deployingMotion.setParallax(normalizedPoint: deploymentPointer)
        check(deployingMotion.targetNormalizedPoint == deploymentPointer
              && deployingMotion.parallaxAnimationCount == 1 && !deployingMotion.hasAmbientAnimations,
              "Pointer events follow during deployment while ambient animation remains stopped")
        let inFlightPointer = deployingPlane.spatial.animation(forKey: "parallax.transform")!.copy() as! CAAnimation
        inFlightPointer.timeOffset = 0.037
        deployingPlane.spatial.add(inFlightPointer, forKey: "parallax.transform")
        let inFlightPose = deployingPlane.spatial.transform
        deployingMotion.startPointerFollowing(reducedMotion: false, initialPoint: .zero)
        check(deployingMotion.targetNormalizedPoint == deploymentPointer
              && deployingPlane.spatial.animation(forKey: "parallax.transform")?.timeOffset == 0.037
              && CATransform3DEqualToTransform(deployingPlane.spatial.transform, inFlightPose),
              "Repeated pointer-only start preserves the current target and finite follow-through phase")
        _ = deployingMotion.registerAmbient(layer: deployingRotor, key: "glow", keyPath: "opacity",
            fromValue: -0.1, toValue: 0, duration: 8)
        deployingMotion.configure(parallax: 1, perspective: 1, ambient: false)
        deployingMotion.configure(parallax: 1, perspective: 1, ambient: true)
        check(!deployingMotion.hasAmbientAnimations && deployingMotion.parallaxAnimationCount == 1,
              "Registering or enabling ambient during deployment cannot join its finite transaction")
        deployingMotion.start(reducedMotion: false, initialPoint: .zero)
        check(deployingMotion.isRunning && deployingMotion.isPointerFollowing
              && deployingMotion.ambientAnimationCount == 2 && deployingMotion.targetNormalizedPoint == deploymentPointer,
              "Completing deployment promotes the current pointer session and starts only the registered ambient tracks")
        check(deployingPlane.spatial.animation(forKey: "parallax.transform")?.timeOffset == 0.037
              && CATransform3DEqualToTransform(deployingPlane.spatial.transform, inFlightPose),
              "Promotion preserves the exact pointer pose and animation phase instead of snapping to a fresh seed")
        let exitTrack = CABasicAnimation(keyPath: "opacity"); exitTrack.duration = 0.4
        deployingPlane.deployment.add(exitTrack, forKey: "deployment.exit")
        deployingMotion.startPointerFollowing(reducedMotion: false, initialPoint: .zero)
        check(!deployingMotion.isRunning && deployingMotion.isPointerFollowing
              && !deployingMotion.hasAmbientAnimations && deployingMotion.parallaxAnimationCount == 1
              && deployingPlane.spatial.animation(forKey: "parallax.transform")?.timeOffset == 0.037
              && deployingPlane.deployment.animation(forKey: "deployment.exit") != nil,
              "Retraction stops ambient motion while preserving both pointer follow-through and independent exit tracks")
        deployingMotion.configure(parallax: 0.5, perspective: 1, ambient: true)
        check(deployingMotion.parallaxAnimationCount == 1 && !deployingMotion.hasAmbientAnimations
              && CATransform3DEqualToTransform(deployingPlane.spatial.transform,
                  HUDMotionMath.transform(normalizedPoint: deploymentPointer, depth: 54, travel: 24, parallaxIntensity: 0.5)),
              "Pointer-only mode applies live parallax settings without enabling ambient work")
        deployingMotion.setParallax(normalizedPoint: CGPoint(x: 0.8, y: -0.6))
        check(deployingMotion.targetNormalizedPoint == CGPoint(x: 0.8, y: -0.6)
              && deployingMotion.parallaxAnimationCount == 1 && !deployingMotion.hasAmbientAnimations,
              "The pointer remains responsive throughout retraction")
        deployingMotion.stop(freezePresentation: true)
        check(!deployingMotion.isRunning && !deployingMotion.isPointerFollowing
              && deployingMotion.parallaxAnimationCount == 0 && deployingMotion.ambientAnimationCount == 0
              && deployingPlane.deployment.animation(forKey: "deployment.exit") != nil,
              "Final close removes all owned motion while leaving the host's finite exit track alone")
        deployingMotion.setParallax(normalizedPoint: .zero)
        check(deployingMotion.parallaxAnimationCount == 0,
              "Late mouse events after pointer-only shutdown cannot restart hidden animation")
        deployingMotion.startPointerFollowing(reducedMotion: true, initialPoint: deploymentPointer)
        deployingMotion.setParallax(normalizedPoint: initialPointer)
        check(deployingMotion.targetNormalizedPoint == .zero && !deployingMotion.hasParallaxAnimations
              && !deployingMotion.hasAmbientAnimations && CATransform3DIsIdentity(deployingPlane.spatial.transform),
              "Reduce Motion also suppresses pointer and ambient tracks during deployment")
        deployingMotion.start(reducedMotion: true, initialPoint: deploymentPointer)
        check(deployingMotion.isRunning && deployingMotion.isPointerFollowing && !deployingMotion.hasAmbientAnimations,
              "Reduced-motion deployment promotes cleanly without starting repeating animation")
        deployingMotion.stop(freezePresentation: false)
        check(!deployingMotion.isPointerFollowing && !deployingMotion.isRunning
              && deployingMotion.targetNormalizedPoint == .zero
              && !deployingMotion.hasAmbientAnimations && !deployingMotion.hasParallaxAnimations,
              "A full stop clears both independent lifecycle states and the pointer target")
        deployingPlane.deployment.removeAllAnimations()
        CATransaction.commit()

        // Settings change the existing depth planes without disabling pointer
        // interaction or introducing an unbounded perspective at either limit.
        let settingsPointer = CGPoint(x: 0.85, y: -0.7)
        let noParallax = HUDMotionMath.transform(normalizedPoint: settingsPointer, depth: 80, travel: 24,
                                                parallaxIntensity: 0, perspectiveIntensity: 1)
        let restingWithoutParallax = HUDMotionMath.transform(normalizedPoint: .zero, depth: 80, travel: 24,
                                                            parallaxIntensity: 0, perspectiveIntensity: 1)
        check(CATransform3DEqualToTransform(noParallax, restingWithoutParallax),
              "Zero parallax removes all cursor response while retaining the resting perspective")
        check(!CATransform3DIsIdentity(noParallax), "Zero parallax does not accidentally flatten the depth shell")
        let flat = HUDMotionMath.transform(normalizedPoint: settingsPointer, depth: 80, travel: 24,
                                           parallaxIntensity: 2, perspectiveIntensity: 0)
        check(flat.m11 == 1 && flat.m22 == 1 && flat.m12 == 0 && flat.m13 == 0 && flat.m23 == 0 && flat.m34 == 0,
              "Zero perspective removes pitch, yaw, roll and perspective distortion")
        let flatCenter = HUDMotionMath.project(.zero, through: flat)
        check(abs(flatCenter.x - settingsPointer.x * 48) < 0.0001
              && abs(flatCenter.y - settingsPointer.y * 48) < 0.0001,
              "Zero perspective independently preserves the selected lateral parallax range")
        check(CATransform3DIsIdentity(HUDMotionMath.transform(normalizedPoint: settingsPointer, depth: 0, travel: 24,
                                     parallaxIntensity: 0, perspectiveIntensity: 0)),
              "Both zero intensity settings produce a flat stationary center")
        let maximumIntensity = HUDMotionMath.transform(normalizedPoint: settingsPointer, depth: 80, travel: 24,
                                                       parallaxIntensity: 2, perspectiveIntensity: 2)
        check(CATransform3DEqualToTransform(maximumIntensity,
              HUDMotionMath.transform(normalizedPoint: settingsPointer, depth: 80, travel: 24,
                                      parallaxIntensity: 1_000, perspectiveIntensity: 1_000)),
              "Intensity values above the Settings maximum clamp to the two-times range")
        check(CATransform3DEqualToTransform(HUDMotionMath.transform(normalizedPoint: settingsPointer, depth: 80, travel: 24,
                                                               parallaxIntensity: -10, perspectiveIntensity: -10),
              HUDMotionMath.transform(normalizedPoint: settingsPointer, depth: 80, travel: 24,
                                      parallaxIntensity: 0, perspectiveIntensity: 0)),
              "Negative intensity settings clamp to their zero limits")
        for invalidIntensity: CGFloat in [.nan, .infinity, -.infinity] {
            check(CATransform3DEqualToTransform(HUDMotionMath.transform(normalizedPoint: settingsPointer, depth: 80, travel: 24,
                                                                     parallaxIntensity: invalidIntensity, perspectiveIntensity: invalidIntensity),
                  HUDMotionMath.transform(normalizedPoint: settingsPointer, depth: 80, travel: 24)),
                  "Nonfinite intensity settings fall back to the normal readable presentation")
        }
        check(CATransform3DIsIdentity(HUDMotionMath.transform(normalizedPoint: settingsPointer, depth: 86, travel: 32,
                                     reducedMotion: true, parallaxIntensity: 2, perspectiveIntensity: 2)),
              "Reduce Motion takes precedence over maximum parallax and perspective settings")

        let durationInputs: [TimeInterval] = [-1, 0, 0.18, 0.24, 0.40, 10, .nan, .infinity, -.infinity]
        for value in durationInputs {
            let duration = HUDMotionMath.pointerResponseDuration(for: value)
            check(duration.isFinite && (0.06...0.12).contains(duration),
                  "Invalid or extreme response profiles cannot create instant jumps or long input stalls")
        }
        let profiles: [(CGFloat, CGFloat, TimeInterval)] = [
            (-95, -12, 0.40), (-52, -10, 0.36), (-24, 3, 0.32), (0, 8, 0.28),
            (26, 16, 0.25), (36, 16, 0.27), (54, 24, 0.22), (68, 28, 0.20), (86, 32, 0.18),
            (54, 24, 0.22)
        ]
        let fullPlanes = profiles.enumerated().map {
            HUDDepthPlane(name: "responsive.\($0.offset)", depth: $0.element.0,
                          travel: $0.element.1, lag: $0.element.2)
        }
        let fullMotion = HUDMotionController(planes: fullPlanes)
        let persistentRotor = CALayer()
        fullPlanes[0].content.addSublayer(persistentRotor)
        _ = fullMotion.registerAmbient(layer: persistentRotor, key: "rotation", keyPath: "transform.rotation.z",
                                       fromValue: 0, toValue: .pi * 2, duration: 45, autoreverses: false)
        fullMotion.start(reducedMotion: false, initialPoint: initialPointer)
        check(fullMotion.targetNormalizedPoint == initialPointer && fullMotion.parallaxAnimationCount == 0,
              "The complete HUD begins at its opening pointer without a neutral-frame correction")
        for plane in fullPlanes {
            check(CATransform3DEqualToTransform(plane.spatial.transform,
                  HUDMotionMath.transform(normalizedPoint: initialPointer, depth: plane.depth, travel: plane.travel)),
                  "All HUD planes receive the opening attitude, including foreground rails")
        }
        let originalAmbientStart = persistentRotor.animation(forKey: "ambient.rotation")?.beginTime
        for index in 0..<24 {
            // Alternating directions exercise rapid reversals before old tracks
            // have finished; each plane still has only its newest target.
            let target = CGPoint(x: index % 2 == 0 ? 0.9 : -0.8, y: CGFloat(index % 3 - 1) * 0.7)
            fullMotion.setParallax(normalizedPoint: target)
            check(fullMotion.parallaxAnimationCount == profiles.count && fullMotion.targetNormalizedPoint == target,
                  "Rapid pointer reversals keep one track per plane")
            check(CATransform3DEqualToTransform(fullPlanes[6].spatial.transform, fullPlanes[9].spatial.transform)
                  && fullPlanes[6].pointerResponseDuration == fullPlanes[9].pointerResponseDuration,
                  "Foreground rails exactly share the central content's stronger motion and timing during pointer reversals")
            check(persistentRotor.animation(forKey: "ambient.rotation")?.beginTime == originalAmbientStart,
                  "Faster pointer response never restarts the independent ambient rotation")
        }
        for plane in fullPlanes {
            check(CATransform3DEqualToTransform(plane.spatial.transform,
                  HUDMotionMath.transform(normalizedPoint: fullMotion.targetNormalizedPoint, depth: plane.depth, travel: plane.travel)),
                  "Response tuning preserves each plane's full depth, tilt and signed travel")
            let keys = plane.spatial.animationKeys() ?? []
            check(keys == ["parallax.transform"] && (plane.spatial.animation(forKey: "parallax.transform")?.duration ?? 1) <= 0.12,
                  "Batched pointer updates add no implicit transform animations")
        }
        fullMotion.stop(freezePresentation: false)
        check(fullMotion.parallaxAnimationCount == 0 && fullMotion.ambientAnimationCount == 0,
              "Every faster pointer track is still removed when the overlay stops")
        // These controller settings are local to the fixture. Restore ordinary
        // defaults afterward so no changed profile leaks into later checks.
        fullMotion.configure(parallax: 1, perspective: 1, ambient: true)
        fullMotion.start(reducedMotion: false, initialPoint: initialPointer)
        check(fullMotion.hasAmbientAnimations, "Ambient starts normally before changing its Settings toggle")
        fullMotion.configure(parallax: 1, perspective: 1, ambient: false)
        check(fullMotion.isRunning && !fullMotion.hasAmbientAnimations,
              "Turning ambient animation off removes repeating tracks without stopping the motion controller")
        fullMotion.setParallax(normalizedPoint: settingsPointer)
        check(fullMotion.hasParallaxAnimations && fullMotion.targetNormalizedPoint == settingsPointer,
              "Pointer motion remains responsive when persistent ambient animation is disabled")
        for plane in fullPlanes {
            check(CATransform3DEqualToTransform(plane.spatial.transform,
                  HUDMotionMath.transform(normalizedPoint: settingsPointer, depth: plane.depth, travel: plane.travel)),
                  "Ambient-off pointer events still apply the normal range to every HUD plane")
        }
        fullMotion.configure(parallax: 0, perspective: 1, ambient: false)
        let stationaryFront = fullPlanes[6].spatial.transform
        fullMotion.setParallax(normalizedPoint: CGPoint(x: -1, y: 1))
        check(CATransform3DEqualToTransform(stationaryFront, fullPlanes[6].spatial.transform),
              "Changing parallax to zero immediately stops cursor movement on the existing planes")
        fullMotion.configure(parallax: 2, perspective: 2, ambient: false)
        for cornerPointer in [CGPoint(x: -1, y: -1), CGPoint(x: 1, y: -1), CGPoint(x: -1, y: 1), CGPoint(x: 1, y: 1)] {
            fullMotion.setParallax(normalizedPoint: cornerPointer, animated: false)
            for plane in fullPlanes {
                let transform = plane.spatial.transform
                check(finite(transform), "The full HUD's maximum-intensity transforms remain finite at pointer extremes")
                for corner in [CGPoint(x: -500, y: -320), CGPoint(x: 500, y: -320), CGPoint(x: -500, y: 320), CGPoint(x: 500, y: 320)] {
                    let projected = HUDMotionMath.project(corner, through: transform)
                    let divisor = transform.m14 * corner.x + transform.m24 * corner.y + transform.m44
                    check(divisor > 0.1 && projected.x.isFinite && projected.y.isFinite
                          && abs(projected.x) < 5_000 && abs(projected.y) < 5_000,
                          "Maximum Settings intensity keeps real HUD-plane corners bounded and in front of the perspective horizon")
                }
            }
        }
        fullMotion.configure(parallax: 2, perspective: 0, ambient: false)
        check(fullPlanes.allSatisfy { $0.spatial.transform.m34 == 0 && $0.spatial.transform.m13 == 0 && $0.spatial.transform.m23 == 0 },
              "A live perspective change flattens every retained plane without recreating the HUD")
        fullMotion.configure(parallax: 1, perspective: 1, ambient: true)
        check(fullMotion.ambientAnimationCount == 1,
              "Re-enabling ambient restores exactly the registered track rather than duplicating it")
        fullMotion.configure(parallax: 1, perspective: 1, ambient: true)
        check(fullMotion.ambientAnimationCount == 1, "Repeated Settings refreshes do not accumulate ambient tracks")
        fullMotion.stop(freezePresentation: false)
        fullMotion.configure(parallax: 1, perspective: 1, ambient: false)
        fullMotion.start(reducedMotion: false, initialPoint: settingsPointer)
        check(!fullMotion.hasAmbientAnimations && fullMotion.targetNormalizedPoint == settingsPointer,
              "Reopening with ambient disabled still seeds the current pointer without starting continuous animations")
        fullMotion.stop(freezePresentation: false)
        fullMotion.configure(parallax: 1, perspective: 1, ambient: true)

        let notePlane = HUDDepthPlane(name: "notes.settings", depth: 36, travel: 16, lag: 0.27)
        let sidePlane = HUDDepthPlane(name: "panels.settings", depth: 36, travel: 16, lag: 0.27)
        let noteMotion = HUDMotionController(planes: [sidePlane, notePlane])
        noteMotion.configure(parallax: 1, perspective: 1, ambient: false)
        noteMotion.start(reducedMotion: false, initialPoint: initialPointer)
        let ordinaryNoteBounds = CGRect(x: -1470 / 2 / 1.15, y: -956 / 2 / 1.15 + 30,
                                       width: 1470 / 1.15, height: 956 / 1.15)
        noteMotion.setProjectionBounds(ordinaryNoteBounds, for: notePlane)
        check(CATransform3DEqualToTransform(notePlane.spatial.transform, sidePlane.spatial.transform),
              "At ordinary HUD scale, full-screen notes still exactly share the side-button transform")
        noteMotion.setParallax(normalizedPoint: settingsPointer)
        noteMotion.setProjectionBounds(CGRect(x: -3200, y: -2070, width: 6400, height: 4200), for: notePlane)
        check(sidePlane.spatial.animation(forKey: "parallax.transform") != nil,
              "Updating note viewport safety does not interrupt other planes' pointer animation")
        for hudScale: CGFloat in [0.2, 1, 2] {
            for viewport in [CGSize(width: 1470, height: 956), CGSize(width: 2560, height: 1440)] {
                let scale = min(viewport.width / 1100, viewport.height / 740, 1.15) * hudScale
                let noteBounds = CGRect(x: -viewport.width / 2 / scale, y: -viewport.height / 2 / scale + 30,
                                        width: viewport.width / scale, height: viewport.height / scale)
                noteMotion.setProjectionBounds(noteBounds, for: notePlane)
                noteMotion.configure(parallax: 2, perspective: 2, ambient: false)
                for p in [CGPoint(x: -1, y: -1), CGPoint(x: 1, y: -1), CGPoint(x: -1, y: 1), CGPoint(x: 1, y: 1)] {
                    noteMotion.setParallax(normalizedPoint: p, animated: false)
                    let t = notePlane.spatial.transform
                    let side = sidePlane.spatial.transform
                    check(t.m13 == side.m13 && t.m23 == side.m23 && t.m41 == side.m41 && t.m42 == side.m42
                          && notePlane.pointerResponseDuration == sidePlane.pointerResponseDuration,
                          "Note lens safety preserves side-button angles, travel and timing even at minimum HUD scale")
                    for x in [noteBounds.minX, noteBounds.maxX] {
                        for y in [noteBounds.minY, noteBounds.maxY] {
                            let w = t.m14 * x + t.m24 * y + t.m44
                            let projected = HUDMotionMath.project(CGPoint(x: x, y: y), through: t)
                            check(finite(t) && w >= 0.299_999 && projected.x.isFinite && projected.y.isFinite
                                  && abs(projected.x * scale) < viewport.width * 8
                                  && abs(projected.y * scale) < viewport.height * 8,
                                  "Full-screen note corners remain finite and in front of the camera across supported UI scales")
                        }
                    }
                }
            }
        }
        noteMotion.setProjectionBounds(CGRect(x: CGFloat.nan, y: 0, width: 10, height: 10), for: notePlane)
        check(notePlane.projectionBounds == HUDMotionMath.defaultProjectionBounds,
              "An invalid surface boundary safely restores the shell's standard projection coverage")
        noteMotion.stop(freezePresentation: false)
        noteMotion.configure(parallax: 1, perspective: 1, ambient: true)
        return count
    }
}
