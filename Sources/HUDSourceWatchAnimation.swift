import Foundation
import CoreGraphics
import simd

/// WatchBlur has its own Linear wrapper. Its alpha keys finish before the
/// main Watch wrapper; do not stretch them to the menu's deployment duration.
struct HUDSourceWatchBlurAnimation {
    struct Track {
        let curve: HUDSourceAnimationCurve
        let duration: Double
        let startAlpha: Double
        let endAlpha: Double
        let controlPoints: SIMD4<Float>
        init(curve: HUDSourceAnimationCurve) throws {
            let keys = curve.body.keys
            guard curve.group == "m_FloatCurves", curve.path == "BlurBG", curve.attribute == "m_Alpha",
                  keys.count == 2, keys[0].time == 0, keys[1].time > 0,
                  keys.allSatisfy({ $0.weightedMode == 0 }),
                  case .scalar(let start) = keys[0].value, case .scalar(let end) = keys[1].value,
                  case .scalar(let outgoing) = keys[0].outSlope,
                  case .scalar(let incoming) = keys[1].inSlope,
                  start != end, outgoing.isFinite, incoming.isFinite else {
                throw HUDSourceError.invalid("Unsupported original WatchBlur alpha track")
            }
            self.curve = curve; duration = keys[1].time; startAlpha = start; endAlpha = end
            // An unweighted Hermite segment is this cubic Bezier with x=t.
            // Core Animation therefore evaluates the original segment without
            // sampled keyframes or a second easing function.
            controlPoints = SIMD4(1 / 3, Float(outgoing * duration / (3 * (end - start))),
                2 / 3, Float(1 - incoming * duration / (3 * (end - start))))
        }
        func alpha(at elapsed: Double) -> Double? {
            guard case .scalar(let alpha)? = curve.sample(at: elapsed) else { return nil }
            return alpha
        }
    }
    let entrance: Track
    let exit: Track
    init(data: Data) throws {
        let source = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self, from: data)
        guard source["wrapper"]["_options"]["animEase"].number == 1, source["default_speed"].number == 1 else {
            throw HUDSourceError.invalid("Unsupported WatchBlur wrapper clock")
        }
        struct Payload: Decodable {
            struct Channel: Decodable { let curve: HUDSourceAnimationCurve }
            let entrance: Channel
            let exit: Channel
        }
        let payload = try HUDSourceJSON.decoder().decode(Payload.self, from: data)
        entrance = try Track(curve: payload.entrance.curve)
        exit = try Track(curve: payload.exit.curve)
    }
}

/// Source property channels stay distinct: CanvasGroup alpha, graphic color
/// alpha, and material alpha multiply at their original places in rendering.
struct HUDSourceWatchPose {
    var transforms: [HUDSourceID: HUDSourceTransformOverride]
    var properties: [HUDSourceID: [String: Double]] = [:]
    var unboundPaths: Set<String> = []
    var unregisteredBindings: Set<String> = []

    func value(_ attribute: String, on node: HUDSourceID, fallback: Double) -> Double {
        properties[node]?[attribute] ?? fallback
    }
}

/// Immutable sampler. Native previews and the live display share this exact
/// evaluation path; seeking does not start timers or Core Animation tracks.
struct HUDSourceWatchAnimation {
    private static let rectTransformScalarAttributes: Set<String> = [
        "m_LocalPosition.x", "m_LocalPosition.y", "m_LocalPosition.z",
        "m_LocalScale.x", "m_LocalScale.y", "m_LocalScale.z",
        "m_AnchoredPosition.x", "m_AnchoredPosition.y",
        "m_AnchorMin.x", "m_AnchorMin.y", "m_AnchorMax.x", "m_AnchorMax.y",
        "m_SizeDelta.x", "m_SizeDelta.y", "m_Pivot.x", "m_Pivot.y"
    ]
    let scene: HUDSourceScene
    let entrance: HUDSourceAnimationClip
    let ambient: HUDSourceAnimationClip
    let exit: HUDSourceAnimationClip

    init(scene: HUDSourceScene, library: HUDSourceAnimationLibrary) throws {
        func clip(_ binding: String) throws -> HUDSourceAnimationClip {
            let matches = library.clips.filter { $0.binding == binding }
            guard matches.count == 1 else { throw HUDSourceError.invalid("Missing/duplicate Watch \(binding)") }
            return matches[0]
        }
        self.scene = scene
        entrance = try clip("_animationIn")
        ambient = try clip("_animationLoop")
        exit = try clip("_animationOut")
    }

