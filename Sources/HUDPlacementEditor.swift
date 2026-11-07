import AppKit
import QuartzCore

/// The redesign's place-and-confirm editor: the element being placed follows a
/// drag, a strip beside it holds its sliders and the Cancel and Confirm pills,
/// Esc cancels and Return confirms. The strip is the confirm bar's band and
/// pills (HUDConfirmBand, HUDPill) with HUDSlider rows, so it reads as the
/// same family as every other confirmation.
///
/// Reuse: the battery bar editor (OverlayController) hosts it in the alert's
/// own panel, with Size and Length rows. Placing the HUD's own overlays (the
/// title, the Sanity gauge, the alert tags) is meant to host the same strip
/// on the HUD canvas: add `layer` to that canvas, forward its pointer events
/// (or add `controls` to its HUDInteractionRouter), and position it with
/// `frame(size:beside:within:gap:)`. The host owns what is being placed and
/// its clamping; this type owns the strip, its controls, the key mapping, the
/// strip's placement next to the target and the drag maths.
final class HUDPlacementEditor {
    /// One slider row: its id is reported back through `onChange`.
    struct Slider: Equatable {
        let id: String
        let title: String
        var value: Double
        var range: HUDSlider.Range
        let format: HUDValueFormat
    }

    /// Room a host keeps above the strip for a dragged slider's value bubble.
    static let bubbleClearance: CGFloat = 8
    static let rowPitch: CGFloat = 49
    static let sliderWidth: CGFloat = 236
    static var titleStyle: HUDTextStyle { HUDTextStyle(size: 13, weight: .medium, color: HUDColor.titleText) }

    let layer = HUDLayer.plain("placement.editor")
    let env: HUDDesignEnvironment
    private let band: HUDConfirmBand
    private let marker = HUDConfirmBand.marker()
    private var titles: [HUDTextLayer] = []
    private(set) var sliders: [HUDSlider] = []
    let cancel: HUDPill
    let confirm: HUDPill
    private let router = HUDInteractionRouter()
    private var state = HUDConfirmState()
    private var drag = HUDPlacementDrag()

    /// (slider id, value, final): final on release and on keyboard or VoiceOver steps.
    var onChange: ((String, Double, Bool) -> Void)?
    var onDecision: ((HUDConfirmState.Decision) -> Void)?
    var size: CGSize { layer.bounds.size }
    var controls: [HUDControl] { sliders + [cancel, confirm] }
    /// A strip control holds the pointer (a host keeps the strip still meanwhile).
    var isAdjusting: Bool { router.isTracking }
    var isDragging: Bool { drag.isActive }

    init(id: String, sliders specs: [Slider], env: HUDDesignEnvironment) {
        self.env = env
        cancel = HUDPill(id: id + ":cancel", title: L10n.text("Cancel", "取消"), icon: .x, kind: .cancel, env: env)
        confirm = HUDPill(id: id + ":confirm", title: L10n.text("Confirm", "确认"), icon: .check, kind: .confirm, env: env)
        let titleWidth = max(48, specs.map { ceil(HUDText.width($0.title, Self.titleStyle)) }.max() ?? 0)
        let sliderX = 46 + titleWidth + 14
        let pillWidth = max(cancel.layer.bounds.width, confirm.layer.bounds.width)
        let pillX = (specs.isEmpty ? 46 : sliderX + Self.sliderWidth + 26)
        let size = CGSize(width: pillX + pillWidth + 34, height: max(112, Self.rowPitch * CGFloat(specs.count) + 14))
        band = HUDConfirmBand(size: size)
        for (index, spec) in specs.enumerated() {
            let center = 37 + Self.rowPitch * CGFloat(index)
            let slider = HUDSlider(id: spec.id, label: spec.title, value: spec.value, range: spec.range, format: spec.format,
                                   width: Self.sliderWidth, dark: true, env: env)
            let title = HUDTextLayer(spec.title, style: Self.titleStyle)
            HUDMotion.perform {
                slider.layer.frame.origin = CGPoint(x: sliderX, y: center - 13.5)
                title.lineFrame = CGRect(x: 46, y: center - 8, width: titleWidth + 2, height: 16)
            }
            slider.bubbleHost = layer
            slider.onChange = { [weak self] value, final in self?.onChange?(spec.id, value, final) }
            sliders.append(slider); titles.append(title)
        }
        cancel.onActivate = { [weak self] in self?.decide(.cancel) }
        confirm.onActivate = { [weak self] in self?.decide(.confirm) }
        HUDMotion.perform {
            layer.bounds = CGRect(origin: .zero, size: size)
            layer.addSublayer(band.layer)
            marker.frame.origin = CGPoint(x: 22, y: 28)
            layer.addSublayer(marker)
            titles.forEach(layer.addSublayer)
            sliders.forEach { layer.addSublayer($0.layer) }
            for (index, pill) in [cancel, confirm].enumerated() {
                let pillSize = pill.layer.bounds.size
                pill.layer.frame = CGRect(x: pillX + pillWidth - pillSize.width, y: 18 + CGFloat(index) * 49,
                                          width: pillSize.width, height: pillSize.height)
                layer.addSublayer(pill.layer)
            }
        }
        router.set(controls)
        state.present(HUDConfirmRequest(id: id, title: "", confirmTitle: confirm.label))
        env.applyScale(to: layer)
    }

