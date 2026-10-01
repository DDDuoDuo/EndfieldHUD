import Foundation
import simd

/// Original Selectable ColorTint, separate from Graphic.m_Color and Animator curves.
/// The caller supplies an unscaled, monotonic clock. Continuous host sampling replaces
/// Unity's per-frame Float elapsed accumulation; this is not a coroutine scheduler.
final class HUDSourceSelectableColor {
    enum State: Int, CaseIterable { case normal = 0, highlighted, pressed, selected, disabled }

    struct ColorBlock {
        let normal: SIMD4<Float>
        let highlighted: SIMD4<Float>
        let pressed: SIMD4<Float>
        let selected: SIMD4<Float>
        let disabled: SIMD4<Float>
        let multiplier: Float
        let fadeDuration: Double

        func color(for state: State) -> SIMD4<Float> {
            let color: SIMD4<Float>
            switch state {
            case .normal: color = normal
            case .highlighted: color = highlighted
            case .pressed: color = pressed
            case .selected: color = selected
            case .disabled: color = disabled
            }
            return color * multiplier
        }
    }
    struct Binding {
        let buttonNodeID: HUDSourceID
        let buttonComponentID: HUDSourceID
        let targetGraphicID: HUDSourceID
        let targetNodeID: HUDSourceID
        let sourceInteractable: Bool
        let colors: ColorBlock
    }
    private struct Instance { var state: State; var enabled = true }
    private struct Tween {
        let start: SIMD4<Float>
        let target: SIMD4<Float>
        let started: Double
        let duration: Double

        func progress(at time: Double) -> Float {
            guard duration > 0 else { return 1 }
            return min(1, max(0, Float((time - started) / duration)))
        }
        func color(at time: Double) -> SIMD4<Float> {
            let t = progress(at: time)
            // Source ColorTween::TweenValue uses subss, mulss, addss per channel.
            let difference = target - start
            let scaled = difference * t
            return start + scaled
        }
    }

    let bindings: [Binding]
    /// Native StartColorTween ignores a null target rather than selecting a Graphic.
    let ignoredNullTargetButtonIDs: [HUDSourceID]
    private let byButton: [HUDSourceID: Binding]
    private var instances: [HUDSourceID: Instance]
    // CanvasRenderer tint belongs to the target Graphic's node. If several
    // Selectables share one renderer, the latest transition cancels its tween.
    private var renderers: [HUDSourceID: Tween]
    private var clock: Double?

    convenience init(document: HUDSourceWatchDocument) throws {
        try self.init(scene: document.scene, components: document.components)
    }

    init(scene: HUDSourceScene, components: [HUDSourceID: [HUDSourceWatchComponent]]) throws {
        var targetNodes: [HUDSourceID: HUDSourceID] = [:]
        for node in scene.traversalIDs {
            for component in components[node] ?? [] {
                guard targetNodes.updateValue(node, forKey: component.id) == nil else {
                    throw HUDSourceError.invalid("Duplicate source ColorTint component ID")
                }
            }
        }
        func sourceColor(_ source: HUDSourceJSONValue) throws -> SIMD4<Float> {
            let keys = ["r", "g", "b", "a"]
            guard keys.allSatisfy({ source[$0].number != nil && Float(source[$0].float()).isFinite }) else {
                throw HUDSourceError.invalid("Invalid source Selectable color")
            }
            return source.color
        }
        var list: [Binding] = [], ignored: [HUDSourceID] = []
        var lookup: [HUDSourceID: Binding] = [:], values: [HUDSourceID: Instance] = [:]
        var channels: [HUDSourceID: Tween] = [:]
        for node in scene.traversalIDs {
            for component in components[node] ?? [] where component.enabled {
                guard ["UIButton", "Button", "Selectable"].contains(component.kind),
                      component["m_Transition"].number == 1 else { continue }
                guard let target = component["m_TargetGraphic"].targetID else {
                    ignored.append(node); continue
                }
                guard let targetNode = targetNodes[target] else {
                    throw HUDSourceError.invalid("Missing source ColorTint target Graphic")
                }
                let source = component["m_Colors"]
                guard let multiplierValue = source["m_ColorMultiplier"].number,
                      let durationValue = source["m_FadeDuration"].number,
                      Float(multiplierValue).isFinite, Float(durationValue).isFinite, durationValue >= 0 else {
                    throw HUDSourceError.invalid("Invalid source Selectable ColorBlock")
                }
                let colors = try ColorBlock(normal: sourceColor(source["m_NormalColor"]),
                    highlighted: sourceColor(source["m_HighlightedColor"]),
                    pressed: sourceColor(source["m_PressedColor"]), selected: sourceColor(source["m_SelectedColor"]),
                    disabled: sourceColor(source["m_DisabledColor"]), multiplier: Float(multiplierValue),
                    fadeDuration: Double(Float(durationValue)))
                let interactable = component["m_Interactable"].flag(true)
                let binding = Binding(buttonNodeID: node, buttonComponentID: component.id,
                    targetGraphicID: target, targetNodeID: targetNode, sourceInteractable: interactable, colors: colors)
                guard lookup.updateValue(binding, forKey: node) == nil else {
                    throw HUDSourceError.invalid("Several source ColorTint Selectables share one button node")
                }
                let initial: State = interactable ? .normal : .disabled
                list.append(binding); values[node] = Instance(state: initial)
                let color = colors.color(for: initial)
                channels[targetNode] = Tween(start: color, target: color, started: 0, duration: 0)
            }
        }
        bindings = list; ignoredNullTargetButtonIDs = ignored; byButton = lookup
        instances = values; renderers = channels
    }