    func pose(entranceTime: Double, ambientTime: Double?, exitTime: Double?,
              canvasResolution: SIMD2<Double>,
              runtimeOverrides: [HUDSourceID: HUDSourceTransformOverride] = [:]) throws -> HUDSourceWatchPose {
        guard canvasResolution.x.isFinite, canvasResolution.y.isFinite,
              canvasResolution.x > 0, canvasResolution.y > 0 else {
            throw HUDSourceError.invalid("Invalid Watch canvas resolution")
        }
        var initial = runtimeOverrides
        var root = initial[scene.rootID] ?? HUDSourceTransformOverride()
        // UIManager's world-panel initialization explicitly stretches the
        // root and resets scale/position. It is not the serialized scale zero.
        root.localScale = HUDSourceVector3(1, 1, 1)
        root.anchoredPosition3D = HUDSourceVector3(0, 0, 0)
        root.anchorMin = HUDSourceVector2(0, 0)
        root.anchorMax = HUDSourceVector2(1, 1)
        root.pivot = HUDSourceVector2(0.5, 0.5)
        // The exported hierarchy excludes its live WorldUIRoot parent. Store
        // the resulting full rect as sizeDelta in this virtual-root resolver;
        // the real engine uses stretched anchors with sizeDelta zero instead.
        root.sizeDelta = HUDSourceVector2(canvasResolution.x, canvasResolution.y)
        initial[scene.rootID] = root
        var pose = HUDSourceWatchPose(transforms: initial)
        let base = try scene.resolve(overrides: initial)
        apply(entrance, time: entranceTime, to: &pose, base: base)
        if let time = ambientTime { apply(ambient, time: time, to: &pose, base: base) }
        if let time = exitTime { apply(exit, time: time, to: &pose, base: base) }
        return pose
    }

    /// Animator states use instance-bound clips. Bindings absent from the PC
    /// prefab remain reported; no nearby node is substituted by name.
    func apply(_ clip: HUDSourceAnimationClip, time: Double, to pose: inout HUDSourceWatchPose,
               base: [HUDSourceID: HUDSourceResolvedNode]) {
        Self.apply(clip, time: time, to: &pose, base: base, scene: scene)
    }

    /// Domain wrappers bind to their own original prefab root. Reuse the same
    /// source channel rules with that scene rather than rebinding by basename.
    static func apply(_ clip: HUDSourceAnimationClip, time: Double, to pose: inout HUDSourceWatchPose,
                      base: [HUDSourceID: HUDSourceResolvedNode], scene: HUDSourceScene) {
        guard let time = clip.localTime(time) else { return }
        for curve in clip.curves {
            // The RectTransform-specific handler covers anchors/size/pivot
            // and Z. Legacy inherited position/scale components also bind
            // through the native Serialized TypeTree float fallback. Keep
            // their original class ID; layout later rewrites driven axes.
            // Other properties remain diagnosed outside this verified set.
            if curve.group == "m_FloatCurves", curve.classID == 224,
               !Self.rectTransformScalarAttributes.contains(curve.attribute) {
                pose.unregisteredBindings.insert("224:" + curve.attribute)
                continue
            }
            if curve.nodeIDs.isEmpty { pose.unboundPaths.insert(curve.path); continue }
            guard let value = curve.sample(at: time) else { continue }
            for id in curve.nodeIDs {
                guard let node = scene.node(id), base[id] != nil else {
                    pose.unboundPaths.insert(curve.path); continue
                }
                var transform = pose.transforms[id] ?? HUDSourceTransformOverride()
                switch (curve.group, value) {
                case ("m_PositionCurves", .vector3(let v)): transform.localPosition = v
                case ("m_ScaleCurves", .vector3(let v)): transform.localScale = v
                case ("m_RotationCurves", .quaternion(let q)): transform.localRotation = q
                case ("m_FloatCurves", .scalar(let scalar)):
                    switch curve.attribute {
                    case "m_IsActive": transform.active = scalar >= 0.5
                    case "m_LocalPosition.x", "m_LocalPosition.y", "m_LocalPosition.z":
                        transform.positionComponents[Self.axis(curve.attribute)] = scalar
                    case "m_LocalScale.x", "m_LocalScale.y", "m_LocalScale.z":
                        transform.localScale = Self.replace((transform.localScale ?? node.transform.localScale).simd,
                            attribute: curve.attribute, value: scalar)
                    case "m_AnchoredPosition.x", "m_AnchoredPosition.y":
                        let p = transform.anchoredPosition3D ?? HUDSourceVector3(
                            node.transform.rect?.anchoredPosition.x ?? node.transform.localPosition.x,
                            node.transform.rect?.anchoredPosition.y ?? node.transform.localPosition.y,
                            node.transform.localPosition.z)
                        transform.anchoredPosition3D = Self.replace(p.simd, attribute: curve.attribute, value: scalar)
                    case "m_AnchorMin.x", "m_AnchorMin.y":
                        let v = transform.anchorMin ?? node.transform.rect?.anchorMin ?? HUDSourceVector2(0, 0)
                        transform.anchorMin = Self.axis(curve.attribute) == 0 ? HUDSourceVector2(scalar, v.y) : HUDSourceVector2(v.x, scalar)
                    case "m_AnchorMax.x", "m_AnchorMax.y":
                        let v = transform.anchorMax ?? node.transform.rect?.anchorMax ?? HUDSourceVector2(0, 0)
                        transform.anchorMax = Self.axis(curve.attribute) == 0 ? HUDSourceVector2(scalar, v.y) : HUDSourceVector2(v.x, scalar)
                    case "m_SizeDelta.x", "m_SizeDelta.y":
                        let v = transform.sizeDelta ?? node.transform.rect?.sizeDelta ?? HUDSourceVector2(0, 0)
                        transform.sizeDelta = Self.axis(curve.attribute) == 0 ? HUDSourceVector2(scalar, v.y) : HUDSourceVector2(v.x, scalar)
                    case "m_Pivot.x", "m_Pivot.y":
                        let v = transform.pivot ?? node.transform.rect?.pivot ?? HUDSourceVector2(0.5, 0.5)
                        transform.pivot = Self.axis(curve.attribute) == 0 ? HUDSourceVector2(scalar, v.y) : HUDSourceVector2(v.x, scalar)
                    default: pose.properties[id, default: [:]][curve.attribute] = scalar
                    }
                default: break
                }
                pose.transforms[id] = transform
            }
        }
    }

