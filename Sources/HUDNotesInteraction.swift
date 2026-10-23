import AppKit
import QuartzCore
import UniformTypeIdentifiers

/// The desktop notes' native input: editors, formatting menus, media import
/// and accessibility stand-ins of the workspace cards. Native text input is
/// temporary and lives inside the existing HUD; settled artwork stays in the
/// projected notes plane. The centre screen routes through NotesCanvas (a
/// HUDDesignCanvas); its Media button hands its sources to this bridge.
final class HUDNotesInteraction: NSObject, NSTextViewDelegate {
    private let canvas: NotesCanvas
    private var workspace: NotesWorkspace { canvas.workspace }
    private weak var host: NSView?
    var workspaceProject: ((CGRect) -> CGRect)?
    var workspaceUnproject: ((CGPoint) -> CGPoint?)?
    var onLock: (() -> Void)?
    var onToggle: (() -> Void)?
    var isDark: (() -> Bool)?
    var onChooseShelfMedia: ((CGPoint) -> Void)?
    private var active = false
    private var dragging = false
    private var externalDrag = false
    private var request: NotesEditRequest?
    private var editor: HUDNoteTextView?
    private var editorScroll: NSScrollView?
    private var projectedEditor: HUDProjectedTextEditor?
    private var chooser: NSOpenPanel?
    private var formatting: NotesDesktopPopup?
    private var formattingNoteID: UUID?
    /// Takes the first press outside an open formatting menu while editing.
    private let forwarder = NotesPressForwarder()
    private var retiringMenuArtwork: CALayer?
    private let importQueue = DispatchQueue(label: "EndfieldCharge.NotesMediaImport", qos: .utility)
    private var importGeneration = 0
    private var pendingImport: NotesImportTicket?
    private var editorScale: CGFloat = 1
    private var editorHasFormatting = false
    private var buttons: [String: HUDNotesActionButton] = [:]
    private var mediaSliders: [String: HUDNotesSeekSlider] = [:]
    private var dialogGeneration = 0
    var isPresentingPanel: Bool { chooser != nil }
    var isInputLocked: Bool { dragging || externalDrag || chooser != nil }
    var capturesPointer: Bool { formatting != nil }

    /// While a note is edited, the first press outside its formatting menu
    /// only closes the menu: it goes to a view that leaves the editor focused.
    func hitTestMenu(at hostPoint: CGPoint) -> NSView? {
        if let view = formatting?.hitTest(hostPoint) { return view }
        guard let host, formatting != nil, editor != nil else { return nil }
        return forwarder.target(in: host)
    }

    /// The host routes its first background click here before invoking a module.
    /// Native child controls receive clicks inside their projected menu frames.
    @discardableResult func dismissMenuIfOutside(at hostPoint: CGPoint) -> Bool {
        guard let formatting, formatting.hitTest(hostPoint) == nil else { return false }
        closeFormatting()
        return true
    }

    init(canvas: NotesCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        super.init()
        let workspace = canvas.workspace
        workspace.onEdit = { [weak self] in self?.beginEditing($0) }
        workspace.onFormat = { [weak self] in self?.showFormatting(noteID: $0, kind: $1) }
        workspace.onChange = { [weak self] in self?.refreshActions() }
        // Media closes the desktop editor and its menus first, as any centre press does.
        let presentSources = workspace.onChooseImage
        workspace.onChooseImage = { [weak self] point in self?.finishEditing(); presentSources?(point) }
        canvas.onChooseFiles = { [weak self] in self?.chooseImages(at: $0) }
        canvas.onChooseShelf = { [weak self] in self?.onChooseShelfMedia?($0) }
    }
    deinit {
        pendingImport?.cancel()
        retiringMenuArtwork?.removeAllAnimations(); retiringMenuArtwork?.removeFromSuperlayer()
    }

    func setActive(_ value: Bool) {
        guard active != value else { return }
        if !value { deactivate() }
        else { active = true; refreshActions() }
    }

