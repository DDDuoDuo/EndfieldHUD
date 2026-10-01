import AppKit
import QuartzCore

/// Geometry shared by the separate mechanical depth planes. Layer coordinates
/// are centered at the plane's anchor before projection.
enum HUDMotionMath {
    static let canvasSize = CGSize(width: 1000, height: 640)
    static let canvasCenter = CGPoint(x: 500, y: 320)
    static let perspectiveDistance: CGFloat = 950
    static let defaultProjectionBounds = CGRect(x: -500, y: -320, width: 1000, height: 640)
    static let restingPitch: CGFloat = 5
    static let restingYaw: CGFloat = -4
    static let restingRoll: CGFloat = -1

    /// Use design-space coordinates so a nearby cursor movement has the same
    /// response on a laptop display and a large external monitor.
    static func normalizedPointer(location: CGPoint, center: CGPoint,
                                  radius: CGSize = CGSize(width: 320, height: 240)) -> CGPoint {
        func component(_ location: CGFloat, _ center: CGFloat, _ radius: CGFloat, fallback: CGFloat) -> CGFloat {
            guard location.isFinite, center.isFinite else { return 0 }
            let radius = radius.isFinite && radius > 0 ? max(1, radius) : fallback
            return finiteClamp((location - center) / radius, lower: -1, upper: 1, fallback: 0)
        }
        return CGPoint(x: component(location.x, center.x, radius.width, fallback: 320),
                       y: component(location.y, center.y, radius.height, fallback: 240))
    }

    static func normalizedPoint(_ point: CGPoint) -> CGPoint {
        CGPoint(x: finiteClamp(point.x, lower: -1, upper: 1, fallback: 0),
                y: finiteClamp(point.y, lower: -1, upper: 1, fallback: 0))
    }

    /// Keep the original near/far follow-through profile, but reach a pointer
    /// target within 60–120 ms. The longer nominal profile is not input latency.
    static func pointerResponseDuration(for nominalLag: TimeInterval) -> TimeInterval {
        let lag = nominalLag.isFinite ? nominalLag : 0.28
        return min(0.12, max(0.06, lag * 0.30))
    }

    static func finiteClamp(_ value: CGFloat, lower: CGFloat, upper: CGFloat, fallback: CGFloat) -> CGFloat {
        value.isFinite ? min(upper, max(lower, value)) : fallback
    }

    static func tiltDegrees(normalizedPoint point: CGPoint, depth: CGFloat) -> CGPoint {
        let point = normalizedPoint(point)
        let depth = finiteClamp(depth, lower: -300, upper: 300, fallback: 0)
        let response = finiteClamp(1 + depth / 500, lower: 0.6, upper: 1.3, fallback: 1)
        // Core Animation uses row vectors: positive pitch raises +Y, while
        // negative yaw raises +X. Design coordinates are flipped (+Y is down),
        // so the lower/right edge should approach the viewer for +Y/+X input.
        // Neutral input has no angular bias: even a small cursor offset must
        // raise that side. Modest angles keep text and side buttons readable;
        // depth offsets, perspective and deployment retain the spatial shell.
        return CGPoint(x: finiteClamp(point.y * 6 * response, lower: -10, upper: 10, fallback: 0),
                       y: finiteClamp(-point.x * 7 * response, lower: -10, upper: 10, fallback: 0))
    }

