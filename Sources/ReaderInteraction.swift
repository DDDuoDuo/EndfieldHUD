import AppKit

final class ReaderInteraction: NSObject {
    let canvas: ReaderCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var unproject: ((CGPoint) -> CGPoint?)?
    var onLock: (() -> Void)?
    var shelfChoices: (() -> [NotesShelfMediaChoice])?
    var shelfAccess: ((UUID) throws -> ShelfFileAccess)?
    var capturesPointer: Bool { secondaryMenu != nil || canvas.isDragging }
    var isPresentingPanel: Bool { panel != nil }
    private(set) var secondaryMenu: NotesRetainedMenu?
    private var menuOrigin = CGPoint.zero
    private var retiring: CALayer?
    private var buttons: [String: ReaderAXButton] = [:]
    private var active = false, presented = false
    private var panel: NSOpenPanel?
    init(canvas: ReaderCanvas, host: NSView) {
        self.canvas = canvas; self.host = host; super.init()
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
        canvas.onMenu = { [weak self] in self?.show($0) }
    }
    func setPresented(_ value: Bool) {
        guard presented != value else { return }; presented = value
        if value { canvas.activate() } else { setActive(false); canvas.deactivate() }
    }
    func setActive(_ value: Bool) {
        guard active != value else { return }; active = value
        if value { setPresented(true); layoutAccessibility() }
        else { closeMenu(animated: false); panel?.cancel(nil); panel = nil; buttons.values.forEach { $0.isHidden = true } }
    }
    func deactivate() { setActive(false); setPresented(false) }
    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active else { return false }; onLock?()
        if let menu = secondaryMenu {
            let local = CGPoint(x: point.x - menuOrigin.x, y: point.y - menuOrigin.y)
            if menu.bounds.contains(local) { menu.activate(at: local) } else { closeMenu() }
            return true
        }
        guard canvas.layer.bounds.contains(point) else { return false }
        return canvas.mouseDown(at: point)
    }
    func mouseDragged(to point: CGPoint) { if active { canvas.mouseDragged(to: point) } }
    func mouseUp() { canvas.mouseUp() }
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        scroll(at: point, deltaX: 0, deltaY: delta)
    }
    @discardableResult func scroll(at point: CGPoint, deltaX: CGFloat, deltaY: CGFloat,
                                    phase: NSEvent.Phase = [], momentumPhase: NSEvent.Phase = [],
                                    precision: Bool = true, zoomModifier: Bool = false) -> Bool {
        guard active else { return false }
        if let menu = secondaryMenu {
            (menu as? ReaderListMenu)?.scroll(delta: deltaY); return true
        }
        return canvas.scroll(at: point, deltaX: deltaX, deltaY: deltaY, phase: phase,
            momentumPhase: momentumPhase, precision: precision, zoomModifier: zoomModifier)
    }
    @discardableResult func magnify(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, secondaryMenu == nil else { return false }
        return canvas.magnify(at: point, amount: event.magnification)
    }
    func keyDown(_ event: NSEvent) -> Bool {
        guard active else { return false }
        if event.keyCode == 53, secondaryMenu != nil { closeMenu(); return true }
        if secondaryMenu != nil { return false }
        // Leave configured summon shortcuts and macOS navigation chords to
        // their normal handlers; reading navigation uses unmodified keys.
        guard event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        if canvas.controller.preferences.vertical {
            switch event.keyCode {
            case 49, 125: _ = canvas.scroll(at: CGPoint(x: canvas.viewport.midX, y: canvas.viewport.midY), delta: event.keyCode == 49 ? canvas.viewport.height : 40)
            case 126: _ = canvas.scroll(at: CGPoint(x: canvas.viewport.midX, y: canvas.viewport.midY), delta: -40)
            default: return false
            }
        } else {
            switch event.keyCode {
            case 123: canvas.perform("previous")
            case 124: canvas.perform("next")
            default: return false
            }
        }
        return true
    }
    func hitTestMenu(at hostPoint: CGPoint) -> NSView? { secondaryMenu?.hitTest(hostPoint) }
    func importFiles(_ urls: [URL]) {
        guard active, let url = urls.first else { return }; closeMenu(); canvas.controller.open(url: url)
    }
    func layoutAccessibility() {
        guard active, let host else { return }
        for action in canvas.accessibleActions {
            let button = buttons[action.id] ?? ReaderAXButton(frame: .zero)
            if buttons[action.id] == nil { host.addSubview(button); buttons[action.id] = button }
            button.frame = project?(action.rect) ?? action.rect; button.isHidden = false; button.isEnabled = action.enabled
            button.title = ""; button.isBordered = false
            let label: String
            switch action.id {
            case "bookmark": label = L10n.text("Toggle bookmark", "切换书签")
            case "zoomIn": label = L10n.text("Zoom in", "放大")
            case "zoomOut": label = L10n.text("Zoom out", "缩小")
            case "zoomReset": label = L10n.text("Reset zoom", "重置缩放")
            default: label = action.label
            }
            button.setAccessibilityLabel(label); button.setAccessibilityHelp(canvas.accessibilityText)
            button.onPress = { [weak self] in self?.canvas.perform(action.id) }
        }
        let visible = Set(canvas.accessibleActions.map(\.id))
        for (id, button) in buttons where !visible.contains(id) { button.isHidden = true }
        positionMenu(); HUDControlHighlightLayer.requestRefresh(on: host)
    }
    private func show(_ action: String) {
        guard active else { return }; onLock?()
        switch action {
        case "open":
            let menu = NotesMediaSourceChooser(dark: true)
            menu.onChoose = { [weak self] shelf in
                guard let self else { return }; self.closeMenu()
                if shelf { self.showShelf() } else { self.chooseFinder() }
            }; present(menu, anchor: "open")
        case "library":
            let choices = canvas.controller.books.map { ($0.id.uuidString, $0.title + " · \(Int($0.progress * 100))%") }
            let menu = ReaderListMenu(choices: choices, title: L10n.text("Library", "书库"), allowsDelete: true)
            menu.onChoose = { [weak self] id in guard let self, let id = UUID(uuidString: id) else { return }; self.closeMenu(); self.canvas.controller.select(id) }; present(menu, anchor: "library")
            menu.onDelete = { [weak self] id in guard let self, let id = UUID(uuidString: id) else { return }; self.closeMenu(); self.canvas.controller.removeBook(id) }
        case "bookmarks":
            let marks = canvas.controller.book?.bookmarks ?? []
            let menu = ReaderListMenu(choices: marks.map { ($0.id.uuidString, "\(Int($0.progress * 100))%") }, title: L10n.text("Bookmarks", "书签"), width: 240)
            menu.onChoose = { [weak self] id in guard let self, let mark = marks.first(where: { $0.id.uuidString == id }) else { return }; self.closeMenu(); self.canvas.controller.jump(to: mark.location) }; present(menu, anchor: "bookmarks")
        case "settings":
            let menu = ReaderSettingsMenu(preferences: canvas.controller.preferences)
            menu.onPreferences = { [weak self] value in self?.canvas.controller.setPreferences(value) }
            menu.onFont = { [weak self] in self?.show("font") }; present(menu, anchor: "settings")
        case "font":
            let prefs = canvas.controller.preferences
            let menu = NotesFormattingControls(kind: "font", dark: true, style: NotesTextStyle(fontName: prefs.fontName, fontSize: prefs.fontSize))
            menu.onChange = { [weak self] change in
                guard let self, case .font(let name) = change else { return }
                var prefs = self.canvas.controller.preferences; prefs.fontName = name; self.canvas.controller.setPreferences(prefs); self.closeMenu()
            }; present(menu, anchor: "settings")
        default: break
        }
    }
    private func chooseFinder() {
        guard let window = host?.window else { return }
        let picker = NSOpenPanel(); picker.allowsMultipleSelection = false; picker.canChooseDirectories = false
        picker.allowedFileTypes = ReaderDocument.extensions; panel = picker
        picker.beginSheetModal(for: window) { [weak self, weak picker] response in
            guard let self else { return }; self.panel = nil
            if self.active, response == .OK, let url = picker?.url { self.canvas.controller.open(url: url) }
        }
    }
    private func showShelf() {
        let choices = (shelfChoices?() ?? []).filter { $0.isSupported && $0.isAvailable }.map { ($0.id.uuidString, $0.title) }
        let menu = ReaderListMenu(choices: choices, title: L10n.text("Choose from Shelf", "从暂存架选择"))
        menu.onChoose = { [weak self] id in
            guard let self, let id = UUID(uuidString: id) else { return }; self.closeMenu()
            do { if let access = try self.shelfAccess?(id) { self.canvas.controller.open(url: access.url, retainedAccess: access) } }
            catch { self.canvas.controller.report(error) }
        }; present(menu)
    }
    private func present(_ menu: NotesRetainedMenu, anchor: String? = nil) {
        guard let host else { return }
        if secondaryMenu != nil { closeMenu() }
        secondaryMenu = menu
        if let anchor, let button = canvas.accessibleActions.first(where: { $0.id == anchor }) {
            menuOrigin = CGPoint(x: min(max(8, button.rect.minX), 392 - menu.contentSize.width), y: button.rect.maxY + 6)
        } else { menuOrigin = CGPoint(x: (400 - menu.contentSize.width) / 2, y: 46) }
        menu.onCancel = { [weak self] in self?.closeMenu() }
        menu.hostToLocal = { [weak self] point in
            guard let self, let local = self.unproject?(point) else { return nil }
            return CGPoint(x: local.x - self.menuOrigin.x, y: local.y - self.menuOrigin.y)
        }
        menu.projectLocal = { [weak self] rect in guard let self else { return .zero }; return self.project?(rect.offsetBy(dx: self.menuOrigin.x, dy: self.menuOrigin.y)) ?? rect }
        host.addSubview(menu); canvas.layer.addSublayer(menu.artwork); menu.artwork.zPosition = 3_000_000
        positionMenu(); host.window?.makeFirstResponder(menu)
        if !HUDRuntimeAppearance.reduceMotion {
            let animation = CABasicAnimation(keyPath: "opacity"); animation.fromValue = 0; animation.toValue = 1; animation.duration = 0.16
            menu.artwork.add(animation, forKey: "reader.menu")
        }
    }
    private func positionMenu() {
        guard let menu = secondaryMenu else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        menu.frame = project?(CGRect(origin: menuOrigin, size: menu.contentSize)) ?? CGRect(origin: menuOrigin, size: menu.contentSize)
        menu.bounds = CGRect(origin: .zero, size: menu.contentSize); menu.artwork.position = menuOrigin; menu.layoutAccessibility()
        CATransaction.commit()
    }
    func closeMenu(animated: Bool = true) {
        retiring?.removeAllAnimations(); retiring?.removeFromSuperlayer(); retiring = nil
        guard let menu = secondaryMenu else { return }; secondaryMenu = nil
        if animated, active, !HUDRuntimeAppearance.reduceMotion {
            menu.detachInputKeepingArtwork(); retiring = menu.artwork
            CATransaction.begin(); CATransaction.setCompletionBlock { [weak self, weak artwork = menu.artwork] in
                guard let self, let artwork, self.retiring === artwork else { return }; artwork.removeFromSuperlayer(); self.retiring = nil
            }
            let animation = CABasicAnimation(keyPath: "opacity"); animation.fromValue = 1; animation.toValue = 0; animation.duration = 0.14
            menu.artwork.opacity = 0; menu.artwork.add(animation, forKey: "reader.menu.close"); CATransaction.commit()
        } else { menu.removeFromSuperview() }
        host?.window?.makeFirstResponder(host)
    }
}

