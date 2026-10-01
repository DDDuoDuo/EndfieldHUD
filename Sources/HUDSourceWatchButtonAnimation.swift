import Foundation
import simd

/// Source Animator instance playback, evaluated using the caller's one clock.
/// Highlighted is finite even when an exported clip has a looping wrap flag.
/// Every state/transition joins to its original per-instance bound clip ID.
final class HUDSourceWatchButtonAnimation {
    enum State: String, CaseIterable { case normal = "Normal", highlighted = "Highlighted", pressed = "Pressed", disabled = "Disabled" }
    private struct Channel: Hashable { let node: HUDSourceID; let group: String; let attribute: String }
    private struct Samples { var channels: [Channel: HUDSourceCurveValue] = [:]; var unbound: Set<String> = [] }
    private struct Template { let clip: HUDSourceAnimationClip; let speed: Double; let cycleOffset: Double }
    private struct Transition { let source: State?; let destination: State; let duration: Double; let fixed: Bool; let canRepeat: Bool }
    private struct Configuration {
        let root: HUDSourceID
        let templates: [State: Template]
        let transitions: [Transition]
        let hoverEnable: HUDSourceID?
    }
    private struct Playback { var state: State; var started: Double; var endpoint: Bool = false }
    private enum Origin { case playback(Playback), snapshot(Samples) }
    private struct Blend { let origin: Origin; let started: Double; let duration: Double }
    private struct Instance { var playback: Playback; var blend: Blend? = nil; var hovered = false }
    private let scene: HUDSourceScene
    private let configurations: [HUDSourceID: Configuration]
    private let order: [HUDSourceID]
    private var instances: [HUDSourceID: Instance]
    private var clock: Double?

