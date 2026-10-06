import AppKit

/// Native text input is limited to the temporary custom-duration field. Timer
/// controls and all settled content remain inside the shared projected HUD.
final class HUDWorkModeInteraction: NSObject, NSTextViewDelegate {
    private let canvas: WorkModeCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var unproject: ((CGPoint) -> CGPoint?)?
    var onLock: (() -> Void)?
    var onToggle: (() -> Void)?
    private var active = false
    private var pressed = false
    private var editor: HUDWorkDurationField?
    private var projectedEditor: HUDProjectedTextEditor?
    private var finishingEdit = false
    private var beginningEdit = false
    private var buttons: [String: HUDWorkActionButton] = [:]
    var isInputLocked: Bool { pressed }
    var isPresentingPanel: Bool { false }

    init(canvas: WorkModeCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        super.init()
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
        canvas.onEditDuration = { [weak self] in self?.beginEditing(rect: $0) }
    }

    deinit { editor?.delegate = nil; projectedEditor?.dispose() }

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
        if editor?.hasMarkedText() == true { return false }
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
        let field = HUDWorkDurationField(frame: .zero)
        field.string = WorkModeDuration.editText(canvas.controller.snapshot.duration)
        field.alignment = .center; field.isEditable = true; field.isSelectable = true
        field.isRichText = false; field.importsGraphics = false; field.allowsUndo = true
        field.font = .monospacedDigitSystemFont(ofSize: 46, weight: .medium)
        field.textColor = NSColor(white: 0.1, alpha: 1); field.insertionPointColor = .black
        field.textContainer?.maximumNumberOfLines = 1
        field.textContainer?.lineBreakMode = .byClipping
        field.delegate = self
        field.setAccessibilityLabel(L10n.text("Custom countdown: minutes and seconds", "自定义倒计时：分和秒"))
        field.setAccessibilityHelp(L10n.text("Enter minutes or min:sec, between 0:01 and 1440:00. Return saves; Escape cancels.", "输入分钟数或分:秒，范围为 0:01 至 1440:00。回车保存，Esc 取消。"))
        field.onToggle = { [weak self] in self?.onToggle?() }
        editor = field
        let surface = HUDProjectedTextEditor(textView: field, rect: rect, host: host, parent: canvas.layer)
        surface.configureSingleLine()
        surface.project = { [weak self] in self?.project?($0) ?? $0 }
        surface.unproject = { [weak self] point in
            guard let self else { return nil }; return self.unproject?(point) ?? (self.unproject == nil ? point : nil)
        }
        surface.setAppearance(background: NSColor(white: 0.86, alpha: 1), border: .systemYellow, radius: 4)
        projectedEditor = surface; surface.resizeDocument(); layoutAccessibility()
        window.makeFirstResponder(field); field.selectAll(nil)
        trace("editor opened; focused=\(window.firstResponder === field)")
    }

    /// Invalid committed input stays editable. Cancellation always removes the
    /// field; section transitions use cancellation before hiding the canvas.
    @discardableResult func finishEditing(commit: Bool = true) -> Bool {
        guard !finishingEdit, let editor else { return true }
        trace("editor finish; commit=\(commit)")
        finishingEdit = true
        defer { finishingEdit = false }
        if commit && !canvas.setCustomDuration(editor.string) {
            host?.window?.makeFirstResponder(editor)
            editor.selectAll(nil)
            return false
        }
        self.editor = nil
        let surface = projectedEditor; projectedEditor = nil
        editor.delegate = nil
        if !commit { canvas.cancelCustomEditing() }
        host?.window?.makeFirstResponder(host)
        surface?.dispose()
        layoutAccessibility()
        return true
    }

    func textView(_ textView: NSTextView, doCommandBy commandSelector: Selector) -> Bool {
        guard !textView.hasMarkedText() else { return false }
        if commandSelector == #selector(NSResponder.cancelOperation(_:)) {
            _ = finishEditing(commit: false); return true
        }
        if commandSelector == #selector(NSResponder.insertNewline(_:))
            || commandSelector == #selector(NSResponder.insertTab(_:)) {
            _ = finishEditing(); return true
        }
        return false
    }

    func textDidEndEditing(_ notification: Notification) {
        trace("editor end notification; beginning=\(beginningEdit), finishing=\(finishingEdit)")
        guard !finishingEdit, !beginningEdit, let field = editor,
              notification.object as? NSTextView === field else { return }
        // Commit after the responder transition settles; a refocused editor
        // must keep its text and composition intact.
        DispatchQueue.main.async { [weak self, weak field] in
            guard let self, let field, self.active, self.editor === field,
                  !self.finishingEdit, !self.beginningEdit else { return }
            if field.window?.firstResponder === field { return }
            _ = self.finishEditing()
        }
    }

    func textDidChange(_ notification: Notification) {
        guard notification.object as? NSTextView === editor else { return }
        projectedEditor?.resizeDocument()
    }

    func layoutAccessibility() {
        guard active, let host else { return }
        defer { HUDControlHighlightLayer.requestRefresh(on: host) }
        if editor != nil && canvas.controller.snapshot.isActive {
            _ = finishEditing(commit: false)
        }
        projectedEditor?.refreshProjection()
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
            button.setAccessibilityHelp(action.id == "work:focusAccess" ? action.label
                : canvas.accessibilityStatus + L10n.text(". Space starts, pauses or resumes.", "。空格键开始、暂停或继续。"))
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

private final class HUDWorkDurationField: HUDProjectedTextView {
    var onToggle: (() -> Void)?
    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        if SummonShortcut.active.matches(event: event) {
            onToggle?(); return true
        }
        return super.performKeyEquivalent(with: event)
    }
}
