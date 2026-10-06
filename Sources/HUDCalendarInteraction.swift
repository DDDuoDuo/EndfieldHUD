import AppKit
import QuartzCore

final class HUDCalendarInteraction: NSObject, NSTextViewDelegate {
    let canvas: HUDCalendarCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?, unproject: ((CGPoint) -> CGPoint?)?
    private var active = false, presented = false
    private(set) var secondaryMenu: NotesRetainedMenu?
    private var menuOrigin = CGPoint(x: 30, y: 77)
    private var editors: [String: HUDProjectedTextEditor] = [:]
    private var editingID: UUID?
    private var buttons: [String: CalendarAXButton] = [:]
    private var retiring: CALayer?, retireWork: DispatchWorkItem?
    var capturesPointer: Bool { secondaryMenu != nil }
    var isPresentingPanel: Bool { false }
    init(canvas: HUDCalendarCanvas, host: NSView) {
        self.canvas = canvas; self.host = host; super.init()
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
        canvas.onAction = { [weak self] in self?.perform($0) }
    }
    deinit {
        retireWork?.cancel(); retiring?.removeFromSuperlayer(); secondaryMenu?.removeFromSuperview()
        editors.values.forEach { $0.dispose() }; buttons.values.forEach { $0.removeFromSuperview() }
    }
    func setPresented(_ value: Bool) {
        guard presented != value else { return }; presented = value
        if !value { setActive(false) }; canvas.setVisible(value)
    }
    func setActive(_ value: Bool) {
        guard active != value else { return }; active = value
        if value { setPresented(true); layoutAccessibility() }
        else { dismissMenu(animated: false); buttons.values.forEach { $0.isHidden = true } }
    }
    func deactivate() { setActive(false); setPresented(false) }
    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active else { return false }
        if let menu = secondaryMenu {
            if menu.bounds.offsetBy(dx: menuOrigin.x, dy: menuOrigin.y).contains(point) {
                if let editor = editors.values.first(where: { $0.logicalRect.contains(point) }) {
                    host?.window?.makeFirstResponder(editor.textView); editor.textView.selectAll(nil)
                } else { menu.activate(at: CGPoint(x: point.x - menuOrigin.x, y: point.y - menuOrigin.y)) }
            } else { dismissMenu() }
            return true
        }
        guard canvas.layer.bounds.contains(point) else { return false }
        host?.window?.makeFirstResponder(host)
        if let action = canvas.actions.last(where: { $0.enabled && $0.rect.contains(point) }) { canvas.perform(action.id) }
        return true
    }
    func mouseDragged(to point: CGPoint) {}
    func mouseUp() {}
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard active else { return false }
        if secondaryMenu != nil {
            if let detail = editors["details"], detail.logicalRect.contains(point) { detail.scroll(delta: delta) }; return true
        }
        return canvas.scroll(at: point, delta: delta)
    }
    func keyDown(_ event: NSEvent) -> Bool {
        guard active else { return false }
        let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
        if event.keyCode == 53, flags.isEmpty, secondaryMenu != nil { dismissMenu(); return true }
        guard secondaryMenu == nil, flags.isEmpty else { return false }
        if event.keyCode == 123 { canvas.perform("previousMonth"); return true }
        if event.keyCode == 124 { canvas.perform("nextMonth"); return true }
        return false
    }
    func hitTestMenu(at point: CGPoint) -> NSView? {
        for editor in editors.values { if let hit = editor.hitTest(point) { return hit } }
        return secondaryMenu?.hitTest(point)
    }
    func editorForVerification(_ field: String) -> HUDProjectedTextEditor? { editors[field] }
    func layoutAccessibility() {
        guard active, let host else { return }
        for action in canvas.actions {
            let button = buttons[action.id] ?? CalendarAXButton(frame: .zero)
            if buttons[action.id] == nil { host.addSubview(button); buttons[action.id] = button }
            button.title = ""; button.isBordered = false; button.isHidden = false; button.isEnabled = action.enabled
            button.frame = project?(action.rect) ?? action.rect
            let label: String
            switch action.id {
            case "new": label = L10n.text("Add event", "添加事项")
            case "previousMonth": label = L10n.text("Previous month", "上个月")
            case "nextMonth": label = L10n.text("Next month", "下个月")
            case "reminders": label = L10n.text("Refresh reminders", "刷新提醒")
            default: label = action.id.hasPrefix("day:") ? String(action.id.dropFirst(4)) : action.label
            }
            button.setAccessibilityLabel(label); button.onPress = { [weak self] in self?.canvas.perform(action.id) }
        }
        let shown = Set(canvas.actions.map(\.id))
        // A calendar only needs the current month's controls. Remove stale
        // dates rather than accumulating native buttons while browsing years.
        for id in Array(buttons.keys) where !shown.contains(id) { buttons.removeValue(forKey: id)?.removeFromSuperview() }
        if let menu = secondaryMenu {
            CATransaction.begin(); CATransaction.setDisableActions(true)
            menu.frame = project?(CGRect(origin: menuOrigin, size: menu.contentSize)) ?? CGRect(origin: menuOrigin, size: menu.contentSize)
            menu.bounds = CGRect(origin: .zero, size: menu.contentSize); menu.artwork.position = menuOrigin
            (menu as? CalendarEventMenu)?.update(busy: canvas.controller.busy, error: canvas.controller.error)
            menu.layoutAccessibility(); editors.values.forEach { $0.refreshProjection() }
            CATransaction.commit()
        }
        HUDControlHighlightLayer.requestRefresh(on: host)
    }
    private func perform(_ action: String) {
        guard active else { return }
        if action == "new" { show(event: nil) }
        else if action.hasPrefix("event:"), let id = UUID(uuidString: String(action.dropFirst(6))),
                let event = canvas.controller.events.first(where: { $0.id == id }) { show(event: event) }
    }
    private func show(event: HUDCalendarEvent?) {
        guard let host else { return }; dismissMenu(animated: false)
        let menu = CalendarEventMenu(editing: event != nil, dark: canvas.dark)
        editingID = event?.id; secondaryMenu = menu
        menu.onCancel = { [weak self] in self?.dismissMenu() }
        menu.onSave = { [weak self] in self?.saveDraft() }
        menu.onDelete = { [weak self] in
            guard let self, let id = self.editingID else { return }
            self.canvas.controller.delete(id) { [weak self] success in if success { self?.dismissMenu() } }
        }
        menu.hostToLocal = { [weak self] point in
            guard let self, let local = self.unproject?(point) ?? (self.unproject == nil ? point : nil) else { return nil }
            return CGPoint(x: local.x - self.menuOrigin.x, y: local.y - self.menuOrigin.y)
        }
        menu.projectLocal = { [weak self] rect in guard let self else { return rect }; let value = rect.offsetBy(dx: self.menuOrigin.x, dy: self.menuOrigin.y); return self.project?(value) ?? value }
        host.addSubview(menu); canvas.layer.addSublayer(menu.artwork); menu.artwork.zPosition = 3_000_000
        let values = [("title", event?.title ?? "", CGRect(x: 14, y: 49, width: 312, height: 30)),
                      ("date", (event?.day ?? canvas.selectedDay).string, CGRect(x: 14, y: 99, width: 312, height: 28)),
                      ("details", event?.details ?? "", CGRect(x: 14, y: 149, width: 312, height: 86))]
        for (field, value, rect) in values {
            let text = CalendarFieldTextView(frame: .zero); text.delegate = self
            text.isEditable = true; text.isSelectable = true; text.isRichText = false; text.importsGraphics = false; text.allowsUndo = true
            let ink: NSColor = canvas.dark ? .white : .black
            text.font = .systemFont(ofSize: field == "title" ? 13 : 11); text.textColor = ink; text.insertionPointColor = ink
            text.typingAttributes = [.font: text.font!, .foregroundColor: ink]
            text.string = value
            text.onCancel = { [weak self] in self?.dismissMenu() }; text.onSave = { [weak self] in self?.saveDraft() }
            let moduleRect = rect.offsetBy(dx: menuOrigin.x, dy: menuOrigin.y)
            let editor = HUDProjectedTextEditor(textView: text, rect: moduleRect, host: host, parent: canvas.layer)
            editor.artwork.zPosition = 3_000_001
            editor.project = { [weak self] in self?.project?($0) ?? $0 }
            editor.unproject = { [weak self] point in guard let self else { return nil }; return self.unproject?(point) ?? (self.unproject == nil ? point : nil) }
            if field != "details" { editor.configureSingleLine() }
            let label = field == "title" ? L10n.text("Title", "标题") : field == "date" ? L10n.text("Date", "日期") : L10n.text("Details", "详情")
            editor.placeholder = label; text.setAccessibilityLabel(label)
            editor.setAppearance(background: NSColor(white: canvas.dark ? 0.12 : 0.86, alpha: 1), border: HUDRuntimeAppearance.accent.withAlphaComponent(0.35), radius: 2)
            editor.resizeDocument(); editors[field] = editor
            if !HUDRuntimeAppearance.reduceMotion { let reveal = CABasicAnimation(keyPath: "opacity"); reveal.fromValue = 0; reveal.toValue = 1; reveal.duration = 0.16; editor.artwork.add(reveal, forKey: "calendar.editor.open") }
        }
        layoutAccessibility()
        if !HUDRuntimeAppearance.reduceMotion { let reveal = CABasicAnimation(keyPath: "opacity"); reveal.fromValue = 0; reveal.toValue = 1; reveal.duration = 0.16; menu.artwork.add(reveal, forKey: "calendar.menu.open") }
        if let title = editors["title"] { host.window?.makeFirstResponder(title.textView) }
    }
    private func saveDraft() {
        guard let title = editors["title"]?.textView.string, let rawDate = editors["date"]?.textView.string,
              let day = HUDCalendarDay.parse(rawDate.trimmingCharacters(in: .whitespacesAndNewlines)) else {
            (secondaryMenu as? CalendarEventMenu)?.update(busy: false, error: HUDCalendarError.invalidDate.localizedDescription); return
        }
        for editor in editors.values where editor.textView.hasMarkedText() { editor.textView.unmarkText() }
        _ = canvas.controller.save(title: title, details: editors["details"]?.textView.string ?? "", day: day, id: editingID) { [weak self] success in
            if success { self?.dismissMenu() }
        }
    }
    func textDidChange(_ notification: Notification) {
        guard let text = notification.object as? NSTextView,
              let pair = editors.first(where: { $0.value.textView === text }) else { return }
        if !text.hasMarkedText() {
            let limit = pair.key == "title" ? 120 : pair.key == "date" ? 10 : 2000
            var value = String(text.string.prefix(limit))
            if pair.key != "details" { value = value.replacingOccurrences(of: "\n", with: " ") }
            if value != text.string { text.string = value; text.setSelectedRange(NSRange(location: (value as NSString).length, length: 0)) }
        }
        pair.value.resizeDocument()
    }
    func dismissMenu(animated: Bool = true) {
        retireWork?.cancel(); retireWork = nil; retiring?.removeFromSuperlayer(); retiring = nil
        guard let menu = secondaryMenu else { return }
        let fading = animated && active && !HUDRuntimeAppearance.reduceMotion
        if fading {
            func copy(_ layer: CALayer) -> CALayer {
                let value = CALayer(); value.frame = layer.frame; value.contents = layer.contents; value.contentsScale = layer.contentsScale
                value.contentsGravity = layer.contentsGravity; value.backgroundColor = layer.backgroundColor
                value.cornerRadius = layer.cornerRadius; value.borderColor = layer.borderColor; value.borderWidth = layer.borderWidth
                value.opacity = layer.opacity; value.masksToBounds = layer.masksToBounds; value.isHidden = layer.isHidden
                layer.sublayers?.forEach { value.addSublayer(copy($0)) }; return value
            }
            for editor in editors.values { editor.captureVisibleArtwork(); let face = copy(editor.artwork); face.frame = face.frame.offsetBy(dx: -menuOrigin.x, dy: -menuOrigin.y); menu.artwork.addSublayer(face) }
        }
        if editors.values.contains(where: { host?.window?.firstResponder === $0.textView }) { host?.window?.makeFirstResponder(host) }
        editors.values.forEach { $0.textView.delegate = nil; $0.dispose() }; editors = [:]; editingID = nil; secondaryMenu = nil
        if fading {
            menu.detachInputKeepingArtwork(); retiring = menu.artwork
            let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 1; fade.toValue = 0; fade.duration = 0.14
            menu.artwork.opacity = 0; menu.artwork.add(fade, forKey: "calendar.menu.close")
            let work = DispatchWorkItem { [weak self, weak artwork = menu.artwork] in guard let self, self.retiring === artwork else { return }; artwork?.removeFromSuperlayer(); self.retiring = nil; self.retireWork = nil }
            retireWork = work; DispatchQueue.main.asyncAfter(deadline: .now() + 0.16, execute: work)
        } else { menu.removeFromSuperview() }
        host?.window?.makeFirstResponder(host)
    }
}