    convenience init(document: HUDSourceWatchDocument) throws {
        let data = try Data(contentsOf: document.root.appendingPathComponent("controller-transitions.json"))
        let transitions = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self, from: data)
        try self.init(scene: document.scene, library: document.library, animators: document.animators, transitionData: transitions)
    }
    init(scene: HUDSourceScene, library: HUDSourceAnimationLibrary,
         animators: [HUDSourceWatchDocument.Animator], transitionData: HUDSourceJSONValue) throws {
        self.scene = scene
        var clips: [HUDSourceID: HUDSourceAnimationClip] = [:]
        for clip in library.clips {
            guard clips.updateValue(clip, forKey: clip.id) == nil else { throw HUDSourceError.invalid("Duplicate source button clip") }
        }
        var settings: [String: (speed: Double, offset: Double)] = [:]
        for controller in transitionData["controllers"].array {
            for machine in controller["state_machines"].array {
                for state in machine["states"].array {
                    guard let id = state["source_clip_id"].string else { continue }
                    settings[id] = (state["speed"].float(1), state["cycle_offset"].float())
                }
            }
        }
        var config: [HUDSourceID: Configuration] = [:], values: [HUDSourceID: Instance] = [:]
        let metadata = transitionData["instances"].array
        for animator in animators {
            guard scene.node(animator.rootID) != nil,
                  let source = metadata.first(where: { $0["root_node_id"].string == animator.rootID.rawValue }) else {
                throw HUDSourceError.invalid("Missing source button instance metadata")
            }
            var templates: [State: Template] = [:]
            for state in animator.states {
                guard let kind = State(rawValue: state.name), let clip = clips[state.clipID] else {
                    throw HUDSourceError.invalid("Missing source button state clip")
                }
                let sourceID = source["transitions"].array.first { $0["destination_bound_clip_id"].string == state.clipID.rawValue }?["destination_source_clip_id"].string
                guard let canonical = sourceID, let timing = settings[canonical], timing.speed.isFinite, timing.offset.isFinite,
                      clip.lastKeyTime.isFinite, clip.lastKeyTime >= 0 else { throw HUDSourceError.invalid("Missing source button state timing") }
                templates[kind] = Template(clip: clip, speed: timing.speed, cycleOffset: timing.offset)
            }
            guard State.allCases.allSatisfy({ templates[$0] != nil }) else { throw HUDSourceError.invalid("Incomplete source button states") }
            var transitions: [Transition] = []
            for t in source["transitions"].array {
                guard let name = t["destination_name"].string, let destination = State(rawValue: name),
                      t["duration"].float().isFinite, t["duration"].float() >= 0 else { throw HUDSourceError.invalid("Invalid source button transition") }
                var origin: State?
                if let index = t["source_state_index"].number {
                    // State order is taken from the original packed controller,
                    // rather than assuming that every controller uses one order.
                    let controller = transitionData["controllers"].array.first { $0["id"].string == source["controller_id"].string }
                    let machine = controller?["state_machines"].array.first { $0["index"].float() == t["state_machine_index"].float() }
                    let state = machine?["states"].array.first { $0["index"].float() == index }
                    if let name = state?["name"].string { origin = State(rawValue: name) }
                }
                transitions.append(Transition(source: origin, destination: destination, duration: t["duration"].float(),
                    fixed: t["has_fixed_duration"].flag(), canRepeat: t["can_transition_to_self"].flag()))
            }
            let hover = source["hover_enable_node_id"].string.map { HUDSourceID(rawValue: $0) }
            if let id = hover, scene.node(id) == nil { throw HUDSourceError.invalid("Unknown source hover-enable transform") }
            config[animator.rootID] = Configuration(root: animator.rootID, templates: templates, transitions: transitions, hoverEnable: hover)
            values[animator.rootID] = Instance(playback: Playback(state: source["source_interactable"].flag(true) ? .normal : .disabled, started: 0))
        }
        configurations = config; instances = values; order = animators.map { $0.rootID }
    }

    var instanceIDs: [HUDSourceID] { order }
    func state(on id: HUDSourceID) -> State? { instances[id]?.playback.state }

    func setState(_ state: State, on id: HUDSourceID, at time: Double, reduceMotion: Bool = false) {
        guard let time = advance(time), let config = configurations[id], var instance = instances[id],
              let transition = config.transitions.first(where: { $0.destination == state && $0.source == instance.playback.state })
                ?? config.transitions.first(where: { $0.destination == state && $0.source == nil }) else { return }
        if state == instance.playback.state && !transition.canRepeat { return }
        let previous = instance.playback
        let current = sample(instance, config: config, at: time)
        let duration: Double
        if transition.fixed { duration = transition.duration }
        else {
            let template = config.templates[previous.state]!
            duration = template.speed == 0 ? 0 : transition.duration * template.clip.lastKeyTime / abs(template.speed)
        }
        let origin: Origin = instance.blend == nil ? .playback(previous) : .snapshot(current)
        instance.playback = Playback(state: state, started: time, endpoint: reduceMotion)
        instance.blend = duration > 0 && !reduceMotion ? Blend(origin: origin, started: time, duration: duration) : nil
        if state == .highlighted { instance.hovered = true }
        if state == .normal || state == .disabled { instance.hovered = false }
        instances[id] = instance
    }

    /// Hover visibility is separate from Pressed. A press while inside retains
    /// its source hover-enable node; leaving can clear it while staying pressed.
    func setHovered(_ hovered: Bool, on id: HUDSourceID, at time: Double, reduceMotion: Bool = false) {
        guard instances[id] != nil else { return }
        if instances[id]!.playback.state != .pressed && instances[id]!.playback.state != .disabled {
            setState(hovered ? .highlighted : .normal, on: id, at: time, reduceMotion: reduceMotion)
        }
        let enabled = instances[id]?.playback.state != .disabled
        instances[id]?.hovered = hovered && enabled
    }

    /// Apply after root/source wrapper sampling and before layout writers.
    /// The packed keyboard constants include X/Y=0; layout then drives those
    /// axes, while the source controller's independent Z values are preserved.
    func apply(to pose: inout HUDSourceWatchPose, at time: Double, reduceMotion: Bool = false) {
        guard let time = advance(time) else { return }
        for id in order {
            guard let config = configurations[id], var instance = instances[id] else { continue }
            if reduceMotion { instance.playback.endpoint = true; instance.blend = nil }
            if let blend = instance.blend, time - blend.started >= blend.duration { instance.blend = nil }
            let sampled = sample(instance, config: config, at: time)
            for (channel, value) in sampled.channels { apply(value, channel: channel, to: &pose) }
            pose.unboundPaths.formUnion(sampled.unbound)
            if let hover = config.hoverEnable {
                var transform = pose.transforms[hover] ?? HUDSourceTransformOverride()
                transform.active = instance.hovered; pose.transforms[hover] = transform
            }
            instances[id] = instance
        }
    }

    /// No timers/completion callbacks survive a concealed or replaced menu.
    func reset(at time: Double, reduceMotion: Bool = false) {
        guard time.isFinite else { return }; clock = time
        for id in order {
            instances[id] = Instance(playback: Playback(state: .normal, started: time, endpoint: reduceMotion))
        }
    }

    private func advance(_ value: Double) -> Double? {
        guard value.isFinite else { return nil }
        let current = max(clock ?? value, value); clock = current; return current
    }
    private func sample(_ playback: Playback, config: Configuration, at time: Double) -> Samples {
        let template = config.templates[playback.state]!, clip = template.clip
        let local = playback.endpoint ? clip.lastKeyTime : min(clip.lastKeyTime, max(0,
            max(0, time - playback.started) * template.speed + template.cycleOffset * clip.lastKeyTime))
        var samples = Samples()
        for curve in clip.curves {
            if curve.nodeIDs.isEmpty { samples.unbound.insert(curve.path); continue }
            guard let value = curve.sample(at: local) else { continue }
            for node in curve.nodeIDs where scene.node(node) != nil {
                samples.channels[Channel(node: node, group: curve.group, attribute: curve.attribute)] = value
            }
        }
        return samples
    }
    private func sample(_ instance: Instance, config: Configuration, at time: Double) -> Samples {
        let destination = sample(instance.playback, config: config, at: time)
        guard let blend = instance.blend else { return destination }
        let origin: Samples
        switch blend.origin {
        case .playback(let playback): origin = sample(playback, config: config, at: time)
        case .snapshot(let snapshot): origin = snapshot
        }
        let fraction = min(1, max(0, (time - blend.started) / blend.duration))
        var result = Samples(); result.unbound = origin.unbound.union(destination.unbound)
        for channel in Set(origin.channels.keys).union(destination.channels.keys) {
            let from = origin.channels[channel] ?? destination.channels[channel]!, to = destination.channels[channel] ?? from
            result.channels[channel] = interpolate(from, to, fraction: fraction)
        }
        return result
    }
    private func interpolate(_ from: HUDSourceCurveValue, _ to: HUDSourceCurveValue, fraction: Double) -> HUDSourceCurveValue {
        func mix(_ a: Double, _ b: Double) -> Double { a + (b - a) * fraction }
        switch (from, to) {
        case (.scalar(let a), .scalar(let b)): return .scalar(mix(a, b))
        case (.vector3(let a), .vector3(let b)): return .vector3(HUDSourceVector3(mix(a.x, b.x), mix(a.y, b.y), mix(a.z, b.z)))
        case (.quaternion(let a), .quaternion(let b)): return .quaternion(HUDSourceQuaternion(mix(a.x, b.x), mix(a.y, b.y), mix(a.z, b.z), mix(a.w, b.w)))
        default: return fraction < 1 ? from : to
        }
    }
    private func apply(_ value: HUDSourceCurveValue, channel: Channel, to pose: inout HUDSourceWatchPose) {
        guard let node = scene.node(channel.node) else { return }
        var transform = pose.transforms[channel.node] ?? HUDSourceTransformOverride()
        let attribute = channel.attribute, axis = attribute.hasSuffix(".x") ? 0 : (attribute.hasSuffix(".y") ? 1 : 2)
        switch (channel.group, value) {
        case ("m_PositionCurves", .vector3(let v)): transform.localPosition = v
        case ("m_ScaleCurves", .vector3(let v)): transform.localScale = v
        case ("m_RotationCurves", .quaternion(let q)): transform.localRotation = q
        case ("m_FloatCurves", .scalar(let scalar)):
            switch attribute {
            case "m_IsActive": transform.active = scalar >= 0.5
            case "m_LocalPosition.x", "m_LocalPosition.y", "m_LocalPosition.z": transform.positionComponents[axis] = scalar
            case "m_LocalScale.x", "m_LocalScale.y", "m_LocalScale.z":
                var v = (transform.localScale ?? node.transform.localScale).simd; v[axis] = scalar
                transform.localScale = HUDSourceVector3(v.x, v.y, v.z)
            case "m_AnchoredPosition.x", "m_AnchoredPosition.y":
                var p = (transform.anchoredPosition3D ?? HUDSourceVector3(node.transform.rect?.anchoredPosition.x ?? 0,
                    node.transform.rect?.anchoredPosition.y ?? 0, node.transform.localPosition.z)).simd
                p[axis] = scalar; transform.anchoredPosition3D = HUDSourceVector3(p.x, p.y, p.z)
            case "m_AnchorMin.x", "m_AnchorMin.y":
                var v = (transform.anchorMin ?? node.transform.rect?.anchorMin ?? HUDSourceVector2(0, 0)).simd; v[axis] = scalar
                transform.anchorMin = HUDSourceVector2(v.x, v.y)
            case "m_AnchorMax.x", "m_AnchorMax.y":
                var v = (transform.anchorMax ?? node.transform.rect?.anchorMax ?? HUDSourceVector2(0, 0)).simd; v[axis] = scalar
                transform.anchorMax = HUDSourceVector2(v.x, v.y)
            default: pose.properties[channel.node, default: [:]][attribute] = scalar
            }
        default: break
        }
        pose.transforms[channel.node] = transform
    }
}
