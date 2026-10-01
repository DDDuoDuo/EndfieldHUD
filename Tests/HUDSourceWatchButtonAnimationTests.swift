import Foundation
import simd

enum HUDSourceWatchButtonAnimationTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: @autoclosure () -> Bool, _ message: String) {
            count += 1; if !value() { fatalError(message) }
        }
        func near(_ a: Double, _ b: Double) -> Bool { abs(a - b) < 1e-8 }
        func id(_ value: Int) -> HUDSourceID { HUDSourceID(rawValue: "CAB-buttons:\(value)") }
        do {
            let root = HUDSourceNode(id: id(1), path: "Watch", name: "Watch", parentID: nil, childIDs: [id(2), id(3)],
                transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(0, 0, 0)))
            let nodes = [root] + (2...3).map { n in
                HUDSourceNode(id: id(n), path: "Watch/Button\(n)", name: "Button\(n)", parentID: id(1), childIDs: [id(n + 2)],
                    transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(Double(n), 3, 0)))
            } + (4...5).map { n in
                HUDSourceNode(id: id(n), path: "Watch/Button\(n-2)/Hover", name: "Hover", parentID: id(n - 2), childIDs: [], active: false,
                    transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(0, 0, 0)))
            }
            let scene = try HUDSourceScene(rootID: id(1), nodes: nodes)
            let gray = 0.9056603908538818, duration = 1.0 / 6
            func curve(_ node: HUDSourceID, _ attribute: String, _ points: [(Double, Double)], linear: Bool = false) throws -> HUDSourceAnimationCurve {
                var keys: [HUDSourceAnimationKey] = []
                for (i, p) in points.enumerated() {
                    let outSlope = linear && i+1 < points.count ? (points[i+1].1 - p.1) / (points[i+1].0 - p.0) : 0
                    let inSlope = linear && i > 0 ? (p.1 - points[i-1].1) / (p.0 - points[i-1].0) : 0
                    keys.append(HUDSourceAnimationKey(time: p.0, value: .scalar(p.1), inSlope: .scalar(inSlope), outSlope: .scalar(outSlope),
                        weightedMode: 0, inWeight: .scalar(0), outWeight: .scalar(0)))
                }
                return try HUDSourceAnimationCurve(group: "m_FloatCurves", path: "", attribute: attribute, nodeIDs: [node],
                    body: HUDSourceAnimationCurve.Body(keys: keys, preInfinity: 2, postInfinity: 2))
            }
            let states = HUDSourceWatchButtonAnimation.State.allCases
            var clips: [HUDSourceAnimationClip] = [], animators: [HUDSourceWatchDocument.Animator] = []
            for n in 2...3 {
                var bound: [HUDSourceWatchDocument.Animator.State] = []
                for (index, state) in states.enumerated() {
                    let clipID = HUDSourceID(rawValue: "CAB-state:\(state.rawValue)@\(n)")
                    let z: Double = state == .highlighted ? -5 : (state == .pressed ? -20 : 0)
                    let stop = state == .highlighted ? duration : 1.0 / 60
                    let colors: [(Double, Double)] = state == .highlighted ? [(0, gray), (1.0/30, 1), (2.0/30, gray), (3.0/30, 1), (duration, 1)] : [(0, gray), (stop, gray)]
                    let color = try curve(id(n), "m_Color.r", colors)
                    let position = try curve(id(n), "m_LocalPosition.z", [(0, state == .highlighted ? 0 : z), (stop, z)], linear: true)
                    let alpha = try curve(id(n), "m_Color.a", [(0, state == .highlighted ? 1 : 0.8627451062202454), (stop, state == .highlighted ? 1 : 0.8627451062202454)])
                    let font = try curve(id(n), "m_fontColor.r", [(0, state == .highlighted ? 1 : gray), (stop, state == .highlighted ? 1 : gray)])
                    let material = try curve(id(n), "material._Alpha", [(0, Double(index) / 10), (stop, Double(index) / 10)])
                    clips.append(HUDSourceAnimationClip(binding: "Animator.state.\(state.rawValue)", id: clipID, name: state.rawValue,
                        sampleRate: 60, wrapMode: 2, lastKeyTime: stop, curves: [color, position, alpha, font, material]))
                    bound.append(HUDSourceWatchDocument.Animator.State(name: state.rawValue, clipID: clipID))
                }
                animators.append(HUDSourceWatchDocument.Animator(rootID: id(n), controllerName: "Fixture", states: bound))
            }
            let library = HUDSourceAnimationLibrary(clips: clips)
            func metadata(speed: Double = 1, offset: Double = 0) -> HUDSourceJSONValue {
                let sourceStates: [HUDSourceJSONValue] = states.enumerated().map { i, state in .object([
                    "index": .number(Double(i)), "name": .string(state.rawValue), "source_clip_id": .string("source-\(state.rawValue)"),
                    "speed": .number(state == .highlighted ? speed : 1), "cycle_offset": .number(state == .highlighted ? offset : 0)]) }
                let controller: HUDSourceJSONValue = .object(["id": .string("source-controller"), "state_machines": .array([
                    .object(["index": .number(0), "states": .array(sourceStates)])])])
                let instances: [HUDSourceJSONValue] = (2...3).map { n in
                    let transitions: [HUDSourceJSONValue] = states.map { state in .object([
                        "destination_name": .string(state.rawValue), "destination_source_clip_id": .string("source-\(state.rawValue)"),
                        "destination_bound_clip_id": .string("CAB-state:\(state.rawValue)@\(n)"), "source_state_index": .null,
                        "duration": .number(state == .normal || state == .pressed ? 0.1 : 0),
                        "has_fixed_duration": .bool(true), "can_transition_to_self": .bool(state == .disabled)]) }
                    return .object(["root_node_id": .string(id(n).rawValue), "controller_id": .string("source-controller"),
                        "source_interactable": .bool(true), "hover_enable_node_id": .string(id(n + 2).rawValue), "transitions": .array(transitions)])
                }
                return .object(["controllers": .array([controller]), "instances": .array(instances)])
            }
            let player = try HUDSourceWatchButtonAnimation(scene: scene, library: library, animators: animators, transitionData: metadata())
            func pose(_ player: HUDSourceWatchButtonAnimation, at time: Double, reduceMotion: Bool = false) -> HUDSourceWatchPose {
                var value = HUDSourceWatchPose(transforms: [:], properties: [id(1): ["m_Alpha": 0.7], id(2): ["m_fontColor.a": 0.6]])
                player.apply(to: &value, at: time, reduceMotion: reduceMotion); return value
            }
            let initial = pose(player, at: 0)
            check(initial.transforms[id(2)]!.positionComponents[2] == 0 && initial.properties[id(2)]?["m_Color.r"] == gray,
                  "All source Animator instances initially sample Normal")
            check(initial.properties[id(1)]?["m_Alpha"] == 0.7 && initial.properties[id(2)]?["m_fontColor.a"] == 0.6,
                  "CanvasGroup and unbound font alpha channels survive controller sampling")
            player.setState(.highlighted, on: id(2), at: 1)
            let white = pose(player, at: 1 + 1.0/30)
            check(white.properties[id(2)]?["m_Color.r"] == 1, "First short source flash reaches white")
            check(white.properties[id(3)]?["m_Color.r"] == gray && white.transforms[id(3)]!.positionComponents[2] == 0,
                  "A bound instance never animates another tile")
            check(white.transforms[id(4)]?.active == true && white.transforms[id(5)]?.active == false,
                  "Original hover-enable references stay instance-specific")
            let grayAgain = pose(player, at: 1 + 2.0/30)
            check(near(grayAgain.properties[id(2)]!["m_Color.r"]!, gray), "The finite source gray-white-gray-white sequence returns to gray")
            let steady = pose(player, at: 1.3)
            check(steady.properties[id(2)]?["m_Color.r"] == 1 && steady.transforms[id(2)]!.positionComponents[2] == -5,
                  "Dwell holds the source Highlighted endpoint without repeating its wrap flag")
            check(steady.properties[id(2)]?["m_Color.a"] == 1 && steady.properties[id(2)]?["m_fontColor.r"] == 1
                    && steady.properties[id(2)]?["material._Alpha"] == 0.1,
                  "Graphic, font and material property channels remain separate")
            player.setState(.normal, on: id(2), at: 1.3)
            let halfway = pose(player, at: 1.35)
            check(near(halfway.transforms[id(2)]!.positionComponents[2]!, -2.5), "Source fixed-duration exit retains partial Z halfway")
            player.setState(.pressed, on: id(2), at: 1.35)
            let pressedStart = pose(player, at: 1.35)
            check(near(pressedStart.transforms[id(2)]!.positionComponents[2]!, -2.5), "Interrupting exit begins from the currently sampled pose")
            let partialPress = pose(player, at: 1.4)
            check(near(partialPress.transforms[id(2)]!.positionComponents[2]!, -11.25), "Interrupted press blends the actual partial depth")
            player.setState(.normal, on: id(2), at: 1.4)
            let leaveStart = pose(player, at: 1.4)
            check(near(leaveStart.transforms[id(2)]!.positionComponents[2]!, -11.25), "Rapid press-to-leave is continuous at its new boundary")
            let returned = pose(player, at: 1.51)
            check(returned.transforms[id(2)]!.positionComponents[2] == 0 && returned.properties[id(2)]?["m_Color.r"] == gray,
                  "Canceled transitions cannot revive a departed highlight")
            player.setState(.highlighted, on: id(2), at: 2, reduceMotion: true)
            let reduced = pose(player, at: 2, reduceMotion: true), stillReduced = pose(player, at: 2.01)
            check(reduced.transforms[id(2)]!.positionComponents[2] == -5 && stillReduced.transforms[id(2)]!.positionComponents[2] == -5,
                  "Reduce Motion seeks the state endpoint and does not replay an old flash after toggling off")
            player.setState(.normal, on: id(2), at: 2.01)
            let stopped = pose(player, at: 2.03, reduceMotion: true)
            check(stopped.transforms[id(2)]!.positionComponents[2] == 0, "Enabling Reduce Motion during exit cancels the remaining blend")
            player.reset(at: 3)
            let reset = pose(player, at: 4)
            check(reset.transforms[id(2)]!.positionComponents[2] == 0 && reset.transforms[id(4)]?.active == false,
                  "Reset removes transient states without timer callbacks")

            let faster = try HUDSourceWatchButtonAnimation(scene: scene, library: library, animators: animators, transitionData: metadata(speed: 2))
            faster.setState(.highlighted, on: id(2), at: 5)
            check(near(pose(faster, at: 5 + 1.0/60).properties[id(2)]!["m_Color.r"]!, 1), "Original state speed changes clip time")
            let offset = try HUDSourceWatchButtonAnimation(scene: scene, library: library, animators: animators, transitionData: metadata(offset: 0.2))
            offset.setState(.highlighted, on: id(2), at: 6)
            check(near(pose(offset, at: 6).properties[id(2)]!["m_Color.r"]!, 1), "Original cycleOffset is applied in normalized clip time")
        } catch { fatalError("Source Watch button animation fixture failed: \(error)") }
        return count
    }
}
