import AppKit
import QuartzCore
import UniformTypeIdentifiers

/// One accessible shelf action (verification reveals the first tile with it).
struct ShelfCanvasAction {
    let id: String
    let label: String
    let rect: CGRect
}

/// What the properties card reads from a reference on a utility queue: the
/// file's own dates and, for a folder, its direct children (never recursive).
struct ShelfFileProperties: Equatable {
    var path: String
    var created: Date?
    var modified: Date?
    var itemCount: Int?

    /// Folders larger than this show no count.
    static let itemBound = 10_000

    static func read(_ url: URL, directory: Bool) -> ShelfFileProperties {
        let values = try? url.resourceValues(forKeys: [.creationDateKey, .contentModificationDateKey])
        var result = ShelfFileProperties(path: url.path, created: values?.creationDate, modified: values?.contentModificationDate)
        if directory, let children = FileManager.default.enumerator(at: url, includingPropertiesForKeys: [],
                                                                     options: [.skipsSubdirectoryDescendants, .skipsPackageDescendants, .skipsHiddenFiles]) {
            var count = 0
            while children.nextObject() != nil {
                count += 1
                if count > itemBound { return result }
            }
            result.itemCount = count
        }
        return result
    }
}

/// File Shelf (spec §3.7): bookmarked references as archive tiles (§2.15) and
/// the properties card (§2.16) while exactly one tile is selected (Windows A1,
/// in place of Quick Look). Only bookmarks and metadata belong to the shelf;
/// nothing here copies, moves or deletes Finder items. The file chooser,
/// opening, Finder and the drag session belong to HUDFileShelfInteraction.
final class FileShelfCanvas: HUDDesignCanvas {
    var onItemsAdded: (([String]) -> Void)?
    var onItemRemoved: ((String) -> Void)?
    var onShelfCleared: ((Int) -> Void)?
    var onChooseFiles: (() -> Void)?
    /// Opens / reveals a reference; false when it could not (the bridge reports why).
    var onOpen: ((UUID) -> Bool)?
    var onReveal: ((UUID) -> Bool)?
    /// Starts the native drag of `ids` from the current pointer event.
    var onBeginDrag: ((_ ids: [UUID], _ primary: UUID, _ event: NSEvent) -> Void)?
    /// The shelf stopped taking input (the bridge closes its chooser).
    var onDeactivate: (() -> Void)?

    private let store: FileShelfStore?
    private var items: [ShelfItem]
    private(set) var selectedIDs: Set<UUID> = []
    /// The keyboard's tile, and the start of a Shift-arrow range.
    private(set) var focusID: UUID?
    private var anchorID: UUID?
    private var pendingError: String?
    private var pendingReveal: Set<UUID> = []
    private var dropTarget = false
    private var clearing = false
    private var properties: [UUID: ShelfFileProperties] = [:]
    private var propertiesRequest: UUID?
    private let propertiesQueue = DispatchQueue(label: "EndfieldHUD.FileShelf.properties", qos: .userInitiated)

    // The current page.
    private weak var grid: HUDScrollArea?
    private var layout: GridLayout?
    private var tiles: [UUID: ShelfTile] = [:]
    private weak var card: CALayer?
    private weak var dropRing: CALayer?
    private var cardBinder: (() -> Void)?
    /// What the page's layout depends on; other selection changes update in place.
    private struct Structure: Equatable { let items: [ShelfItem], card: UUID? }
    private var renderedStructure: Structure?
    private var renderedSelection: Set<UUID> = []
    private var renderedCard: UUID?

    init(context: HUDDesignContext, store: FileShelfStore?, error: String? = nil) {
        self.store = store
        items = store?.items ?? []
        pendingError = error
        super.init(module: .fileShelf, context: context)
    }

    // MARK: Host compatibility

    var itemCount: Int { items.count }
    var scrollOffset: CGFloat { grid?.offset ?? 0 }
    var accessibleActions: [ShelfCanvasAction] {
        items.map { ShelfCanvasAction(id: "shelf:\($0.id.uuidString):select", label: $0.name, rect: tileRect(for: $0.id) ?? .zero) }
    }

    // MARK: Lifecycle

