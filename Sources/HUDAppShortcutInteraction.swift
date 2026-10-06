import AppKit
import UniformTypeIdentifiers

/// The application chooser and temporary name field are the only native
/// controls; cards, icon choices and the saved state stay inside the HUD.
final class HUDAppShortcutInteraction: NSObject, NSTextViewDelegate {
    private let canvas: AppShortcutCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var unproject: ((CGPoint) -> CGPoint?)?
    var onLock: (() -> Void)?
    var onToggle: (() -> Void)?
    var isDark: (() -> Bool)?
    private var active = false
    private var pressed = false
    private var externalDrag = false
    private var chooser: NSOpenPanel?
    private var dialogGeneration = 0
    private var editor: HUDShortcutNameField?
    private var projectedEditor: HUDProjectedTextEditor?
    private var beginningEdit = false
    private var finishingEdit = false
    private var buttons: [String: HUDShortcutActionButton] = [:]
    var isInputLocked: Bool { pressed || externalDrag || chooser != nil }
    var isPresentingPanel: Bool { chooser != nil }

    init(canvas: AppShortcutCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        super.init()
        canvas.onChooseApplication = { [weak self] in self?.chooseApplication() }
        canvas.onEditName = { [weak self] in self?.beginEditing(rect: $0, name: $1) }
        canvas.onWillTransition = { [weak self] in self?.finishEditing() }
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
    }
    deinit { editor?.delegate = nil; projectedEditor?.dispose() }
    func setActive(_ value: Bool) {
        guard active != value else { return }
        if value { active = true; canvas.activate(); layoutAccessibility() }
        else { deactivate() }
    }
    func deactivate() {
        finishEditing(); active = false; pressed = false; externalDrag = false
        dialogGeneration += 1
        let panel = chooser; chooser = nil; panel?.cancel(nil)
        canvas.deactivate(); buttons.values.forEach { $0.isHidden = true }
    }
    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, canvas.layer.bounds.contains(point) else { return false }
        finishEditing(); onLock?(); pressed = true
        _ = canvas.mouseDown(at: point)
        if editor == nil && chooser == nil { host?.window?.makeFirstResponder(host) }
        return true
    }
    func mouseUp() { pressed = false }
    func mouseDragged(to point: CGPoint, event: NSEvent) {}
    func keyDown(_ event: NSEvent) -> Bool {
        guard active, chooser == nil else { return false }
        if editor?.hasMarkedText() == true { return false }
        let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
        if flags.isEmpty && event.keyCode == 53 {
            if editor != nil { finishEditing(commit: false); return true }
            if canvas.isEditing { canvas.cancelDraft(); return true }
        }
        if flags.isEmpty && (event.keyCode == 36 || event.keyCode == 76) && editor != nil { finishEditing(); return true }
        if flags == [.command], event.charactersIgnoringModifiers?.lowercased() == "v", editor == nil { return importPasteboard(.general) }
        return false
    }
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard active, chooser == nil, editor == nil else { return false }
        return canvas.scroll(at: point, delta: delta)
    }
    static func acceptsApplications(_ pasteboard: NSPasteboard) -> Bool {
        let urls = applicationURLs(pasteboard)
        return urls.count == 1 && urls[0].pathExtension.lowercased() == "app"
    }
    private static func applicationURLs(_ pasteboard: NSPasteboard) -> [URL] {
        pasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
    }
    @discardableResult func importPasteboard(_ pasteboard: NSPasteboard) -> Bool {
        guard active, chooser == nil else { return false }
        finishEditing(); onLock?()
        let urls = Self.applicationURLs(pasteboard)
        guard !urls.isEmpty else { return false }
        return canvas.importURLs(urls)
    }
    func beginExternalDrag() {
        guard active, !externalDrag, chooser == nil else { return }
        finishEditing(); onLock?(); externalDrag = true; canvas.setDropTarget(true)
    }
    func endExternalDrag() { externalDrag = false; canvas.setDropTarget(false) }

    private func chooseApplication() {
        guard active, chooser == nil, let window = host?.window else { return }
        finishEditing(); onLock?()
        let panel = NSOpenPanel()
        panel.title = L10n.text("Choose an application", "选择应用")
        panel.prompt = L10n.text("Choose", "选择")
        panel.canChooseFiles = true; panel.canChooseDirectories = false
        panel.treatsFilePackagesAsDirectories = false; panel.allowsMultipleSelection = false
        panel.allowsOtherFileTypes = false; panel.resolvesAliases = true
        if #available(macOS 11.0, *) { panel.allowedContentTypes = [.applicationBundle] }
        else { panel.allowedFileTypes = ["app"] }
        panel.directoryURL = URL(fileURLWithPath: "/Applications", isDirectory: true)
        panel.level = NSWindow.Level(rawValue: max(panel.level.rawValue, window.level.rawValue + 1))
        dialogGeneration += 1
        let token = dialogGeneration; chooser = panel
        panel.beginSheetModal(for: window) { [weak self, weak panel] response in
            guard let self, self.dialogGeneration == token else { return }
            self.chooser = nil
            guard self.active, self.host?.window != nil else { return }
            self.host?.window?.makeKeyAndOrderFront(nil)
            if response == .OK, let url = panel?.url { _ = self.canvas.importURLs([url]) }
            if self.editor == nil { self.host?.window?.makeFirstResponder(self.host) }
        }
    }

    private func beginEditing(rect: CGRect, name: String) {
        guard active, !canvas.isTransitioning, let host, host.window != nil else { return }
        finishEditing(); onLock?(); beginningEdit = true
        defer { beginningEdit = false }
        let field = HUDShortcutNameField(frame: .zero)
        field.string = name; field.isEditable = true; field.isSelectable = true
        field.isRichText = false; field.importsGraphics = false; field.allowsUndo = true
        field.font = .systemFont(ofSize: 11, weight: .semibold)
        field.textColor = isDark?() == true ? .white : .black; field.insertionPointColor = field.textColor ?? .black
        field.textContainer?.maximumNumberOfLines = 1; field.textContainer?.lineBreakMode = .byClipping
        field.delegate = self
        field.setAccessibilityLabel(L10n.text("Shortcut name", "快捷方式名称"))
        field.setAccessibilityHelp(L10n.text("Return applies the name. Save shortcut stores your changes.", "回车应用名称，点击保存快捷方式以保存修改。"))
        field.onToggle = { [weak self] in self?.onToggle?() }
        editor = field
        let surface = HUDProjectedTextEditor(textView: field, rect: rect, host: host, parent: canvas.layer)
        surface.configureSingleLine()
        surface.project = { [weak self] in self?.project?($0) ?? $0 }
        surface.unproject = { [weak self] point in
            guard let self else { return nil }; return self.unproject?(point) ?? (self.unproject == nil ? point : nil)
        }
        surface.setAppearance(background: NSColor(white: isDark?() == true ? 0.17 : 0.91, alpha: 1), border: HUDRuntimeAppearance.accent)
        projectedEditor = surface; surface.resizeDocument(); layoutAccessibility()
        host.window?.makeFirstResponder(field); field.selectAll(nil)
    }
    func finishEditing(commit: Bool = true) {
        guard !finishingEdit, let editor else { return }
        finishingEdit = true
        defer { finishingEdit = false }
        if commit { editor.unmarkText(); normalizeName() }
        self.editor = nil; editor.delegate = nil
        let surface = projectedEditor; projectedEditor = nil
        if commit { canvas.setDraftName(editor.string) }
        host?.window?.makeFirstResponder(host); surface?.dispose(); layoutAccessibility()
    }
    func textView(_ textView: NSTextView, doCommandBy commandSelector: Selector) -> Bool {
        guard !textView.hasMarkedText() else { return false }
        if commandSelector == #selector(NSResponder.cancelOperation(_:)) { finishEditing(commit: false); return true }
        if commandSelector == #selector(NSResponder.insertNewline(_:)) || commandSelector == #selector(NSResponder.insertTab(_:)) { finishEditing(); return true }
        return false
    }
    func textDidEndEditing(_ notification: Notification) {
        guard !beginningEdit, !finishingEdit, let field = editor, notification.object as? NSTextView === field else { return }
        DispatchQueue.main.async { [weak self, weak field] in
            guard let self, let field, self.active, self.editor === field, !self.beginningEdit, !self.finishingEdit else { return }
            if field.window?.firstResponder === field { return }
            self.finishEditing()
        }
    }
    func textDidChange(_ notification: Notification) {
        guard notification.object as? NSTextView === editor else { return }
        normalizeName(); projectedEditor?.resizeDocument()
    }
    private func normalizeName() {
        guard let editor, !editor.hasMarkedText() else { return }
        let bounded = String(editor.string.components(separatedBy: .newlines).joined(separator: " ").prefix(128))
        guard editor.string != bounded else { return }
        let selection = editor.selectedRange()
        editor.string = bounded
        editor.setSelectedRange(NSRange(location: min(selection.location, bounded.utf16.count), length: 0))
    }
    func layoutAccessibility() {
        guard active, let host else { return }
        defer { HUDControlHighlightLayer.requestRefresh(on: host) }
        projectedEditor?.refreshProjection()
        let actions = canvas.accessibleActions.filter { !(editor != nil && $0.id == "apps:name") }
        let help = canvas.accessibilityStatus
        let wanted = Set(actions.map(\.id))
        for id in Array(buttons.keys) where !wanted.contains(id) { buttons.removeValue(forKey: id)?.removeFromSuperview() }
        for action in actions {
            let button: HUDShortcutActionButton
            if let existing = buttons[action.id] { button = existing }
            else {
                button = HUDShortcutActionButton(frame: .zero); button.actionID = action.id
                button.title = ""; button.isBordered = false; button.target = self; button.action = #selector(activateAction(_:))
                button.projectedFrame = { [weak self, weak host, weak button] in
                    guard let self, let host, let button, let window = host.window else { return .zero }
                    return window.convertToScreen(host.convert(self.project?(button.sourceRect) ?? button.sourceRect, to: nil))
                }
                host.addSubview(button); buttons[action.id] = button
            }
            button.isHidden = false; button.sourceRect = action.rect
            if button.accessibilityLabel() != action.label { button.setAccessibilityLabel(action.label) }
            if button.accessibilityHelp() != help { button.setAccessibilityHelp(help) }
            let frame = project?(action.rect) ?? action.rect
            if button.frame != frame { button.frame = frame }
        }
    }
    @objc private func activateAction(_ sender: HUDShortcutActionButton) {
        guard active else { return }
        finishEditing(); onLock?(); canvas.perform(actionID: sender.actionID)
    }
}

private final class HUDShortcutActionButton: NSButton {
    override func draw(_ dirtyRect: NSRect) {}
    var actionID = ""
    var sourceRect = CGRect.zero
    var projectedFrame: (() -> CGRect)?
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}

private final class HUDShortcutNameField: HUDProjectedTextView {
    var onToggle: (() -> Void)?
    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        if SummonShortcut.active.matches(event: event) { onToggle?(); return true }
        return super.performKeyEquivalent(with: event)
    }
}