    func deactivate() {
        // Hide immediately when the owner is leaving; the overlay's own close
        // animation governs the entire workspace and owns its teardown.
        active = false
        finishEditing()
        closeFormatting()
        retiringMenuArtwork?.removeAllAnimations(); retiringMenuArtwork?.removeFromSuperlayer(); retiringMenuArtwork = nil
        importGeneration += 1
        pendingImport?.cancel(); pendingImport = nil
        workspace.cancelInteraction()
        dragging = false; externalDrag = false; active = false
        dialogGeneration += 1
        let panel = chooser
        chooser = nil
        panel?.cancel(nil)
        buttons.values.forEach { $0.isHidden = true }; mediaSliders.values.forEach { $0.isHidden = true }
    }

    @discardableResult func mouseDownInWorkspace(at point: CGPoint, clickCount: Int) -> Bool {
        guard active, workspace.containsWorkspacePoint(point) else { return false }
        if let action = workspace.formatAction(at: point) { onLock?(); workspace.perform(actionID: action); return true }
        finishEditing(); onLock?()
        guard workspace.mouseDownInWorkspace(at: point, clickCount: clickCount) else { return false }
        dragging = workspace.isDragging
        if editor == nil { host?.window?.makeFirstResponder(host) }
        return true
    }

    /// The host sends every drag here while `NotesCanvas.isDragging`, which
    /// includes a drag on a centre control (a long menu's scroll bar): that
    /// one goes back to the canvas at the event's content-box point.
    func mouseDraggedInWorkspace(to point: CGPoint) {
        guard !dragging, canvas.isActive, let host, let event = NSApp.currentEvent,
              let box = canvas.context.unproject?(host.convert(event.locationInWindow, from: nil)) else { mouseDragged(to: point); return }
        canvas.pointerDragged(to: box)
    }

    func layoutAccessibility() { refreshActions() }

    private func projectRect(_ rect: CGRect) -> CGRect { workspaceProject?(rect) ?? rect }

    func mouseDragged(to point: CGPoint) {
        guard active, dragging else { return }
        workspace.mouseDragged(to: point)
    }

    func mouseUp() {
        guard dragging else { return }
        workspace.mouseUp(); dragging = false
        refreshActions()
    }

    func setVisible(_ visible: Bool) { workspace.setVisible(visible) }
    func mouseMoved(at point: CGPoint?) { workspace.mouseMoved(at: active ? point : nil) }
    @discardableResult func rightMouseDown(at point: CGPoint) -> Bool {
        guard active else { return false }
        return workspace.rightMouseDown(at: point)
    }