    /// Bookmarks resolve before the module is revealed, so activation never
    /// replaces tiles that just finished their entrance (no polling).
    override func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        refreshStore()
        if renderedLanguage != nil, structure != renderedStructure { rerender() }
        return super.makeContent(for: module, style: style)
    }

    override func didActivate() {
        if structure != renderedStructure { rerender() }
        if let error = pendingError { pendingError = nil; showError(error) }
    }

    override func didDeactivate() {
        if dropTarget { setDropTarget(false) }
        onDeactivate?()
    }

    override func playEntrance(from offset: CGPoint) {
        super.playEntrance(from: offset)
        if let card { HUDMotion.enter(card, from: CGPoint(x: 24, y: 0), duration: 0.3, env: env) }
    }

    override func recordingModeChanged() {
        super.recordingModeChanged()
        if context.recordingMode, root.superlayer != nil { HUDRedaction.playEntrance(in: [page.layer], env: env) }
    }

    private func refreshStore() {
        guard let store else { return }
        do { try store.refresh() } catch { pendingError = error.localizedDescription }
        properties = [:]
        reloadItems()
    }

    private func reloadItems() {
        items = store?.items ?? []
        let ids = Set(items.map(\.id))
        selectedIDs.formIntersection(ids)
        if let id = focusID, !ids.contains(id) { focusID = nil }
        if let id = anchorID, !ids.contains(id) { anchorID = nil }
    }

    private var structure: Structure { Structure(items: items, card: cardID) }
    private var cardID: UUID? { selectedIDs.count == 1 ? selectedIDs.first : nil }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        renderedStructure = structure
        tiles = [:]; cardBinder = nil; card = nil; grid = nil
        let previousCard = renderedCard
        renderedCard = nil
        let add = HUDButton(id: "shelf.add", title: L10n.text("Add", "添加"), icon: .plus, kind: .primary, size: .small, env: env)
        add.place(at: .zero)
        add.onActivate = { [weak self] in self?.chooseFiles() }
        let clear = HUDButton(id: "shelf.clear", title: L10n.text("Clear", "清空"), icon: .trash, kind: .secondary, size: .small, env: env)
        clear.place(at: .zero)
        clear.isEnabled = !items.isEmpty
        clear.onActivate = { [weak self] in self?.confirmClear() }
        let count = HUDFormat.pad2(items.count)
        header.configure(crumbs: ["FILE SHELF"], code: "MOD-02", title: HUDModuleTitle("File Shelf", "文件暂存架"),
                         counter: L10n.text("\(count) FILES", "\(count) 个文件"), showsBack: false, actions: [add, clear])
        let top = HUDMetric.headerHeight
        let body: CGRect
        if items.isEmpty {
            layout = nil
            let empty = HUDEmptyState.bilingual("SHELF EMPTY", "拖入文件以暂存", env: env)
            HUDMotion.perform { empty.frame.origin = CGPoint(x: 0, y: top); page.layer.addSublayer(empty) }
            page.stagger(empty)
            body = empty.frame
        } else {
            let item = cardID.flatMap { id in items.first { $0.id == id } }
            let width = HUDMetric.box.width - (item == nil ? 0 : 186 + 12)
            let grid = GridLayout(width: width, columns: item == nil ? 4 : 3, count: items.count)
            layout = grid
            let area = page.scrollArea(id: "shelf.grid", frame: CGRect(x: 0, y: top, width: width, height: grid.height))
            self.grid = area
            area.setContentHeight(grid.contentHeight)
            page.restoreOffset(of: area)
            syncTiles(page, offset: area.offset, routing: false)
            body = area.layer.frame
            if let item { buildCard(item, page: page, top: top, replaying: previousCard != item.id) }
        }
        let ring = HUDLayer.plain("shelf.drop")
        HUDMotion.perform {
            ring.frame = body; ring.borderWidth = 1; ring.borderColor = HUDColor.accentInk
            ring.backgroundColor = HUDColor.yellow.copy(alpha: 0.05); ring.opacity = dropTarget ? 1 : 0
            page.layer.addSublayer(ring)
        }
        dropRing = ring
        renderedSelection = selectedIDs
    }

    /// The archive grid (spec §3.7): 4 columns (3 beside the card), gap 12,
    /// padding 5 for the brackets, at most 420 tall.
    struct GridLayout {
        static let gap: CGFloat = 12, padding: CGFloat = 5, tileHeight: CGFloat = 124
        let columns: Int, tileWidth: CGFloat, height: CGFloat, contentHeight: CGFloat

        init(width: CGFloat, columns: Int, count: Int) {
            self.columns = columns
            let rows = (count + columns - 1) / columns
            contentHeight = Self.padding * 2 + CGFloat(rows) * Self.tileHeight + CGFloat(max(0, rows - 1)) * Self.gap
            height = min(420, contentHeight)
            // The 4 px scroll bar takes room only when the grid scrolls.
            let inner = width - Self.padding * 2 - (contentHeight > height ? 4 : 0)
            tileWidth = ((inner - Self.gap * CGFloat(columns - 1)) / CGFloat(columns)).rounded(.down)
        }

        var pitch: CGFloat { Self.tileHeight + Self.gap }
        func frame(_ index: Int) -> CGRect {
            CGRect(x: Self.padding + CGFloat(index % columns) * (tileWidth + Self.gap), y: Self.padding + CGFloat(index / columns) * pitch,
                   width: tileWidth, height: Self.tileHeight)
        }
        /// Tiles worth building: the visible rows and one either side.
        func indices(offset: CGFloat, count: Int) -> Range<Int> {
            let first = max(0, Int(floor((offset - Self.padding) / pitch)) - 1)
            let last = Int(ceil((offset + height - Self.padding) / pitch)) + 1
            return min(count, first * columns)..<min(count, max(first, last) * columns)
        }
    }

    /// Builds the tiles in view and drops the ones that left it; the hover
    /// pass and accessibility only ever see a few rows.
    private func syncTiles(_ page: HUDDesignPage, offset: CGFloat, routing: Bool) {
        guard let grid, let layout else { return }
        let wanted = layout.indices(offset: offset, count: items.count)
        let wantedIDs = Set(items[wanted].map(\.id))
        var changed = false
        for (id, tile) in tiles where !wantedIDs.contains(id) {
            tile.clearInteractionState()
            HUDMotion.perform { tile.layer.removeFromSuperlayer() }
            page.remove(tile); tiles[id] = nil; changed = true
        }
        for index in wanted where tiles[items[index].id] == nil {
            let item = items[index]
            let tile = ShelfTile(item: item, number: index + 1, width: layout.tileWidth, redacted: context.recordingMode, canvas: self, env: env)
            let selected = selectedIDs.contains(item.id)
            tile.tile.setSelected(selected && renderedSelection.contains(item.id), animated: false)
            HUDMotion.perform { tile.layer.frame = layout.frame(index); grid.content.addSublayer(tile.layer) }
            if selected { tile.tile.setSelected(true) }
            page.add(tile)
            if !routing { page.stagger(tile.layer) }
            tiles[item.id] = tile; changed = true
        }
        if routing, changed { refreshRouter(); context.onInputStateChange?() }
    }

    /// Box rect of a built tile (drag images, verification).
    func tileRect(for id: UUID) -> CGRect? {
        guard let tile = tiles[id], tile.layer.superlayer != nil else { return nil }
        return tile.layer.convert(tile.layer.bounds, to: root)
    }

    override func handleScroll(at point: CGPoint, delta: CGPoint, event: NSEvent?) -> Bool {
        guard let grid, grid.layer.isVisible(in: root), grid.layer.bounds.contains(grid.layer.convert(point, from: root)) else { return false }
        if grid.scroll(by: delta.y) { syncTiles(page, offset: grid.offset, routing: true) }
        return true
    }

    private func scrollGrid(to offset: CGFloat) {
        guard let grid else { return }
        grid.setOffset(offset)
        syncTiles(page, offset: grid.offset, routing: true)
    }

    private func revealTile(_ id: UUID) {
        guard let grid, let layout, let index = items.firstIndex(where: { $0.id == id }) else { return }
        let frame = layout.frame(index).insetBy(dx: 0, dy: -GridLayout.padding)
        grid.reveal(frame)
        syncTiles(page, offset: grid.offset, routing: true)
    }

    // MARK: Properties card (spec §2.16)

    /// - replaying: the card enters from the right on every selection change.
    private func buildCard(_ item: ShelfItem, page: HUDDesignPage, top: CGFloat, replaying: Bool) {
        let width: CGFloat = 186, inner: CGFloat = 186 - 24
        let x = HUDMetric.box.width - width
        let redacted = context.recordingMode
        let unavailable = item.availabilityError != nil
        let card = HUDLayer.plain("shelf.props")
        let plate = HUDLayer.plain("props.plate")
        let preview = HUDLayer.plain("props.preview")
        let doc = HUDArchiveTile.docGlyph(ext: Self.tileKind(item), label: Self.extensionLabel(item), env: env)
        // h4: 700 13 px / 1.25, breaks anywhere.
        let nameStyle = HUDTextStyle(size: 13, weight: .bold, color: HUDColor.nearBlack, lineHeight: 1.25)
        let name = HUDTextLayer(item.name, style: nameStyle)
        name.maximumLines = 3
        let nameLines = redacted ? 1 : max(1, HUDText.lines(HUDText.attributed(name.runs, language: HUDTextLanguage.tag()), width: inner,
                                                              maximumLines: 3, truncate: true).count)
        var y = 12 + 84 + 10 + CGFloat(nameLines) * 16.25 + 8
        // dl: auto + 1fr columns, gap 5 × 8; dt 700 9 px / 1.6 #666 +.08em, dd 400 11 px / 1.3.
        let termStyle = HUDTextStyle(size: 9, weight: .bold, color: HUDColor.rgb(0x666666), lineHeight: 1.6, tracking: 0.08)
        let valueStyle = HUDTextStyle(size: 11, weight: .regular, color: HUDColor.nearBlack, lineHeight: 1.3)
        let rows: [(term: String, value: String, private: Bool, live: ((ShelfFileProperties?) -> String)?)] = [
            (L10n.text("KIND", "类型"), Self.kindText(item), false, nil),
            item.isDirectory && !Self.isPackage(item)
                ? (L10n.text("ITEMS", "项目"), "", false, { $0?.itemCount.map { L10n.text("\($0) items", "\($0) 项") } ?? "—" })
                : (L10n.text("SIZE", "大小"), item.sizeLabel ?? "—", false, nil),
            (L10n.text("WHERE", "位置"), Self.location(item), true, nil),
            (L10n.text("CREATED", "创建"), "", false, { $0?.created.map { HUDFormat.dotted($0, time: true) } ?? "—" }),
            (L10n.text("MODIFIED", "修改"), "", false, { $0?.modified.map { HUDFormat.dotted($0, time: true) } ?? "—" })
        ]
        let termWidth = ceil(rows.map { HUDText.width($0.term, termStyle) }.max() ?? 0)
        let valueX = 12 + termWidth + 8, valueWidth = inner - termWidth - 8
        var live: [(HUDTextLayer, (ShelfFileProperties?) -> String)] = []
        HUDMotion.perform {
            preview.frame = CGRect(x: 12, y: 12, width: inner, height: 84)
            preview.showArtwork(HUDArtwork.plate([HUDColor.rgb(0x8A8885), HUDColor.rgb(0xC9C9C7)], size: preview.bounds.size, cut: 0,
                                                 scale: env.renderScale), scale: env.renderScale)
            doc.position = CGPoint(x: inner / 2, y: 42)
            preview.addSublayer(doc)
            card.addSublayer(preview)
            let nameLine = CGRect(x: 12, y: 106, width: inner, height: CGFloat(nameLines) * 16.25)
            if redacted { card.addSublayer(HUDRedaction.bar(replacing: nameLine, style: nameStyle, width: 150, env: env)) }
            else { name.lineFrame = nameLine; card.addSublayer(name) }
            for row in rows {
                let term = HUDTextLayer(row.term, style: termStyle)
                term.lineFrame = CGRect(x: 12, y: y, width: termWidth + 1, height: 14.4)
                card.addSublayer(term)
                let value = HUDTextLayer(row.live?(properties[item.id]) ?? row.value, style: valueStyle)
                value.maximumLines = row.private ? 3 : 1
                let lines = row.private && !redacted
                    ? max(1, HUDText.lines(HUDText.attributed(value.runs, language: HUDTextLanguage.tag()), width: valueWidth, maximumLines: 3, truncate: true).count)
                    : 1
                let line = CGRect(x: valueX, y: y, width: valueWidth, height: CGFloat(lines) * 14.3)
                if row.private, redacted { card.addSublayer(HUDRedaction.bar(replacing: line, style: valueStyle, width: 96, env: env)) }
                else { value.lineFrame = line; card.addSublayer(value) }
                if let update = row.live { live.append((value, update)) }
                y += max(14.4, line.height) + 5
            }
        }
        y += 10 - 5
        let open = HUDButton(id: "props.open", title: L10n.text("Open", "打开"), kind: .primary, size: .small, env: env)
        open.place(at: CGPoint(x: 12, y: y))
        open.isEnabled = !unavailable
        open.onActivate = { [weak self] in self?.open(item.id) }
        let reveal = HUDIconButton(id: "props.reveal", label: L10n.text("Reveal in Finder", "在访达中显示"), icon: .folder, env: env)
        let remove = HUDIconButton(id: "props.remove", label: L10n.text("Remove from shelf", "从暂存架移除"), icon: .x, env: env)
        reveal.isEnabled = !unavailable
        reveal.onActivate = { [weak self] in self?.reveal(item.id) }
        remove.onActivate = { [weak self] in self?.remove([item.id]) }
        let height = y + 30 + 12
        HUDMotion.perform {
            reveal.layer.frame.origin = CGPoint(x: open.layer.frame.maxX + 6, y: y)
            remove.layer.frame.origin = CGPoint(x: reveal.layer.frame.maxX + 6, y: y)
            card.frame = CGRect(x: x, y: top, width: width, height: height)
            plate.frame = card.bounds
            plate.showArtwork(HUDArtwork.plate([HUDColor.plateHigh, HUDColor.rgb(0xCFD0CB)], size: card.bounds.size, cut: 12,
                                               scale: env.renderScale), scale: env.renderScale)
            card.insertSublayer(plate, at: 0)
            [open.layer, reveal.layer, remove.layer].forEach(card.addSublayer)
            page.layer.addSublayer(card)
        }
        env.applyScale(to: card)
        page.add([open, reveal, remove])
        self.card = card
        cardBinder = { [weak self] in
            guard let self else { return }
            let value = self.properties[item.id]
            HUDMotion.perform { for (layer, text) in live { layer.text = text(value) } }
        }
        if replaying, root.superlayer != nil { HUDMotion.enter(card, from: CGPoint(x: 24, y: 0), duration: 0.3, env: env) }
        renderedCard = item.id
        loadProperties(item)
    }

    private func loadProperties(_ item: ShelfItem) {
        guard item.availabilityError == nil, properties[item.id]?.path != item.lastKnownPath, propertiesRequest != item.id,
              let access = try? store?.access(id: item.id) else { return }
        propertiesRequest = item.id
        let id = item.id, directory = item.isDirectory && !Self.isPackage(item)
        propertiesQueue.async {
            let result = ShelfFileProperties.read(access.url, directory: directory)
            access.close()
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                if self.propertiesRequest == id { self.propertiesRequest = nil }
                guard self.items.contains(where: { $0.id == id }) else { return }
                self.properties[id] = result
                if self.cardID == id { self.cardBinder?() }
            }
        }
    }

    // MARK: Selection (spec §3.7)

    /// A plain click selects only that tile (again: none); Cmd, Ctrl or Shift
    /// toggles it in a multi-selection; a double-click keeps it selected.
    func click(_ id: UUID, modifiers: NSEvent.ModifierFlags = [], clickCount: Int = 1) {
        guard !clearing, items.contains(where: { $0.id == id }) else { return }
        if !modifiers.intersection([.command, .control, .shift]).isEmpty {
            if selectedIDs.contains(id) { selectedIDs.remove(id) } else { selectedIDs.insert(id) }
        } else if clickCount >= 2 {
            selectedIDs = [id]
        } else {
            selectedIDs = selectedIDs == [id] ? [] : [id]
        }
        focusID = id; anchorID = id
        selectionChanged()
    }

    /// A press on empty space in the body (around or between the tiles, the
    /// empty grid rows) clears the selection; the properties card and the
    /// header keep theirs. Focus and the Shift anchor stay for the keyboard.
    override func handlePointerDown(at point: CGPoint, event: NSEvent?) -> Bool {
        guard !selectedIDs.isEmpty, !clearing, point.y >= HUDMetric.headerHeight,
              CGRect(origin: .zero, size: HUDMetric.box).contains(point), card.map({ !$0.frame.contains(point) }) ?? true else { return false }
        select([])
        return true
    }

    func select(_ ids: Set<UUID>) {
        selectedIDs = ids.intersection(items.map(\.id))
        selectionChanged()
    }

    private func selectionChanged() {
        if structure != renderedStructure { rerender() }
        else {
            for (id, tile) in tiles { tile.tile.setSelected(selectedIDs.contains(id)) }
            renderedSelection = selectedIDs
        }
        context.onInputStateChange?()
    }

    /// Arrow keys move through the grid; Shift extends from the anchor.
    func moveSelection(_ step: Int, extending: Bool = false) {
        guard !items.isEmpty, !clearing else { return }
        let current = focusID.flatMap { id in items.firstIndex { $0.id == id } }
        let index = min(items.count - 1, max(0, current.map { $0 + step } ?? (step < 0 ? items.count - 1 : 0)))
        let id = items[index].id
        focusID = id
        if extending, let anchor = anchorID.flatMap({ id in items.firstIndex { $0.id == id } }) {
            selectedIDs = Set(items[min(anchor, index)...max(anchor, index)].map(\.id))
        } else {
            selectedIDs = [id]; anchorID = id
        }
        selectionChanged()
        revealTile(id)
    }

    /// The drag carries the selection when it starts on a selected tile;
    /// unavailable references can be selected but never exported.
    func dragSelection(primaryID: UUID) -> [UUID] {
        let wanted = selectedIDs.contains(primaryID) ? selectedIDs : [primaryID]
        return items.filter { wanted.contains($0.id) && $0.availabilityError == nil }.map(\.id)
    }

    fileprivate func beginDrag(from id: UUID) {
        guard !clearing, let event = NSApp.currentEvent, [.leftMouseDragged, .leftMouseDown].contains(event.type) else { return }
        let ids = dragSelection(primaryID: id)
        if !ids.isEmpty { onBeginDrag?(ids, id, event) }
    }

    // MARK: Actions

    private func chooseFiles() {
        guard store != nil else { reportUnavailable(); return }
        onChooseFiles?()
    }

    @discardableResult
    func importURLs(_ urls: [URL]) -> Bool {
        guard let store else { reportUnavailable(); return false }
        guard !urls.isEmpty else { return false }
        let previous = Set(store.items.map(\.id))
        do {
            try store.add(urls: urls)
            let added = store.items.filter { !previous.contains($0.id) }
            reloadItems()
            guard !added.isEmpty else { return true } // Existing references are valid drops, too.
            onItemsAdded?(added.map(\.name))
            reveal(Set(added.map(\.id)), animated: true)
            return true
        } catch {
            reloadItems()
            if renderedLanguage != nil, structure != renderedStructure { rerender() }
            showError(error.localizedDescription)
            return false
        }
    }

    /// File URLs from a drop or the clipboard (Cmd-V).
    @discardableResult
    func importFiles(from pasteboard: NSPasteboard) -> Bool {
        let urls = pasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
        return !urls.isEmpty && importURLs(urls)
    }

    /// A menu-bar drop added these while the HUD was open: show them when the
    /// shelf is next on screen.
    func revealItems(_ ids: Set<UUID>) {
        reloadItems()
        reveal(ids.intersection(items.map(\.id)), animated: false)
    }

    /// Add (spec §3.7): scrolls the grid to the bottom; new tiles enter from below.
    private func reveal(_ ids: Set<UUID>, animated: Bool) {
        guard !ids.isEmpty else { return }
        guard renderedLanguage != nil else { pendingReveal.formUnion(ids); return }
        if structure != renderedStructure { rerender() }
        scrollGrid(to: .greatestFiniteMagnitude)
        guard animated, isActive, root.superlayer != nil else { return }
        for id in ids { if let tile = tiles[id] { HUDMotion.enter(tile.layer, from: CGPoint(x: 0, y: 16), env: env) } }
    }

    override func rerender() {
        super.rerender()
        if !pendingReveal.isEmpty { pendingReveal = []; scrollGrid(to: .greatestFiniteMagnitude) }
    }

    func open(_ id: UUID) {
        guard let item = items.first(where: { $0.id == id }), item.availabilityError == nil, onOpen?(id) == true else { return }
        toast(icon: .ext, "Opened", "已打开", detail: context.recordingMode ? nil : item.name)
    }

    func reveal(_ id: UUID) {
        guard let item = items.first(where: { $0.id == id }), item.availabilityError == nil, onReveal?(id) == true else { return }
        toast(icon: .folder, "Revealed in Finder", "已在访达中显示", detail: context.recordingMode ? nil : Self.location(item))
    }

    /// Removes references only (spec: removes at once and clears the selection).
    func remove(_ ids: [UUID]) {
        guard let store else { reportUnavailable(); return }
        guard !ids.isEmpty, !clearing else { return }
        do {
            for id in ids {
                let name = store.items.first { $0.id == id }?.name
                try store.remove(id: id)
                if let name { onItemRemoved?(name) }
            }
            selectedIDs = []
            reloadItems(); rerender()
            toast(icon: .x, "Removed from shelf", "从暂存架上移除")
        } catch {
            reloadItems(); rerender()
            showError(error.localizedDescription)
        }
    }

    private func confirmClear() {
        guard !items.isEmpty, !clearing else { return }
        let count = items.count
        confirm(HUDConfirmRequest(id: "shelf.clear", title: L10n.text("Remove \(count) files from the shelf?", "从暂存架移除 \(count) 个文件？"),
                                  message: L10n.text("Original files stay where they are.", "原文件保持不变。"),
                                  confirmTitle: L10n.text("Remove all", "全部移除"), danger: true, icon: .trash)) { [weak self] in
            self?.clearShelf()
        }
    }

    /// Every tile plays `gone` 30 ms apart; the shelf empties after 380 ms.
    func clearShelf() {
        guard let store, !items.isEmpty, !clearing else { return }
        clearing = true
        let shown = tiles.values.sorted { $0.number < $1.number }
        for (index, tile) in shown.enumerated() {
            let delay = Double(index) * 0.03
            HUDMotion.animate(tile.layer, "opacity", to: Float(0), duration: 0.28, delay: delay, env: env)
            HUDMotion.animate(tile.layer, "transform.scale", to: 0.85, duration: 0.28, delay: delay, env: env)
        }
        let finish = { [weak self] in
            guard let self else { return }
            self.clearing = false
            let count = store.items.count
            do { try store.clear() } catch { self.showError(error.localizedDescription) }
            self.selectedIDs = []
            self.reloadItems()
            if self.renderedLanguage != nil { self.rerender() }
            guard store.items.isEmpty, count > 0 else { return }
            self.onShelfCleared?(count)
            if self.isActive { self.toast(icon: .check, "Shelf cleared", "暂存架已清空") }
        }
        if env.reducesMotion || shown.isEmpty { finish() }
        else { DispatchQueue.main.asyncAfter(deadline: .now() + 0.38, execute: finish) }
    }

    /// Incoming file drag feedback over the grid (or the empty state).
    func setDropTarget(_ value: Bool) {
        guard dropTarget != value else { return }
        dropTarget = value
        if let dropRing { HUDMotion.animate(dropRing, "opacity", to: Float(value ? 1 : 0), duration: 0.14, timing: HUDEasing.ease, env: env) }
    }

    func showError(_ message: String) {
        guard renderedLanguage != nil else { pendingError = message; return }
        toast(icon: .warn, "Notice", "提示", detail: message)
    }

    private func reportUnavailable() { showError(L10n.text("File shelf storage is unavailable.", "文件暂存架存储不可用。")) }

    /// Accessibility and verification actions: `shelf:add`, `shelf:clear`,
    /// `shelf:<id>:select|open|reveal|remove`.
    func perform(actionID value: String) {
        switch value {
        case "shelf:add": chooseFiles()
        case "shelf:clear": confirmClear()
        default:
            let parts = value.split(separator: ":")
            guard parts.count == 3, parts[0] == "shelf", let id = UUID(uuidString: String(parts[1])),
                  items.contains(where: { $0.id == id }) else { return }
            switch parts[2] {
            case "select": click(id)
            case "open": open(id)
            case "reveal": reveal(id)
            case "remove": remove([id])
            default: break
            }
        }
    }

    // MARK: Keyboard and context menu

    override func handleKey(_ event: NSEvent) -> Bool {
        let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
        if flags.isEmpty || flags == [.shift] {
            let columns = layout?.columns ?? 4
            let step: Int?
            switch event.keyCode {
            case 123: step = -1
            case 124: step = 1
            case 125: step = columns
            case 126: step = -columns
            default: step = nil
            }
            if let step { moveSelection(step, extending: flags == [.shift]); return true }
        }
        if flags.isEmpty {
            let page = (grid?.layer.bounds.height ?? 0) - 40
            switch event.keyCode {
            case 121: scrollGrid(to: scrollOffset + page); return true
            case 116: scrollGrid(to: scrollOffset - page); return true
            case 115: scrollGrid(to: 0); return true
            case 119: scrollGrid(to: .greatestFiniteMagnitude); return true
            case 49: toggleProperties(); return true
            case 51, 117: remove(items.filter { selectedIDs.contains($0.id) }.map(\.id)); return true
            default: break
            }
        }
        if flags == [.command] {
            switch event.charactersIgnoringModifiers?.lowercased() {
            case "o": if let id = cardID { open(id) }; return true
            case "r": if let id = cardID { reveal(id) }; return true
            case "v": return importFiles(from: .general)
            default: break
            }
        }
        return false
    }

    /// Space shows the properties card of the keyboard's tile, or closes it.
    func toggleProperties() {
        if selectedIDs.count == 1 { select([]); return }
        let focus = focusID.flatMap { selectedIDs.isEmpty || selectedIDs.contains($0) ? $0 : nil }
        guard let id = focus ?? items.first(where: { selectedIDs.contains($0.id) })?.id ?? items.first?.id else { return }
        focusID = id
        select([id])
    }

    override func handleRightPointerDown(at point: CGPoint) -> Bool {
        guard let tile = tiles.values.first(where: { $0.layer.isVisible(in: root) && $0.layer.isUnclipped(point, in: root)
                && $0.contains($0.layer.convert(point, from: root)) }),
              let item = items.first(where: { $0.id == tile.itemID }) else { return false }
        let available = item.availabilityError == nil
        let menu = [HUDMenu.Item(id: "open", title: L10n.text("Open", "打开"), enabled: available),
                    HUDMenu.Item(id: "reveal", title: L10n.text("Reveal in Finder", "在访达中显示"), enabled: available),
                    HUDMenu.Item(id: "remove", title: L10n.text("Remove from shelf", "从暂存架移除"), danger: true, separatorBefore: true)]
        openMenu(id: "shelf.menu", items: menu, anchorRect: CGRect(x: point.x, y: point.y, width: 1, height: 1), minimumWidth: 170) { [weak self] pick in
            switch pick.id {
            case "open": self?.open(item.id)
            case "reveal": self?.reveal(item.id)
            default: self?.remove([item.id])
            }
        }
        return true
    }

    // MARK: Formatting (spec §2.15, §2.16)

    static func isPackage(_ item: ShelfItem) -> Bool {
        let ext = URL(fileURLWithPath: item.lastKnownPath).pathExtension
        return item.isDirectory && !ext.isEmpty && UTType(filenameExtension: ext)?.conforms(to: .package) == true
    }

    /// The doc glyph's colour: pdf, img, zip, vid or txt.
    static func tileKind(_ item: ShelfItem) -> HUDArchiveTile.Extension {
        let ext = URL(fileURLWithPath: item.lastKnownPath).pathExtension
        guard !item.isDirectory || isPackage(item), !ext.isEmpty, let type = UTType(filenameExtension: ext) else { return .txt }
        if type.conforms(to: .pdf) { return .pdf }
        if type.conforms(to: .image) { return .img }
        if type.conforms(to: .audiovisualContent) { return .vid }
        if type.conforms(to: .archive) || type.conforms(to: .diskImage) { return .zip }
        return .txt
    }

    /// The doc glyph's label: the file's own extension, DIR for a folder.
    static func extensionLabel(_ item: ShelfItem) -> String {
        if item.isDirectory, !isPackage(item) { return "DIR" }
        let ext = URL(fileURLWithPath: item.lastKnownPath).pathExtension
        return ext.isEmpty ? "FILE" : ext.uppercased()
    }

    /// KIND: `PDF file`, `Folder`, or the system's name for a package.
    static func kindText(_ item: ShelfItem) -> String {
        if isPackage(item), !item.typeDescription.isEmpty { return item.typeDescription }
        if item.isDirectory { return L10n.text("Folder", "文件夹") }
        let ext = URL(fileURLWithPath: item.lastKnownPath).pathExtension.uppercased()
        if ext.isEmpty { return item.typeDescription.isEmpty ? L10n.text("File", "文件") : item.typeDescription }
        return L10n.text("\(ext) file", "\(ext) 文件")
    }

    /// WHERE: the containing folder, `~` for the home folder.
    static func location(_ item: ShelfItem) -> String {
        ((item.lastKnownPath as NSString).deletingLastPathComponent as NSString).abbreviatingWithTildeInPath
    }

    static func sizeTag(_ item: ShelfItem) -> String? {
        item.availabilityError != nil ? L10n.text("Unavailable", "不可用") : item.sizeLabel
    }

    // MARK: Testing

    var columnsForTesting: Int? { layout?.columns }
    var builtTileCountForTesting: Int { tiles.count }
    var hasPropertiesCardForTesting: Bool { card != nil }
    var propertiesForTesting: [UUID: ShelfFileProperties] { properties }
}

