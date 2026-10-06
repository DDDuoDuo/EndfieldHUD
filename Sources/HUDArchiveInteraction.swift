import AppKit
import QuartzCore
import UniformTypeIdentifiers

final class HUDArchiveInteraction: NSObject, NSTextViewDelegate {
    let canvas: ArchiveCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var unproject: ((CGPoint) -> CGPoint?)?
    var shelfChoices: (() -> [NotesShelfMediaChoice])?
    var shelfAccess: ((UUID) throws -> ShelfFileAccess)?
    private var active = false
    private var presented = false
    private var editors: [String: HUDProjectedTextEditor] = [:]
    private var editorID: UUID?
    private var editorBodyRect = CGRect.zero
    private var editorAppearanceKey = ""
    private var retiringEditors: CALayer?
    private var retireEditorsWork: DispatchWorkItem?
    private var buttons: [String: ArchiveActionButton] = [:]
    private var galleryScroller: ArchiveGalleryScroller?
    private var scrollGrabOffset: CGFloat?
    private var secondaryMenu: NotesRetainedMenu?
    private var menuOrigin = CGPoint.zero
    private var retiringMenu: CALayer?
    private var retireMenuWork: DispatchWorkItem?
    private var panel: NSOpenPanel?
    private var importGeneration = 0
    private var importTicket = ArchiveImportTicket()
    private let importQueue = DispatchQueue(label: "EndfieldHUD.ArchiveImport", qos: .utility)
    private var edited = false
    private var syncing = false
    private var seeking = false
    private var categoryEditor: HUDProjectedTextEditor?
    private var categoryAction: ((UUID?) -> Void)?
    private var formattingTarget: String?
    var capturesPointer: Bool { secondaryMenu != nil || scrollGrabOffset != nil }
    var isPresentingPanel: Bool { panel != nil }
    init(canvas: ArchiveCanvas, host: NSView) {
        self.canvas = canvas; self.host = host; super.init()
        canvas.onChange = { [weak self] in self?.synchronize() }
        canvas.onAction = { [weak self] in self?.perform($0) }
        canvas.onBeforeAction = { [weak self] in self?.commitEditing() }
    }
    deinit {
        importTicket.cancel(); panel?.cancel(nil)
        retireMenuWork?.cancel(); retireEditorsWork?.cancel()
        retiringMenu?.removeFromSuperlayer(); retiringEditors?.removeFromSuperlayer()
        secondaryMenu?.removeFromSuperview(); categoryEditor?.dispose()
        editors.values.forEach { $0.textView.delegate = nil; $0.dispose() }
        buttons.values.forEach { $0.removeFromSuperview() }
        galleryScroller?.removeFromSuperview()
        canvas.setVisible(false)
    }
    func setPresented(_ value: Bool) {
        guard presented != value else { return }; presented = value
        if !value { setActive(false) }
        canvas.setVisible(value)
    }
    func setActive(_ value: Bool) {
        guard active != value else { return }; active = value
        if value { clearRetiringEditors(); setPresented(true); synchronize() }
        else {
            seeking = false; scrollGrabOffset = nil; commitEditing(); freezeEditors(); endEditors(); dismissMenu(animated: false)
            panel?.cancel(nil); panel = nil; importGeneration += 1; importTicket.cancel(); importTicket = ArchiveImportTicket()
            buttons.values.forEach { $0.isHidden = true }
            galleryScroller?.isHidden = true
        }
    }
    func deactivate() { setActive(false); setPresented(false) }
    private func commitEditing() {
        guard canvas.controller.selected?.id == editorID else { return }
        for editor in editors.values where editor.textView.hasMarkedText() {
            editor.textView.unmarkText()
            textDidChange(Notification(name: NSText.didChangeNotification, object: editor.textView))
        }
        if let responder = host?.window?.firstResponder as? NSTextView,
           editors.values.contains(where: { $0.textView === responder }) { host?.window?.makeFirstResponder(host) }
        canvas.controller.flush()
    }
    private func endEditors() {
        if edited { canvas.controller.onEvent?("edited"); edited = false }
        editors.values.forEach { $0.textView.delegate = nil; $0.dispose() }; editors.removeAll(); editorID = nil
        editorAppearanceKey = ""; canvas.formattingField = nil; canvas.controller.flush()
    }
    private func synchronize() {
        guard active, !syncing, let host else { return }; syncing = true
        defer { syncing = false }
        let entry = canvas.controller.selected
        if entry?.id != editorID {
            if editorID != nil { freezeEditors(animated: true) }
            endEditors()
            if let entry {
                editorID = entry.id; editorBodyRect = canvas.bodyRect
                for (field, value, rect) in [("title", entry.title, ArchiveCanvas.titleRect), ("date", ArchiveCanvas.dateString(entry.date), ArchiveCanvas.dateRect), ("body", entry.body, canvas.bodyRect)] {
                    installEditor(field: field, value: value, rect: rect, host: host)
                }
            }
        } else if entry != nil, canvas.bodyRect != editorBodyRect, let body = editors.removeValue(forKey: "body") {
            // Attachments resize only the body viewport. Reuse TextKit itself
            // so selection, composition and undo survive this geometry change.
            let text = body.textView, offset = body.scrollView.contentView.bounds.origin
            let wasFirstResponder = host.window?.firstResponder === text
            text.delegate = nil; body.dispose(); editorBodyRect = canvas.bodyRect
            installEditor(field: "body", value: text.string, rect: canvas.bodyRect, host: host, retaining: text)
            if let replacement = editors["body"] {
                replacement.scrollView.contentView.scroll(to: CGPoint(x: 0, y: min(offset.y, max(0, text.frame.height - canvas.bodyRect.height))))
                replacement.invalidateArtwork()
            }
            if wasFirstResponder { host.window?.makeFirstResponder(text) }
        }
        let appearance = "\(canvas.dark):\(L10n.resolvedLanguage.rawValue)"
        if let entry, editorAppearanceKey != appearance {
            editorAppearanceKey = appearance
            for (field, editor) in editors {
                let ink: NSColor = canvas.dark ? .white : .black
                let storedRich = field == "title" ? entry.titleRichText : entry.bodyRichText
                let style = field == "title" ? entry.titleStyle : entry.bodyStyle
                // Only theme-default runs change color. Explicit user colors survive.
                let storage = editor.textView.textStorage
                if let storage, storage.length > 0 {
                    let ranges = storedRich?.runs.filter { $0.style.color == nil }.map(\.range)
                        ?? [NSRange(location: 0, length: storage.length)]
                    for range in ranges where NSMaxRange(range) <= storage.length { storage.addAttribute(.foregroundColor, value: ink, range: range) }
                }
                var typing = editor.textView.typingAttributes
                if field == "date" || style.color == nil { typing[.foregroundColor] = ink }
                editor.textView.typingAttributes = typing; editor.textView.insertionPointColor = ink
                let label = field == "body" ? L10n.text("Content", "内容") : field == "title" ? L10n.text("Title", "标题") : L10n.text("Date", "日期")
                editor.textView.setAccessibilityLabel(label)
                editor.placeholder = field == "date" ? "" : label
                editor.placeholderColor = NSColor(white: canvas.dark ? 0.55 : 0.45, alpha: 1)
                editor.setAppearance(background: NSColor(white: canvas.dark ? 0.065 : 0.94, alpha: 1), border: .clear, radius: 0)
                editor.invalidateArtwork()
            }
        }
        layoutAccessibility()
    }
    private func installEditor(field: String, value: String, rect: CGRect, host: NSView, retaining existing: HUDProjectedTextView? = nil) {
        let text = existing ?? ArchiveDocumentTextView(frame: .zero)
        if existing == nil {
            text.isEditable = true; text.isSelectable = true
            text.isRichText = field != "date"; text.importsGraphics = false; text.allowsUndo = true
            let entry = canvas.controller.selected
            let style = field == "title" ? entry?.titleStyle ?? NotesTextStyle(fontSize: 17) : entry?.bodyStyle ?? NotesTextStyle()
            let rich = field == "title" ? entry?.titleRichText : entry?.bodyRichText
            let ink: NSColor = canvas.dark ? .white : .black
            let paragraph = NSMutableParagraphStyle(); paragraph.lineSpacing = 1
            var attributes = style.attributes(defaultColor: ink); attributes[.paragraphStyle] = paragraph
            if field == "date" { attributes[.font] = NSFont.systemFont(ofSize: 10) }
            text.defaultParagraphStyle = paragraph; text.typingAttributes = attributes
            let value = field != "date" ? rich?.attributed(value, defaultColor: ink) ?? NSAttributedString(string: value, attributes: attributes) : NSAttributedString(string: value, attributes: attributes)
            text.textStorage?.setAttributedString(value)
            text.textStorage?.addAttribute(.paragraphStyle, value: paragraph, range: NSRange(location: 0, length: value.length))
        }
        text.delegate = self
        (text as? ArchiveDocumentTextView)?.onFocus = { [weak self, weak text] in
            guard let self, let text, field != "date" else { return }
            self.canvas.formattingColor = text.typingAttributes[.foregroundColor] as? NSColor ?? (self.canvas.dark ? .white : .black)
            self.canvas.formattingField = field
        }
        let editor = HUDProjectedTextEditor(textView: text, rect: rect, host: host, parent: canvas.layer)
        editor.project = { [weak self] in self?.project?($0) ?? $0 }
        editor.unproject = { [weak self] point in guard let self else { return nil }; return self.unproject?(point) ?? (self.unproject == nil ? point : nil) }
        if field != "body" { editor.configureSingleLine() }
        editor.resizeDocument(); editors[field] = editor; editorAppearanceKey = ""
        if existing == nil && !HUDRuntimeAppearance.reduceMotion {
            let reveal = CABasicAnimation(keyPath: "opacity"); reveal.fromValue = 0; reveal.toValue = 1; reveal.duration = 0.18
            editor.artwork.add(reveal, forKey: "archive.editor.reveal")
        }
    }
    private func freezeEditors(animated: Bool = false) {
        clearRetiringEditors()
        guard !editors.isEmpty else { return }
        let frozen = CALayer(); frozen.name = "archive.outgoingText"; frozen.frame = canvas.layer.bounds; frozen.zPosition = 90
        func copy(_ source: CALayer) -> CALayer {
            let result = CALayer(); result.frame = source.frame; result.contents = source.contents
            result.contentsScale = source.contentsScale; result.contentsGravity = source.contentsGravity
            result.backgroundColor = source.backgroundColor; result.opacity = source.opacity
            result.cornerRadius = source.cornerRadius; result.masksToBounds = source.masksToBounds
            result.borderColor = source.borderColor; result.borderWidth = source.borderWidth; result.isHidden = source.isHidden
            source.sublayers?.forEach { result.addSublayer(copy($0)) }; return result
        }
        for editor in editors.values {
            editor.captureVisibleArtwork(); frozen.addSublayer(copy(editor.artwork))
        }
        canvas.layer.addSublayer(frozen); retiringEditors = frozen
        if animated && !HUDRuntimeAppearance.reduceMotion {
            let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 1; fade.toValue = 0; fade.duration = 0.18
            frozen.opacity = 0; frozen.add(fade, forKey: "archive.text.exit")
        }
        let work = DispatchWorkItem { [weak self, weak frozen] in
            guard let self, let frozen, self.retiringEditors === frozen else { return }; self.clearRetiringEditors()
        }
        retireEditorsWork = work; DispatchQueue.main.asyncAfter(deadline: .now() + (animated ? 0.2 : 0.6), execute: work)
    }
    private func clearRetiringEditors() {
        retireEditorsWork?.cancel(); retireEditorsWork = nil
        retiringEditors?.removeFromSuperlayer(); retiringEditors = nil
    }
    func textDidBeginEditing(_ notification: Notification) {
        guard let text = notification.object as? NSTextView,
              let field = editors.first(where: { $0.value.textView === text })?.key, field != "date" else { return }
        canvas.formattingField = field
        canvas.formattingColor = text.typingAttributes[.foregroundColor] as? NSColor ?? (canvas.dark ? .white : .black)
        canvas.render()
    }
    func textViewDidChangeSelection(_ notification: Notification) {
        guard let text = notification.object as? NSTextView,
              editors[canvas.formattingField ?? ""]?.textView === text else { return }
        let style = NotesFormattingEditor.style(in: text, defaultColor: canvas.dark ? .white : .black)
        let color = style.color?.color ?? (canvas.dark ? NSColor.white : .black)
        if color != canvas.formattingColor { canvas.formattingColor = color; canvas.render() }
    }
    func textDidChange(_ notification: Notification) {
        guard let text = notification.object as? NSTextView, !text.hasMarkedText() else { return }
        if text === categoryEditor?.textView {
            let value = String(text.string.replacingOccurrences(of: "\0", with: "").prefix(40))
            if value != text.string { text.string = value }; categoryEditor?.resizeDocument(); return
        }
        guard let field = editors.first(where: { $0.value.textView === text })?.key,
              var entry = canvas.controller.selected else { return }
        if field == "title" {
            let value = String(text.string.replacingOccurrences(of: "\0", with: "").prefix(200))
            if value != text.string { text.string = value }; entry.title = value
        } else if field == "body" {
            if text.string.utf8.count > ArchiveStore.maximumBodyBytes {
                var bytes = Data(text.string.utf8.prefix(ArchiveStore.maximumBodyBytes))
                while String(data: bytes, encoding: .utf8) == nil && !bytes.isEmpty { bytes.removeLast() }
                text.string = String(data: bytes, encoding: .utf8) ?? ""
            }; entry.body = text.string
        } else { guard let date = ArchiveCanvas.parseDate(text.string) else { return }; entry.date = date }
        if field != "date", let storage = text.textStorage {
            let ink: NSColor = canvas.dark ? .white : .black
            let rich = NotesRichText.capture(storage, defaultColor: ink)
            let typing = NotesRichText.capture(NSAttributedString(string: "x", attributes: text.typingAttributes), defaultColor: ink).runs.first?.style ?? NotesTextStyle()
            if field == "title" { entry.titleRichText = rich; entry.titleStyle = typing }
            else { entry.bodyRichText = rich; entry.bodyStyle = typing }
        }
        canvas.controller.update(entry); edited = true; editors[field]?.resizeDocument()
    }
    func textDidEndEditing(_ notification: Notification) {
        if let dateEditor = editors["date"], let source = notification.object as? NSTextView,
           source === dateEditor.textView, let entry = canvas.controller.selected {
            source.string = ArchiveCanvas.dateString(entry.date); dateEditor.resizeDocument()
        }
        canvas.controller.flush()
        DispatchQueue.main.async { [weak self] in
            guard let self, self.secondaryMenu == nil,
                  !self.editors.values.contains(where: { $0.textView === self.host?.window?.firstResponder }) else { return }
            self.canvas.formattingField = nil
        }
    }
    func textView(_ textView: NSTextView, doCommandBy selector: Selector) -> Bool {
        guard !textView.hasMarkedText() else { return false }
        if textView === categoryEditor?.textView {
            if selector == #selector(NSResponder.insertNewline(_:)) { finishCategory(); return true }
            if selector == #selector(NSResponder.cancelOperation(_:)) { dismissMenu(); return true }
        }
        if selector == #selector(NSResponder.cancelOperation(_:)) || (selector == #selector(NSResponder.insertNewline(_:)) && editors["body"]?.textView !== textView) {
            host?.window?.makeFirstResponder(host); canvas.controller.flush(); return true
        }
        return false
    }
    func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active else { return false }
        if let menu = secondaryMenu {
            let local = CGPoint(x: point.x - menuOrigin.x, y: point.y - menuOrigin.y)
            if menu.bounds.contains(local) { menu.activate(at: local) } else { dismissMenu() }; return true
        }
        guard canvas.layer.bounds.contains(point) else { return false }
        if let thumb = canvas.galleryScrollThumb, ArchiveCanvas.galleryScrollerRect.insetBy(dx: -3, dy: 0).contains(point) {
            scrollGrabOffset = thumb.contains(point) ? point.y - thumb.minY : thumb.height / 2
            dragGalleryScroll(to: point); return true
        }
        if canvas.presentation?.reference.kind == .video, ArchiveCanvas.seekRect.insetBy(dx: 0, dy: -3).contains(point) {
            seeking = true; seek(point); return true
        }
        if let action = canvas.actions.last(where: { $0.enabled && $0.rect.contains(point) }) { canvas.perform(action.id) }
        return true
    }
    func mouseDragged(to point: CGPoint) {
        guard active else { return }
        if scrollGrabOffset != nil { dragGalleryScroll(to: point) }
        else if seeking { seek(point) }
    }
    func mouseUp() { seeking = false; scrollGrabOffset = nil }
    private func dragGalleryScroll(to point: CGPoint) {
        guard let grab = scrollGrabOffset, let thumb = canvas.galleryScrollThumb else { return }
        let travel = ArchiveCanvas.galleryScrollerRect.height - thumb.height
        guard travel > 0 else { return }
        canvas.setGalleryScrollOffset((point.y - ArchiveCanvas.galleryScrollerRect.minY - grab) / travel * canvas.galleryScrollMaximum)
    }
    private func seek(_ point: CGPoint) {
        guard let media = canvas.presentation, let duration = media.reference.duration else { return }
        media.seek(to: duration * min(1, max(0, (point.x - ArchiveCanvas.seekRect.minX) / ArchiveCanvas.seekRect.width)))
    }
    func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard active, delta.isFinite else { return false }
        if let menu = secondaryMenu { (menu as? ArchiveCategoryMenu)?.scroll(delta: delta); return true }
        guard canvas.layer.bounds.contains(point) else { return false }
        if let body = editors["body"], canvas.bodyRect.contains(point) { body.scroll(delta: delta) }
        else { canvas.scroll(at: point, delta: delta) }; return true
    }
    func keyDown(_ event: NSEvent) -> Bool {
        guard active else { return false }
        if event.keyCode == 53, secondaryMenu != nil { dismissMenu(); return true }
        if event.modifierFlags.contains(.command), event.charactersIgnoringModifiers == "n" { perform("new"); return true }
        return false
    }
    func hitTestMenu(at point: CGPoint) -> NSView? { categoryEditor?.hitTest(point) ?? secondaryMenu?.hitTest(point) }
    private func perform(_ id: String) {
        guard active else { return }
        if id.hasPrefix("format") {
            showFormatting(String(id.dropFirst(6)).lowercased()); return
        }
        if id == "new" || id == "moveCategory" {
            let moving = id == "moveCategory", selected = canvas.controller.selected?.id
            let menu = ArchiveCategoryMenu(categories: canvas.controller.categories, dark: canvas.dark)
            let action: (UUID?) -> Void = { [weak self] category in
                guard let self, self.active else { return }
                if moving { if self.canvas.controller.selected?.id == selected { self.canvas.controller.moveSelected(to: category) } }
                else { self.canvas.controller.create(categoryID: category) }
            }
            menu.onChoice = { [weak self] category in self?.dismissMenu(); action(category) }
            menu.onNewCategory = { [weak self, weak menu] in guard let self, let menu else { return }; self.installCategoryEditor(menu: menu, action: action) }
            menu.onCreate = { [weak self] in self?.finishCategory() }
            show(menu, at: CGPoint(x: moving ? 126 : 100, y: moving ? 38 : 49))
        } else if id == "deleteCategory", let categoryID = canvas.categoryID,
                  canvas.controller.categories.contains(where: { $0.id == categoryID }) {
            let menu = ArchiveChoiceMenu(title: L10n.text("Remove this category?", "删除此分类？"),
                detail: L10n.text("Documents will move to Uncategorized.", "档案将移至未分类。"),
                choices: [("cancel", L10n.text("Cancel", "取消")), ("delete", L10n.text("Delete", "删除"))], dark: canvas.dark)
            menu.onChoice = { [weak self] value in
                guard let self else { return }; self.dismissMenu()
                if value == "delete" { self.canvas.controller.deleteCategory(categoryID) }
            }
            show(menu, at: CGPoint(x: 14, y: 177))
        } else if id == "delete" {
            let selected = canvas.controller.selected?.id
            let menu = ArchiveChoiceMenu(title: L10n.text("Delete this document?", "删除此档案？"), choices: [("cancel", L10n.text("Cancel", "取消")), ("delete", L10n.text("Delete", "删除"))], dark: canvas.dark)
            menu.onChoice = { [weak self] value in guard let self else { return }; self.dismissMenu(); if value == "delete", selected == self.canvas.controller.selected?.id { self.endEditors(); self.canvas.controller.deleteSelected() } }
            show(menu)
        } else if id == "addMedia" {
            let menu = NotesMediaSourceChooser(dark: canvas.dark)
            menu.onChoose = { [weak self] shelf in self?.dismissMenu(); if shelf { self?.showShelf() } else { self?.chooseFiles() } }
            let anchor = canvas.actions.first { $0.id == "addMedia" }?.rect ?? CGRect(x: 327, y: 5, width: 26, height: 25)
            show(menu, at: CGPoint(x: min(392 - menu.contentSize.width, max(8, anchor.maxX - menu.contentSize.width)), y: anchor.maxY + 6))
        } else if id == "removeMedia", var entry = canvas.controller.selected, entry.media.indices.contains(canvas.mediaIndex) {
            entry.media.remove(at: canvas.mediaIndex); canvas.controller.update(entry); canvas.controller.onEvent?("mediaRemoved"); canvas.render(animated: true)
        }
    }
    private func showFormatting(_ kind: String) {
        let field = canvas.formattingField ?? "body"
        guard let editor = editors[field], field != "date" else { return }
        if editor.textView.hasMarkedText() { editor.textView.unmarkText(); textDidChange(Notification(name: NSText.didChangeNotification, object: editor.textView)) }
        let style = NotesFormattingEditor.style(in: editor.textView, defaultColor: canvas.dark ? .white : .black)
        let menu = NotesFormattingControls(kind: kind, dark: canvas.dark, style: style)
        menu.onChange = { [weak self, weak editor] change in
            guard let self, let editor, self.editors[field] === editor else { return }
            NotesFormattingEditor.apply(change, to: editor.textView)
            self.textDidChange(Notification(name: NSText.didChangeNotification, object: editor.textView))
            self.textViewDidChangeSelection(Notification(name: NSTextView.didChangeSelectionNotification, object: editor.textView))
        }
        show(menu, at: CGPoint(x: 24, y: 376 - menu.contentSize.height))
        formattingTarget = field; canvas.formattingField = field
    }
    private func installCategoryEditor(menu: ArchiveCategoryMenu, action: @escaping (UUID?) -> Void) {
        guard let host, secondaryMenu === menu else { return }
        categoryEditor?.dispose(); categoryAction = action
        let rect = menu.nameRect.offsetBy(dx: menuOrigin.x, dy: menuOrigin.y)
        let text = HUDProjectedTextView(frame: .zero); text.isRichText = false; text.isEditable = true; text.isSelectable = true
        text.font = .systemFont(ofSize: 12); text.textColor = canvas.dark ? .white : .black
        text.insertionPointColor = canvas.dark ? .white : .black; text.delegate = self
        let editor = HUDProjectedTextEditor(textView: text, rect: rect, host: host, parent: canvas.layer)
        editor.project = { [weak self] in self?.project?($0) ?? $0 }
        editor.unproject = { [weak self] in self?.unproject?($0) ?? (self?.unproject == nil ? $0 : nil) }
        editor.configureSingleLine(); editor.placeholder = L10n.text("Category name", "分类名称")
        editor.textView.setAccessibilityLabel(L10n.text("Category name", "分类名称"))
        editor.setAppearance(background: NSColor(white: canvas.dark ? 0.12 : 0.92, alpha: 1), border: HUDRuntimeAppearance.accent)
        editor.artwork.zPosition = 2_000_001; editor.resizeDocument(); categoryEditor = editor
        animate(editor.artwork, showing: true); host.window?.makeFirstResponder(text)
    }
    private func finishCategory() {
        guard let editor = categoryEditor else { return }
        if editor.textView.hasMarkedText() { editor.textView.unmarkText() }
        let name = editor.textView.string.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty, let id = canvas.controller.createCategory(named: name) else { return }
        let action = categoryAction; dismissMenu(); action?(id)
    }
    private func show(_ menu: NotesRetainedMenu, at origin: CGPoint? = nil) {
        dismissMenu(animated: false); guard let host else { return }
        secondaryMenu = menu; menuOrigin = origin ?? CGPoint(x: (400 - menu.contentSize.width) / 2, y: 150)
        menu.artwork.position = menuOrigin; canvas.layer.addSublayer(menu.artwork); host.addSubview(menu)
        menu.hostToLocal = { [weak self] point in guard let self, let local = self.unproject?(point) ?? (self.unproject == nil ? point : nil) else { return nil }; return CGPoint(x: local.x - self.menuOrigin.x, y: local.y - self.menuOrigin.y) }
        menu.projectLocal = { [weak self] rect in guard let self else { return .zero }; let local = rect.offsetBy(dx: self.menuOrigin.x, dy: self.menuOrigin.y); return self.project?(local) ?? local }
        menu.onCancel = { [weak self] in self?.dismissMenu() }; positionMenu(); menu.paint()
        host.window?.makeFirstResponder(menu); layoutAccessibility()
        animate(menu.artwork, showing: true)
    }
    private func positionMenu() {
        guard let menu = secondaryMenu else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        menu.frame = project?(CGRect(origin: menuOrigin, size: menu.contentSize)) ?? CGRect(origin: menuOrigin, size: menu.contentSize)
        menu.bounds = CGRect(origin: .zero, size: menu.contentSize)
        menu.artwork.position = menuOrigin; menu.layoutAccessibility(); CATransaction.commit()
    }
    private func dismissMenu(animated: Bool = true) {
        retireMenuWork?.cancel(); retireMenuWork = nil
        retiringMenu?.removeFromSuperlayer(); retiringMenu = nil
        guard let menu = secondaryMenu else { return }; secondaryMenu = nil
        let returnEditor = formattingTarget.flatMap { editors[$0] }; formattingTarget = nil
        categoryEditor?.textView.delegate = nil; categoryEditor?.dispose(); categoryEditor = nil; categoryAction = nil
        menu.onCancel = nil
        if animated, active, !HUDRuntimeAppearance.reduceMotion {
            menu.detachInputKeepingArtwork(); animate(menu.artwork, showing: false); retiringMenu = menu.artwork
            let work = DispatchWorkItem { [weak self, weak artwork = menu.artwork] in
                guard let self, let artwork, self.retiringMenu === artwork else { return }
                artwork.removeFromSuperlayer(); self.retiringMenu = nil; self.retireMenuWork = nil
            }
            retireMenuWork = work; DispatchQueue.main.asyncAfter(deadline: .now() + 0.16, execute: work)
        } else { menu.removeFromSuperview() }
        if let returnEditor { host?.window?.makeFirstResponder(returnEditor.textView) } else { host?.window?.makeFirstResponder(host) }
        layoutAccessibility()
    }
    private func animate(_ layer: CALayer, showing: Bool) {
        guard !HUDRuntimeAppearance.reduceMotion else { if !showing { layer.removeFromSuperlayer() }; return }
        let animation = CABasicAnimation(keyPath: "opacity"); animation.fromValue = showing ? 0 : 1; animation.toValue = showing ? 1 : 0; animation.duration = 0.16
        animation.fillMode = .forwards; animation.isRemovedOnCompletion = showing; layer.add(animation, forKey: "archive.menu")
    }
    private func showShelf() {
        let menu = NotesShelfMediaPicker(choices: shelfChoices?() ?? [], dark: canvas.dark)
        menu.onSelect = { [weak self] id in
            guard let self else { return }; self.dismissMenu()
            do { guard let access = try self.shelfAccess?(id) else { return }; self.importFiles([access.url], release: { access.close() }) }
            catch { self.canvas.statusMessage = error.localizedDescription; self.canvas.render() }
        }; show(menu)
    }
    private func chooseFiles() {
        guard let window = host?.window, panel == nil else { return }
        let chooser = NSOpenPanel(); chooser.canChooseDirectories = false; chooser.allowsMultipleSelection = true
        if #available(macOS 11.0, *) { chooser.allowedContentTypes = NotesMediaFactory.supportedFileExtensions.compactMap { UTType(filenameExtension: $0) } }
        else { chooser.allowedFileTypes = NotesMediaFactory.supportedFileExtensions }
        panel = chooser
        chooser.beginSheetModal(for: window) { [weak self, weak chooser] result in
            guard let self, let chooser, self.panel === chooser else { return }; self.panel = nil
            if result == .OK, self.active { self.importFiles(chooser.urls) }
        }
    }
    func importFiles(_ urls: [URL], release: (() -> Void)? = nil) {
        guard active, let selected = canvas.controller.selected else { release?(); return }
        let remaining = max(0, 16 - selected.media.count), candidates = Array(urls.prefix(remaining))
        let scopes = candidates.map { $0.startAccessingSecurityScopedResource() }
        let generation = importGeneration, ticket = importTicket
        importQueue.async { [weak self] in
            defer { for (url, scope) in zip(candidates, scopes) where scope { url.stopAccessingSecurityScopedResource() }; release?() }
            var references: [NotesMediaReference] = [], errorMessage: String?
            for url in candidates {
                guard !ticket.isCancelled else { break }
                do { references.append(try NotesMediaFactory.makeReference(from: url)) }
                catch { errorMessage = error.localizedDescription }
            }
            DispatchQueue.main.async { [weak self] in
                guard let self, self.active, self.importGeneration == generation,
                      var entry = self.canvas.controller.selected, entry.id == selected.id else { return }
                let added = references.prefix(max(0, 16 - entry.media.count))
                entry.media.append(contentsOf: added)
                self.canvas.statusMessage = errorMessage
                if !added.isEmpty { self.canvas.controller.update(entry); self.canvas.controller.onEvent?("mediaAdded") }
                self.canvas.render(animated: true)
            }
        }
    }
    func layoutAccessibility() {
        guard active, let host else { return }
        editors.values.forEach { $0.refreshProjection() }; categoryEditor?.refreshProjection(); positionMenu()
        let actions = canvas.actions
        let wanted = Set(actions.map(\.id))
        for id in Array(buttons.keys) where !wanted.contains(id) { buttons.removeValue(forKey: id)?.removeFromSuperview() }
        for action in actions {
            let button = buttons[action.id] ?? ArchiveActionButton(frame: .zero)
            if buttons[action.id] == nil { button.isBordered = false; button.target = self; button.action = #selector(activate(_:)); host.addSubview(button); buttons[action.id] = button }
            button.actionID = action.id; button.isHidden = false; button.isEnabled = action.enabled; button.setAccessibilityLabel(accessibleTitle(action)); button.frame = project?(action.rect) ?? action.rect
            button.projectedFrame = { [weak self, weak host] in guard let self, let host, let window = host.window else { return .zero }; return window.convertToScreen(host.convert(self.project?(action.rect) ?? action.rect, to: nil)) }
        }
        if canvas.galleryScrollThumb != nil {
            let scroller = galleryScroller ?? ArchiveGalleryScroller(frame: .zero)
            if galleryScroller == nil { galleryScroller = scroller; scroller.target = self; scroller.action = #selector(scrollGallery(_:)); host.addSubview(scroller) }
            scroller.minValue = 0; scroller.maxValue = Double(canvas.galleryScrollMaximum); scroller.doubleValue = Double(canvas.scrollOffset)
            scroller.frame = project?(ArchiveCanvas.galleryScrollerRect) ?? ArchiveCanvas.galleryScrollerRect
            scroller.setAccessibilityLabel(L10n.text("Documents", "档案")); scroller.isHidden = secondaryMenu != nil
        } else { galleryScroller?.isHidden = true }
        HUDControlHighlightLayer.requestRefresh(on: host)
    }
    @objc private func scrollGallery(_ sender: NSSlider) { guard active else { return }; canvas.setGalleryScrollOffset(CGFloat(sender.doubleValue)) }
    private func accessibleTitle(_ action: NotesRetainedMenu.Item) -> String {
        switch action.id {
        case "retry": return L10n.text("Retry", "重试")
        case "back": return L10n.text("Return", "返回")
        case "new": return L10n.text("New document", "新建档案")
        case "deleteCategory": return L10n.text("Delete category", "删除分类")
        case "moveCategory": return L10n.text("Move to category", "移至分类")
        case "formatSize": return L10n.text("Font size", "字号")
        case "formatFont": return L10n.text("Font", "字体")
        case "formatColor": return L10n.text("Color", "颜色")
        case "formatSpecial": return L10n.text("Text style", "特殊")
        case "delete": return L10n.text("Delete", "删除")
        case "addMedia": return L10n.text("Add image/video", "添加图片/视频")
        case "mediaPrevious": return L10n.text("Previous", "上一个")
        case "mediaNext": return L10n.text("Next", "下一个")
        case "mediaPlay": return L10n.text("Play / Pause", "播放 / 暂停")
        case "removeMedia": return L10n.text("Remove media", "移除媒体")
        default: return action.title
        }
    }
    @objc private func activate(_ sender: ArchiveActionButton) { if active { canvas.perform(sender.actionID) } }
}
private final class ArchiveActionButton: NSButton {
    var actionID = ""; var projectedFrame: (() -> CGRect)?
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}
private final class ArchiveChoiceMenu: NotesRetainedMenu {
    var onChoice: ((String) -> Void)?
    let caption: String
    let detail: String?
    init(title: String, detail: String? = nil, choices: [(String, String)], dark: Bool) {
        caption = title; self.detail = detail; super.init(size: CGSize(width: 240, height: detail == nil ? 83 : 111), dark: dark)
        items = choices.enumerated().map { Item(id: $0.element.0, title: $0.element.1, rect: CGRect(x: 10 + $0.offset * 115, y: detail == nil ? 44 : 72, width: 105, height: 28)) }; paint()
    }
    required init?(coder: NSCoder) { nil }
    override func paintContent(on layer: CALayer) {
        text("// " + caption, rect: CGRect(x: 10, y: 12, width: 220, height: 25), size: 12, parent: layer)
        if let detail { text(detail, rect: CGRect(x: 10, y: 40, width: 220, height: 25), size: 10, parent: layer) }
    }
    override func perform(_ id: String) { onChoice?(id) }
}

private final class ArchiveImportTicket {
    private let lock = NSLock()
    private var cancelled = false
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    func cancel() { lock.lock(); cancelled = true; lock.unlock() }
}

/// Shared retained menu geometry, with a scrollable category list and an inline
/// projected name editor supplied by the interaction owner.
private final class ArchiveCategoryMenu: NotesRetainedMenu {
    var onChoice: ((UUID?) -> Void)?
    var onNewCategory: (() -> Void)?
    var onCreate: (() -> Void)?
    let categories: [ArchiveCategory]
    let nameRect = CGRect(x: 12, y: 252, width: 194, height: 28)
    private var editingName = false
    private(set) var scrollOffset: CGFloat = 0
    private let viewport = CGRect(x: 12, y: 43, width: 236, height: 188)
    private var visibleRows: [(String, String, CGRect)] = []
    private var rowLayers: [String: CALayer] = [:]
    private var rowClip: CALayer?
    init(categories: [ArchiveCategory], dark: Bool) {
        self.categories = categories
        super.init(size: CGSize(width: 260, height: 294), dark: dark)
        refresh()
    }
    required init?(coder: NSCoder) { nil }
    private var choices: [(String, String)] { [("uncategorized", L10n.text("Uncategorized", "未分类"))] + categories.map { ($0.id.uuidString, $0.name) } }
    private func refresh() {
        let previousIDs = items.map(\.id)
        items = [.init(id: "close", title: "×", rect: CGRect(x: 229, y: 8, width: 23, height: 23))]
        visibleRows = []
        let first = min(choices.count, Int(floor(scrollOffset / 32)))
        let last = min(choices.count, Int(ceil((scrollOffset + viewport.height) / 32)))
        for index in first..<max(first, last) {
            let pair = choices[index]
            let rect = CGRect(x: viewport.minX, y: viewport.minY + CGFloat(index) * 32 - scrollOffset, width: 236, height: 28)
            let clipped = rect.intersection(viewport)
            guard !clipped.isNull, clipped.height >= 2 else { continue }
            visibleRows.append((pair.0, pair.1, rect))
            items.append(.init(id: pair.0, title: pair.1, rect: clipped, color: .clear, accessibilityTitle: pair.1))
        }
        items.append(editingName ? .init(id: "create", title: "✓", rect: CGRect(x: 218, y: 252, width: 30, height: 28), accessibilityTitle: L10n.text("Create category", "创建分类"))
            : .init(id: "newCategory", title: "+ " + L10n.text("New category", "新建分类"), rect: CGRect(x: 12, y: 252, width: 236, height: 28)))
        if previousIDs == items.map(\.id), rowClip != nil {
            CATransaction.begin(); CATransaction.setDisableActions(true)
            for (id, _, rect) in visibleRows { rowLayers[id]?.frame = rect.offsetBy(dx: -viewport.minX, dy: -viewport.minY) }
            CATransaction.commit(); layoutAccessibility()
            if let host = superview { HUDControlHighlightLayer.requestRefresh(on: host) }
        } else { paint() }
    }
    override func paintContent(on layer: CALayer) {
        text("// " + L10n.text("Category", "分类"), rect: CGRect(x: 12, y: 12, width: 209, height: 22), size: 12, parent: layer)
        // The base menu still owns native AX buttons. Row feedback and text
        // belong to the clip so fractional scrolling can move retained artwork.
        layer.sublayers?.filter { $0 is HUDControlHighlightLayer && viewport.contains($0.frame) }.forEach { $0.removeFromSuperlayer() }
        rowLayers.removeAll()
        let clip = CALayer(); clip.name = "archive.categoryMenu.rows"; clip.frame = viewport.insetBy(dx: -3, dy: -3)
        clip.bounds = CGRect(x: -3, y: -3, width: viewport.width + 6, height: viewport.height + 6)
        clip.masksToBounds = true; layer.addSublayer(clip); rowClip = clip
        for (id, title, rect) in visibleRows {
            let plate = CALayer(); plate.frame = rect.offsetBy(dx: -viewport.minX, dy: -viewport.minY)
            plate.backgroundColor = ink.withAlphaComponent(0.07).cgColor; clip.addSublayer(plate)
            text(title, rect: plate.bounds.insetBy(dx: 6, dy: 5), parent: plate)
            HUDControlHighlightLayer.add(to: plate, rect: plate.bounds, shape: .cutCorner, framed: true)
            rowLayers[id] = plate
        }
    }
    func scroll(delta: CGFloat) {
        guard delta.isFinite else { return }
        let next = min(max(0, CGFloat(choices.count) * 32 - 4 - viewport.height), max(0, scrollOffset + delta))
        guard next != scrollOffset else { return }; scrollOffset = next; refresh()
    }
    override func scrollWheel(with event: NSEvent) { scroll(delta: -event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1 : 12)) }
    override func perform(_ id: String) {
        if id == "newCategory" { editingName = true; refresh(); onNewCategory?() }
        else if id == "create" { onCreate?() }
        else if id == "uncategorized" { onChoice?(nil) }
        else if let uuid = UUID(uuidString: id), categories.contains(where: { $0.id == uuid }) { onChoice?(uuid) }
        else { super.perform(id) }
    }
}

private final class ArchiveDocumentTextView: HUDProjectedTextView {
    var onFocus: (() -> Void)?
    override func becomeFirstResponder() -> Bool {
        let accepted = super.becomeFirstResponder()
        if accepted { onFocus?() }; return accepted
    }
}

private final class ArchiveGalleryScroller: NSSlider {
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func setAccessibilityValue(_ value: Any?) {
        guard !isHidden, let value = value as? NSNumber, value.doubleValue.isFinite else { return }
        doubleValue = min(maxValue, max(minValue, value.doubleValue)); _ = sendAction(action, to: target)
    }
    override func accessibilityPerformIncrement() -> Bool { step(122) }
    override func accessibilityPerformDecrement() -> Bool { step(-122) }
    private func step(_ delta: Double) -> Bool {
        guard !isHidden else { return false }; setAccessibilityValue(NSNumber(value: doubleValue + delta)); return true
    }
}
