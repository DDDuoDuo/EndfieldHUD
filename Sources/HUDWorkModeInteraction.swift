import AppKit

/// Native text input is limited to the temporary custom-duration field. Timer
/// controls and all settled content remain inside the shared projected HUD.
final class HUDWorkModeInteraction: NSObject, NSTextFieldDelegate {
    private let canvas: WorkModeCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var onLock: (() -> Void)?
    var onToggle: (() -> Void)?
    private var active = false
    private var pressed = false
    private var editor: HUDWorkDurationField?
    private var editorRect: CGRect?
    private var finishingEdit = false
    private var beginningEdit = false
    private var buttons: [String: HUDWorkActionButton] = [:]
    var isInputLocked: Bool { pressed || editor != nil }
    var isPresentingPanel: Bool { false }

    init(canvas: WorkModeCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        super.init()
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
        canvas.onEditDuration = { [weak self] in self?.beginEditing(rect: $0) }
    }

    func setActive(_ value: Bool) {
        guard active != value else { return }
        if value { active = true; canvas.activate(); layoutAccessibility() }
        else { deactivate() }
    }

    func deactivate() {
        active = false; pressed = false
        _ = finishEditing(commit: false)
        canvas.deactivate()
        buttons.values.forEach { $0.isHidden = true }
    }

    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, WorkModeDialGeometry.canvas.contains(point) else { return false }
        if editor != nil && !finishEditing() { return true }
        onLock?(); pressed = true
        _ = canvas.mouseDown(at: point)
        if editor == nil { host?.window?.makeFirstResponder(host) }
        return true
    }
    func mouseDragged(to point: CGPoint, event: NSEvent) {}
    func mouseUp() { pressed = false }

    func keyDown(_ event: NSEvent) -> Bool {
        guard active else { return false }
        let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
        guard flags.isEmpty else { return false }
        if editor != nil {
            if event.keyCode == 53 { _ = finishEditing(commit: false); return true }
            if event.keyCode == 36 || event.keyCode == 76 { _ = finishEditing(); return true }
            return false
        }
        if event.keyCode == 49 || event.keyCode == 36 || event.keyCode == 76 {
            onLock?()
            switch canvas.controller.snapshot.phase {
            case .running: canvas.perform(actionID: "work:pause")
            case .paused: canvas.perform(actionID: "work:resume")
            case .idle, .stopped, .completed: canvas.perform(actionID: "work:start")
            }
            return true
        }
        return false
    }

    private func beginEditing(rect: CGRect) {
        guard active, !canvas.controller.snapshot.isActive, let host, let window = host.window else { return }
        _ = finishEditing(commit: false)
        beginningEdit = true
        defer { beginningEdit = false }
        onLock?()
        let field = HUDWorkDurationField(frame: project?(rect) ?? rect)
        field.stringValue = WorkModeDuration.editText(canvas.controller.snapshot.duration)
        field.placeholderString = "30:00"
        field.alignment = .center
        field.isEditable = true; field.isSelectable = true
        field.isBezeled = false; field.drawsBackground = true
        field.backgroundColor = NSColor(white: 0.86, alpha: 1)
        field.textColor = NSColor(white: 0.1, alpha: 1)
        field.focusRingType = .none
        field.wantsLayer = true; field.layer?.cornerRadius = 4
        field.layer?.borderWidth = 1.5; field.layer?.borderColor = NSColor.systemYellow.cgColor
        field.delegate = self
        field.setAccessibilityLabel(L10n.text("Custom countdown: minutes and seconds", "自定义倒计时：分和秒"))
        field.setAccessibilityHelp(L10n.text("Enter minutes or min:sec, between 0:01 and 1440:00. Return saves; Escape cancels.", "输入分钟数或分:秒，范围为 0:01 至 1440:00。回车保存，Esc 取消。"))
        field.onToggle = { [weak self] in self?.onToggle?() }
        editorRect = rect; editor = field
        host.addSubview(field)
        layoutAccessibility()
        // selectText acquires the window's shared field editor itself. Sending
        // makeFirstResponder first can end that first editing session while
        // selectText starts another, before the opening click has returned.
        field.selectText(nil)
        trace("editor opened; focused=\(window.firstResponder === field.currentEditor())")
    }

    /// Invalid committed input stays editable. Cancellation always removes the
    /// field; section transitions use cancellation before hiding the canvas.
    @discardableResult func finishEditing(commit: Bool = true) -> Bool {
        guard !finishingEdit, let editor else { return true }
        trace("editor finish; commit=\(commit)")
        finishingEdit = true
        defer { finishingEdit = false }
        if commit && !canvas.setCustomDuration(editor.stringValue) {
            host?.window?.makeFirstResponder(editor)
            editor.selectText(nil)
            return false
        }
        self.editor = nil; editorRect = nil
        editor.delegate = nil
        if !commit { canvas.cancelCustomEditing() }
        host?.window?.makeFirstResponder(host)
        editor.removeFromSuperview()
        layoutAccessibility()
        return true
    }

    func control(_ control: NSControl, textView: NSTextView, doCommandBy commandSelector: Selector) -> Bool {
        if commandSelector == #selector(NSResponder.cancelOperation(_:)) {
            _ = finishEditing(commit: false); return true
        }
        if commandSelector == #selector(NSResponder.insertNewline(_:))
            || commandSelector == #selector(NSResponder.insertTab(_:)) {
            _ = finishEditing(); return true
        }
        return false
    }

    func controlTextDidEndEditing(_ notification: Notification) {
        trace("editor end notification; beginning=\(beginningEdit), finishing=\(finishingEdit)")
        guard !finishingEdit, !beginningEdit, let field = editor,
              notification.object as? NSTextField === field else { return }
        // AppKit can hand the shared field editor back to the same control
        // synchronously. Commit only after that responder transition settles.
        DispatchQueue.main.async { [weak self, weak field] in
            guard let self, let field, self.active, self.editor === field,
                  !self.finishingEdit, !self.beginningEdit else { return }
            if let text = field.currentEditor(), field.window?.firstResponder === text { return }
            _ = self.finishEditing()
        }
    }

    func layoutAccessibility() {
        guard active, let host else { return }
        defer { HUDControlHighlightLayer.requestRefresh(on: host) }
        if editor != nil && canvas.controller.snapshot.isActive {
            _ = finishEditing(commit: false)
        }
        if let editor, let rect = editorRect {
            let frame = project?(rect) ?? rect
            if editor.frame != frame { editor.frame = frame }
            editor.font = .monospacedDigitSystemFont(ofSize: max(18, min(60, 46 * frame.height / rect.height)), weight: .medium)
        }
        let actions = canvas.accessibleActions
        let expected = Set(actions.map(\.id))
        for id in Array(buttons.keys) where !expected.contains(id) { buttons.removeValue(forKey: id)?.removeFromSuperview() }
        for action in actions {
            let button: HUDWorkActionButton
            if let existing = buttons[action.id] { button = existing }
            else {
                button = HUDWorkActionButton(frame: .zero); button.actionID = action.id
                button.title = ""; button.isBordered = false; button.target = self; button.action = #selector(activateAction(_:))
                host.addSubview(button); buttons[action.id] = button
            }
            button.isHidden = false
            button.setAccessibilityLabel(action.label)
            button.setAccessibilityHelp(canvas.accessibilityStatus + L10n.text(". Space starts, pauses or resumes.", "。空格键开始、暂停或继续。"))
            button.frame = project?(action.rect) ?? action.rect
            button.projectedFrame = { [weak self, weak host] in
                guard let self, let host, let window = host.window else { return .zero }
                return window.convertToScreen(host.convert(self.project?(action.rect) ?? action.rect, to: nil))
            }
        }
    }

    @objc private func activateAction(_ sender: HUDWorkActionButton) {
        guard active, finishEditing() else { return }
        onLock?(); canvas.perform(actionID: sender.actionID)
    }

    private func trace(_ message: String) {
        guard CommandLine.arguments.contains("--ui-test") else { return }
        print("Work Mode: " + message)
        fflush(stdout)
    }
}

private final class HUDWorkActionButton: NSButton {
    override func draw(_ dirtyRect: NSRect) {}
    var actionID = ""
    var projectedFrame: (() -> CGRect)?
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}

private final class HUDWorkDurationField: NSTextField {
    var onToggle: (() -> Void)?
    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        if SummonShortcut.active.matches(event: event) {
            onToggle?(); return true
        }
        return super.performKeyEquivalent(with: event)
    }
}
