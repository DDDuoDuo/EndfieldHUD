import AppKit
import UniformTypeIdentifiers

/// Native text entry and image selection bridge the retained profile surface.
/// Both menus are drawn and highlighted by the HUD canvas itself.
final class HUDPersonalProfileInteraction: NSObject, NSTextViewDelegate {
    private let canvas: PersonalProfileCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var unproject: ((CGPoint) -> CGPoint?)?
    var onLock: (() -> Void)?
    var onToggle: (() -> Void)?
    var isDark: (() -> Bool)?
    private var active = false
    private var chooser: NSOpenPanel?
    private var colorPanel: NSColorPanel?
    private var colorCloseObserver: NSObjectProtocol?
    private var editor: HUDProfileField?
    private var projectedEditor: HUDProjectedTextEditor?
    private var editingField: PersonalProfileField?
    private var beginningEdit = false
    private var finishingEdit = false
    private var dialogGeneration = 0
    private var buttons: [String: HUDProfileActionButton] = [:]
    private var sliders: [String: HUDProfileAccessibilitySlider] = [:]
    // HUD popovers capture input independently from parallax. Only a native
    // chooser or an active slider drag holds the surface steady.
    var isInputLocked: Bool { canvas.isDragging || chooser != nil || colorPanel != nil }
    var capturesPointer: Bool { active && canvas.isPopoverOpen }
    var isPresentingPanel: Bool { chooser != nil || editor != nil || colorPanel != nil }

    func hitTestEditor(at point: CGPoint) -> NSView? {
        guard active else { return nil }
        return projectedEditor?.hitTest(point)
    }