private final class ReaderAXButton: NSButton {
    var onPress: (() -> Void)?
    override init(frame: CGRect) { super.init(frame: frame); target = self; action = #selector(press) }
    required init?(coder: NSCoder) { nil }
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    @objc private func press() { onPress?() }
}

private final class ReaderListMenu: NotesRetainedMenu {
    let choices: [(String, String)], heading: String
    var onChoose: ((String) -> Void)?
    var onDelete: ((String) -> Void)?
    private var offset = 0
    private var scrollRemainder: CGFloat = 0
    private let allowsDelete: Bool
    private var deletion: String?
    init(choices: [(String, String)], title: String, allowsDelete: Bool = false, width: CGFloat = 350) { self.choices = choices; heading = title; self.allowsDelete = allowsDelete; super.init(size: CGSize(width: width, height: 294), dark: true); refresh() }
    required init?(coder: NSCoder) { nil }
    private func refresh() {
        items = [Item(id: "close", title: "×", rect: CGRect(x: contentSize.width - 32, y: 8, width: 23, height: 23))]
        for index in offset..<min(choices.count, offset + 6) {
            items.append(Item(id: choices[index].0, title: choices[index].1, rect: CGRect(x: 8, y: 39 + CGFloat(index - offset) * 35, width: contentSize.width - (allowsDelete ? 54 : 16), height: 29)))
            if allowsDelete { items.append(Item(id: "delete:" + choices[index].0, title: "×", rect: CGRect(x: contentSize.width - 33, y: 42 + CGFloat(index - offset) * 35, width: 23, height: 23))) }
        }; paint()
        if deletion != nil {
            items += [Item(id: "cancelDelete", title: "×", rect: CGRect(x: contentSize.width - 128, y: 258, width: 52, height: 27)),
                      Item(id: "confirmDelete", title: "✓", rect: CGRect(x: contentSize.width - 64, y: 258, width: 56, height: 27))]; paint()
        }
    }
    override func paintContent(on layer: CALayer) {
        text(heading, rect: CGRect(x: 10, y: 12, width: contentSize.width - 50, height: 20), size: 12, parent: layer)
        if choices.count > 6 {
            let track = CALayer(); track.frame = CGRect(x: contentSize.width - 6, y: 39, width: 2, height: 204)
            track.backgroundColor = ink.withAlphaComponent(0.12).cgColor; layer.addSublayer(track)
            let height = max(12, 204 * 6 / CGFloat(choices.count))
            let thumb = CALayer(); thumb.frame = CGRect(x: contentSize.width - 6, y: 39 + CGFloat(offset) / CGFloat(choices.count - 6) * (204 - height), width: 2, height: height)
            thumb.backgroundColor = ink.withAlphaComponent(0.55).cgColor; layer.addSublayer(thumb)
        }
    }
    override func scrollWheel(with event: NSEvent) { scroll(delta: -event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1 : 12)) }
    func scroll(delta: CGFloat) {
        guard delta.isFinite, deletion == nil else { return }
        scrollRemainder += min(350, max(-350, delta))
        let rows = Int(scrollRemainder / 35)
        guard rows != 0 else { return }
        scrollRemainder -= CGFloat(rows) * 35
        let next = min(max(0, choices.count - 6), max(0, offset + rows))
        if next != offset { offset = next; refresh() }
    }
    override func perform(_ id: String) {
        if id.hasPrefix("delete:") { deletion = String(id.dropFirst(7)); refresh() }
        else if id == "cancelDelete" { deletion = nil; refresh() }
        else if id == "confirmDelete", let deletion { onDelete?(deletion) }
        else if id == "close" { super.perform(id) } else { onChoose?(id) }
    }
}