    /// Native editor wheel events are handled by its NSScrollView. A bubbled
    /// event over that same card must not save/destroy it or move its backdrop.
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard active else { return false }
        if let request, workspace.contains(noteID: request.noteID, point: point) { return true }
        return workspace.scroll(at: point, delta: delta)
    }

    func keyDown(_ event: NSEvent) -> Bool {
        guard active else { return false }
        let modifiers = event.modifierFlags.intersection([.command, .control, .option, .shift])
        if modifiers.isEmpty && (event.keyCode == 51 || event.keyCode == 117) {
            guard workspace.hasSelection else { return false }
            workspace.deleteSelection(); return true
        }
        if workspace.notesSelected && modifiers == [.command] && event.charactersIgnoringModifiers?.lowercased() == "v" {
            return importPasteboard(.general, at: CGPoint(x: 110, y: 75))
        }
        return false
    }

    private func beginEditing(_ next: NotesEditRequest) {
        guard active, let host = host, let window = host.window else { return }
        finishEditing()
        onLock?()
        request = next
        workspace.setEditing(next)
        let rect = next.rect
        let scale: CGFloat = 1
        editorScale = scale
        editorHasFormatting = next.richText != nil
        let text = HUDNoteTextView(frame: CGRect(origin: .zero, size: rect.size))
        text.multiline = next.multiline
        text.isRichText = next.itemID == nil
        text.importsGraphics = false
        text.allowsUndo = true
        text.isAutomaticQuoteSubstitutionEnabled = false
        text.isAutomaticDashSubstitutionEnabled = false
        let initialFont = HUDFontResolver.nsFont(size: max(1, next.fontSize * scale))
        text.font = initialFont
        text.textColor = isDark?() == true ? .white : .black
        // The redesign's caret and selection: the theme colour.
        text.insertionPointColor = NSColor(cgColor: isDark?() == true ? HUDColor.yellow : HUDColor.nearBlack) ?? .white
        text.selectedTextAttributes = [.backgroundColor: NSColor(cgColor: HUDColor.yellow)?.withAlphaComponent(0.35) ?? .selectedTextBackgroundColor]
        text.drawsBackground = false
        text.textContainerInset = .zero
        text.textContainer?.lineFragmentPadding = 0
        text.isHorizontallyResizable = false
        text.isVerticallyResizable = true
        text.autoresizingMask = [.width]
        text.minSize = NSSize(width: 0, height: rect.height)
        text.maxSize = NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude)
        text.textContainer?.containerSize = NSSize(width: rect.width, height: CGFloat.greatestFiniteMagnitude)
        text.textContainer?.widthTracksTextView = true
        let paragraph = NSMutableParagraphStyle()
        if next.richText == nil {
            paragraph.minimumLineHeight = NotesTextMetrics.lineHeight(fontSize: next.fontSize) * scale
            paragraph.maximumLineHeight = paragraph.minimumLineHeight
        } else { paragraph.lineSpacing = scale }
        text.defaultParagraphStyle = paragraph
        text.typingAttributes = [.font: initialFont, .paragraphStyle: paragraph, .foregroundColor: isDark?() == true ? NSColor.white : NSColor.black]
        text.string = next.text
        if let rich = next.richText {
            text.textStorage?.setAttributedString(HUDStandardRichText.displayed(rich.attributed(next.text, scale: scale,
                defaultColor: isDark?() == true ? .white : .black)))
        }
        if next.richText == nil, (text.textStorage?.length ?? 0) > 0 {
            text.textStorage?.addAttribute(.paragraphStyle, value: paragraph, range: NSRange(location: 0, length: (text.string as NSString).length))
        }
        text.delegate = self
        text.setAccessibilityLabel(next.multiline ? L10n.text("Edit text note", "编辑文字便笺") : L10n.text("Edit checklist item", "编辑待办事项"))
        text.setAccessibilityHelp(L10n.text("Escape or Command-Return saves. Text notes support multiple lines.", "按 Esc 或 Command-Return 保存。文字便笺支持多行。"))
        text.onFinish = { [weak self] in self?.finishEditing() }
        text.onToggle = { [weak self] in self?.onToggle?() }
        text.onEscape = { [weak self] in
            guard let self, self.formatting != nil else { return false }
            self.closeFormatting(); return true
        }
        text.onFormatShortcut = { [weak self] change in self?.applyFormat(change) }
        let projected = HUDProjectedTextEditor(textView: text, rect: rect, host: host, parent: workspace.workspaceLayer)
        projected.project = { [weak self] in self?.projectRect($0) ?? $0 }
        projected.unproject = { [weak self] point in
            guard let self else { return nil }
            return self.workspaceUnproject == nil ? point : self.workspaceUnproject?(point)
        }
        let scroll = projected.scrollView
        editor = text; editorScroll = scroll; projectedEditor = projected
        layoutEditor()
        window.makeFirstResponder(text)
        text.setSelectedRange(NSRange(location: (text.string as NSString).length, length: 0))
        resizeEditorDocument()
        scroll.contentView.scroll(to: CGPoint(x: 0, y: min(max(0, text.frame.height - scroll.contentSize.height), next.scrollOffset * scale)))
        scroll.reflectScrolledClipView(scroll.contentView)
    }

    func finishEditing() {
        closeFormatting()
        guard let request = request, let editor = editor else { return }
        self.request = nil; self.editor = nil
        let text = editor.string
        let scale: CGFloat = 1
        let offset = (editorScroll?.contentView.bounds.minY ?? 0) / scale
        var rich: NotesRichText?
        if request.itemID == nil, let storage = editor.textStorage {
            // Runs in the standard faces are stored as before: no family.
            let captured = NotesRichText.capture(HUDStandardRichText.stored(storage), scale: scale,
                                                 defaultColor: isDark?() == true ? .white : .black)
            let system = NSFont.systemFont(ofSize: 12).fontName
            let plain = captured.runs.allSatisfy { run in
                let style = run.style
                return (style.fontName == nil || style.fontName == system) && abs(style.fontSize - 12) < 0.01 && style.color == nil
                    && !style.bold && !style.italic && !style.underline && !style.strikethrough
            }
            rich = plain && request.richText == nil ? nil : captured
        }
        editor.delegate = nil
        projectedEditor?.dispose(); projectedEditor = nil; editorScroll = nil
        workspace.finishEditing(request, text: text, scrollOffset: offset, richText: rich)
        workspace.setEditing(nil)
        if host?.window?.firstResponder === editor { host?.window?.makeFirstResponder(host) }
        refreshActions()
    }

    /// TextKit retains logical dimensions while its owned artwork inherits the
    /// same projection as the note. Moving the HUD cannot rewrite rich text,
    /// undo groups, selection or marked-text composition.
    private func layoutEditor() {
        guard let projectedEditor else { return }
        projectedEditor.setAppearance(
            background: NSColor(cgColor: isDark?() == true ? HUDColor.field : HUDColor.plateHigh) ?? .black,
            border: NSColor(cgColor: HUDColor.yellow) ?? .yellow, radius: 0)
        projectedEditor.refreshProjection()
    }

    /// The press being handled lands on the edited note's format buttons
    /// (AppKit gives the HUD view focus before the press opens the menu).
    private func pressOpensFormatting() -> Bool {
        guard let host, let request, let event = NSApp.currentEvent, event.window === host.window, event.type == .leftMouseDown,
              let point = workspaceUnproject?(host.convert(event.locationInWindow, from: nil)) else { return false }
        return workspace.contains(noteID: request.noteID, point: point) && workspace.formatAction(at: point) != nil
    }

    func textDidChange(_ notification: Notification) { resizeEditorDocument() }
    /// Focus moving elsewhere (another field, the centre) saves and closes the
    /// editor, so no caret stays behind; its own formatting menu keeps it open,
    /// and a press on its format buttons gives the focus back afterwards.
    func textDidEndEditing(_ notification: Notification) {
        guard let editor, notification.object as? NSTextView === editor else { return }
        let formats = pressOpensFormatting()
        DispatchQueue.main.async { [weak self, weak editor] in
            guard let self, let editor, self.editor === editor, let window = editor.window else { return }
            let responder = window.firstResponder as? NSView
            if responder === editor { return }
            if let responder, let formatting = self.formatting, responder === formatting || responder.isDescendant(of: formatting) { return }
            if formats, responder === self.host { window.makeFirstResponder(editor); return }
            self.finishEditing()
        }
    }
    func textViewDidChangeSelection(_ notification: Notification) {
        guard let editor else { return }
        let color = editor.typingAttributes[.foregroundColor] as? NSColor ?? (isDark?() == true ? .white : .black)
        workspace.setEditingColor(color)
    }

    private func resizeEditorDocument() { projectedEditor?.resizeDocument() }

    private func showFormatting(noteID: UUID, kind: String) {
        guard active, let host, let kind = NotesDesktopPopup.Kind(rawValue: kind) else { return }
        // A second press on the button that opened it closes it.
        if formattingNoteID == noteID, formatting?.kind == kind { closeFormatting(); return }
        closeFormatting()
        let drawing = workspace.noteKind(for: noteID) == .drawing, ink: NSColor = isDark?() == true ? .white : .black
        var style = NotesTextStyle()
        if drawing { style.color = NotesRGBA(workspace.currentDrawingColor) }
        if let editor { style = NotesFormattingEditor.style(in: editor, scale: editorScale, defaultColor: ink) }
        let popup = NotesDesktopPopup(kind: kind, style: style, defaultColor: ink,
                                      colorTitle: drawing ? L10n.text("PEN COLOR", "画笔颜色") : L10n.text("TEXT COLOR", "文字颜色"),
                                      env: workspace.env)
        popup.takesFocus = editor == nil
        formatting = popup; formattingNoteID = noteID
        popup.onClose = { [weak self] in self?.closeFormatting() }
        popup.onChange = { [weak self] change in
            guard let self else { return }
            if drawing {
                if case .color(let color) = change { self.workspace.setDrawingColor(color) }
            } else { self.applyFormat(change) }
        }
        host.addSubview(popup); layoutFormatting(); popup.playEntrance()
        HUDControlHighlightLayer.requestRefresh(on: host)
    }

    private func closeFormatting() {
        let wasOpen = formatting != nil
        dismiss(formatting); formatting = nil; formattingNoteID = nil
        if wasOpen { layoutEditor() }
    }

    private func layoutFormatting() {
        guard let controls = formatting, let id = formattingNoteID, let note = workspace.noteRect(for: id) else { return }
        let logical = controls.contentSize
        let bounds = workspace.workspaceBounds
        let source = CGRect(x: min(bounds.maxX - logical.width, max(bounds.minX, note.minX)),
            y: min(bounds.maxY - logical.height, max(bounds.minY, note.maxY + 4)),
            width: logical.width, height: logical.height)
        positionPopup(controls, source: source)
    }

    /// Apply only to the selection (or future typing), retaining all unrelated
    /// runs and undoing the exact original attributed substring.
    func applyFormat(_ change: NotesFormatChange) {
        guard let editor, let storage = editor.textStorage, request?.itemID == nil else { return }
        let selected = editor.selectedRange()
        if !editorHasFormatting {
            // Paragraph attributes apply to whole paragraphs. Changing only a
            // middle run lets AppKit restore the first character's fixed plain
            // line height, clipping large selected or newly typed characters.
            // Normalize layout alone before the first rich edit; preserve every
            // character attribute and the caret's separate pending style.
            var typing = editor.typingAttributes
            let paragraph = NSMutableParagraphStyle(); paragraph.lineSpacing = editorScale
            let undo = editor.undoManager
            let undoEnabled = undo?.isUndoRegistrationEnabled == true
            if undoEnabled { undo?.disableUndoRegistration() }
            editor.defaultParagraphStyle = paragraph
            storage.addAttribute(.paragraphStyle, value: paragraph,
                                 range: NSRange(location: 0, length: storage.length))
            editor.setSelectedRange(selected)
            typing[.paragraphStyle] = paragraph
            editor.typingAttributes = typing
            if undoEnabled { undo?.enableUndoRegistration() }
            editorHasFormatting = true
            resizeEditorDocument()
        }
        NotesFormattingEditor.apply(change, to: editor, scale: editorScale)
        if case .color(let color) = change { workspace.setEditingColor(color) }
        resizeEditorDocument()
    }

    func importMedia(urls: [URL], at point: CGPoint) {
        guard active, !urls.isEmpty else { return }
        finishEditing()
        importGeneration += 1
        let token = importGeneration
        pendingImport?.cancel()
        let ticket = NotesImportTicket(urls: urls)
        pendingImport = ticket
        // Native URL objects retain their security metadata through dispatch.
        importQueue.async { [weak self, ticket] in
            guard !ticket.isCancelled else { return }
            let result = Result { () throws -> [NotesMediaReference] in
                var references: [NotesMediaReference] = []
                for url in ticket.urls {
                    guard !ticket.isCancelled else { throw CocoaError(.userCancelled) }
                    references.append(try NotesMediaFactory.makeReference(from: url))
                }
                return references
            }
            DispatchQueue.main.async { [weak self] in
                guard let self, self.active, self.importGeneration == token, !ticket.isCancelled else { return }
                self.pendingImport = nil
                switch result {
                case .success(let references):
                    for (index, reference) in references.enumerated() {
                        _ = self.workspace.importMedia(reference: reference,
                            at: CGPoint(x: point.x + CGFloat(index % 4) * 12, y: point.y + CGFloat(index % 4) * 12))
                    }
                case .failure(let error): self.workspace.reportImportError(error)
                }
            }
        }
    }

    /// The host lists the File Shelf for the Media button's "Choose from Shelf".
    func presentShelfMedia(choices: [NotesShelfMediaChoice], at point: CGPoint, onSelect: @escaping (UUID) -> Void) {
        guard active else { return }
        finishEditing()
        canvas.presentShelfMedia(choices: choices, onSelect: onSelect)
    }

    /// The pop-over's artwork lies on the notes plane at `source` (workspace
    /// coordinates); its input surface covers the same rect projected.
    private func positionPopup(_ popup: NotesDesktopPopup, source: CGRect) {
        popup.frame = projectRect(source); popup.bounds = CGRect(origin: .zero, size: source.size)
        popup.hostToLocal = { [weak self] point in
            guard let self else { return nil }
            guard let inverse = self.workspaceUnproject else { return CGPoint(x: point.x - source.minX, y: point.y - source.minY) }
            return inverse(point).map { CGPoint(x: $0.x - source.minX, y: $0.y - source.minY) }
        }
        popup.projectLocal = { [weak self] rect in self?.projectRect(rect.offsetBy(dx: source.minX, dy: source.minY)) ?? rect }
        let parent = workspace.workspaceLayer, origin = workspace.workspaceBounds.origin
        HUDMotion.perform {
            if popup.chrome.superlayer !== parent { parent.addSublayer(popup.chrome) }
            popup.chrome.position = CGPoint(x: source.minX - origin.x, y: source.minY - origin.y)
        }
        popup.layoutAccessibility()
    }

    /// Input stops at once; the artwork finishes a short fade on the plane.
    private func dismiss(_ popup: NotesDesktopPopup?) {
        guard let popup else { return }
        popup.cancelInteraction()
        retiringMenuArtwork?.removeAllAnimations(); retiringMenuArtwork?.removeFromSuperlayer(); retiringMenuArtwork = nil
        popup.removeFromSuperview()
        let layer = popup.chrome
        guard active, !workspace.env.reducesMotion, layer.superlayer != nil else { layer.removeAllAnimations(); layer.removeFromSuperlayer(); return }
        retiringMenuArtwork = layer
        CATransaction.begin()
        CATransaction.setCompletionBlock { [weak self, weak layer] in
            layer?.removeFromSuperlayer()
            if self?.retiringMenuArtwork === layer { self?.retiringMenuArtwork = nil }
        }
        HUDMotion.animate(layer, "opacity", to: Float(0), duration: 0.14, timing: HUDEasing.ease, env: workspace.env, key: "notes.controls.dismiss")
        CATransaction.commit()
    }

    /// Media → Choose in Finder: images and videos become desktop notes at `point`.
    func chooseImages(at point: CGPoint) {
        guard active, chooser == nil, let window = host?.window else { return }
        finishEditing(); onLock?()
        let panel = NSOpenPanel()
        panel.title = L10n.text("Add image/video notes", "添加图片/视频便笺")
        panel.prompt = L10n.text("Add media", "添加媒体")
        if #available(macOS 11.0, *) { panel.allowedContentTypes = [.image, .movie] }
        else { panel.allowedFileTypes = NotesMediaFactory.supportedFileExtensions }
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
            if response == .OK, let urls = panel?.urls { self.importMedia(urls: urls, at: point) }
            self.host?.window?.makeKeyAndOrderFront(nil)
            self.host?.window?.makeFirstResponder(self.host)
        }
    }

    static func acceptsImages(_ pasteboard: NSPasteboard) -> Bool {
        pasteboard.canReadObject(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true, .urlReadingContentsConformToTypes: ["public.image", "public.movie"]])
            || pasteboard.availableType(from: [.png, .tiff]) != nil
    }

    @discardableResult func importPasteboard(_ pasteboard: NSPasteboard, at point: CGPoint) -> Bool {
        guard active else { return false }
        finishEditing()
        let urls = pasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true, .urlReadingContentsConformToTypes: ["public.image", "public.movie"]]) as? [URL] ?? []
        if !urls.isEmpty { importMedia(urls: urls, at: point); return true }
        for type in [NSPasteboard.PasteboardType.png, .tiff] {
            if let data = pasteboard.data(forType: type) { return workspace.importImage(data: data, at: point) }
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
        layoutFormatting()
        let sliders = workspace.mediaSeekActions
        let wantedSliders = Set(sliders.map(\.id))
        for id in Array(mediaSliders.keys) where !wantedSliders.contains(id) { mediaSliders.removeValue(forKey: id)?.removeFromSuperview() }
        for action in sliders {
            guard let id = UUID(uuidString: action.id) else { continue }
            let slider: HUDNotesSeekSlider
            if let existing = mediaSliders[action.id] { slider = existing }
            else {
                slider = HUDNotesSeekSlider(frame: .zero)
                let workspace = self.workspace
                slider.readValue = { [weak workspace] in workspace?.mediaPosition(for: id) ?? 0 }
                slider.maximum = { [weak workspace] in workspace?.mediaDuration(for: id) ?? 0 }
                slider.onChange = { [weak workspace] in workspace?.seekMedia(id: id, to: $0) }
                host.addSubview(slider); mediaSliders[action.id] = slider
            }
            slider.isHidden = false; slider.setAccessibilityLabel(action.label)
            slider.frame = projectRect(action.rect)
        }
        let actions = workspace.accessibleActions
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
                    return window.convertToScreen(host.convert(self.projectRect(button.sourceRect), to: nil))
                }
                host.addSubview(button)
                buttons[action.id] = button
            }
            button.isHidden = false
            button.sourceRect = action.rect
            if button.accessibilityLabel() != action.label { button.setAccessibilityLabel(action.label) }
            let frame = projectRect(action.rect)
            if button.frame != frame { button.frame = frame }
        }
    }

    @objc private func activateAction(_ sender: HUDNotesActionButton) {
        guard active else { return }
        if !sender.actionID.contains(":format") { finishEditing() }
        onLock?()
        workspace.perform(actionID: sender.actionID)
    }
}