    init(canvas: PersonalProfileCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        super.init()
        canvas.onEditField = { [weak self] in self?.beginEditing(field: $0, rect: $1, value: $2) }
        canvas.onChooseImage = { [weak self] in self?.chooseImage(kind: $0) }
        canvas.onChooseColor = { [weak self] in self?.chooseColor($0) }
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
    }
    deinit { editor?.delegate = nil; projectedEditor?.dispose(); clearColorPanelOwnership() }
    func setActive(_ value: Bool) {
        guard active != value else { return }
        if value { active = true; canvas.activate(); layoutAccessibility() } else { deactivate() }
    }
    func deactivate() {
        if !finishEditing() { _ = finishEditing(commit: false) }
        canvas.mouseUp()
        active = false; dialogGeneration += 1
        dismissColorPanel()
        let panel = chooser; chooser = nil; panel?.cancel(nil)
        canvas.deactivate(); buttons.values.forEach { $0.isHidden = true }
        sliders.values.forEach { $0.isHidden = true }
    }
    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, colorPanel == nil else { return false }
        if canvas.isPopoverOpen, !canvas.layer.bounds.contains(point) {
            canvas.dismissPopover()
            return true
        }
        guard canvas.layer.bounds.contains(point) else { return false }
        guard finishEditing() else { return true }
        if canvas.accessibleSliders.contains(where: { $0.rect.contains(point) }) { onLock?() }
        _ = canvas.mouseDown(at: point)
        if editor == nil && chooser == nil && colorPanel == nil { host?.window?.makeFirstResponder(host) }
        return true
    }
    func mouseDragged(to point: CGPoint) { if active { canvas.mouseDragged(to: point) } }
    func mouseUp() { if active { canvas.mouseUp() } }
    func keyDown(_ event: NSEvent) -> Bool {
        guard active, chooser == nil, colorPanel == nil else { return false }
        if editor?.hasMarkedText() == true { return false }
        let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
        if flags.isEmpty, event.keyCode == 53 {
            if editor != nil { _ = finishEditing(commit: false); return true }
            if canvas.isPopoverOpen { canvas.dismissPopover(); return true }
        }
        if flags.isEmpty, [UInt16(36), 76].contains(event.keyCode), editor != nil { _ = finishEditing(); return true }
        if flags.isEmpty, editor == nil, !canvas.accessibleSliders.isEmpty,
           [UInt16(123), 124].contains(event.keyCode) {
            onLock?()
            return canvas.nudgeSlider(event.keyCode == 123 ? -1 : 1)
        }
        return false
    }
    private func chooseImage(kind: UserProfileImageKind) {
        guard active, chooser == nil, colorPanel == nil, let window = host?.window else { return }
        guard finishEditing() else { return }
        onLock?()
        let panel = NSOpenPanel()
        panel.title = kind == .avatar ? L10n.text("Choose profile picture", "选择头像") : L10n.text("Choose profile background", "选择名片背景")
        panel.prompt = L10n.text("Choose", "选择")
        panel.canChooseFiles = true; panel.canChooseDirectories = false; panel.allowsMultipleSelection = false
        panel.allowsOtherFileTypes = false; panel.resolvesAliases = true
        if #available(macOS 11.0, *) { panel.allowedContentTypes = [.image] }
        else { panel.allowedFileTypes = ["png", "jpg", "jpeg", "heic", "tif", "tiff", "gif", "webp"] }
        panel.level = NSWindow.Level(rawValue: max(panel.level.rawValue, window.level.rawValue + 1))
        dialogGeneration += 1; let token = dialogGeneration; chooser = panel
        panel.beginSheetModal(for: window) { [weak self, weak panel] response in
            guard let self, self.dialogGeneration == token else { return }
            self.chooser = nil
            guard self.active, self.host?.window != nil else { return }
            self.host?.window?.makeKeyAndOrderFront(nil)
            if response == .OK, let url = panel?.url { _ = self.canvas.importImage(from: url, kind: kind) }
            self.host?.window?.makeFirstResponder(self.host)
        }
    }
    private func chooseColor(_ initial: NSColor) {
        guard active, colorPanel == nil, chooser == nil, let window = host?.window, finishEditing() else { return }
        onLock?()
        let panel = NSColorPanel.shared; colorPanel = panel
        panel.setTarget(nil); panel.setAction(nil)
        panel.color = initial; panel.showsAlpha = false; panel.mode = .wheel; panel.isContinuous = true
        panel.title = L10n.text("Card color", "名片颜色")
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
    private func beginEditing(field: PersonalProfileField, rect: CGRect, value: String) {
        guard active, canvas.canEdit(field), chooser == nil, colorPanel == nil, let host, host.window != nil else { return }
        guard finishEditing() else { return }
        onLock?(); beginningEdit = true
        defer { beginningEdit = false }
        let control = HUDProfileField(frame: .zero)
        control.string = value; control.isEditable = true; control.isSelectable = true
        control.isRichText = false; control.importsGraphics = false; control.allowsUndo = true
        control.textColor = isDark?() == true ? .white : .black; control.insertionPointColor = control.textColor ?? .black
        control.alignment = field.isNumeric ? .right : .left
        control.font = .systemFont(ofSize: field == .introduction ? 11 : max(10, min(20, rect.height * 0.55)), weight: .semibold)
        control.textContainer?.maximumNumberOfLines = field == .introduction ? 0 : 1
        control.textContainer?.lineBreakMode = field == .introduction ? .byWordWrapping : .byClipping
        control.delegate = self; control.setAccessibilityLabel(field.title)
        control.setAccessibilityHelp(L10n.text("Return saves. Escape cancels.", "回车保存，Esc 取消。"))
        control.onToggle = { [weak self] in self?.onToggle?() }
        editingField = field; editor = control
        let surface = HUDProjectedTextEditor(textView: control, rect: rect, host: host, parent: canvas.layer)
        if field != .introduction { surface.configureSingleLine() }
        surface.project = { [weak self] in self?.project?($0) ?? $0 }
        surface.unproject = { [weak self] point in
            guard let self else { return nil }; return self.unproject?(point) ?? (self.unproject == nil ? point : nil)
        }
        surface.setAppearance(background: NSColor(white: isDark?() == true ? 0.13 : 0.93, alpha: 1), border: canvas.resolvedAccent, radius: 2)
        projectedEditor = surface; surface.resizeDocument(); layoutAccessibility()
        host.window?.makeFirstResponder(control); control.selectAll(nil)
        if !HUDRuntimeAppearance.reduceMotion {
            let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 0.3; fade.toValue = 1; fade.duration = 0.14
            surface.artwork.add(fade, forKey: "profile.editor.open")
        }
    }
    @discardableResult func finishEditing(commit: Bool = true) -> Bool {
        guard !finishingEdit, let editor, let field = editingField else { return true }
        finishingEdit = true
        defer { finishingEdit = false }
        let shouldCommit = commit && canvas.canEdit(field)
        if shouldCommit { editor.unmarkText(); normalizeEditor() }
        if shouldCommit, !canvas.commit(field: field, text: editor.string) {
            projectedEditor?.setAppearance(background: NSColor(white: isDark?() == true ? 0.13 : 0.93, alpha: 1), border: .systemRed, radius: 2)
            editor.setAccessibilityHelp(canvas.accessibilityStatus)
            host?.window?.makeFirstResponder(editor); editor.selectAll(nil); return false
        }
        self.editor = nil; editingField = nil; editor.delegate = nil
        let surface = projectedEditor; projectedEditor = nil
        host?.window?.makeFirstResponder(host); surface?.dispose(); layoutAccessibility(); return true
    }
    func textView(_ textView: NSTextView, doCommandBy commandSelector: Selector) -> Bool {
        guard !textView.hasMarkedText() else { return false }
        if commandSelector == #selector(NSResponder.cancelOperation(_:)) { _ = finishEditing(commit: false); return true }
        if commandSelector == #selector(NSResponder.insertNewline(_:)) || commandSelector == #selector(NSResponder.insertTab(_:)) { _ = finishEditing(); return true }
        return false
    }
    func textDidChange(_ notification: Notification) {
        guard notification.object as? NSTextView === editor else { return }
        normalizeEditor(); projectedEditor?.resizeDocument()
    }
    private func normalizeEditor() {
        guard let editor, !editor.hasMarkedText(), let field = editingField, let limit = field.textLimit else { return }
        let normalized = field == .tag && editor.string.hasPrefix("#") ? String(editor.string.dropFirst()) : editor.string
        let bounded = String(normalized.prefix(limit))
        guard bounded != editor.string else { return }
        let caret = editor.selectedRange().location
        editor.string = bounded
        editor.setSelectedRange(NSRange(location: min(caret, bounded.utf16.count), length: 0))
    }
    func textDidEndEditing(_ notification: Notification) {
        guard !beginningEdit, !finishingEdit, let field = editor, notification.object as? NSTextView === field else { return }
        DispatchQueue.main.async { [weak self, weak field] in
            guard let self, let field, self.active, self.editor === field, !self.beginningEdit, !self.finishingEdit else { return }
            if field.window?.firstResponder === field { return }
            _ = self.finishEditing()
        }
    }
    func layoutAccessibility() {
        guard active, let host else { return }
        if let editingField, !canvas.canEdit(editingField) {
            _ = finishEditing(commit: false)
            return
        }
        defer { HUDControlHighlightLayer.requestRefresh(on: host) }
        projectedEditor?.refreshProjection()
        let actions = canvas.accessibleActions.filter { action in editingField.map { action.id != "profile:" + $0.rawValue } ?? true }
        let wanted = Set(actions.map(\.id))
        for id in Array(buttons.keys) where !wanted.contains(id) { buttons.removeValue(forKey: id)?.removeFromSuperview() }
        for action in actions {
            let button: HUDProfileActionButton
            if let existing = buttons[action.id] { button = existing }
            else {
                button = HUDProfileActionButton(frame: .zero); button.actionID = action.id
                button.title = ""; button.isBordered = false; button.target = self; button.action = #selector(activateAction(_:))
                host.addSubview(button); buttons[action.id] = button
            }
            button.isHidden = false; button.setAccessibilityLabel(action.label); button.setAccessibilityHelp(canvas.accessibilityStatus)
            button.frame = project?(action.rect) ?? action.rect
            button.projectedFrame = { [weak self, weak host] in
                guard let self, let host, let window = host.window else { return .zero }
                return window.convertToScreen(host.convert(self.project?(action.rect) ?? action.rect, to: nil))
            }
        }
        let controls = canvas.accessibleSliders
        let wantedSliders = Set(controls.map { $0.field.rawValue })
        for id in Array(sliders.keys) where !wantedSliders.contains(id) { sliders.removeValue(forKey: id)?.removeFromSuperview() }
        for control in controls {
            let id = control.field.rawValue
            let slider: HUDProfileAccessibilitySlider
            if let existing = sliders[id] { slider = existing }
            else {
                slider = HUDProfileAccessibilitySlider(frame: .zero); slider.controlID = id; slider.isContinuous = true
                slider.target = self; slider.action = #selector(adjustSlider(_:))
                host.addSubview(slider); sliders[id] = slider
            }
            slider.isHidden = false; slider.isEnabled = true
            slider.minValue = control.minimum; slider.maxValue = control.maximum; slider.doubleValue = control.value
            slider.step = control.step
            slider.frame = project?(control.rect) ?? control.rect
            slider.setAccessibilityLabel(control.label); slider.setAccessibilityHelp(control.valueDescription)
            slider.projectedFrame = { [weak self, weak host] in
                guard let self, let host, let window = host.window else { return .zero }
                return window.convertToScreen(host.convert(self.project?(control.rect) ?? control.rect, to: nil))
            }
        }
    }
    @objc private func activateAction(_ sender: HUDProfileActionButton) {
        guard active, finishEditing() else { return }
        canvas.perform(actionID: sender.actionID)
    }
    @objc private func adjustSlider(_ sender: HUDProfileAccessibilitySlider) {
        guard active, sender.isEnabled, chooser == nil, colorPanel == nil,
              let field = PersonalProfileField(rawValue: sender.controlID), finishEditing() else { return }
        onLock?(); _ = canvas.setSlider(field: field, value: sender.doubleValue)
    }
}

private final class HUDProfileActionButton: NSButton {
    override func draw(_ dirtyRect: NSRect) {}
    var actionID = ""
    var projectedFrame: (() -> CGRect)?
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}
private final class HUDProfileField: HUDProjectedTextView {
    var onToggle: (() -> Void)?
    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        if SummonShortcut.active.matches(event: event) { onToggle?(); return true }
        return super.performKeyEquivalent(with: event)
    }
}

private final class HUDProfileAccessibilitySlider: NSSlider {
    var controlID = ""
    var step: Double = 1
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