private final class ReaderSettingsMenu: NotesRetainedMenu {
    private var preferences: ReaderPreferences
    var onPreferences: ((ReaderPreferences) -> Void)?, onFont: (() -> Void)?
    init(preferences: ReaderPreferences) { self.preferences = preferences; super.init(size: CGSize(width: 248, height: 222), dark: true); refresh() }
    required init?(coder: NSCoder) { nil }
    private func refresh() {
        items = [Item(id: "close", title: "×", rect: CGRect(x: 215, y: 8, width: 23, height: 23)),
                 Item(id: "font", title: L10n.text("Font", "字体") + " · " + preferences.fontName, rect: CGRect(x: 8, y: 8, width: 198, height: 26))]
        let settings = [("size", L10n.text("Font size", "字号"), preferences.fontSize), ("spacing", L10n.text("Line spacing", "行间距"), preferences.lineSpacing), ("margin", L10n.text("Margins", "页边距"), preferences.margin)]
        for (index, setting) in settings.enumerated() {
            items.append(Item(id: setting.0 + "-", title: "−", rect: CGRect(x: 174, y: 47 + index * 40, width: 30, height: 30)))
            items.append(Item(id: setting.0 + "+", title: "+", rect: CGRect(x: 208, y: 47 + index * 40, width: 30, height: 30)))
        }
        items += [Item(id: "horizontal", title: L10n.text("Left to right", "从左到右"), rect: CGRect(x: 8, y: 176, width: 112, height: 32), selected: !preferences.vertical),
                  Item(id: "vertical", title: L10n.text("Top to bottom", "从上到下"), rect: CGRect(x: 128, y: 176, width: 112, height: 32), selected: preferences.vertical)]
        paint()
    }
    override func paintContent(on layer: CALayer) {
        let settings = [(L10n.text("Font size", "字号"), preferences.fontSize), (L10n.text("Line spacing", "行间距"), preferences.lineSpacing), (L10n.text("Margins", "页边距"), preferences.margin)]
        for (index, value) in settings.enumerated() { text(value.0 + "  \(Int(value.1))", rect: CGRect(x: 10, y: 53 + index * 40, width: 158, height: 24), parent: layer) }
    }
    override func perform(_ id: String) {
        let amount: Double = id.hasSuffix("+") ? 1 : -1
        if id == "font" { onFont?(); return }
        if id.hasPrefix("size") { preferences.fontSize = min(32, max(10, preferences.fontSize + amount)) }
        else if id.hasPrefix("spacing") { preferences.lineSpacing = min(18, max(0, preferences.lineSpacing + amount)) }
        else if id.hasPrefix("margin") { preferences.margin = min(48, max(6, preferences.margin + amount)) }
        else if id == "horizontal" { preferences.vertical = false }
        else if id == "vertical" { preferences.vertical = true }
        else { super.perform(id); return }
        onPreferences?(preferences); refresh()
    }
}
