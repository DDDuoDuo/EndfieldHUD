import AppKit
import QuartzCore
import UniformTypeIdentifiers

/// Native text input is temporary and lives inside the existing HUD. All
/// settled artwork stays in the projected Notes layer and shares its wipe.
final class HUDNotesInteraction: NSObject, NSTextViewDelegate {
    private let canvas: NotesCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var workspaceProject: ((CGRect) -> CGRect)?
    var unproject: ((CGPoint) -> CGPoint?)?
    var workspaceUnproject: ((CGPoint) -> CGPoint?)?
    var moduleToWorkspace: ((CGRect) -> CGRect)?
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
    private var formatting: NotesFormattingControls?
    private var formattingNoteID: UUID?
    private var mediaSourceChooser: NotesMediaSourceChooser?
    private var shelfPicker: NotesShelfMediaPicker?
    private var mediaSourcePoint: CGPoint?
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
    var capturesPointer: Bool { formatting != nil || mediaSourceChooser != nil || shelfPicker != nil }

    func hitTestMenu(at hostPoint: CGPoint) -> NSView? {
        let menus: [NotesRetainedMenu] = [formatting, mediaSourceChooser, shelfPicker].compactMap { $0 }
        return menus.reversed().compactMap { $0.hitTest(hostPoint) }.first
    }

    /// The host routes its first background click here before invoking a module.
    /// Native child controls receive clicks inside their projected menu frames.
    @discardableResult func dismissMenuIfOutside(at hostPoint: CGPoint) -> Bool {
        let menus: [NotesRetainedMenu] = [formatting, mediaSourceChooser, shelfPicker].compactMap { $0 }
        guard !menus.isEmpty, !menus.contains(where: { $0.hitTest(hostPoint) != nil }) else { return false }
        closeFormatting()
        dismiss(mediaSourceChooser); mediaSourceChooser = nil
        dismiss(shelfPicker); shelfPicker = nil
        return true
    }

    init(canvas: NotesCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        super.init()
        canvas.onEdit = { [weak self] in self?.beginEditing($0) }
        canvas.onChooseImage = { [weak self] in self?.chooseMediaSource(at: $0) }
        canvas.onFormat = { [weak self] in self?.showFormatting(noteID: $0, kind: $1) }
        canvas.onChange = { [weak self] in self?.refreshActions() }
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
        closeFormatting(); dismiss(mediaSourceChooser); mediaSourceChooser = nil
        dismiss(shelfPicker); shelfPicker = nil
        retiringMenuArtwork?.removeAllAnimations(); retiringMenuArtwork?.removeFromSuperlayer(); retiringMenuArtwork = nil
        importGeneration += 1
        pendingImport?.cancel(); pendingImport = nil
        canvas.cancelInteraction()
        dragging = false; externalDrag = false; active = false
        dialogGeneration += 1
        let panel = chooser
        chooser = nil
        panel?.cancel(nil)
        buttons.values.forEach { $0.isHidden = true }; mediaSliders.values.forEach { $0.isHidden = true }
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
        if let action = canvas.formatAction(at: point) { onLock?(); canvas.perform(actionID: action); return true }
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

    func setVisible(_ visible: Bool) { canvas.setVisible(visible) }
    func mouseMoved(at point: CGPoint?) { canvas.mouseMoved(at: active ? point : nil) }
    @discardableResult func rightMouseDown(at point: CGPoint) -> Bool {
        guard active else { return false }
        return canvas.rightMouseDown(at: point)
    }

    /// Native editor wheel events are handled by its NSScrollView. A bubbled
    /// event over that same card must not save/destroy it or move its backdrop.
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard active else { return false }
        if let request, canvas.contains(noteID: request.noteID, point: point) { return true }
        return canvas.scroll(at: point, delta: delta)
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
        let initialFont = NSFont.systemFont(ofSize: max(1, next.fontSize * scale))
        text.font = initialFont
        text.textColor = isDark?() == true ? .white : .black
        text.insertionPointColor = text.textColor ?? .white
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
            text.textStorage?.setAttributedString(rich.attributed(next.text, scale: scale,
                defaultColor: isDark?() == true ? .white : .black))
        }
        if next.richText == nil, (text.textStorage?.length ?? 0) > 0 {
            text.textStorage?.addAttribute(.paragraphStyle, value: paragraph, range: NSRange(location: 0, length: (text.string as NSString).length))
        }
        text.delegate = self
        text.setAccessibilityLabel(next.multiline ? L10n.text("Edit text note", "编辑文字便笺") : L10n.text("Edit checklist item", "编辑待办事项"))
        text.setAccessibilityHelp(L10n.text("Escape or Command-Return saves. Text notes support multiple lines.", "按 Esc 或 Command-Return 保存。文字便笺支持多行。"))
        text.onFinish = { [weak self] in self?.finishEditing() }
        text.onToggle = { [weak self] in self?.onToggle?() }
        let projected = HUDProjectedTextEditor(textView: text, rect: rect, host: host,
            parent: next.space == .workspace ? canvas.workspaceLayer : canvas.layer)
        projected.project = { [weak self] in self?.projectRect($0, space: next.space) ?? $0 }
        projected.unproject = { [weak self] point in
            guard let self else { return nil }
            let inverse = next.space == .workspace ? self.workspaceUnproject : self.unproject
            return inverse == nil ? point : inverse?(point)
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
            let captured = NotesRichText.capture(storage, scale: scale, defaultColor: isDark?() == true ? .white : .black)
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
        canvas.finishEditing(request, text: text, scrollOffset: offset, richText: rich)
        canvas.setEditing(nil)
        if host?.window?.firstResponder === editor { host?.window?.makeFirstResponder(host) }
        refreshActions()
    }