    static func transform(normalizedPoint point: CGPoint, depth: CGFloat, travel: CGFloat,
                          reducedMotion: Bool = false, parallaxIntensity: CGFloat = 1,
                          perspectiveIntensity: CGFloat = 1,
                          projectionBounds: CGRect = defaultProjectionBounds) -> CATransform3D {
        guard !reducedMotion else { return CATransform3DIdentity }
        let point = normalizedPoint(point)
        let depth = finiteClamp(depth, lower: -300, upper: 300, fallback: 0)
        // Travel is signed: a negative rear-plane value moves against the pointer.
        let travel = finiteClamp(travel, lower: -120, upper: 120, fallback: 0)
        let parallax = finiteClamp(parallaxIntensity, lower: 0, upper: 2, fallback: 1)
        let perspective = finiteClamp(perspectiveIntensity, lower: 0, upper: 2, fallback: 1)
        let tilt = tiltDegrees(normalizedPoint: point, depth: depth)
        let radians = CGFloat.pi / 180
        // One shared resting attitude keeps the complete instrument inclined
        // even at neutral input. Cursor response remains relative to that pose.
        let tiltX = (restingPitch + tilt.x * parallax) * radians * perspective
        let tiltY = (restingYaw + tilt.y * parallax) * radians * perspective
        // Surface orientation raises the cursor-side edge; camera parallax
        // independently keeps nearer centers following the pointer and farther
        // centers moving against it. Use the opposite camera attitude for
        // this depth offset so surface tilt does not reverse the established
        // near/far travel on either screen axis.
        // Row-vector (0, 0, z) * Ry * Rx gives
        // (z*sinY, -z*cosY*sinX, z*cosY*cosX) before perspective projection.
        let cameraTilt = tiltDegrees(normalizedPoint: CGPoint(x: -point.x, y: -point.y), depth: 0)
        let cameraX = cameraTilt.x * radians * parallax * perspective
        let cameraY = cameraTilt.y * radians * parallax * perspective
        let depthX = depth * sin(cameraY)
        let depthY = -depth * cos(cameraY) * sin(cameraX)
        let depthZ = depth * cos(cameraY) * cos(cameraX)
        var transform = CATransform3DIdentity
        transform = CATransform3DTranslate(transform, point.x * travel * parallax + depthX,
                                          point.y * travel * parallax + depthY, depthZ)
        transform = CATransform3DRotate(transform, tiltX, 1, 0, 0)
        transform = CATransform3DRotate(transform, tiltY, 0, 1, 0)
        transform = CATransform3DRotate(transform, restingRoll * radians * perspective, 0, 0, 1)
        // Keep the complete design plane in front of the camera at the maximum
        // combination of tilt and depth. Only the lens is limited; the requested
        // pointer travel and angular response remain intact. A positive near
        // margin also keeps inverse hit-testing well-conditioned.
        let covered = safeProjectionBounds(projectionBounds)
        let nearestZ = max(transform.m13 * covered.minX, transform.m13 * covered.maxX)
            + max(transform.m23 * covered.minY, transform.m23 * covered.maxY) + transform.m43
        let requestedLens = perspective / perspectiveDistance
        let lens = nearestZ > 0 ? min(requestedLens, 0.7 / nearestZ) : requestedLens
        var projection = CATransform3DIdentity
        projection.m34 = -lens
        return CATransform3DConcat(transform, projection)
    }

    /// Full-screen notes can cover more design space than the shell when the
    /// HUD is scaled down. Keep that real surface in front of the same camera.
    static func safeProjectionBounds(_ bounds: CGRect) -> CGRect {
        guard bounds.width > 0, bounds.height > 0,
              [bounds.minX, bounds.minY, bounds.maxX, bounds.maxY].allSatisfy({ $0.isFinite }) else {
            return defaultProjectionBounds
        }
        return defaultProjectionBounds.union(bounds)
    }

    /// Homogeneous projection for checking the bounded perspective geometry.
    static func project(_ point: CGPoint, through transform: CATransform3D) -> CGPoint {
        guard point.x.isFinite, point.y.isFinite else { return .zero }
        let divisor = transform.m14 * point.x + transform.m24 * point.y + transform.m44
        guard divisor.isFinite, abs(divisor) > 0.0001 else { return .zero }
        let x = (transform.m11 * point.x + transform.m21 * point.y + transform.m41) / divisor
        let y = (transform.m12 * point.x + transform.m22 * point.y + transform.m42) / divisor
        guard x.isFinite, y.isFinite else { return .zero }
        return CGPoint(x: x, y: y)
    }
}

/// Deployment animations belong on `deployment`; pointer motion belongs on
/// `spatial`; artwork and its independent ambient animations belong in `content`.
final class HUDDepthPlane {
    let name: String
    let depth: CGFloat
    let travel: CGFloat
    let lag: TimeInterval
    fileprivate(set) var projectionBounds = HUDMotionMath.defaultProjectionBounds
    var pointerResponseDuration: TimeInterval { HUDMotionMath.pointerResponseDuration(for: lag) }
    let deployment = CALayer()
    let spatial = CATransformLayer()
    let content = CALayer()

    init(name: String, depth: CGFloat, travel: CGFloat, lag: TimeInterval) {
        self.name = name
        self.depth = HUDMotionMath.finiteClamp(depth, lower: -300, upper: 300, fallback: 0)
        self.travel = HUDMotionMath.finiteClamp(travel, lower: -120, upper: 120, fallback: 0)
        self.lag = lag.isFinite ? min(0.40, max(0.18, lag)) : 0.28
        for layer in [deployment, spatial, content] {
            layer.bounds = CGRect(origin: .zero, size: HUDMotionMath.canvasSize)
            layer.position = HUDMotionMath.canvasCenter
            layer.anchorPoint = CGPoint(x: 0.5, y: 0.5)
            layer.masksToBounds = false
            layer.isDoubleSided = true
        }
        deployment.name = name + ".deployment"
        spatial.name = name + ".spatial"
        content.name = name + ".content"
        deployment.addSublayer(spatial)
        spatial.addSublayer(content)
        spatial.transform = HUDMotionMath.transform(normalizedPoint: .zero, depth: self.depth, travel: self.travel)
    }
}

