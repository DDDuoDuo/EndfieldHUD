import AppKit
import UniformTypeIdentifiers

/// Native text input is temporary and lives inside the existing HUD. All
/// settled artwork stays in the projected Notes layer and shares its wipe.
final class HUDNotesInteraction: NSObject, NSTextViewDelegate {
    private let canvas: NotesCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var workspaceProject: ((CGRect) -> CGRect)?
    var onLock: (() -> Void)?
    var onToggle: (() -> Void)?
    var isDark: (() -> Bool)?
    private var active = false
    private var dragging = false
    private var externalDrag = false
    private var request: NotesEditRequest?
    private var editor: HUDNoteTextView?
    private var editorScroll: NSScrollView?
    private var chooser: NSOpenPanel?
    private var buttons: [String: HUDNotesActionButton] = [:]
    private var dialogGeneration = 0
    var isPresentingPanel: Bool { chooser != nil }
    var isInputLocked: Bool { dragging || externalDrag || editor != nil || chooser != nil }

    init(canvas: NotesCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        super.init()
        canvas.onEdit = { [weak self] in self?.beginEditing($0) }
        canvas.onChooseImage = { [weak self] in self?.chooseImages(at: $0) }
        canvas.onChange = { [weak self] in self?.refreshActions() }
    }

    func setActive(_ value: Bool) {
        guard active != value else { return }
        if !value { deactivate() }
        else { active = true; refreshActions() }
    }

    func deactivate() {
        finishEditing()
        canvas.cancelInteraction()
        dragging = false; externalDrag = false; active = false
        dialogGeneration += 1
        let panel = chooser
        chooser = nil
        panel?.cancel(nil)
        buttons.values.forEach { $0.isHidden = true }
    }

    @discardableResult func mouseDown(at point: CGPoint, clickCount: Int) -> Bool {
        guard active else { return false }
        finishEditing()
        guard canvas.mouseDown(at: point, clickCount: clickCount) else { return false }
        onLock?()
        dragging = canvas.isDragging
        if editor == nil { host?.window?.makeFirstResponder(host) }
        return true
    }

    @discardableResult func mouseDownInWorkspace(at point: CGPoint, clickCount: Int) -> Bool {
        guard active, canvas.containsWorkspacePoint(point) else { return false }
        finishEditing(); onLock?()
        guard canvas.mouseDownInWorkspace(at: point, clickCount: clickCount) else { return false }
        dragging = canvas.isDragging
        if editor == nil { host?.window?.makeFirstResponder(host) }
        return true
    }

    func mouseDraggedInWorkspace(to point: CGPoint) { mouseDragged(to: point) }

    func layoutAccessibility() { refreshActions() }

    private func projectRect(_ rect: CGRect, space: NotesCoordinateSpace) -> CGRect {
        if space == .workspace { return workspaceProject?(rect) ?? rect }
        return project?(rect) ?? rect
    }

    func mouseDragged(to point: CGPoint) {
        guard active, dragging else { return }
        canvas.mouseDragged(to: point)
    }

    func mouseUp() {
        guard dragging else { return }
        canvas.mouseUp(); dragging = false
        refreshActions()
    }

    func keyDown(_ event: NSEvent) -> Bool {
        guard active else { return false }
        let modifiers = event.modifierFlags.intersection([.command, .control, .option, .shift])
        if modifiers.isEmpty && (event.keyCode == 51 || event.keyCode == 117) {
            guard canvas.hasSelection else { return false }
            canvas.deleteSelection(); return true
        }
        if canvas.notesSelected && modifiers == [.command] && event.charactersIgnoringModifiers?.lowercased() == "v" {
            return importPasteboard(.general, at: CGPoint(x: 110, y: 75))
        }
        return false
    }

