import AppKit

/// One projected input bridge for the two report canvases. Readouts remain
/// native vector artwork; AppKit contributes keyboard and accessibility only.
protocol HUDTelemetryCanvas: AnyObject {
    var onChange: (() -> Void)? { get set }
    var accessibleActions: [TelemetryCanvasAction] { get }
    var accessibilityStatus: String { get }
    func activate()
    func deactivate()
    func mouseDown(at point: CGPoint) -> Bool
    func perform(actionID: String)
    func cancelDetail() -> Bool
}

extension StorageCanvas: HUDTelemetryCanvas {}
extension ActivityMonitorCanvas: HUDTelemetryCanvas {}

final class HUDTelemetryInteraction: NSObject {
    private let canvas: HUDTelemetryCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var onLock: (() -> Void)?
    private var active = false
    private var pressed = false
    private var buttons: [String: HUDTelemetryActionButton] = [:]
    private let status = HUDTelemetryStatusView(frame: .zero)
    var isInputLocked: Bool { pressed }

    init(canvas: HUDTelemetryCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        super.init()
        status.setAccessibilityElement(true)
        status.setAccessibilityRole(.staticText)
        status.isHidden = true
        host.addSubview(status)
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
    }

    func setActive(_ value: Bool) {
        guard active != value else { return }
        if value { active = true; canvas.activate(); layoutAccessibility() }
        else { deactivate() }
    }

    func deactivate() {
        active = false; pressed = false; canvas.deactivate()
        status.isHidden = true
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
        guard active, event.keyCode == 53,
              event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        return canvas.cancelDetail()
    }

    func layoutAccessibility() {
        defer { HUDControlHighlightLayer.requestRefresh(on: host) }
        guard active, let host else { return }
        status.isHidden = false
        status.setAccessibilityLabel(canvas.accessibilityStatus)
        let content = CGRect(x: 0, y: 0, width: 400, height: 334)
        status.frame = project?(content) ?? content
        status.projectedFrame = { [weak self, weak host] in
            guard let self, let host, let window = host.window else { return .zero }
            return window.convertToScreen(host.convert(self.project?(content) ?? content, to: nil))
        }
        let actions = canvas.accessibleActions
        let expected = Set(actions.map(\.id))
        for id in Array(buttons.keys) where !expected.contains(id) { buttons.removeValue(forKey: id)?.removeFromSuperview() }
        for action in actions {
            let button: HUDTelemetryActionButton
            if let existing = buttons[action.id] { button = existing }
            else {
                button = HUDTelemetryActionButton(frame: .zero)
                button.actionID = action.id; button.title = ""; button.isBordered = false
                button.focusRingType = .none
                button.target = self; button.action = #selector(activateAction(_:))
                host.addSubview(button); buttons[action.id] = button
            }
            button.isHidden = false; button.setAccessibilityLabel(action.label)
            button.frame = project?(action.rect) ?? action.rect
            button.projectedFrame = { [weak self, weak host] in
                guard let self, let host, let window = host.window else { return .zero }
                return window.convertToScreen(host.convert(self.project?(action.rect) ?? action.rect, to: nil))
            }
        }
    }

    @objc private func activateAction(_ sender: HUDTelemetryActionButton) {
        guard active else { return }
        onLock?(); canvas.perform(actionID: sender.actionID)
    }
}

private final class HUDTelemetryActionButton: NSButton {
    var actionID = ""
    var projectedFrame: (() -> CGRect)?
    // The canvas owns all pixels, including the refresh arrow. The native
    // projection is exclusively an accessibility target, never a second face.
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}

private final class HUDTelemetryStatusView: NSView {
    var projectedFrame: (() -> CGRect)?
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}