/// No timers or frame callbacks. Ambient tracks run in Core Animation; pointer
/// events retarget short finite transforms from their current presentation pose.
final class HUDMotionController {
    let planes: [HUDDepthPlane]
    /// Full visible mode permits ambient tracks; pointer following can also be
    /// active independently during the finite opening and closing transitions.
    private(set) var isRunning = false
    private(set) var isPointerFollowing = false
    private(set) var targetNormalizedPoint = CGPoint.zero
    private var reducedMotion = false
    private var parallaxIntensity: CGFloat = 1
    private var perspectiveIntensity: CGFloat = 1
    private var ambientEnabled = true
    private var ambientEpoch: TimeInterval?
    private var ambientTracks: [AmbientTrack] = []
    private var baselinePoses: [ObjectIdentifier: BaselinePose] = [:]
    private let pointerTiming = CAMediaTimingFunction(controlPoints: 0.16, 0.75, 0.30, 1)

    var ambientAnimationCount: Int { countAnimations(prefix: "ambient.") }
    var parallaxAnimationCount: Int { countAnimations(prefix: "parallax.") }
    var hasAmbientAnimations: Bool { ambientAnimationCount > 0 }
    var hasParallaxAnimations: Bool { parallaxAnimationCount > 0 }

    init(planes: [HUDDepthPlane]) { self.planes = planes }

    /// Update only the affected surface when the viewport or HUD scale changes;
    /// do not interrupt the other planes' current pointer follow-through.
    func setProjectionBounds(_ bounds: CGRect, for plane: HUDDepthPlane) {
        precondition(Thread.isMainThread)
        guard planes.contains(where: { $0 === plane }) else { return }
        let next = HUDMotionMath.safeProjectionBounds(bounds)
        guard plane.projectionBounds != next else { return }
        plane.projectionBounds = next
        retargetPlanes(to: targetNormalizedPoint, animated: false, targets: [plane])
    }

    func configure(parallax: CGFloat, perspective: CGFloat, ambient: Bool) {
        let changed = self.parallaxIntensity != parallax || self.perspectiveIntensity != perspective
        parallaxIntensity = parallax; perspectiveIntensity = perspective
        if changed { retargetPlanes(to: targetNormalizedPoint, animated: isPointerFollowing && !reducedMotion) }
        guard ambientEnabled != ambient else { return }
        ambientEnabled = ambient
        if ambient && isRunning && !reducedMotion {
            ambientEpoch = CACurrentMediaTime()
            for track in ambientTracks { install(track) }
        }
        else {
            for track in ambientTracks { track.layer?.removeAnimation(forKey: track.key) }
            restoreBaselinePoses()
        }
    }

    deinit {
        for layer in allManagedLayers() {
            for key in layer.animationKeys() ?? [] where key.hasPrefix("ambient.") || key.hasPrefix("parallax.") {
                layer.removeAnimation(forKey: key)
            }
        }
    }

    /// Values are additive offsets from the artwork's static pose. Rotation is
    /// in radians; translation is in points; opacity offsets are clamped to ±1.
    /// A negative beginOffset starts a track partway through its independent cycle.
    @discardableResult
    func registerAmbient(layer: CALayer, key: String, keyPath: String,
                         fromValue: CGFloat, toValue: CGFloat, duration: TimeInterval,
                         autoreverses: Bool = true, beginOffset: TimeInterval = 0,
                         timingFunction: CAMediaTimingFunctionName? = nil) -> Bool {
        precondition(Thread.isMainThread)
        let supported = ["transform.rotation.z", "transform.translation.x", "transform.translation.y",
                         "transform.translation.z", "opacity"]
        guard supported.contains(keyPath), !key.isEmpty, fromValue.isFinite, toValue.isFinite,
              duration.isFinite, duration > 0, beginOffset.isFinite else { return false }
        let animationKey = key.hasPrefix("ambient.") ? key : "ambient." + key
        let limit: CGFloat = keyPath == "opacity" ? 1 : (keyPath == "transform.rotation.z" ? .pi * 16 : 1000)
        let track = AmbientTrack(layer: layer, key: animationKey, keyPath: keyPath,
                                 from: min(limit, max(-limit, fromValue)), to: min(limit, max(-limit, toValue)),
                                 duration: min(3600, max(0.05, duration)), autoreverses: autoreverses,
                                 beginOffset: min(3600, max(-3600, beginOffset)),
                                 timingFunction: timingFunction ?? (keyPath == "transform.rotation.z" && !autoreverses
                                     ? .linear : .easeInEaseOut))
        let identity = ObjectIdentifier(layer)
        if baselinePoses[identity] == nil {
            baselinePoses[identity] = BaselinePose(layer: layer, transform: layer.transform, opacity: layer.opacity)
        }
        if let index = ambientTracks.firstIndex(where: { $0.layer === layer && $0.key == animationKey }) {
            let previous = ambientTracks[index]
            if previous.keyPath == track.keyPath && previous.from == track.from && previous.to == track.to
                && previous.duration == track.duration && previous.autoreverses == track.autoreverses
                && previous.beginOffset == track.beginOffset && previous.timingFunction == track.timingFunction { return true }
            ambientTracks[index] = track
        } else {
            ambientTracks.append(track)
        }
        if isRunning && !reducedMotion && ambientEnabled { install(track) }
        return true
    }

