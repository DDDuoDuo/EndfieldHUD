import AppKit
import QuartzCore
import UniformTypeIdentifiers

/// The App Shortcuts screen of the redesign (spec §3.23): one row per saved
/// launcher with its icon tile (Choose icon), an editable name, Open and
/// Remove; Add App chooses an application. Launching is delegated to the
/// overlay owner (`onLaunch`) so the HUD can finish closing first.
final class AppShortcutCanvas: HUDDesignCanvas {
    var onLaunch: ((UUID) -> Void)?
    var onSaved: ((AppShortcut) -> Void)?
    var onRemoved: ((String) -> Void)?
    private let store: AppShortcutStore?
    private let storeError: String?
    private(set) var items: [AppShortcut] = []
    /// Application icons by shortcut, misses included (nil).
    private var icons: [UUID: CGImage?] = [:]
    private var tileColors: [UUID: CGColor] = [:]
    private var chooser: NSOpenPanel?
    private var dialogGeneration = 0
    private(set) var isDropTarget = false
    private var revealsEnd = false
    static let rowHeight: CGFloat = 56
    static let nameLimit = 24
    override var isPresentingPanel: Bool { chooser != nil }
    override var isInputLocked: Bool { super.isInputLocked || chooser != nil || isDropTarget }

    init(context: HUDDesignContext, store: AppShortcutStore?, error: String? = nil) {
        self.store = store; storeError = error
        items = store?.items ?? []
        super.init(module: .addApp, context: context)
    }

    override func didActivate() {
        reload()
        if let storeError { showError(storeError) }
    }

    override func didDeactivate() { closeChooser(); isDropTarget = false }
    override func willLeave() { closeChooser() }

    /// Rebuilds from the store when its records changed.
    func reload() {
        let next = store?.items ?? []
        let retained = Set(next.map(\.id))
        icons = icons.filter { retained.contains($0.key) }; tileColors = tileColors.filter { retained.contains($0.key) }
        guard next != items || renderedLanguage == nil else { return }
        items = next
        rerender()
    }

    func showError(_ message: String) { toast(icon: .warn, "Notice", "提示", detail: message) }

    /// An application dragged over the screen (the host forwards file drags);
    /// the plane holds still while it is over the drop target.
    func setDropTarget(_ value: Bool) {
        let next = value && isActive && chooser == nil
        guard next != isDropTarget else { return }
        isDropTarget = next
        if next { context.onLockParallax?() }
        if renderedLanguage != nil { rerender() }
    }

    /// A drop holds exactly one application.
    static func acceptsApplications(_ pasteboard: NSPasteboard) -> Bool {
        let urls = pasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
        return urls.count == 1 && urls[0].pathExtension.lowercased() == "app"
    }

    /// The application dropped on the screen.
    @discardableResult func importPasteboard(_ pasteboard: NSPasteboard) -> Bool {
        guard isActive, chooser == nil else { return false }
        let urls = Self.applicationURLs(pasteboard)
        return !urls.isEmpty && importURLs(urls)
    }

