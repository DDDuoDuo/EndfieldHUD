import AppKit

/// Native keyboard/accessibility routing for the projected history rows.
final class HUDClipboardInteraction: NSObject {
    private let canvas: ClipboardCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var onLock: (() -> Void)?
    private var active = false
    private var pressed = false
    private var buttons: [String: HUDClipboardActionButton] = [:]
    var isInputLocked: Bool { pressed }

    init(canvas: ClipboardCanvas, host: NSView) {
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
        active = false; pressed = false
        canvas.deactivate()
        buttons.values.forEach { $0.isHidden = true }
    }

    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, CGRect(x: 0, y: 0, width: 400, height: 334).contains(point) else { return false }
        onLock?(); pressed = true
        _ = canvas.mouseDown(at: point)
        host?.window?.makeFirstResponder(host)
        return true
    }

    func mouseUp() { pressed = false }

    func keyDown(_ event: NSEvent) -> Bool {
        guard active else { return false }
        let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
        guard flags.isEmpty else { return false }
        switch event.keyCode {
        case 125: onLock?(); canvas.selectNext(1)
        case 126: onLock?(); canvas.selectNext(-1)
        case 36, 76: onLock?(); canvas.copySelection()
        case 51, 117: onLock?(); canvas.deleteSelection()
        default:
            guard let character = event.charactersIgnoringModifiers, let number = Int(character), (1...6).contains(number) else { return false }
            onLock?(); canvas.copyVisibleItem(at: number - 1)
        }
        return true
    }

    func layoutAccessibility() {
        guard active, let host else { return }
        defer { HUDControlHighlightLayer.requestRefresh(on: host) }
        let actions = canvas.accessibleActions
        let help = canvas.accessibilityStatus + L10n.text(". Arrow keys select; Return copies; Delete removes the cached item.", "。方向键选择，回车复制，Delete 删除缓存项目。")
        let expected = Set(actions.map(\.id))
        for id in Array(buttons.keys) where !expected.contains(id) { buttons.removeValue(forKey: id)?.removeFromSuperview() }
        for action in actions {
            let button: HUDClipboardActionButton
            if let existing = buttons[action.id] { button = existing }
            else {
                button = HUDClipboardActionButton(frame: .zero); button.actionID = action.id
                button.title = ""; button.isBordered = false; button.target = self; button.action = #selector(activateAction(_:))
                button.projectedFrame = { [weak self, weak host, weak button] in
                    guard let self, let host, let button, let window = host.window else { return .zero }
                    return window.convertToScreen(host.convert(self.project?(button.sourceRect) ?? button.sourceRect, to: nil))
                }
                host.addSubview(button); buttons[action.id] = button
            }
            button.isHidden = false
            button.sourceRect = action.rect
            if button.accessibilityLabel() != action.label { button.setAccessibilityLabel(action.label) }
            if button.accessibilityHelp() != help { button.setAccessibilityHelp(help) }
            let frame = project?(action.rect) ?? action.rect
            if button.frame != frame { button.frame = frame }
        }
    }

    @objc private func activateAction(_ sender: HUDClipboardActionButton) {
        guard active else { return }
        onLock?(); canvas.perform(actionID: sender.actionID)
    }
}

private final class HUDClipboardActionButton: NSButton {
    override func draw(_ dirtyRect: NSRect) {}
    var actionID = ""
    var sourceRect = CGRect.zero
    var projectedFrame: (() -> CGRect)?
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}