    /// Begin event-driven pointer response before deployment. No ambient tracks
    /// are installed, so finite transition completions cannot inherit an
    /// infinite animation. Repeating this preserves the current pose and tracks.
    /// Demoting a fully open HUD freezes only ambient artwork for retraction;
    /// pointer follow-through remains continuous until the final `stop`.
    func startPointerFollowing(reducedMotion: Bool, initialPoint: CGPoint = .zero) {
        precondition(Thread.isMainThread)
        if isPointerFollowing, self.reducedMotion == reducedMotion {
            if isRunning {
                isRunning = false
                removeOwnedAnimations(includeParallax: false, freezePresentation: true)
            }
            return
        }
        if isPointerFollowing || isRunning { stop(freezePresentation: false) }
        self.reducedMotion = reducedMotion
        restoreBaselinePoses()
        targetNormalizedPoint = reducedMotion ? .zero : HUDMotionMath.normalizedPoint(initialPoint)
        retargetPlanes(to: targetNormalizedPoint, animated: false)
        isPointerFollowing = true
    }

    /// Call after the opening CATransaction has fully completed, e.g. in a new
    /// main-queue turn. Infinite tracks must not join the opening completion group.
    /// Repeating this with the same accessibility setting leaves running tracks intact.
    /// An already-following deployment promotes without re-seeding or replacing
    /// its current pointer tracks. A fresh start seeds the supplied position.
    func start(reducedMotion: Bool, initialPoint: CGPoint = .zero) {
        precondition(Thread.isMainThread)
        if isRunning, self.reducedMotion == reducedMotion { return }
        if !isPointerFollowing || self.reducedMotion != reducedMotion {
            startPointerFollowing(reducedMotion: reducedMotion, initialPoint: initialPoint)
        }
        restoreBaselinePoses()
        isRunning = true
        ambientEpoch = CACurrentMediaTime()
        guard !reducedMotion, ambientEnabled else { return }
        for track in ambientTracks { install(track) }
    }

    func setParallax(normalizedPoint: CGPoint, animated: Bool = true) {
        precondition(Thread.isMainThread)
        guard isPointerFollowing, !reducedMotion else { return }
        let point = HUDMotionMath.normalizedPoint(normalizedPoint)
        guard point != targetNormalizedPoint else { return }
        targetNormalizedPoint = point
        retargetPlanes(to: point, animated: animated)
    }

    func resetParallax(animated: Bool = true) {
        precondition(Thread.isMainThread)
        targetNormalizedPoint = .zero
        // Reset can prepare an inactive view before its next deployment.
        retargetPlanes(to: .zero, animated: animated && isPointerFollowing && !reducedMotion)
    }

    /// Hold the visible input surface during direct manipulation. Ambient
    /// artwork remains independent and keeps its original animation phase.
    func freezeParallax() {
        withoutActions {
            for plane in self.planes {
                if let shown = plane.spatial.presentation() { plane.spatial.transform = shown.transform }
                plane.spatial.removeAnimation(forKey: "parallax.transform")
            }
        }
    }

    /// Commit the rendered pose before removing scoped tracks, so closing can
    /// retract the currently visible geometry instead of snapping to its model.
    /// Deployment/exit animations use other keys and are never removed here.
    func stop(freezePresentation: Bool) {
        precondition(Thread.isMainThread)
        isRunning = false
        isPointerFollowing = false
        ambientEpoch = nil
        removeOwnedAnimations(includeParallax: true, freezePresentation: freezePresentation)
        if !freezePresentation {
            restoreBaselinePoses()
            resetParallax(animated: false)
        }
    }