    private func beginEditing(_ next: NotesEditRequest) {
        guard active, let host = host, let window = host.window else { return }
        finishEditing()
        onLock?()
        request = next
        canvas.setEditing(next)
        let rect = projectRect(next.rect, space: next.space).insetBy(dx: -2, dy: -2)
        let scale = max(0.5, rect.width / max(1, next.rect.width))
        let scroll = NSScrollView(frame: rect)
        scroll.wantsLayer = true
        scroll.borderType = .noBorder
        scroll.drawsBackground = true
        scroll.backgroundColor = isDark?() == true ? NSColor(white: 0.12, alpha: 1) : NSColor(white: 0.96, alpha: 1)
        scroll.layer?.cornerRadius = 3
        scroll.layer?.borderWidth = 1
        scroll.layer?.borderColor = HUDRuntimeAppearance.accent.cgColor
        scroll.hasVerticalScroller = next.multiline
        scroll.autohidesScrollers = true
        let text = HUDNoteTextView(frame: CGRect(origin: .zero, size: rect.size))
        text.multiline = next.multiline
        text.isRichText = false
        text.importsGraphics = false
        text.allowsUndo = true
        text.isAutomaticQuoteSubstitutionEnabled = false
        text.isAutomaticDashSubstitutionEnabled = false
        text.font = .systemFont(ofSize: max(11, next.fontSize * scale))
        text.textColor = isDark?() == true ? .white : .black
        text.insertionPointColor = text.textColor ?? .white
        text.drawsBackground = false
        text.textContainerInset = NSSize(width: 4, height: 3)
        text.isHorizontallyResizable = false
        text.isVerticallyResizable = true
        text.autoresizingMask = [.width]
        text.minSize = NSSize(width: 0, height: rect.height)
        text.maxSize = NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude)
        text.textContainer?.containerSize = NSSize(width: rect.width, height: CGFloat.greatestFiniteMagnitude)
        text.textContainer?.widthTracksTextView = true
        text.string = next.text
        text.delegate = self
        text.setAccessibilityLabel(next.multiline ? L10n.text("Edit text note", "编辑文字便笺") : L10n.text("Edit checklist item", "编辑待办事项"))
        text.setAccessibilityHelp(L10n.text("Escape or Command-Return saves. Text notes support multiple lines.", "按 Esc 或 Command-Return 保存。文字便笺支持多行。"))
        text.onFinish = { [weak self] in self?.finishEditing() }
        text.onToggle = { [weak self] in self?.onToggle?() }
        scroll.documentView = text
        editor = text; editorScroll = scroll
        host.addSubview(scroll)
        window.makeFirstResponder(text)
        text.setSelectedRange(NSRange(location: (text.string as NSString).length, length: 0))
    }

    func finishEditing() {
        guard let request = request, let editor = editor else { return }
        self.request = nil; self.editor = nil
        let text = editor.string
        editor.delegate = nil
        editorScroll?.removeFromSuperview(); editorScroll = nil
        canvas.finishEditing(request, text: text)
        canvas.setEditing(nil)
        if host?.window?.firstResponder === editor { host?.window?.makeFirstResponder(host) }
        refreshActions()
    }

    /// Parallax is committed after the click that creates an editor. Keep its
    /// native input surface on the same projected rectangle as the note while
    /// preserving the live text, selection and undo history.
    private func layoutEditor() {
        guard let request, let editor, let scroll = editorScroll else { return }
        let frame = projectRect(request.rect, space: request.space).insetBy(dx: -2, dy: -2)
        guard frame.width > 0, frame.height > 0,
              [frame.minX, frame.minY, frame.width, frame.height].allSatisfy({ $0.isFinite }) else { return }
        if scroll.frame != frame {
            scroll.frame = frame
            let content = scroll.contentSize
            editor.minSize = NSSize(width: 0, height: content.height)
            editor.textContainer?.containerSize = NSSize(width: content.width, height: .greatestFiniteMagnitude)
            editor.setFrameSize(NSSize(width: content.width, height: max(content.height, editor.frame.height)))
        }
        let fontSize = max(11, request.fontSize * max(0.5, frame.width / max(1, request.rect.width)))
        if editor.font?.pointSize != fontSize { editor.font = .systemFont(ofSize: fontSize) }
        scroll.layer?.borderColor = HUDRuntimeAppearance.accent.cgColor
    }

    private func chooseImages(at point: CGPoint) {
        guard active, chooser == nil, let window = host?.window else { return }
        finishEditing(); onLock?()
        let panel = NSOpenPanel()
        panel.title = L10n.text("Add image notes", "添加图片便笺")
        panel.prompt = L10n.text("Add images", "添加图片")
        if #available(macOS 11.0, *) { panel.allowedContentTypes = [.image] }
        else { panel.allowedFileTypes = ["public.image"] }
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = true
        panel.level = NSWindow.Level(rawValue: max(panel.level.rawValue, window.level.rawValue + 1))
        dialogGeneration += 1
        let token = dialogGeneration
        chooser = panel // Set before the sheet takes key focus.
        panel.beginSheetModal(for: window) { [weak self, weak panel] response in
            guard let self = self, self.dialogGeneration == token else { return }
            self.chooser = nil
            guard self.active, self.host?.window != nil else { return }
            if response == .OK, let urls = panel?.urls { self.canvas.importImages(urls: urls, at: point) }
            self.host?.window?.makeKeyAndOrderFront(nil)
            self.host?.window?.makeFirstResponder(self.host)
        }
    }

    static func acceptsImages(_ pasteboard: NSPasteboard) -> Bool {
        pasteboard.canReadObject(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true, .urlReadingContentsConformToTypes: ["public.image"]])
            || pasteboard.availableType(from: [.png, .tiff]) != nil
    }

    @discardableResult func importPasteboard(_ pasteboard: NSPasteboard, at point: CGPoint) -> Bool {
        guard active else { return false }
        finishEditing()
        let urls = pasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true, .urlReadingContentsConformToTypes: ["public.image"]]) as? [URL] ?? []
        if !urls.isEmpty { return canvas.importImages(urls: urls, at: point) }
        for type in [NSPasteboard.PasteboardType.png, .tiff] {
            if let data = pasteboard.data(forType: type) { return canvas.importImage(data: data, at: point) }
        }
        return false
    }

    func beginExternalDrag() {
        guard active, !externalDrag else { return }
        finishEditing(); onLock?(); externalDrag = true
    }
    func endExternalDrag() { externalDrag = false }

    private func refreshActions() {
        guard active, let host = host else { return }
        defer { HUDControlHighlightLayer.requestRefresh(on: host) }
        layoutEditor()
        let actions = canvas.accessibleActions
        let wanted = Set(actions.map(\.id))
        for id in Array(buttons.keys) where !wanted.contains(id) {
            buttons.removeValue(forKey: id)?.removeFromSuperview()
        }
        for action in actions {
            let button: HUDNotesActionButton
            if let existing = buttons[action.id] { button = existing }
            else {
                button = HUDNotesActionButton(frame: .zero)
                button.actionID = action.id
                button.target = self; button.action = #selector(activateAction(_:))
                button.title = ""; button.isBordered = false
                button.projectedFrame = { [weak self, weak host, weak button] in
                    guard let self, let host, let button, let window = host.window else { return .zero }
                    return window.convertToScreen(host.convert(self.projectRect(button.sourceRect, space: button.sourceSpace), to: nil))
                }
                host.addSubview(button)
                buttons[action.id] = button
            }
            button.isHidden = false
            button.sourceRect = action.rect; button.sourceSpace = action.space
            if button.accessibilityLabel() != action.label { button.setAccessibilityLabel(action.label) }
            let frame = projectRect(action.rect, space: action.space)
            if button.frame != frame { button.frame = frame }
        }
    }

    @objc private func activateAction(_ sender: HUDNotesActionButton) {
        guard active else { return }
        finishEditing(); onLock?()
        canvas.perform(actionID: sender.actionID)
    }
}

private final class HUDNotesActionButton: NSButton {
    override func draw(_ dirtyRect: NSRect) {}
    var actionID = ""
    var sourceRect = CGRect.zero
    var sourceSpace: NotesCoordinateSpace = .workspace
    var projectedFrame: (() -> CGRect)?
    // Mouse input is handled against the projected canvas, including dragging.
    // These native actions expose the same controls to keyboard/VoiceOver.
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}

private final class HUDNoteTextView: NSTextView {
    var multiline = true
    var onFinish: (() -> Void)?
    var onToggle: (() -> Void)?
    override func keyDown(with event: NSEvent) {
        let flags = event.modifierFlags.intersection([.command, .option, .control, .shift])
        // Return/Escape belongs to the IME while it is composing Chinese text.
        if !hasMarkedText() {
            if event.keyCode == 53 || ((event.keyCode == 36 || event.keyCode == 76) && (!multiline || flags == [.command])) {
                onFinish?(); return
            }
            if SummonShortcut.active.matches(event: event) { onToggle?(); return }
        }
        super.keyDown(with: event)
    }
}