private final class NotesImportTicket {
    let urls: [URL]
    private let scopes: [Bool]
    private let lock = NSLock()
    private var cancelled = false
    init(urls: [URL]) { self.urls = urls; scopes = urls.map { $0.startAccessingSecurityScopedResource() } }
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    func cancel() { lock.lock(); cancelled = true; lock.unlock() }
    deinit { for (url, scoped) in zip(urls, scopes) where scoped { url.stopAccessingSecurityScopedResource() } }
}

private final class HUDNotesSeekSlider: NSView {
    var readValue: (() -> Double)?
    var maximum: (() -> Double)?
    var onChange: ((Double) -> Void)?
    override init(frame: NSRect) { super.init(frame: frame); setAccessibilityElement(true); setAccessibilityRole(.slider) }
    required init?(coder: NSCoder) { nil }
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityValue() -> Any? { readValue?() ?? 0 }
    override func accessibilityMinValue() -> Any? { 0 }
    override func accessibilityMaxValue() -> Any? { maximum?() ?? 0 }
    override func setAccessibilityValue(_ value: Any?) {
        guard let number = value as? NSNumber, number.doubleValue.isFinite else { return }
        onChange?(min(maximum?() ?? 0, max(0, number.doubleValue)))
    }
    override func accessibilityPerformIncrement() -> Bool { onChange?(min(maximum?() ?? 0, (readValue?() ?? 0) + 5)); return true }
    override func accessibilityPerformDecrement() -> Bool { onChange?(max(0, (readValue?() ?? 0) - 5)); return true }
}