    /// Adds the one application in `urls` (chooser, drop or paste).
    @discardableResult func importURLs(_ urls: [URL]) -> Bool {
        guard let store else { showError(L10n.text("Shortcut storage is unavailable.", "快捷方式存储不可用。")); return false }
        guard urls.count == 1, let url = urls.first else {
            showError(L10n.text("Choose one application to customize.", "请选择一个应用进行设置。")); return false
        }
        do {
            let candidate = try store.inspect(url: url)
            let saved = try store.save(candidate: candidate, name: candidate.name, iconPreset: .original)
            revealsEnd = true
            reload()
            onSaved?(saved)
            toast(icon: .plus, "App added", "已添加到右侧列表")
            return true
        } catch { showError(error.localizedDescription); return false }
    }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        let add = HUDButton(id: "apps:add", title: L10n.text("Add App", "添加应用"), icon: .plus, kind: .primary, size: .small, env: env)
        add.place(at: .zero)
        add.isEnabled = store != nil
        add.onActivate = { [weak self] in self?.chooseApplication() }
        header.configure(crumbs: ["SHORTCUTS"], code: "MOD-18", title: HUDModuleTitle("Shortcuts", "应用快捷方式"),
                         counter: HUDFormat.pad2(items.count), showsBack: false, actions: [add])
        let top = HUDMetric.headerHeight
        if items.isEmpty {
            let empty = HUDEmptyState.make(title: "NO SHORTCUTS", env: env)
            HUDMotion.perform { empty.frame.origin = CGPoint(x: 0, y: top); page.layer.addSublayer(empty) }
            page.stagger(empty)
        } else {
            let height = CGFloat(items.count) * (Self.rowHeight + HUDMetric.rowGap) - HUDMetric.rowGap
            let scroll = page.scrollArea(id: "apps", frame: CGRect(x: 0, y: top, width: HUDMetric.box.width, height: min(420, HUDMetric.box.height - top)))
            scroll.setContentHeight(height)
            let width = scroll.contentWidth
            for (index, item) in items.enumerated() {
                let row = makeRow(item, width: width, page: page)
                HUDMotion.perform {
                    row.layer.frame.origin = CGPoint(x: 0, y: CGFloat(index) * (Self.rowHeight + HUDMetric.rowGap))
                    scroll.content.addSublayer(row.layer)
                }
                page.stagger(row.layer)
            }
            page.restoreOffset(of: scroll)
            if revealsEnd { scroll.setOffset(scroll.maximumOffset) }
        }
        revealsEnd = false
        if isDropTarget {
            let ring = HUDLayer.plain("apps.drop")
            HUDMotion.perform {
                ring.frame = CGRect(x: 0, y: top, width: HUDMetric.box.width, height: min(420, HUDMetric.box.height - top))
                ring.borderWidth = 1; ring.borderColor = HUDColor.yellow
                let bar = HUDLayer.plain(); bar.frame = CGRect(x: 0, y: 0, width: 3, height: ring.bounds.height)
                bar.backgroundColor = HUDColor.yellow; ring.addSublayer(bar)
                page.layer.addSublayer(ring)
            }
        }
    }

    /// Row plate, min-height 56, padding 8 10 6 16: tile, name field, Open, Remove.
    private func makeRow(_ item: AppShortcut, width: CGFloat, page: HUDDesignPage) -> HUDRowPlate {
        let row = HUDRowPlate(id: "apps:\(item.id.uuidString)", title: "", width: width, height: Self.rowHeight, env: env)
        let centre: CGFloat = 8 + (Self.rowHeight - 14) / 2
        let tile = AppShortcutTile(id: "apps:\(item.id.uuidString):icon", label: L10n.text("Choose icon", "选择图标") + ", " + item.name,
                                   artwork: artwork(item.iconPreset, for: item), env: env)
        tile.onActivate = { [weak self, weak tile] in
            guard let self, let tile else { return }
            if self.isMenuOpen { self.closeMenu() } else { self.openIconMenu(item, anchor: tile) }
        }
        let remove = HUDIconButton(id: "apps:\(item.id.uuidString):remove", label: L10n.text("Remove", "移除") + " " + item.name, icon: .x, env: env)
        remove.onActivate = { [weak self] in self?.confirmRemove(item) }
        let open = HUDIconButton(id: "apps:\(item.id.uuidString):open", label: L10n.text("Open", "打开") + " " + item.name, icon: .ext, style: .light, env: env)
        open.onActivate = { [weak self] in
            self?.onLaunch?(item.id)
            self?.toast(icon: .ext, "Opening", "正在打开", detail: item.name)
        }
        let fieldX: CGFloat = 16 + 40 + 14
        let removeX = width - 10 - 30, openX = removeX - 8 - 30
        var options = HUDDesignTextField.Options()
        options.light = true; options.maximumLength = Self.nameLimit
        let field = HUDDesignTextField(id: "apps:\(item.id.uuidString):name", label: L10n.text("Shortcut name", "快捷方式名称"),
                                       text: item.name, size: CGSize(width: openX - 8 - fieldX, height: 30), options: options, env: env)
        field.onCommit = { [weak self, weak field] text in self?.rename(item.id, to: text, field: field) }
        register(field)
        HUDMotion.perform {
            tile.layer.frame.origin = CGPoint(x: 16, y: centre - 20)
            field.layer.frame.origin = CGPoint(x: fieldX, y: centre - 15)
            open.layer.frame.origin = CGPoint(x: openX, y: centre - 15)
            remove.layer.frame.origin = CGPoint(x: removeX, y: centre - 15)
            [tile.layer, field.layer, open.layer, remove.layer].forEach(row.layer.addSublayer)
        }
        env.applyScale(to: row.layer)
        page.add(row); page.add([tile, field, open, remove])
        return row
    }

    /// Choose icon: every preset as the tile it would make, in an icon grid.
    private func openIconMenu(_ item: AppShortcut, anchor: HUDControl) {
        let choices = AppShortcutIcon.allCases.map {
            HUDMenu.Item(id: $0.rawValue, title: $0.title, selected: $0 == item.iconPreset, icon: artwork($0, for: item))
        }
        openMenu(id: "apps:iconMenu", items: choices, anchor: anchor, minimumWidth: 0) { [weak self] picked in
            guard let self, let preset = AppShortcutIcon(rawValue: picked.id), preset != item.iconPreset else { return }
            self.save(item.id, name: item.name, preset: preset)
        }
    }

    private func rename(_ id: UUID, to text: String, field: HUDDesignTextField?) {
        guard let item = items.first(where: { $0.id == id }) else { return }
        let name = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty else { field?.setText(item.name); return }
        guard name != item.name else { return }
        save(id, name: name, preset: item.iconPreset)
    }

    private func save(_ id: UUID, name: String, preset: AppShortcutIcon) {
        guard let store else { return }
        do {
            let candidate = try store.inspect(url: store.resolvedURL(for: id))
            let saved = try store.save(candidate: candidate, name: name, iconPreset: preset, editingID: id)
            icons[id] = nil; tileColors[id] = nil
            reload()
            onSaved?(saved)
        } catch {
            showError(error.localizedDescription)
            rerender()
        }
    }

    private func confirmRemove(_ item: AppShortcut) {
        confirm(HUDConfirmRequest(id: "apps:remove", title: L10n.text("Remove \(item.name)?", "移除 \(item.name)？"),
                                  message: L10n.text("The app itself stays installed.", "应用本身不会被卸载。"),
                                  confirmTitle: L10n.text("Remove", "移除"), danger: true, icon: .trash)) { [weak self] in
            guard let self, let store = self.store else { return }
            do {
                try store.remove(id: item.id)
                self.reload()
                self.onRemoved?(item.name)
                self.toast(icon: .trash, "Shortcut removed", "应用未被卸载")
            } catch { self.showError(error.localizedDescription) }
        }
    }

    // MARK: Application chooser

    private func chooseApplication() {
        guard store != nil, chooser == nil, let window = context.host?.window else { return }
        endEditing(commit: true)
        let panel = NSOpenPanel()
        panel.title = L10n.text("Choose an application", "选择应用")
        panel.prompt = L10n.text("Choose", "选择")
        panel.canChooseFiles = true; panel.canChooseDirectories = false
        panel.treatsFilePackagesAsDirectories = false; panel.allowsMultipleSelection = false
        panel.allowsOtherFileTypes = false; panel.resolvesAliases = true
        if #available(macOS 11.0, *) { panel.allowedContentTypes = [.applicationBundle] } else { panel.allowedFileTypes = ["app"] }
        panel.directoryURL = URL(fileURLWithPath: "/Applications", isDirectory: true)
        panel.level = NSWindow.Level(rawValue: max(panel.level.rawValue, window.level.rawValue + 1))
        dialogGeneration += 1
        let token = dialogGeneration
        chooser = panel
        context.onInputStateChange?()
        panel.beginSheetModal(for: window) { [weak self, weak panel] response in
            guard let self, self.dialogGeneration == token else { return }
            self.chooser = nil
            self.context.onInputStateChange?()
            guard self.isActive, let host = self.context.host, host.window != nil else { return }
            host.window?.makeKeyAndOrderFront(nil)
            if response == .OK, let url = panel?.url { self.importURLs([url]) }
            host.window?.makeFirstResponder(host)
        }
    }

    private func closeChooser() {
        dialogGeneration += 1
        let panel = chooser; chooser = nil; panel?.cancel(nil)
    }

    /// ⌘V adds an application copied in Finder.
    override func handleKey(_ event: NSEvent) -> Bool {
        guard event.modifierFlags.intersection([.command, .control, .option, .shift]) == [.command],
              event.charactersIgnoringModifiers?.lowercased() == "v", chooser == nil else { return false }
        let urls = Self.applicationURLs(.general)
        guard !urls.isEmpty else { return false }
        importURLs(urls)
        return true
    }

    static func applicationURLs(_ pasteboard: NSPasteboard) -> [URL] {
        (pasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? [])
            .filter { $0.pathExtension.lowercased() == "app" }
    }

    // MARK: Icons

    /// The tile of `preset` for `item` (its row tile and each Choose icon
    /// choice): the app's own icon on its darkened average colour, or the
    /// preset glyph, dark on the theme accent. Rasters come from the caches.
    private func artwork(_ preset: AppShortcutIcon, for item: AppShortcut) -> HUDMenu.Icon {
        let bounds = CGRect(x: 0, y: 0, width: 40, height: 40), box = bounds.insetBy(dx: 5, dy: 5)
        guard preset != .original || icon(item) == nil else {
            return HUDMenu.Icon(image: icon(item), size: bounds.size, frame: box, fill: originalColor(item))
        }
        let tint = preset == .original ? NSColor(white: 0.93, alpha: 1) : NSColor(cgColor: HUDColor.nearBlack) ?? .black
        let placed = AppShortcutArtwork.placedImage(for: preset, in: box, color: tint)
        return HUDMenu.Icon(image: placed?.image, size: bounds.size, frame: placed?.frame ?? box,
                            fill: preset == .original ? originalColor(item) : HUDColor.yellow)
    }

    private func icon(_ item: AppShortcut) -> CGImage? {
        if let cached = icons[item.id] { return cached }
        var rect = CGRect(x: 0, y: 0, width: 96, height: 96)
        let image = store?.icon(for: item.id)?.cgImage(forProposedRect: &rect, context: nil, hints: nil)
        icons.updateValue(image, forKey: item.id)
        return image
    }

    /// Original icons sit on their own average colour (darkened so the icon reads).
    private func originalColor(_ item: AppShortcut) -> CGColor {
        if let cached = tileColors[item.id] { return cached }
        let color = Self.averageColor(icon(item)) ?? HUDColor.rgb(0x2A2A2A)
        tileColors[item.id] = color
        return color
    }

    static func averageColor(_ image: CGImage?) -> CGColor? {
        guard let image, let space = CGColorSpace(name: CGColorSpace.sRGB),
              let ctx = CGContext(data: nil, width: 8, height: 8, bitsPerComponent: 8, bytesPerRow: 32, space: space,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.interpolationQuality = .medium
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: 8, height: 8))
        guard let data = ctx.data?.assumingMemoryBound(to: UInt8.self) else { return nil }
        var r = 0.0, g = 0.0, b = 0.0, a = 0.0
        for index in stride(from: 0, to: 8 * 8 * 4, by: 4) {
            r += Double(data[index]); g += Double(data[index + 1]); b += Double(data[index + 2]); a += Double(data[index + 3])
        }
        guard a > 0 else { return nil }
        // Premultiplied sums divided by coverage; then darkened to at most 55 % luminance.
        var rgb = [r / a, g / a, b / a].map { $0 * 0.72 }
        let luminance = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]
        if luminance > 0.55 { rgb = rgb.map { $0 * 0.55 / luminance } }
        return CGColor(srgbRed: CGFloat(rgb[0]), green: CGFloat(rgb[1]), blue: CGFloat(rgb[2]), alpha: 1)
    }
}

