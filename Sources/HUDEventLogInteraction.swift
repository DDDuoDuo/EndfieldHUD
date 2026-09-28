import AppKit

/// Keyboard and accessibility controls follow the HUD's projected coordinates.
final class HUDEventLogInteraction: NSObject {
    private let canvas: EventLogCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var onLock: (() -> Void)?
    private var active = false
    private var pressed = false
    private var buttons: [String: HUDEventLogActionButton] = [:]
    var isInputLocked: Bool { pressed }

    init(canvas: EventLogCanvas, host: NSView) {
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
        active = false; pressed = false; canvas.deactivate()
        buttons.values.forEach { $0.isHidden = true }
    }
    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, CGRect(x: 0, y: 0, width: 400, height: 334).contains(point) else { return false }
        onLock?(); pressed = true; _ = canvas.mouseDown(at: point)
        host?.window?.makeFirstResponder(host); return true
    }
    func mouseUp() { pressed = false }
    func keyDown(_ event: NSEvent) -> Bool {
        guard active, event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        switch event.keyCode {
        case 125: onLock?(); canvas.selectNext(1)
        case 126: onLock?(); canvas.selectNext(-1)
        case 121: onLock?(); canvas.scrollBy(EventLogCanvas.viewport.height - 30)
        case 116: onLock?(); canvas.scrollBy(-EventLogCanvas.viewport.height + 30)
        case 115: onLock?(); canvas.scrollBy(-CGFloat.greatestFiniteMagnitude)
        case 119: onLock?(); canvas.scrollBy(CGFloat.greatestFiniteMagnitude)
        case 53: return canvas.cancelConfirmation()
        default: return false
        }
        return true
    }
    func layoutAccessibility() {
        guard active, let host else { return }
        defer { HUDControlHighlightLayer.requestRefresh(on: host) }
        let actions = canvas.accessibleActions, expected = Set(canvas.accessibleActions.map(\.id))
        for id in Array(buttons.keys) where !expected.contains(id) { buttons.removeValue(forKey: id)?.removeFromSuperview() }
        for action in actions {
            let button: HUDEventLogActionButton
            if let existing = buttons[action.id] { button = existing }
            else {
                button = HUDEventLogActionButton(frame: .zero); button.actionID = action.id; button.title = ""
                button.isBordered = false; button.target = self; button.action = #selector(activateAction(_:))
                host.addSubview(button); buttons[action.id] = button
            }
            button.isHidden = false; button.setAccessibilityLabel(action.label)
            button.setAccessibilityHelp(canvas.accessibilityStatus + ". Arrow keys select events; Page Up and Page Down scroll.")
            button.frame = project?(action.rect) ?? action.rect
            button.projectedFrame = { [weak self, weak host] in
                guard let self, let host, let window = host.window else { return .zero }
                return window.convertToScreen(host.convert(self.project?(action.rect) ?? action.rect, to: nil))
            }
        }
    }
    @objc private func activateAction(_ sender: HUDEventLogActionButton) {
        guard active else { return }
        onLock?(); canvas.perform(actionID: sender.actionID)
    }
}

private final class HUDEventLogActionButton: NSButton {
    override func draw(_ dirtyRect: NSRect) {}
    var actionID = ""
    var projectedFrame: (() -> CGRect)?
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}