/// A shelf tile: the shared archive tile plus the press that becomes either
/// a click (selection) or, after 5 px, a drag-out of the selection.
private final class ShelfTile: HUDControl {
    let tile: HUDArchiveTile
    let itemID: UUID
    let number: Int
    private weak var canvas: FileShelfCanvas?
    private var press: (point: CGPoint, modifiers: NSEvent.ModifierFlags, clicks: Int)?
    private var dragging = false

    init(item: ShelfItem, number: Int, width: CGFloat, redacted: Bool, canvas: FileShelfCanvas, env: HUDDesignEnvironment) {
        itemID = item.id; self.number = number; self.canvas = canvas
        tile = HUDArchiveTile(id: "tile:\(number)", number: number, title: item.name, ext: FileShelfCanvas.tileKind(item),
                              extLabel: FileShelfCanvas.extensionLabel(item), size: FileShelfCanvas.sizeTag(item), date: nil,
                              width: width, redacted: redacted, env: env)
        let details = [redacted ? L10n.text("Redacted", "已隐藏") : item.name, FileShelfCanvas.kindText(item), FileShelfCanvas.sizeTag(item)]
        super.init(id: "file:\(number)", label: details.compactMap { $0 }.joined(separator: ", "), env: env)
        HUDMotion.perform { layer.bounds = tile.layer.bounds; tile.layer.frame = layer.bounds; layer.addSublayer(tile.layer) }
    }

    override func pointerDown(at point: CGPoint) -> Bool {
        let event = NSApp.currentEvent
        press = (point, event?.modifierFlags ?? [], max(1, event?.clickCount ?? 1))
        dragging = false
        return true
    }

    override func pointerDragged(to point: CGPoint) {
        guard let press, !dragging, hypot(point.x - press.point.x, point.y - press.point.y) >= 5 else { return }
        dragging = true
        canvas?.beginDrag(from: itemID)
    }

    override func pointerUp(at point: CGPoint) {
        defer { press = nil; dragging = false }
        guard let press, !dragging, contains(point) else { return }
        canvas?.click(itemID, modifiers: press.modifiers, clickCount: press.clicks)
    }

    override func clearInteractionState() { super.clearInteractionState(); press = nil; dragging = false }
    override func activate() { canvas?.click(itemID) }
    override var accessibilityRole: HUDAccessibilityItem.Role? { .tab(selected: tile.isSelected) }
}