/// The 40 × 40 app icon tile (spec §3.23): the shortcut's artwork with an
/// inset ring rgba(0,0,0,.25); it opens Choose icon.
final class AppShortcutTile: HUDControl {
    private let brighten = HUDLayer.plain("apps.tile.brighten")

    init(id: String, label: String, artwork: HUDMenu.Icon, env: HUDDesignEnvironment) {
        super.init(id: id, label: label, env: env)
        let bounds = CGRect(origin: .zero, size: artwork.size)
        HUDMotion.perform {
            layer.bounds = bounds
            layer.backgroundColor = artwork.fill
            let picture = HUDLayer.plain("apps.tile.icon")
            picture.frame = artwork.frame ?? bounds.insetBy(dx: 5, dy: 5)
            picture.contents = artwork.image; picture.contentsGravity = .resizeAspect
            let ring = HUDLayer.plain("apps.tile.ring")
            ring.frame = bounds; ring.borderWidth = 1; ring.borderColor = HUDColor.black(0.25)
            brighten.frame = bounds; brighten.backgroundColor = HUDColor.white(0.07); brighten.opacity = 0
            [picture, brighten, ring].forEach(layer.addSublayer)
        }
        setTarget(bounds)
    }

    override func hoverChanged() { applyHoverMotion(lift: 1, pressScale: 0.965, brighten: brighten) }
    override func pressChanged() { applyHoverMotion(lift: 1, pressScale: 0.965, brighten: brighten) }
    override var accessibilityRole: HUDAccessibilityItem.Role? { .popUp(label) }
}
