import Foundation
import simd

enum HUDSourceSelectableColorTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: @autoclosure () -> Bool, _ message: String) {
            count += 1; if !condition() { fatalError(message) }
        }
        func id(_ value: Int) -> HUDSourceID { HUDSourceID(rawValue: "CAB-tint:\(value)") }
        func near(_ actual: Float, _ expected: Float) -> Bool { abs(actual - expected) < 2e-7 }
        let fade = Double(Float(0.1))
        func color(_ rgb: Double, _ alpha: Double) -> HUDSourceJSONValue {
            .object(["r": .number(rgb), "g": .number(rgb), "b": .number(rgb), "a": .number(alpha)])
        }
        func button(_ component: Int, target: Int?, transition: Int = 1, enabled: Bool = true,
                    interactable: Bool = true, multiplier: Double = 1, duration: Double = Double(Float(0.1))) -> HUDSourceWatchComponent {
            HUDSourceWatchComponent(id: id(component), type: "MonoBehaviour", script: "UIButton", data: [
                "m_Enabled": .bool(enabled), "m_Interactable": .bool(interactable), "m_Transition": .number(Double(transition)),
                "m_TargetGraphic": target.map { .object(["target_id": .string(id($0).rawValue)]) } ?? .null,
                "m_Colors": .object(["m_NormalColor": color(1, 0), "m_HighlightedColor": color(1, 1),
                    "m_PressedColor": color(1, Double(Float(0.2))), "m_SelectedColor": color(Double(Float(0.9607843160629272)), 1),
                    "m_DisabledColor": color(Double(Float(0.7843137383460999)), Double(Float(0.501960813999176))),
                    "m_ColorMultiplier": .number(multiplier), "m_FadeDuration": .number(duration)])])
        }
        func graphic(_ component: Int) -> HUDSourceWatchComponent {
            HUDSourceWatchComponent(id: id(component), type: "MonoBehaviour", script: "UIImage",
                data: ["m_Color": color(1, Double(Float(0.2)))])
        }
        do {
            let root = HUDSourceNode(id: id(1), path: "Watch", name: "Watch", parentID: nil,
                childIDs: (2...12).map(id), transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(0, 0, 0)))
            let nodes = [root] + (2...12).map { number in
                HUDSourceNode(id: id(number), path: "Watch/Node\(number)", name: "Node\(number)", parentID: id(1), childIDs: [],
                    transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(0, 0, 0)))
            }
            let scene = try HUDSourceScene(rootID: id(1), nodes: nodes)
            let components: [HUDSourceID: [HUDSourceWatchComponent]] = [
                id(2): [button(100, target: 200)], id(4): [graphic(200)],
                id(3): [button(101, target: 201, multiplier: 2)], id(5): [graphic(201)],
                id(6): [button(102, target: 200, transition: 3)], id(7): [button(103, target: nil)],
                id(8): [button(104, target: 200, enabled: false)],
                id(9): [button(105, target: 203, interactable: false)], id(10): [graphic(203)],
                id(11): [button(106, target: 204, duration: 0)], id(12): [graphic(204)]]
            func player() throws -> HUDSourceSelectableColor {
                try HUDSourceSelectableColor(scene: scene, components: components)
            }
            let p = try player(); p.reset(at: 0)
            let initial = p.colors(at: 0)
            check(Set(p.instanceIDs) == Set([id(2), id(3), id(9), id(11)]), "ColorTint does not claim Animator or disabled component instances")
            check(p.ignoredNullTargetButtonIDs == [id(7)], "Native null target is ignored without guessing the button's own Graphic")
            check(p.bindings.first(where: { $0.buttonNodeID == id(2) })?.targetGraphicID == id(200), "PPtr resolves the target component, not the button node")
            check(initial[id(4)] == SIMD4<Float>(1, 1, 1, 0) && initial[id(2)] == nil, "Initial normal tint belongs to the separately targeted Graphic node")
            check(initial[id(5)] == SIMD4<Float>(2, 2, 2, 0), "Source ColorMultiplier multiplies all channels without clamping HDR RGB")
            check(p.state(on: id(9)) == .disabled && initial[id(10)] == SIMD4<Float>(repeating: Float(0.7843137383460999))
                .replacingAlpha(Float(0.501960813999176)), "A source non-interactable button resets instantly to its Disabled ColorBlock")
            check(!p.requiresFrames(at: 0), "OnEnable-style reset has no opening fade")

            p.setState(.highlighted, on: id(2), at: 1)
            check(p.colors(at: 1)[id(4)]!.w == 0 && p.requiresFrames(at: 1), "Hover starts at the current renderer tint")
            check(!p.requiresFrames(at: 10), "Demand query sees the endpoint without advancing the one clock")
            let middle = p.colors(at: 1 + fade / 2)
            check(middle[id(4)]!.w == 0.5, "Original linear all-channel ColorTween has half alpha at half duration")
            check(near(middle[id(4)]!.w * Float(0.2), Float(0.1)), "Renderer tint half alpha multiplies Light's original point-two vertex alpha")
            check(middle[id(5)] == SIMD4<Float>(2, 2, 2, 0), "An instance does not alter another target renderer")
            p.setState(.pressed, on: id(2), at: 1 + fade / 2)
            check(p.colors(at: 1 + fade / 2)[id(4)]!.w == 0.5, "Hover interruption preserves the current alpha")
            let pressMiddle = p.colors(at: 1 + fade)[id(4)]!.w
            check(near(pressMiddle, 0.35), "Press retargets the partially visible layer toward source point-two alpha")
            p.setState(.normal, on: id(2), at: 1 + fade)
            check(near(p.colors(at: 1 + fade)[id(4)]!.w, pressMiddle), "Leaving while pressing cancels without a discontinuity")
            check(near(p.colors(at: 1 + 1.5 * fade)[id(4)]!.w, 0.175), "Interrupted exit uses a fresh source fade from the sampled color")
            check(p.colors(at: 1 + 2 * fade)[id(4)]!.w == 0 && !p.requiresFrames(at: 1 + 2 * fade), "Normal settles to zero and releases frame demand")
            check(components[id(4)]![0]["m_Color"].color.w == Float(0.2), "Tint playback never overwrites Graphic.m_Color")

            p.setState(.highlighted, on: id(2), at: 2)
            p.setState(.normal, on: id(2), at: 2)
            check(p.colors(at: 2)[id(4)]!.w == 0 && !p.requiresFrames(at: 2), "Requesting the current renderer color cancels an in-flight tween")
            p.setState(.highlighted, on: id(2), at: 3)
            let half = p.colors(at: 3 + fade / 2)[id(4)]!
            p.setState(.highlighted, on: id(2), at: 3 + fade / 2)
            check(p.colors(at: 3 + fade / 2)[id(4)] == half, "A repeated actual CrossFade request samples and restarts from the current color")
            check(near(p.colors(at: 3 + fade)[id(4)]!.w, 0.75), "Restarting a partially complete same-state request does not jump to its old endpoint")
            check(p.colors(at: 3 + 1.5 * fade)[id(4)]!.w == 1, "Repeated request reaches the new tween endpoint")
            p.setState(.selected, on: id(3), at: 4, reduceMotion: true)
            let selected = p.colors(at: 4)[id(5)]!
            check(selected.x == Float(0.9607843160629272) * 2 && selected.w == 2, "Selected source RGBA and multiplier survive without a fabricated white endpoint")
            check(!p.requiresFrames(at: 4), "Reduced motion samples immediately without retaining frames")
            p.setState(.disabled, on: id(3), at: 5)
            let disabled = p.colors(at: 5 + fade)[id(5)]!
            check(disabled.x == Float(0.7843137383460999) * 2 && disabled.w == Float(0.501960813999176) * 2, "Disabled state uses its original RGBA rather than lifecycle white")
            p.setEnabled(false, on: id(3), at: 6)
            check(p.colors(at: 6)[id(5)] == SIMD4<Float>(repeating: 1), "Source OnDisable instant clear sets white, bypassing ColorMultiplier")
            p.setState(.pressed, on: id(3), at: 7)
            check(p.colors(at: 7)[id(5)] == SIMD4<Float>(repeating: 1), "A disabled component does not receive effective-state transitions")
            p.setEnabled(true, on: id(3), at: 8)
            check(p.colors(at: 8)[id(5)] == SIMD4<Float>(2, 2, 2, 0) && !p.requiresFrames(at: 8), "OnEnable restores instantaneous normal rather than fading from lifecycle white")
            p.setState(.highlighted, on: id(11), at: 9)
            check(p.colors(at: 9)[id(12)]!.w == 1 && !p.requiresFrames(at: 9), "Zero fade duration reaches its source endpoint immediately")
            p.setState(.normal, on: id(2), at: 10)
            let settled = p.colors(at: 10.05, reduceMotion: true)
            check(settled[id(4)]!.w == 0 && !p.requiresFrames(at: 10.05), "Changing reduced-motion policy settles an already playing tween")
            p.setState(.highlighted, on: id(2), at: .nan)
            check(p.colors(at: .infinity) == settled && !p.requiresFrames(at: .nan), "Non-finite caller time cannot poison tint values")
            p.setState(.highlighted, on: id(2), at: 11)
            let monotonic = p.colors(at: 11 + fade / 2)
            check(p.colors(at: 10) == monotonic, "A backward caller clock cannot rewind a color transition")
            p.reset(at: 12)
            check(p.colors(at: 12)[id(4)]!.w == 0 && p.state(on: id(2)) == .normal && !p.requiresFrames(at: 12), "Reset cancels pending colors and clears effective pointer/selection state")

            var shared = components
            shared[id(3)] = [button(101, target: 200)]
            let sharedPlayer = try HUDSourceSelectableColor(scene: scene, components: shared)
            sharedPlayer.setState(.highlighted, on: id(2), at: 1)
            let sharedStart = sharedPlayer.colors(at: 1 + fade / 2)[id(4)]!.w
            sharedPlayer.setState(.pressed, on: id(3), at: 1 + fade / 2)
            check(sharedStart == 0.5 && sharedPlayer.colors(at: 1 + fade / 2)[id(4)]!.w == sharedStart,
                "Shared target renderers retain one color channel and cancel the prior owner's tween")
            check(near(sharedPlayer.colors(at: 1 + 1.5 * fade)[id(4)]!.w, 0.2), "The latest shared-renderer transition owns its endpoint")
            var missing = components; missing[id(2)] = [button(100, target: 999)]
            var rejected = false
            do { _ = try HUDSourceSelectableColor(scene: scene, components: missing) } catch { rejected = true }
            check(rejected, "A non-null unresolved source PPtr is diagnosed instead of drawing a guessed tint target")

            // Actual source resources, including separately remapped banner cells.
            let original = try HUDSourceWatchDocument(includeWidgets: false)
            let source = try HUDSourceSelectableColor(document: original)
            check(source.bindings.count == 11 && source.ignoredNullTargetButtonIDs.count == 2,
                "All thirteen original transition-one controls resolve eleven actual targets and two native nulls")
            let banner = HUDSourceID(rawValue: "CAB-194e41a66c2317b9df19269f505210be:6644296005435813079")
            let light = HUDSourceID(rawValue: "CAB-194e41a66c2317b9df19269f505210be:-2394189767403475753")
            let target = HUDSourceID(rawValue: "CAB-194e41a66c2317b9df19269f505210be:1112811429148650711")
            guard let binding = source.bindings.first(where: { $0.buttonNodeID == banner }) else { fatalError("Original BannerCell ColorTint missing") }
            check(binding.targetGraphicID == target && binding.targetNodeID == light && binding.colors.fadeDuration == fade,
                "BannerCell retains exact source target IDs and Float fade duration")
            check(source.colors(at: 0)[light] == SIMD4<Float>(1, 1, 1, 0), "Actual Banner Light initially receives transparent Normal tint")
            source.setState(.highlighted, on: banner, at: 1)
            let vertex = original.components[light]!.first(where: { $0.id == target })!["m_Color"].color
            check(vertex.w == Float(0.2) && near(source.colors(at: 1 + fade / 2)[light]!.w * vertex.w, 0.1),
                "Actual source Light base alpha is preserved and half-hover produces point-one effective alpha")
            let mounted = try HUDSourceWatchDocument()
            let clones = try HUDSourceSelectableColor(document: mounted)
            guard let widgets = mounted.widgets, let cell = widgets.bannerInstances.first,
                  let cloneBinding = clones.bindings.first(where: { $0.buttonNodeID == cell.buttonNodeID }) else {
                fatalError("Remapped original runtime banner ColorTint missing")
            }
            check(cloneBinding.targetNodeID != light && mounted.scene.node(cloneBinding.targetNodeID) != nil,
                "Runtime banner target PPtr resolves to its cloned Light rather than the inactive template")
            clones.setState(.highlighted, on: cell.buttonNodeID, at: 1, reduceMotion: true)
            let cloneColors = clones.colors(at: 1)
            check(cloneColors[cloneBinding.targetNodeID]!.w == 1 && cloneColors[light]!.w == 0,
                "Hovering the runtime banner does not change the original inactive template")
            for other in widgets.bannerInstances.dropFirst() {
                guard let otherBinding = clones.bindings.first(where: { $0.buttonNodeID == other.buttonNodeID }) else {
                    fatalError("Other runtime banner ColorTint missing")
                }
                check(cloneColors[otherBinding.targetNodeID]!.w == 0, "A banner's tint does not leak to another runtime cell")
            }
        } catch { fatalError("Source Selectable color fixture: \(error)") }
        return count
    }
}

private extension SIMD4 where Scalar == Float {
    func replacingAlpha(_ alpha: Float) -> SIMD4<Float> { SIMD4(x, y, z, alpha) }
}