    /// TextKit retains logical dimensions while its owned artwork inherits the
    /// same projection as the note. Moving the HUD cannot rewrite rich text,
    /// undo groups, selection or marked-text composition.
    private func layoutEditor() {
        guard let projectedEditor else { return }
        projectedEditor.setAppearance(
            background: isDark?() == true ? NSColor(white: 0.12, alpha: 1) : NSColor(white: 0.96, alpha: 1),
            border: HUDRuntimeAppearance.accent)
        projectedEditor.refreshProjection()
    }

    func textDidChange(_ notification: Notification) { resizeEditorDocument() }
    func textViewDidChangeSelection(_ notification: Notification) {
        guard let editor else { return }
        let color = editor.typingAttributes[.foregroundColor] as? NSColor ?? (isDark?() == true ? .white : .black)
        canvas.setEditingColor(color)
    }

    private func resizeEditorDocument() { projectedEditor?.resizeDocument() }

    private func showFormatting(noteID: UUID, kind: String) {
        guard active, let host else { return }
        closeFormatting()
        var style = NotesTextStyle()
        if canvas.noteKind(for: noteID) == .drawing { style.color = NotesRGBA(canvas.currentDrawingColor) }
        if let editor {
            style = NotesFormattingEditor.style(in: editor, scale: editorScale,
                defaultColor: isDark?() == true ? .white : .black)
        }
        let controls = NotesFormattingControls(kind: kind, dark: isDark?() == true, style: style)
        formatting = controls; formattingNoteID = noteID
        controls.onClose = { [weak self] in self?.closeFormatting() }
        controls.onChange = { [weak self] change in
            guard let self else { return }
            if self.canvas.noteKind(for: noteID) == .drawing {
                if case .color(let color) = change { self.canvas.setDrawingColor(color) }
            } else { self.applyFormat(change) }
        }
        host.addSubview(controls); layoutFormatting(); reveal(controls)
    }

    private func closeFormatting() {
        let wasOpen = formatting != nil
        dismiss(formatting); formatting = nil; formattingNoteID = nil
        if wasOpen { layoutEditor() }
    }

