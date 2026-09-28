import AppKit

/// Projected hit testing uses the canvas, while real AppKit sliders expose
/// their values and actions to keyboard and accessibility clients.
final class HUDVolumeInteraction: NSObject {
    private let canvas: VolumeCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var onLock: (() -> Void)?
    private var active = false
    private var buttons: [String: HUDVolumeActionButton] = [:]
    private var sliders: [String: HUDVolumeSlider] = [:]
    var isInputLocked: Bool { canvas.isDragging }
    var isPresentingPanel: Bool { false }

    init(canvas: VolumeCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        super.init()
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
    }

    func setActive(_ value: Bool) {
        guard active != value else { return }
        if value { active = true; canvas.activate(); layoutAccessibility() }
        else { deactivate() }
    }

    func deactivate() {
        active = false; canvas.deactivate()
        buttons.values.forEach { $0.isHidden = true }
        sliders.values.forEach { $0.isHidden = true }
    }

    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, CGRect(x: 0, y: 0, width: 400, height: 334).contains(point) else { return false }
        onLock?()
        _ = canvas.mouseDown(at: point)
        host?.window?.makeFirstResponder(host)
        return true
    }
    func mouseDragged(to point: CGPoint) { if active { canvas.mouseDragged(to: point) } }
    func mouseUp() { canvas.mouseUp() }

    func keyDown(_ event: NSEvent) -> Bool {
        guard active, event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        if event.keyCode == 53 { return canvas.dismissChooser() }
        if canvas.isChoosingDevice { return false }
        switch event.keyCode {
        case 123: onLock?(); canvas.nudgeSelectedSlider(by: -1)
        case 124: onLock?(); canvas.nudgeSelectedSlider(by: 1)
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
            let button: HUDVolumeActionButton
            if let existing = buttons[action.id] { button = existing }
            else {
                button = HUDVolumeActionButton(frame: .zero); button.actionID = action.id; button.title = ""; button.isBordered = false
                button.target = self; button.action = #selector(activateAction(_:)); host.addSubview(button); buttons[action.id] = button
            }
            button.isHidden = false; button.isEnabled = action.enabled
            button.setAccessibilityLabel(action.label); button.setAccessibilityHelp(canvas.accessibilityStatus)
            let visible = action.visibleRect ?? action.rect
            button.frame = project?(visible) ?? visible
            button.projectedFrame = projectedFrame(visible)
        }
        let controls = canvas.accessibleSliders
        let sliderIDs = Set(controls.map(\.id))
        for id in Array(sliders.keys) where !sliderIDs.contains(id) { sliders.removeValue(forKey: id)?.removeFromSuperview() }
        for control in controls {
            let slider: HUDVolumeSlider
            if let existing = sliders[control.id] { slider = existing }
            else {
                slider = HUDVolumeSlider(frame: .zero); slider.controlID = control.id; slider.isContinuous = true
                slider.target = self; slider.action = #selector(adjustSlider(_:)); host.addSubview(slider); sliders[control.id] = slider
            }
            slider.isHidden = false; slider.isEnabled = control.enabled
            slider.minValue = control.minimum; slider.maxValue = control.maximum; slider.doubleValue = control.value ?? (control.minimum + control.maximum) / 2
            slider.reportedValue = control.value
            slider.setAccessibilityLabel(control.label)
            let value = control.value.map { control.id != "balance" ? "\(Int(($0 * 100).rounded()))%" : (abs($0) < 0.01 ? L10n.text("Centered", "居中") : "\($0 < 0 ? "L" : "R") \(Int((abs($0) * 100).rounded()))%") }
                ?? L10n.text("Unavailable", "不可用")
            slider.setAccessibilityHelp(value + (control.help.map { " · " + $0 }
                ?? (control.enabled ? "" : L10n.text(" · Not adjustable on this device", " · 此设备不支持调整"))))
            let visible = control.visibleRect ?? control.rect
            slider.frame = project?(visible) ?? visible
            slider.projectedFrame = projectedFrame(visible)
        }
    }

    private func projectedFrame(_ rect: CGRect) -> () -> CGRect {
        { [weak self] in
            guard let self, let host = self.host, let window = host.window else { return .zero }
            return window.convertToScreen(host.convert(self.project?(rect) ?? rect, to: nil))
        }
    }

    @objc private func activateAction(_ sender: HUDVolumeActionButton) {
        guard active, sender.isEnabled else { return }
        onLock?(); canvas.perform(actionID: sender.actionID)
    }
    @objc private func adjustSlider(_ sender: HUDVolumeSlider) {
        guard active, sender.isEnabled else { return }
        onLock?(); _ = canvas.setSlider(id: sender.controlID, value: sender.doubleValue)
    }
}

private final class HUDVolumeActionButton: NSButton {
    override func draw(_ dirtyRect: NSRect) {}
    var actionID = ""
    var projectedFrame: (() -> CGRect)?
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}

private final class HUDVolumeSlider: NSSlider {
    var controlID = ""
    var projectedFrame: (() -> CGRect)?
    var reportedValue: Double?
    override func draw(_ dirtyRect: NSRect) {} // Artwork belongs to the projected canvas.
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
    override func accessibilityValue() -> Any? {
        if let reportedValue { return NSNumber(value: reportedValue) }
        return L10n.text("Unavailable", "不可用")
    }
    override func setAccessibilityValue(_ value: Any?) {
        guard isEnabled, let number = value as? NSNumber, number.doubleValue.isFinite else { return }
        doubleValue = min(maxValue, max(minValue, number.doubleValue))
        _ = sendAction(action, to: target)
    }
    override func accessibilityPerformIncrement() -> Bool { adjustAccessibilityValue(1) }
    override func accessibilityPerformDecrement() -> Bool { adjustAccessibilityValue(-1) }
    private func adjustAccessibilityValue(_ direction: Double) -> Bool {
        guard isEnabled, reportedValue != nil else { return false }
        setAccessibilityValue(NSNumber(value: doubleValue + direction * (maxValue - minValue) * 0.02))
        return true
    }
}
