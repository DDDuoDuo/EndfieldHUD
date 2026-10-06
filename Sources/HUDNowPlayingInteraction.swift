import AppKit

/// Native keyboard/AX controls project the canvas's own hit rectangles. They
/// never paint a second face or intercept perspective-aware pointer routing.
final class HUDNowPlayingInteraction: NSObject {
    private let canvas: NowPlayingCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var onLock: (() -> Void)?
    private var active = false
    private var presented = false
    private(set) var presentationStartCountForVerification = 0
    var isPresentedForVerification: Bool { presented }
    var isInteractiveForVerification: Bool { active }
    private var buttons: [String: HUDNowPlayingButton] = [:]
    private var sliders: [String: HUDNowPlayingSlider] = [:]
    var isInputLocked: Bool { canvas.isDragging }
    var isPresentingPanel: Bool { active && canvas.isRequestingPermission }

    init(canvas: NowPlayingCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        super.init()
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
    }
    /// Begin the bounded media subscription while the incoming HUD is revealing.
    /// Native controls remain disabled until its mechanical transition settles.
    func setPresented(_ value: Bool) {
        guard presented != value else { return }
        presented = value
        if value { presentationStartCountForVerification += 1; canvas.activate() }
        else { setActive(false); canvas.deactivate() }
    }
    func setActive(_ value: Bool) {
        guard active != value else { return }
        active = value
        if value { setPresented(true); layoutAccessibility() }
        else { hideControls() }
    }
    func deactivate() {
        setActive(false); setPresented(false)
    }
    private func hideControls() {
        buttons.values.forEach { $0.isHidden = true }; sliders.values.forEach { $0.isHidden = true }
    }
    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, canvas.layer.bounds.contains(point) else { return false }
        onLock?(); _ = canvas.mouseDown(at: point); host?.window?.makeFirstResponder(host); return true
    }
    func mouseDragged(to point: CGPoint) { if active { canvas.mouseDragged(to: point) } }
    func mouseUp() { canvas.mouseUp() }
    func keyDown(_ event: NSEvent) -> Bool {
        guard active, event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        switch event.keyCode {
        case 53 where canvas.capturesPointer: canvas.dismissPopover()
        case 49: onLock?(); canvas.perform(actionID: "playPause")
        case 123, 124:
            guard let seek = canvas.accessibleSliders.first(where: { $0.id == "seek" }), seek.enabled, let value = seek.value else { return false }
            onLock?(); _ = canvas.setSlider(id: "seek", value: value + (event.keyCode == 123 ? -5 : 5))
        default: return false
        }
        return true
    }
    func layoutAccessibility() {
        guard active, let host else { return }
        defer { HUDControlHighlightLayer.requestRefresh(on: host) }
        let actions = canvas.accessibleActions
        let actionIDs = Set(actions.map(\.id))
        for id in Array(buttons.keys) where !actionIDs.contains(id) { buttons.removeValue(forKey: id)?.removeFromSuperview() }
        for action in actions {
            let button: HUDNowPlayingButton
            if let existing = buttons[action.id] { button = existing }
            else {
                button = HUDNowPlayingButton(frame: .zero); button.actionID = action.id
                button.title = ""; button.isBordered = false; button.focusRingType = .none
                button.target = self; button.action = #selector(activateAction(_:)); host.addSubview(button); buttons[action.id] = button
            }
            button.isHidden = false; button.isEnabled = action.enabled; button.setAccessibilityLabel(action.label)
            button.setAccessibilityHelp(canvas.accessibilityStatus); button.frame = project?(action.rect) ?? action.rect
            button.projectedFrame = projectedFrame(action.rect)
        }
        let controls = canvas.accessibleSliders
        let controlIDs = Set(controls.map(\.id))
        for id in Array(sliders.keys) where !controlIDs.contains(id) { sliders.removeValue(forKey: id)?.removeFromSuperview() }
        for control in controls {
            let slider: HUDNowPlayingSlider
            if let existing = sliders[control.id] { slider = existing }
            else {
                slider = HUDNowPlayingSlider(frame: .zero); slider.controlID = control.id
                // Seeking commits once at release; do not send an Apple event
                // for each native slider drag update.
                slider.isContinuous = control.id != "seek"; slider.target = self; slider.action = #selector(adjustSlider(_:))
                host.addSubview(slider); sliders[control.id] = slider
            }
            slider.isHidden = false; slider.isEnabled = control.enabled
            slider.minValue = control.minimum; slider.maxValue = control.maximum; slider.doubleValue = control.value ?? control.minimum
            slider.reportedValue = control.value; slider.setAccessibilityLabel(control.label)
            slider.setAccessibilityHelp(control.help ?? (control.id == "seek" ? NowPlayingCanvas.time(control.value) : canvas.accessibilityStatus))
            slider.frame = project?(control.rect) ?? control.rect; slider.projectedFrame = projectedFrame(control.rect)
        }
    }
    private func projectedFrame(_ rect: CGRect) -> () -> CGRect {
        { [weak self] in
            guard let self, let host = self.host, let window = host.window else { return .zero }
            return window.convertToScreen(host.convert(self.project?(rect) ?? rect, to: nil))
        }
    }
    @objc private func activateAction(_ sender: HUDNowPlayingButton) {
        guard active, sender.isEnabled else { return }; onLock?(); canvas.perform(actionID: sender.actionID)
    }
    @objc private func adjustSlider(_ sender: HUDNowPlayingSlider) {
        guard active, sender.isEnabled else { return }; onLock?(); _ = canvas.setSlider(id: sender.controlID, value: sender.doubleValue)
    }
}

private final class HUDNowPlayingButton: NSButton {
    var actionID = ""
    var projectedFrame: (() -> CGRect)?
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}
private final class HUDNowPlayingSlider: NSSlider {
    var controlID = ""
    var reportedValue: Double?
    var projectedFrame: (() -> CGRect)?
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
    override func accessibilityValue() -> Any? {
        if let reportedValue { return NSNumber(value: reportedValue) }
        return L10n.text("Unavailable", "不可用")
    }
    override func setAccessibilityValue(_ value: Any?) {
        guard isEnabled, let value = value as? NSNumber, value.doubleValue.isFinite else { return }
        doubleValue = min(maxValue, max(minValue, value.doubleValue)); _ = sendAction(action, to: target)
    }
    override func accessibilityPerformIncrement() -> Bool { adjust(1) }
    override func accessibilityPerformDecrement() -> Bool { adjust(-1) }
    private func adjust(_ direction: Double) -> Bool {
        guard isEnabled, reportedValue != nil else { return false }
        setAccessibilityValue(NSNumber(value: doubleValue + direction * (controlID == "seek" ? 5 : 0.02))); return true
    }
}