    private func removeOwnedAnimations(includeParallax: Bool, freezePresentation: Bool) {
        let layers = allManagedLayers()
        withoutActions {
            for layer in layers {
                let keys = layer.animationKeys() ?? []
                let ownedKeys = keys.filter { $0.hasPrefix("ambient.") || (includeParallax && $0.hasPrefix("parallax.")) }
                if freezePresentation, !ownedKeys.isEmpty, let shown = layer.presentation() {
                    if ownedKeys.contains(where: { $0.hasPrefix("parallax.") })
                        || self.ambientTracks.contains(where: { $0.layer === layer && $0.keyPath.hasPrefix("transform.") }) {
                        layer.transform = shown.transform
                    }
                    if self.ambientTracks.contains(where: { $0.layer === layer && $0.keyPath == "opacity" }) {
                        layer.opacity = shown.opacity
                    }
                }
                for key in ownedKeys { layer.removeAnimation(forKey: key) }
            }
        }
    }

    private func retargetPlanes(to point: CGPoint, animated: Bool, targets: [HUDDepthPlane]? = nil) {
        // One transaction submits all planes together. Explicit finite
        // tracks still run; implicit animations cannot extend pointer latency.
        withoutActions {
            for plane in targets ?? planes {
                let previous = plane.spatial.presentation()?.transform ?? plane.spatial.transform
                let next = HUDMotionMath.transform(normalizedPoint: point, depth: plane.depth,
                                                   travel: plane.travel, reducedMotion: reducedMotion,
                                                   parallaxIntensity: parallaxIntensity, perspectiveIntensity: perspectiveIntensity,
                                                   projectionBounds: plane.projectionBounds)
                plane.spatial.transform = next
                guard animated else {
                    plane.spatial.removeAnimation(forKey: "parallax.transform")
                    continue
                }
                let animation = CABasicAnimation(keyPath: "transform")
                animation.fromValue = NSValue(caTransform3D: previous)
                animation.toValue = NSValue(caTransform3D: next)
                animation.duration = plane.pointerResponseDuration
                // A positive initial slope keeps continuous input responsive;
                // retarget from the displayed pose so reversals do not jump.
                animation.timingFunction = self.pointerTiming
                plane.spatial.add(animation, forKey: "parallax.transform")
            }
        }
    }

    private func install(_ track: AmbientTrack) {
        guard let layer = track.layer else { return }
        let animation = CABasicAnimation(keyPath: track.keyPath)
        animation.fromValue = track.from
        animation.toValue = track.to
        animation.isAdditive = true
        animation.duration = track.duration
        animation.autoreverses = track.autoreverses
        animation.repeatCount = .infinity
        animation.beginTime = layer.convertTime(ambientEpoch ?? CACurrentMediaTime(), from: nil) + track.beginOffset
        animation.fillMode = .backwards
        animation.timingFunction = CAMediaTimingFunction(name: track.timingFunction)
        layer.add(animation, forKey: track.key)
    }

    private func restoreBaselinePoses() {
        withoutActions {
            for pose in self.baselinePoses.values {
                guard let layer = pose.layer else { continue }
                layer.transform = pose.transform
                layer.opacity = pose.opacity
            }
        }
    }

    private func allManagedLayers() -> [CALayer] {
        var seen = Set<ObjectIdentifier>()
        var result: [CALayer] = []
        func append(_ layer: CALayer) {
            guard seen.insert(ObjectIdentifier(layer)).inserted else { return }
            result.append(layer)
            layer.sublayers?.forEach(append)
        }
        planes.forEach { append($0.deployment) }
        ambientTracks.compactMap { $0.layer }.forEach(append)
        return result
    }

    private func countAnimations(prefix: String) -> Int {
        allManagedLayers().reduce(0) { total, layer in
            total + (layer.animationKeys() ?? []).filter { $0.hasPrefix(prefix) }.count
        }
    }

    private func withoutActions(_ changes: () -> Void) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        changes()
        CATransaction.commit()
    }

    private struct AmbientTrack {
        weak var layer: CALayer?
        let key: String
        let keyPath: String
        let from: CGFloat
        let to: CGFloat
        let duration: TimeInterval
        let autoreverses: Bool
        let beginOffset: TimeInterval
        let timingFunction: CAMediaTimingFunctionName
    }

    private struct BaselinePose {
        weak var layer: CALayer?
        let transform: CATransform3D
        let opacity: Float
    }
}