    var instanceIDs: [HUDSourceID] { bindings.map { $0.buttonNodeID } }
    func state(on id: HUDSourceID) -> State? { instances[id]?.state }

    /// Source OnEnable transitions instantly to current selection state. This
    /// host reset establishes no pointer-down/inside/selection, so Normal (or
    /// source non-interactable Disabled) is sampled without an opening fade.
    func reset(at value: Double) {
        guard let time = advance(value) else { return }
        for binding in bindings {
            let state: State = binding.sourceInteractable ? .normal : .disabled
            instances[binding.buttonNodeID] = Instance(state: state)
            assign(binding.colors.color(for: state), to: binding.targetNodeID, at: time)
        }
    }

    /// State is the caller's effective Selectable state, including CanvasGroup
    /// interaction policy. The host must dispatch events only to active controls.
    func setState(_ state: State, on id: HUDSourceID, at value: Double, reduceMotion: Bool = false) {
        guard let time = advance(value), let binding = byButton[id], var instance = instances[id],
              instance.enabled else { return }
        instance.state = state; instances[id] = instance
        let current = renderers[binding.targetNodeID]!.color(at: time)
        let target = binding.colors.color(for: state)
        // Graphic::CrossFadeColor cancels even when the requested target equals
        // the renderer's current color. It never changes serialized m_Color.
        let duration = reduceMotion || current == target ? 0 : binding.colors.fadeDuration
        renderers[binding.targetNodeID] = Tween(start: current, target: target, started: time, duration: duration)
    }

    /// Disabling the component is distinct from its Disabled selection state:
    /// source OnDisable/InstantClearState resets CanvasRenderer tint to white.
    func setEnabled(_ enabled: Bool, on id: HUDSourceID, at value: Double) {
        guard let time = advance(value), let binding = byButton[id], var instance = instances[id],
              instance.enabled != enabled else { return }
        instance.enabled = enabled
        instance.state = binding.sourceInteractable ? .normal : .disabled
        instances[id] = instance
        assign(enabled ? binding.colors.color(for: instance.state) : SIMD4<Float>(repeating: 1),
               to: binding.targetNodeID, at: time)
    }

    /// Results are the renderer tint channel, keyed by target Graphic node ID.
    /// RGB conversion and mesh color multiplication belong to the render adapter.
    func colors(at value: Double, reduceMotion: Bool = false) -> [HUDSourceID: SIMD4<Float>] {
        let time = advance(value) ?? clock ?? 0
        if reduceMotion {
            for (id, tween) in renderers { assign(tween.target, to: id, at: time) }
        }
        return renderers.mapValues { $0.color(at: time) }
    }

    /// Querying demand does not advance the sampler clock or restart any tween.
    func requiresFrames(at value: Double) -> Bool {
        guard value.isFinite else { return false }
        let time = max(clock ?? value, value)
        return renderers.values.contains { $0.progress(at: time) < 1 }
    }

    private func assign(_ color: SIMD4<Float>, to id: HUDSourceID, at time: Double) {
        renderers[id] = Tween(start: color, target: color, started: time, duration: 0)
    }
    private func advance(_ value: Double) -> Double? {
        guard value.isFinite else { return nil }
        let time = max(clock ?? value, value); clock = time; return time
    }
}