    private static func replace(_ p: SIMD3<Double>, attribute: String, value: Double) -> HUDSourceVector3 {
        if attribute.hasSuffix(".x") { return HUDSourceVector3(value, p.y, p.z) }
        if attribute.hasSuffix(".y") { return HUDSourceVector3(p.x, value, p.z) }
        return HUDSourceVector3(p.x, p.y, value)
    }
    private static func axis(_ attribute: String) -> Int {
        attribute.hasSuffix(".x") ? 0 : (attribute.hasSuffix(".y") ? 1 : 2)
    }
}

/// Visibility lifecycle owns one clock. Interrupted completion handlers cannot
/// revive a concealed menu. Reduce Motion seeks the exact source final pose.
final class HUDSourceWatchPlayback {
    enum Phase: Equatable { case concealed, opening, visible, closing }
    private(set) var phase: Phase = .concealed
    private(set) var generation: UInt64 = 0
    private var phaseStart: Double = 0
    private var loopStart: Double = 0
    private var completion: (() -> Void)?
    let animation: HUDSourceWatchAnimation

    init(animation: HUDSourceWatchAnimation) { self.animation = animation }

    func open(at time: Double, reduceMotion: Bool, completion: @escaping () -> Void = {}) {
        guard time.isFinite else { return }
        generation &+= 1; self.completion = nil
        phaseStart = time; loopStart = time + animation.entrance.lastKeyTime
        phase = reduceMotion ? .visible : .opening
        if reduceMotion { loopStart = time; completion() } else { self.completion = completion }
    }
    func close(at time: Double, reduceMotion: Bool, completion: @escaping () -> Void = {}) {
        guard time.isFinite else { return }
        generation &+= 1; self.completion = nil; phaseStart = time
        phase = reduceMotion ? .concealed : .closing
        if reduceMotion { completion() } else { self.completion = completion }
    }
    func conceal() { generation &+= 1; phase = .concealed; completion = nil }
    func showStable(at time: Double) {
        guard time.isFinite else { return }
        generation &+= 1; phase = .visible; phaseStart = time; loopStart = time; completion = nil
    }

    func sample(at time: Double, canvasResolution: SIMD2<Double>, reduceMotion: Bool,
                runtimeOverrides: [HUDSourceID: HUDSourceTransformOverride] = [:]) throws -> HUDSourceWatchPose? {
        guard time.isFinite, phase != .concealed else { return nil }
        if phase == .opening && (reduceMotion || time - phaseStart >= animation.entrance.lastKeyTime) {
            phase = .visible
            let handler = completion; completion = nil; handler?()
        } else if phase == .closing && (reduceMotion || time - phaseStart >= animation.exit.lastKeyTime) {
            phase = .concealed
            let handler = completion; completion = nil; handler?()
            return nil
        }
        guard phase != .concealed else { return nil }
        return try animation.pose(
            entranceTime: phase == .opening ? Self.clipTime(elapsed: time - phaseStart, length: animation.entrance.lastKeyTime) : animation.entrance.lastKeyTime,
            ambientTime: phase == .opening || reduceMotion ? nil : max(0, time - loopStart),
            exitTime: phase == .closing ? Self.clipTime(elapsed: time - phaseStart, length: animation.exit.lastKeyTime) : nil,
            canvasResolution: canvasResolution, runtimeOverrides: runtimeOverrides)
    }

    /// UIAnimationWrapper.PlayWithTween uses the serialized animEase=6
    /// (OutQuad) for finite clips. UIAnimationTween._SetValue multiplies this
    /// eased progress by AnimationState.length before SampleClip. Looping
    /// wrapper clips explicitly use ease=1 (Linear), so ambient stays linear.
    static func clipTime(elapsed: Double, length: Double) -> Double {
        guard length > 0 else { return 0 }
        let progress = min(1, max(0, elapsed / length))
        return (2 * progress - progress * progress) * length
    }
}
