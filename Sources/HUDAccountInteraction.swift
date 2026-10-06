import AppKit

/// Pointer input remains on the module's projected plane. Native controls are
/// transparent accessibility peers, never a separate, untilted preferences UI.
final class HUDAccountInteraction: NSObject {
    let canvas: HUDAccountCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?, unproject: ((CGPoint) -> CGPoint?)?
    private var active = false, presented = false
    private var buttons: [String: AccountAXButton] = [:]
    var capturesPointer: Bool { active && canvas.isPopoverOpen }
    var isPresentingPanel: Bool { false }
    var isInputLocked: Bool { false }
    init(canvas: HUDAccountCanvas, host: NSView) {
        self.canvas = canvas; self.host = host; super.init()
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
    }
    deinit { buttons.values.forEach { $0.removeFromSuperview() } }
    func setPresented(_ value: Bool) {
        guard presented != value else { return }; presented = value
        if !value { setActive(false) }; canvas.setVisible(value)
    }
    func setActive(_ value: Bool) {
        guard active != value else { return }; active = value
        if value { setPresented(true); layoutAccessibility() }
        else { canvas.dismissPopover(animated: false); buttons.values.forEach { $0.isHidden = true } }
    }
    func deactivate() { setActive(false); setPresented(false) }
    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active else { return false }
        let handled = canvas.mouseDown(at: point)
        if handled { host?.window?.makeFirstResponder(host) }
        return handled
    }
    func mouseDragged(to point: CGPoint) {}
    func mouseUp() {}
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        active && canvas.scroll(at: point, delta: delta)
    }
    func keyDown(_ event: NSEvent) -> Bool {
        guard active, canvas.isPopoverOpen, event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        switch event.keyCode {
        case 53: canvas.dismissPopover()
        case 125: canvas.moveMenuSelection(1)
        case 126: canvas.moveMenuSelection(-1)
        case 36, 49, 76: canvas.activateMenuSelection()
        default: return false
        }
        return true
    }
    func layoutAccessibility() {
        guard active, let host else { return }
        let controls = canvas.accessibleActions, ids = Set(controls.map(\.id))
        for id in Array(buttons.keys) where !ids.contains(id) { buttons.removeValue(forKey: id)?.removeFromSuperview() }
        for control in controls {
            let button = buttons[control.id] ?? AccountAXButton(frame: .zero)
            if buttons[control.id] == nil { host.addSubview(button); buttons[control.id] = button }
            button.title = ""; button.isBordered = false; button.isHidden = false; button.isEnabled = control.enabled
            button.frame = project?(control.rect) ?? control.rect
            button.setAccessibilityLabel(control.label)
            button.onPress = { [weak self] in self?.canvas.perform(control.id) }
        }
        HUDControlHighlightLayer.requestRefresh(on: host)
    }
}

private final class AccountAXButton: NSButton {
    var onPress: (() -> Void)?
    override init(frame frameRect: NSRect) { super.init(frame: frameRect); target = self; action = #selector(activate) }
    required init?(coder: NSCoder) { nil }
    @objc private func activate() { onPress?() }
    override func accessibilityPerformPress() -> Bool { guard isEnabled else { return false }; onPress?(); return true }
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}
