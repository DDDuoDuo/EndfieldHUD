import AppKit

/// Keeps pointer input in projected canvas coordinates, and provides native
/// accessibility controls without adding a separate preferences window.
final class HUDSettingsInteraction: NSObject {
    let canvas: HUDSettingsCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var onLock: (() -> Void)?
    private var active = false
    private var buttons: [String: HUDSettingsActionButton] = [:]
    private var sliders: [String: HUDSettingsAccessibilitySlider] = [:]
    private var colorPanel: NSColorPanel?
    private var colorCloseObserver: NSObjectProtocol?
    var isInputLocked: Bool { canvas.isDragging || canvas.isCapturingShortcut || colorPanel != nil }
    var isPresentingPanel: Bool { colorPanel != nil }
    var isCapturingShortcut: Bool { active && canvas.isCapturingShortcut }

    init(canvas: HUDSettingsCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        super.init()
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
        canvas.onChooseColor = { [weak self] in self?.chooseColor($0) }
        canvas.onCaptureChanged = { [weak self] _ in self?.host?.window?.makeFirstResponder(self?.host) }
    }
    deinit {
        if let colorCloseObserver { NotificationCenter.default.removeObserver(colorCloseObserver) }
        if let colorPanel { colorPanel.setTarget(nil); colorPanel.setAction(nil) }
    }
    func setActive(_ value: Bool) {
        guard value != active else { return }
        if value { active = true; canvas.activate() }
        else { deactivate() }
    }
    func deactivate() {
        active = false; dismissColorPanel(); canvas.deactivate()
        buttons.values.forEach { $0.isHidden = true }
        sliders.values.forEach { $0.isHidden = true }
    }
    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, canvas.layer.bounds.contains(point), colorPanel == nil else { return false }
        onLock?(); _ = canvas.mouseDown(at: point)
        host?.window?.makeFirstResponder(host)
        return true
    }
    func mouseDragged(to point: CGPoint) { if active { canvas.mouseDragged(to: point) } }
    func mouseUp() { if active { canvas.mouseUp() } }
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard active, colorPanel == nil else { return false }
        return canvas.scroll(at: point, delta: delta)
    }
    func keyDown(_ event: NSEvent) -> Bool {
        guard active, colorPanel == nil else { return false }
        if canvas.isCapturingShortcut { return canvas.capture(event) }
        let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
        guard flags.isEmpty else { return false }
        switch event.keyCode {
        case 53: return canvas.escape()
        case 123: onLock?(); canvas.nudgeSlider(-1); return true
        case 124: onLock?(); canvas.nudgeSlider(1); return true
        case 125: return canvas.scroll(at: CGPoint(x: 200, y: 165), delta: 34)
        case 126: return canvas.scroll(at: CGPoint(x: 200, y: 165), delta: -34)
        default: return false
        }
    }

    private func chooseColor(_ initial: NSColor) {
        guard active, colorPanel == nil, let window = host?.window else { return }
        onLock?()
        let panel = NSColorPanel.shared
        colorPanel = panel
        panel.setTarget(nil); panel.setAction(nil)
        panel.color = initial; panel.showsAlpha = false; panel.mode = .wheel; panel.isContinuous = true
        panel.title = L10n.text("Theme color", "主题颜色")
        panel.level = NSWindow.Level(rawValue: max(NSWindow.Level.floating.rawValue, window.level.rawValue + 1))
        panel.setTarget(self); panel.setAction(#selector(colorChanged(_:)))
        colorCloseObserver = NotificationCenter.default.addObserver(forName: NSWindow.willCloseNotification, object: panel, queue: .main) { [weak self] _ in
            guard let self else { return }
            self.clearColorPanelOwnership()
            if self.active { self.host?.window?.makeKeyAndOrderFront(nil); self.host?.window?.makeFirstResponder(self.host) }
        }
        panel.makeKeyAndOrderFront(nil)
    }
    @objc private func colorChanged(_ sender: NSColorPanel) {
        guard active, sender === colorPanel else { return }
        canvas.setCustomColor(sender.color)
    }
    private func dismissColorPanel() {
        guard let panel = colorPanel else { return }
        clearColorPanelOwnership(); panel.orderOut(nil)
    }
    private func clearColorPanelOwnership() {
        colorPanel?.setTarget(nil); colorPanel?.setAction(nil); colorPanel = nil
        if let colorCloseObserver { NotificationCenter.default.removeObserver(colorCloseObserver) }
        colorCloseObserver = nil
    }

    func layoutAccessibility() {
        guard active, let host else { return }
        defer { HUDControlHighlightLayer.requestRefresh(on: host) }
        let actions = canvas.accessibleActions
        let wanted = Set(actions.map(\.id))
        for id in Array(buttons.keys) where !wanted.contains(id) { buttons.removeValue(forKey: id)?.removeFromSuperview() }
        for action in actions {
            let button: HUDSettingsActionButton
            if let existing = buttons[action.id] { button = existing }
            else {
                button = HUDSettingsActionButton(frame: .zero); button.actionID = action.id
                button.isBordered = false; button.title = ""; button.target = self; button.action = #selector(activateAction(_:))
                host.addSubview(button); buttons[action.id] = button
            }
            button.isHidden = false; button.isEnabled = action.enabled
            button.frame = project?(action.rect) ?? action.rect
            button.setAccessibilityLabel(action.label); button.setAccessibilityHelp(canvas.accessibilityStatus)
            button.projectedFrame = screenFrame(action.rect)
        }
        let controls = canvas.accessibleSliders
        let sliderIDs = Set(controls.map(\.id))
        for id in Array(sliders.keys) where !sliderIDs.contains(id) { sliders.removeValue(forKey: id)?.removeFromSuperview() }
        for control in controls {
            let slider: HUDSettingsAccessibilitySlider
            if let existing = sliders[control.id] { slider = existing }
            else {
                slider = HUDSettingsAccessibilitySlider(frame: .zero); slider.controlID = control.id; slider.isContinuous = true
                slider.target = self; slider.action = #selector(adjustSlider(_:)); host.addSubview(slider); sliders[control.id] = slider
            }
            slider.isHidden = false; slider.isEnabled = true
            slider.minValue = control.minimum; slider.maxValue = control.maximum; slider.doubleValue = control.value
            slider.step = control.id == "duration" ? 1 : control.id == "uiScale" ? 0.05 : control.id.hasPrefix("position") ? 0.01 : (control.maximum - control.minimum) * 0.02
            slider.frame = project?(control.rect) ?? control.rect; slider.projectedFrame = screenFrame(control.rect)
            slider.setAccessibilityLabel(control.label); slider.setAccessibilityHelp(control.valueDescription)
        }
    }
    private func screenFrame(_ rect: CGRect) -> () -> CGRect {
        { [weak self] in
            guard let self, let host = self.host, let window = host.window else { return .zero }
            return window.convertToScreen(host.convert(self.project?(rect) ?? rect, to: nil))
        }
    }
    @objc private func activateAction(_ sender: HUDSettingsActionButton) {
        guard active, sender.isEnabled else { return }
        onLock?(); canvas.perform(actionID: sender.actionID)
        if colorPanel == nil { host?.window?.makeFirstResponder(host) }
    }
    @objc private func adjustSlider(_ sender: HUDSettingsAccessibilitySlider) {
        guard active, sender.isEnabled else { return }
        onLock?(); _ = canvas.setSlider(id: sender.controlID, value: sender.doubleValue)
    }
}

private final class HUDSettingsActionButton: NSButton {
    override func draw(_ dirtyRect: NSRect) {}
    var actionID = ""
    var projectedFrame: (() -> CGRect)?
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}

private final class HUDSettingsAccessibilitySlider: NSSlider {
    var controlID = ""
    var step: Double = 0.02
    var projectedFrame: (() -> CGRect)?
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
    override func setAccessibilityValue(_ value: Any?) {
        guard isEnabled, let number = value as? NSNumber, number.doubleValue.isFinite else { return }
        doubleValue = min(maxValue, max(minValue, number.doubleValue)); _ = sendAction(action, to: target)
    }
    override func accessibilityPerformIncrement() -> Bool { adjust(1) }
    override func accessibilityPerformDecrement() -> Bool { adjust(-1) }
    private func adjust(_ direction: Double) -> Bool {
        guard isEnabled else { return false }
        setAccessibilityValue(NSNumber(value: doubleValue + direction * step)); return true
    }
}