    /// The confirm bar's entrance: the band wipes open from its centre, then
    /// the rows slide in from the left and the pills from the right.
    func show() {
        HUDMotion.wipe(band.layer, from: .centerX, duration: HUDDuration.bandIn, timing: HUDEasing.snap, env: env)
        HUDMotion.enter(marker, from: CGPoint(x: -12, y: 0), duration: HUDDuration.confirmText,
                        delay: HUDDuration.confirmTextDelay, env: env)
        for (index, row) in zip(titles, sliders).enumerated() {
            for item in [row.0, row.1.layer] {
                HUDMotion.enter(item, from: CGPoint(x: -12, y: 0), duration: HUDDuration.confirmText,
                                delay: HUDDuration.confirmTextDelay + Double(index) * HUDDuration.pillStep, env: env)
            }
        }
        for (index, pill) in [cancel, confirm].enumerated() {
            HUDMotion.enter(pill.layer, from: CGPoint(x: 12, y: 0), duration: HUDDuration.confirmText,
                            delay: HUDDuration.pillDelay + Double(index) * HUDDuration.pillStep, env: env)
        }
    }

    // MARK: Values

    func value(for id: String) -> Double? { sliders.first { $0.id == id }?.value }

    /// A value changed elsewhere (another slider, a display change); no callback.
    func setValue(_ value: Double, for id: String) { sliders.first { $0.id == id }?.setValue(value) }

    /// A range that depends on other values; no callback.
    func setRange(_ range: HUDSlider.Range, value: Double, for id: String) {
        sliders.first { $0.id == id }?.setRange(range, value: value)
    }

    // MARK: Decisions

    /// Esc cancels and Return confirms, once; other keys are left to the host.
    @discardableResult
    func key(_ keyCode: UInt16) -> Bool {
        guard [53, 36, 76].contains(keyCode) else { return false }
        if let decision = state.key(keyCode) { finish(decision) }
        return true
    }

    private func decide(_ decision: HUDConfirmState.Decision) {
        if let decision = state.decide(decision) { finish(decision) }
    }

    private func finish(_ decision: HUDConfirmState.Decision) {
        cancelPointer()
        onDecision?(decision)
    }

    // MARK: Pointer

    /// A press on the strip goes to its controls; a press anywhere else starts
    /// dragging the placed element from `anchor`. `point` is in `root`, the
    /// layer the strip sits in; `pointer` and `anchor` are in the host's
    /// placement space (screen points for a panel that moves with the drag).
    func pointerDown(at point: CGPoint, in root: CALayer, pointer: CGPoint, anchor: CGPoint) {
        guard state.isOpen else { return }
        if stripContains(point, in: root) {
            router.pointerDown(at: point, in: root)
            updateHighlights(in: root, point: point, pressed: true)
        } else {
            drag.begin(anchor: anchor, pointer: pointer)
        }
    }

    /// The placed element's proposed anchor while dragging; nil otherwise.
    func pointerDragged(to point: CGPoint, in root: CALayer, pointer: CGPoint) -> CGPoint? {
        if router.isTracking { router.pointerDragged(to: point, in: root); return nil }
        return drag.anchor(for: pointer)
    }

    func pointerUp(at point: CGPoint, in root: CALayer) {
        if router.isTracking { router.pointerUp(at: point, in: root) }
        drag.end()
        updateHighlights(in: root, point: point)
    }

    /// Ends any gesture without committing it (the editor closes, focus leaves).
    func cancelPointer() {
        sliders.forEach { $0.cancelDrag() }
        router.cancel()
        drag.end()
        controls.forEach { $0.clearInteractionState() }
    }

    func stripContains(_ point: CGPoint, in root: CALayer) -> Bool {
        layer.isVisible(in: root) && layer.contains(layer.convert(point, from: root))
    }

    /// Hover and press feedback for the strip's controls only.
    func updateHighlights(in root: CALayer, point: CGPoint?, pressed: Bool = false) {
        HUDControlHighlightLayer.update(in: layer, point: point.map { layer.convert($0, from: root) }, pressed: pressed)
    }

    // MARK: Accessibility

    func accessibilityItems(in root: CALayer) -> [HUDAccessibilityItem] { router.accessibilityItems(in: root) }

    /// A VoiceOver press on a pill.
    func perform(_ id: String) { controls.first { $0.id == id }?.activate() }

    /// A VoiceOver adjustment of a slider (reported as final).
    func adjust(_ id: String, to value: Double) {
        guard let slider = sliders.first(where: { $0.id == id }) else { return }
        slider.setValue(value)
        onChange?(id, slider.value, true)
    }

    // MARK: Placement

    /// Where the strip goes beside `target` (the visible element), in a y-down
    /// space: centred `gap` below it, or above it when only that fits, and
    /// kept inside `bounds` (centred on it when the strip is wider).
    static func frame(size: CGSize, beside target: CGRect, within bounds: CGRect, gap: CGFloat) -> CGRect {
        var y = target.maxY + gap
        if y + size.height > bounds.maxY, target.minY - gap - size.height >= bounds.minY {
            y = target.minY - gap - size.height
        }
        y = max(bounds.minY, min(y, bounds.maxY - size.height))
        let x = size.width > bounds.width ? bounds.midX - size.width / 2
            : max(bounds.minX, min(target.midX - size.width / 2, bounds.maxX - size.width))
        return CGRect(origin: CGPoint(x: x, y: y), size: size)
    }
}

/// The drag maths shared by placement editors: the element keeps its offset
/// from the pointer from where the press began. The host clamps the result.
struct HUDPlacementDrag: Equatable {
    private var startAnchor: CGPoint?
    private var startPointer: CGPoint?
    var isActive: Bool { startAnchor != nil }

    mutating func begin(anchor: CGPoint, pointer: CGPoint) { startAnchor = anchor; startPointer = pointer }

    func anchor(for pointer: CGPoint) -> CGPoint? {
        guard let startAnchor, let startPointer else { return nil }
        return CGPoint(x: startAnchor.x + pointer.x - startPointer.x, y: startAnchor.y + pointer.y - startPointer.y)
    }

    mutating func end() { startAnchor = nil; startPointer = nil }
}