private final class CalendarEventMenu: NotesRetainedMenu {
    let editing: Bool
    private var busy = false, deleting = false, error: String?
    var onSave: (() -> Void)?, onDelete: (() -> Void)?
    init(editing: Bool, dark: Bool) { self.editing = editing; super.init(size: CGSize(width: 340, height: 310), dark: dark); refresh() }
    required init?(coder: NSCoder) { nil }
    func update(busy: Bool, error: String?) {
        guard self.busy != busy || self.error != error else { return }; self.busy = busy; self.error = error; refresh()
    }
    private func refresh() {
        items = [Item(id: "close", title: "×", rect: CGRect(x: 308, y: 8, width: 23, height: 23)),
                 Item(id: "save", title: "✓", rect: CGRect(x: 297, y: 265, width: 30, height: 30), enabled: !busy, accessibilityTitle: L10n.text("Save", "保存"))]
        if editing {
            items.append(Item(id: deleting ? "cancelDelete" : "delete", title: "×", rect: CGRect(x: 14, y: 265, width: 30, height: 30), enabled: !busy,
                              accessibilityTitle: deleting ? L10n.text("Cancel", "取消") : L10n.text("Delete", "删除")))
            if deleting { items.append(Item(id: "confirmDelete", title: "✓", rect: CGRect(x: 52, y: 265, width: 30, height: 30), enabled: !busy, accessibilityTitle: L10n.text("Delete", "删除"))) }
        }
        paint()
    }
    override func paintContent(on layer: CALayer) {
        for label in layer.sublayers?.compactMap({ $0 as? CATextLayer }) ?? [] {
            if let glyph = label.string as? String, glyph == "✓" || glyph == "×" { label.alignmentMode = .center }
        }
        text("// " + (editing ? L10n.text("Edit event", "编辑事项") : L10n.text("Add event", "添加事项")), rect: CGRect(x: 14, y: 10, width: 280, height: 22), size: 13, parent: layer)
        for (title, y) in [(L10n.text("Title", "标题"), 32), (L10n.text("Date", "日期"), 82), (L10n.text("Details", "详情"), 132)] {
            text(title, rect: CGRect(x: 14, y: y, width: 312, height: 14), size: 9, parent: layer, color: ink.withAlphaComponent(0.55))
        }
        let status = error ?? (deleting ? L10n.text("Delete this event?", "删除此事项？") : "")
        text(status, rect: CGRect(x: 14, y: 243, width: 312, height: 17), size: 9, parent: layer, color: error == nil ? ink.withAlphaComponent(0.5) : .systemOrange)
    }
    override func perform(_ id: String) {
        if id == "save" { onSave?() }
        else if id == "delete" { deleting = true; refresh() }
        else if id == "cancelDelete" { deleting = false; refresh() }
        else if id == "confirmDelete" { onDelete?() }
        else { super.perform(id) }
    }
}
private final class CalendarFieldTextView: HUDProjectedTextView {
    var onCancel: (() -> Void)?, onSave: (() -> Void)?
    override func keyDown(with event: NSEvent) {
        if event.keyCode == 53 { onCancel?() }
        else if event.keyCode == 36 && event.modifierFlags.contains(.command) { onSave?() }
        else { super.keyDown(with: event) }
    }
}
private final class CalendarAXButton: NSButton {
    var onPress: (() -> Void)?
    override init(frame: CGRect) { super.init(frame: frame); target = self; action = #selector(press) }
    required init?(coder: NSCoder) { nil }
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    @objc private func press() { onPress?() }
}