private final class HUDNotesActionButton: NSButton {
    override func draw(_ dirtyRect: NSRect) {}
    var actionID = ""
    var sourceRect = CGRect.zero
    var projectedFrame: (() -> CGRect)?
    // Mouse input is handled against the projected canvas, including dragging.
    // These native actions expose the same controls to keyboard/VoiceOver.
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}

private final class HUDNoteTextView: HUDProjectedTextView {
    var multiline = true
    var onFinish: (() -> Void)?
    var onToggle: (() -> Void)?
    /// Escape closes an open formatting menu first (true when it did).
    var onEscape: (() -> Bool)?
    var onFormatShortcut: ((NotesFormatChange) -> Void)?
    override func keyDown(with event: NSEvent) {
        let flags = event.modifierFlags.intersection([.command, .option, .control, .shift])
        // Return/Escape belongs to the IME while it is composing Chinese text.
        if !hasMarkedText() {
            if event.keyCode == 53, onEscape?() == true { return }
            if event.keyCode == 53 || ((event.keyCode == 36 || event.keyCode == 76) && (!multiline || flags == [.command])) {
                onFinish?(); return
            }
            if SummonShortcut.active.matches(event: event) { onToggle?(); return }
        }
        super.keyDown(with: event)
    }
    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        if window?.firstResponder === self, isRichText, !hasMarkedText(), let onFormatShortcut, let change = NotesFormatChange(shortcut: event) {
            onFormatShortcut(change); return true
        }
        return super.performKeyEquivalent(with: event)
    }
}