    private func layoutFormatting() {
        guard let controls = formatting, let id = formattingNoteID, let note = canvas.noteRect(for: id) else { return }
        let logical = controls.contentSize
        let workspace = canvas.workspaceBounds
        let source = CGRect(x: min(workspace.maxX - logical.width, max(workspace.minX, note.minX)),
            y: min(workspace.maxY - logical.height, max(workspace.minY, note.maxY + 4)),
            width: logical.width, height: logical.height)
        positionMenu(controls, source: source, space: .workspace)
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
        if case .color(let color) = change { canvas.setEditingColor(color) }
        resizeEditorDocument()
    }

    private func chooseMediaSource(at point: CGPoint) {
        guard active, let host else { return }
        finishEditing(); closeFormatting(); dismiss(mediaSourceChooser)
        dismiss(shelfPicker); shelfPicker = nil
        let chooser = NotesMediaSourceChooser(dark: isDark?() == true)
        mediaSourceChooser = chooser; mediaSourcePoint = point
        chooser.onChoose = { [weak self] shelf in
            guard let self else { return }
            self.dismiss(self.mediaSourceChooser); self.mediaSourceChooser = nil
            if shelf { self.onChooseShelfMedia?(point) }
            else { self.chooseImages(at: point) }
        }
        chooser.onCancel = { [weak self] in self?.dismiss(self?.mediaSourceChooser); self?.mediaSourceChooser = nil }
        host.addSubview(chooser)
        layoutMediaMenus()
        reveal(chooser)
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
                        _ = self.canvas.importMedia(reference: reference,
                            at: CGPoint(x: point.x + CGFloat(index % 4) * 12, y: point.y + CGFloat(index % 4) * 12))
                    }
                case .failure(let error): self.canvas.reportImportError(error)
                }
            }
        }
    }

    func presentShelfMedia(choices: [NotesShelfMediaChoice], at point: CGPoint, onSelect: @escaping (UUID) -> Void) {
        guard active, let host else { return }
        finishEditing(); closeFormatting(); dismiss(shelfPicker)
        dismiss(mediaSourceChooser); mediaSourceChooser = nil
        let picker = NotesShelfMediaPicker(choices: choices, dark: isDark?() == true)
        shelfPicker = picker
        picker.onCancel = { [weak self] in self?.dismiss(self?.shelfPicker); self?.shelfPicker = nil }
        picker.onSelect = { [weak self] id in
            guard let self, self.active else { return }
            self.dismiss(self.shelfPicker); self.shelfPicker = nil
            onSelect(id)
        }
        host.addSubview(picker)
        layoutMediaMenus()
        reveal(picker)
    }

    private func layoutMediaMenus() {
        if let source = mediaSourceChooser {
            let size = source.contentSize
            let rect = CGRect(x: 96, y: 212, width: size.width, height: size.height)
            positionMenu(source, source: moduleToWorkspace?(rect) ?? rect, space: .workspace)
        }
        if let picker = shelfPicker {
            let size = picker.contentSize
            let rect = CGRect(x: 30, y: 28, width: size.width, height: size.height)
            positionMenu(picker, source: moduleToWorkspace?(rect) ?? rect, space: .workspace)
        }
    }

    private func positionMenu(_ menu: NotesRetainedMenu, source: CGRect, space: NotesCoordinateSpace) {
        let projected = projectRect(source, space: space)
        let size = menu.contentSize
        let sx = source.width / max(1, size.width), sy = source.height / max(1, size.height)
        menu.frame = projected; menu.bounds = CGRect(origin: .zero, size: size)
        menu.hostToLocal = { [weak self, weak menu] point in
            guard let self, let menu else { return nil }
            let inverse = space == .workspace ? self.workspaceUnproject : self.unproject
            if let inverse {
                guard let logical = inverse(point) else { return nil }
                return CGPoint(x: (logical.x - source.minX) / sx, y: (logical.y - source.minY) / sy)
            }
            return CGPoint(x: (point.x - menu.frame.minX) * size.width / max(1, menu.frame.width),
                           y: (point.y - menu.frame.minY) * size.height / max(1, menu.frame.height))
        }
        menu.projectLocal = { [weak self] rect in
            self?.projectRect(rect.applying(CGAffineTransform(scaleX: sx, y: sy))
                .offsetBy(dx: source.minX, dy: source.minY), space: space) ?? rect
        }
        let parent = space == .workspace ? canvas.workspaceLayer : canvas.layer
        CATransaction.begin(); CATransaction.setDisableActions(true)
        if menu.artwork.superlayer !== parent { parent.addSublayer(menu.artwork) }
        menu.artwork.position = source.origin; menu.artwork.bounds = CGRect(origin: .zero, size: size)
        menu.artwork.setAffineTransform(CGAffineTransform(scaleX: sx, y: sy))
        CATransaction.commit()
        menu.layoutAccessibility()
    }

    private func reveal(_ view: NSView) {
        guard !HUDRuntimeAppearance.reduceMotion, let layer = (view as? NotesRetainedMenu)?.artwork ?? view.layer else { return }
        let movement = CABasicAnimation(keyPath: "transform.translation.y")
        movement.fromValue = 6; movement.toValue = 0
        let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 0; fade.toValue = 1
        let group = CAAnimationGroup(); group.animations = [movement, fade]; group.duration = 0.14
        group.timingFunction = CAMediaTimingFunction(name: .easeOut)
        layer.add(group, forKey: "notes.controls.reveal")
    }

    private func dismiss(_ menu: NotesRetainedMenu?) {
        guard let menu else { return }
        retiringMenuArtwork?.removeAllAnimations(); retiringMenuArtwork?.removeFromSuperlayer(); retiringMenuArtwork = nil
        guard active, !HUDRuntimeAppearance.reduceMotion, menu.artwork.superlayer != nil else {
            menu.artwork.removeAllAnimations(); menu.removeFromSuperview(); return
        }
        let layer = menu.artwork, opacity = layer.presentation()?.opacity ?? layer.opacity
        menu.detachInputKeepingArtwork()
        layer.removeAllAnimations(); retiringMenuArtwork = layer
        CATransaction.begin(); CATransaction.setDisableActions(true)
        CATransaction.setCompletionBlock { [weak self, weak layer] in
            layer?.removeFromSuperlayer()
            if self?.retiringMenuArtwork === layer { self?.retiringMenuArtwork = nil }
        }
        layer.opacity = 0
        let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = opacity; fade.toValue = 0
        let movement = CABasicAnimation(keyPath: "transform.translation.y"); movement.fromValue = 0; movement.toValue = 4
        let group = CAAnimationGroup(); group.animations = [fade, movement]; group.duration = 0.12
        group.timingFunction = CAMediaTimingFunction(name: .easeIn)
        layer.add(group, forKey: "notes.controls.dismiss")
        CATransaction.commit()
    }

    private func chooseImages(at point: CGPoint) {
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
        layoutFormatting()
        layoutMediaMenus()
        let sliders = canvas.mediaSeekActions
        let wantedSliders = Set(sliders.map(\.id))
        for id in Array(mediaSliders.keys) where !wantedSliders.contains(id) { mediaSliders.removeValue(forKey: id)?.removeFromSuperview() }
        for action in sliders {
            guard let id = UUID(uuidString: action.id) else { continue }
            let slider: HUDNotesSeekSlider
            if let existing = mediaSliders[action.id] { slider = existing }
            else {
                slider = HUDNotesSeekSlider(frame: .zero)
                slider.readValue = { [weak canvas] in canvas?.mediaPosition(for: id) ?? 0 }
                slider.maximum = { [weak canvas] in canvas?.mediaDuration(for: id) ?? 0 }
                slider.onChange = { [weak canvas] in canvas?.seekMedia(id: id, to: $0) }
                host.addSubview(slider); mediaSliders[action.id] = slider
            }
            slider.isHidden = false; slider.setAccessibilityLabel(action.label)
            slider.frame = projectRect(action.rect, space: .workspace)
        }
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
        if !sender.actionID.contains(":format") { finishEditing() }
        onLock?()
        canvas.perform(actionID: sender.actionID)
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
    var sourceSpace: NotesCoordinateSpace = .workspace
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
